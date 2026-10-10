// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "TreeItems/HwoGroupTreeItem.h"

#include "Dimensions/HwoBuiltInDimensions.h"
#include "HwoLocalization.h"
#include "HwoMode.h"
#include "TreeItems/HwoActorDescTreeItem.h"
#include "TreeItems/HwoActorTreeItem.h"

#include "ActorFolder.h"
#include "ISceneOutliner.h"
#include "SSceneOutliner.h"
#include "SceneOutlinerPublicTypes.h"
#include "Styling/AppStyle.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

const FSceneOutlinerTreeItemType FHwoGroupTreeItem::Type(&ISceneOutlinerTreeItem::Type);

FString FHwoGroupTreeItem::GetDisplayString() const
{
	// 分组徽标方案 A（TDD §5.3）：组名 + 子代去重 Actor 计数
	return FString::Printf(TEXT("%s (%d)"), *DisplayName.ToString(), UniqueActorGuids.Num());
}

TSharedRef<SWidget> FHwoGroupTreeItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	TWeakPtr<FHwoGroupTreeItem> WeakItem = StaticCastSharedRef<FHwoGroupTreeItem>(AsShared());
	TWeakPtr<ISceneOutliner> WeakOutliner = StaticCastSharedRef<ISceneOutliner>(Outliner.AsShared());

	return SNew(SHorizontalBox)

		// 维度图标（16px 尺寸约束 + 属性驱动，可随展开状态切换，如文件夹 Open/Closed）
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FSceneOutlinerDefaultTreeItemMetrics::IconPadding())
		[
			SNew(SBox)
			.WidthOverride(FSceneOutlinerDefaultTreeItemMetrics::IconSize())
			.HeightOverride(FSceneOutlinerDefaultTreeItemMetrics::IconSize())
			[
				SNew(SImage)
				.Image_Lambda([WeakItem]() -> const FSlateBrush*
				{
					const TSharedPtr<FHwoGroupTreeItem> Item = WeakItem.Pin();
					if (Item.IsValid() && Item->Path.Num() > 0)
					{
						if (const TSharedPtr<IHwoDimension> Dim = Item->Dimension.Pin())
						{
							if (const FSlateBrush* Brush = Dim->GetGroupIcon(Item->Path.Last(), Item->Flags.bIsExpanded, Item->GetChildren().Num() > 0))
							{
								return Brush;
							}
						}
						// 维度未提供动态图标时回退到静态图标
						return Item->Icon.GetIcon();
					}
					return nullptr;
				})
			]
		]

		// 组名 + 计数徽标（属性驱动，计数变化随下次绘制刷新）
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text_Lambda([WeakItem]()
			{
				const TSharedPtr<FHwoGroupTreeItem> Item = WeakItem.Pin();
				return Item.IsValid() ? FText::FromString(Item->GetDisplayString()) : FText::GetEmpty();
			})
			.Font(FAppStyle::GetFontStyle("BoldFont"))
			.HighlightText(Outliner.GetFilterHighlightText())
			.ColorAndOpacity_Lambda([WeakItem, WeakOutliner]() -> FSlateColor
			{
				const TSharedPtr<FHwoGroupTreeItem> Item = WeakItem.Pin();
				const TSharedPtr<ISceneOutliner> OutlinerPin = WeakOutliner.Pin();
				if (Item.IsValid() && OutlinerPin.IsValid())
				{
					const TOptional<FLinearColor> Color = FSceneOutlinerCommonLabelData::GetForegroundColor(WeakOutliner, *Item);
					if (Color.IsSet())
					{
						return FSlateColor(Color.GetValue());
					}
				}
				return FSlateColor::UseForeground();
			})
		];
}

