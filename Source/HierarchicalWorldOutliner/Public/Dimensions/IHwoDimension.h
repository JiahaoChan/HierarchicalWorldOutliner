// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Dimensions/HwoDimensionTypes.h"

#include "Textures/SlateIcon.h"

struct FSlateBrush;

/**
 * 维度接口（纯 C++，不进反射）：对 Actor 的一种分组依据
 *
 * 由 FHwoDimensionStack 持有并按 1~5 层堆叠；取数面向 FHwoActorContext 统一视图，
 * 对实现者屏蔽"已加载 / WP 未加载"差异。内置实现为 HwoBuiltInDimensions.h 中的九个维度类。
 *
 * 实现者须知：GetDimensionID / GetDisplayName / GetIcon / GetGroupKeys / GetGroupDisplayName
 * 为纯虚函数，必须实现；GetGroupIcon（动态图标）与 ExpandKeyToPathSegments（一键多段展开，
 * 如文件夹维度嵌套）有默认实现，按需覆写。
 */
class IHwoDimension
{
public:
	virtual ~IHwoDimension() = default;

	/**
	 *	取稳定维度 ID（"DataLayer" / "Class" / "Folder" / "Tag" / "HLOD"），用于持久化与按 ID 查找
	 *
	 *	@return	维度 ID
	 */
	virtual FName GetDimensionID() const = 0;

	/**
	 *	取维度显示名，用于 Chip 与分组节点
	 *
	 *	@return	本地化后的维度显示名
	 */
	virtual FText GetDisplayName() const = 0;

	/**
	 *	取分组节点的静态图标（区分于 Actor 图标）
	 *
	 *	@return	维度图标
	 */
	virtual FSlateIcon GetIcon() const = 0;

	/**
	 *	计算一个 Actor 在本维度下的全部分组键
	 *
	 *	一个 Actor 可属多个分组（多 DataLayer、多 Tag）→ 产出多个键，叶子按跨维度路径笛卡尔积复制；
	 *	无归属 → 返回空，由 FHwoDimensionStack 归入"未分配"特殊分组。
	 *	每个键默认占路径的一段；需多段展开的维度（如文件夹维度按 "/" 拆嵌套）覆写 ExpandKeyToPathSegments。
	 *
	 *	@param	Ctx			Actor 统一视图
	 *	@param	OutKeys		输出的分组键列表
	 */
	virtual void GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const = 0;

	/**
	 *	把一个分组键展开成路径段序列
	 *
	 *	默认实现：一键一段（绝大多数维度）。文件夹维度覆写为按 "/" 拆成多段（"A/B" → ["A","B"]），
	 *	树上呈现真实嵌套。段键只存段名（不存累计路径），分组节点 ID 由全路径编码，天然无碰撞。
	 *
	 *	@param	Key				要展开的分组键（HwoGroupKeys::Unassigned 占位键不得拆分）
	 *	@param	OutSegments		输出的路径段（与 Key 同 DimensionID）
	 */
	virtual void ExpandKeyToPathSegments(const FHwoGroupKey& Key, TArray<FHwoGroupKey>& OutSegments) const
	{
		OutSegments.Add(Key);
	}

	/**
	 *	取分组键的显示名
	 *
	 *	@param	Key		分组键；Key.Key 为 HwoGroupKeys::Unassigned 时应返回"（未分配 X）"形式
	 *	@return	分组节点的显示名
	 */
	virtual FText GetGroupDisplayName(const FHwoGroupKey& Key) const = 0;

	/**
	 *	取分组节点的动态图标（可随展开状态 / 子节点数变化）
	 *
	 *	默认实现返回维度静态图标；文件夹维度覆写为 Open / Closed 切换（先例 ActorFolderTreeItem.cpp:136-145），
	 *	类维度覆写为类的实际图标（FSlateIconFinder）。
	 *
	 *	@param	Key				分组键
	 *	@param	bExpanded		分组节点是否展开
	 *	@param	bHasChildren	分组节点是否有子节点
	 *	@return	分组节点图标画刷
	 */
	virtual const FSlateBrush* GetGroupIcon(const FHwoGroupKey& Key, bool bExpanded, bool bHasChildren) const
	{
		return GetIcon().GetIcon();
	}
};
