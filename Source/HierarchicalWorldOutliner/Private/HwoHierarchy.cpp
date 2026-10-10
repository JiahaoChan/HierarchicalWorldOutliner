// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "HwoHierarchy.h"

#include "Dimensions/HwoBuiltInDimensions.h"
#include "HwoMode.h"
#include "TreeItems/HwoActorDescTreeItem.h"
#include "TreeItems/HwoActorTreeItem.h"
#include "TreeItems/HwoGroupTreeItem.h"

#include "ActorDescTreeItem.h"
#include "Containers/Ticker.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "Editor.h"
#include "EditorActorFolders.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "LocationVolume.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "SourceControlHelpers.h"
#include "UnsavedAssetsTrackerModule.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "WorldTreeItem.h"

FHwoHierarchy::FHwoHierarchy(FHwoMode* InMode, const TWeakObjectPtr<UWorld>& InWorld, TSharedRef<FHwoDimensionStack> InStack, bool bInShowUnloadedActors)
	: ISceneOutlinerHierarchy(InMode)
	, HwoMode(InMode)
	, RepresentingWorld(InWorld)
	, Stack(InStack)
	, bShowUnloadedActors(bInShowUnloadedActors)
{
	BindEvents();
}

FHwoHierarchy::~FHwoHierarchy()
{
	UnbindEvents();
}

void FHwoHierarchy::CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const
{
	UWorld* World = RepresentingWorld.Get();
	if (!World)
	{
		return;
	}

	// 0) 世界根节点（复用引擎 FWorldTreeItem，全导出：标签/图标/类型列/右键菜单与原生一致），
	// 所有顶层节点（维度分组、未分配组、空文件夹分组）都挂在它下面
	if (FSceneOutlinerTreeItemPtr WorldItem = Mode->CreateItemFor<FWorldTreeItem>(World))
	{
		OutItems.Add(WorldItem);
	}

	// 1) 已加载 Actor
	for (AActor* Actor : FActorRange(World))
	{
		if (Actor && HwoMode->IsActorDisplayableForHwo(Actor))
		{
			CreateLeafItemsForActor(Actor, OutItems);
		}
	}

	// 2) WP 未加载 Actor（D4）
	if (bShowUnloadedActors)
	{
		if (UWorldPartition* WorldPartition = World->GetWorldPartition())
		{
			// 与已加载枚举去重的守卫，先例 FActorHierarchy::CreateItems
			const TMap<FGuid, AActor*> LoadedActors = FWorldPartitionHelpers::GetLoadedActorsForLevel(World->PersistentLevel);
			const TMap<FGuid, AActor*> RegisteredActors = FWorldPartitionHelpers::GetRegisteredActorsForLevel(World->PersistentLevel);

			FWorldPartitionHelpers::ForEachActorDescInstance(WorldPartition, AActor::StaticClass(),
				[this, &LoadedActors, &RegisteredActors, &OutItems](const FWorldPartitionActorDescInstance* ActorDescInstance)
			{
				if (FActorDescTreeItem::ShouldDisplayInOutliner(ActorDescInstance))
				{
					if (AActor* const* LoadedActor = LoadedActors.Find(ActorDescInstance->GetGuid()))
					{
						if (!IsValid(*LoadedActor) || RegisteredActors.Contains(ActorDescInstance->GetGuid()))
						{
							return true;
						}
					}
					CreateLeafItemsForActorDesc(ActorDescInstance, OutItems);
				}
				return true;
			});
		}
	}

	// 3) 空文件夹分组（Bug-P7）：文件夹维度的分组本由 Actor 的 FolderPath 数据派生，空文件夹无任何 Actor
	// 故而不可见；此处枚举 FActorFolders 中已注册的文件夹补齐零成员分组（计数徽标显示 0）。
	// 仅当文件夹维度是最外层有效维度时枚举——非最外层时分组键缺少外层维度的值，无法定位
	if (IsFolderDimensionOutermost())
	{
		TArray<TSharedRef<IHwoDimension>> EffectiveDims;
		Stack->GetEffectiveDimensions(EffectiveDims);
		const TSharedRef<IHwoDimension>& FolderDim = EffectiveDims[0];

		FActorFolders::Get().ForEachFolderWithRootObject(*World, FFolder::GetWorldRootFolder(World).GetRootObject(),
			[this, &OutItems, &FolderDim](const FFolder& InFolder) -> bool
		{
			if (InFolder.GetPath().IsNone())
			{
				return true; // 跳过根
			}
			// 完整路径经 ExpandKeyToPathSegments 拆成多段（"A/B" → 嵌套两级分组），与 Actor 派生分组同构
			TArray<FHwoGroupKey> GroupPath;
			FolderDim->ExpandKeyToPathSegments(FHwoGroupKey{ FolderDim->GetDimensionID(), InFolder.GetPath().ToString() }, GroupPath);
			if (FSceneOutlinerTreeItemPtr GroupItem = CreateGroupItemForPath(GroupPath, /*bForce*/false))
			{
				OutItems.Add(GroupItem);
			}
			return true;
		});
	}
}

