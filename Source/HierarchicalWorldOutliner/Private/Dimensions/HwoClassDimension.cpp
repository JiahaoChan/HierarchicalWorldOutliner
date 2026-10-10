// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "Dimensions/HwoBuiltInDimensions.h"

#include "HwoLocalization.h"

#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"
#include "UObject/UObjectBaseUtility.h"
#include "UObject/UObjectGlobals.h"

FName FHwoClassDimension::GetDimensionID() const
{
	return FName(TEXT("Class"));
}

FText FHwoClassDimension::GetDisplayName() const
{
	return HWO_TEXT("Dim.Class.Name", "Class", "类");
}

FSlateIcon FHwoClassDimension::GetIcon() const
{
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Default");
}

void FHwoClassDimension::GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const
{
	// TODO(v0.2): 支持可配置的"基类深度"（按第 N 层基类分组）
	//
	// 口径说明（S2 根治）：两侧统一取"父原生类"路径——
	// 未加载侧 FWorldPartitionActorDesc::GetActorNativeClass() 在 Init 时就是
	// GetParentNativeClass(ActorClass)（WorldPartitionActorDesc.cpp:105），
	// 已加载侧若直接用 Actor->GetClass()，BP 类 Actor 会与未加载侧键不一致
	// （加载/卸载互换时叶子 ID 对不上）。故已加载侧同样走 GetParentNativeClass。
	if (UClass* ActorClass = Ctx.GetNativeClass())
	{
		UClass* NativeClass = Ctx.IsLoaded() ? GetParentNativeClass(ActorClass) : ActorClass;
		if (NativeClass)
		{
			OutKeys.Add(FHwoGroupKey{ GetDimensionID(), NativeClass->GetPathName() });
		}
	}
}

FText FHwoClassDimension::GetGroupDisplayName(const FHwoGroupKey& Key) const
{
	if (Key.Key == HwoGroupKeys::Unassigned)
	{
		return FText::Format(HWO_TEXT("Dim.Class.Unassigned", "(No {0})", "(未分配 {0})"), GetDisplayName());
	}

	// 键为类路径名（"/Script/Engine.PointLight"），显示短类名
	FString ShortName = Key.Key;
	int32 DotIndex = INDEX_NONE;
	if (ShortName.FindLastChar(TEXT('.'), DotIndex))
	{
		ShortName.RightChopInline(DotIndex + 1);
	}
	return FText::FromString(ShortName);
}

const FSlateBrush* FHwoClassDimension::GetGroupIcon(const FHwoGroupKey& Key, bool bExpanded, bool bHasChildren) const
{
	// 未分配组没有对应类，回退维度静态图标
	if (Key.Key == HwoGroupKeys::Unassigned)
	{
		return IHwoDimension::GetGroupIcon(Key, bExpanded, bHasChildren);
	}

	if (const FSlateIcon* CachedIcon = GroupIconCache.Find(Key.Key))
	{
		return CachedIcon->GetIcon();
	}

	// 键为原生类路径（"/Script/Engine.PointLight"），原生类常驻内存可直接 FindObject；
	// FindIconForClass 沿类继承链上溯找专属图标（如 ClassIcon.PointLight），找不到回退 ClassIcon.Default
	const FSlateIcon Icon = FSlateIconFinder::FindIconForClass(FindObject<UClass>(nullptr, *Key.Key), TEXT("ClassIcon.Default"));
	return GroupIconCache.Add(Key.Key, Icon).GetIcon();
}
