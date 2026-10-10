// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Dimensions/HwoDimensionTypes.h"

#include "ISceneOutlinerTreeItem.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "Textures/SlateIcon.h"
#include "WorldPartition/WorldPartitionHandle.h"

class IHwoDimension;
class ISceneOutliner;
class SSceneOutliner;
class UWorld;
template <typename ItemType> class STableRow;
class UToolMenu;

/**
 * FHwoGroupTreeItem 的构造数据
 *
 * 由 FHwoHierarchy::FindOrCreateParentItem 按需创建分组节点时填充，仅作为构造期的数据载体。
 */
struct FHwoGroupTreeItemData
{
	// 完整维度分组路径（末段为本分组自身的键）
	TArray<FHwoGroupKey> Path;

	// 分组显示名（不含计数徽标）
	FText DisplayName;

	// 静态分组图标（维度未提供动态图标时的回退）
	FSlateIcon Icon;

	// 末段键所属维度（弱引用，由 FHwoDimensionStack 持有），用于动态分组图标
	TWeakPtr<IHwoDimension> Dimension;

	// 本分组所属世界（弱引用；文件夹维度分组的未保存列经它定位 UActorFolder）
	TWeakObjectPtr<UWorld> World;
};

/**
 * 虚拟分组节点：不对应任何 UObject，是纯路径实体
 *
 * 由 FHwoHierarchy 按维度路径逐层创建，承载分组的显示名、图标与子节点。
 * 节点 ID 为 MD5("G|" + 路径串)（HwoTreeItemId::ForGroup），全树唯一且跨刷新稳定。
 * DisplayString 携带子代去重 Actor 计数徽标 "名称 (N)"（TDD §5.3 方案 A）。
 * 空组应消失：ShouldRemoveOnceLastChildRemoved 返回 true（基类默认实现仅在被过滤时才移除）。
 * 文件夹维度的分组还原原生文件夹节点的列行为：可视性列（OR 聚合 + 点击递归传播）、
 * 未保存列（经 FFolder::GetActorFolder 的外部包）；其余维度分组不显示。
 */
struct FHwoGroupTreeItem : ISceneOutlinerTreeItem
{
	// 自有节点类型，CastTo<> 分流用
	static const FSceneOutlinerTreeItemType Type;

	// 完整维度分组路径（末段为本分组自身的键）
	TArray<FHwoGroupKey> Path;

	// 分组显示名（不含计数徽标）
	FText DisplayName;

	// 静态分组图标（维度未提供动态图标时的回退）
	FSlateIcon Icon;

	// 末段键所属维度（弱引用，由 FHwoDimensionStack 持有），用于动态分组图标
	TWeakPtr<IHwoDimension> Dimension;

	// 本分组所属世界（弱引用；文件夹维度分组的未保存列经它定位 UActorFolder）
	TWeakObjectPtr<UWorld> World;

	// 子代去重 Actor Guid 集合（计数徽标数据源，由 FHwoMode::OnItemAdded / OnItemRemoved 维护）
	TSet<FGuid> UniqueActorGuids;

	FHwoGroupTreeItem(const FHwoGroupTreeItemData& InData)
		: ISceneOutlinerTreeItem(Type)
		, Path(InData.Path)
		, DisplayName(InData.DisplayName)
		, Icon(InData.Icon)
		, Dimension(InData.Dimension)
		, World(InData.World)
	{}

	//~ Begin ISceneOutlinerTreeItem Interface
	virtual bool IsValid() const override { return true; }

	virtual FSceneOutlinerTreeItemID GetID() const override { return HwoTreeItemId::ForGroup(Path); }

	virtual FString GetDisplayString() const override;

	virtual bool CanInteract() const override { return true; }

	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;

	virtual void GenerateContextMenu(UToolMenu* Menu, SSceneOutliner& Outliner) override;

	// HasVisibilityInfo 保持 false：可视性由框架按子代 OR 聚合（FSceneOutlinerVisibilityCache::RecurseChildren）
	virtual bool HasVisibilityInfo() const override { return false; }

	virtual bool ShouldShowVisibilityState() const override;

	virtual FString GetPackageName() const override;

	virtual bool ShouldRemoveOnceLastChildRemoved() const override { return true; }
	//~ End ISceneOutlinerTreeItem Interface

	/**
	 *	递归收集该节点子代中的全部已加载 Actor 与未加载 Actor 句柄
	 *
	 *	@param	Item		起始节点（分组或叶子均可）
	 *	@param	OutActors	输出：已加载 Actor 集合（TSet 保证去重）
	 *	@param	OutHandles	输出：WP 未加载 Actor 的句柄数组
	 */
	static void CollectActors(const ISceneOutlinerTreeItem& Item, TSet<AActor*>& OutActors, TArray<FWorldPartitionHandle>& OutHandles);

	/**
	 *	判断本分组是否为文件夹维度分组（末段键属于文件夹维度）
	 *
	 *	@return	是则返回 true
	 */
	bool IsFolderDimensionGroup() const;

	/**
	 *	从 Path 中的文件夹维度段拼回完整文件夹路径（世界根相对，如 "A/B"；跳过"未分配"占位段）
	 *
	 *	@return	完整文件夹路径；无文件夹段时返回空串
	 */
	FString ReconstructFolderPath() const;
};
