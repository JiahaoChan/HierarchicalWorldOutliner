// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Modules/ModuleInterface.h"

class FLayoutExtender;
class FSpawnTabArgs;
class FTabManager;
class SDockTab;

/**
 * 层级世界大纲插件模块（入口）
 *
 * 启动时通过 FLevelEditorModule::OnRegisterTabs 向 LevelEditor 注册与内置 World Outliner 同体系的并排 Tab
 * （先例 WorldPartitionEditorModule），并在「窗口」菜单追加入口。
 * 游戏模式（IsRunningGame）下不执行任何注册；模块生命周期由引擎管理（IMPLEMENT_MODULE）。
 */
class FHierarchicalWorldOutlinerModule : public IModuleInterface
{
private:
	// Tab 的唯一标识名（Tab 注册 / 唤起共用）
	static const FName HwoTabId;

public:
	//~ Begin IModuleInterface Interface
	virtual void StartupModule() override;

	virtual void ShutdownModule() override;
	//~ End IModuleInterface Interface

private:
	/**
	 *	挂接 FLevelEditorModule::OnRegisterTabs 并扩展「窗口」菜单（UToolMenus 启动回调中执行）
	 */
	void RegisterMenus();

	/**
	 *	向 LevelEditor 的 TabManager 注册本插件 Tab
	 *
	 *	@param	InTabManager	LevelEditor 的 Tab 管理器
	 */
	void RegisterHwoTab(TSharedPtr<FTabManager> InTabManager);

	/**
	 *	默认布局扩展回调：把本插件 Tab 插入内置 World Outliner 之后并默认打开
	 *
	 *	@param	Extender	布局扩展器（FLevelEditorModule::OnRegisterLayoutExtensions 广播时传入，作用于实例化前的布局）
	 */
	void RegisterLayoutExtensions(FLayoutExtender& Extender);

	/**
	 *	生成 Tab 内容（SHwoPanel）
	 *
	 *	@param	Args	Tab 生成参数
	 *	@return	承载 SHwoPanel 的停靠 Tab
	 */
	TSharedRef<SDockTab> SpawnHwoTab(const FSpawnTabArgs& Args);

	/**
	 *	菜单入口：唤起（或聚焦）本插件 Tab
	 */
	void InvokeHwoTab() const;
};
