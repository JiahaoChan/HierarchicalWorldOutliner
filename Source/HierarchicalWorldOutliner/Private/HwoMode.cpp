// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "HwoMode.h"

#include "Dimensions/HwoBuiltInDimensions.h"
#include "HwoHierarchy.h"
#include "HwoLocalization.h"
#include "TreeItems/HwoActorDescTreeItem.h"
#include "TreeItems/HwoActorTreeItem.h"
#include "TreeItems/HwoGroupTreeItem.h"

#include "Editor.h"
#include "EditorActorFolders.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementList.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Engine/Blueprint.h"
#include "Engine/Selection.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManager.h"
#include "LevelEditor.h"
#include "LevelEditorContextMenu.h"
#include "LevelInstance/LevelInstanceInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SSceneOutliner.h"
#include "SceneOutlinerDelegates.h"
#include "SceneOutlinerMenuContext.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

// 右键菜单的 UToolMenus 注册名
static const FName HwoContextMenuName("HwoOutliner.DefaultContextMenu");

/** 视图选项持久化：OutlinerState.ini 的 [ViewOptions] 节（原生 UActorBrowserConfig 在 SceneOutliner/Private 未导出，自建） */
namespace HwoViewOptionsConfig
{
	// ini 节名
	static const TCHAR* SectionName = TEXT("ViewOptions");

	// 仅显示当前选中 Actor
	static const TCHAR* KeyOnlySelected = TEXT("bShowOnlySelectedActors");

	// 仅显示当前关卡的 Actor
	static const TCHAR* KeyOnlyCurrentLevel = TEXT("bShowOnlyCurrentLevel");

	// 隐藏 WP 未加载 Actor
	static const TCHAR* KeyHideUnloaded = TEXT("bHideUnloadedActors");

	// 视口选中时大纲自动滚动定位
	static const TCHAR* KeyAlwaysFrame = TEXT("bAlwaysFrameSelection");
}

/** 视图选项的完整状态（齿轮菜单四个开关） */
struct FHwoViewOptionsState
{
	bool bOnlySelectedActors = false;
	bool bOnlyCurrentLevel = false;
	bool bHideUnloadedActors = false;
	bool bAlwaysFrameSelection = true;
};

/**
 *	从 Saved ini 读取视图选项；文件不存在或键缺失时保留默认值
 *
 *	@return	读取到的视图选项状态
 */
static FHwoViewOptionsState LoadViewOptionsFromConfig()
{
	FHwoViewOptionsState State;

	const FString ConfigPath = FHwoDimensionStack::GetConfigFilePath();
	if (!IFileManager::Get().FileExists(*ConfigPath))
	{
		return State;
	}

	FConfigFile ConfigFile;
	ConfigFile.Read(ConfigPath);
	ConfigFile.GetBool(HwoViewOptionsConfig::SectionName, HwoViewOptionsConfig::KeyOnlySelected, State.bOnlySelectedActors);
	ConfigFile.GetBool(HwoViewOptionsConfig::SectionName, HwoViewOptionsConfig::KeyOnlyCurrentLevel, State.bOnlyCurrentLevel);
	ConfigFile.GetBool(HwoViewOptionsConfig::SectionName, HwoViewOptionsConfig::KeyHideUnloaded, State.bHideUnloadedActors);
	ConfigFile.GetBool(HwoViewOptionsConfig::SectionName, HwoViewOptionsConfig::KeyAlwaysFrame, State.bAlwaysFrameSelection);
	return State;
}

