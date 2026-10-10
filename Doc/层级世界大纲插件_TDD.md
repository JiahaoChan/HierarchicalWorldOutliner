# 层级世界大纲（HierarchicalWorldOutliner）技术设计文档

| 项目 | 内容 |
|------|------|
| 文档版本 | V0.1（设计稿） |
| 对应 PRD | `Doc/层级世界大纲插件_PRD.docx` V1.0 |
| 覆盖范围 | v0.1 MVP 的完整设计 + v0.2~v1.0 的架构预留 |
| 目标引擎 | UE 5.8 官方预编译（`E:\UE_5.8`），纯编辑器插件，不改引擎源码 |
| 工作跟踪 | `Doc/TodoList.md`（Feature-1~19 / Design-1~7 / Bug-P1~8） |

---

## 1. 总体架构决策

### 1.1 路线：复用 SceneOutliner 框架（DataLayerEditor 范式）

不重写树控件。`SceneOutliner` 引擎模块（`Engine/Source/Editor/SceneOutliner/`）本身就是一套"自定义大纲"通用框架，Epic 官方的 DataLayer 面板（`Engine/Source/Editor/DataLayerEditor/`）即以该范式实现：

```
SDataLayerOutliner : public SSceneOutliner        // 控件子类
FDataLayerMode     : public ISceneOutlinerMode    // 行为策略
FDataLayerHierarchy: public ISceneOutlinerHierarchy // 数据→节点
FDataLayerTreeItem : public ISceneOutlinerTreeItem  // 自定义节点
```

我们照此实现 `SHwoOutliner / FHwoMode / FHwoHierarchy / FHwo*TreeItem`。

**复用直接获得的能力**（自写路线需全部重写）：

- `STreeView` 行虚拟化 + Slate 动态失效（`SOutlinerTreeView.h:19`，`SSceneOutliner.cpp:275`）；
- 增量刷新管线：`FPendingTreeOperation` 队列 + 每帧 5ms 处理预算（CVar `SceneOutliner.ProcessingBudgetPerFrame`，`SSceneOutliner.cpp:774`），十万级 Actor 渐进填充不卡帧；
- 搜索框 + `TTextFilter` 文本过滤、列系统（含可见性眼睛列 `FSceneOutlinerGutter`）、右键菜单 UToolMenus 集成、撤销钩子（`SSceneOutliner : FEditorUndoClient`）；
- World Partition 未加载 Actor 的官方代理节点 `FActorDescTreeItem`（持 `FWorldPartitionHandle`，不加载 UObject）。

### 1.2 已确认的决策记录（2026-10-10）

| # | 决策点 | 结论 |
|---|--------|------|
| D1 | 技术路线 | 复用 SceneOutliner 框架（本文档全部基于此） |
| D2 | 选择同步语义 | 视口选中 Actor → 大纲中该 Actor 的**全部**维度实例置选中态，滚动定位第一个 |
| D3 | 维度 Chip 栏空态 | 回退为纯文件夹维度，观感与自带 World Outliner 一致 |
| D4 | MVP 范围 | **包含** WP 未加载 Actor（复用 `FActorDescTreeItem` 方案） |
| D5 | 面板形态 | 注册进 LevelEditor 的 Tab 系统，与内置 World Outliner 并排 |
| D6 | Mode 基类 | 继承 `FActorMode`（不继承 `FActorBrowsingMode`） |

---

## 2. 引擎框架基础（SceneOutliner 分层回顾）

证据均来自 `E:/UE_5.8/Engine/Source/Editor/SceneOutliner/`（预编译安装含完整 Private 源码）。

```
┌─────────────────────────────────────────────────────────┐
│ SSceneOutliner (Public/SSceneOutliner.h:238)            │
│   控件壳：持有 TreeItemMap[ID→Item] + RootTreeItems      │
│   管搜索/过滤/列/选择/增量刷新管线，FEditorUndoClient     │
├─────────────────────────────────────────────────────────┤
│ ISceneOutlinerMode (Public/ISceneOutlinerMode.h:16)     │
│   行为策略：CreateHierarchy()、选择同步、右键菜单、拖拽、 │
│   面板开关(ShowStatusBar 等)、交互回调                   │
├─────────────────────────────────────────────────────────┤
│ ISceneOutlinerHierarchy (Public/ISceneOutlinerHierarchy.h│
│   :11) 数据→节点：                                       │
│   CreateItems() 扁平产出全部节点                          │
│   FindOrCreateParentItem() 推导父子（框架递归组装成树）    │
│   OnHierarchyChanged 事件推增量(Added/Removed/Moved/…)   │
├─────────────────────────────────────────────────────────┤
│ ISceneOutlinerTreeItem (Public/ISceneOutlinerTreeItem.h) │
│   节点：GetID() 多态 variant、GetDisplayString()、Flags、 │
│   GenerateLabelWidget()、GenerateContextMenu()           │
└─────────────────────────────────────────────────────────┘
```

关键约束与事实：

1. **ID 全树唯一**：`SSceneOutliner::TreeItemMap` 以 `FSceneOutlinerTreeItemID` 为键，重复 Add 触发 `check(false)`（`SSceneOutliner.cpp:1081`）。ID 支持 `Object/Folder/UniqueID/Guid` 四种 variant（`SceneOutlinerStandaloneTypes.h:36`）。
2. **父子关系由框架组装**：`CreateItems()` 只扁平产出节点，`CreateChildren()` 可留空；框架对每个节点调 `FindOrCreateParentItem()` 挂父（DataLayer 面板即如此，`DataLayerHierarchy.h:31`）。
3. **增量事件**：Hierarchy 通过 `OnHierarchyChanged()` 广播 `FSceneOutlinerHierarchyChangedData{Added/Removed/Moved/FolderMoved/FullRefresh}`（`SceneOutlinerStandaloneTypes.h:228`），SSceneOutliner 转为 pending 队列分批应用。
4. **选择同步在 Mode 层**：`FActorMode::SynchronizeActorSelection`（`ActorMode.cpp:247`）与 `OnItemSelectionChanged` 负责双向同步，含 `BeginBatchSelectOperation` 批处理与重入防护。
5. **展开状态按 ID 缓存**：`CachedExpansionStateInfo` 跨刷新保留展开，前提是 ID 稳定。

---

## 3. 插件模块与文件结构

```
Plugins/HierarchicalWorldOutliner/
├── HierarchicalWorldOutliner.uplugin
├── Doc/                                  # PRD / 本文档 / TodoList
└── Source/HierarchicalWorldOutliner/
    ├── HierarchicalWorldOutliner.Build.cs
    ├── Public/
    │   ├── HierarchicalWorldOutlinerModule.h
    │   ├── HwoMode.h
    │   ├── HwoHierarchy.h
    │   ├── Dimensions/
    │   │   ├── IHwoDimension.h           # 维度接口
    │   │   ├── HwoDimensionTypes.h       # FHwoGroupKey / FHwoActorContext 等
    │   │   └── HwoDimensionStack.h       # 堆叠容器
    │   └── TreeItems/
    │       ├── HwoGroupTreeItem.h        # 虚拟分组节点
    │       ├── HwoActorTreeItem.h        # 已加载 Actor 叶子
    │       └── HwoActorDescTreeItem.h    # WP 未加载 Actor 叶子
    └── Private/
        ├── HierarchicalWorldOutlinerModule.cpp   # 模块 + LevelEditor Tab 注册
        ├── HwoMode.cpp
        ├── HwoHierarchy.cpp
        ├── Dimensions/
        │   ├── HwoDimensionStack.cpp
        │   ├── HwoDataLayerDimension.cpp
        │   ├── HwoClassDimension.cpp
        │   ├── HwoFolderDimension.cpp
        │   └── HwoTagDimension.cpp
        ├── TreeItems/ (*.cpp)
        └── Widgets/
            ├── SHwoPanel.h/.cpp          # 面板容器（Chip栏+过滤行+树+状态栏）
            ├── SHwoOutliner.h/.cpp       # : SSceneOutliner
            ├── SHwoDimensionBar.h/.cpp   # 维度 Chip 栏
            └── SHwoDimensionChip.h/.cpp  # 单个 Chip（可拖拽排序）
```

