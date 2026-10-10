// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "Dimensions/HwoBuiltInDimensions.h"

#include "HwoLocalization.h"

#include "GameFramework/Actor.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "SourceControlHelpers.h"
#include "Styling/AppStyle.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"

/** 版本控制维度的分组键（稳定常量串，不用本地化文本做键，保证持久化与跨语言稳定） */
namespace HwoSourceControlKeys
{
	// 状态为空 / IsUnknown（含缓存未命中、Provider 未启用的降级）
	inline const TCHAR* Unknown = TEXT("Unknown");

	// 未受版本控制（IsIgnored 并入此组）
	inline const TCHAR* NotControlled = TEXT("NotControlled");

	// 冲突（IsModified 含冲突，归组优先级冲突必须在前）
	inline const TCHAR* Conflicted = TEXT("Conflicted");

	// 已删除
	inline const TCHAR* Deleted = TEXT("Deleted");

	// 新增（未提交）
	inline const TCHAR* Added = TEXT("Added");

	// 他人签出 / 锁定
	inline const TCHAR* CheckedOutOther = TEXT("CheckedOutOther");

	// 本人签出（SVN 语义下即 svn:lock，常年为空属正常）
	inline const TCHAR* CheckedOut = TEXT("CheckedOut");

	// 本地已修改
	inline const TCHAR* Modified = TEXT("Modified");

	// 有新版可取（IsCurrent 依赖联网 --show-updates；离线时不可信，归"最新"）
	inline const TCHAR* OutOfDate = TEXT("OutOfDate");

	// 最新
	inline const TCHAR* Current = TEXT("Current");
}

FText FHwoSourceControlDimension::GetDisplayName() const
{
	return HWO_TEXT("Dim.SourceControl.Name", "Source Control", "版本控制");
}

FSlateIcon FHwoSourceControlDimension::GetIcon() const
{
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "MainFrame.ConnectToSourceControl");
}

void FHwoSourceControlDimension::GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const
{
	// 包名取法与 Unsaved 维度完全同链（已加载 GetSceneOutlinerItemPackage / 未加载 GetActorPackage）
	FString PackageName;
	if (const AActor* Actor = Ctx.Actor)
	{
		if (const UPackage* Package = Actor->GetSceneOutlinerItemPackage())
		{
			PackageName = Package->GetName();
		}
	}
	else if (Ctx.DescInstance)
	{
		PackageName = Ctx.DescInstance->GetActorPackage().ToString();
	}
	if (PackageName.IsEmpty())
	{
		return; // 包名取不到 → 未分配组
	}

	const FString Filename = USourceControlHelpers::PackageFilename(PackageName);
	if (Filename.IsEmpty())
	{
		return;
	}

	const TCHAR* GroupKey = ComputeGroupKeyForPackageFile(Filename, /*bQueueBackfill*/true);
	KeyCache.FindOrAdd(Filename) = GroupKey;
	OutKeys.Add(FHwoGroupKey{ GetDimensionID(), GroupKey });
}