FHwoMode::FHwoMode(const FActorModeParams& Params, TSharedRef<FHwoDimensionStack> InStack)
	: FActorMode(Params)
	, DimensionStack(InStack)
{
	// 视口/细节面板选择同步（D2）。FActorMode 本身不订阅，参照 FActorModeInteractive 先例
	USelection::SelectionChangedEvent.AddRaw(this, &FHwoMode::OnLevelSelectionChanged);
	USelection::SelectObjectEvent.AddRaw(this, &FHwoMode::OnLevelSelectionChanged);

	FCoreDelegates::OnActorLabelChanged.AddRaw(this, &FHwoMode::OnActorLabelChanged);

	// 基类 FActorMode 不初始化这两个成员（原生由 FActorBrowsingMode 从配置回填），此处显式置初值
	bCollapseOutlinerTreeOnNewSelection = false;

	// 新建文件夹后的"选中+定位"挂在整树刷新完成钩子上（FullRefresh 为异步分批重建，不能即时选中）
	SceneOutliner->GetOnFullRefreshDone().AddRaw(this, &FHwoMode::OnFullRefreshDone);

	// 视图选项（齿轮菜单）：自建持久化，构造时恢复
	const FHwoViewOptionsState ViewOptions = LoadViewOptionsFromConfig();
	bHideUnloadedActors = ViewOptions.bHideUnloadedActors;
	bAlwaysFrameSelection = ViewOptions.bAlwaysFrameSelection;

	// 齿轮菜单的过滤开关：与内置大纲相同的 FilterInfoMap 体系（先例 ActorBrowsingMode.cpp:110-250）。
	// 谓词对 FActorTreeItem / FActorDescTreeItem 的 CastTo 经 Type 父链命中我们的叶子子类；
	// 分组节点被过滤后，框架会在其子代命中时把它强制显示为父链（EnsureParentForItem），故分组结构不受影响
	{
		FSceneOutlinerFilterInfo ShowOnlySelectedActorsInfo(
			HWO_TEXT("View.OnlySelected", "Only Selected", "仅选中项"),
			HWO_TEXT("View.OnlySelectedTooltip", "When enabled, only displays actors that are currently selected.", "启用后仅显示当前选中的 Actor。"),
			ViewOptions.bOnlySelectedActors,
			FCreateSceneOutlinerFilter::CreateLambda([]()
			{
				auto IsActorSelected = [](const AActor* InActor) { return InActor && InActor->IsSelected(); };
				return MakeShared<TSceneOutlinerPredicateFilter<FActorTreeItem>>(FActorTreeItem::FFilterPredicate::CreateStatic(IsActorSelected), FSceneOutlinerFilter::EDefaultBehaviour::Fail, FActorTreeItem::FFilterPredicate::CreateStatic(IsActorSelected));
			}));
		ShowOnlySelectedActorsInfo.OnToggle().AddLambda([this](bool bIsActive) { SaveViewOptions(); });
		FilterInfoMap.Add(TEXT("ShowOnlySelectedActors"), ShowOnlySelectedActorsInfo);
	}

	{
		FSceneOutlinerFilterInfo OnlyCurrentLevelInfo(
			HWO_TEXT("View.OnlyCurrentLevel", "Only in Current Level", "仅当前关卡"),
			HWO_TEXT("View.OnlyCurrentLevelTooltip", "When enabled, only shows Actors that are in the Current Level.", "启用后仅显示当前关卡中的 Actor。"),
			ViewOptions.bOnlyCurrentLevel,
			FCreateSceneOutlinerFilter::CreateLambda([]()
			{
				return MakeShared<TSceneOutlinerPredicateFilter<ISceneOutlinerTreeItem>>(ISceneOutlinerTreeItem::FFilterPredicate::CreateStatic([](const ISceneOutlinerTreeItem& InItem)
				{
					if (const FActorTreeItem* ActorItem = InItem.CastTo<FActorTreeItem>())
					{
						if (const AActor* Actor = ActorItem->Actor.Get(); Actor && Actor->GetWorld())
						{
							if (const ILevelInstanceInterface* LevelInstance = Cast<ILevelInstanceInterface>(Actor))
							{
								if (LevelInstance->IsEditing())
								{
									return Actor->GetWorld()->GetCurrentLevel() == LevelInstance->GetLoadedLevel();
								}
							}
							return Actor->GetLevel()->IsCurrentLevel();
						}
					}
					// 其余节点（分组 / 未加载叶子）默认隐藏，与原生行为一致
					return false;
				}), FSceneOutlinerFilter::EDefaultBehaviour::Pass);
			}));
		OnlyCurrentLevelInfo.OnToggle().AddLambda([this](bool bIsActive) { SaveViewOptions(); });
		FilterInfoMap.Add(TEXT("ShowOnlyCurrentLevel"), OnlyCurrentLevelInfo);
	}

	{
		// Hide Unloaded Actors：过滤级隐藏（立即生效）+ 枚举级开关（bHideUnloadedActors 驱动下次
		// CreateHierarchy 不再枚举未加载 Actor，FHwoMode::CreateHierarchy 传入 !bHideUnloadedActors）。
		// 开关切换本身由框架触发 FullRefresh（SSceneOutliner.cpp:131 Filters->OnChanged），两级天然同步
		FSceneOutlinerFilterInfo HideUnloadedActorsInfo(
			HWO_TEXT("View.HideUnloaded", "Hide Unloaded Actors", "隐藏未加载 Actor"),
			HWO_TEXT("View.HideUnloadedTooltip", "When enabled, hides all unloaded World Partition actors.", "启用后隐藏全部 World Partition 未加载 Actor。"),
			bHideUnloadedActors,
			FCreateSceneOutlinerFilter::CreateLambda([]()
			{
				return MakeShared<TSceneOutlinerPredicateFilter<FActorDescTreeItem>>(FActorDescTreeItem::FFilterPredicate::CreateStatic([](const FWorldPartitionActorDescInstance*) { return false; }), FSceneOutlinerFilter::EDefaultBehaviour::Pass);
			}));
		HideUnloadedActorsInfo.OnToggle().AddLambda([this](bool bIsActive)
		{
			bHideUnloadedActors = bIsActive;
			SaveViewOptions();
		});
		FilterInfoMap.Add(TEXT("HideUnloadedActorsFilter"), HideUnloadedActorsInfo);
	}
}

FHwoMode::~FHwoMode()
{
	USelection::SelectionChangedEvent.RemoveAll(this);
	USelection::SelectObjectEvent.RemoveAll(this);
	FCoreDelegates::OnActorLabelChanged.RemoveAll(this);

	if (SceneOutliner)
	{
		SceneOutliner->GetOnFullRefreshDone().RemoveAll(this);
	}
}

void FHwoMode::Rebuild()
{
	FActorMode::Rebuild();

	// SSceneOutliner::EmptyTreeItems 直接重置 TreeItemMap，不回调 OnItemRemoved；
	// 派生索引/计数必须在此清空，否则 FullRefresh 后残留膨胀（先例 FActorBrowsingMode::Rebuild）
	ActorGuidToLeafIds.Empty();
	TotalLeafCount = 0;
	UniqueActorCount = 0;
	DirtyGroupCountIDs.Empty();
	ApplicableActorGuids.Empty();
	LoadedLeafCountByGuid.Empty();
}