### 3.1 `HierarchicalWorldOutliner.uplugin`

```json
{
  "FileVersion": 3,
  "Version": 1,
  "VersionName": "0.1.0",
  "FriendlyName": "Hierarchical World Outliner",
  "Description": "Multi-dimensional pivot view over the World Outliner (DataLayer / Class / Folder / Tag ...)",
  "Category": "Editor",
  "Modules": [
    {
      "Name": "HierarchicalWorldOutliner",
      "Type": "Editor",
      "LoadingPhase": "Default"
    }
  ]
}
```

并将插件加入 `OutlinerDev.uproject` 的 `Plugins` 列表（`"Enabled": true`），之后重新生成项目文件。

### 3.2 `HierarchicalWorldOutliner.Build.cs` 依赖

```csharp
PublicDependencyModuleNames:  Core, CoreUObject, Engine, Slate, SlateCore, SceneOutliner
PrivateDependencyModuleNames: UnrealEd, LevelEditor, ToolMenus, DataLayerEditor, ApplicationCore, Projects
```

- `SceneOutliner`：框架本体（`SSceneOutliner`/`FActorMode`/`FActorTreeItem`/`FActorDescTreeItem` 均 `SCENEOUTLINER_API` 导出）；
- `UnrealEd`：`UEditorActorSubsystem`、`GEditor` 选择 API、`FScopedTransaction`；
- `LevelEditor`：`FLevelEditorModule::OnRegisterTabs` 注册并排 Tab（先例 `WorldPartitionEditorModule.cpp:1128`）；`OnRegisterLayoutExtensions` + `FLayoutExtender` 把 Tab 插入默认布局（先例 `ColorGradingEditorModule.cpp:86`）；
- `DataLayerEditor`：`UDataLayerEditorSubsystem::OnActorDataLayersChanged` 事件（v0.2 还需其写 API）；
- WP ActorDesc 全套类型（`FWorldPartitionActorDescInstance`、`FWorldPartitionHandle`）在 `Engine` 模块内，无需额外依赖。

### 3.3 命名规范

- 类前缀：`FHwo*`（普通类/结构）、`SHwo*`（Slate 控件）、`IHwo*`（接口）。插件名长，类名一律用短前缀 `Hwo`；
- 日志：`DEFINE_LOG_CATEGORY_STATIC(LogHwo, ...)`；
- 版权头：`// Copyright TA.Jiahao.Chan, SCT-X8, XJTG, Alibaba-Inc. All Rights Reserved.`（所有者 2026-10 指定，覆盖项目根的 Epic 头约定）；
- 本地化：全部 UI 文本走 `HWO_TEXT` 宏（见 §3.4，2026-10-11 起替代 LOCTEXT）；**新代码禁止再写 `LOCTEXT`/`NSLOCTEXT`**。

### 3.4 本地化方案（2026-10-11 实施）

UI 文案统一经 `Public/HwoLocalization.h` 的 `HWO_TEXT(Key, English, Chinese)` 宏产出：

- 底层为 `FPolyglotTextData`（Category=Editor、Namespace="HierarchicalWorldOutliner"、NativeCulture=en），英文为源文化；
- 中文同文注册三个键：`zh` / `zh-Hans` / `zh-CN`——引擎本地化回退链是**精确字符串匹配**，三键缺一不可（大小写严格为 `zh-Hans`）；
- **必须 static 缓存**：`FPolyglotTextData::GetText` 每次新建都会 UpdateLiveTable 并使全局 FText 修订号递增，本插件存在每帧取值路径（状态栏 `GetStatusText`），不缓存会导致全编辑器 FText 每帧过期；宏内以 lambda 静态局部变量保证每个调用点只注册一次；
- Key 命名规范 `分类.语义`（如 `Tab.Label`、`Menu.SelectGroupActors`、`Dim.Class.Name`、`Dim.Folder.Unassigned`、`Status.AllActors`、`ChipBar.AddDimensionTooltip`），全插件唯一；同一文案多处使用应收敛为单一访问点（如模块内 `HwoModuleText::GetTabLabel()`），跨文件不复用同一 Key（七轮审查 J1：共享 Key 已拆分为各维度独立 Key，防单点改文案互相覆盖）；
- Format 场景：`FText::Format(HWO_TEXT(...), ...)`，pattern 走宏、占位符英中一致；
- `FText::FromString` 的运行时数据透传点（分组名回落、包路径短名等）**不**走本地化。

---

## 4. 核心类设计

### 4.1 模块类 `FHierarchicalWorldOutlinerModule`

职责：启动时向 LevelEditor 注册并排 Tab、扩展默认布局（Tab 插在内置 World Outliner 之后并默认打开）；菜单入口。

```cpp
class FHierarchicalWorldOutlinerModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;    // 挂 FLevelEditorModule::OnRegisterTabs / OnRegisterLayoutExtensions
    virtual void ShutdownModule() override;   // 反注册 TabSpawner
private:
    TSharedRef<SDockTab> SpawnHwoTab(const FSpawnTabArgs& Args);
    static const FName HwoTabId;              // "HierarchicalWorldOutliner"
};
```

要点（先例 `WorldPartitionEditorModule.cpp:1128` 与 `SLevelEditor.cpp:1592`）：

- 用 `FLevelEditorModule::OnRegisterTabs()` 委托向 **LevelEditorTabManager**（非 GlobalTabmanager）注册，使面板与 World Outliner 同体系、可停靠在同区域、随布局持久化；
- 用 `FLevelEditorModule::OnRegisterLayoutExtensions()` + `FLayoutExtender::ExtendLayout(LevelEditorTabIds::LevelEditorSceneOutliner, After, OpenedTab)` 把 Tab 插入默认布局：首次启动与 Reset/Load Layout 重建时自动出现在内置 World Outliner 之后并默认打开；`ProcessExtensions` 内有防重复检查——用户布局已含该 Tab（含 ClosedTab 状态）时不重复插入，即用户手动关闭后不会被强行重开（先例 `ColorGradingEditorModule.cpp:86`、`SLevelEditorToolBox.cpp:50`）；
- 通过 `UToolMenus::RegisterStartupCallback` 在「窗口」菜单追加入口；
- Tab 内容 = `SNew(SHwoPanel)`；
- `ShutdownModule` 必须对称反注册（插件可被热卸载）。

### 4.2 面板容器 `SHwoPanel : SCompoundWidget`

组合四个区域（垂直 SVerticalBox）：

```
┌───────────────────────────────────────────────┐
│ SHwoDimensionBar   维度 Chip 栏（自绘）          │
│ SSearchBox 结构    结构化过滤行（自绘，见 §8.2） │
│ SHwoOutliner       树（: SSceneOutliner）       │ ← 本体
│ 状态栏由 Mode::ShowStatusBar/GetStatusText 提供 │
└───────────────────────────────────────────────┘
```

```cpp
class SHwoPanel : public SCompoundWidget
{
    SLATE_BEGIN_ARGS(SHwoPanel) {}
    SLATE_END_ARGS()
    void Construct(const FArguments& InArgs);

private:
    TSharedPtr<SHwoDimensionBar> DimensionBar;
    TSharedPtr<SHwoOutliner> Outliner;
    TSharedRef<FHwoDimensionStack> DimensionStack;   // 面板持有，Mode/Hierarchy 共享（TWeakPtr 传入）

    void OnDimensionStackChanged();                  // → Outliner->FullRefresh()
};
```