FText FHwoSourceControlDimension::GetGroupDisplayName(const FHwoGroupKey& Key) const
{
	if (Key.Key == HwoGroupKeys::Unassigned)
	{
		return FText::Format(HWO_TEXT("Dim.SourceControl.Unassigned", "(Unassigned {0})", "(未分配 {0})"), GetDisplayName());
	}

	// 组键 → 显示名映射（与 HwoSourceControlKeys 一一对应）
	struct FGroupNameEntry
	{
		const TCHAR* Key;
		FText DisplayName;
	};
	static const FGroupNameEntry GroupNames[] =
	{
		{ HwoSourceControlKeys::Unknown,         HWO_TEXT("Dim.SourceControl.Group.Unknown", "Unknown", "未知") },
		{ HwoSourceControlKeys::NotControlled,   HWO_TEXT("Dim.SourceControl.Group.NotControlled", "Not Source Controlled", "未受控") },
		{ HwoSourceControlKeys::Conflicted,      HWO_TEXT("Dim.SourceControl.Group.Conflicted", "Conflicted", "冲突") },
		{ HwoSourceControlKeys::Deleted,         HWO_TEXT("Dim.SourceControl.Group.Deleted", "Deleted", "已删除") },
		{ HwoSourceControlKeys::Added,           HWO_TEXT("Dim.SourceControl.Group.Added", "Added", "新增") },
		{ HwoSourceControlKeys::CheckedOutOther, HWO_TEXT("Dim.SourceControl.Group.CheckedOutOther", "Checked Out by Others", "他人签出") },
		{ HwoSourceControlKeys::CheckedOut,      HWO_TEXT("Dim.SourceControl.Group.CheckedOut", "Checked Out", "已签出") },
		{ HwoSourceControlKeys::Modified,        HWO_TEXT("Dim.SourceControl.Group.Modified", "Modified", "已修改") },
		{ HwoSourceControlKeys::OutOfDate,       HWO_TEXT("Dim.SourceControl.Group.OutOfDate", "Out of Date", "过期") },
		{ HwoSourceControlKeys::Current,         HWO_TEXT("Dim.SourceControl.Group.Current", "Current", "最新") },
	};
	for (const FGroupNameEntry& Entry : GroupNames)
	{
		if (Key.Key == Entry.Key)
		{
			return Entry.DisplayName;
		}
	}
	// 键损坏（不在映射表内）回退原串显示
	return FText::FromString(Key.Key);
}

bool FHwoSourceControlDimension::ReevaluateKeysAndDetectChanges()
{
	bool bChanged = false;
	for (TPair<FString, FString>& Pair : KeyCache)
	{
		const TCHAR* NewKey = ComputeGroupKeyForPackageFile(Pair.Key, /*bQueueBackfill*/false);
		if (Pair.Value != NewKey)
		{
			Pair.Value = NewKey;
			bChanged = true;
		}
	}
	return bChanged;
}

void FHwoSourceControlDimension::ClearKeyCache()
{
	KeyCache.Empty();
}

const TCHAR* FHwoSourceControlDimension::ComputeGroupKeyForPackageFile(const FString& Filename, bool bQueueBackfill) const
{
	// Provider 未启用 → 降级方案 a：维度可用，全部落"未知"组
	if (!ISourceControlModule::Get().IsEnabled())
	{
		return HwoSourceControlKeys::Unknown;
	}

	ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
	const FSourceControlStatePtr State = Provider.GetState(Filename, EStateCacheUsage::Use); // 纯缓存读，绝不触网
	if (!State.IsValid() || State->IsUnknown())
	{
		// 缓存未命中：顺手懒回填（模块自带节流批处理与新鲜度去重），本次仍归"未知"
		if (bQueueBackfill)
		{
			ISourceControlModule::Get().QueueStatusUpdate(Filename);
		}
		return HwoSourceControlKeys::Unknown;
	}

	// 优先级链：首个命中即归组（IsModified 含 Conflicted，冲突必须判在前面）
	if (!State->IsSourceControlled() || State->IsIgnored())
	{
		return HwoSourceControlKeys::NotControlled;
	}
	if (State->IsConflicted())
	{
		return HwoSourceControlKeys::Conflicted;
	}
	if (State->IsDeleted())
	{
		return HwoSourceControlKeys::Deleted;
	}
	if (State->IsAdded())
	{
		return HwoSourceControlKeys::Added;
	}
	if (State->IsCheckedOutOther())
	{
		return HwoSourceControlKeys::CheckedOutOther;
	}
	if (State->IsCheckedOut())
	{
		return HwoSourceControlKeys::CheckedOut;
	}
	if (State->IsModified())
	{
		return HwoSourceControlKeys::Modified;
	}
	// 离线时 IsCurrent 不可信（依赖 --show-updates），不区分过期，落"最新"
	if (!State->IsCurrent() && Provider.IsAvailable())
	{
		return HwoSourceControlKeys::OutOfDate;
	}
	return HwoSourceControlKeys::Current;
}