void FHwoMode::CreateViewContent(FMenuBuilder& MenuBuilder)
{
	// Options 段：Always Frame Selection 开关（先例 ActorBrowsingMode::InitializeViewMenuExtender:518-590）
	MenuBuilder.BeginSection("HwoOutlinerOptions", HWO_TEXT("View.OptionsSection", "Options", "选项"));
	{
		MenuBuilder.AddMenuEntry(
			HWO_TEXT("View.AlwaysFrameSelection", "Always Frame Selection", "始终定位选中项"),
			HWO_TEXT("View.AlwaysFrameSelectionTooltip", "When enabled, selecting an Actor in the Viewport also scrolls to that Actor in the Outliner.", "启用后，在视口中选中 Actor 时大纲自动滚动定位到该 Actor。"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					bAlwaysFrameSelection = !bAlwaysFrameSelection;
					SaveViewOptions();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return bAlwaysFrameSelection; })
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	MenuBuilder.EndSection();
}

void FHwoMode::OnItemAdded(FSceneOutlinerTreeItemPtr Item)
{
	FGuid ActorGuid;
	bool bIsLeaf = false;
	bool bIsLoadedLeaf = false;
	const TArray<FHwoGroupKey>* Path = nullptr;

	if (FHwoActorTreeItem* ActorItem = Item->CastTo<FHwoActorTreeItem>())
	{
		ActorGuid = ActorItem->ActorGuid;
		Path = &ActorItem->Path;
		bIsLeaf = true;
		bIsLoadedLeaf = true;

		// 新增叶子时同步视口选中态（先例 ActorBrowsingMode::OnItemAdded；用 AddToSelection 避免 ClearSelection 副作用）
		if (ActorItem->Actor.IsValid() && GEditor && GEditor->GetSelectedActors()->IsSelected(ActorItem->Actor.Get()))
		{
			SceneOutliner->AddToSelection(Item, ESelectInfo::Direct);
		}
	}
	else if (FHwoActorDescTreeItem* DescItem = Item->CastTo<FHwoActorDescTreeItem>())
	{
		ActorGuid = DescItem->CachedActorGuid;
		Path = &DescItem->Path;
		bIsLeaf = true;
	}

	if (bIsLeaf)
	{
		if (!ActorGuidToLeafIds.Contains(ActorGuid))
		{
			++UniqueActorCount;
		}
		if (bIsLoadedLeaf)
		{
			// 已加载唯一 Actor 计数（状态栏 WP"已加载"段）：同一 Guid 多片叶子只计一次
			LoadedLeafCountByGuid.FindOrAdd(ActorGuid)++;
		}
		ActorGuidToLeafIds.Add(ActorGuid, Item->GetID());
		++TotalLeafCount;
		AddLeafToGroupCounts(*Path, ActorGuid);
	}
}

void FHwoMode::OnItemRemoved(FSceneOutlinerTreeItemPtr Item)
{
	FGuid ActorGuid;
	bool bIsLeaf = false;
	bool bIsLoadedLeaf = false;
	const TArray<FHwoGroupKey>* Path = nullptr;

	if (FHwoActorTreeItem* ActorItem = Item->CastTo<FHwoActorTreeItem>())
	{
		ActorGuid = ActorItem->ActorGuid;
		Path = &ActorItem->Path;
		bIsLeaf = true;
		bIsLoadedLeaf = true;
	}
	else if (FHwoActorDescTreeItem* DescItem = Item->CastTo<FHwoActorDescTreeItem>())
	{
		ActorGuid = DescItem->CachedActorGuid;
		Path = &DescItem->Path;
		bIsLeaf = true;
	}

	if (bIsLeaf)
	{
		if (bIsLoadedLeaf)
		{
			// 该 Guid 的最后一片已加载叶子移除时从集合删除（计数归零即无已加载实例）
			if (--LoadedLeafCountByGuid.FindOrAdd(ActorGuid) <= 0)
			{
				LoadedLeafCountByGuid.Remove(ActorGuid);
			}
		}
		ActorGuidToLeafIds.RemoveSingle(ActorGuid, Item->GetID());
		if (!ActorGuidToLeafIds.Contains(ActorGuid))
		{
			--UniqueActorCount;
			// 该 Actor 已无叶子：从"过滤前总数"集合移除，保持口径精确（先例 FActorBrowsingMode::OnLevelActorDeleted，ActorBrowsingMode.cpp:1012-1015）
			ApplicableActorGuids.Remove(ActorGuid);
		}
		--TotalLeafCount;
		RemoveLeafFromGroupCounts(*Path, ActorGuid, Item->GetID());
	}
}

void FHwoMode::OnItemSelectionChanged(FSceneOutlinerTreeItemPtr Item, ESelectInfo::Type SelectionType, const FSceneOutlinerItemSelection& Selection)
{
	// 只同步 Actor 叶子：分组节点与世界节点不推向 GEditor 选择（对齐原生 WO：点文件夹只选中文件夹本身）。
	// 混合选择（分组+Actor）时只同步 Actor 部分；"选中组内全部 Actor"能力保留在分组右键菜单
	TSet<AActor*> ActorsToSelect;
	TArray<FWorldPartitionHandle> HandlesToSelect;
	for (const TWeakPtr<ISceneOutlinerTreeItem>& WeakItem : Selection.SelectedItems)
	{
		if (const FSceneOutlinerTreeItemPtr SelectedItem = WeakItem.Pin())
		{
			if (SelectedItem->IsA<FHwoActorTreeItem>() || SelectedItem->IsA<FHwoActorDescTreeItem>())
			{
				FHwoGroupTreeItem::CollectActors(*SelectedItem, ActorsToSelect, HandlesToSelect);
			}
		}
	}

	// 树内选择不含任何 Actor 叶子（纯分组 / 世界节点 / 空选择）时，清空视口 Actor 选择
	// （对齐原生统一选择语义；未加载句柄选择同源清空）。
	// 防回流：本函数恒在 SSceneOutliner::OnOutlinerTreeSelectionChanged 的 bIsReentrant 保护内执行，
	// 清空触发的 USelection 事件回流到 OnLevelSelectionChanged 时被 GetIsReentrant() 拦截；
	// 且 SelectActorsInViewport 对"视口本就无选择"先判等早退，幂等收敛
	SelectActorsInViewport(ActorsToSelect, HandlesToSelect);
}

void FHwoMode::OnItemPassesFilters(const ISceneOutlinerTreeItem& Item)
{
	// 状态栏"过滤前总数"口径（原生 ApplicableActors 先例）：叶子通过过滤器时累计唯一 Guid
	FGuid ActorGuid;
	if (const FHwoActorTreeItem* ActorItem = Item.CastTo<FHwoActorTreeItem>())
	{
		ActorGuid = ActorItem->ActorGuid;
	}
	else if (const FHwoActorDescTreeItem* DescItem = Item.CastTo<FHwoActorDescTreeItem>())
	{
		ActorGuid = DescItem->CachedActorGuid;
	}
	if (ActorGuid.IsValid())
	{
		ApplicableActorGuids.Add(ActorGuid);
	}
}

TSharedPtr<SWidget> FHwoMode::CreateContextMenu()
{
	// 选中项含【已加载】Actor 叶子 → 直接复用 LevelEditor 的 Actor 上下文菜单（"LevelEditor.ActorContextMenu"，与内置大纲同源）。
	// 该菜单内部从 GEditor 的 ElementSelectionSet 取选择——D2 选择同步已保证树选择 = 视口选择。
	// 已知缺口：其中 Force Load/Release Force Load 依赖 PinItems/CanPinItems（FHwoMode 未覆写，灰显）。
	// 注意：未加载叶子（FHwoActorDescTreeItem）不命中此分支——未加载 Actor 不在 GEditor Actor 选择集，
	// LevelEditor 菜单会落回无 Actor 的 SceneOutlinerContextMenu，丢失基类自带的 Focus/Copy Path（五轮审查 W1）；
	// 混合选择（分组+Actor）时同样走 LevelEditor 菜单，分组操作暂不可达（TDD §4.4 已注明）。
	for (const FSceneOutlinerTreeItemPtr& SelectedItem : SceneOutliner->GetTree().GetSelectedItems())
	{
		if (SelectedItem.IsValid() && SelectedItem->IsA<FHwoActorTreeItem>())
		{
			return FLevelEditorContextMenu::BuildMenuWidget(
				FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor").GetLevelEditorInstance(),
				ELevelEditorMenuContext::SceneOutliner);
		}
	}

	// 纯分组节点（或无选中）→ 自定义菜单
	RegisterContextMenu();

	const FSceneOutlinerItemSelection ItemSelection(SceneOutliner->GetSelection());

	USceneOutlinerMenuContext* ContextObject = NewObject<USceneOutlinerMenuContext>();
	ContextObject->SceneOutliner = StaticCastSharedRef<SSceneOutliner>(SceneOutliner->AsShared());
	ContextObject->bShowParentTree = SceneOutliner->GetSharedData().bShowParentTree;
	ContextObject->NumSelectedItems = ItemSelection.Num();

	FToolMenuContext Context(ContextObject);
	UToolMenus* ToolMenus = UToolMenus::Get();
	UToolMenu* Menu = ToolMenus->GenerateMenu(HwoContextMenuName, Context);
	for (const FToolMenuSection& Section : Menu->Sections)
	{
		if (Section.Blocks.Num() > 0)
		{
			return ToolMenus->GenerateWidget(Menu);
		}
	}
	return nullptr;
}

void FHwoMode::SynchronizeSelection()
{
	if (!GEditor)
	{
		return;
	}
	USelection* SelectedActors = GEditor->GetSelectedActors();

	// 1) 取消选中树中已不在视口选择集的 Actor 叶子
	const FSceneOutlinerItemSelection Selection(SceneOutliner->GetSelection());
	TArray<FSceneOutlinerTreeItemPtr> ItemsToDeselect;
	for (const TWeakPtr<ISceneOutlinerTreeItem>& WeakItem : Selection.SelectedItems)
	{
		FSceneOutlinerTreeItemPtr Item = WeakItem.Pin();
		if (!Item.IsValid())
		{
			continue;
		}
		if (const FHwoActorTreeItem* ActorItem = Item->CastTo<FHwoActorTreeItem>())
		{
			if (!ActorItem->Actor.IsValid() || !ActorItem->Actor->IsSelected())
			{
				ItemsToDeselect.Add(Item);
			}
		}
	}
	if (ItemsToDeselect.Num() > 0)
	{
		// 用 RemoveFromSelection：SetItemSelection 会先 ClearSelection（SSceneOutliner.cpp:1296），分批填充期间会互相清空
		SceneOutliner->RemoveFromSelection(ItemsToDeselect, ESelectInfo::Direct);
	}

	// 2) 视口选中 Actor → 查索引，全部维度实例置选中（D2）
	TArray<FSceneOutlinerTreeItemPtr> ItemsToSelect;
	FSceneOutlinerTreeItemPtr FirstItem;
	for (FSelectionIterator SelectionIt(*SelectedActors); SelectionIt; ++SelectionIt)
	{
		AActor* Actor = CastChecked<AActor>(*SelectionIt);

		TArray<FSceneOutlinerTreeItemID> LeafIds;
		ActorGuidToLeafIds.MultiFind(Actor->GetActorGuid(), LeafIds);
		for (const FSceneOutlinerTreeItemID& LeafId : LeafIds)
		{
			if (FSceneOutlinerTreeItemPtr LeafItem = SceneOutliner->GetTreeItem(LeafId))
			{
				if (!FirstItem.IsValid())
				{
					FirstItem = LeafItem;
				}
				ItemsToSelect.Add(LeafItem);
			}
		}
	}
	if (ItemsToSelect.Num() > 0)
	{
		// 同上：AddToSelection 无 ClearSelection 副作用
		SceneOutliner->AddToSelection(ItemsToSelect, ESelectInfo::Direct);
	}

	// 3) 滚动定位第一个实例，并展开其父链（受齿轮菜单 Always Frame Selection 开关控制）
	if (bAlwaysFrameSelection && FirstItem.IsValid())
	{
		FSceneOutlinerTreeItemPtr Parent = FirstItem->GetParent();
		while (Parent.IsValid())
		{
			SceneOutliner->SetItemExpansion(Parent, true);
			Parent = Parent->GetParent();
		}
		SceneOutliner->ScrollItemIntoView(FirstItem);
	}

	FSceneOutlinerDelegates::Get().SelectionChanged.Broadcast();
}

int32 FHwoMode::GetTypeSortPriority(const ISceneOutlinerTreeItem& Item) const
{
	// 分组节点排在 Actor 前；基类对未知类型 check(false)，必须先分流
	if (const FHwoGroupTreeItem* GroupItem = Item.CastTo<FHwoGroupTreeItem>())
	{
		if (!GroupItem->Path.IsEmpty())
		{
			const FHwoGroupKey& LastKey = GroupItem->Path.Last();

			// "未分配"占位组沉到真实分组之后、Actor 之前（各维度统一；其标签以 "(" 开头，不处理会按字母序排到最前）。
			// 固有表现：降序时未分配组相应变到最前，可接受
			if (LastKey.Key == HwoGroupKeys::Unassigned)
			{
				return FActorMode::EItemSortOrder::Folder + 1;
			}

			// Unsaved 维度："未保存"组排在"已保存"前（同档原本按显示名字母序，"已保存"会在"未保存"前）。
			// 固有取舍同未分配组：降序时翻转
			if (LastKey.DimensionID == FHwoUnsavedDimension::StaticDimensionID() && LastKey.Key == FHwoUnsavedDimension::GetUnsavedGroupKey())
			{
				return FActorMode::EItemSortOrder::Folder - 1;
			}

			// RuntimeGrid 维度的"非空间加载"特殊组沉到未分配组旁边（同档按标签互排）；
			// 固有取舍同未分配组：降序时翻转
			if (LastKey.DimensionID == FHwoRuntimeGridDimension::StaticDimensionID() && LastKey.Key == FHwoRuntimeGridDimension::GetNonSpatiallyLoadedGroupKey())
			{
				return FActorMode::EItemSortOrder::Folder + 1;
			}
		}
		return FActorMode::EItemSortOrder::Folder;
	}
	return FActorMode::GetTypeSortPriority(Item);
}

bool FHwoMode::CompareItemWithClassName(SceneOutliner::FilterBarType InItem, const TSet<FTopLevelAssetPath>& AssetClassPaths) const
{
	// 复刻 FActorBrowsingMode::CompareItemWithClassName（ActorBrowsingMode.cpp:2000）：
	// FActorMode 未覆写该函数，基类默认 false——不覆写的话类型过滤启用时所有条目都会被滤掉
	if (const FActorTreeItem* ActorItem = InItem.CastTo<FActorTreeItem>())
	{
		AActor* Actor = ActorItem->Actor.Get();
		if (!Actor)
		{
			return false;
		}

		const FTopLevelAssetPath AssetClassPath = Actor->GetClass()->GetClassPathName();

		// BP 类同时匹配蓝图类自身与父原生类
		if (UBlueprint* ClassBP = UBlueprint::GetBlueprintFromClass(Actor->GetClass()))
		{
			return AssetClassPaths.Contains(ClassBP->GetClass()->GetClassPathName()) || AssetClassPaths.Contains(AssetClassPath);
		}

		return AssetClassPaths.Contains(AssetClassPath);
	}
	else if (const FActorDescTreeItem* ActorDescItem = InItem.CastTo<FActorDescTreeItem>())
	{
		if (const FWorldPartitionActorDescInstance* ActorDescInstance = *ActorDescItem->ActorDescHandle)
		{
			// 未加载 Actor 取原生类路径
			return AssetClassPaths.Contains(ActorDescInstance->GetNativeClass());
		}
	}

	// 分组节点等其余类型按 false 处理（与原生文件夹一致，由 EnsureParentForItem 保活父链）
	return false;
}

FText FHwoMode::GetStatusText() const
{
	// 复刻原生四分支（先例 FActorBrowsingMode::GetStatusText，ActorBrowsingMode.cpp:344-401）；
	// 状态栏 Text 是 TAttribute 每帧轮询（SSceneOutliner::Construct），无需事件接线。
	// 计数口径取舍：Actor 总数用"树内唯一 Actor 数"（UniqueActorCount，多路径复制不像叶子数那样膨胀）；
	// 过滤前总数用 ApplicableActorGuids（OnItemPassesFilters 累计的唯一 Guid 数）；
	// loaded 数 = LoadedLeafCountByGuid.Num()（已加载唯一 Actor 数，增量维护）。
	// 中文译文逐字对齐引擎官方简体（Editor/zh-Hans/Editor.locres）
	if (!RepresentingWorld.IsValid())
	{
		return FText();
	}

	const bool bIsPartitionedWorld = RepresentingWorld->GetWorldPartition() != nullptr;
	const int32 TotalActorCount = ApplicableActorGuids.Num();
	const int32 SelectedActorCount = SceneOutliner->GetSelection().Num<FActorTreeItem, FActorDescTreeItem>();
	// 注意：D2 语义下选中一个 Actor 会选中其全部维度实例，选中数是叶子数而非唯一 Actor 数（可能大于实际选中 Actor 数）

	if (!SceneOutliner->IsTextFilterActive())
	{
		if (SelectedActorCount == 0)
		{
			// WP 世界追加"已加载"段（原生同款；原生"有选中"分支不带 loaded 段，已核实）
			if (bIsPartitionedWorld)
			{
				return FText::Format(HWO_TEXT("Status.AllActorsLoaded", "{0} actors ({1} loaded)", "{0}个Actor（{1}个已加载）"), FText::AsNumber(UniqueActorCount), FText::AsNumber(LoadedLeafCountByGuid.Num()));
			}
			return FText::Format(HWO_TEXT("Status.AllActors", "{0} actors", "{0}个Actor"), FText::AsNumber(UniqueActorCount));
		}
		else
		{
			return FText::Format(HWO_TEXT("Status.AllActorsSelected", "{0} actors ({1} selected)", "{0}个Actor（已选{1}个）"), FText::AsNumber(UniqueActorCount), FText::AsNumber(SelectedActorCount));
		}
	}
	else if (UniqueActorCount == 0)
	{
		return FText::Format(HWO_TEXT("Status.NoMatching", "No matching actors ({0} total)", "没有匹配的Actor（总共{0}个）"), FText::AsNumber(TotalActorCount));
	}
	else if (SelectedActorCount != 0)
	{
		return FText::Format(HWO_TEXT("Status.SomeActorsSelected", "Showing {0} of {1} actors ({2} selected)", "显示{0}/{1}个Actor（已选{2}个）"), FText::AsNumber(UniqueActorCount), FText::AsNumber(TotalActorCount), FText::AsNumber(SelectedActorCount));
	}
	else
	{
		return FText::Format(HWO_TEXT("Status.SomeActors", "Showing {0} of {1} actors", "显示{0}/{1}个Actor"), FText::AsNumber(UniqueActorCount), FText::AsNumber(TotalActorCount));
	}
}

FSlateColor FHwoMode::GetStatusTextColor() const
{
	// 多路径提醒前置：任一选中 Actor 在树中有多片叶子（多维度实例）时整行紫色——
	// 非常规提醒色，提示"该 Actor 被多维复制到多个分组"，与品牌紫共用但不表示选中态
	if (HasMultiPathSelectedActor())
	{
		return FSlateColor(HwoStyle::AccentPurple);
	}

	// 以下复刻原生三色（ActorBrowsingMode.cpp:392-401）
	if (!SceneOutliner->IsTextFilterActive())
	{
		return FSlateColor::UseForeground();
	}
	if (UniqueActorCount == 0)
	{
		return FAppStyle::Get().GetSlateColor("Colors.AccentRed");
	}
	return FAppStyle::Get().GetSlateColor("Colors.AccentGreen");
}

FFolder FHwoMode::CreateNewFolder()
{
	// 新建包含当前选中项的文件夹（先例 ActorBrowsingMode::CreateNewFolder，ActorBrowsingMode.cpp:1509-1521）。
	// 选中 Actor 走 GEditor 全局选择（与 D2 选择同步兼容）；
	// 移动后由 FHwoHierarchy 监听的 OnLevelActorFolderChanged 触发 FullRefresh
	if (TStrongObjectPtr<UWorld> World = RepresentingWorld.Pin())
	{
		const FScopedTransaction Transaction(HWO_TEXT("Folder.CreateNew", "Create Folder", "新建文件夹"));

		// 引擎的 SceneOutliner::FFolderPathSelector 实现在 SceneOutliner 模块 Private 且未导出，插件链接不到；
		// 且本面板的树没有 FFolderTreeItem 节点。自定义 selector（先例 FolderTreeItem.cpp:20-31）：
		// 分组节点的 Path 即祖先链的扁平展开——把其中文件夹维度的段（跳过"未分配"占位段）按序拼回
		// 完整文件夹路径作为新文件夹父级（对齐原生"选中文件夹作为父级"语义）；堆叠中间层场景下，
		// 文件夹维度的段可能散落在不同层级（夹着其它维度的段），拼接结果仍是正确路径。
		// 无任何文件夹段的节点（Actor 叶子、其它维度分组）一律跳过
		auto HwoFolderPathSelector = [this](const TWeakPtr<ISceneOutlinerTreeItem>& WeakItem, FFolder& DataOut) -> bool
		{
			const FSceneOutlinerTreeItemPtr Item = WeakItem.Pin();
			const FHwoGroupTreeItem* GroupItem = Item.IsValid() ? Item->CastTo<FHwoGroupTreeItem>() : nullptr;
			if (!GroupItem)
			{
				return false;
			}

			const FString FolderPath = GroupItem->ReconstructFolderPath();
			if (FolderPath.IsEmpty())
			{
				return false;
			}

			// 文件夹路径段即 Actor::GetFolderPath 的世界根相对路径
			DataOut = FFolder(FFolder::GetWorldRootFolder(RepresentingWorld.Get()).GetRootObject(), FName(*FolderPath));
			return true;
		};

		TArray<FFolder> SelectedFolders = SceneOutliner->GetSelection().GetData<FFolder>(HwoFolderPathSelector);
		const FFolder NewFolderName = FActorFolders::Get().GetDefaultFolderForSelection(*World, &SelectedFolders);
		FActorFolders::Get().CreateFolderContainingSelection(*World, NewFolderName);

		// 登记一次性 pending 动作：FullRefresh 是异步分批重建，等 OnFullRefreshDone 再选中+定位
		// （刷新由 FActorFolders::OnFolderCreated / OnLevelActorFolderChanged 触发；此处显式补一次 FullRefresh 兜底）
		PendingSelectFolderPath = NewFolderName.GetPath().ToString();
		SceneOutliner->FullRefresh();

		return NewFolderName;
	}
	return FFolder::GetInvalidFolder();
}

void FHwoMode::Tick()
{
	// 分组计数徽标统一刷新（批量入树期间 OnItemAdded 只打脏标记，防逐叶子风暴）
	if (DirtyGroupCountIDs.Num() > 0)
	{
		for (const FSceneOutlinerTreeItemID& GroupID : DirtyGroupCountIDs)
		{
			if (FSceneOutlinerTreeItemPtr GroupItem = SceneOutliner->GetTreeItem(GroupID, /*bIncludePending*/true))
			{
				// 被过滤的分组（搜索时经 CreateItemFor(bForce) 入树）走 OnItemLabelChanged 会被误判为
				// "不再匹配过滤"而删行，导致命中的子叶随父脱钩消失；被过滤行本就不可见，跳过刷新
				if (!GroupItem->Flags.bIsFilteredOut)
				{
					SceneOutliner->OnItemLabelChanged(GroupItem, /*bFlashHighlight*/false);
				}
			}
		}
		DirtyGroupCountIDs.Empty();
	}
}

TUniquePtr<ISceneOutlinerHierarchy> FHwoMode::CreateHierarchy()
{
	return FHwoHierarchy::Create(this, RepresentingWorld, DimensionStack, !bHideUnloadedActors);
}

void FHwoMode::RequestFullRefresh()
{
	SceneOutliner->FullRefresh();
}

void FHwoMode::SelectActorsInViewport(const TSet<AActor*>& InActors, const TArray<FWorldPartitionHandle>& InHandles)
{
	if (!GEditor)
	{
		return;
	}

	// 拿不到 SelectionSet 时无法安全写选择，直接返回（提前检查：避免句柄通道已写、Actor 通道未写的两通道不一致）
	USelection* ActorSelection = GEditor->GetSelectedActors();
	UTypedElementSelectionSet* SelectionSet = ActorSelection->GetElementSelectionSet();
	if (!SelectionSet)
	{
		return;
	}

	// 未加载 Actor 走 WorldPartition 句柄选择通道（先例 ActorBrowsingMode::SynchronizeSelectedActorDescs）
	if (UWorldPartitionSubsystem* WorldPartitionSubsystem = UWorld::GetSubsystem<UWorldPartitionSubsystem>(RepresentingWorld.Get()))
	{
		WorldPartitionSubsystem->SelectedActorHandles.Empty();
		WorldPartitionSubsystem->SelectedActorHandles.Append(InHandles);
	}

	// 与当前视口选择比较，有差异才更新（避免无谓的 SelectionChanged 回环）
	TArray<AActor*> CurrentActors;
	ActorSelection->GetSelectedObjects<AActor>(CurrentActors);
	const TSet<AActor*> CurrentSet(CurrentActors);

	if (CurrentSet.Difference(InActors).IsEmpty() && InActors.Difference(CurrentSet).IsEmpty())
	{
		return;
	}

	// 写选择走原生 TypedElement 通道（先例 ActorBrowsingMode.cpp:1130-1156），不开 legacy 批处理：
	// legacy 批处理会让 TypedElementList 的 pending-notify 标记残留到帧末回声广播（在 bIsReentrant 守卫之外），
	// 导致"选中后首次点击分组被清空"（Bug-P9）；SetSelection + NotifyPendingChanges 在守卫内同步完成通知
	TArray<FTypedElementHandle> ElementsToSelect;
	ElementsToSelect.Reserve(InActors.Num());
	for (AActor* Actor : InActors)
	{
		if (Actor)
		{
			ElementsToSelect.Add(UEngineElementsLibrary::AcquireEditorActorElementHandle(Actor));
		}
	}

	{
		const FTypedElementSelectionOptions SelectionOptions = FTypedElementSelectionOptions()
			.SetAllowHidden(true)
			.SetWarnIfLocked(false)
			.SetAllowLegacyNotifications(false)
			.SetAllowSubRootSelection(true);

		FTypedElementList::FScopedClearNewPendingChange ClearNewPendingChange = SelectionSet->GetScopedClearNewPendingChange();
		SelectionSet->SetSelection(ElementsToSelect, SelectionOptions);
		// NotifyPendingChanges 同步触发 OnEditorElementSelectionChanged → NoteSelectionChange，
		// legacy 监听者（含我们的 OnLevelSelectionChanged）照常收到广播，且此时 bIsReentrant 守卫在位
		SelectionSet->NotifyPendingChanges();
	}

	SetAsMostRecentOutliner();
}

void FHwoMode::RegisterContextMenu()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus->IsMenuRegistered(HwoContextMenuName))
	{
		UToolMenu* Menu = ToolMenus->RegisterMenu(HwoContextMenuName);
		Menu->AddDynamicSection("DynamicHwoSection", FNewToolMenuDelegate::CreateStatic(&FHwoMode::FillDefaultContextMenu));
	}
}