组装 `SSceneOutliner` 的初始化参数（对照 `FSceneOutlinerInitializationOptions`，`SceneOutlinerPublicTypes.h:174`）：

```cpp
FSceneOutlinerInitializationOptions InitOptions;
InitOptions.ModeFactory = FCreateSceneOutlinerMode::CreateLambda(
    [Stack = DimensionStack](SSceneOutliner* Outliner) -> ISceneOutlinerMode*
    {
        FActorModeParams Params(Outliner);
        Params.bHideUnloadedActors = false;          // D4：MVP 含 WP 未加载 Actor
        return new FHwoMode(Params, Stack);
    });
InitOptions.OutlinerIdentifier = "HierarchicalWorldOutliner";  // 必填：列布局等配置持久化（UOutlinerConfig）
InitOptions.bShowCreateNewFolder = true;             // 2026-10-11 起支持：新建包含当前选中项的文件夹（FHwoMode::CreateNewFolder）
// 过滤栏（2026-10-12 启用，Feature-18）：与内置 World Outliner 同一套过滤器（SLevelEditor.cpp:807-816），
// 自定义文本过滤与 LevelEditor 各 Outliner 共享
InitOptions.FilterBarOptions.bHasFilterBar = true;
InitOptions.FilterBarOptions.bUseSharedSettings = true;
FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
const TSharedPtr<FLevelEditorOutlinerSettings> OutlinerSettings = LevelEditorModule.GetLevelEditorOutlinerSettings();
OutlinerSettings->GetOutlinerFilters(InitOptions.FilterBarOptions);
InitOptions.FilterBarOptions.CategoryToExpand = OutlinerSettings->GetFilterCategory(FLevelEditorOutlinerBuiltInCategories::Common());
// 列组与内置 World Outliner 同款：CreateActorBrowserColumns 产出 Label/Gutter(可视性)/ActorInfo(类型)/SourceControl/Unsaved，
// 并经 OnCreateActorBrowserColumns 广播获得 WorldPartitionEditor 追加的 Pinned(强制加载) 列（SceneOutlinerModule.cpp:216-245）
FModuleManager::LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner").CreateActorBrowserColumns(InitOptions);
// v0.2 再按需加自定义列
```

注意：`SSceneOutliner::Construct` 内 `check(ModeFactory.IsBound())`（`SSceneOutliner.cpp:76`），ModeFactory 必填。

### 4.3 `SHwoOutliner : SSceneOutliner`

薄子类（先例 `SDataLayerOutliner.h:12`）。MVP 阶段只为两点预留：

1. 覆写/补充工具栏区域（如需在框架工具栏旁追加维度相关按钮）；
2. 暴露受保护成员给 `SHwoPanel`（如需要）。

若最终无覆写需求，可退化为直接使用 `SSceneOutliner`，删除该类——保持最小实现。

### 4.4 行为层 `FHwoMode : FActorMode`

基类选择理由：`FActorMode`（`ActorMode.h:65`）提供 Actor 世界事件监听、选择同步骨架、`CanPopulate`（PIE 暂停）等地基，且不含 `FActorBrowsingMode` 的文件夹专属包袱（文件夹在我们这里只是十个维度之一）。

```cpp
class FHwoMode : public FActorMode
{
public:
    FHwoMode(const FActorModeParams& Params, TSharedRef<FHwoDimensionStack> InStack);

    // —— 数据入口：挂上我们的维度堆叠 Hierarchy ——
    virtual TUniquePtr<ISceneOutlinerHierarchy> CreateHierarchy() override;

    // —— 选择同步（D2）——
    virtual void SynchronizeSelection() override;                 // 视口→大纲：全实例选中+定位首个
    virtual void OnItemSelectionChanged(FSceneOutlinerTreeItemPtr Item,
        ESelectInfo::Type SelectionType,
        const FSceneOutlinerItemSelection& Selection) override;   // 大纲→视口：按 Actor 去重批量选择

    // —— 面板行为开关 ——
    virtual ESelectionMode::Type GetSelectionMode() const override { return ESelectionMode::Multi; }
    virtual bool ShowStatusBar() const override { return true; }
    virtual bool ShowViewButton() const override { return true; }  // 2026-10-11 起开启齿轮菜单（Expand/Collapse All、Stack Hierarchy Headers + FilterInfo 开关 + Always Frame Selection）
    virtual FText GetStatusText() const override;                  // "命中 X / 总节点 Y（唯一 Actor Z）"
    virtual int32 GetTypeSortPriority(const ISceneOutlinerTreeItem& Item) const override;
                                                                    // 分组节点(20) 排在 Actor(30) 前
    // —— FilterBar 类型过滤（Feature-18）：FActorMode 未覆写，基类默认 false 会把全部条目滤掉，复刻 FActorBrowsingMode 实现 ——
    virtual bool CompareItemWithClassName(SceneOutliner::FilterBarType InItem,
        const TSet<FTopLevelAssetPath>& AssetClassPaths) const override;

    // —— 右键菜单：分组 vs Actor 分流 ——
    virtual TSharedPtr<SWidget> CreateContextMenu() override;

    // —— MVP 禁用拖拽（v0.2 实现赋值语义，见 Design-4）——
    virtual TSharedPtr<FDragDropOperation> CreateDragDropOperation(
        const FPointerEvent&, const TArray<FSceneOutlinerTreeItemPtr>&) const override { return nullptr; }

    // —— 维度数据变更通知（Hierarchy 转发）——
    void RequestFullRefresh();

private:
    TSharedRef<FHwoDimensionStack> DimensionStack;
    TMultiMap<FGuid, FSceneOutlinerTreeItemID> ActorGuidToLeafIds; // 选择同步索引（Design-1/2）
    // —— 事件句柄（析构时全部解绑）——
    TArray<FDelegateHandle> EventHandles;
};
```

**右键菜单**：按选中内容分流（2026-10-11 更新，五轮审查 W1 修正）——选中含【已加载】Actor 叶子（`FHwoActorTreeItem`）时直接返回 `FLevelEditorContextMenu::BuildMenuWidget(LevelEditorInstance, ELevelEditorMenuContext::SceneOutliner)`（`LevelEditorContextMenu.h:51`，落到 `LevelEditor.ActorContextMenu`，与内置大纲同源；其内部从 GEditor 的 ElementSelectionSet 取选择，D2 选择同步保证树选择=视口选择；已知缺口：Force Load 项依赖未覆写的 PinItems/CanPinItems，灰显）。未加载叶子（`FHwoActorDescTreeItem`）与纯分组节点走 UToolMenus 注册的 `HwoOutliner.DefaultContextMenu`（基菜单 + 动态 section，先例 `ActorBrowsingMode.cpp:696`），单选委托 item 自身 `GenerateContextMenu`（未加载叶子由此带出基类 `FActorDescTreeItem` 自带的 Focus Actor Bounds/Copy Actor File Path）。语义注明：**混合选择（分组+Actor）时走 LevelEditor 菜单，分组操作不可达**；分组菜单 MVP 只放只读操作（选中组内全部 Actor）；写操作随 v0.2 批量功能进。

**选择同步细节见 §6。**

### 4.5 维度接口 `IHwoDimension` 与堆叠容器

维度 = "对 Actor 的一种分组依据"（PRD §3.1）。接口面向**统一 Actor 视图**取数，对维度实现者屏蔽"已加载/未加载"差异：

