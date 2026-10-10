// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "Widgets/SHwoDimensionChip.h"

#include "HwoLocalization.h"

#include "Input/Events.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

TSharedPtr<SWidget> FHwoChipDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(4.f, 2.f))
		[
			SNew(STextBlock)
			.Text(DisplayName)
		];
}

void SHwoDimensionChip::Construct(const FArguments& InArgs)
{
	DimensionID = InArgs._DimensionID;
	ChipText = InArgs._Text.Get();
	OnDropped = InArgs._OnDropped;

	// 彩色圆角矩形底（画刷由本控件持有，SBorder 只存裸指针）
	const FLinearColor ChipColor = InArgs._ChipColor.Get(FLinearColor(0.35f, 0.35f, 0.35f));
	BackgroundBrush = MakeShared<FSlateRoundedBoxBrush>(ChipColor, 4.f);

	TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox)

		// 维度图标
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SImage)
			.Image(InArgs._Icon.IsSet() ? InArgs._Icon.Get().GetIcon() : nullptr)
			.DesiredSizeOverride(FVector2D(14.f, 14.f))
		]

		// 维度名称（白色保证在中等明度底色上的对比度）
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(InArgs._Text)
			.ColorAndOpacity(FSlateColor(FLinearColor::White))
		];

	// × 移除钮（未绑定 OnRemoved 则不显示，空态回退 Chip 不可移除）
	if (InArgs._OnRemoved.IsBound())
	{
		FOnHwoChipRemoved OnRemoved = InArgs._OnRemoved;
		Content->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.f, 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(HWO_TEXT("Chip.RemoveDimensionTooltip", "Remove this dimension", "移除该维度"))
			.OnClicked_Lambda([OnRemoved, DimensionID = DimensionID]()
			{
				OnRemoved.ExecuteIfBound(DimensionID);
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.X"))
				.DesiredSizeOverride(FVector2D(10.f, 10.f))
			]
		];
	}

	SBorder::Construct(SBorder::FArguments()
		.BorderImage(BackgroundBrush.Get())
		.Padding(FMargin(8.f, 3.f))
		[
			Content
		]);
}

FReply SHwoDimensionChip::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (OnDropped.IsBound() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	}
	return SBorder::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SHwoDimensionChip::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (OnDropped.IsBound())
	{
		return FReply::Handled().BeginDragDrop(FHwoChipDragDropOp::New(DimensionID, ChipText));
	}
	return SBorder::OnDragDetected(MyGeometry, MouseEvent);
}

FReply SHwoDimensionChip::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (OnDropped.IsBound())
	{
		if (TSharedPtr<FHwoChipDragDropOp> Operation = DragDropEvent.GetOperationAs<FHwoChipDragDropOp>())
		{
			if (Operation->DimensionID != DimensionID)
			{
				OnDropped.Execute(DimensionID, Operation->DimensionID);
				return FReply::Handled();
			}
		}
	}
	return SBorder::OnDrop(MyGeometry, DragDropEvent);
}