void FHwoMode::FillDefaultContextMenu(UToolMenu* InMenu)
{
	USceneOutlinerMenuContext* Context = InMenu->FindContext<USceneOutlinerMenuContext>();
	if (!Context)
	{
		return;
	}

	TSharedPtr<SSceneOutliner> Outliner = Context->SceneOutliner.Pin();
	if (!Outliner.IsValid())
	{
		return;
	}

	// 单选：委托 item 自身生成（分组节点与 Actor 节点分流）
	if (Context->NumSelectedItems == 1)
	{
		const TArray<FSceneOutlinerTreeItemPtr> SelectedItems = Outliner->GetTree().GetSelectedItems();
		if (SelectedItems.Num() == 1)
		{
			SelectedItems[0]->GenerateContextMenu(InMenu, *Outliner);
		}
	}
	else if (Context->NumSelectedItems > 1)
	{
		// 多选：跨节点去重选择全部子代 Actor
		FToolMenuSection& Section = InMenu->AddSection("HwoMultiActions", HWO_TEXT("Menu.MultiSection", "For Selection", "对选中项"));
		TWeakPtr<SSceneOutliner> WeakOutliner = Outliner;
		Section.AddMenuEntry(
			"HwoSelectActorsInSelection",
			HWO_TEXT("Menu.SelectAllChildActors", "Select All Child Actors", "选中全部子代 Actor"),
			HWO_TEXT("Menu.SelectAllChildActorsTooltip", "Select all de-duplicated actors under every selected item (groups included) in the viewport.", "在视口中选中所有选中节点（含分组）内的全部去重 Actor"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakOutliner]()
			{
				const TSharedPtr<SSceneOutliner> OutlinerPin = WeakOutliner.Pin();
				if (!OutlinerPin.IsValid())
				{
					return;
				}
				TSet<AActor*> Actors;
				TArray<FWorldPartitionHandle> Handles;
				for (const FSceneOutlinerTreeItemPtr& SelectedItem : OutlinerPin->GetTree().GetSelectedItems())
				{
					if (SelectedItem.IsValid())
					{
						FHwoGroupTreeItem::CollectActors(*SelectedItem, Actors, Handles);
					}
				}
				// ISceneOutliner::GetMode() 仅提供 const 版本；此 Mode 由本类创建，安全
				if (FHwoMode* Mode = const_cast<FHwoMode*>(static_cast<const FHwoMode*>(OutlinerPin->GetMode())))
				{
					Mode->SelectActorsInViewport(Actors, Handles);
				}
			})));
	}
}