FSceneOutlinerTreeItemPtr FHwoHierarchy::FindOrCreateParentItem(const ISceneOutlinerTreeItem& Item, const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items, bool bCreate)
{
	// 取 item 的路径：叶子 = 全路径（每一段都是分组键），分组 = 自身路径
	const TArray<FHwoGroupKey>* Path = nullptr;
	bool bIsLeaf = false;
	if (const FHwoGroupTreeItem* GroupItem = Item.CastTo<FHwoGroupTreeItem>())
	{
		Path = &GroupItem->Path;
	}
	else if (const FHwoActorTreeItem* ActorItem = Item.CastTo<FHwoActorTreeItem>())
	{
		Path = &ActorItem->Path;
		bIsLeaf = true;
	}
	else if (const FHwoActorDescTreeItem* DescItem = Item.CastTo<FHwoActorDescTreeItem>())
	{
		Path = &DescItem->Path;
		bIsLeaf = true;
	}

	if (!Path || Path->IsEmpty())
	{
		return nullptr; // 世界根节点等非分组类型：挂根
	}

	// 叶子的父 = 全路径对应的最深分组；分组的父 = 去掉自身末段
	const int32 ParentDepth = bIsLeaf ? Path->Num() : Path->Num() - 1;
	if (ParentDepth <= 0)
	{
		// 顶层节点挂到世界根节点下（ID 为 FObjectKey(World)，与 CreateItems 第 0 步一致）
		UWorld* World = RepresentingWorld.Get();
		if (!World)
		{
			return nullptr;
		}
		const FSceneOutlinerTreeItemID WorldID(World);
		if (const FSceneOutlinerTreeItemPtr* FoundItem = Items.Find(WorldID))
		{
			return *FoundItem;
		}
		if (bCreate)
		{
			return Mode->CreateItemFor<FWorldTreeItem>(World, /*bForce*/true);
		}
		return nullptr;
	}

	const TArray<FHwoGroupKey> ParentPath(Path->GetData(), ParentDepth);
	const FSceneOutlinerTreeItemID ParentID = HwoTreeItemId::ForGroup(ParentPath);

	if (const FSceneOutlinerTreeItemPtr* FoundItem = Items.Find(ParentID))
	{
		return *FoundItem;
	}

	if (bCreate)
	{
		// 框架会对该分组节点递归回调本函数挂上层
		return CreateGroupItemForPath(ParentPath, /*bForce*/true);
	}

	return nullptr;
}

