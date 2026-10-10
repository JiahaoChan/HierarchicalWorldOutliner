// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "Dimensions/HwoBuiltInDimensions.h"

#include "HwoLocalization.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Styling/AppStyle.h"
#include "WorldPartition/ActorDescContainerInstance.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"

/** 空间网格维度的特殊分组键（\x02 前缀防与真实 Grid 名碰撞；\x01 已被"未分配"占位占用） */
namespace HwoRuntimeGridKeys
{
	// 非空间加载的 Actor（不进格子的特殊组）
	inline const TCHAR* NonSpatiallyLoaded = TEXT("\x02NonSpatiallyLoaded");
}

const TCHAR* FHwoRuntimeGridDimension::GetNonSpatiallyLoadedGroupKey()
{
	return HwoRuntimeGridKeys::NonSpatiallyLoaded;
}

FText FHwoRuntimeGridDimension::GetDisplayName() const
{
	return HWO_TEXT("Dim.RuntimeGrid.Name", "Runtime Grid", "空间网格");
}

FSlateIcon FHwoRuntimeGridDimension::GetIcon() const
{
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "ShowFlagsMenu.Grid");
}

void FHwoRuntimeGridDimension::GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const
{
	// 双态一致性约束：两侧必须算出完全相同的键（同序列化字段 + 同一算法）
	UWorld* World = nullptr;
	bool bSpatiallyLoaded = false;
	FName RuntimeGridName = NAME_None;
	FBox ActorBounds(ForceInit);

	if (const AActor* Actor = Ctx.Actor)
	{
		World = Actor->GetWorld();
		bSpatiallyLoaded = Actor->GetIsSpatiallyLoaded();
		RuntimeGridName = Actor->GetRuntimeGrid();
		// 双态口径差异（已知边缘项，见 Doc/TodoList.md）：Desc 侧的 GetIsSpatiallyLoaded 还折叠了
		// RuntimeBounds.IsValid 条件（WorldPartitionActorDesc.h:315）——"标记空间加载但包围盒无效"的 Actor，
		// 未加载侧落"非空间加载"组；而已加载侧 GetActorBounds 对无包围盒 Actor 产出原点退化盒（恒有效），
		// 会算到 (0,0) 格。加载/卸载互换瞬间组间跳动，同帧收敛自愈、场景稀少，不改行为。
		// 另注：已加载侧 GetActorBounds 与未加载侧 GetEditorBounds 口径非完全同义（编辑器 padding），
		// 贴格边 Actor 换叶瞬间可能短暂换组，同帧收敛
		FVector Origin;
		FVector BoxExtent;
		Actor->GetActorBounds(false, Origin, BoxExtent, /*bIncludeFromChildActors*/true);
		ActorBounds = FBox::BuildAABB(Origin, BoxExtent);
	}
	else if (const FWorldPartitionActorDescInstance* DescInstance = Ctx.DescInstance)
	{
		// 未加载侧经容器 Outer 链取世界（同 HLOD 维度先例）
		if (const UActorDescContainerInstance* ContainerInstance = DescInstance->GetContainerInstance())
		{
			if (const UWorldPartition* OuterWorldPartition = ContainerInstance->GetOuterWorldPartition())
			{
				World = OuterWorldPartition->GetWorld();
			}
		}
		// 注意：Desc 侧的 GetIsSpatiallyLoaded 已折叠 ForceNonSpatiallyLoaded；已加载侧无该取口
		// （强制非空间是编辑器侧临时状态，仅存于 Desc），已加载侧只按 GetIsSpatiallyLoaded 判定（取舍）
		bSpatiallyLoaded = DescInstance->GetIsSpatiallyLoaded();
		RuntimeGridName = DescInstance->GetRuntimeGrid();
		ActorBounds = DescInstance->GetEditorBounds();
	}

	// 世界守卫：非 WP 或 RuntimeHash 非 UWorldPartitionRuntimeSpatialHash → 未分配组
	const UWorldPartition* WorldPartition = World ? World->GetWorldPartition() : nullptr;
	const UWorldPartitionRuntimeSpatialHash* SpatialHash = WorldPartition ? Cast<UWorldPartitionRuntimeSpatialHash>(WorldPartition->RuntimeHash) : nullptr;
	if (!SpatialHash)
	{
		return;
	}

	// 非空间加载分流：不进格子，归特殊组
	if (!bSpatiallyLoaded)
	{
		OutKeys.Add(FHwoGroupKey{ GetDimensionID(), HwoRuntimeGridKeys::NonSpatiallyLoaded });
		return;
	}

	// Grid 配置解析（GetFixedGridInfo 内部已含 NAME_None → 第一个 Grid 的回落，与引擎同约定）；
	// 找不到匹配配置 → 未分配组
	const TOptional<FFixedGridInfo> GridInfo = SpatialHash->GetFixedGridInfo(RuntimeGridName);
	if (!GridInfo.IsSet() || GridInfo->CellSize == 0)
	{
		return;
	}

	// Cell 坐标：包围盒中心点（X/Y 二轴）相对 Origin 的 floor 除法。
	// 未套用引擎 FSquare2DGridHelper::FGrid2D::GetCellCoords 的 GridSize 居中偏移与有效性钳制
	// （GridSize 在流送生成阶段才稳定，编辑器内不可得）；坐标为相对 Origin 的 Cell 索引，仅用于分组展示
	const FVector Center = ActorBounds.GetCenter();
	// 用 double 中间值避免大世界坐标（LWC）下 float 精度丢失
	const int32 CellX = FMath::FloorToInt((Center.X - GridInfo->Origin.X) / static_cast<double>(GridInfo->CellSize));
	const int32 CellY = FMath::FloorToInt((Center.Y - GridInfo->Origin.Y) / static_cast<double>(GridInfo->CellSize));

	// NAME_None 回落默认 Grid 名（Grids 是 private，经公开取口 GetDefaultGrid 读，语义同为 Grids[0].GridName）
	const FName EffectiveGridName = RuntimeGridName.IsNone() ? SpatialHash->GetDefaultGrid() : RuntimeGridName;
	OutKeys.Add(FHwoGroupKey{ GetDimensionID(), FString::Printf(TEXT("%s|%d|%d"), *EffectiveGridName.ToString(), CellX, CellY) });
}

FText FHwoRuntimeGridDimension::GetGroupDisplayName(const FHwoGroupKey& Key) const
{
	if (Key.Key == HwoGroupKeys::Unassigned)
	{
		return FText::Format(HWO_TEXT("Dim.RuntimeGrid.Unassigned", "(Unassigned {0})", "(未分配 {0})"), GetDisplayName());
	}
	if (Key.Key == HwoRuntimeGridKeys::NonSpatiallyLoaded)
	{
		return HWO_TEXT("Dim.RuntimeGrid.NonSpatiallyLoaded", "Not Spatially Loaded", "非空间加载");
	}

	// cell 键 "Grid|X|Y" → "Grid (X, Y)"；解析失败（键损坏）回退原串显示。
	// 前提：Grid 名不含 "|"（WorldSettings 可自由命名，含 "|" 时显示退化但不影响分组 ID 与归属）
	FString GridName;
	FString CoordRest;
	FString CoordX;
	FString CoordY;
	if (Key.Key.Split(TEXT("|"), &GridName, &CoordRest) && CoordRest.Split(TEXT("|"), &CoordX, &CoordY))
	{
		return FText::FromString(FString::Printf(TEXT("%s (%s, %s)"), *GridName, *CoordX, *CoordY));
	}
	return FText::FromString(Key.Key);
}