void FHwoMode::OnLevelSelectionChanged(UObject* Obj)
{
	// 大纲自身的选择变更会置 Reentrant，已在 OnItemSelectionChanged 处理，此处防回环
	if (!SceneOutliner->GetIsReentrant())
	{
		SceneOutliner->ClearSelection();
		SceneOutliner->RefreshSelection();
		// 滚动定位在 SynchronizeSelection 内完成（D2：定位第一个实例）
	}
}

void FHwoMode::OnActorLabelChanged(AActor* ChangedActor)
{
	if (!ChangedActor || RepresentingWorld.Get() != ChangedActor->GetWorld())
	{
		return;
	}

	TArray<FSceneOutlinerTreeItemID> LeafIds;
	ActorGuidToLeafIds.MultiFind(ChangedActor->GetActorGuid(), LeafIds);
	for (const FSceneOutlinerTreeItemID& LeafId : LeafIds)
	{
		if (FSceneOutlinerTreeItemPtr Item = SceneOutliner->GetTreeItem(LeafId))
		{
			SceneOutliner->OnItemLabelChanged(Item);
		}
	}
}

void FHwoMode::OnFullRefreshDone()
{
	if (!PendingSelectFolderPath.IsSet())
	{
		return;
	}

	// 一次性语义：无论命中与否都清除。文件夹维度不在堆叠 / 非最外层时新文件夹没有对应分组节点
	// （空文件夹枚举与选中共用同一边界），此处直接丢弃，不留待下次刷新
	const FString FolderPath = PendingSelectFolderPath.GetValue();
	PendingSelectFolderPath.Reset();

	TArray<TSharedRef<IHwoDimension>> EffectiveDims;
	DimensionStack->GetEffectiveDimensions(EffectiveDims);
	if (EffectiveDims.IsEmpty() || EffectiveDims[0]->GetDimensionID() != FHwoFolderDimension::StaticDimensionID())
	{
		return;
	}

	// 多段键编码：完整路径经 ExpandKeyToPathSegments 拆段后构造分组 ID（与建树同构）
	TArray<FHwoGroupKey> GroupPath;
	EffectiveDims[0]->ExpandKeyToPathSegments(FHwoGroupKey{ FHwoFolderDimension::StaticDimensionID(), FolderPath }, GroupPath);
	const FSceneOutlinerTreeItemID GroupID = HwoTreeItemId::ForGroup(GroupPath);
	if (FSceneOutlinerTreeItemPtr GroupItem = SceneOutliner->GetTreeItem(GroupID))
	{
		// 树内选择（AddToSelection：避开 SetItemSelection 的 ClearSelection 隐藏副作用），不动 GEditor 的 Actor 选择
		SceneOutliner->AddToSelection(GroupItem, ESelectInfo::Direct);

		// 展开父链并滚动定位
		FSceneOutlinerTreeItemPtr Parent = GroupItem->GetParent();
		while (Parent.IsValid())
		{
			SceneOutliner->SetItemExpansion(Parent, true);
			Parent = Parent->GetParent();
		}
		SceneOutliner->ScrollItemIntoView(GroupItem);
	}
}

