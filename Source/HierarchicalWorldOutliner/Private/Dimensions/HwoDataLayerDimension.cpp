// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "Dimensions/HwoBuiltInDimensions.h"

#include "HwoLocalization.h"

#include "Styling/AppStyle.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"

FName FHwoDataLayerDimension::GetDimensionID() const
{
	return FName(TEXT("DataLayer"));
}

FText FHwoDataLayerDimension::GetDisplayName() const
{
	return HWO_TEXT("Dim.DataLayer.Name", "Data Layer", "数据层");
}

FSlateIcon FHwoDataLayerDimension::GetIcon() const
{
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.DataLayers");
}

void FHwoDataLayerDimension::GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const
{
	const FName DimensionID = GetDimensionID();
	if (AActor* Actor = Ctx.Actor)
	{
		for (const UDataLayerInstance* DataLayerInstance : Actor->GetDataLayerInstances())
		{
			if (DataLayerInstance)
			{
				OutKeys.Add(FHwoGroupKey{ DimensionID, DataLayerInstance->GetFName().ToString() });
			}
		}
	}
	else if (Ctx.DescInstance)
	{
		for (const FName& DataLayerInstanceName : Ctx.DescInstance->GetDataLayerInstanceNames().ToArray())
		{
			OutKeys.Add(FHwoGroupKey{ DimensionID, DataLayerInstanceName.ToString() });
		}
	}
}

FText FHwoDataLayerDimension::GetGroupDisplayName(const FHwoGroupKey& Key) const
{
	if (Key.Key == HwoGroupKeys::Unassigned)
	{
		return FText::Format(HWO_TEXT("Dim.DataLayer.Unassigned", "(No {0})", "(未分配 {0})"), GetDisplayName());
	}
	return FText::FromString(Key.Key);
}