FSceneOutlinerTreeItemPtr FHwoHierarchy::CreateGroupItemForPath(const TArray<FHwoGroupKey>& GroupPath, bool bForce) const
{
	// 分组节点显示名/图标由路径末段的维度提供
	FHwoGroupTreeItemData Data;
	Data.Path = GroupPath;
	Data.World = RepresentingWorld;
	const FHwoGroupKey& LastKey = GroupPath.Last();
	if (const TSharedPtr<IHwoDimension> Dimension = Stack->FindDimension(LastKey.DimensionID))
	{
		Data.DisplayName = Dimension->GetGroupDisplayName(LastKey);
		Data.Icon = Dimension->GetIcon();
		Data.Dimension = Dimension;
	}
	else
	{
		Data.DisplayName = FText::FromString(LastKey.Key);
	}
	return Mode->CreateItemFor<FHwoGroupTreeItem>(Data, bForce);
}

bool FHwoHierarchy::IsFolderDimensionOutermost() const
{
	TArray<TSharedRef<IHwoDimension>> EffectiveDims;
	Stack->GetEffectiveDimensions(EffectiveDims);
	return EffectiveDims.Num() > 0 && EffectiveDims[0]->GetDimensionID() == FHwoFolderDimension::StaticDimensionID();
}

TUniquePtr<FHwoHierarchy> FHwoHierarchy::Create(FHwoMode* InMode, const TWeakObjectPtr<UWorld>& InWorld, TSharedRef<FHwoDimensionStack> InStack, bool bInShowUnloadedActors)
{
	return TUniquePtr<FHwoHierarchy>(new FHwoHierarchy(InMode, InWorld, InStack, bInShowUnloadedActors));
}

void FHwoHierarchy::BindEvents()
{
	// 事件订阅先例：DataLayerHierarchy.cpp:53-84
	if (GEngine)
	{
		GEngine->OnLevelActorAdded().AddRaw(this, &FHwoHierarchy::OnLevelActorAdded);
		GEngine->OnLevelActorDeleted().AddRaw(this, &FHwoHierarchy::OnLevelActorDeleted);
		GEngine->OnLevelActorFolderChanged().AddRaw(this, &FHwoHierarchy::OnLevelActorFolderChanged);
	}

	if (UWorld* World = RepresentingWorld.Get())
	{
		if (World->PersistentLevel)
		{
			World->PersistentLevel->OnLoadedActorAddedToLevelEvent.AddRaw(this, &FHwoHierarchy::OnLoadedActorAdded);
			World->PersistentLevel->OnLoadedActorRemovedFromLevelEvent.AddRaw(this, &FHwoHierarchy::OnLoadedActorRemoved);
		}

		if (UWorldPartition* WorldPartition = World->GetWorldPartition())
		{
			WorldPartition->OnActorDescInstanceAddedEvent.AddRaw(this, &FHwoHierarchy::OnActorDescInstanceAdded);
			WorldPartition->OnActorDescInstanceRemovedEvent.AddRaw(this, &FHwoHierarchy::OnActorDescInstanceRemoved);
		}
	}

	if (UDataLayerEditorSubsystem* DataLayerEditorSubsystem = UDataLayerEditorSubsystem::Get())
	{
		DataLayerEditorSubsystem->OnActorDataLayersChanged().AddRaw(this, &FHwoHierarchy::OnActorDataLayersChanged);
	}

	FEditorDelegates::MapChange.AddRaw(this, &FHwoHierarchy::OnMapChange);
	// 注意：Undo/Redo 无需订阅 FEditorDelegates::PostUndoRedo——SSceneOutliner::PostUndo（FEditorUndoClient）已自带 FullRefresh

	// 空文件夹的创建 / 删除 / 移动不经 Actor 事件（OnLevelActorFolderChanged 只在 Actor 移文件夹时触发），单独监听
	FActorFolders::OnFolderCreated.AddRaw(this, &FHwoHierarchy::OnFolderCreatedOrDeleted);
	FActorFolders::OnFolderDeleted.AddRaw(this, &FHwoHierarchy::OnFolderCreatedOrDeleted);
	FActorFolders::OnFolderMoved.AddRaw(this, &FHwoHierarchy::OnFolderMoved);

	// Unsaved 维度：外部包未保存状态变化（资产编辑 / 保存）→ 由处理器按栈内是否含 Unsaved 维度决定刷新
	FUnsavedAssetsTrackerModule& UnsavedTracker = FModuleManager::LoadModuleChecked<FUnsavedAssetsTrackerModule>("UnsavedAssetsTracker");
	UnsavedTracker.OnUnsavedAssetAdded.AddRaw(this, &FHwoHierarchy::OnUnsavedAssetChanged);
	UnsavedTracker.OnUnsavedAssetRemoved.AddRaw(this, &FHwoHierarchy::OnUnsavedAssetChanged);

	// LocationVolume 改名：组键即 Volume 的 Actor Label，改名后组名需刷新（处理器内按类型与栈守卫过滤）
	FCoreDelegates::OnActorLabelChanged.AddRaw(this, &FHwoHierarchy::OnActorLabelChangedForHierarchy);

	// RuntimeGrid 维度：Actor 移动可能改变 Cell 归属（处理器内去抖合并）
	if (GEngine)
	{
		GEngine->OnActorMoved().AddRaw(this, &FHwoHierarchy::OnActorMoved);
	}

	// SourceControl 维度：状态批处理后的无参广播（处理器内做键级变化检测 + 去抖）+ Provider 切换
	if (FModuleManager::Get().IsModuleLoaded("SourceControl"))
	{
		ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
		SourceControlStateChangedHandle = Provider.RegisterSourceControlStateChanged_Handle(
			FSourceControlStateChanged::FDelegate::CreateRaw(this, &FHwoHierarchy::OnSourceControlStateChanged));
		SourceControlProviderChangedHandle = ISourceControlModule::Get().RegisterProviderChanged(
			FSourceControlProviderChanged::FDelegate::CreateRaw(this, &FHwoHierarchy::OnSourceControlProviderChanged));
	}

	// TODO(v0.2): Tag/类 等属性变更暂无专门事件，可挂 FCoreUObjectDelegates::OnObjectPropertyChanged 过滤后 FullRefresh
}