void FHwoMode::AddLeafToGroupCounts(const TArray<FHwoGroupKey>& LeafPath, const FGuid& ActorGuid)
{
	// 沿路径逐级找到分组节点，累加去重计数（此时父链已由框架链接，但按 ID 反查更稳）
	for (int32 Depth = 1; Depth <= LeafPath.Num(); ++Depth)
	{
		const FSceneOutlinerTreeItemID GroupID = HwoTreeItemId::ForGroup(TArray<FHwoGroupKey>(LeafPath.GetData(), Depth));
		if (FSceneOutlinerTreeItemPtr GroupItem = SceneOutliner->GetTreeItem(GroupID))
		{
			if (FHwoGroupTreeItem* Group = GroupItem->CastTo<FHwoGroupTreeItem>())
			{
				bool bAlreadyInSet = false;
				Group->UniqueActorGuids.Add(ActorGuid, &bAlreadyInSet);
				if (!bAlreadyInSet)
				{
					DirtyGroupCountIDs.Add(GroupID);
				}
			}
		}
	}
}

void FHwoMode::RemoveLeafFromGroupCounts(const TArray<FHwoGroupKey>& LeafPath, const FGuid& ActorGuid, const FSceneOutlinerTreeItemID& RemovedLeafID)
{
	// 移除时父链可能已断开（RemoveItemFromTree 先断链再回调），分组按 ID 反查。
	// 同组同 Actor 可能有多片叶子（多路径/多 Tag），只有该 Actor 在本组再无其他叶子时才减计数。
	for (int32 Depth = 1; Depth <= LeafPath.Num(); ++Depth)
	{
		const TArray<FHwoGroupKey> GroupPath(LeafPath.GetData(), Depth);
		const FSceneOutlinerTreeItemID GroupID = HwoTreeItemId::ForGroup(GroupPath);

		FSceneOutlinerTreeItemPtr GroupItem = SceneOutliner->GetTreeItem(GroupID);
		FHwoGroupTreeItem* Group = GroupItem.IsValid() ? GroupItem->CastTo<FHwoGroupTreeItem>() : nullptr;
		if (!Group)
		{
			continue;
		}

		bool bHasOtherLeafInGroup = false;
		for (TMultiMap<FGuid, FSceneOutlinerTreeItemID>::TConstKeyIterator It(ActorGuidToLeafIds, ActorGuid); It; ++It)
		{
			const FSceneOutlinerTreeItemID& OtherLeafID = It.Value();
			if (OtherLeafID == RemovedLeafID)
			{
				continue;
			}

			const FSceneOutlinerTreeItemPtr OtherLeaf = SceneOutliner->GetTreeItem(OtherLeafID, /*bIncludePending*/true);
			if (!OtherLeaf.IsValid())
			{
				continue;
			}

			const TArray<FHwoGroupKey>* OtherPath = nullptr;
			if (const FHwoActorTreeItem* ActorItem = OtherLeaf->CastTo<FHwoActorTreeItem>())
			{
				OtherPath = &ActorItem->Path;
			}
			else if (const FHwoActorDescTreeItem* DescItem = OtherLeaf->CastTo<FHwoActorDescTreeItem>())
			{
				OtherPath = &DescItem->Path;
			}

			// 其他叶子的路径以本组路径为前缀 → 仍属本组
			if (OtherPath && OtherPath->Num() >= Depth)
			{
				bool bPrefixMatch = true;
				for (int32 PathIndex = 0; PathIndex < Depth; ++PathIndex)
				{
					if (!((*OtherPath)[PathIndex] == GroupPath[PathIndex]))
					{
						bPrefixMatch = false;
						break;
					}
				}
				if (bPrefixMatch)
				{
					bHasOtherLeafInGroup = true;
					break;
				}
			}
		}

		if (!bHasOtherLeafInGroup && Group->UniqueActorGuids.Remove(ActorGuid) > 0)
		{
			DirtyGroupCountIDs.Add(GroupID);
		}
	}
}

