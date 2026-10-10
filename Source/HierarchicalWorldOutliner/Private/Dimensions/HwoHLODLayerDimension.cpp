// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "Dimensions/HwoBuiltInDimensions.h"

#include "HwoLocalization.h"

#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "Styling/AppStyle.h"
#include "WorldPartition/ActorDescContainerInstance.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/WorldPartition.h"

namespace HwoHLODKeys
{
	// "不参与 HLOD"特殊分组键（以不可显示字符 \x01 前缀，保证不与任何层资产名冲突）
	inline const TCHAR* NotRelevant = TEXT("\x01NotHLODRelevant");
}

FName FHwoHLODLayerDimension::GetDimensionID() const
{
	return FName(TEXT("HLOD"));
}

FText FHwoHLODLayerDimension::GetDisplayName() const
{
	return HWO_TEXT("Dim.HLOD.Name", "HLOD Layer", "HLOD 层");
}

FSlateIcon FHwoHLODLayerDimension::GetIcon() const
{
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.HLOD");
}

void FHwoHLODLayerDimension::GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const
{
	const FName DimensionID = GetDimensionID();

	// 键口径（W1）：两侧统一用长包名（"/Game/.../L_Lighting"）——已加载侧取资产的 GetPackage()->GetName()，
	// 未加载侧取软路径的 GetLongPackageName()，两种写法产出同一形式，保证加载/卸载互换时键一致；
	// 同名层资产在不同包中不再错误并组。显示时再映射回短名（见 GetGroupDisplayName）。
	// 注意：层资产被删除后键（包路径）将漂移为历史值，Actor 留在旧组名直到资产重建（W3，仅记录不改逻辑）。
	// TODO(W2/Bug-P2): LevelInstance 子 WP 的默认层应取子 WP 的 GetDefaultHLODLayer；当前不枚举 LI 子容器，修 Bug-P2 时一并处理。

	if (AActor* Actor = Ctx.Actor)
	{
		if (!Actor->IsHLODRelevant())
		{
			OutKeys.Add(FHwoGroupKey{ DimensionID, HwoHLODKeys::NotRelevant });
			return;
		}

		// 显式层
		if (const UHLODLayer* HLODLayer = Actor->GetHLODLayer())
		{
			if (const UPackage* LayerPackage = HLODLayer->GetPackage())
			{
				OutKeys.Add(FHwoGroupKey{ DimensionID, LayerPackage->GetName() });
				return;
			}
		}

		// 默认层（仅 WP 关卡有此概念；非 WP 关卡落空 → 未分配）
		if (const UWorld* World = Actor->GetWorld())
		{
			if (const UWorldPartition* WorldPartition = World->GetWorldPartition())
			{
				if (const UHLODLayer* DefaultLayer = WorldPartition->GetDefaultHLODLayer())
				{
					if (const UPackage* LayerPackage = DefaultLayer->GetPackage())
					{
						OutKeys.Add(FHwoGroupKey{ DimensionID, LayerPackage->GetName() });
						return;
					}
				}
			}
		}
	}
	else if (const FWorldPartitionActorDescInstance* DescInstance = Ctx.DescInstance)
	{
		if (!DescInstance->GetActorIsHLODRelevant())
		{
			OutKeys.Add(FHwoGroupKey{ DimensionID, HwoHLODKeys::NotRelevant });
			return;
		}

		// 显式层（免加载软路径，取长包名）
		const FSoftObjectPath HLODLayerPath = DescInstance->GetHLODLayer();
		if (HLODLayerPath.IsValid())
		{
			const FString LongPackageName = HLODLayerPath.GetLongPackageName();
			if (!LongPackageName.IsEmpty())
			{
				OutKeys.Add(FHwoGroupKey{ DimensionID, LongPackageName });
				return;
			}
		}

		// 默认层：从 DescInstance 的容器 Outer 链取 UWorldPartition
		if (const UActorDescContainerInstance* ContainerInstance = DescInstance->GetContainerInstance())
		{
			if (const UWorldPartition* WorldPartition = ContainerInstance->GetOuterWorldPartition())
			{
				if (const UHLODLayer* DefaultLayer = WorldPartition->GetDefaultHLODLayer())
				{
					if (const UPackage* LayerPackage = DefaultLayer->GetPackage())
					{
						OutKeys.Add(FHwoGroupKey{ DimensionID, LayerPackage->GetName() });
						return;
					}
				}
			}
		}
	}

	// 参与 HLOD 但无任何层 → 返回空，由 Stack 补"未分配"键
}

FText FHwoHLODLayerDimension::GetGroupDisplayName(const FHwoGroupKey& Key) const
{
	if (Key.Key == HwoGroupKeys::Unassigned)
	{
		return HWO_TEXT("Dim.HLOD.Unassigned", "(Unassigned HLOD Layer)", "(未分配 HLOD 层)");
	}
	if (Key.Key == HwoHLODKeys::NotRelevant)
	{
		return HWO_TEXT("Dim.HLOD.NotRelevant", "(Not HLOD Relevant)", "(不参与 HLOD)");
	}
	// 键为长包名（"/Game/HLOD/Layers/L_Lighting"），显示末段短名
	return FText::FromString(FPackageName::GetShortName(Key.Key));
}
