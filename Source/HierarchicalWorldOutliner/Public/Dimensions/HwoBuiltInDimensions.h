// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Dimensions/IHwoDimension.h"

class ALocationVolume;

/**
 * DataLayer 维度：按 Actor 所属的 DataLayer 实例分组
 *
 * 内置维度之一，由 HwoBuiltInDimensions::CreateDimension 按 ID 创建，FHwoDimensionStack 持有。
 * 已加载 Actor 取 AActor::GetDataLayerInstances，WP 未加载 Actor 取 ActorDescInstance 的
 * GetDataLayerInstanceNames，两侧键均为 DataLayer 实例名，保证加载 / 卸载互换时分组一致。
 * 一个 Actor 可属多个 DataLayer，会产出多个分组键。
 */
class FHwoDataLayerDimension : public IHwoDimension
{
public:
	virtual FName GetDimensionID() const override;

	virtual FText GetDisplayName() const override;

	virtual FSlateIcon GetIcon() const override;

	virtual void GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const override;

	virtual FText GetGroupDisplayName(const FHwoGroupKey& Key) const override;
};

/**
 * 类维度：按 Actor 的父原生类路径分组
 *
 * 内置维度之一。已加载 Actor 取 GetClass 后再上溯父原生类，WP 未加载 Actor 直接取
 * ActorDescInstance 的 GetActorNativeClass（其值本就是父原生类），两侧口径统一为
 * 父原生类路径（如 "/Script/Engine.PointLight"），显示时映射回短类名。
 * 分组图标覆写为类的实际图标（FSlateIconFinder 沿继承链上溯，结果缓存）。
 */
class FHwoClassDimension : public IHwoDimension
{
private:
	// 分组图标缓存：FindIconForClass 会沿类继承链上溯查找，按类路径名缓存避免逐帧重复查找
	mutable TMap<FString, FSlateIcon> GroupIconCache;

public:
	virtual FName GetDimensionID() const override;

	virtual FText GetDisplayName() const override;

	virtual FSlateIcon GetIcon() const override;

	virtual void GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const override;

	virtual FText GetGroupDisplayName(const FHwoGroupKey& Key) const override;

	virtual const FSlateBrush* GetGroupIcon(const FHwoGroupKey& Key, bool bExpanded, bool bHasChildren) const override;
};

/**
 * 文件夹维度：按 Actor 的文件夹路径分组
 *
 * 内置维度之一，已加载 / 未加载两侧统一取 GetFolderPath。
 * 同时是维度栈为空时的回退维度（D3，见 FHwoDimensionStack::GetFallbackFolderDimension）。
 * 分组图标覆写为随展开状态在 Open / Closed 间切换（先例 ActorFolderTreeItem.cpp:136-145）。
 * 完整路径键（"A/B"）经 ExpandKeyToPathSegments 拆成多段，树上呈现真实嵌套（2026-10-12 起，
 * 此前为完整路径单段键平铺）。
 */
class FHwoFolderDimension : public IHwoDimension
{
public:
	// 维度 ID 的静态取口：供不持有维度实例的调用方比对分组路径来源（如 FHwoMode 的新建文件夹 selector）
	static FName StaticDimensionID() { return FName(TEXT("Folder")); }

	virtual FName GetDimensionID() const override { return StaticDimensionID(); }

	virtual FText GetDisplayName() const override;

	virtual FSlateIcon GetIcon() const override;

	virtual void GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const override;

	virtual FText GetGroupDisplayName(const FHwoGroupKey& Key) const override;

	virtual void ExpandKeyToPathSegments(const FHwoGroupKey& Key, TArray<FHwoGroupKey>& OutSegments) const override;

	virtual const FSlateBrush* GetGroupIcon(const FHwoGroupKey& Key, bool bExpanded, bool bHasChildren) const override;
};

/**
 * 标签维度：按 Actor 的标签分组
 *
 * 内置维度之一。已加载 Actor 取 AActor::Tags，WP 未加载 Actor 取 ActorDescInstance 的 GetTags。
 * 一个 Actor 可有多个标签，会产出多个分组键。
 */
class FHwoTagDimension : public IHwoDimension
{
public:
	virtual FName GetDimensionID() const override;

	virtual FText GetDisplayName() const override;

	virtual FSlateIcon GetIcon() const override;

	virtual void GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const override;

	virtual FText GetGroupDisplayName(const FHwoGroupKey& Key) const override;
};

class ALocationVolume;

/**
 * HLOD 层维度：按 Actor 所属的 HLOD 层分组
 *
 * 内置维度之一。归属优先级：显式层资产 → World Partition 默认层 → "未分配"；
 * 不参与 HLOD 的 Actor 归入"（不参与 HLOD）"特殊分组。
 * 两侧键统一为层资产的长包名（"/Game/.../L_Xxx"），显示时映射回短名。
 */
