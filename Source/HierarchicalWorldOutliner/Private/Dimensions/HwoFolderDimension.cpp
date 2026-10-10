// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "Dimensions/HwoBuiltInDimensions.h"

#include "HwoLocalization.h"

#include "Styling/AppStyle.h"

FText FHwoFolderDimension::GetDisplayName() const
{
	return HWO_TEXT("Dim.Folder.Name", "Folder", "文件夹");
}

FSlateIcon FHwoFolderDimension::GetIcon() const
{
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderClosed");
}

void FHwoFolderDimension::GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const
{
	const FName FolderPath = Ctx.GetFolderPath();
	if (!FolderPath.IsNone())
	{
		// 键仍是完整路径（如 "A/B"）；嵌套展开在 ExpandKeyToPathSegments 完成
		OutKeys.Add(FHwoGroupKey{ GetDimensionID(), FolderPath.ToString() });
	}
}

void FHwoFolderDimension::ExpandKeyToPathSegments(const FHwoGroupKey& Key, TArray<FHwoGroupKey>& OutSegments) const
{
	// "未分配"占位键不得拆分
	if (Key.Key == HwoGroupKeys::Unassigned)
	{
		OutSegments.Add(Key);
		return;
	}

	// 完整路径按 "/" 拆成多段（"A/B" → ["A","B"]），树上呈现真实嵌套；
	// 段键只存段名，分组 ID 由全路径编码，天然与同名顶层文件夹区分。
	// bCullEmpty=true 会吞并 "A//B" 或首尾斜杠产生的空段——依赖 FActorFolders 路径规范不产生
	// 此类输入的前提（无害防御：即使出现也只是少建一级，不会错位）
	TArray<FString> Parts;
	Key.Key.ParseIntoArray(Parts, TEXT("/"), /*bCullEmpty*/true);
	for (const FString& Part : Parts)
	{
		OutSegments.Add(FHwoGroupKey{ Key.DimensionID, Part });
	}
}

FText FHwoFolderDimension::GetGroupDisplayName(const FHwoGroupKey& Key) const
{
	if (Key.Key == HwoGroupKeys::Unassigned)
	{
		return FText::Format(HWO_TEXT("Dim.Folder.Unassigned", "(No {0})", "(未分配 {0})"), GetDisplayName());
	}
	return FText::FromString(Key.Key);
}

const FSlateBrush* FHwoFolderDimension::GetGroupIcon(const FHwoGroupKey& Key, bool bExpanded, bool bHasChildren) const
{
	// 与自带大纲一致：展开且有子节点用 Open，否则 Closed（ActorFolderTreeItem.cpp:136-145）
	return FAppStyle::Get().GetBrush((bExpanded && bHasChildren) ? TEXT("SceneOutliner.FolderOpen") : TEXT("SceneOutliner.FolderClosed"));
}