```cpp
// HwoDimensionTypes.h

/** 统一 Actor 视图：已加载包 AActor*，WP 未加载包 ActorDescInstance，二者必居其一 */
struct FHwoActorContext
{
    AActor* Actor = nullptr;                                     // 已加载时非空
    const FWorldPartitionActorDescInstance* DescInstance = nullptr; // 未加载时非空

    bool IsLoaded() const { return Actor != nullptr; }
    FGuid GetActorGuid() const;
    FString GetLabel() const;       // Actor->GetActorLabel() / Desc->GetActorLabel()
    UClass* GetNativeClass() const; // Actor->GetClass() / Desc->GetActorNativeClass()
    FName GetFolderPath() const;    // Actor.h:2758 / WorldPartitionActorDesc.h:333
    // DataLayer/Tag 等访问由对应 Dimension 自行用 Actor / DescInstance 取，
    // Context 只收敛"每个维度都要用"的通用字段，避免无限膨胀。
};

/** 一个分组的定位键：维度 ID + 维度内键值（如 DataLayer 名 / 类路径 / 文件夹路径 / Tag 名） */
struct FHwoGroupKey
{
    FName DimensionID;
    FString Key;
    bool operator==(const FHwoGroupKey&) const = default;
};

/** 维度接口（纯 C++，不进反射） */
class IHwoDimension
{
public:
    virtual ~IHwoDimension() = default;

    virtual FName GetDimensionID() const = 0;        // "DataLayer" / "Class" / "Folder" / "Tag"
    virtual FText GetDisplayName() const = 0;        // Chip 与分组节点显示名
    virtual FSlateIcon GetIcon() const = 0;          // 分组节点图标（区分于 Actor 图标）

    /**
     * 计算一个 Actor 在本维度下的全部分组键。
     * 一个 Actor 可属多个分组（多 DataLayer、多 Tag）→ 返回多个键，叶子按路径笛卡尔积复制；
     * 无归属 → 返回空，由 Stack 归入"未分配"特殊分组。
     */
    virtual void GetGroupKeys(const FHwoActorContext& Ctx, TArray<FHwoGroupKey>& OutKeys) const = 0;

    /** 把分组键展开成路径段序列（2026-10-12 新增）。默认一键一段；
     *  文件夹维度覆写为按 "/" 拆多段（"A/B" → ["A","B"]），树上呈现真实嵌套。
     *  段键只存段名，分组 ID 由全路径编码，无碰撞。 */
    virtual void ExpandKeyToPathSegments(const FHwoGroupKey& Key, TArray<FHwoGroupKey>& OutSegments) const { OutSegments.Add(Key); }

    virtual FText GetGroupDisplayName(const FHwoGroupKey& Key) const = 0;

    /** 该维度关心的数据变更事件（Hierarchy 统一订阅后转发），返回 false 表示无需订阅 */
    virtual bool WantsDataLayerEvents() const { return false; }  // DataLayer 维度返回 true，以此类推
};
```

```cpp
// HwoDimensionStack.h

/** 维度堆叠（1~5 层，可空=回退文件夹维度 D3）。面板持有，Mode/Hierarchy 共享只读 */
class FHwoDimensionStack : public TSharedFromThis<FHwoDimensionStack>
{
public:
    static constexpr int32 MaxStackDepth = 5;   // 2026-10-11 由 3 调整为 5；所有引用处走同一常量
    /** 单 Actor 路径总数防爆护栏（五轮审查 W2）：多值维度组合膨胀时截断，超限仅一次性 UE_LOG 告警 */
    static constexpr int32 MaxPathsPerActor = 64;

    void SetDimensions(const TArray<TSharedRef<IHwoDimension>>& InDims);
    const TArray<TSharedRef<IHwoDimension>>& GetDimensions() const { return Dimensions; }
    bool IsEmpty() const { return Dimensions.IsEmpty(); }          // 空态 → 等效 [Folder]

    /** 计算 Actor 的完整叶子路径列表。
     *  每个维度产出 0..N 键（键集合先经 TSet 去重），跨维度做笛卡尔积；任一维度空 → 该层补"未分配"键；
     *  单 Actor 累积路径数达 MaxPathsPerActor 即截断（保留已生成路径，停止展开后续维度）。
     *  例 [DataLayer, Class]：Actor 属 DL_A/DL_B × PointLight → 2 条路径。 */
    void ComputeLeafPaths(const FHwoActorContext& Ctx, TArray<TArray<FHwoGroupKey>>& OutPaths) const;

    DECLARE_MULTICAST_DELEGATE(FOnStackChanged);
    FOnStackChanged OnStackChanged;   // SHwoPanel 订阅 → FullRefresh

    static TSharedRef<IHwoDimension> GetFallbackFolderDimension();  // D3 空态回退
};
```

**内置维度的取数来源**（已加载 / WP 未加载双路；当前共九个：DataLayer / Class / Folder / Tag / HLOD / Unsaved / LocationVolume / RuntimeGrid / SourceControl）：

| 维度 | 已加载（AActor） | 未加载（FWorldPartitionActorDesc(Instance)） |
|---|---|---|
| DataLayer | `GetDataLayerInstancesForLevel()`（Actor.h:1517 区段） | `GetDataLayerInstanceNames()`（WorldPartitionActorDescInstance.h:104） |
| Class | `GetParentNativeClass(GetClass())`（与 ActorDesc 的 `NativeClass` 初始化严格同口径，见 `WorldPartitionActorDesc.cpp:105`；BP Actor 归父原生类分组，基类深度配置留 v0.2） | `GetActorNativeClass()` / `GetBaseClass()`（:310-312） |
| Folder | `GetFolderPath()`（Actor.h:2758）；完整路径键经 `ExpandKeyToPathSegments` 按 "/" 拆多段嵌套（2026-10-12 起，此前单段平铺） | `GetFolderPath()`（:333），同左 |
| Tag | `Tags`（Actor.h:1344） | `GetTags()`（:328） |
| HLOD 层 | `IsHLODRelevant()` → `GetHLODLayer()`（Actor.h:2568/1038），空则 WP 默认层 `UWorldPartition::GetDefaultHLODLayer()`（WorldPartition.h:343）；键 = 层资产长包名 | `GetActorIsHLODRelevant()` → `GetHLODLayer()`（FSoftObjectPath，:321-322），默认层经 `GetContainerInstance()->GetOuterWorldPartition()`；键口径与左侧一致 |
| Unsaved（是否保存，2026-10-12） | `GetSceneOutlinerItemPackage()`（与原生未保存列同源）→ `USourceControlHelpers::PackageFilename` → `FUnsavedAssetsTrackerModule::IsAssetUnsaved`；键 = 常量串 Saved/Unsaved 二分；排序约定："未保存"组优先级 Folder-1 排在"已保存"前（FHwoMode::GetTypeSortPriority） | `GetActorPackage()`，同左判定链；包名取不到 → 未分配 |
| LocationVolume（2026-10-12） | `GetStreamingBounds` 编辑器包围盒 ↔ 世界内全部 `ALocationVolume` 包围盒 AABB 相交（保守，对齐 WP 按 Volume 包围盒加载语义，不做 brush 精确判定）；Volume 枚举按（世界， 帧号）缓存；键 = Volume 的 Actor Label（同名合并） | `GetEditorBounds()`；世界退回 `GEditor->GetEditorWorldContext().World()`（已知取舍，见 TodoList Bug-P11） |
| RuntimeGrid（空间网格，2026-10-12，Feature-24 方案 C） | `IsSpatiallyLoaded()` 分流：`false` → "非空间加载"特殊组；`GetRuntimeGrid()`（`NAME_None` 回落 `UWorldPartitionRuntimeSpatialHash::GetDefaultGrid()`）→ 匹配 `Grids` 配置取 Origin/CellSize → `GetActorBounds(false,true)` 中心点 floor 除法得 cell；键 = `"Grid|X|Y"`；不按盒尺寸分层级（只用最细层）；刷新 = `UEngine::OnActorMoved` + FTSTicker 0.2s 去抖 | `GetIsSpatiallyLoaded()`（:50，已折叠 ForceNonSpatiallyLoaded 与 RuntimeBounds 有效性）→ `GetRuntimeGrid()`（同序列化字段）→ `GetEditorBounds()` 中心点同式；双态口径微小差异见 TodoList Bug-P13；"非空间加载"组排序 Folder+1 档与未分配组同档沉底 |
| SourceControl（版本控制，2026-10-12，Feature-25） | `GetSceneOutlinerItemPackage()`（与 Unsaved 维度同链）→ `PackageFilename` → `ISourceControlProvider::GetState(Use)` **纯缓存读**；未命中顺手 `QueueStatusUpdate` 懒回填（模块节流去重）本次归"未知"；十级归组优先级 Unknown→NotControlled(含 Ignored)→Conflicted(先于 Modified)→Deleted→Added→CheckedOutOther→CheckedOut→Modified→OutOfDate(仅在线区分)→Current；刷新 = state-changed 无参广播 → `ReevaluateKeysAndDetectChanges` 键级变化检测 → 有变才去抖 0.2s FullRefresh；Provider 切换句柄随迁 + 清缓存 | `GetActorPackage()`，同左判定链；包名取不到 → 未分配；Provider 未启用降级全体落"未知"组 |

