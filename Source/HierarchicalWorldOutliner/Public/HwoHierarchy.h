// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Dimensions/HwoDimensionStack.h"

#include "Containers/Ticker.h"
#include "ISceneOutlinerHierarchy.h"

class FHwoMode;
class FWorldPartitionActorDescInstance;
class ISourceControlProvider;

/**
 * 数据层：把世界中的 Actor（已加载 + WP 未加载）按维度堆叠路径展开为树节点。
 *
 * CreateItems 扁平产出全部叶子；父子关系由框架对每个节点回调 FindOrCreateParentItem 组装（TDD §4.6）。
 * 事件驱动分级刷新：Actor 增删与 WP 流送交换走增量 Added / Removed，DataLayer / 文件夹 / 地图变更走 FullRefresh。
 * 由 FHwoMode::CreateHierarchy 创建并持有，随 Mode 销毁。
 */
class FHwoHierarchy : public ISceneOutlinerHierarchy
{
private:
	// 所属行为层（反向裸指针，本对象生命周期不超过 Mode）
	FHwoMode* HwoMode = nullptr;

	// 本视图代表的 World（弱引用防悬空）
	TWeakObjectPtr<UWorld> RepresentingWorld;

	// 维度堆叠，决定叶子路径的分组维度与堆叠顺序
	TSharedRef<FHwoDimensionStack> Stack;

	// 是否枚举 WP 未加载 Actor 的叶子
	bool bShowUnloadedActors = true;

	// Actor 移动事件的 0.2s 去抖定时器句柄（RuntimeGrid 维度刷新合并用，UnbindEvents 时取消；
	// 类型是 FTSTicker 的嵌套句柄，UE5 Ticker 现代化后不通用全局 FDelegateHandle）
	FTSTicker::FDelegateHandle ActorMovedDebounceHandle;

	// SourceControl 状态变化委托句柄（注册在当前 Provider 上，UnbindEvents 时反注册）
	FDelegateHandle SourceControlStateChangedHandle;

	// SourceControl Provider 切换委托句柄（注册在 ISourceControlModule 上）
	FDelegateHandle SourceControlProviderChangedHandle;

	// SourceControl 状态批处理广播的 0.2s 去抖定时器句柄（与 ActorMovedDebounceHandle 对称，语义独立不共用）
	FTSTicker::FDelegateHandle SourceControlDebounceHandle;

public:
	virtual ~FHwoHierarchy();

private:
	FHwoHierarchy(FHwoMode* InMode, const TWeakObjectPtr<UWorld>& InWorld, TSharedRef<FHwoDimensionStack> InStack, bool bInShowUnloadedActors);

public:
	//~ Begin ISceneOutlinerHierarchy Interface
	virtual void CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const override;

	virtual void CreateChildren(const FSceneOutlinerTreeItemPtr& Item, TArray<FSceneOutlinerTreeItemPtr>& OutChildren) const override {}

	virtual FSceneOutlinerTreeItemPtr FindOrCreateParentItem(const ISceneOutlinerTreeItem& Item, const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items, bool bCreate = false) override;
	//~ End ISceneOutlinerHierarchy Interface

	/**
	 *	创建层级实例（私有构造对外的唯一入口）
	 *
	 *	@param	InMode					所属行为层
	 *	@param	InWorld					要呈现的世界（弱引用持有）
	 *	@param	InStack					维度堆叠
	 *	@param	bInShowUnloadedActors	是否枚举 WP 未加载 Actor
	 *	@return	新的层级实例
	 */
	static TUniquePtr<FHwoHierarchy> Create(FHwoMode* InMode, const TWeakObjectPtr<UWorld>& InWorld, TSharedRef<FHwoDimensionStack> InStack, bool bInShowUnloadedActors);

private:
	/**
	 *	订阅引擎 / World / WorldPartition / DataLayer 的变更事件（构造时调用一次）
	 */
	void BindEvents();

	/**
	 *	解除全部事件订阅（析构时调用）
	 */
	void UnbindEvents();

	/**
	 *	Actor 加入关卡：增量补该 Actor 的全部叶子（Added 广播）
	 *
	 *	@param	InActor	新加入的 Actor
	 */
	void OnLevelActorAdded(AActor* InActor);

	/**
	 *	Actor 离开关卡：按 ID 增量移除该 Actor 的全部叶子（Removed 广播）
	 *
	 *	@param	InActor	被移除的 Actor
	 */
	void OnLevelActorDeleted(AActor* InActor);

	/**
	 *	WP 流送加载：Desc 叶子换成 Actor 叶子（同路径同 ID，用户无感）
	 *
	 *	@param	InActor	被加载的 Actor
	 */
	void OnLoadedActorAdded(AActor& InActor);

	/**
	 *	WP 流送卸载：Actor 叶子换回 Desc 叶子
	 *
	 *	@param	InActor	被卸载的 Actor
	 */
	void OnLoadedActorRemoved(AActor& InActor);

	/**
	 *	WP 新增 ActorDesc：增量补未加载叶子
	 *
	 *	@param	InDescInstance	新增的 ActorDesc 实例
	 */
	void OnActorDescInstanceAdded(FWorldPartitionActorDescInstance* InDescInstance);

	/**
	 *	WP 移除 ActorDesc：按 ID 移除未加载叶子
	 *
	 *	@param	InDescInstance	被移除的 ActorDesc 实例
	 */
	void OnActorDescInstanceRemoved(FWorldPartitionActorDescInstance* InDescInstance);

