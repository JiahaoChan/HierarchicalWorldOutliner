// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SSceneOutliner.h"

struct FSceneOutlinerInitializationOptions;

/**
 * 层级世界大纲树控件：SSceneOutliner 的薄子类（先例 SDataLayerOutliner）
 *
 * MVP 阶段无覆写需求，Construct 仅透传基类；为 v0.2 的工具栏扩展等预留派生点。
 * 由 SHwoPanel 在 Construct 中以 SNew 创建并持有。
 */
class SHwoOutliner : public SSceneOutliner
{
public:
	SHwoOutliner() {}

	virtual ~SHwoOutliner() = default;

	/**
	 *	Slate 初始化入口，透传基类 SSceneOutliner::Construct
	 *
	 *	@param	InArgs		SNew 传入的构造参数
	 *	@param	InitOptions	SceneOutliner 初始化参数（列组、ModeFactory 等，由 SHwoPanel 组装）
	 */
	void Construct(const FArguments& InArgs, const FSceneOutlinerInitializationOptions& InitOptions)
	{
		SSceneOutliner::Construct(InArgs, InitOptions);
	}
};
