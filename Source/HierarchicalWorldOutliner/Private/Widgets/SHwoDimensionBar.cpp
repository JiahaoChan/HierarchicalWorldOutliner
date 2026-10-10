// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "Widgets/SHwoDimensionBar.h"

#include "Dimensions/HwoBuiltInDimensions.h"
#include "Dimensions/HwoDimensionStack.h"
#include "HwoLocalization.h"
#include "Widgets/SHwoDimensionChip.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

SHwoDimensionBar::~SHwoDimensionBar()
{
	if (Stack.IsValid())
	{
		Stack->OnStackChanged.RemoveAll(this);
	}
}

void SHwoDimensionBar::Construct(const FArguments& InArgs)
{
	Stack = InArgs._Stack;
	check(Stack.IsValid());

	// OnStackChanged 可能在 Slate 事件路由中广播，中转函数里延迟一帧重建（见 HandleStackChanged）
	Stack->OnStackChanged.AddSP(this, &SHwoDimensionBar::HandleStackChanged);

	ChildSlot
	[
		SNew(SHorizontalBox)

		// Chip 列表
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SAssignNew(ChipBox, SHorizontalBox)
		]

		// [+] 添加维度
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.f, 0.f, 0.f, 0.f)
		[
			SNew(SComboButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(FText::Format(HWO_TEXT("ChipBar.AddDimensionTooltip", "Add Dimension (up to {0} stacked)", "添加维度（最多 {0} 层）"), FHwoDimensionStack::MaxStackDepth))
			.OnGetMenuContent(this, &SHwoDimensionBar::BuildAddDimensionMenuContent)
			.ButtonContent()
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Plus"))
				.DesiredSizeOverride(FVector2D(14.f, 14.f))
			]
		]
	];

	RebuildChips();
}

void SHwoDimensionBar::HandleStackChanged()
{
	// OnStackChanged 可能在 Slate 事件路由中广播（Chip 的 × 点击 / 拖拽放下）——事件路由中
	// 结构性销毁控件（RebuildChips 里 ClearChildren 会析构正在路由事件的控件子树）会崩溃，
	// 必须延迟一帧重建；一帧内多次变更由 bChipRebuildPending 合并为一个定时器。
	// 定时器挂在控件上：控件销毁则不再触发，天然安全
	if (bChipRebuildPending)
	{
		return;
	}
	bChipRebuildPending = true;
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double InCurrentTime, float InDeltaTime)
	{
		bChipRebuildPending = false;
		RebuildChips();
		return EActiveTimerReturnType::Stop;
	}));
}

void SHwoDimensionBar::RebuildChips()
{
	if (!ChipBox.IsValid() || !Stack.IsValid())
	{
		return;
	}

	ChipBox->ClearChildren();

	TArray<TSharedRef<IHwoDimension>> EffectiveDims;
	Stack->GetEffectiveDimensions(EffectiveDims);

	const bool bIsFallback = Stack->IsEmpty();
	for (const TSharedRef<IHwoDimension>& Dim : EffectiveDims)
	{
		const FName DimensionID = Dim->GetDimensionID();

		// 回退 Chip 不可移除、不可拖拽（D3）
		ChipBox->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SHwoDimensionChip)
			.DimensionID(DimensionID)
			.Text(Dim->GetDisplayName())
			.Icon(Dim->GetIcon())
			.ChipColor(GetChipColor(DimensionID))
			.OnRemoved(bIsFallback ? SHwoDimensionChip::FOnHwoChipRemoved() : SHwoDimensionChip::FOnHwoChipRemoved::CreateSP(this, &SHwoDimensionBar::OnRemoveDimension))
			.OnDropped(bIsFallback ? SHwoDimensionChip::FOnHwoChipDropped() : SHwoDimensionChip::FOnHwoChipDropped::CreateSP(this, &SHwoDimensionBar::OnChipDroppedOn))
		];
	}
}

FLinearColor SHwoDimensionBar::GetChipColor(FName DimensionID)
{
	// 固定调色板（HSV8：S=220 高饱和、V=130 低明度，白字可读、跨会话稳定），九个内置维度各占一色相
	static const TMap<FName, FLinearColor> Palette = {
		{ FName(TEXT("DataLayer")),       FLinearColor::MakeFromHSV8(142, 220, 130) },	// 青蓝
		{ FName(TEXT("Class")),           FLinearColor::MakeFromHSV8(187, 220, 130) },	// 紫
		{ FName(TEXT("Folder")),          FLinearColor::MakeFromHSV8(23, 220, 130) },		// 琥珀
		{ FName(TEXT("Tag")),             FLinearColor::MakeFromHSV8(99, 220, 130) },		// 绿
		{ FName(TEXT("HLOD")),            FLinearColor::MakeFromHSV8(230, 220, 130) },	// 品红
		{ FName(TEXT("Unsaved")),         FLinearColor::MakeFromHSV8(0, 220, 130) },		// 红
		{ FName(TEXT("LocationVolume")),  FLinearColor::MakeFromHSV8(60, 220, 130) },		// 黄绿
		{ FName(TEXT("RuntimeGrid")),     FLinearColor::MakeFromHSV8(120, 220, 130) },	// 蓝绿
		{ FName(TEXT("SourceControl")),   FLinearColor::MakeFromHSV8(165, 220, 130) },	// 蓝紫
	};

	if (const FLinearColor* Color = Palette.Find(DimensionID))
	{
		return *Color;
	}

	// 未知维度（未来扩展）按 ID 哈希取色相，S/V 与调色板一致，结果同样稳定
	const uint8 Hue = static_cast<uint8>(GetTypeHash(DimensionID) & 0xFF);
	return FLinearColor::MakeFromHSV8(Hue, 220, 130);
}

TSharedRef<SWidget> SHwoDimensionBar::BuildAddDimensionMenuContent()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	for (const FName DimensionID : HwoBuiltInDimensions::GetDimensionIDs())
	{
		TSharedRef<IHwoDimension> Dim = HwoBuiltInDimensions::CreateDimension(DimensionID);

		const bool bAlreadyStacked = Stack->Contains(DimensionID);
		const bool bStackFull = Stack->GetDimensions().Num() >= FHwoDimensionStack::MaxStackDepth;

		MenuBuilder.AddMenuEntry(
			Dim->GetDisplayName(),
			bAlreadyStacked
				? HWO_TEXT("ChipBar.DimensionAlreadyStacked", "This dimension is already stacked", "该维度已在堆叠中")
				: FText::Format(HWO_TEXT("ChipBar.AddDimensionEntryTooltip", "Group by {0}", "按 {0} 分组"), Dim->GetDisplayName()),
			Dim->GetIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SHwoDimensionBar::OnAddDimension, DimensionID),
				FCanExecuteAction::CreateLambda([bAlreadyStacked, bStackFull]() { return !bAlreadyStacked && !bStackFull; })
			)
		);
	}

	return MenuBuilder.MakeWidget();
}

void SHwoDimensionBar::OnAddDimension(FName DimensionID)
{
	Stack->AddDimension(HwoBuiltInDimensions::CreateDimension(DimensionID));
}

void SHwoDimensionBar::OnRemoveDimension(FName DimensionID)
{
	Stack->RemoveDimension(DimensionID);
}

void SHwoDimensionBar::OnChipDroppedOn(FName TargetID, FName DraggedID)
{
	Stack->MoveDimensionBefore(DraggedID, TargetID);
}
