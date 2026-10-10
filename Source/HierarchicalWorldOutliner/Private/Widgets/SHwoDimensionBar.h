// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Widgets/SCompoundWidget.h"

class FHwoDimensionStack;
class SHorizontalBox;
class SWidget;

/**
 * 维度 Chip 栏：当前堆叠维度的横向 Chip 列表 + [+] 添加按钮
 *
 * Chip 支持 × 移除与拖拽排序；[+] 弹出菜单列出全部内置维度，已在栈中或栈满（MaxStackDepth）的条目置灰。
 * 空栈（D3）时显示一个不可移除、不可拖拽的「文件夹」回退 Chip，如实反映 FHwoDimensionStack 的语义回退。
 * 由 SHwoPanel 创建并持有，随面板生命周期销毁。
 */
class SHwoDimensionBar : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SHwoDimensionBar) {}

		// 维度堆叠数据源，由宿主 SHwoPanel 创建并共享
		SLATE_ARGUMENT(TSharedPtr<FHwoDimensionStack>, Stack)

	SLATE_END_ARGS()

private:
	// 维度堆叠数据源，增删与排序变更通过其 OnStackChanged 委托通知本控件重建 Chip
	TSharedPtr<FHwoDimensionStack> Stack;

	// Chip 横向排列容器
	TSharedPtr<SHorizontalBox> ChipBox;

	// Chip 重建延迟标记（一帧内多次堆叠变更合并为一次定时器，见 HandleStackChanged）
	bool bChipRebuildPending = false;

public:
	virtual ~SHwoDimensionBar();

	/**
	 *	Slate 初始化入口，由 SNew 调用：订阅堆叠变更，构建 Chip 列表与 [+] 添加按钮
	 *
	 *	@param	InArgs	SNew 传入的构造参数
	 */
	void Construct(const FArguments& InArgs);

private:
	/**
	 *	堆叠变更中转回调：OnStackChanged 可能在 Slate 事件路由中广播（Chip 的 × 点击 / 拖拽放下），
	 *	结构性销毁控件（RebuildChips 里 ClearChildren）必须延迟一帧，此处注册一次性 ActiveTimer
	 */
	void HandleStackChanged();

	/**
	 *	依据 Stack 当前有效维度重建全部 Chip
	 *
	 *	@note	空栈时 GetEffectiveDimensions 返回回退的文件夹维度（D3），对应 Chip 不绑定移除与拖拽回调
	 *	@warning	只能在非 Slate 事件路由的上下文中调用（Construct / ActiveTimer 回调）
	 */
	void RebuildChips();

	/**
	 *	按维度 ID 取固定 Chip 颜色
	 *
	 *	@param	DimensionID		维度 ID
	 *	@return	该维度的 Chip 填充色（固定调色板：高饱和、低明度，白字可读且跨会话稳定；未知维度按 ID 哈希取色相）
	 */
	static FLinearColor GetChipColor(FName DimensionID);

	/**
	 *	构建 [+] 按钮的弹出菜单内容，列出全部内置维度
	 *
	 *	@return	菜单控件；已在栈中或栈已达 MaxStackDepth 上限的条目置灰不可选
	 */
	TSharedRef<SWidget> BuildAddDimensionMenuContent();

	/**
	 *	添加维度菜单条目选中回调，把该维度追加到堆叠末尾
	 *
	 *	@param	DimensionID		要添加的维度 ID
	 */
	void OnAddDimension(FName DimensionID);

	/**
	 *	Chip 的 × 移除钮回调，从堆叠中移除该维度
	 *
	 *	@param	DimensionID		要移除的维度 ID
	 */
	void OnRemoveDimension(FName DimensionID);

	/**
	 *	Chip 拖拽放下回调：把被拖拽维度移到目标维度之前
	 *
	 *	@param	TargetID	放下目标 Chip 的维度 ID
	 *	@param	DraggedID	被拖拽 Chip 的维度 ID
	 */
	void OnChipDroppedOn(FName TargetID, FName DraggedID);
};
