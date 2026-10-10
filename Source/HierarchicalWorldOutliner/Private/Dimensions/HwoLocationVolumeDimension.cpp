// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "Dimensions/HwoBuiltInDimensions.h"

#include "HwoLocalization.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "LocationVolume.h"
#include "Styling/SlateIconFinder.h"

FText FHwoLocationVolumeDimension::GetDisplayName() const
{
	return HWO_TEXT("Dim.LocationVolume.Name", "Location Volume", "位置体积");
}

FSlateIcon FHwoLocationVolumeDimension::GetIcon() const
{
	return FSlateIconFinder::FindIconForClass(ALocationVolume::StaticClass());
}

void FHwoLocationVolumeDimension::GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const
{
	// Actor 包围盒：已加载侧取流送包围盒的编辑器部分（与 WP 按 Volume 包围盒加载 cell 的语义对齐），
	// 未加载侧取 GetEditorBounds
	FBox ActorBounds(ForceInit);
	UWorld* World = nullptr;
	if (const AActor* Actor = Ctx.Actor)
	{
		FBox RuntimeBounds(ForceInit);
		Actor->GetStreamingBounds(RuntimeBounds, ActorBounds);
		World = Actor->GetWorld();
	}
	else if (Ctx.DescInstance)
	{
		ActorBounds = Ctx.DescInstance->GetEditorBounds();
		// 未加载侧没有 World 通道，退回编辑器当前世界
		World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}
	if (!World || !ActorBounds.IsValid)
	{
		return;
	}

	RefreshVolumeCache(World);

	// 与多个 Volume 相交 → 多键（框架已支持多键）；都不相交 → 无键（未分配组）
	for (const FCachedVolume& Entry : CachedVolumes)
	{
		const ALocationVolume* Volume = Entry.Volume.Get();
		if (Volume && Entry.EditorBounds.IsValid && Entry.EditorBounds.Intersect(ActorBounds))
		{
			OutKeys.Add(FHwoGroupKey{ GetDimensionID(), Volume->GetActorLabel() });
		}
	}
}

FText FHwoLocationVolumeDimension::GetGroupDisplayName(const FHwoGroupKey& Key) const
{
	if (Key.Key == HwoGroupKeys::Unassigned)
	{
		return FText::Format(HWO_TEXT("Dim.LocationVolume.Unassigned", "(Unassigned {0})", "(未分配 {0})"), GetDisplayName());
	}
	// 键即 Volume 的 Actor Label，直接显示
	return FText::FromString(Key.Key);
}

void FHwoLocationVolumeDimension::RefreshVolumeCache(UWorld* World) const
{
	// 帧缓存：建树在同一帧内跑完，一帧只枚举一次；世界或帧号变化才重建（规避逐 Actor 枚举的 O(N²)）
	if (CachedWorld.Get() == World && CachedFrame == GFrameCounter)
	{
		return;
	}
	CachedWorld = World;
	CachedFrame = GFrameCounter;
	CachedVolumes.Reset();

	for (TActorIterator<ALocationVolume> It(World); It; ++It)
	{
		ALocationVolume* Volume = *It;
		if (!IsValid(Volume))
		{
			continue;
		}

		FBox RuntimeBounds(ForceInit);
		FBox EditorBounds(ForceInit);
		Volume->GetStreamingBounds(RuntimeBounds, EditorBounds);
		CachedVolumes.Add(FCachedVolume{ Volume, EditorBounds });
	}
}
