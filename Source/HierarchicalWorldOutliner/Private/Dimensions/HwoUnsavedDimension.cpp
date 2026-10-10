// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "Dimensions/HwoBuiltInDimensions.h"

#include "HwoLocalization.h"

#include "SourceControlHelpers.h"
#include "Styling/AppStyle.h"
#include "UnsavedAssetsTrackerModule.h"

/** 是否保存维度的分组键（稳定常量串，不用本地化文本做键，保证持久化与跨语言稳定） */
namespace HwoUnsavedKeys
{
	// 外部包无未保存修改
	inline const TCHAR* Saved = TEXT("Saved");

	// 外部包有未保存修改
	inline const TCHAR* Unsaved = TEXT("Unsaved");
}

const TCHAR* FHwoUnsavedDimension::GetUnsavedGroupKey()
{
	return HwoUnsavedKeys::Unsaved;
}

FText FHwoUnsavedDimension::GetDisplayName() const
{
	return HWO_TEXT("Dim.Unsaved.Name", "Unsaved", "是否保存");
}

FSlateIcon FHwoUnsavedDimension::GetIcon() const
{
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save");
}

void FHwoUnsavedDimension::GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const
{
	// 判定与原生未保存列同源（SceneOutlinerUnsavedColumn.cpp:96-123）：
	// 包名 → USourceControlHelpers::PackageFilename → FUnsavedAssetsTrackerModule::IsAssetUnsaved
	FString PackageName;
	if (const AActor* Actor = Ctx.Actor)
	{
		// 与原生列读的 FActorTreeItem::GetPackageName 一致（OFPA 外部包，否则关卡包）
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

	const bool bUnsaved = FUnsavedAssetsTrackerModule::Get().IsAssetUnsaved(Filename);
	OutKeys.Add(FHwoGroupKey{ GetDimensionID(), bUnsaved ? HwoUnsavedKeys::Unsaved : HwoUnsavedKeys::Saved });
}

FText FHwoUnsavedDimension::GetGroupDisplayName(const FHwoGroupKey& Key) const
{
	if (Key.Key == HwoGroupKeys::Unassigned)
	{
		return FText::Format(HWO_TEXT("Dim.Unsaved.Unassigned", "(Unassigned {0})", "(未分配 {0})"), GetDisplayName());
	}
	if (Key.Key == HwoUnsavedKeys::Unsaved)
	{
		return HWO_TEXT("Dim.Unsaved.Group.Unsaved", "Unsaved", "未保存");
	}
	return HWO_TEXT("Dim.Unsaved.Group.Saved", "Saved", "已保存");
}

const FSlateBrush* FHwoUnsavedDimension::GetGroupIcon(const FHwoGroupKey& Key, bool bExpanded, bool bHasChildren) const
{
	// 染色画刷副本挂维度成员（懒初始化、复用），返回指针的生命周期随维度实例（由 Stack 持有、随面板存活）
	auto GetTintedGroupBrush = [this](TSharedPtr<FSlateBrush>& InOutBrush, const FSlateColor& Tint) -> const FSlateBrush*
	{
		if (!InOutBrush.IsValid())
		{
			const FSlateBrush* BaseBrush = GetIcon().GetIcon();
			if (!BaseBrush)
			{
				return nullptr;
			}
			InOutBrush = MakeShared<FSlateBrush>(*BaseBrush);
			InOutBrush->TintColor = Tint;
		}
		return InOutBrush.Get();
	};

	// 染色语义：红 = 有未保存修改（警示），绿 = 全部已保存（与状态栏过滤提示色同源，引擎调色板一致）；
	// 其余（含未分配占位键）回退白色默认图标
	if (Key.Key == HwoUnsavedKeys::Unsaved)
	{
		return GetTintedGroupBrush(UnsavedGroupBrush, FAppStyle::Get().GetSlateColor("Colors.AccentRed"));
	}
	if (Key.Key == HwoUnsavedKeys::Saved)
	{
		return GetTintedGroupBrush(SavedGroupBrush, FAppStyle::Get().GetSlateColor("Colors.AccentGreen"));
	}
	return GetIcon().GetIcon();
}
