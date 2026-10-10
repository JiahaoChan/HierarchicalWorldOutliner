# Hierarchical World Outliner

![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.8-313131?logo=unrealengine)
![Version](https://img.shields.io/badge/version-0.1.0-blue)
![Type](https://img.shields.io/badge/type-Editor%20Plugin-green)
![License](https://img.shields.io/badge/license-Proprietary-lightgrey)

A multi-dimensional pivot view over the Unreal Engine World Outliner. Regroup scene actors by stackable dimensions — DataLayer, Class, Folder, Tag, HLOD, and more — without touching the original level hierarchy.

---

## Overview

The built-in World Outliner only shows one view of your level: the folder hierarchy. **Hierarchical World Outliner** adds a second, fully independent outliner panel that lets you *pivot* the same actor set by any combination of grouping dimensions, stacked in whatever order you need. It is built on top of the engine's `SceneOutliner` framework (the same foundation as the official DataLayers panel), so virtualization, search, filtering, columns and World Partition support come for free.

## Features

- **9 built-in grouping dimensions**, freely stackable and reorderable via a dimension chip bar:
  `DataLayer` · `Class` · `Folder` · `Tag` · `HLOD` · `Unsaved` · `LocationVolume` · `RuntimeGrid` · `SourceControl`
- **Native-feeling tree** — row virtualization, incremental refresh with a per-frame processing budget, text search & filters, visibility gutter column, and right-click context menus, all inherited from `SceneOutliner`.
- **World Partition ready** — unloaded actors appear through the engine's official `ActorDesc` proxy items; nothing is force-loaded.
- **Bidirectional selection sync** — selecting an actor in the viewport reveals and selects all of its dimension instances in the panel.
- **Graceful empty state** — with no dimension active, the panel falls back to the classic folder view.
- **Persistent workspace** — dimension stack and panel state are saved per project (`Saved/HierarchicalWorldOutliner/OutlinerState.ini`).
- **Editor-only, zero runtime cost** — the plugin ships a single Editor module and never touches packaged games.

## Installation

1. Clone or copy this repository into your project's `Plugins/` folder:
   ```
   <YourProject>/Plugins/HierarchicalWorldOutliner/
   ```
2. Regenerate project files and build, or let the editor compile the plugin on startup.
3. Enable **Hierarchical World Outliner** in *Edit → Plugins* and restart the editor.

## Usage

- Open the panel from the Level Editor tab menu (**Window → Hierarchical World Outliner**); it docks side by side with the built-in World Outliner.
- Click dimension chips in the top bar to stack grouping dimensions; drag to reorder, click again to remove.
- Right-click group nodes for contextual operations (e.g. create folder).

## Compatibility

| Engine Version | Status |
|----------------|--------|
| UE 5.8         | ✅ Tested |

Pure editor plugin — no engine source modifications required. Other 5.x versions are untested but likely work with a recompile.

## Documentation

- Technical design (Chinese): `Doc/层级世界大纲插件_TDD.md`
- Product requirements (Chinese): `Doc/层级世界大纲插件_PRD.docx`

## License & Copyright

Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

---

## 中文简介

**层级世界大纲**为 UE 场景大纲提供多维度透视视图：把场景 Actor 按可自由堆叠的维度（DataLayer / Class / Folder / Tag / HLOD / Unsaved / LocationVolume / RuntimeGrid / SourceControl）重新分组，复用引擎 SceneOutliner 框架（虚拟化树、增量刷新、搜索过滤、World Partition 未加载 Actor 代理节点），支持视口选择双向同步与面板状态持久化。纯编辑器插件，无运行时开销。