### 4.6 数据层 `FHwoHierarchy : ISceneOutlinerHierarchy`

```cpp
class FHwoHierarchy : public ISceneOutlinerHierarchy
{
public:
    FHwoHierarchy(ISceneOutlinerMode* InMode, TWeakObjectPtr<UWorld> InWorld,
                  TSharedRef<FHwoDimensionStack> InStack);

    // —— ISceneOutlinerHierarchy ——
    virtual void CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const override;
    virtual void CreateChildren(const FSceneOutlinerTreeItemPtr&, TArray<FSceneOutlinerTreeItemPtr>&) const override {}
    virtual FSceneOutlinerTreeItemPtr FindOrCreateParentItem(const ISceneOutlinerTreeItem& Item,
        const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items, bool bCreate) override;

private:
    // 事件订阅清单（抄 DataLayerHierarchy.cpp:53-84 + FActorHierarchy 的监听面）：
    void BindEvents();
    void OnLevelActorAdded(AActor*);                 // → 增量 Added（新叶子）
    void OnLevelActorDeleted(AActor*);               // → 增量 Removed（该 Actor 全部叶子 ID）
    void OnActorDescInstanceAdded(FWorldPartitionActorDescInstance*);
    void OnActorDescInstanceRemoved(FWorldPartitionActorDescInstance*);
    void OnActorDataLayersChanged(...);              // UDataLayerEditorSubsystem 事件
    void OnActorFolderChanged(...);                  // → MVP：FullRefreshEvent()（见 §5.3 取舍）
    void OnMapChange(...);                           // FEditorDelegates::MapChange → FullRefresh
    void OnPostUndoRedo(...);                        // FEditorDelegates::PostUndoRedo → FullRefresh

    FSceneOutlinerTreeItemPtr CreateLeafItems(const FHwoActorContext& Ctx,
        TArray<FSceneOutlinerTreeItemPtr>& OutItems) const; // 按 ComputeLeafPaths 每路径造一片叶子

    TWeakObjectPtr<UWorld> RepresentingWorld;
    TSharedRef<FHwoDimensionStack> Stack;
    TArray<FDelegateHandle> EventHandles;
};
```

**`CreateItems` 算法**（全量构建，由框架分批入树，见 §5）：

```
0. 世界根节点（2026-10-12，Feature-21）：Mode->CreateItemFor<FWorldTreeItem>(World)，
   复用引擎全导出的 FWorldTreeItem（标签 "Lvl_Xxx（编辑器）"/图标/类型列"世界场景"/
   右键菜单全部原生）；树从多根变单根，所有顶层节点挂在它下面
1. 已加载：TActorIterator<AActor>(World, SkipPendingKill)，
   用 FActorMode::IsActorDisplayable 静态过滤（ActorMode.h:86）
2. 未加载：若 World->GetWorldPartition() 有效且 Mode 未隐藏未加载：
   FWorldPartitionHelpers::ForEachActorDescInstance(WorldPartition, AActor::StaticClass(), ...)
   先用 FActorDescTreeItem::ShouldDisplayInOutliner 过滤（ActorDescTreeItem.h:43）
3. 对每个 Actor 构造 FHwoActorContext → Stack->ComputeLeafPaths
4. 每条路径造一片叶子：已加载 → FHwoActorTreeItem；未加载 → FHwoActorDescTreeItem
   （分组节点不在这里造，由 FindOrCreateParentItem 按需造）
5. 空文件夹分组（2026-10-12，Bug-P7）：仅当文件夹维度为最外层有效维度时，
   枚举 FActorFolders::ForEachFolderWithRootObject 中已注册文件夹（含零成员文件夹），
   直接造 FHwoGroupTreeItem（完整路径经 ExpandKeyToPathSegments 拆段，空文件夹 "A/B" 补出
   A、A/B 两级嵌套，与 Actor 派生分组同构）。非最外层时无外层维度键可填，不枚举。
   事件：FActorFolders::OnFolderCreated/Deleted/Moved（EditorActorFolders.h:62-64）→ FullRefresh
```

**`FindOrCreateParentItem` 算法**（维度堆叠的落点）：

```
取 item 的路径 Path：叶子 = 全路径（每一段都是分组键）；分组 = 自身路径
  叶子：ParentPath = Path（全路径，父 = 最深一层分组）
  分组：ParentPath = Path[0..N-2]（去掉自身末段）
  ParentPath 为空       → 挂到世界根节点（ID = FObjectKey(World)，未找到且 bCreate 时按需创建）
  ParentID   = GroupItemID(ParentPath)                 // 见 §4.7
  在 Items 中找 ParentID：
    找到   → 返回之
    未找到且 bCreate → 创建 FHwoGroupTreeItem(ParentPath)（框架会再对它递归调用本函数挂上层）
```

> 注意：叶子与分组的父路径计算**不同**（叶子保留全路径、分组去末段）。初版实现对所有节点都去末段，导致单层维度不分组（Bug-1，2026-10-11 用户实测发现并修复）。

`CreateChildren` 留空——框架从扁平列表自行组装树（DataLayer 面板同款做法）。

### 4.7 节点层 TreeItem

**ID 方案（Design-1 的落地，核心约束：TreeItemMap 全树唯一）**：

```cpp
namespace HwoTreeItemId
{
    // 路径 → 稳定字符串："DimA=KeyA|DimB=KeyB"（键内含分隔符需转义）
    FString SerializePath(const TArray<FHwoGroupKey>& Path);

    // 分组节点 ID：Guid 变体，MD5(路径串) —— 稳定 → 展开状态跨刷新保留
    FSceneOutlinerTreeItemID ForGroup(const TArray<FHwoGroupKey>& Path);

    // 叶子 ID：MD5(路径串 + "#" + ActorGuid) —— 同一 Actor 每条路径一片叶子，互不冲突
    // （先例：FDataLayerActorTreeItem 用 FArchiveMD5 哈希 (ActorKey, DataLayerKey)，DataLayerActorTreeItem.h:51）
    FSceneOutlinerTreeItemID ForLeaf(const TArray<FHwoGroupKey>& Path, const FGuid& ActorGuid);
}
```