void FHwoHierarchy::UnbindEvents()
{
	if (GEngine)
	{
		GEngine->OnLevelActorAdded().RemoveAll(this);
		GEngine->OnLevelActorDeleted().RemoveAll(this);
		GEngine->OnLevelActorFolderChanged().RemoveAll(this);
	}

	if (UWorld* World = RepresentingWorld.Get())
	{
		if (World->PersistentLevel)
		{
			World->PersistentLevel->OnLoadedActorAddedToLevelEvent.RemoveAll(this);
			World->PersistentLevel->OnLoadedActorRemovedFromLevelEvent.RemoveAll(this);
		}

		if (UWorldPartition* WorldPartition = World->GetWorldPartition())
		{
			WorldPartition->OnActorDescInstanceAddedEvent.RemoveAll(this);
			WorldPartition->OnActorDescInstanceRemovedEvent.RemoveAll(this);
		}
	}

	if (UDataLayerEditorSubsystem* DataLayerEditorSubsystem = UDataLayerEditorSubsystem::Get())
	{
		DataLayerEditorSubsystem->OnActorDataLayersChanged().RemoveAll(this);
	}

	FEditorDelegates::MapChange.RemoveAll(this);

	FActorFolders::OnFolderCreated.RemoveAll(this);
	FActorFolders::OnFolderDeleted.RemoveAll(this);
	FActorFolders::OnFolderMoved.RemoveAll(this);

	if (FUnsavedAssetsTrackerModule* UnsavedTracker = FModuleManager::GetModulePtr<FUnsavedAssetsTrackerModule>("UnsavedAssetsTracker"))
	{
		UnsavedTracker->OnUnsavedAssetAdded.RemoveAll(this);
		UnsavedTracker->OnUnsavedAssetRemoved.RemoveAll(this);
	}

	FCoreDelegates::OnActorLabelChanged.RemoveAll(this);

	if (GEngine)
	{
		GEngine->OnActorMoved().RemoveAll(this);
	}
	// 取消未触发的去抖定时器（Hierarchy 销毁后不得再回调）
	if (ActorMovedDebounceHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ActorMovedDebounceHandle);
		ActorMovedDebounceHandle.Reset();
	}
	if (SourceControlDebounceHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SourceControlDebounceHandle);
		SourceControlDebounceHandle.Reset();
	}

	// SourceControl 委托反注册：模块 / Provider 可能已换或已卸载，关机时引擎可能已部分销毁
	// （防御先例 SceneOutlinerTreeItemSCC.cpp:171-194）
	if (!IsEngineExitRequested() && FModuleManager::Get().IsModuleLoaded("SourceControl"))
	{
		if (SourceControlProviderChangedHandle.IsValid())
		{
			ISourceControlModule::Get().UnregisterProviderChanged(SourceControlProviderChangedHandle);
		}
		if (SourceControlStateChangedHandle.IsValid())
		{
			ISourceControlModule::Get().GetProvider().UnregisterSourceControlStateChanged_Handle(SourceControlStateChangedHandle);
		}
	}
	SourceControlStateChangedHandle.Reset();
	SourceControlProviderChangedHandle.Reset();
}

