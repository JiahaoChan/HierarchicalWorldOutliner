// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Dimensions/HwoDimensionStack.h"
#include "HwoStyle.h"

#include "ActorMode.h"
#include "WorldPartition/WorldPartitionHandle.h"

class UToolMenu;

/**
 * 行为层：层级世界大纲的 SceneOutliner 模式（D6：继承 FActorMode）
 *
 * 挂接 FHwoHierarchy 作为数据源，负责双向选择同步（D2）、右键菜单分流、
 * 分组排序优先级、状态栏统计与分组计数徽标。
 * 由 SHwoPanel 创建，生命周期与面板一致。
 */
class FHwoMode : public FActorMode
{
private:
	// 维度堆叠（由面板传入，决定分组维度与堆叠顺序）
	TSharedRef<FHwoDimensionStack> DimensionStack;

	// 选择同步索引（Design-1/2）：ActorGuid → 全部叶子 ID
	TMultiMap<FGuid, FSceneOutlinerTreeItemID> ActorGuidToLeafIds;

	// 当前树中唯一 Actor 数（状态栏用，增量维护）
	int32 UniqueActorCount = 0;

	// 计数变化过的分组 ID，Tick 内统一刷新徽标
	TSet<FSceneOutlinerTreeItemID> DirtyGroupCountIDs;

	// 新建文件夹后待选中的文件夹路径（一次性 pending 动作，OnFullRefreshDone 时命中后清除）
	TOptional<FString> PendingSelectFolderPath;

	// 通过过滤器的唯一 Actor Guid 集合（状态栏"过滤前总数"口径，Rebuild 时清空，OnItemPassesFilters 累计）
	TSet<FGuid> ApplicableActorGuids;

	// 已加载叶子的逐 Guid 计数（状态栏 WP"已加载"段口径：已加载唯一 Actor 数 = Keys 数；Rebuild 时清空）
	TMap<FGuid, int32> LoadedLeafCountByGuid;

	// 当前树中叶子总数（暂无消费方，备用）
	int32 TotalLeafCount = 0;

public:
	FHwoMode(const FActorModeParams& Params, TSharedRef<FHwoDimensionStack> InStack);

	virtual ~FHwoMode();

	//~ Begin ISceneOutlinerMode Interface
	virtual void Rebuild() override;

	virtual void CreateViewContent(FMenuBuilder& MenuBuilder) override;

	virtual TSharedPtr<FDragDropOperation> CreateDragDropOperation(const FPointerEvent& MouseEvent, const TArray<FSceneOutlinerTreeItemPtr>& InTreeItems) const override { return nullptr; }

	virtual FSceneOutlinerDragValidationInfo ValidateDrop(const ISceneOutlinerTreeItem& DropTarget, const FSceneOutlinerDragDropPayload& Payload) const override { return FSceneOutlinerDragValidationInfo::Invalid(); }

	virtual void OnItemAdded(FSceneOutlinerTreeItemPtr Item) override;

	virtual void OnItemRemoved(FSceneOutlinerTreeItemPtr Item) override;

	virtual void OnItemSelectionChanged(FSceneOutlinerTreeItemPtr Item, ESelectInfo::Type SelectionType, const FSceneOutlinerItemSelection& Selection) override;

	virtual void OnItemPassesFilters(const ISceneOutlinerTreeItem& Item) override;

	virtual TSharedPtr<SWidget> CreateContextMenu() override;

	virtual void SynchronizeSelection() override;

	virtual ESelectionMode::Type GetSelectionMode() const override { return ESelectionMode::Multi; }

	virtual int32 GetTypeSortPriority(const ISceneOutlinerTreeItem& Item) const override;

	virtual bool CompareItemWithClassName(SceneOutliner::FilterBarType InItem, const TSet<FTopLevelAssetPath>& AssetClassPaths) const override;

	virtual FText GetStatusText() const override;

	virtual FSlateColor GetStatusTextColor() const override;

	virtual bool ShowStatusBar() const override { return true; }

