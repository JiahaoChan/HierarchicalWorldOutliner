// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Dimensions/HwoDimensionTypes.h"

#include "ActorTreeItem.h"

/**
 * FHwoActorTreeItem 的构造数据
 *
 * 聚合已加载 Actor 的弱引用与其完整维度路径，
 * 由 FHwoHierarchy 创建叶子节点时填充，仅作为构造期的数据载体。
 */
struct FHwoActorTreeItemData
{
	// 已加载 Actor 的弱引用（Actor 销毁后自动失效）
	TWeakObjectPtr<AActor> Actor;

	// 完整维度分组路径（末段为该叶子的直属分组）
	TArray<FHwoGroupKey> Path;

	FHwoActorTreeItemData(AActor* InActor, const TArray<FHwoGroupKey>& InPath)
		: Actor(InActor)
		, Path(InPath)
	{}
};

/**
 * 已加载 Actor 的叶子节点
 *
 * 继承 FActorTreeItem 以复用框架内置的图标、可见性列、重命名等表现，
 * 由 FHwoHierarchy 为已加载 Actor 创建，挂在对应维度分组之下。
 * GetID 覆写为 (维度路径, ActorGuid) 组合 ID（HwoTreeItemId::ForLeaf），
 * 同一 Actor 在多个分组下各有一片全树唯一的叶子（Design-1）。
 */
struct FHwoActorTreeItem : FActorTreeItem
{
	// 自有节点类型，Parent 指回 FActorTreeItem::Type，引擎内置列的 FActorTreeItem 分支依然生效
	static const FSceneOutlinerTreeItemType Type;

	// 完整维度分组路径（末段为该叶子的直属分组）
	TArray<FHwoGroupKey> Path;

	// 构造期缓存的 Actor Guid，Actor 死亡后仍可索引与移除
	FGuid ActorGuid;

	FHwoActorTreeItem(const FHwoActorTreeItemData& InData)
		: FActorTreeItem(Type, InData.Actor.Get())
		, Path(InData.Path)
		, ActorGuid(InData.Actor.IsValid() ? InData.Actor->GetActorGuid() : FGuid())
	{}

	virtual FSceneOutlinerTreeItemID GetID() const override { return HwoTreeItemId::ForLeaf(Path, ActorGuid); }

	virtual const FGuid& GetGuid() const override { return ActorGuid; }
};