void FHwoHierarchy::OnLevelActorAdded(AActor* InActor)
{
	// LocationVolume 自身的增删会改变其他 Actor 的分组归属 → FullRefresh；
	// Volume 的变换 / 形状编辑没有现成事件，不处理（已知取舍）
	if (InActor && InActor->GetWorld() == RepresentingWorld.Get() && InActor->IsA<ALocationVolume>()
		&& Stack->Contains(FHwoLocationVolumeDimension::StaticDimensionID()))
	{
		FullRefreshEvent();
		return;
	}

	if (!InActor || InActor->GetWorld() != RepresentingWorld.Get() || !HwoMode->IsActorDisplayableForHwo(InActor))
	{
		return;
	}

	FSceneOutlinerHierarchyChangedData EventData;
	EventData.Type = FSceneOutlinerHierarchyChangedData::Added;
	CreateLeafItemsForActor(InActor, EventData.Items);
	if (!EventData.Items.IsEmpty())
	{
		HierarchyChangedEvent.Broadcast(EventData);
	}
}

void FHwoHierarchy::OnLevelActorDeleted(AActor* InActor)
{
	// LocationVolume 删除同理（与 OnLevelActorAdded 的前置分支对应）
	if (InActor && InActor->GetWorld() == RepresentingWorld.Get() && InActor->IsA<ALocationVolume>()
		&& Stack->Contains(FHwoLocationVolumeDimension::StaticDimensionID()))
	{
		FullRefreshEvent();
		return;
	}

	if (!InActor || InActor->GetWorld() != RepresentingWorld.Get())
	{
		return;
	}

	FHwoActorContext Ctx;
	Ctx.Actor = InActor;

	FSceneOutlinerHierarchyChangedData EventData;
	EventData.Type = FSceneOutlinerHierarchyChangedData::Removed;
	ComputeLeafIDs(Ctx, EventData.ItemIDs);
	if (!EventData.ItemIDs.IsEmpty())
	{
		HierarchyChangedEvent.Broadcast(EventData);
	}
}