	virtual bool SupportsCreateNewFolder() const override { return true; }

	virtual bool ShowViewButton() const override { return true; }

	virtual bool ShowFilterOptions() const override { return true; }

	virtual FFolder CreateNewFolder() override;

	virtual void Tick() override;

protected:
	virtual TUniquePtr<ISceneOutlinerHierarchy> CreateHierarchy() override;
	//~ End ISceneOutlinerMode Interface

public:
	/**
	 *	判断 Actor 是否可在大纲中显示（转发基类 protected 的 IsActorDisplayable，供 FHwoHierarchy 使用）
	 *
	 *	@param	InActor	要判断的 Actor
	 *	@return	可显示返回 true
	 */
	bool IsActorDisplayableForHwo(const AActor* InActor) const { return IsActorDisplayable(InActor); }

	/**
	 *	请求整树刷新（维度数据变更时由外部调用）
	 */
	void RequestFullRefresh();

	/**
	 *	在视口批量选择一组 Actor（去重）并同步未加载 Actor 的句柄选择；供右键菜单与选择同步共用
	 *
	 *	@param	InActors	要选择的已加载 Actor 集合
	 *	@param	InHandles	要选择的 WP 未加载 Actor 句柄
	 */
	void SelectActorsInViewport(const TSet<AActor*>& InActors, const TArray<FWorldPartitionHandle>& InHandles);

	/**
	 *	注册 UToolMenus 右键菜单（幂等，重复调用安全）
	 */
	static void RegisterContextMenu();

private:
	/**
	 *	填充默认右键菜单：单选委托节点自身生成，多选提供跨节点批量操作
	 *
	 *	@param	InMenu	待填充的菜单实例
	 */
	static void FillDefaultContextMenu(UToolMenu* InMenu);

	/**
	 *	视口 / 细节面板选择变化（USelection 事件）时同步大纲选中态
	 *
	 *	@param	Obj	事件携带对象（未使用）
	 */
	void OnLevelSelectionChanged(UObject* Obj);

	/**
	 *	Actor 改名时刷新该 Actor 全部叶子的显示
	 *
	 *	@param	ChangedActor	改名的 Actor
	 */
	void OnActorLabelChanged(AActor* ChangedActor);

	/**
	 *	整树刷新完成（SSceneOutliner::GetOnFullRefreshDone）时执行一次性的"新建文件夹选中+定位"动作
	 */
	void OnFullRefreshDone();

	/**
	 *	叶子加入时沿路径逐级累加各分组的去重 Actor 计数
	 *
	 *	@param	LeafPath	叶子的完整分组路径
	 *	@param	ActorGuid	叶子对应 Actor 的 Guid
	 */
	void AddLeafToGroupCounts(const TArray<FHwoGroupKey>& LeafPath, const FGuid& ActorGuid);

	/**
	 *	叶子移除时沿路径回减分组计数；同组同 Actor 还有其他叶子时计数不减
	 *
	 *	@param	LeafPath		被移除叶子的完整分组路径
	 *	@param	ActorGuid		被移除叶子对应 Actor 的 Guid
	 *	@param	RemovedLeafID	被移除叶子的树节点 ID（用于排除自身）
	 */
	void RemoveLeafFromGroupCounts(const TArray<FHwoGroupKey>& LeafPath, const FGuid& ActorGuid, const FSceneOutlinerTreeItemID& RemovedLeafID);

	/**
	 *	把视图选项（齿轮菜单四个开关）的当前状态写入 Saved ini（OutlinerState.ini 的 [ViewOptions] 节）
	 */
	void SaveViewOptions() const;

	/**
	 *	判断当前树选中集中是否有 Actor 存在多片叶子（多维度实例，状态栏紫色提醒用）
	 *
	 *	@return	存在多路径选中 Actor 返回 true
	 */
	bool HasMultiPathSelectedActor() const;

public:
	TSharedRef<FHwoDimensionStack> GetDimensionStack() const { return DimensionStack; }
};
