// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "Dimensions/HwoDimensionTypes.h"

#include "Misc/ArchiveMD5.h"

namespace HwoTreeItemId
{
	/**
	 *	转义后追加字符串：键内出现的 \、|、=、# 分隔符前补 \ 转义，保证路径串可无损解析
	 *
	 *	@param	Out		目标字符串
	 *	@param	In		待追加的原始字符串
	 */
	static void AppendEscaped(FString& Out, const FString& In)
	{
		for (const TCHAR Ch : In)
		{
			if (Ch == TEXT('\\') || Ch == TEXT('|') || Ch == TEXT('=') || Ch == TEXT('#'))
			{
				Out.AppendChar(TEXT('\\'));
			}
			Out.AppendChar(Ch);
		}
	}

	/**
	 *	把键路径序列化为稳定字符串："DimA=KeyA|DimB=KeyB"（键内含分隔符会被转义）
	 *
	 *	@param	Path	分组键路径
	 *	@return	序列化后的路径字符串
	 */
	FString SerializePath(const TArray<FHwoGroupKey>& Path)
	{
		FString Result;
		for (const FHwoGroupKey& Key : Path)
		{
			if (!Result.IsEmpty())
			{
				Result.AppendChar(TEXT('|'));
			}
			AppendEscaped(Result, Key.DimensionID.ToString());
			Result.AppendChar(TEXT('='));
			AppendEscaped(Result, Key.Key);
		}
		return Result;
	}

	/**
	 *	把序列化字符串经 FArchiveMD5 哈希为树节点 ID
	 *
	 *	@param	Serialized		已序列化的路径串
	 *	@return	由 MD5 哈希派生的稳定树节点 ID
	 */
	static FSceneOutlinerTreeItemID HashToID(const FString& Serialized)
	{
		// 先例 FDataLayerActorTreeItem::ComputeTreeItemID（DataLayerActorTreeItem.h:51）
		FArchiveMD5 Ar;
		FString MutableCopy = Serialized;
		Ar << MutableCopy;
		return FSceneOutlinerTreeItemID(Ar.GetGuidFromHash());
	}

	/**
	 *	计算分组节点 ID：MD5("G|" + 路径串)
	 *
	 *	@param	Path	分组键路径
	 *	@return	全树唯一且跨刷新稳定的分组节点 ID
	 */
	FSceneOutlinerTreeItemID ForGroup(const TArray<FHwoGroupKey>& Path)
	{
		return HashToID(FString(TEXT("G|")) + SerializePath(Path));
	}

	/**
	 *	计算叶子节点 ID：MD5("L|" + 路径串，再写入 ActorGuid)，同一 Actor 每条路径一片叶子
	 *
	 *	@param	Path		分组键路径
	 *	@param	ActorGuid	Actor 的 Guid
	 *	@return	全树唯一且跨刷新稳定的叶子节点 ID
	 */
	FSceneOutlinerTreeItemID ForLeaf(const TArray<FHwoGroupKey>& Path, const FGuid& ActorGuid)
	{
		FArchiveMD5 Ar;
		FString Serialized = FString(TEXT("L|")) + SerializePath(Path);
		FGuid MutableGuid = ActorGuid;
		Ar << Serialized;
		Ar << MutableGuid;
		return FSceneOutlinerTreeItemID(Ar.GetGuidFromHash());
	}
}