class FHwoHLODLayerDimension : public IHwoDimension
{
public:
	virtual FName GetDimensionID() const override;

	virtual FText GetDisplayName() const override;

	virtual FSlateIcon GetIcon() const override;

	virtual void GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const override;

	virtual FText GetGroupDisplayName(const FHwoGroupKey& Key) const override;
};

/**
 * 是否保存维度：按 Actor 外部包是否有未保存修改二分分组（"未保存" / "已保存"）
 *
 * 内置维度之一。判定与原生未保存列同源：包名 → USourceControlHelpers::PackageFilename
 * → FUnsavedAssetsTrackerModule::IsAssetUnsaved。已加载侧取 GetSceneOutlinerItemPackage
 * （与原生列读的 FActorTreeItem::GetPackageName 一致），未加载侧取 ActorDescInstance 的
 * GetActorPackage；包名取不到 → 未分配组。
 */
class FHwoUnsavedDimension : public IHwoDimension
{
private:
	// "未保存"分组的染色画刷：懒初始化（首次 GetGroupIcon 时从静态图标拷贝并改 TintColor），随维度实例存活
	mutable TSharedPtr<FSlateBrush> UnsavedGroupBrush;

	// 同上，"已保存"分组
	mutable TSharedPtr<FSlateBrush> SavedGroupBrush;

public:
	// 维度 ID 的静态取口：供不持有维度实例的调用方比对（如 FHwoHierarchy 的事件守卫）
	static FName StaticDimensionID() { return FName(TEXT("Unsaved")); }

	// "未保存"分组的键（稳定常量串；供排序等外部判定用，如 FHwoMode::GetTypeSortPriority）
	static const TCHAR* GetUnsavedGroupKey();

	virtual FName GetDimensionID() const override { return StaticDimensionID(); }

	virtual FText GetDisplayName() const override;

	virtual FSlateIcon GetIcon() const override;

	virtual void GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const override;

	virtual FText GetGroupDisplayName(const FHwoGroupKey& Key) const override;

	virtual const FSlateBrush* GetGroupIcon(const FHwoGroupKey& Key, bool bExpanded, bool bHasChildren) const override;
};

/**
 * LocationVolume 维度：按 Actor 包围盒与 ALocationVolume 包围盒的相交关系分组
 *
 * 内置维度之一。判定刻意用包围盒相交（保守 AABB 语义），与 WP 加载器按 Volume 包围盒
 * 加载 cell 的语义对齐，不做 brush 精确判定。可与多个 Volume 相交 → 多键分组；
 * 都不相交 → 未分配组。键为 Volume 的 Actor Label 字符串，同名 Volume 会合并为一组。
 * Volume 枚举结果按（世界， 帧号）缓存，建树一帧内只枚举一次，规避逐 Actor 枚举的 O(N²)。
 */
class FHwoLocationVolumeDimension : public IHwoDimension
{
private:
	// 缓存的单条 Volume 记录（弱引用 + 编辑器包围盒）
	struct FCachedVolume
	{
		TWeakObjectPtr<ALocationVolume> Volume;
		FBox EditorBounds;
	};

	// 缓存所属世界（弱引用防悬空）
	mutable TWeakObjectPtr<UWorld> CachedWorld;

	// 缓存建立时的帧号（与 CachedWorld 共同判定缓存有效性）
	mutable uint64 CachedFrame = 0;

	// 当前世界的全部 LocationVolume 缓存
	mutable TArray<FCachedVolume> CachedVolumes;

public:
	// 维度 ID 的静态取口：供不持有维度实例的调用方比对（如 FHwoHierarchy 的事件守卫）
	static FName StaticDimensionID() { return FName(TEXT("LocationVolume")); }

	virtual FName GetDimensionID() const override { return StaticDimensionID(); }

	virtual FText GetDisplayName() const override;

	virtual FSlateIcon GetIcon() const override;

	virtual void GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const override;

	virtual FText GetGroupDisplayName(const FHwoGroupKey& Key) const override;

private:
	/**
	 *	刷新 LocationVolume 缓存（世界或帧号变化才重建）
	 *
	 *	@param	World	要枚举的世界
	 */
	void RefreshVolumeCache(UWorld* World) const;
};

/**
 * 空间网格维度：按 WP Runtime Grid 单元分组（"Grid 名 (X, Y)"）
 *
 * 内置维度之一。归属规则：世界非 WP 或 RuntimeHash 非 UWorldPartitionRuntimeSpatialHash → 未分配组；
 * 非空间加载的 Actor（GetIsSpatiallyLoaded()==false）→"非空间加载"特殊组；其余按 Grid 名
 * （NAME_None 回落第一个 Grid 配置，与引擎同约定）+ 包围盒中心点的 Cell 坐标分组。
 * Cell 坐标用 GetFixedGridInfo 的 Origin / CellSize 做 floor 除法——未套用引擎 GetCellCoords 的
 * GridSize 居中偏移（GridSize 在流送生成阶段才稳定，编辑器内不可得），坐标为相对 Origin 的
 * Cell 索引，仅用于分组展示。不按盒尺寸分层级（引擎流送会给大物体放粗层级，展示分组只用最细层）。
 * 取舍：已加载侧无 ForceNonSpatiallyLoaded 取口（该状态仅存于 Desc 侧），已加载侧只按
 * GetIsSpatiallyLoaded 判定。
 */
