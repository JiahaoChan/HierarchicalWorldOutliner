// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "HierarchicalWorldOutlinerModule.h"

#include "HwoLocalization.h"
#include "HwoStyle.h"
#include "Widgets/SHwoPanel.h"

#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/TabManager.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

const FName FHierarchicalWorldOutlinerModule::HwoTabId("HierarchicalWorldOutliner");

namespace HwoModuleText
{
	/**
	 *	获取 Tab 标题（菜单入口 / Tab spawner / DockTab 共用同一 Key，收敛为单一访问点避免文案漂移）
	 *
	 *	@return	本地化的 Tab 标题
	 */
	const FText& GetTabLabel()
	{
		return HWO_TEXT("Tab.Label", "Hierarchical Outliner", "层级世界大纲");
	}
}

void FHierarchicalWorldOutlinerModule::StartupModule()
{
	if (IsRunningGame())
	{
		return;
	}

	// 等 ToolMenus 就绪后再挂接 LevelEditor（先例 WorldPartitionEditorModule.cpp:326）
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FHierarchicalWorldOutlinerModule::RegisterMenus));
}

void FHierarchicalWorldOutlinerModule::ShutdownModule()
{
	if (IsRunningGame())
	{
		return;
	}

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	if (FLevelEditorModule* LevelEditorModule = FModuleManager::Get().GetModulePtr<FLevelEditorModule>("LevelEditor"))
	{
		LevelEditorModule->OnRegisterTabs().RemoveAll(this);
		LevelEditorModule->OnRegisterLayoutExtensions().RemoveAll(this);

		if (LevelEditorModule->GetLevelEditorTabManager())
		{
			LevelEditorModule->GetLevelEditorTabManager()->UnregisterTabSpawner(HwoTabId);
		}
	}
}

void FHierarchicalWorldOutlinerModule::RegisterMenus()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	// 注册进 LevelEditor 的 Tab 体系：与内置 World Outliner 同体系、可停靠、随布局持久化
	LevelEditorModule.OnRegisterTabs().AddRaw(this, &FHierarchicalWorldOutlinerModule::RegisterHwoTab);

	// 默认布局扩展：插在内置 World Outliner 之后、默认打开（先例 ColorGradingEditorModule.cpp:86）
	LevelEditorModule.OnRegisterLayoutExtensions().AddRaw(this, &FHierarchicalWorldOutlinerModule::RegisterLayoutExtensions);

	// 「窗口」菜单入口
	FToolMenuOwnerScoped OwnerScoped(this);
	if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window"))
	{
		FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
		Section.AddMenuEntry(
			"HierarchicalWorldOutliner",
			HWO_TEXT("Menu.OpenTab", "Hierarchical World Outliner", "层级世界大纲"),
			HWO_TEXT("Menu.OpenTabTooltip", "Open the Hierarchical World Outliner: a multi-dimensional pivot over the World Outliner.", "打开层级世界大纲：多维透视的 World Outliner。"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"),
			FUIAction(FExecuteAction::CreateRaw(this, &FHierarchicalWorldOutlinerModule::InvokeHwoTab))
		);
	}
}

void FHierarchicalWorldOutlinerModule::RegisterHwoTab(TSharedPtr<FTabManager> InTabManager)
{
	const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();

	InTabManager->RegisterTabSpawner(HwoTabId, FOnSpawnTab::CreateRaw(this, &FHierarchicalWorldOutlinerModule::SpawnHwoTab))
		.SetDisplayName(HwoModuleText::GetTabLabel())
		.SetTooltipText(HWO_TEXT("Tab.Tooltip", "Hierarchical World Outliner: pivot scene actors by stacked dimensions such as DataLayer / Class / Folder / Tag.", "层级世界大纲：按 DataLayer / 类 / 文件夹 / 标签等维度堆叠透视场景 Actor。"))
		.SetGroup(MenuStructure.GetLevelEditorOutlinerCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"));
}

TSharedRef<SDockTab> FHierarchicalWorldOutlinerModule::SpawnHwoTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(HwoModuleText::GetTabLabel())
		// Tab 图标染品牌紫：与状态栏文字同色（HwoStyle::AccentPurple，SDockTab 自带非激活 70% 不透明度）
		.IconColor(HwoStyle::AccentPurple)
		[
			SNew(SHwoPanel)
		];
}

void FHierarchicalWorldOutlinerModule::RegisterLayoutExtensions(FLayoutExtender& Extender)
{
	// After = 与谓词 Tab 同一 Stack、紧随其后；OpenedTab = 布局恢复时立即生成（先例 SLevelEditorToolBox.cpp:50）
	// ProcessExtensions 内部有防重复检查：用户布局中已存在本 Tab 时不重复插入
	Extender.ExtendLayout(
		LevelEditorTabIds::LevelEditorSceneOutliner,
		ELayoutExtensionPosition::After,
		FTabManager::FTab(FTabId(HwoTabId), ETabState::OpenedTab));
}

void FHierarchicalWorldOutlinerModule::InvokeHwoTab() const
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	if (const TSharedPtr<FTabManager> TabManager = LevelEditorModule.GetLevelEditorTabManager())
	{
		TabManager->TryInvokeTab(HwoTabId);
	}
}

IMPLEMENT_MODULE(FHierarchicalWorldOutlinerModule, HierarchicalWorldOutliner)
