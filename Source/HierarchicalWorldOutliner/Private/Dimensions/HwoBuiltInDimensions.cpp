// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "Dimensions/HwoBuiltInDimensions.h"

namespace HwoBuiltInDimensions
{
	/**
	 *	取全部内置维度 ID（Chip 栏 [+] 菜单的数据源）
	 *
	 *	@return	九个内置维度 ID 的有序列表
	 */
	TArray<FName> GetDimensionIDs()
	{
		return { FName(TEXT("DataLayer")), FName(TEXT("Class")), FName(TEXT("Folder")), FName(TEXT("Tag")), FName(TEXT("HLOD")), FName(TEXT("Unsaved")), FName(TEXT("LocationVolume")), FName(TEXT("RuntimeGrid")), FName(TEXT("SourceControl")) };
	}

	/**
	 *	判断指定 ID 是否为内置维度（持久化解析时的未知 ID 判定用）
	 *
	 *	@param	DimensionID		待判定的维度 ID
	 *	@return	是内置维度返回 true
	 */
	bool IsBuiltInDimensionID(FName DimensionID)
	{
		return GetDimensionIDs().Contains(DimensionID);
	}

	/**
	 *	按 ID 创建内置维度实例
	 *
	 *	@param	DimensionID		内置维度 ID
	 *	@return	新建的维度实例
	 *
	 *	@warning	传入未知 ID 会触发 checkf，调用方应先以 IsBuiltInDimensionID 校验
	 */
	TSharedRef<IHwoDimension> CreateDimension(FName DimensionID)
	{
		if (DimensionID == FName(TEXT("DataLayer")))
		{
			return MakeShared<FHwoDataLayerDimension>();
		}
		if (DimensionID == FName(TEXT("Class")))
		{
			return MakeShared<FHwoClassDimension>();
		}
		if (DimensionID == FName(TEXT("Folder")))
		{
			return MakeShared<FHwoFolderDimension>();
		}
		if (DimensionID == FName(TEXT("Tag")))
		{
			return MakeShared<FHwoTagDimension>();
		}
		if (DimensionID == FName(TEXT("HLOD")))
		{
			return MakeShared<FHwoHLODLayerDimension>();
		}
		if (DimensionID == FName(TEXT("Unsaved")))
		{
			return MakeShared<FHwoUnsavedDimension>();
		}
		if (DimensionID == FName(TEXT("LocationVolume")))
		{
			return MakeShared<FHwoLocationVolumeDimension>();
		}
		if (DimensionID == FName(TEXT("RuntimeGrid")))
		{
			return MakeShared<FHwoRuntimeGridDimension>();
		}
		if (DimensionID == FName(TEXT("SourceControl")))
		{
			return MakeShared<FHwoSourceControlDimension>();
		}
		checkf(false, TEXT("Unknown built-in dimension ID: %s"), *DimensionID.ToString());
		return MakeShared<FHwoFolderDimension>();
	}
}
