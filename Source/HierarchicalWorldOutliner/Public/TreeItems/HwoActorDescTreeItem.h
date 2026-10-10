// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Dimensions/HwoDimensionTypes.h"

#include "ActorDescTreeItem.h"

class FWorldPartitionActorDescInstance;

/**
 * FHwoActorDescTreeItem 的构造数据
 *
 * 聚合 WorldPartition 未加载 Actor 的 ActorDescInstance 与其完整维度路径，
 * 由 FHwoHierarchy 创建叶子节点时填充，仅作为构造期的数据载体。
 */
struct FHwoActorDescTreeItemData
{
	// WP 未加载 Actor 的 ActorDescInstance 指针（不长期持有，仅构造期取数）
	const FWorldPartitionActorDescInstance* DescInstance;

	// 完整维度分组路径（末段为该叶子的直属分组）
	TArray<FHwoGroupKey> Path;

	FHwoActorDescTreeItemData(const FWorldPartitionActorDescInstance* InDescInstance, const TArray<FHwoGroupKey>& InPath)
		: DescInstance(InDescInstance)
		, Path(InPath)
	{}
};

/**
 * WorldPartition 未加载 Actor 的叶子节点
 *
 * 继承 FActorDescTreeItem：内部持 FWorldPartitionHandle，不加载 Actor UObject。
 * 由 FHwoHierarchy 为 WP 未加载 Actor 创建，挂在对应维度分组之下。
 * GetID 覆写为 (维度路径, ActorGuid) 组合 ID（HwoTreeItemId::ForLeaf），
 * 同一 Actor 在多个分组下各有一片全树唯一的叶子（Design-1）。
 */
struct FHwoActorDescTreeItem : FActorDescTreeItem
{
	// 自有节点类型，Parent 指回 FActorDescTreeItem::Type
	static const FSceneOutlinerTreeItemType Type;

	// 完整维度分组路径（末段为该叶子的直属分组）
	TArray<FHwoGroupKey> Path;

	// 构造期缓存的 Actor Guid（DescInstance 指针不长期持有，后续索引与移除均使用缓存值）
	FGuid CachedActorGuid;

	FHwoActorDescTreeItem(const FHwoActorDescTreeItemData& InData)
		: FActorDescTreeItem(InData.DescInstance)
		, Path(InData.Path)
		, CachedActorGuid(InData.DescInstance ? InData.DescInstance->GetGuid() : FGuid())
	{
		// FActorDescTreeItem 没有 TypeIn 构造，直接改写类型标识（TDD §4.7）
		TreeType = Type;
	}

	virtual FSceneOutlinerTreeItemID GetID() const override { return HwoTreeItemId::ForLeaf(Path, CachedActorGuid); }

	virtual const FGuid& GetGuid() const override { return CachedActorGuid; }
};