void FHwoMode::SaveViewOptions() const
{
	// 先读回已有文件：同一 ini 还保存维度堆叠（DimensionStack 节），避免整文件覆盖
	const FString ConfigPath = FHwoDimensionStack::GetConfigFilePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath), /*Tree*/true);

	FConfigFile ConfigFile;
	ConfigFile.Read(ConfigPath);

	// 过滤开关的当前状态以 FilterInfoMap 为准（单一事实源），成员变量直接读
	ConfigFile.SetBool(HwoViewOptionsConfig::SectionName, HwoViewOptionsConfig::KeyOnlySelected,
		FilterInfoMap.FindChecked(TEXT("ShowOnlySelectedActors")).IsFilterActive());
	ConfigFile.SetBool(HwoViewOptionsConfig::SectionName, HwoViewOptionsConfig::KeyOnlyCurrentLevel,
		FilterInfoMap.FindChecked(TEXT("ShowOnlyCurrentLevel")).IsFilterActive());
	ConfigFile.SetBool(HwoViewOptionsConfig::SectionName, HwoViewOptionsConfig::KeyHideUnloaded, bHideUnloadedActors);
	ConfigFile.SetBool(HwoViewOptionsConfig::SectionName, HwoViewOptionsConfig::KeyAlwaysFrame, bAlwaysFrameSelection);

	ConfigFile.Write(ConfigPath);
}