```cpp
/** 虚拟分组节点：不对应任何 UObject，纯路径实体 */
struct FHwoGroupTreeItem : ISceneOutlinerTreeItem
{
    static const FSceneOutlinerTreeItemType Type;   // 自有类型，CastTo<> 分流用

    TArray<FHwoGroupKey> Path;                      // 全路径（最后一段 = 本组）
    FString CachedDisplayString;                    // "Lighting (1,204)" —— 徽标方案 A（Design-5）

    virtual FSceneOutlinerTreeItemID GetID() const override { return HwoTreeItemId::ForGroup(Path); }
    virtual FString GetDisplayString() const override;      // 组名 + (子代 Actor 去重计数)
    virtual bool IsValid() const override { return true; }  // 由 Hierarchy 生命周期管理
    virtual bool CanInteract() const override { return true; }
    virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner&, const STableRow<...>&) override;
                                                            // 维度图标 + 名称 + 徽标
    virtual void GenerateContextMenu(UToolMenu*, SSceneOutliner&) override;  // 分组操作 section
    virtual bool HasVisibilityInfo() const override { return false; }        // 可视性由框架按子代 OR 聚合；文件夹分组经 ShouldShowVisibilityState 显示眼睛（2026-10-12）
    virtual FString GetPackageName() const override;                         // 文件夹分组：UActorFolder 外部包（未保存列）；其余维度分组为空
};

/** 已加载 Actor 叶子 */
struct FHwoActorTreeItem : FActorTreeItem
{
    static const FSceneOutlinerTreeItemType Type;   // Parent = FActorTreeItem::Type
    TArray<FHwoGroupKey> Path;
    FGuid ActorGuid;                                // 缓存，Actor 死亡后仍可索引/移除

    virtual FSceneOutlinerTreeItemID GetID() const override;  // ForLeaf(Path, ActorGuid)，
                                                              // 覆盖基类的 FObjectKey ID
};

/** WP 未加载 Actor 叶子 */
struct FHwoActorDescTreeItem : FActorDescTreeItem
{
    static const FSceneOutlinerTreeItemType Type;
    TArray<FHwoGroupKey> Path;

    virtual FSceneOutlinerTreeItemID GetID() const override;  // ForLeaf(Path, GetGuid())
};
```

注意点：

- 基类 `FActorTreeItem` 的 `GetID()` 返回 `FObjectKey`（`ActorTreeItem.h:43`），必须覆写为组合 ID，否则同一 Actor 第二片叶子即崩（`SSceneOutliner.cpp:1081`）；
- 两个叶子类的 `Type` 以基类 Type 为 ParentType 声明（`FSceneOutlinerTreeItemType` 支持父子链 `IsA`，`SceneOutlinerStandaloneTypes.h:184`），这样引擎内置列（Label/Visibility/DataLayer 文本列）里针对 `FActorTreeItem`/`FActorDescTreeItem` 的分支对我们依然生效——**这是继承而非重写这两个类的核心理由**：眼睛列、类型列、DataLayer 列全部免费继续工作；
- `GetDisplayString()` 参与排序与搜索，分组徽标放这里（方案 A）零额外机制。

---

## 5. 数据流与刷新管线

### 5.1 全量构建序列

```
用户改维度栏 → Stack.OnStackChanged → SHwoPanel → Outliner->FullRefresh()
  → SSceneOutliner 下次 Tick → Mode->Rebuild() → Hierarchy->CreateItems()
  → 全部叶子入 PendingOperations → Populate() 每帧 ≤5ms 分批 AddItemToTree
  → 每片叶子 FindOrCreateParentItem 逐级 FindOrCreate 分组节点
  → 全部处理完 → 最终排序 → OnFullRefreshDone
```

### 5.2 增量事件分级（MVP 取舍）

| 事件 | MVP 策略 | 后续优化 |
|---|---|---|
| Actor 新增/删除 | 增量 Added/Removed（只造/只删该 Actor 的叶子） | — |
| ActorDescInstance 增/删（WP 流送） | 增量 Added/Removed | — |
| Actor 加载↔卸载互换 | 删 Desc 叶子 + 加 Actor 叶子（同路径 ID 不变） | — |
| DataLayer/Tag/Folder/Label 变更 | **FullRefreshEvent()**（保守） | 改为 Moved 增量（v0.2） |
| Undo/Redo、MapChange | FullRefreshEvent() | — |
| 维度堆叠变更 | FullRefresh（由 Panel 直接调） | — |

依据：属性类变更频率低，而维度路径改变意味着叶子 ID 整体变化，实现 Moved 增量的收益/复杂度比在 MVP 不划算；框架的 5ms/帧预算保证 FullRefresh 也不卡。

### 5.3 分组计数与徽标刷新

分组节点 `GetDisplayString()` 的 `(N)` 计数 = 子代**去重 Actor 数**（同一 Actor 多片叶子只计一次）。计数在 `AddItemToTree`/`RemoveItemFromTree` 的节点增删钩子里沿父链累加（Mode::OnItemAdded/OnItemRemoved，ISceneOutlinerMode.h:73-75），分组节点缓存计数，变化时 `OnLabelChanged()` 触发行重绘。Actor 加载/卸载互换时计数不变（同 ActorGuid）。

---

## 6. 选择同步设计（D2）

### 6.1 大纲 → 视口

`FHwoMode::OnItemSelectionChanged`（2026-10-12 起，分组不再默认联动选中 Actor，对齐原生 WO）：

```
收集当前选中树节点：
  叶子 → Actor（FHwoActorTreeItem / FHwoActorDescTreeItem）
  分组 / 世界节点 → 不传播（点文件夹只选中文件夹本身）；
  混合选择只同步 Actor 部分；纯分组/空选择直接返回，不动视口选择
按 FObjectKey 去重 → 原生 TypedElement 通道写入：SelectionSet->SetSelection(元素, AllowLegacyNotifications=false)
→ NotifyPendingChanges()（守卫内同步触发 NoteSelectionChange；legacy 批处理会因 pending 标记残留
在帧末回声广播导致 Bug-P9，禁用）
```

"选中组内全部 Actor"（递归，含嵌套子文件夹）作为能力入口保留在分组右键菜单（`FHwoGroupTreeItem::GenerateContextMenu` → CollectActors）。

先例：`FActorBrowsingMode::OnItemSelectionChanged`（ActorBrowsingMode.cpp:1289-1307）。未加载 Actor 走 `FWorldPartitionHandle` 的选择通道（同步 `ActorBrowsingMode.cpp:1494` 的 `SynchronizeSelectedActorDescs` 做法）。

### 6.2 视口 → 大纲（D2 核心）

`FActorModeInteractive` 已订阅 `USelection::SelectionChangedEvent` 并置脏（`ActorModeInteractive.cpp:13`），我们在 `FHwoMode::SynchronizeSelection()` 覆写中：

```
对每个 GEditor 选中 Actor 的 Guid：
  查 ActorGuidToLeafIds（TMultiMap<FGuid, FSceneOutlinerTreeItemID>，由 OnItemAdded/Removed 维护）
  → 全部 ID 置选中（SetItemSelection(..., ESelectInfo::Direct) 防递归回环）
  → 第一个 ID RequestScrollIntoView + 沿父链 SetItemExpansion 展开
```

`bIsReentrant` 防护照抄基类模式（SSceneOutliner.cpp:2002 区段）。

---

## 7. 过滤设计（v0.2，已移出 MVP）

> 范围说明：结构化过滤行原定为 MVP 项，实现排期时推迟至 v0.2（TodoList Feature-7）。MVP 仅保留框架内置搜索框的纯文本快搜。以下设计维持不变，直接适用于 v0.2 实现。