void FHwoHierarchy::OnLoadedActorAdded(AActor& InActor)
{
	if (InActor.GetWorld() != RepresentingWorld.Get())
	{
		return;
	}

	// 删掉对应的 Desc 叶子：必须用 DescInstance 上下文算 ID（先例 FActorHierarchy::RemoveActorDesc）——
	// 同一 Actor 的 Actor 侧与 Desc 侧上下文在同一维度下应产出相同键（类维度已统一为父原生类口径），
	// 但稳妥起见移除时以"创建 Desc 叶子时所用的同一上下文"为准，避免任何口径漂移导致残留。
	if (UWorldPartition* WorldPartition = InActor.GetWorld() ? InActor.GetWorld()->GetWorldPartition() : nullptr)
	{
		if (FWorldPartitionActorDescInstance* ActorDescInstance = WorldPartition->GetActorDescInstance(InActor.GetActorGuid()))
		{
			FHwoActorContext DescCtx;
			DescCtx.DescInstance = ActorDescInstance;

			FSceneOutlinerHierarchyChangedData RemovedData;
			RemovedData.Type = FSceneOutlinerHierarchyChangedData::Removed;
			ComputeLeafIDs(DescCtx, RemovedData.ItemIDs);
			if (!RemovedData.ItemIDs.IsEmpty())
			{
				HierarchyChangedEvent.Broadcast(RemovedData);
			}
		}
	}

	// 加 Actor 叶子
	OnLevelActorAdded(&InActor);
}

void FHwoHierarchy::OnLoadedActorRemoved(AActor& InActor)
{
	if (InActor.GetWorld() != RepresentingWorld.Get())
	{
		return;
	}

	// 删 Actor 叶子
	OnLevelActorDeleted(&InActor);

	// 换回 Desc 叶子
	if (bShowUnloadedActors)
	{
		if (UWorldPartition* WorldPartition = InActor.GetWorld() ? InActor.GetWorld()->GetWorldPartition() : nullptr)
		{
			if (FWorldPartitionActorDescInstance* ActorDescInstance = WorldPartition->GetActorDescInstance(InActor.GetActorGuid());
				FActorDescTreeItem::ShouldDisplayInOutliner(ActorDescInstance))
			{
				FSceneOutlinerHierarchyChangedData EventData;
				EventData.Type = FSceneOutlinerHierarchyChangedData::Added;
				CreateLeafItemsForActorDesc(ActorDescInstance, EventData.Items);
				if (!EventData.Items.IsEmpty())
				{
					HierarchyChangedEvent.Broadcast(EventData);
				}
			}
		}
	}
}

void FHwoHierarchy::OnActorDescInstanceAdded(FWorldPartitionActorDescInstance* InDescInstance)
{
	if (!bShowUnloadedActors || !InDescInstance || InDescInstance->IsLoaded(true) || !FActorDescTreeItem::ShouldDisplayInOutliner(InDescInstance))
	{
		return;
	}

	FSceneOutlinerHierarchyChangedData EventData;
	EventData.Type = FSceneOutlinerHierarchyChangedData::Added;
	CreateLeafItemsForActorDesc(InDescInstance, EventData.Items);
	if (!EventData.Items.IsEmpty())
	{
		HierarchyChangedEvent.Broadcast(EventData);
	}
}

void FHwoHierarchy::OnActorDescInstanceRemoved(FWorldPartitionActorDescInstance* InDescInstance)
{
	if (!bShowUnloadedActors || !InDescInstance)
	{
		return;
	}

	FHwoActorContext Ctx;
	Ctx.DescInstance = InDescInstance;

	FSceneOutlinerHierarchyChangedData EventData;
	EventData.Type = FSceneOutlinerHierarchyChangedData::Removed;
	ComputeLeafIDs(Ctx, EventData.ItemIDs);
	if (!EventData.ItemIDs.IsEmpty())
	{
		HierarchyChangedEvent.Broadcast(EventData);
	}
}

void FHwoHierarchy::OnActorDataLayersChanged(const TWeakObjectPtr<AActor>& InActor)
{
	const AActor* Actor = InActor.Get();
	if (Actor && Actor->GetWorld() == RepresentingWorld.Get())
	{
		FullRefreshEvent();
	}
}

