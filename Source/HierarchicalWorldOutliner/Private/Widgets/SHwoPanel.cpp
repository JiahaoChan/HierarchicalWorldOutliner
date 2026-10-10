// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "Widgets/SHwoPanel.h"

#include "Dimensions/HwoDimensionStack.h"
#include "HwoLocalization.h"
#include "HwoMode.h"
#include "TreeItems/HwoGroupTreeItem.h"
#include "Widgets/SHwoDimensionBar.h"
#include "Widgets/SHwoOutliner.h"

#include "ActorDescTreeItem.h"
#include "ActorMode.h"
#include "ActorTreeItem.h"
#include "LevelEditor.h"
#include "LevelEditorOutlinerSettings.h"
#include "Modules/ModuleManager.h"
#include "SSceneOutliner.h"
#include "SceneOutlinerModule.h"
#include "SceneOutlinerPublicTypes.h"
#include "SceneOutlinerTextInfoColumn.h"
#include "Widgets/SBoxPanel.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldTreeItem.h"

/**
 *	类型列的取值函子：引擎 ActorInfo 列的类型串由硬编码静态函子产出（SceneOutlinerActorInfoColumn.cpp 的
 *	FGetInfo），不认识我们的分组节点（显示为空）。此处补充分组类型串，叶子与世界节点沿用原生口径
 *
 *	@param	Item	树节点
 *	@return	类型显示串；无法识别时返回空串
 */
static FString GetHwoItemTypeText(const ISceneOutlinerTreeItem& Item)
{
	if (const FHwoGroupTreeItem* GroupItem = Item.CastTo<FHwoGroupTreeItem>())
	{
		// 文件夹分组对齐原生文件夹节点的 "Folder" 类型串
		if (GroupItem->IsFolderDimensionGroup())
		{
			return HWO_TEXT("Column.Type.Folder", "Folder", "文件夹").ToString();
		}
		// 其余维度分组显示维度名（语义：本行是一个"维度分组"）
		if (const TSharedPtr<IHwoDimension> Dim = GroupItem->Dimension.Pin())
		{
			return Dim->GetDisplayName().ToString();
		}
		return FString();
	}
	if (const FActorTreeItem* ActorItem = Item.CastTo<FActorTreeItem>())
	{
		const AActor* Actor = ActorItem->Actor.Get();
		return Actor ? Actor->GetClass()->GetName() : FString();
	}
	if (Item.IsA<FWorldTreeItem>())
	{
		return HWO_TEXT("Column.Type.World", "World", "世界场景").ToString();
	}
	if (const FActorDescTreeItem* DescItem = Item.CastTo<FActorDescTreeItem>())
	{
		if (const FWorldPartitionActorDescInstance* DescInstance = *DescItem->ActorDescHandle)
		{
			return DescInstance->GetDisplayClassNameString();
		}
	}
	return FString();
}

SHwoPanel::~SHwoPanel()
{
	if (DimensionStack.IsValid())
	{
		DimensionStack->OnStackChanged.RemoveAll(this);
	}
}