**机制选择**：不碰 `SSceneOutliner` 内置搜索框（它走 `TTextFilter` + 各列 `PopulateSearchStrings`）。在 `SHwoPanel` 自绘一行结构化过滤框，命中后写入 `InitOptions.Filters`（`FSceneOutlinerFilters`，`SceneOutlinerFilters.h:182`）：

```cpp
// 解析 "class:PointLight AND tag:Interactable AND (datalayer:Lighting OR folder:Props)"
// → 递归下降解析为谓词树 → 编译成单个 Lambda 注册：
InitOptions.Filters->AddFilterPredicate<ISceneOutlinerTreeItem>(
    [](const ISceneOutlinerTreeItem& Item) { return CompiledPredicate(Item); });
```

- 谓词对 `FHwoActorTreeItem`/`FHwoActorDescTreeItem`/`FHwoGroupTreeItem` 三分支：叶子按字段求值；**分组节点恒通过**（否则空组消失导致子叶连带消失——框架对被过滤节点的子节点处理依赖父节点通过，`bIsFilteredOut` 语义见 `ISceneOutlinerTreeItem.h:22-38`，正确语义是"组内无命中 → 组自然为空不显示"，由 `ShouldRemoveOnceLastChildRemoved` 兜底）；
- 字段谓词复用维度的取数双路（Actor / ActorDescInstance），`class:` 支持子类匹配（`IsChildOf`）；
- 语法编译结果缓存，输入防抖（~200ms）后 `FullRefresh`；
- 内置搜索框保留做纯文本快搜，两者 AND 叠加（框架天然支持多过滤器）。

---

## 8. UI 设计落地

### 8.1 维度 Chip 栏 `SHwoDimensionBar`（核心差异化 UI，全自绘）

```
[📁 DataLayer ▾] [💡 类 ▾] [+]      预设: [默认 ▾]   ← 空态时显示 [📁 文件夹]
```

- Chip 列表：`SHorizontalBox` + 每维度一个 `SHwoDimensionChip`（SBorder 圆角 + 图标 + 文本 + ×移除钮）；
- **Slate 禁区（2026-10-12，Bug-P12）**：`OnStackChanged` 可能在 Slate 事件路由中广播（× 点击 / 拖拽放下），结构性销毁控件（`RebuildChips` 的 ClearChildren、面板的 FullRefresh 重建树）必须延迟一帧——`SHwoDimensionBar::HandleStackChanged` 与 `SHwoPanel::OnDimensionStackChanged` 用一次性 ActiveTimer + pending 合并标记实现；
- 拖拽排序：Chip 实现 `OnDragDetected/OnDrop`（自定义 `FHwoChipDragDropOp : FDragDropOperation`），放下即重排 Stack；
- `[+]`：`SComboButton` 弹出维度菜单（内置维度清单，已堆叠的置灰）；
- 空态（D3）：不显示空栏，而是显示一个不可移除的"文件夹"Chip——即 Stack 空时 `ComputeLeafPaths` 内部回退到 Folder 维度（D3 的语义落在 Stack 层而非 UI 层，UI 只是如实反映）；
- Chip 右键/下拉：该维度的参数（如 Class 维度的"基类深度"），v0.2 预留；
- 预设下拉：v0.2（PRD F-202/F-3.4），MVP 只留灰显占位按钮。

### 8.2 状态栏

用框架自带状态栏通道（`ShowStatusBar/GetStatusText`）。2026-10-12 起复刻原生四分支文本（`ActorBrowsingMode.cpp:344-401`）：

```
无过滤无选中：  "{0}个Actor"（WP 世界为 "{0}个Actor（{1}个已加载）"）
无过滤有选中：  "{0}个Actor（已选{1}个）"
过滤零命中：    "没有匹配的Actor（总共{0}个）"（红 AccentRed）
过滤有命中：    "显示{0}/{1}个Actor（已选{2}个）" / 无选中变体（绿 AccentGreen）
```

计数口径：Actor 总数 = 树内唯一 Actor 数（`UniqueActorCount`，多路径复制不膨胀）；过滤前总数 = `ApplicableActorGuids`（OnItemPassesFilters 累计的唯一 Guid）；选中数 = `GetSelection().Num<FActorTreeItem, FActorDescTreeItem>()`（注意 D2 语义下是叶子数，可能大于唯一选中 Actor 数）。WP 世界无过滤无选中分支追加"已加载"段（2026-10-12 实现）：loaded 数 = `LoadedLeafCountByGuid.Num()`（OnItemAdded/Removed 按 Guid 增量维护，已加载叶子进、未加载叶子不进，计数归零即移除键）；原生"有选中"分支不带 loaded 段（已核实，对齐）。
颜色（`GetStatusTextColor`）：前置判定——任一选中 Actor 在树中有多片叶子时整行紫色（`HwoStyle::AccentPurple`，非常规提醒"该 Actor 被多维复制"）；否则原生三色（默认前景 / 零命中红 / 有命中绿）。状态栏控件为 TAttribute 每帧轮询，无需事件接线。
本地化：状态栏全部格式串的中文逐字对齐引擎官方简体（`Engine/Content/Localization/Editor/zh-Hans/Editor.locres`，如 "{0} actors ({1} selected)" → "{0}个Actor（已选{1}个）"）。

### 8.3 树内视觉规则

- 分组节点：分组图标 + **加粗** + `名称 (N)` 徽标；文件夹维度分组还原原生文件夹节点的可视性眼睛（子代 OR 聚合 + 点击递归传播）与未保存点（UActorFolder 外部包），其余维度分组不显示（2026-10-12）。类型列：引擎 ActorInfo 列不认识分组节点，已替换为自定义 `FTextInfoColumn`（文件夹分组 →"文件夹"，其余维度分组 → 维度显示名，叶子/世界节点沿用原生口径；代价：失去类超链接；按类型搜索不受影响——FTextInfoColumn::PopulateSearchStrings 会把函子输出喂进搜索串）。图标由 `IHwoDimension::GetGroupIcon` 提供：默认为维度静态图标；文件夹维度随展开状态 Open/Closed 切换；类维度为类的实际图标（`FSlateIconFinder::FindIconForClass` 沿继承链上溯，结果按类路径缓存）；
- Actor 叶子：完全复用框架表现（图标、眼睛、重命名、SCC 徽标）；
- 未加载叶子：沿用 `FActorDescTreeItem` 的灰显表现（基类自带）；
- "未分配"特殊组：图标用警告色（如 `Icons.Warning` 弱化版），组名"(未分配 DataLayer)"——这同时是 PRD 审计场景的入口。

---

## 9. World Partition 支持（D4）

- **枚举**：`FWorldPartitionHelpers::ForEachActorDescInstance`（WorldPartitionHelpers.h:95），普通关卡 `GetWorldPartition()==nullptr` 自然跳过；
- **取数**：`FWorldPartitionActorDesc` 免加载字段覆盖四维所需（§4.5 表）；
- **节点互换**：Actor 加载/卸载时由 `UActorDescContainerInstance::OnActorDescInstanceAdded/RemovedEvent` + `GEngine->OnLevelActorAdded/Deleted` 驱动叶子类型互换（同路径同 ID，用户无感）；`FActorHierarchy::AddActorDesc/RemoveActorDesc`（ActorHierarchy.h:90-93）是现成参考；
- **交互**：未加载叶子的选择/聚焦走 `FWorldPartitionHandle` 与 `FocusActorBounds()`（ActorDescTreeItem.h:82）。

---

## 10. 性能设计（对齐 PRD §七）