void FHwoHierarchy::OnUnsavedAssetChanged(const FString& FileAbsPathname)
{
	// Unsaved 维度不在栈中时，保存状态变化不影响树
	if (!Stack->Contains(FHwoUnsavedDimension::StaticDimensionID()))
	{
		return;
	}

	// 路径过滤（该委托是全编辑器任意资产广播，不过滤会过度刷新）：
	// ① OFPA Actor 外部包（路径含 __ExternalActors__）直接相关；
	// ② 非 WP 地图的 Actor 未保存状态记在关卡包上，比对 RepresentingWorld 的包文件路径
	if (FileAbsPathname.Contains(TEXT("/__ExternalActors__/")))
	{
		FullRefreshEvent();
		return;
	}
	if (const UWorld* World = RepresentingWorld.Get())
	{
		if (const UPackage* WorldPackage = World->GetPackage())
		{
			if (FileAbsPathname == USourceControlHelpers::PackageFilename(WorldPackage->GetName()))
			{
				FullRefreshEvent();
			}
		}
	}
}

void FHwoHierarchy::OnActorLabelChangedForHierarchy(AActor* InActor)
{
	// 组键即 Volume 的 Actor Label：LocationVolume 改名后组名需刷新
	if (InActor && InActor->GetWorld() == RepresentingWorld.Get() && InActor->IsA<ALocationVolume>()
		&& Stack->Contains(FHwoLocationVolumeDimension::StaticDimensionID()))
	{
		FullRefreshEvent();
	}
}

void FHwoHierarchy::OnActorMoved(AActor* InActor)
{
	// 守卫：世界匹配 + RuntimeGrid 维度在栈 + 仅空间加载 Actor 的 Cell 归属才随位置变化
	if (!InActor || InActor->GetWorld() != RepresentingWorld.Get()
		|| !Stack->Contains(FHwoRuntimeGridDimension::StaticDimensionID())
		|| !InActor->GetIsSpatiallyLoaded())
	{
		return;
	}

	// 拖动期间高频触发：去抖 0.2s 合并（每次事件重置计时）；句柄在 UnbindEvents 取消，防销毁后回调
	FTSTicker::GetCoreTicker().RemoveTicker(ActorMovedDebounceHandle);
	ActorMovedDebounceHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this](float DeltaTime)
		{
			FullRefreshEvent();
			return false;
		}),
		0.2f);
}

void FHwoHierarchy::OnSourceControlStateChanged()
{
	// 无参广播（每批回填落库后触发）：几千包初始回填会连续几十次广播，
	// 必须做键级变化检测——只有组键真的变化才去抖刷新，否则反复全树重建
	if (!Stack->Contains(FHwoSourceControlDimension::StaticDimensionID()))
	{
		return;
	}

	FHwoSourceControlDimension* SourceControlDim = nullptr;
	if (const TSharedPtr<IHwoDimension> Dim = Stack->FindDimension(FHwoSourceControlDimension::StaticDimensionID()))
	{
		// 注册表保证该 ID 的实例类型（HwoBuiltInDimensions::CreateDimension）
		SourceControlDim = static_cast<FHwoSourceControlDimension*>(Dim.Get());
	}
	if (!SourceControlDim || !SourceControlDim->ReevaluateKeysAndDetectChanges())
	{
		return;
	}

	// 去抖 0.2s 合并（同 OnActorMoved 模式；句柄独立，UnbindEvents 对称取消）
	FTSTicker::GetCoreTicker().RemoveTicker(SourceControlDebounceHandle);
	SourceControlDebounceHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this](float DeltaTime)
		{
			FullRefreshEvent();
			return false;
		}),
		0.2f);
}

