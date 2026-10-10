// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "Internationalization/PolyglotTextData.h"

// 插件本地化宏：命名空间统一 "HierarchicalWorldOutliner"；英文为源文化（NativeCulture=en），
// zh / zh-Hans / zh-CN 三键同文注册（引擎本地化回退链是精确字符串匹配，三键缺一不可）。
//
// 必须 static 缓存：FPolyglotTextData::GetText 每次新建都会 UpdateLiveTable 并使全局 FText
// 修订号递增；本插件存在每帧取值路径（状态栏），不缓存会导致全编辑器 FText 每帧过期。
//
// 用法：HWO_TEXT("分类.语义", "English", "中文")，Key 全插件唯一、跨文件不复用；
// 新代码禁止再写 LOCTEXT / NSLOCTEXT
#define HWO_TEXT(Key, English, Chinese) \
	([]() -> const FText& { \
		static const FText CachedText = []() { \
			FPolyglotTextData PolyglotData(ELocalizedTextSourceCategory::Editor, \
				TEXT("HierarchicalWorldOutliner"), TEXT(Key), TEXT(English), TEXT("en")); \
			PolyglotData.AddLocalizedString(TEXT("zh"), TEXT(Chinese)); \
			PolyglotData.AddLocalizedString(TEXT("zh-Hans"), TEXT(Chinese)); \
			PolyglotData.AddLocalizedString(TEXT("zh-CN"), TEXT(Chinese)); \
			return PolyglotData.GetText(); \
		}(); \
		return CachedText; \
	}())