| PRD 要求 | 落地方案 |
|---|---|
| 10 万 Actor 滚动流畅 | 继承 STreeView 行虚拟化 + Slate 动态失效 |
| 视角切换 ≤ 2s | FullRefresh 走 5ms/帧分批管线；`ComputeLeafPaths` 是热路径：维度取数全部 O(1) 字段读取，禁止资产加载；路径串用预分配 FStringBuilder；10 万 × 3 维 ≈ 30 万次键计算，预估 < 1s，可接受 |
| 异步化 | MVP 取数全同步（均为主线程轻量字段）；面数/内存统计列（v0.2）才引入异步任务+缓存 |
| 编辑器帧率下降 ≤5% | 事件全走增量队列；Tick 内除框架管线外不做任何遍历 |

ID 用 MD5（`FArchiveMD5`，对齐 DataLayerEditor 先例）——每叶子一次哈希，10 万叶子 ≈ 毫秒级，无虞。

---

## 11. Undo / 事务

MVP 面板**只读**（除改名/可见性等框架自带能力）：这些走框架既有的 `FScopedTransaction` 封装，无需我们处理。v0.2 写操作（拖拽赋值、批量属性）统一模式：

```cpp
const FScopedTransaction Transaction(HWO_TEXT("BulkOp.UndoLabel", "...", "..."));
for (AActor* Actor : Actors) { Actor->Modify(); /* ...写... */ }
// 结束后 OnPostUndoRedo / 各维度事件 → Hierarchy 刷新
```

先例：`DataLayerMode.cpp:1037`（拖拽赋 DataLayer）、`DataLayerEditorSubsystem.cpp:1040`（批量加 DataLayer）。

## 12. 持久化

- 列可见性/宽度：`OutlinerIdentifier = "HierarchicalWorldOutliner"` → 框架自动存 `UOutlinerConfig`（SSceneOutliner.cpp:2707-2726），零代码（已实现）；
- 维度堆叠（已实现，2026-10-11，用户指定存储位置）：`FConfigFile` 直写 `<Project>/Saved/HierarchicalWorldOutliner/OutlinerState.ini` 的 `[DimensionStack] Dimensions=DataLayer,Class`（有序 ID 列表）；`FHwoDimensionStack::SaveToConfig/LoadFromConfig`，Stack 增删/拖拽排序即落盘，Panel 建树前 Load（Load 不广播 OnStackChanged，无回环）；未知 ID 静默跳过、空栈往返自洽。**命名预设（PRD F-202）仍属 v0.2**（TodoList Feature-14 后半）。
- 视图选项（2026-10-11）：齿轮菜单四开关（Only Selected / Only in Current Level / Hide Unloaded Actors / Always Frame Selection）存同一 ini 的 `[ViewOptions]` 节（`bShowOnlySelectedActors` / `bShowOnlyCurrentLevel` / `bHideUnloadedActors` / `bAlwaysFrameSelection`）；Mode 构造时 Load、切换即 Save；写盘前都先 Read 回已有文件避免互清其它节。原生 `UActorBrowserConfig` 在 SceneOutliner/Private 未导出故自建。

## 13. 风险与未决项

| 风险 | 应对 |
|---|---|
| `FActorMode` 拖拽默认实现（Attach/文件夹）与分组节点语义冲突 | 已禁死：`CreateDragDropOperation` 返回 nullptr + 显式覆写 `ValidateDrop` 返回 Invalid（双向）；v0.2 按维度分发（Design-4） |
| 框架对"分组节点被过滤但子叶命中"的展示细节 | §7 谓词设计（分组恒通过）+ Tick 徽标刷新跳过 `bIsFilteredOut` 分组（审查 N1 修复）；实现已验证 |
| 同名分组键跨维度撞 ID | ID 串行化含维度 ID 前缀（`DimA=KeyA` 格式）+ 键转义 + 每层 TSet 去重（审查 S3），已隔离 |
| DataLayerEditor 模块在纯 Runtime 项目不可用时 | 插件 Type=Editor，无此问题 |
| 引擎升级 5.9+ API 漂移 | 维度接口已隔离引擎 API；升级时只动 Dimension 实现层 |
| Level Instance 子 WorldPartition 未枚举（审查 W5） | MVP 已知限制（TodoList Bug-P2）；修复方向：`UWorldPartitionSubsystem::ForEachWorldPartition`（先例 `ActorHierarchy.cpp:512`）与 `FWorldPartitionHelpers::GetWorldPartition(&InActor)` |
| 未加载叶子的选择同步不对称（审查 W4） | 已知限制（TodoList Bug-P1），v0.2 参照 `ActorBrowsingMode.cpp:1494` 补齐 |
| Actor Tag 变更无事件，树不自动刷新（审查 J5） | 已知限制（TodoList Bug-P3），v0.2 挂 `OnObjectPropertyChanged` |
| 徽标计数参与排序/搜索键（审查 J2） | 低优先（TodoList Design-6），候选：徽标移出 DisplayString 或独立 Badge 列 |

未决项跟踪：见 `Doc/TodoList.md` Design-1~7 与 Bug-P1~8。

---

## 附录 A：关键引擎文件索引

| 主题 | 文件 |
|---|---|
| 框架接口 | `Engine/Source/Editor/SceneOutliner/Public/{SSceneOutliner, ISceneOutlinerMode, ISceneOutlinerHierarchy, ISceneOutlinerTreeItem, ISceneOutlinerColumn}.h` |
| 行为基类 | `SceneOutliner/Public/ActorMode.h`、`ActorModeInteractive.h`、`ActorBrowsingMode.h` |
| 节点基类 | `SceneOutliner/Public/{ActorTreeItem, ActorDescTreeItem, ActorBaseTreeItem, FolderTreeItem}.h` |
| 性能管线 | `SceneOutliner/Private/SSceneOutliner.cpp`（Populate :715 / PendingOps :2071 / 5ms 预算 :774） |
| 最佳先例 | `Engine/Source/Editor/DataLayerEditor/Private/DataLayer/`（SDataLayerBrowser.cpp:141 组装、DataLayerHierarchy.cpp:162 构建、:53 事件、DataLayerMode.cpp:1025 拖拽赋值、DataLayerActorTreeItem.h:51 组合 ID） |
| 第二先例 | `Engine/Source/Editor/WorldPartitionEditor/Public/WorldPartition/ContentBundle/Outliner/` |
| WP 取数 | `Engine/Source/Runtime/Engine/Public/WorldPartition/{WorldPartitionActorDesc, WorldPartitionActorDescInstance, WorldPartitionHelpers, WorldPartitionHandle}.h` |
| DataLayer API | `Engine/Source/Runtime/Engine/Public/WorldPartition/DataLayer/{DataLayerManager, DataLayerInstance}.h`；写操作 `Editor/DataLayerEditor/Public/DataLayer/DataLayerEditorSubsystem.h` |
| 编辑器服务 | `Editor/UnrealEd/Public/Subsystems/EditorActorSubsystem.h`、`Classes/Editor/EditorEngine.h:1577`（SelectActor）、`Public/ScopedTransaction.h` |
| Tab 注册 | `Editor/WorldPartitionEditor/Private/WorldPartitionEditorModule.cpp:1128`（OnRegisterTabs） |

## 附录 B：Feature 映射

| TodoList | 本文档章节 |
|---|---|
| Feature-1 脚手架/Tab | §3、§4.1 |
| Feature-2 面板骨架/空态 | §4.2、§8.1 |
| Feature-3 维度堆叠 | §4.5 |
| Feature-4 Hierarchy/WP | §4.6、§9 |
| Feature-5 四维视角 | §4.5 表 |
| Feature-6 选择联动 | §6 |
| Feature-7 结构化过滤 | §7 |
| Feature-8 徽标/Undo | §5.3、§11 |