void FHwoGroupTreeItem::GenerateContextMenu(UToolMenu* Menu, SSceneOutliner& Outliner)
{
	FToolMenuSection& Section = Menu->AddSection("HwoGroupActions",
		FText::Format(HWO_TEXT("Menu.GroupSection", "For Group ({0} Items)", "对分组（{0} 项）"), FText::AsNumber(UniqueActorGuids.Num())));

	TWeakPtr<FHwoGroupTreeItem> WeakItem = StaticCastSharedRef<FHwoGroupTreeItem>(AsShared());
	TWeakPtr<SSceneOutliner> WeakOutliner = StaticCastSharedRef<SSceneOutliner>(Outliner.AsShared());

	Section.AddMenuEntry(
		"HwoSelectGroupActors",
		HWO_TEXT("Menu.SelectGroupActors", "Select All Actors in Group", "选中组内全部 Actor"),
		HWO_TEXT("Menu.SelectGroupActorsTooltip", "Select all de-duplicated actors in this group in the viewport (unloaded actors included).", "在视口中选中该分组内全部去重后的 Actor（含未加载 Actor）"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakItem, WeakOutliner]()
		{
			const TSharedPtr<FHwoGroupTreeItem> Item = WeakItem.Pin();
			const TSharedPtr<SSceneOutliner> OutlinerPin = WeakOutliner.Pin();
			if (!Item.IsValid() || !OutlinerPin.IsValid())
			{
				return;
			}

			TSet<AActor*> Actors;
			TArray<FWorldPartitionHandle> Handles;
			FHwoGroupTreeItem::CollectActors(*Item, Actors, Handles);

			// ISceneOutliner::GetMode() 仅提供 const 版本；此 Mode 由本插件创建，安全
			if (FHwoMode* Mode = const_cast<FHwoMode*>(static_cast<const FHwoMode*>(OutlinerPin->GetMode())))
			{
				Mode->SelectActorsInViewport(Actors, Handles);
			}
		})));
}

void FHwoGroupTreeItem::CollectActors(const ISceneOutlinerTreeItem& Item, TSet<AActor*>& OutActors, TArray<FWorldPartitionHandle>& OutHandles)
{
	if (const FHwoActorTreeItem* ActorItem = Item.CastTo<FHwoActorTreeItem>())
	{
		if (AActor* Actor = ActorItem->Actor.Get())
		{
			OutActors.Add(Actor);
		}
	}
	else if (const FHwoActorDescTreeItem* DescItem = Item.CastTo<FHwoActorDescTreeItem>())
	{
		if (DescItem->ActorDescHandle.IsValid())
		{
			OutHandles.Add(DescItem->ActorDescHandle);
		}
	}
	else if (Item.IsA<FHwoGroupTreeItem>())
	{
		for (const TWeakPtr<ISceneOutlinerTreeItem>& WeakChild : Item.GetChildren())
		{
			if (const FSceneOutlinerTreeItemPtr Child = WeakChild.Pin())
			{
				CollectActors(*Child, OutActors, OutHandles);
			}
		}
	}
}

bool FHwoGroupTreeItem::ShouldShowVisibilityState() const
{
	// 只还原文件夹维度分组的可视性眼睛（对齐原生文件夹节点）；
	// HasVisibilityInfo=false 时显示值为子代 OR 聚合，点击由框架递归传播到全部子代叶子
	// （未加载叶子 FActorDescTreeItem::GetVisibility 恒 false 且不可设置，与原生一致）
	return IsFolderDimensionGroup();
}

FString FHwoGroupTreeItem::GetPackageName() const
{
	// 未保存列：原生文件夹节点取 UActorFolder 的外部包（ActorFolderTreeItem.cpp:357）
	if (!IsFolderDimensionGroup())
	{
		return FString();
	}

	UWorld* WorldPtr = World.Get();
	const FString FolderPath = ReconstructFolderPath();
	if (!WorldPtr || FolderPath.IsEmpty())
	{
		return FString();
	}

	const FFolder Folder(FFolder::GetWorldRootFolder(WorldPtr).GetRootObject(), FName(*FolderPath));
	if (const UActorFolder* ActorFolder = Folder.GetActorFolder())
	{
		if (ActorFolder->IsPackageExternal())
		{
			return ActorFolder->GetExternalPackage()->GetName();
		}
	}
	return FString();
}

bool FHwoGroupTreeItem::IsFolderDimensionGroup() const
{
	return !Path.IsEmpty() && Path.Last().DimensionID == FHwoFolderDimension::StaticDimensionID();
}

FString FHwoGroupTreeItem::ReconstructFolderPath() const
{
	// Path 即祖先链的扁平展开：滤出文件夹维度的段（跳过"未分配"占位段）按序拼接
	FString FolderPath;
	for (const FHwoGroupKey& Key : Path)
	{
		if (Key.DimensionID == FHwoFolderDimension::StaticDimensionID() && Key.Key != HwoGroupKeys::Unassigned)
		{
			if (!FolderPath.IsEmpty())
			{
				FolderPath += TEXT("/");
			}
			FolderPath += Key.Key;
		}
	}
	return FolderPath;
}
