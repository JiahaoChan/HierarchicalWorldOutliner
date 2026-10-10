// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Dimensions/IHwoDimension.h"

#include "Templates/SharedPointer.h"

/**
 * 维度堆叠（1~5 层，可空）
 *
 * 持有有序的维度列表，顺序即分组层级（第一层为最外层分组）。空栈时参与构建的维度
 * 回退为单层文件夹维度（D3，见 GetEffectiveDimensions / GetFallbackFolderDimension）。
 * 由面板（SHwoPanel）持有，Mode / Hierarchy 共享只读；整体替换、增删、拖拽排序都会
 * 广播 OnStackChanged，堆叠内容即时持久化到 Saved ini（SaveToConfig / LoadFromConfig）。
 */
class FHwoDimensionStack : public TSharedFromThis<FHwoDimensionStack>
{
public:
	/**
	 *	维度堆叠内容变化（整体替换、增删、拖拽排序）时广播
	 */
	DECLARE_MULTICAST_DELEGATE(FOnStackChanged);

	// 维度堆叠的最大层数（1~5 层）
	static constexpr int32 MaxStackDepth = 5;

	// 单 Actor 叶子路径总数上限：多值维度（DataLayer / Tag）跨维度笛卡尔积膨胀时截断，超出部分不再展开
	static constexpr int32 MaxPathsPerActor = 64;

	// 堆叠内容变化时广播的委托实例，面板与 Chip 栏订阅后触发重建
	FOnStackChanged OnStackChanged;

private:
	// 当前堆叠的维度列表（有序，顺序即分组层级）
	TArray<TSharedRef<IHwoDimension>> Dimensions;

public:
	/**
	 *	整体替换维度堆叠并广播 OnStackChanged
	 *
	 *	@param	InDims	新的维度列表（有序，顺序即分组层级）
	 */
	void SetDimensions(const TArray<TSharedRef<IHwoDimension>>& InDims);

	/**
	 *	追加一个维度到栈顶，成功时广播 OnStackChanged
	 *
	 *	@param	InDim	要追加的维度实例
	 *	@return	追加成功返回 true；已达 MaxStackDepth 上限或同 ID 维度已在栈中则返回 false
	 */
	bool AddDimension(TSharedRef<IHwoDimension> InDim);

	/**
	 *	移除指定 ID 的维度，移除成功时广播 OnStackChanged
	 *
	 *	@param	DimensionID		要移除的维度 ID
	 *	@return	栈中存在该维度并已移除返回 true
	 */
	bool RemoveDimension(FName DimensionID);

	/**
	 *	把一个维度移动到另一个维度之前（Chip 拖拽排序用），移动成功时广播 OnStackChanged
	 *
	 *	@param	DraggedID	被拖拽维度的 ID
	 *	@param	TargetID	目标位置维度的 ID
	 *	@return	两个 ID 都在栈中且不相同、移动成功时返回 true
	 */
	bool MoveDimensionBefore(FName DraggedID, FName TargetID);

	/**
	 *	判断指定 ID 的维度是否在真实堆叠中（不含 D3 回退维度）
	 *
	 *	@param	DimensionID		要查找的维度 ID
	 *	@return	在栈中返回 true
	 */
	bool Contains(FName DimensionID) const;

	/**
	 *	计算 Actor 的完整叶子路径列表：每个维度产出 0..N 个键，跨维度做笛卡尔积；任一维度无键时该层补"未分配"键
	 *
	 *	例 [DataLayer, Class]：Actor 属 DL_A / DL_B 两个数据层且为 PointLight → 产出 2 条路径。
	 *	路径总数达 MaxPathsPerActor 即截断（保留已生成路径，停止展开后续维度）。
	 *
	 *	@param	Ctx			Actor 统一视图
	 *	@param	OutPaths	输出的路径列表，每条路径为与有效维度等长的键序列
	 */
	void ComputeLeafPaths(const FHwoActorContext& Ctx, TArray<TArray<FHwoGroupKey>>& OutPaths) const;

	/**
	 *	取参与构建的维度列表；空栈时回退为单层文件夹维度（D3）
	 *
	 *	@param	OutDims		输出的有效维度列表
	 */
	void GetEffectiveDimensions(TArray<TSharedRef<IHwoDimension>>& OutDims) const;

	/**
	 *	按 ID 在有效维度列表（含 D3 回退维度）中查找维度
	 *
	 *	@param	DimensionID		要查找的维度 ID
	 *	@return	找到的维度实例，未找到返回 nullptr
	 */
	TSharedPtr<IHwoDimension> FindDimension(FName DimensionID) const;

	/**
	 *	取 D3 空态回退维度（静态缓存实例，身份稳定，分组节点可持其弱引用）
	 *
	 *	@return	共享的文件夹维度实例
	 */
	static TSharedRef<IHwoDimension> GetFallbackFolderDimension();

	/**
	 *	取插件状态 ini 的完整路径（Saved/HierarchicalWorldOutliner/OutlinerState.ini），供各持久化点共用
	 *
	 *	@return	ini 完整路径
	 */
	static FString GetConfigFilePath();

	/**
	 *	把当前维度堆叠按顺序写入 Saved/HierarchicalWorldOutliner/OutlinerState.ini
	 */
	void SaveToConfig() const;

	/**
	 *	从 Saved ini 读取维度堆叠并覆盖当前堆叠
	 *
	 *	未知 ID 静默跳过（向前兼容）；文件不存在时得到空栈（自然走 D3 文件夹回退）。
	 *
	 *	@note	不广播 OnStackChanged，由调用方决定何时刷新
	 */
	void LoadFromConfig();

	const TArray<TSharedRef<IHwoDimension>>& GetDimensions() const { return Dimensions; }

	bool IsEmpty() const { return Dimensions.IsEmpty(); }
};
