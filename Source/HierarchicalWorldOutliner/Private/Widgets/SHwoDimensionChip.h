// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Input/DragAndDrop.h"
#include "Input/Reply.h"
#include "Textures/SlateIcon.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBorder.h"

/**
 * Chip 拖拽排序操作，仅 Chip 栏内部使用
 *
 * 携带被拖拽 Chip 的维度 ID 与显示名；拖拽经过时以显示名渲染默认 Decorator，
 * 放下时由目标 SHwoDimensionChip::OnDrop 取出并回调 OnDropped。
 */
class FHwoChipDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FHwoChipDragDropOp, FDragDropOperation)

	// 被拖拽 Chip 的维度 ID
	FName DimensionID;

	// 被拖拽 Chip 的显示名，用于拖拽中的 Decorator 文本
	FText DisplayName;

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;

	/**
	 *	创建一次 Chip 拖拽操作
	 *
	 *	@param	InDimensionID	被拖拽 Chip 的维度 ID
	 *	@param	InDisplayName	被拖拽 Chip 的显示名
	 *	@return	已 Construct、可直接交给 BeginDragDrop 的拖拽操作
	 */
	static TSharedRef<FHwoChipDragDropOp> New(FName InDimensionID, const FText& InDisplayName)
	{
		TSharedRef<FHwoChipDragDropOp> Operation = MakeShared<FHwoChipDragDropOp>();
		Operation->DimensionID = InDimensionID;
		Operation->DisplayName = InDisplayName;
		Operation->Construct();
		return Operation;
	}
};

/**
 * 单个维度 Chip：彩色圆角矩形底（FSlateRoundedBoxBrush）+ 图标 + 名称 + × 移除钮，可拖拽排序
 *
 * 背景色由 SHwoDimensionBar 按维度 ID 缓存后传入，Chip 重建时颜色不跳变。
 * OnRemoved 未绑定时不显示 ×（用于空态回退的文件夹 Chip，D3）；OnDropped 未绑定时不可拖拽。
 */
class SHwoDimensionChip : public SBorder
{
public:
	/**
	 *	点击 Chip 的 × 移除钮时广播
	 *
	 *	@param	DimensionID		被移除维度的 ID
	 */
	DECLARE_DELEGATE_OneParam(FOnHwoChipRemoved, FName /*DimensionID*/);

	/**
	 *	拖拽中的 Chip 放下到本 Chip 上时广播，用于堆叠排序
	 *
	 *	@param	TargetID	放下目标 Chip 的维度 ID
	 *	@param	DraggedID	被拖拽 Chip 的维度 ID
	 */
	DECLARE_DELEGATE_TwoParams(FOnHwoChipDropped, FName /*TargetID*/, FName /*DraggedID*/);

	SLATE_BEGIN_ARGS(SHwoDimensionChip) {}

		// 本 Chip 对应的维度 ID
		SLATE_ARGUMENT(FName, DimensionID)

		// 圆角底填充色，缺省为默认灰
		SLATE_ARGUMENT(TOptional<FLinearColor>, ChipColor)

		// Chip 上显示的维度名称
		SLATE_ATTRIBUTE(FText, Text)

		// 维度图标，未设置时不显示图标
		SLATE_ATTRIBUTE(FSlateIcon, Icon)

		// × 移除钮点击回调；未绑定则不显示 ×
		SLATE_EVENT(FOnHwoChipRemoved, OnRemoved)

		// 拖拽放下回调；未绑定则本 Chip 不可拖拽
		SLATE_EVENT(FOnHwoChipDropped, OnDropped)

	SLATE_END_ARGS()

private:
	// 拖拽放下回调的委托实例，未绑定时 Chip 不可拖拽
	FOnHwoChipDropped OnDropped;

	// 本 Chip 对应的维度 ID
	FName DimensionID;

	// Chip 上显示的维度名称（缓存值，供拖拽 Decorator 使用）
	FText ChipText;

	// 彩色圆角底画刷（SBorder 只存裸指针，画刷需由本控件持有）
	TSharedPtr<FSlateRoundedBoxBrush> BackgroundBrush;

public:
	/**
	 *	Slate 初始化入口，由 SNew 调用：构建图标、名称与可选的 × 移除钮，并以圆角画刷为底
	 *
	 *	@param	InArgs	SNew 传入的构造参数
	 */
	void Construct(const FArguments& InArgs);

	//~ Begin SWidget Interface
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	//~ End SWidget Interface
};
