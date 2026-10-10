// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GameFramework/Actor.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"

namespace HwoGroupKeys
{
	// "未分配"特殊分组的键值占位符（以不可显示字符 \x01 前缀，保证不与任何真实键冲突）
	inline const TCHAR* Unassigned = TEXT("\x01Unassigned");
}

/**
 * 统一 Actor 视图：已加载包 AActor*，WP 未加载包 ActorDescInstance，二者必居其一
 *
 * 只收敛"每个维度都要用"的通用字段；DataLayer / Tag 等维度特有数据由对应 Dimension 自行取数。
 * 聚合数据载体，字段允许内联默认值。
 */
struct FHwoActorContext
{
	// 已加载的 Actor 指针（与 DescInstance 互斥，非空即已加载）
	AActor* Actor = nullptr;

	// WP 未加载 Actor 的 ActorDescInstance 指针（与 Actor 互斥）
	const FWorldPartitionActorDescInstance* DescInstance = nullptr;

	bool IsLoaded() const { return Actor != nullptr; }

	FGuid GetActorGuid() const
	{
		if (Actor)
		{
			return Actor->GetActorGuid();
		}
		return DescInstance ? DescInstance->GetGuid() : FGuid();
	}

	FString GetLabel() const
	{
		if (Actor)
		{
			return Actor->GetActorLabel();
		}
		return DescInstance ? DescInstance->GetActorLabelString() : FString();
	}

	UClass* GetNativeClass() const
	{
		if (Actor)
		{
			return Actor->GetClass();
		}
		return DescInstance ? DescInstance->GetActorNativeClass() : nullptr;
	}

	FName GetFolderPath() const
	{
		if (Actor)
		{
			return Actor->GetFolderPath();
		}
		return DescInstance ? DescInstance->GetFolderPath() : NAME_None;
	}
};

/**
 * 一个分组的定位键：维度 ID + 维度内键值（如 DataLayer 名 / 类路径 / 文件夹路径 / Tag 名）
 */
struct FHwoGroupKey
{
	// 所属维度的 ID（IHwoDimension::GetDimensionID）
	FName DimensionID;

	// 维度内的键值（DataLayer 名 / 类路径 / 文件夹路径 / Tag 名等）
	FString Key;

	bool operator==(const FHwoGroupKey&) const = default;
};

/**
 *	FHwoGroupKey 的哈希函数，供 TSet / TMap 使用
 *
 *	@param	InKey	要计算哈希的分组键
 *	@return	DimensionID 与 Key 组合后的哈希值
 */
inline uint32 GetTypeHash(const FHwoGroupKey& InKey)
{
	return HashCombine(GetTypeHash(InKey.DimensionID), GetTypeHash(InKey.Key));
}

/** 树节点 ID 方案（TDD §4.7）：路径串行化 + FArchiveMD5，Guid 变体，全树唯一且跨刷新稳定 */
namespace HwoTreeItemId
{
	/**
	 *	把键路径序列化为稳定字符串："DimA=KeyA|DimB=KeyB"（键内含分隔符会被转义）
	 *
	 *	@param	Path	分组键路径
	 *	@return	序列化后的路径字符串
	 */
	FString SerializePath(const TArray<FHwoGroupKey>& Path);

	/**
	 *	计算分组节点 ID：MD5("G|" + 路径串)
	 *
	 *	@param	Path	分组键路径
	 *	@return	全树唯一且跨刷新稳定的分组节点 ID
	 */
	FSceneOutlinerTreeItemID ForGroup(const TArray<FHwoGroupKey>& Path);

	/**
	 *	计算叶子节点 ID：MD5("L|" + 路径串，再写入 ActorGuid)，同一 Actor 每条路径一片叶子
	 *
	 *	@param	Path		分组键路径
	 *	@param	ActorGuid	Actor 的 Guid
	 *	@return	全树唯一且跨刷新稳定的叶子节点 ID
	 */
	FSceneOutlinerTreeItemID ForLeaf(const TArray<FHwoGroupKey>& Path, const FGuid& ActorGuid);
}