class FHwoRuntimeGridDimension : public IHwoDimension
{
public:
	// 维度 ID 的静态取口：供不持有维度实例的调用方比对（如 FHwoHierarchy 的事件守卫）
	static FName StaticDimensionID() { return FName(TEXT("RuntimeGrid")); }

	// "非空间加载"分组的键（稳定常量串；供排序等外部判定用，如 FHwoMode::GetTypeSortPriority）
	static const TCHAR* GetNonSpatiallyLoadedGroupKey();

	virtual FName GetDimensionID() const override { return StaticDimensionID(); }

	virtual FText GetDisplayName() const override;

	virtual FSlateIcon GetIcon() const override;

	virtual void GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const override;

	virtual FText GetGroupDisplayName(const FHwoGroupKey& Key) const override;
};

/**
 * 版本控制维度：按 Actor 外部包的 Source Control 状态分组
 *
 * 内置维度之一。包名取法与 Unsaved 维度完全同链（已加载 GetSceneOutlinerItemPackage /
 * 未加载 GetActorPackage → USourceControlHelpers::PackageFilename）。状态读取全部走
 * ISourceControlProvider::GetState(Use) 纯缓存（绝不触网；ForceUpdate 同步阻塞，禁用）；
 * 缓存未命中时顺手 QueueStatusUpdate 懒回填（模块自带节流批处理与新鲜度去重）。
 * 组键按优先级唯一归组（见 HwoSourceControlDimension.cpp）。
 * 取舍：不用 FSourceControlFileStatusMonitor（v0.2 大世界优化候选）；组排序走默认字典序；
 * KeyCache 不随切图清理（陈旧条目无害）。
 */
class FHwoSourceControlDimension : public IHwoDimension
{
private:
	// 包文件名 → 组键缓存（变化检测用；不随切图清理，陈旧条目无害）
	mutable TMap<FString, FString> KeyCache;

public:
	// 维度 ID 的静态取口：供不持有维度实例的调用方比对（如 FHwoHierarchy 的事件守卫）
	static FName StaticDimensionID() { return FName(TEXT("SourceControl")); }

	virtual FName GetDimensionID() const override { return StaticDimensionID(); }

	virtual FText GetDisplayName() const override;

	virtual FSlateIcon GetIcon() const override;

	virtual void GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const override;

	virtual FText GetGroupDisplayName(const FHwoGroupKey& Key) const override;

	/**
	 *	重算缓存中全部包名的组键并检测变化（SourceControlStateChanged 无参广播后由 FHwoHierarchy 调用）
	 *
	 *	@return	有任意键变化返回 true（调用方据此去抖刷新）
	 */
	bool ReevaluateKeysAndDetectChanges();

	/**
	 *	清空键缓存（Provider 切换时调用，懒回填会自然重建）
	 */
	void ClearKeyCache();

private:
	/**
	 *	计算单个包文件名的组键（纯缓存读；Provider 未启用时一律归"未知"组）
	 *
	 *	@param	Filename		包文件绝对路径
	 *	@param	bQueueBackfill	缓存未命中时是否顺手 QueueStatusUpdate 懒回填（重算路径不传，避免重复入队）
	 *	@return	组键（HwoSourceControlKeys 常量串之一）
	 */
	const TCHAR* ComputeGroupKeyForPackageFile(const FString& Filename, bool bQueueBackfill) const;
};

namespace HwoBuiltInDimensions
{
	/**
	 *	取全部内置维度 ID（Chip 栏 [+] 菜单的数据源）
	 *
	 *	@return	九个内置维度 ID 的有序列表
	 */
	TArray<FName> GetDimensionIDs();

	/**
	 *	判断指定 ID 是否为内置维度（持久化解析时的未知 ID 判定用）
	 *
	 *	@param	DimensionID		待判定的维度 ID
	 *	@return	是内置维度返回 true
	 */
	bool IsBuiltInDimensionID(FName DimensionID);

	/**
	 *	按 ID 创建内置维度实例
	 *
	 *	@param	DimensionID		内置维度 ID
	 *	@return	新建的维度实例
	 *
	 *	@warning	传入未知 ID 会触发 checkf，调用方应先以 IsBuiltInDimensionID 校验
	 */
	TSharedRef<IHwoDimension> CreateDimension(FName DimensionID);
}