void FHwoHierarchy::OnSourceControlProviderChanged(ISourceControlProvider& OldProvider, ISourceControlProvider& NewProvider)
{
	// state-changed 句柄随迁：句柄注册在旧 Provider 的委托列表上，不随迁则收不到新 Provider 的回填广播
	// （典型场景：编辑器运行中才在偏好设置里接入 SVN）
	if (SourceControlStateChangedHandle.IsValid())
	{
		OldProvider.UnregisterSourceControlStateChanged_Handle(SourceControlStateChangedHandle);
	}
	SourceControlStateChangedHandle = NewProvider.RegisterSourceControlStateChanged_Handle(
		FSourceControlStateChanged::FDelegate::CreateRaw(this, &FHwoHierarchy::OnSourceControlStateChanged));

	// 清键缓存（懒回填会自然重建）并刷新
	if (const TSharedPtr<IHwoDimension> Dim = Stack->FindDimension(FHwoSourceControlDimension::StaticDimensionID()))
	{
		static_cast<FHwoSourceControlDimension*>(Dim.Get())->ClearKeyCache();
	}
	FullRefreshEvent();
}

void FHwoHierarchy::OnLevelActorFolderChanged(const AActor* InActor, FName OldPath)
{
	if (InActor && InActor->GetWorld() == RepresentingWorld.Get())
	{
		FullRefreshEvent();
	}
}

void FHwoHierarchy::OnFolderCreatedOrDeleted(UWorld& InWorld, const FFolder& InFolder)
{
	// 空文件夹枚举只在文件夹维度最外层时生效，其它堆叠下文件夹增删不影响树
	if (&InWorld == RepresentingWorld.Get() && IsFolderDimensionOutermost())
	{
		FullRefreshEvent();
	}
}

void FHwoHierarchy::OnFolderMoved(UWorld& InWorld, const FFolder& Src, const FFolder& Dst)
{
	if (&InWorld == RepresentingWorld.Get() && IsFolderDimensionOutermost())
	{
		FullRefreshEvent();
	}
}

void FHwoHierarchy::OnMapChange(uint32 MapFlags)
{
	FullRefreshEvent();
}

void FHwoHierarchy::FullRefreshEvent()
{
	FSceneOutlinerHierarchyChangedData EventData;
	EventData.Type = FSceneOutlinerHierarchyChangedData::FullRefresh;
	HierarchyChangedEvent.Broadcast(EventData);
}

void FHwoHierarchy::CreateLeafItemsForActor(AActor* InActor, TArray<FSceneOutlinerTreeItemPtr>& OutItems) const
{
	FHwoActorContext Ctx;
	Ctx.Actor = InActor;

	TArray<TArray<FHwoGroupKey>> Paths;
	Stack->ComputeLeafPaths(Ctx, Paths);
	for (const TArray<FHwoGroupKey>& Path : Paths)
	{
		if (FSceneOutlinerTreeItemPtr Item = Mode->CreateItemFor<FHwoActorTreeItem>(FHwoActorTreeItemData(InActor, Path)))
		{
			OutItems.Add(Item);
		}
	}
}

void FHwoHierarchy::CreateLeafItemsForActorDesc(const FWorldPartitionActorDescInstance* InDescInstance, TArray<FSceneOutlinerTreeItemPtr>& OutItems) const
{
	if (!InDescInstance)
	{
		return;
	}

	FHwoActorContext Ctx;
	Ctx.DescInstance = InDescInstance;

	TArray<TArray<FHwoGroupKey>> Paths;
	Stack->ComputeLeafPaths(Ctx, Paths);
	for (const TArray<FHwoGroupKey>& Path : Paths)
	{
		if (FSceneOutlinerTreeItemPtr Item = Mode->CreateItemFor<FHwoActorDescTreeItem>(FHwoActorDescTreeItemData(InDescInstance, Path)))
		{
			OutItems.Add(Item);
		}
	}
}

void FHwoHierarchy::ComputeLeafIDs(const FHwoActorContext& Ctx, TArray<FSceneOutlinerTreeItemID>& OutIDs) const
{
	TArray<TArray<FHwoGroupKey>> Paths;
	Stack->ComputeLeafPaths(Ctx, Paths);
	OutIDs.Reserve(OutIDs.Num() + Paths.Num());
	for (const TArray<FHwoGroupKey>& Path : Paths)
	{
		OutIDs.Add(HwoTreeItemId::ForLeaf(Path, Ctx.GetActorGuid()));
	}
}