void SHwoPanel::Construct(const FArguments& InArgs)
{
	// 初始为空栈（D3：等效单层文件夹维度，观感与自带 World Outliner 一致）
	DimensionStack = MakeShared<FHwoDimensionStack>();

	// 建树前恢复上次会话的维度堆叠（LoadFromConfig 不触发 OnStackChanged，避免重复刷新）
	DimensionStack->LoadFromConfig();

	// 组装 SSceneOutliner 初始化参数（先例 SDataLayerBrowser.cpp:141-157）
	FSceneOutlinerInitializationOptions InitOptions;
	// 与内置 World Outliner 同款的完整列组（Label/Gutter(可视性)/ActorInfo(类型)/SourceControl/Unsaved），
	// 且经 OnCreateActorBrowserColumns 广播自动获得 WorldPartitionEditor 追加的 Pinned(强制加载) 列
	// （SceneOutlinerModule.cpp:216-245；不传 World 自动取 LevelEditor 当前世界）
	FModuleManager::LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner").CreateActorBrowserColumns(InitOptions);

	// 类型列替换为自定义文本列（引擎 ActorInfo 列不认识分组节点，见 GetHwoItemTypeText 注释）。
	// 代价：失去原生类型列的类超链接；按类型搜索不受影响（FTextInfoColumn::PopulateSearchStrings 会把本函子输出喂进搜索串，SceneOutlinerTextInfoColumn.cpp:63-70）
	InitOptions.ColumnMap[FSceneOutlinerBuiltInColumnTypes::ActorInfo()] = FSceneOutlinerColumnInfo(
		ESceneOutlinerColumnVisibility::Visible, 20,
		FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& InOutliner)
		{
			return FTextInfoColumn::CreateTextInfoColumn(InOutliner, FSceneOutlinerBuiltInColumnTypes::ActorInfo(),
				FGetTextForItem::CreateStatic(&GetHwoItemTypeText),
				HWO_TEXT("Column.Type.Tooltip", "Item type", "条目类型"));
		}),
		true, TOptional<float>(), FSceneOutlinerBuiltInColumnTypes::ActorInfo_Localized());
	InitOptions.OutlinerIdentifier = TEXT("HierarchicalWorldOutliner"); // 列布局等配置持久化（UOutlinerConfig）
	InitOptions.bShowCreateNewFolder = true;                             // 新建包含当前选中项的文件夹（FHwoMode::CreateNewFolder 实现）

	// 过滤栏：与内置 World Outliner 同一套过滤器与展开分类（对齐 SLevelEditor.cpp:807-816），
	// 自定义文本过滤与 LevelEditor 各 Outliner 共享（bUseSharedSettings）
	InitOptions.FilterBarOptions.bHasFilterBar = true;
	InitOptions.FilterBarOptions.bUseSharedSettings = true;

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	const TSharedPtr<FLevelEditorOutlinerSettings> OutlinerSettings = LevelEditorModule.GetLevelEditorOutlinerSettings();
	OutlinerSettings->GetOutlinerFilters(InitOptions.FilterBarOptions);
	InitOptions.FilterBarOptions.CategoryToExpand = OutlinerSettings->GetFilterCategory(FLevelEditorOutlinerBuiltInCategories::Common());

	TSharedPtr<FHwoDimensionStack> Stack = DimensionStack;
	InitOptions.ModeFactory = FCreateSceneOutlinerMode::CreateLambda([Stack](SSceneOutliner* InOutliner) -> ISceneOutlinerMode*
	{
		FActorModeParams Params(InOutliner);
		Params.bHideUnloadedActors = false; // D4：MVP 含 WP 未加载 Actor
		return new FHwoMode(Params, Stack.ToSharedRef());
	});

	ChildSlot
	[
		SNew(SVerticalBox)

		// 维度 Chip 栏（顶部留 9px，避免贴边）
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.f, 9.f, 2.f, 2.f)
		[
			SAssignNew(DimensionBar, SHwoDimensionBar)
			.Stack(DimensionStack)
		]

		// 树本体
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SAssignNew(Outliner, SHwoOutliner, InitOptions)
		]
	];

	DimensionStack->OnStackChanged.AddSP(this, &SHwoPanel::OnDimensionStackChanged);
}

void SHwoPanel::OnDimensionStackChanged()
{
	// 增/删/拖拽排序都走 OnStackChanged，此处统一持久化（无控件操作，保持同步）
	DimensionStack->SaveToConfig();

	// 该委托可能在 Slate 事件路由中广播（Chip 的 × 点击 / 拖拽放下）——FullRefresh 会重建树控件，
	// 结构性销毁必须延迟一帧；一帧内多次变更由 pending 标记合并。
	// 定时器挂在控件上：控件销毁则不再触发，天然安全
	if (bOutlinerRefreshPending)
	{
		return;
	}
	bOutlinerRefreshPending = true;
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double InCurrentTime, float InDeltaTime)
	{
		bOutlinerRefreshPending = false;
		if (Outliner.IsValid())
		{
			Outliner->FullRefresh();
		}
		return EActiveTimerReturnType::Stop;
	}));
}