bool FHwoMode::HasMultiPathSelectedActor() const
{
	// 遍历树选中集的叶子，查索引中该 Actor 的叶子数（D2 语义下选中 Actor 的全部实例本就都被选中）
	const FSceneOutlinerItemSelection Selection(SceneOutliner->GetSelection());
	TSet<FGuid> CheckedGuids;
	for (const TWeakPtr<ISceneOutlinerTreeItem>& WeakItem : Selection.SelectedItems)
	{
		const FSceneOutlinerTreeItemPtr Item = WeakItem.Pin();
		if (!Item.IsValid())
		{
			continue;
		}

		FGuid ActorGuid;
		if (const FHwoActorTreeItem* ActorItem = Item->CastTo<FHwoActorTreeItem>())
		{
			ActorGuid = ActorItem->ActorGuid;
		}
		else if (const FHwoActorDescTreeItem* DescItem = Item->CastTo<FHwoActorDescTreeItem>())
		{
			ActorGuid = DescItem->CachedActorGuid;
		}
		else
		{
			continue;
		}
		if (!ActorGuid.IsValid())
		{
			continue;
		}

		bool bAlreadyChecked = false;
		CheckedGuids.Add(ActorGuid, &bAlreadyChecked);
		if (bAlreadyChecked)
		{
			continue;
		}

		TArray<FSceneOutlinerTreeItemID> LeafIds;
		ActorGuidToLeafIds.MultiFind(ActorGuid, LeafIds);
		if (LeafIds.Num() > 1)
		{
			return true;
		}
	}
	return false;
}