	/**
	 *	Actor 的 DataLayer 变更：属性类变更 MVP 一律 FullRefresh（TDD §5.2 取舍）
	 *
	 *	@param	InActor	DataLayer 发生变化的 Actor
	 */
	void OnActorDataLayersChanged(const TWeakObjectPtr<AActor>& InActor);

	/**
	 *	资产未保存状态变化（UnsavedAssetsTracker）：Unsaved 维度在栈中时 FullRefresh
	 *
	 *	@param	FileAbsPathname	变化的资产文件绝对路径
	 */
	void OnUnsavedAssetChanged(const FString& FileAbsPathname);

	/**
	 *	Actor 改名（FCoreDelegates::OnActorLabelChanged）：LocationVolume 的组键即 Label，改名需 FullRefresh
	 *
	 *	@param	InActor	改名的 Actor
	 */
	void OnActorLabelChangedForHierarchy(AActor* InActor);

	/**
	 *	Actor 移动（UEngine::OnActorMoved）：RuntimeGrid 维度的 Cell 归属可能变化；拖动期间高频触发，
	 *	去抖 0.2s 合并后 FullRefresh
	 *
	 *	@param	InActor	移动的 Actor
	 */
	void OnActorMoved(AActor* InActor);

	/**
	 *	SourceControl 状态变化（无参广播，每批回填落库后触发）：键级变化检测 + 去抖 FullRefresh
	 */
	void OnSourceControlStateChanged();

	/**
	 *	SourceControl Provider 切换：state-changed 句柄随迁到新 Provider，清键缓存并 FullRefresh
	 *
	 *	@param	OldProvider	旧 Provider（从其委托列表反注册 state-changed 句柄）
	 *	@param	NewProvider	新 Provider（向其注册 state-changed 句柄）
	 */
	void OnSourceControlProviderChanged(ISourceControlProvider& OldProvider, ISourceControlProvider& NewProvider);

	/**
	 *	Actor 文件夹路径变更：FullRefresh
	 *
	 *	@param	InActor	文件夹发生变化的 Actor
	 *	@param	OldPath	变更前的文件夹路径（未使用）
	 */
	void OnLevelActorFolderChanged(const AActor* InActor, FName OldPath);

	/**
	 *	文件夹创建 / 删除 / 移动（FActorFolders 静态委托）：空文件夹事件不经 Actor，故单独监听，FullRefresh
	 *
	 *	@param	InWorld		事件发生的世界
	 *	@param	InFolder	变化的文件夹（未使用）
	 */
	void OnFolderCreatedOrDeleted(UWorld& InWorld, const FFolder& InFolder);

	/**
	 *	文件夹移动（src → dst）：FullRefresh
	 *
	 *	@param	InWorld	事件发生的世界
	 *	@param	Src		源路径（未使用）
	 *	@param	Dst		目标路径（未使用）
	 */
	void OnFolderMoved(UWorld& InWorld, const FFolder& Src, const FFolder& Dst);

	/**
	 *	地图变更（切换 / 新建 / 保存等）：FullRefresh
	 *
	 *	@param	MapFlags	变更标志（未使用）
	 */
	void OnMapChange(uint32 MapFlags);

	/**
	 *	广播 FullRefresh 层级变更事件
	 */
	void FullRefreshEvent();

	/**
	 *	为指定分组路径造一个分组节点（含维度显示名 / 图标解析）
	 *
	 *	@param	GroupPath	分组的完整路径
	 *	@param	bForce		为 true 时无视过滤器强制创建（父链按需创建用）
	 *	@return	新建的分组节点，被过滤且不强制时返回 nullptr
	 */
	FSceneOutlinerTreeItemPtr CreateGroupItemForPath(const TArray<FHwoGroupKey>& GroupPath, bool bForce) const;

	/**
	 *	文件夹维度是否为最外层有效维度（空文件夹枚举与新建文件夹选中定位共用此边界）
	 *
	 *	@return	是则返回 true
	 */
	bool IsFolderDimensionOutermost() const;

	/**
	 *	按 ComputeLeafPaths 为一个已加载 Actor 的每条路径造一片叶子
	 *
	 *	@param	InActor		要造叶子的 Actor
	 *	@param	OutItems	输出：新建的叶子节点
	 */
	void CreateLeafItemsForActor(AActor* InActor, TArray<FSceneOutlinerTreeItemPtr>& OutItems) const;

	/**
	 *	按 ComputeLeafPaths 为一个 WP 未加载 Actor 的每条路径造一片叶子
	 *
	 *	@param	InDescInstance	要造叶子的 ActorDesc 实例
	 *	@param	OutItems		输出：新建的叶子节点
	 */
	void CreateLeafItemsForActorDesc(const FWorldPartitionActorDescInstance* InDescInstance, TArray<FSceneOutlinerTreeItemPtr>& OutItems) const;

	/**
	 *	计算一个统一视图的全部叶子 ID（增量 Removed 用）
	 *
	 *	@param	Ctx		Actor 上下文（已加载 / 未加载二选一）
	 *	@param	OutIDs	输出：全部叶子 ID
	 */
	void ComputeLeafIDs(const FHwoActorContext& Ctx, TArray<FSceneOutlinerTreeItemID>& OutIDs) const;
};
