// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Widgets/SCompoundWidget.h"

class FHwoDimensionStack;
class SHwoDimensionBar;
class SHwoOutliner;

/**
 * 层级世界大纲面板容器：维度 Chip 栏（SHwoDimensionBar）+ 大纲树（SHwoOutliner）
 *
 * 持有 FHwoDimensionStack 并与 FHwoMode / FHwoHierarchy 共享；堆叠增删与拖拽排序变更时统一持久化到 ini 并整树 FullRefresh。
 * 由 FHierarchicalWorldOutlinerModule 注册的 Tab 生成；MVP 不含结构化过滤行与预设按钮（v0.2 规划）。
 */
class SHwoPanel : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SHwoPanel) {}
	SLATE_END_ARGS()

private:
	// 维度堆叠数据源，与 FHwoMode / FHwoHierarchy 共享；初始为空栈（D3：等效单层文件夹维度）
	TSharedPtr<FHwoDimensionStack> DimensionStack;

	// 维度 Chip 栏
	TSharedPtr<SHwoDimensionBar> DimensionBar;

	// 大纲树本体
	TSharedPtr<SHwoOutliner> Outliner;

	// 整树刷新延迟标记（一帧内多次堆叠变更合并为一次定时器，见 OnDimensionStackChanged）
	bool bOutlinerRefreshPending = false;

public:
	virtual ~SHwoPanel();

	/**
	 *	Slate 初始化入口，由 SNew 调用：创建维度堆叠并恢复上次会话配置，组装 SHwoOutliner 与 FHwoMode
	 *
	 *	@param	InArgs	SNew 传入的构造参数
	 */
	void Construct(const FArguments& InArgs);

private:
	/**
	 *	维度堆叠变更（增 / 删 / 拖拽排序）回调：同步持久化堆叠配置；整树 FullRefresh 延迟一帧
	 *	（OnStackChanged 可能在 Slate 事件路由中广播，重建树控件属结构性销毁，不能在路由中执行）
	 */
	void OnDimensionStackChanged();
};
