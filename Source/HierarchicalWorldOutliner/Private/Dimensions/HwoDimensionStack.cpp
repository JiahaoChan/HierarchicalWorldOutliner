// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "Dimensions/HwoDimensionStack.h"

#include "Dimensions/HwoBuiltInDimensions.h"

#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogHwo, Log, All);

/** 持久化位置：Saved/HierarchicalWorldOutliner/OutlinerState.ini 的 [DimensionStack] Dimensions=DataLayer,Class */
namespace HwoStackConfig
{
	// ini 节名
	static const TCHAR* SectionName = TEXT("DimensionStack");

	// ini 键名，值为逗号分隔的维度 ID 列表
	static const TCHAR* KeyName = TEXT("Dimensions");
}

void FHwoDimensionStack::SetDimensions(const TArray<TSharedRef<IHwoDimension>>& InDims)
{
	Dimensions = InDims;
	OnStackChanged.Broadcast();
}

bool FHwoDimensionStack::AddDimension(TSharedRef<IHwoDimension> InDim)
{
	if (Dimensions.Num() >= MaxStackDepth || Contains(InDim->GetDimensionID()))
	{
		return false;
	}
	Dimensions.Add(InDim);
	OnStackChanged.Broadcast();
	return true;
}

bool FHwoDimensionStack::RemoveDimension(FName DimensionID)
{
	const int32 Removed = Dimensions.RemoveAll([DimensionID](const TSharedRef<IHwoDimension>& Dim)
	{
		return Dim->GetDimensionID() == DimensionID;
	});
	if (Removed > 0)
	{
		OnStackChanged.Broadcast();
	}
	return Removed > 0;
}

bool FHwoDimensionStack::MoveDimensionBefore(FName DraggedID, FName TargetID)
{
	if (DraggedID == TargetID)
	{
		return false;
	}

	int32 DraggedIndex = INDEX_NONE;
	int32 TargetIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Dimensions.Num(); ++Index)
	{
		const FName ID = Dimensions[Index]->GetDimensionID();
		if (ID == DraggedID)
		{
			DraggedIndex = Index;
		}
		if (ID == TargetID)
		{
			TargetIndex = Index;
		}
	}
	if (DraggedIndex == INDEX_NONE || TargetIndex == INDEX_NONE)
	{
		return false;
	}

	TSharedRef<IHwoDimension> Dragged = Dimensions[DraggedIndex];
	Dimensions.RemoveAt(DraggedIndex);
	if (DraggedIndex < TargetIndex)
	{
		--TargetIndex;
	}
	Dimensions.Insert(Dragged, TargetIndex);
	OnStackChanged.Broadcast();
	return true;
}

bool FHwoDimensionStack::Contains(FName DimensionID) const
{
	// 只检查真实堆叠的维度（不含 D3 回退），否则空栈时无法添加文件夹维度
	for (const TSharedRef<IHwoDimension>& Dim : Dimensions)
	{
		if (Dim->GetDimensionID() == DimensionID)
		{
			return true;
		}
	}
	return false;
}

void FHwoDimensionStack::ComputeLeafPaths(const FHwoActorContext& Ctx, TArray<TArray<FHwoGroupKey>>& OutPaths) const
{
	OutPaths.Reset();

	TArray<TSharedRef<IHwoDimension>> EffectiveDims;
	GetEffectiveDimensions(EffectiveDims);

	// 每个维度的键集合；空集合补"未分配"键
	TArray<TArray<FHwoGroupKey>> KeysPerDim;
	KeysPerDim.SetNum(EffectiveDims.Num());
	for (int32 DimIndex = 0; DimIndex < EffectiveDims.Num(); ++DimIndex)
	{
		EffectiveDims[DimIndex]->GetGroupKeys(Ctx, KeysPerDim[DimIndex]);

		// 去重：维度数据源可能含重复键（如 Actor Tags），重复键会产生重复路径/重复 ID 导致 TreeItemMap check(false)
		{
			TArray<FHwoGroupKey>& Keys = KeysPerDim[DimIndex];
			TSet<FHwoGroupKey> Seen;
			TArray<FHwoGroupKey> UniqueKeys;
			UniqueKeys.Reserve(Keys.Num());
			for (const FHwoGroupKey& Key : Keys)
			{
				bool bAlreadyInSet = false;
				Seen.Add(Key, &bAlreadyInSet);
				if (!bAlreadyInSet)
				{
					UniqueKeys.Add(Key);
				}
			}
			Keys = MoveTemp(UniqueKeys);
		}

		if (KeysPerDim[DimIndex].IsEmpty())
		{
			KeysPerDim[DimIndex].Add(FHwoGroupKey{ EffectiveDims[DimIndex]->GetDimensionID(), HwoGroupKeys::Unassigned });
		}
	}

	// 把每个键展开成路径段序列（默认一键一段；文件夹维度按 "/" 拆成多段呈现嵌套，见 IHwoDimension::ExpandKeyToPathSegments）
	TArray<TArray<TArray<FHwoGroupKey>>> SegmentsPerDim; // [维度][备选键][段]
	SegmentsPerDim.SetNum(EffectiveDims.Num());
	for (int32 DimIndex = 0; DimIndex < EffectiveDims.Num(); ++DimIndex)
	{
		TArray<TArray<FHwoGroupKey>>& Alternatives = SegmentsPerDim[DimIndex];
		Alternatives.SetNum(KeysPerDim[DimIndex].Num());
		for (int32 KeyIndex = 0; KeyIndex < KeysPerDim[DimIndex].Num(); ++KeyIndex)
		{
			EffectiveDims[DimIndex]->ExpandKeyToPathSegments(KeysPerDim[DimIndex][KeyIndex], Alternatives[KeyIndex]);
		}
	}

	// 跨维度笛卡尔积（防爆护栏 W2：单 Actor 路径总数达 MaxPathsPerActor 即截断，保留已生成路径、停止展开后续维度）
	OutPaths.Add(TArray<FHwoGroupKey>());
	for (int32 DimIndex = 0; DimIndex < SegmentsPerDim.Num(); ++DimIndex)
	{
		TArray<TArray<FHwoGroupKey>> NextPaths;
		bool bHitCap = false;
		for (const TArray<FHwoGroupKey>& ExistingPath : OutPaths)
		{
			for (const TArray<FHwoGroupKey>& Alternative : SegmentsPerDim[DimIndex])
			{
				if (NextPaths.Num() >= MaxPathsPerActor)
				{
					bHitCap = true;
					break;
				}
				TArray<FHwoGroupKey> NewPath = ExistingPath;
				NewPath.Append(Alternative);
				NextPaths.Add(MoveTemp(NewPath));
			}
			if (bHitCap)
			{
				break;
			}
		}
		OutPaths = MoveTemp(NextPaths);

		if (bHitCap)
		{
			// 每进程只告警一次，避免 Populate 期间刷屏
			static bool bCapWarningLogged = false;
			if (!bCapWarningLogged)
			{
				bCapWarningLogged = true;
				UE_LOG(LogHwo, Warning, TEXT("Actor '%s' 的维度组合路径数超过上限 %d（多值维度组合膨胀），多余路径已截断。请精简维度堆叠或减少该 Actor 的 DataLayer/Tag 数量。"),
					*Ctx.GetLabel(), MaxPathsPerActor);
			}
			break;
		}
	}
}

