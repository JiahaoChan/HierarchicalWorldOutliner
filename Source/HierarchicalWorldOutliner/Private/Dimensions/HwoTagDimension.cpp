// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "Dimensions/HwoBuiltInDimensions.h"

#include "HwoLocalization.h"

#include "Styling/AppStyle.h"

FName FHwoTagDimension::GetDimensionID() const
{
	return FName(TEXT("Tag"));
}

FText FHwoTagDimension::GetDisplayName() const
{
	return HWO_TEXT("Dim.Tag.Name", "Tag", "标签");
}

FSlateIcon FHwoTagDimension::GetIcon() const
{
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Tag");
}

void FHwoTagDimension::GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const
{
	const FName DimensionID = GetDimensionID();
	if (AActor* Actor = Ctx.Actor)
	{
		for (const FName& Tag : Actor->Tags)
		{
			if (!Tag.IsNone())
			{
				OutKeys.Add(FHwoGroupKey{ DimensionID, Tag.ToString() });
			}
		}
	}
	else if (Ctx.DescInstance)
	{
		for (const FName& Tag : Ctx.DescInstance->GetTags())
		{
			if (!Tag.IsNone())
			{
				OutKeys.Add(FHwoGroupKey{ DimensionID, Tag.ToString() });
			}
		}
	}
}

FText FHwoTagDimension::GetGroupDisplayName(const FHwoGroupKey& Key) const
{
	if (Key.Key == HwoGroupKeys::Unassigned)
	{
		return FText::Format(HWO_TEXT("Dim.Tag.Unassigned", "(No {0})", "(未分配 {0})"), GetDisplayName());
	}
	return FText::FromString(Key.Key);
}