void FHwoDimensionStack::GetEffectiveDimensions(TArray<TSharedRef<IHwoDimension>>& OutDims) const
{
	if (Dimensions.IsEmpty())
	{
		// D3：空栈回退为纯文件夹维度，观感与自带 World Outliner 一致
		OutDims.Add(GetFallbackFolderDimension());
	}
	else
	{
		OutDims = Dimensions;
	}
}

TSharedPtr<IHwoDimension> FHwoDimensionStack::FindDimension(FName DimensionID) const
{
	TArray<TSharedRef<IHwoDimension>> EffectiveDims;
	GetEffectiveDimensions(EffectiveDims);
	for (const TSharedRef<IHwoDimension>& Dim : EffectiveDims)
	{
		if (Dim->GetDimensionID() == DimensionID)
		{
			return Dim;
		}
	}
	return nullptr;
}

TSharedRef<IHwoDimension> FHwoDimensionStack::GetFallbackFolderDimension()
{
	// 静态缓存实例：回退维度需身份稳定——分组节点持维度弱引用（动态图标），
	// 若每次新建临时实例，弱引用立即失效
	static TSharedRef<IHwoDimension> Fallback = MakeShared<FHwoFolderDimension>();
	return Fallback;
}

FString FHwoDimensionStack::GetConfigFilePath()
{
	return FPaths::ProjectSavedDir() / TEXT("HierarchicalWorldOutliner") / TEXT("OutlinerState.ini");
}

void FHwoDimensionStack::SaveToConfig() const
{
	TArray<FString> DimensionIDs;
	DimensionIDs.Reserve(Dimensions.Num());
	for (const TSharedRef<IHwoDimension>& Dim : Dimensions)
	{
		DimensionIDs.Add(Dim->GetDimensionID().ToString());
	}
	const FString JoinedIDs = FString::Join(DimensionIDs, TEXT(","));

	const FString ConfigPath = GetConfigFilePath();
	// FConfigFile::Write 不会自建目录，先确保 Saved 子目录存在
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath), /*Tree*/true);

	FConfigFile ConfigFile;
	// 先读回已有文件：同一 ini 还保存视图选项（ViewOptions 节）等其它数据，避免整文件覆盖
	ConfigFile.Read(ConfigPath);
	ConfigFile.SetString(HwoStackConfig::SectionName, HwoStackConfig::KeyName, *JoinedIDs);
	ConfigFile.Write(ConfigPath);
}

void FHwoDimensionStack::LoadFromConfig()
{
	Dimensions.Empty();

	const FString ConfigPath = GetConfigFilePath();
	if (!IFileManager::Get().FileExists(*ConfigPath))
	{
		// 文件不存在 → 空栈（D3 回退），保持静默
		return;
	}

	FConfigFile ConfigFile;
	ConfigFile.Read(ConfigPath);

	FString JoinedIDs;
	if (!ConfigFile.GetString(HwoStackConfig::SectionName, HwoStackConfig::KeyName, JoinedIDs))
	{
		return;
	}

	TArray<FString> DimensionIDs;
	JoinedIDs.ParseIntoArray(DimensionIDs, TEXT(","), /*bCullEmpty*/true);
	for (FString& DimensionIDString : DimensionIDs)
	{
		DimensionIDString.TrimStartAndEndInline();
		const FName DimensionID(*DimensionIDString);

		// 未知 ID 静默跳过（向前兼容：新版维度在旧版编辑器中打开时不报错）
		if (DimensionID.IsNone() || !HwoBuiltInDimensions::IsBuiltInDimensionID(DimensionID))
		{
			continue;
		}
		// 超出最大堆叠深度的部分静默忽略
		if (Dimensions.Num() >= MaxStackDepth)
		{
			break;
		}
		// 重复 ID 跳过
		if (Contains(DimensionID))
		{
			continue;
		}
		Dimensions.Add(HwoBuiltInDimensions::CreateDimension(DimensionID));
	}
	// 有意不广播 OnStackChanged：由调用方决定何时刷新
}
