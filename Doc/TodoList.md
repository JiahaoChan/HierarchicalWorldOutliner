# HierarchicalWorldOutliner TodoList

单一事实来源：已确认 Bug、设计问题、待实现功能、快速修复项与已归档方案。

## Bugs

- ~~**Bug-1** 单层维度完全不分组、两层维度只有第一层生效（用户实测 2026-10-11）~~ ✅ 2026-10-11 已修复：`FHwoHierarchy::FindOrCreateParentItem` off-by-one——原实现对所有节点都去路径末段求父，但叶子的全路径每一段都是分组键（父 = 全路径最深分组），只有分组节点才去末段。修复后按节点类型分流。
- ~~**Bug-2** 文件夹分组图标为白色单色图标且与自带大纲不一致（用户报告 2026-10-11）~~ ✅ 2026-10-11 已修复：改用 `SceneOutliner.FolderClosed/FolderOpen`（随展开状态切换，逐条对齐 `ActorFolderTreeItem.cpp:136-145`）+ SBox 16px 尺寸约束；附带修复 D3 空栈回退维度每次新建导致分组节点维度弱引用失效的问题（回退维度改 static 缓存）。
- **Bug-P1**（审查 W4）视口→大纲选择同步对未加载叶子（`FHwoActorDescTreeItem`）不对称：取消选中分支只处理已加载叶子。影响小，v0.2 处理。
- **Bug-P2**（审查 W5）Level Instance 子 WorldPartition 未枚举：`HwoHierarchy::CreateItems` 只查 `World->GetWorldPartition()`，引擎先例为 `UWorldPartitionSubsystem::ForEachWorldPartition`（`ActorHierarchy.cpp:512`）。MVP 声明不支持 LevelInstance 内未加载 Actor，v0.2 处理。
- **Bug-P3**（审查 J5）Actor Tag 变更无引擎事件订阅，改 Tag 后树不自动刷新（Undo/Redo 或 FullRefresh 后恢复）。v0.2 挂 `OnObjectPropertyChanged` 过滤。
- **Bug-P4**（四轮审查 W2/W3，潜伏）HLOD 维度两处边缘场景：① LevelInstance 子 WP 的默认层口径（已加载侧取外层 WP、Desc 侧取子 WP）——当前被 Bug-P2（不枚举 LI 子 WP）掩盖，修 Bug-P2 时一并改已加载侧为 `FWorldPartitionHelpers::GetWorldPartition(Actor)`；② 层资产被删后 Desc 侧键保留历史资产名、加载侧落默认层，互换时短暂残留旧组（地图重存后自愈）。代码内已加 TODO 注释。
- **Bug-P5**（2026-10-11 登记）Actor 右键菜单（LevelEditor.ActorContextMenu）中的 "Force Load / Release Force Load" 依赖 `ISceneOutlinerMode::PinItems/CanPinItems`（基类默认空实现/false），`FHwoMode` 未覆写 → 灰显不可用。v0.2 随 Pin 语义一并实现。
- **Bug-P6**（八轮审查 J1）新建文件夹后无"选中+重命名"反馈。~~v0.2 候选：返回路径映射分组 ID~~ ✅ 2026-10-12 **选中+定位已实现**（`FHwoMode` 一次性 pending 路径 + `SSceneOutliner::GetOnFullRefreshDone` 钩子，命中后 AddToSelection+展开父链+ScrollItemIntoView）；**重命名仍欠**（新建即进入重命名态需框架 PendingRenameItem 机制配合，另行评估）。
- **Bug-P7**（八轮审查 J2）~~空文件夹在 HWO 面板不可见~~ ✅ 2026-10-12 已修复：文件夹维度为最外层有效维度时，`FHwoHierarchy::CreateItems` 枚举 `FActorFolders` 已注册文件夹补建零成员分组（随文件夹维度多段化改为按嵌套路径逐级补节点，见 Feature-20）；绑定 `FActorFolders::OnFolderCreated/Deleted/Moved` → FullRefresh。边界：文件夹维度非最外层（如 [Class, Folder]）时仍不枚举（无外层键可填）。
- **Bug-P8**（九轮审查 J2，登记）`PendingSelectFolderPath` 极端滞留：FullRefresh 时树完全为空则 `bProcessingFullRefresh` 为 false（SSceneOutliner.cpp:807），OnFullRefreshDone 不广播，pending 滞留到下一次有内容的刷新才消费，可能选中"很久以前"建的文件夹。触发条件苛刻（空世界 + 中途改堆叠）。可选修法：pending 带时间戳/刷新版本号守卫，超期丢弃。
- ~~**Bug-P9**（用户实测）选中 Actor 后首次点击分组节点，选择被清空需二次点击~~ ✅ 2026-10-12 已修复：根因是 `SelectActorsInViewport` 的 legacy 批处理（BeginBatchSelectOperation/SelectNone/SelectActor/NoteSelectionChange）使 `UTypedElementList` 的 pending-notify 标记残留（TypedElementList.cpp:438-441），帧末自动 flush 的回声广播落在 SSceneOutliner 的 bIsReentrant 守卫之外，触发 `OnLevelSelectionChanged` 误判外部变更而 ClearSelection。修复：改走原生通道 `UTypedElementSelectionSet::SetSelection` + `NotifyPendingChanges`（先例 ActorBrowsingMode.cpp:1130-1156），通知在守卫内同步消费；Build.cs 补 TypedElementFramework/TypedElementRuntime 依赖。与原生的刻意差异：选择变更未包 `FScopedTransaction`（原生 ActorBrowsingMode.cpp:1133 有，选择可撤销）——为最小修改取舍，选择不进 undo 栈，若日后需要再补。
- **Bug-P10**（Feature-22 已知取舍，登记）LocationVolume 的变换 / 形状编辑没有现成引擎事件，不触发刷新（增删与改名已接线：OnLevelActorAdded/Deleted + FCoreDelegates::OnActorLabelChanged）。代码注释已注明。
- **Bug-P11**（Feature-22 审查 W3②，登记）LocationVolume 维度未加载侧取世界退回 `GEditor->GetEditorWorldContext().World()`，多世界/PIE 场景下可能与 RepresentingWorld 不一致。v0.2 候选：`FHwoActorContext` 注入 RepresentingWorld（接口扩展）。
- ~~**Bug-P12**（用户实测崩溃）Chip 栈移除维度时崩溃~~ ✅ 2026-10-12 已修复：根因是 Slate 禁区——× 按钮点击仍在事件路由中，`Stack->RemoveDimension` 同步广播 OnStackChanged → `SHwoDimensionBar::RebuildChips` 的 ClearChildren 销毁了正在路由事件的控件子树（面板侧 FullRefresh 同理）。修复：Chip 栏与面板的堆叠变更响应统一改一次性 ActiveTimer 延迟一帧（pending 标记合并，定时器挂控件天然安全），ini 持久化保持同步。
- **Bug-P13**（十八轮审查 W1/W2，已知边缘项，不改行为）RuntimeGrid 维度双态口径两处微小差异：① Desc 侧 `GetIsSpatiallyLoaded` 折叠了 `RuntimeBounds.IsValid`（WorldPartitionActorDesc.h:315），"标记空间加载但包围盒无效"的 Actor 未加载侧落"非空间加载"组、已加载侧算到 (0,0) 格——加载/卸载互换瞬间跳动，同帧收敛自愈、场景稀少；② 已加载侧 GetActorBounds 与未加载侧 GetEditorBounds 口径非完全同义（编辑器 padding），贴格边 Actor 换叶瞬间可能短暂换组（同帧收敛）。代码注释已写明（HwoRuntimeGridDimension.cpp）。

## Design Issues

- **Design-1** 同一 Actor 多维度重复出现 vs TreeItemMap ID 唯一约束。已定并已验证：分组 ID = MD5(`G|`+路径串)，叶子 ID = MD5(`L|`+路径串+ActorGuid 二进制)，键转义防歧义（审查一轮验证通过）。
- **Design-2** 已定（用户确认）：全部维度实例置选中态、滚动定位第一个。已实现于 `FHwoMode::SynchronizeSelection`/`OnItemSelectionChanged`（AddToSelection/RemoveFromSelection + Direct 防回环，审查验证无回环）。
- **Design-3** 已定（用户确认）：`FHwoMode` 继承 `FActorMode`。已实现。
- **Design-4** 拖拽到"维度分组节点"的赋值语义按维度分发（DataLayer→`UDataLayerEditorSubsystem::AddActorsToDataLayer`，文件夹→`SetFolderPath`，标签→`Tags.Add`），先例 `DataLayerMode.cpp:1025`。状态：v0.2 范围，待定。
- **Design-5** 已定：方案 A——分组节点 DisplayString/标签带 `(N)` 去重计数徽标（`FHwoMode` 增量维护 + Tick 批量刷新）。
- **Design-6**（审查 J2）徽标计数目前参与 DisplayString → 计入排序键与搜索文本，填充期间分组顺序可能随计数跳动。低优先；候选：徽标只进 LabelWidget 或改独立 Badge 列。
- **Design-7**（八轮审查 J3，齿轮菜单未还原项登记）以下原生开关有意未迁移：Only in Current Data Layers / Only in Current Content Bundle / Hide Actor Components / Hide Empty Folders / Hide Temporary Actors / Hide Level Instance Content / Double Click toggles Current Folder——其中部分对 HWO 无对应节点类型（无组件/真实文件夹节点），如需还原逐项评估。另（八轮审查 J4，可选风格优化，暂不做）：`FHwoMode::SaveViewOptions` 与 `FHwoDimensionStack::SaveToConfig` 的"先 Read 再 Write"逻辑重复，可抽公共 helper。

## Features

### v0.1 MVP（对齐 PRD 路线图）

全部完成并归档（见下方归档区）。

### v0.2+（PRD 后续版本）

- **Feature-7** 基础结构化过滤：`class:` / `datalayer:` / `tag:` / `folder:` 字段语法（自绘过滤行 → 编译谓词写入 `InitOptions.Filters`，TDD §7）。从 MVP 推迟至 v0.2。
- **Feature-9** 其余视角：关卡/流送、附加层级、资源引用、规则分组（v0.2）；空间分区、修改者（v1.0）。
- **Feature-10** 批量操作：拖拽赋值、批量改名、批量属性（v0.2）。
- **Feature-11** 列视图与聚合统计：可配置列、分组面数/内存合计（v0.2）。
- **Feature-12** 审计体检、规则着色、CSV 导出、快照对比（v0.3）。
- **Feature-13** PIE 支持、多人协作状态、本地化完善（v1.0）。
- **Feature-14** 维度堆叠 GConfig 持久化 + 视图预设（TDD §12；预设下拉占位按钮）。从 MVP 推迟至 v0.2。

## 归档

- **Feature-1** ✅ 2026-10-11 插件脚手架：`.uplugin`、Editor 模块、LevelEditor Tab 注册（`FLevelEditorModule::OnRegisterTabs`，Outliner 分组 + 窗口菜单入口）、加入 `OutlinerDev.uproject`。
- **Feature-2** ✅ 2026-10-11 面板骨架 `SHwoPanel`（Chip 栏 + 树 + 状态栏），空态回退纯文件夹维度（D3）。
- **Feature-3** ✅ 2026-10-11 维度堆叠框架：`IHwoDimension`/`FHwoDimensionStack`，1~5 层堆叠（上限 2026-10-11 由 3 调整为 5），Chip 增删 + 拖拽排序，每层键 TSet 去重（审查 S3）。
- **Feature-4** ✅ 2026-10-11 `FHwoHierarchy`：已加载 `FActorRange` + WP 未加载 `ForEachActorDescInstance` 双路枚举（含去重守卫），`FindOrCreateParentItem` 按路径造分组节点，事件增量 + FullRefresh 分级。
- **Feature-5** ✅ 2026-10-11 四维内置视角；Class 维度两侧统一按 `GetParentNativeClass` 口径（审查 S2 根治；BP Actor 归父原生类，基类深度配置留 v0.2）。
- **Feature-6** ✅ 2026-10-11 双向选择联动（D2 语义，审查验证无回环、无分批填充互清）。
- **Feature-8** ✅ 2026-10-11 分组数量徽标（方案 A + 去重计数 + Tick 批量刷新）。MVP 面板只读，写操作事务不适用（TDD §11）。
- **Feature-15** ✅ 2026-10-12 默认布局集成（用户提出）：`FLevelEditorModule::OnRegisterLayoutExtensions` + `FLayoutExtender::ExtendLayout(LevelEditorSceneOutliner, After, OpenedTab)` 把 Tab 插在内置 World Outliner 之后并默认打开；Reset/Load Layout 重建时自动恢复，用户布局已含该 Tab（含 ClosedTab）时不重复插入，即手动关闭不会被强开。
- **Feature-16** ✅ 2026-10-12 Chip 固定配色（用户提出）：五内置维度固定调色板（HSV8 S=220 高饱和 / V=130 低明度：DataLayer 青蓝、Class 紫、Folder 琥珀、Tag 绿、HLOD 品红），替代会话内 HSV 随机；未知维度按 ID 哈希取色相，跨会话稳定。
- **Feature-17** ✅ 2026-10-12 类维度分组图标（用户提出）：`FHwoClassDimension::GetGroupIcon` 覆写为类的实际图标（`FSlateIconFinder::FindIconForClass` 沿类继承链上溯，回退 `ClassIcon.Default`），按类路径名缓存避免逐帧上溯；未分配组回退维度静态图标。
- **Feature-18** ✅ 2026-10-12 恢复过滤栏（用户提出，原 MVP 推迟项）：`SHwoPanel` 启用 `FilterBarOptions`（对齐 `SLevelEditor.cpp:807-816`——`FLevelEditorOutlinerSettings::GetOutlinerFilters` 同款过滤器集 + Common 分类展开 + 共享文本过滤设置）；`FHwoMode` 补覆写 `CompareItemWithClassName`（FActorMode 未覆写，基类默认 false 会把全部条目滤掉，复刻 `FActorBrowsingMode.cpp:2000` 含 BP 双路径匹配与未加载 Actor 原生类路径）；分组节点与原生文件夹同语义（false，由 `EnsureParentForItem` 保活父链，空组自消）。结构化字段语法（Feature-7）仍属 v0.2。
- **Feature-19** ✅ 2026-10-12 还原原生工具栏两个功能：① 新建包含当前选中项的文件夹（`bShowCreateNewFolder=true` + `FHwoMode::CreateNewFolder` 照抄 `ActorBrowsingMode.cpp:1509` 先例）；② 齿轮菜单（`ShowViewButton=true` 自动获得框架三条；Only Selected / Only in Current Level / Hide Unloaded Actors 走 `FilterInfoMap`，Hide Unloaded 为过滤级+枚举级双保险；Always Frame Selection 走 `CreateViewContent`），四开关持久化到 Saved ini `[ViewOptions]` 节（原生 UActorBrowserConfig 未导出故自建）。
- **Feature-20** ✅ 2026-10-12 文件夹维度多段嵌套（用户定案）：`IHwoDimension` 新增 `ExpandKeyToPathSegments`（默认一键一段），文件夹维度按 "/" 拆多段呈现真实嵌套（堆叠任意位置生效）；`ComputeLeafPaths` 改为"键→段序列→笛卡尔积"；空文件夹枚举、新建文件夹父级推导（分组 Path 内文件夹段拼回）、Bug-P6 选中定位的 ID 构造同步改多段；无文件夹 Actor 保留"未分配"分组（用户明确不还原散落根级）。
- **Feature-21** ✅ 2026-10-12 世界根节点 + 分组选择解耦（用户定案）：① 复用引擎全导出的 `FWorldTreeItem` 作单根——`FHwoHierarchy::CreateItems` 第 0 步创建、`FindOrCreateParentItem` 顶层挂接到世界节点（ID=FObjectKey(World)），标签/图标/类型列/右键菜单全部原生；② `FHwoMode::OnItemSelectionChanged` 只同步 Actor 叶子，分组与世界节点不再联动选中子代 Actor（能力保留在分组右键"选中组内全部 Actor"）。
- **Feature-22** ✅ 2026-10-12 新增两个内置维度：Unsaved（是否保存——外部包脏状态二分，与原生未保存列同源判定链，OnUnsavedAssetAdded/Removed 接线含 __ExternalActors__ 路径过滤）与 LocationVolume（AABB 相交分组，帧缓存防 O(N²)，Volume 增删/改名接线）；Build.cs 补 SourceControl/UnsavedAssetsTracker；Chip 调色板补红/黄绿两色。

## 变更日志

| 日期 | 变更 | Agent |
|------|------|-------|
| 2026-10-10 | **初始化**: 依据 `Doc/层级世界大纲插件_PRD.docx` 建立 Features（v0.1~v1.0）；记录 SceneOutliner 调研结论 Design-1~5 | Kimi |
| 2026-10-10 | **技术路线决策**: 用户确认方案 A（复用 SceneOutliner 框架 / DataLayerEditor 范式）、Design-2 选择同步语义、Design-3 Mode 继承 FActorMode、空态回退文件夹维度、MVP 含 WP 未加载 Actor、面板与内置 WorldOutliner 并排 | Kimi |
| 2026-10-10 | **新增文档**: `Doc/层级世界大纲插件_TDD.md` V0.1——v0.1 MVP 完整技术设计（模块结构、类签名、ID 方案、数据流、选择同步、过滤、UI、性能），含 Feature 映射表 | Kimi |
| 2026-10-11 | **Feature-1~6/8 代码实现**: 按 TDD 写入全部 MVP C++ 源码（模块+Tab 注册、SHwoPanel/SHwoOutliner/Chip 栏、FHwoMode/FHwoHierarchy、四内置维度、三个 TreeItem、ID 哈希方案、选择同步、分组计数徽标） | Kimi |
| 2026-10-11 | **编译通过**: `OutlinerDevEditor Win64 Development` 构建成功，0 警告（修复 include 路径、`StaticCastSharedRef` 转换、InputCore 依赖） | Kimi |
| 2026-10-11 | **独立审查三轮通过**: 修复 S1（Rebuild 清索引）/S2（加载卸载换叶 ID 口径）/S3（维度键去重防 check 崩溃）/W1/W2/W3/W6/W7/J1/N1（Tick 刷新误删过滤分组），终审"通过"；遗留 W4/W5/J5 转 Bug-P1~3，J2 转 Design-6，结构化过滤与堆叠持久化（W8）排入 v0.2（Feature-7/14） | Kimi |
| 2026-10-11 | **Bug-1 修复**: 用户实测发现单层维度不分组、两层只有第一层生效——`FindOrCreateParentItem` 叶子父路径 off-by-one，已修复（待用户编辑器内 Live Coding 热编译后实测确认） | Kimi |
| 2026-10-11 | **Feature-14 前半（堆叠持久化）**: 按用户指定存 `<Project>/Saved/HierarchicalWorldOutliner/OutlinerState.ini`（`[DimensionStack] Dimensions=...`），增删/排序即时落盘、开面板恢复；命名预设仍属 v0.2 | Kimi |
| 2026-10-11 | **HLOD 层维度 + Bug-2 修复**: 新增第五内置维度 HLOD 层（显式层→WP 默认层→未分配，不参与 HLOD 单独分组，键=包路径）；文件夹图标对齐自带大纲（SceneOutliner.FolderOpen/Closed 随展开切换）；四轮审查"通过"+W1（键跨包冲突）修复；编译通过 0 警告 | Kimi |
| 2026-10-11 | **三需求**: ① 面板列组改为 `FSceneOutlinerModule::CreateActorBrowserColumns`（还原 Label/Gutter/ActorInfo/SourceControl/Unsaved + WP Pinned 列，与内置大纲同款）；② Actor 叶子右键改走 `FLevelEditorContextMenu::BuildMenuWidget`（LevelEditor.ActorContextMenu，纯分组选中仍走自定义菜单；PinItems 灰显登记为 Bug-P5）；③ 维度堆叠上限 3→5（`FHwoDimensionStack::MaxStackDepth`，Chip 栏 tooltip 同步改走常量格式化） | Kimi |
| 2026-10-11 | **五轮审查修复**: W1 未加载叶子右键回归——分流条件收窄为只判已加载 `FHwoActorTreeItem`，未加载叶子回落自定义菜单路径（基类自带 Focus/Copy Path）；W2 组合爆炸护栏——`ComputeLeafPaths` 加 `MaxPathsPerActor=64` 截断 + 每进程一次性 UE_LOG 告警（LogHwo） | Kimi |
| 2026-10-11 | **Chip 彩色圆角底**: Chip 背景换 `FSlateRoundedBoxBrush`（半径 4，内边距 8/3，白字），颜色按维度 ID 在 `SHwoDimensionBar::ChipColors` 缓存（会话内 HSV 随机：H 0~360、S 0.45~0.65、V 0.55~0.75），Chip 重建不跳色；顺带清理 TDD 页眉索引（Feature-14/Design-6/Bug-P1~5） | Kimi |
| 2026-10-11 | **FText 本地化改造**: 新增 `Public/HwoLocalization.h` 的 `HWO_TEXT` 宏（FPolyglotTextData，Category=Editor/NativeCulture=en，zh/zh-Hans/zh-CN 三键同文，static 缓存防每帧 UpdateLiveTable）；27 处 UI 文本调用点全部替换（Format 场景 pattern 走宏），LOCTEXT_NAMESPACE 全插件清零；运行时数据透传点（FText::FromString）不动；TDD 新增 §3.4 本地化方案 | Kimi |
| 2026-10-11 | **七轮审查尾巴（J1/J4）**: J1 共享 Key `Dim.Unassigned` 拆分为 `Dim.Class/DataLayer/Folder/Tag.Unassigned`（文案不变），`Tab.Label` 两处调用收敛为模块内 `HwoModuleText::GetTabLabel()` 单一访问点；J4 TDD §3.3 明确"新代码禁止再写 LOCTEXT"，§3.4 补充 Key 共享约定（跨文件不复用 Key、同文件收敛单一访问点），§11 示例代码同步为 HWO_TEXT | Kimi |
| 2026-10-12 | **全插件代码形式规范整理**: 按 ue5-cpp-standards skill 对 32 个源文件做注释规范化（中文类/函数/属性/委托注释、@param 对齐、override 与 Getter/Setter 豁免、审校过时注释）+ 排版整理（CoreMinimal.h 居首独占段、include 分组字母序、前向声明区块、类内成员总顺序、Slate 类 SLATE_BEGIN_ARGS 专项、override 基类横幅）；零逻辑改动（代码行多重集比对一致），编译通过 0 警告，两轮独立审查终审"通过"；保留项：普通成员内联默认值未挪构造函数（范围控制）、SHwoDimensionChip 委托声明因 SLATE_EVENT 值引用保留在参数块之前 | Kimi |
| 2026-10-12 | **Feature-15/16（用户两需求）**: ① 默认布局集成——`OnRegisterLayoutExtensions` + `FLayoutExtender::ExtendLayout(LevelEditorSceneOutliner, After, OpenedTab)`，Tab 默认打开并插在内置 World Outliner 之后，Reset/Load Layout 自动恢复、用户关闭（ClosedTab）不强开；② Chip 颜色改固定调色板（HSV8 S=220/V=130 高饱和低明度，五内置维度各一色相），移除 `SHwoDimensionBar::ChipColors` 会话随机缓存，未知维度按 ID 哈希取色；TDD §3.2/§4.1 与 AGENTS.md 同步 | Kimi |
| 2026-10-12 | **Tab 标题缩短**: `Tab.Label` 英文 "Hierarchical World Outliner"→"Hierarchical Outliner"（太长被缩略）；仅 Tab 标题，菜单入口（`Menu.OpenTab`）等其余文案与中文不变 | Kimi |
| 2026-10-12 | **Feature-17（类维度分组图标）**: `FHwoClassDimension::GetGroupIcon` 覆写为类实际图标（`FSlateIconFinder::FindIconForClass`，SlateCore 模块无新依赖），按类路径缓存；未分配组回退静态图标；TDD §8.3 同步 | Kimi |
| 2026-10-12 | **品牌紫统一降饱和**: 新增 `Public/HwoStyle.h` 共享常量 `HwoStyle::AccentPurple`（HSV H300°/S0.65/V1.0，由引擎 FSlateColor 默认 Fuchsia 降饱和）；Tab 图标 `IconColor` 与状态栏文字（`FHwoMode::GetStatusTextColor` 覆写）共用该常量 | Kimi |
| 2026-10-12 | **Chip 栏顶部留白**: `SHwoPanel` Chip 栏 Slot Padding 由 `(2)` 改 `(2,9,2,2)`，顶部留 9px 避免贴边（先定 6px，用户实测后调为 9px） | Kimi |
| 2026-10-12 | **Feature-18（恢复过滤栏）**: 搜索框左侧过滤器按原生 World Outliner 还原——`FilterBarOptions.bHasFilterBar/bUseSharedSettings` 开启 + `GetOutlinerFilters` 同款过滤器集；`FHwoMode::CompareItemWithClassName` 覆写（复刻 FActorBrowsingMode，否则类型过滤会滤掉全部条目）；编译通过 0 警告；TDD §4.2/§5 同步 | Kimi |
| 2026-10-12 | **Feature-19（新建文件夹 + 齿轮菜单）**: 还原原生工具栏——`bShowCreateNewFolder=true` + `FHwoMode::CreateNewFolder/SupportsCreateNewFolder`（FActorFolders 先例）；`ShowViewButton/ShowFilterOptions=true`，三过滤开关走 FilterInfoMap（Only Selected/Only Current Level/Hide Unloaded），Always Frame Selection 走 CreateViewContent 并门控 SynchronizeSelection 滚动；四开关持久化到 OutlinerState.ini `[ViewOptions]` 节（先 Read 再 Write 防互清，Stack 的 SaveToConfig 同步加了 Read 保护）；`FHwoDimensionStack::GetConfigFilePath` 提升为公共静态；TDD §4.2/§4.4/§12 同步 | Kimi |
| 2026-10-12 | **Feature-19 链接修复**: 引擎 `SceneOutliner::FFolderPathSelector` 实现未导出（LNK2019）——`FHwoMode::CreateNewFolder` 改用本地 lambda selector：文件夹维度分组节点（经 `FHwoGroupTreeItem::Path` 末段键 + `FHwoFolderDimension::StaticDimensionID()` 判定）取路径作新文件夹父级，其余节点跳过；附带给 `FHwoFolderDimension` 加 `StaticDimensionID()` 静态取口（GetDimensionID 内联到头文件转发） | Kimi |
| 2026-10-12 | **八轮审查尾巴登记（纯文档，无代码改动）**: J1→Bug-P6（新建文件夹无选中/重命名反馈，NewItemActions 对 MD5 ID 树无效）、J2→Bug-P7（空文件夹不可见，D3 固有取舍）、J3→Design-7（齿轮菜单未还原项清单）、J4（Save 路径"先 Read 再 Write"抽 helper）作为可选优化记入 Design-7 | Kimi |
| 2026-10-12 | **Bug-P6/P7 修复（用户实测驱动）**: ① 空文件夹可见——文件夹维度为最外层有效维度时枚举 FActorFolders 注册文件夹补建零成员分组（`CreateGroupItemForPath` 抽出与 FindOrCreateParentItem 共用），绑定 OnFolderCreated/Deleted/Moved 三委托 → FullRefresh；② 新建文件夹选中+定位——`FHwoMode` 一次性 `PendingSelectFolderPath` + `GetOnFullRefreshDone` 钩子（AddToSelection+展开父链+ScrollIntoView，不动 GEditor 选择），文件夹维度非最外层时丢弃；重命名仍欠（Bug-P6 保留） | Kimi |
| 2026-10-12 | **九轮审查尾巴**: J1 措辞修正——文件夹维度为完整路径单段键，"A" 与 "A/B" 平铺为平级根级分组（HwoHierarchy.cpp 注释 / TDD §4.6 / TodoList Bug-P7 三处同步）；J2 登记 Bug-P8（PendingSelectFolderPath 空树滞留，可选时间戳守卫）；TDD 索引 Bug-P1~8 | Kimi |
| 2026-10-12 | **Feature-20（文件夹维度多段嵌套，用户定案）**: `IHwoDimension::ExpandKeyToPathSegments` 新增（默认一键一段），文件夹维度覆写按 "/" 拆段呈嵌套；`ComputeLeafPaths` 键→段序列→笛卡尔积；空文件夹枚举 / `HwoFolderPathSelector`（分组 Path 内文件夹段拼回完整路径，跳过未分配段）/ Bug-P6 选中定位 ID 构造同步多段化；排序核实：分组=Folder(20) 先于 Actor(30) 与原生一致，保持现状；TDD §4.5 契约/§4.6 同步 | Kimi |
| 2026-10-12 | **十轮审查尾巴**: J2 注释——`HwoFolderDimension::ExpandKeyToPathSegments` 的 bCullEmpty 吞段前提（依赖 FActorFolders 路径规范，无害防御）；J3 核实——五个维度"未分配" Key 实为早已独立（七轮 J1 已拆），本轮顺手把 Class/DataLayer/Folder/Tag 的英文文案区分化为 "(No {0})"（中文统一"未分配"不变），HLOD 维度本就独立 | Kimi |
| 2026-10-12 | **Feature-21（世界根节点 + 分组选择解耦）**: ① 树多根→单根，复用引擎 `FWorldTreeItem`（SCENEOUTLINER_API 全导出，原生标签/菜单零成本）；② 单击分组不再递归选中子代 Actor（OnItemSelectionChanged 只处理叶子，纯分组选择不动视口）；TDD §4.6（建树流程）/§6.1（选择同步）同步 | Kimi |
| 2026-10-12 | **十一轮审查 J1/J2（对齐原生选择语义）**: `FHwoMode::OnItemSelectionChanged`——树内选择不含 Actor 叶子（纯分组/世界节点/空选择）时改为清空视口 Actor 选择 + 未加载句柄选择（走 SelectActorsInViewport 空集路径，判等早退幂等）；防回流靠既有 bIsReentrant 保护（本函数恒在其中执行），无需新增守卫 | Kimi |
| 2026-10-12 | **未分配组排序沉底（用户实测反馈）**: `FHwoMode::GetTypeSortPriority`——末段键为 HwoGroupKeys::Unassigned 的分组返回 Folder+1（21），排在真实分组（20）之后、Actor（30）之前，各维度统一生效；降序时未分配组相应到最前（优先级方案固有表现，注释已注明） | Kimi |
| 2026-10-12 | **文件夹分组三列还原（用户实测反馈）**: ① 可视性——`FHwoGroupTreeItem::ShouldShowVisibilityState` 对文件夹维度分组返回 true（显示=子代 OR 聚合、点击框架递归传播，未加载叶子与原生一致恒 false 不可设）；② 未保存——分组节点新增 `World` 弱引用 + `GetPackageName` 覆写经 `FFolder::GetActorFolder` 取外部包（新增 `IsFolderDimensionGroup`/`ReconstructFolderPath`，HwoMode 的 selector 同步复用后者去重）；③ 类型列——引擎 ActorInfo 硬编码函子不认识分组节点，`SHwoPanel` 用 `FTextInfoColumn` 自定义列替换（文件夹分组"文件夹"/其余维度分组显示维度名/叶子与世界节点原生口径，代价：失去类超链接；按类型搜索不受影响（FTextInfoColumn::PopulateSearchStrings 会把函子输出喂进搜索串））；TDD §4.7/§8.3 同步 | Kimi |
| 2026-10-12 | **Bug-P9 修复（分组首击丢选择）**: `SelectActorsInViewport` 从 legacy 批处理改走原生 TypedElement 通道（`SetSelection`+`NotifyPendingChanges`，守卫内同步通知）；Build.cs 补 TypedElementFramework/TypedElementRuntime；判等早退保留，拿不到 SelectionSet 时直接返回 | Kimi |
| 2026-10-12 | **Feature-22（Unsaved + LocationVolume 维度）+ 十四轮审查修复**: 两个新内置维度实现并注册（调色板/持久化/菜单自动生效）；审查尾巴——W1 OnUnsavedAssetChanged 加路径过滤（__ExternalActors__ 或 RepresentingWorld 关卡包）；W2 LocationVolume 前置分支补世界守卫；W3① 挂 FCoreDelegates::OnActorLabelChanged 处理 Volume 改名（组名即 Label）；F1 两维度补 StaticDimensionID 静态取口消除魔法字符串；F3 中文文案有意保留英文术语加注释；登记 Bug-P10（Volume 变换不刷新）/Bug-P11（未加载侧世界回退 GEditor）；TDD §4.5 表补两行、数量描述改七个 | Kimi |
- **Feature-23** ✅ 2026-10-12 状态栏原生对齐 + 多路径紫色提醒 + Unsaved 排序 + 位置体积翻译：① `GetStatusText` 复刻原生四分支（唯一 Actor 数 / ApplicableActorGuids 过滤前总数 / 选中叶子数），WP loaded 分支省略；② `GetStatusTextColor` 原生三色 + 多路径选中紫色前置（`HasMultiPathSelectedActor`）；③ Unsaved 维度"未保存"组 Folder-1 档排在"已保存"前；④ LocationVolume 中文名改"位置体积"。
| 2026-10-12 | **Feature-23（状态栏对齐 + Unsaved 排序 + 位置体积翻译）**: 状态栏复刻原生四分支文本与三色（计数口径：树内唯一 Actor 数 / 过滤前 ApplicableActorGuids / 选中叶子数），任一选中 Actor 多路径时整行紫色前置；`GetTypeSortPriority` 加 Unsaved 未保存组 Folder-1 档；`FHwoUnsavedDimension` 补 `GetUnsavedGroupKey` 静态取口；`TotalLeafCount` 转备用注释；TDD §8.2 重写、§4.5 Unsaved 行补排序约定、§3.4 示例 Key 更新 | Kimi |
| 2026-10-12 | **十五轮审查 W1**: `FHwoMode::OnItemRemoved` 在索引移除后补 `ApplicableActorGuids.Remove`（Actor 无剩余叶子时），保持状态栏"过滤前总数"口径精确（先例 ActorBrowsingMode.cpp:1012） | Kimi |
| 2026-10-12 | **状态栏 WP loaded 分支 + 官方译文对齐（用户实测反馈）**: 新增 `LoadedLeafCountByGuid`（TMap<Guid,int>，OnItemAdded/Removed 按已加载叶子增量维护，归零删键），WP 世界无过滤无选中分支补 "{0} actors ({1} loaded)"（原生"有选中"分支经核实不带 loaded 段）；全部状态栏格式串中文逐字对齐引擎官方简体（从 Editor.locres 二进制解析提取："{0}个Actor（已选{1}个）"等六条）；TDD §8.2 同步 | Kimi |
| 2026-10-12 | **Unsaved 分组图标染色**: `FHwoUnsavedDimension::GetGroupIcon` 覆写——"未保存"组染 AccentRed（警示）、"已保存"组染 AccentGreen（与状态栏过滤提示色同源），画刷副本懒初始化存维度 mutable 成员（生命周期随面板），其余键回退默认图标；接口与 GenerateLabelWidget 不动 | Kimi |
| 2026-10-12 | **Bug-P12 修复（移除 Chip 崩溃）**: Slate 禁区——事件路由中同步销毁控件；`SHwoDimensionBar::HandleStackChanged` 与 `SHwoPanel::OnDimensionStackChanged` 改一次性 ActiveTimer 延迟一帧重建（pending 合并标记），持久化保持同步；TDD §8.1 补该约束 | Kimi |
- **Feature-24** ✅ 2026-10-12 RuntimeGrid 空间网格维度（用户定案方案 C）：第八个内置维度——先分流"非空间加载"（双侧 `GetIsSpatiallyLoaded`，Desc 侧接口已折叠 ForceNonSpatiallyLoaded），再按 Grid 名（`GetRuntimeGrid` 同序列化字段，NAME_None 回落 `GetDefaultGrid()`）+ 包围盒中心点 floor 除法得 cell 键 `"Grid|X|Y"`；`UEngine::OnActorMoved` + FTSTicker 0.2s 去抖刷新；"非空间加载"组 Folder+1 档与未分配组同档沉底；Chip 调色板第 8 色（蓝绿）；已知边缘项登记 Bug-P13。
| 2026-10-12 | **Feature-24（RuntimeGrid 空间网格维度，方案 C）**: 新增 `HwoRuntimeGridDimension.cpp` + 注册三处/调色板/排序档；弃用 `FSquare2DGridHelper`（GridSize 生成期才稳定）改手写 floor 除法（注释标出处）；`Grids` private 改走 `GetDefaultGrid()`；编译两轮（Ticker 句柄类型 `FTSTicker::FDelegateHandle` 修复）0 错 0 警；十八轮审查 PASS，W1/W2/N2 以注释补强 + 登记 Bug-P13（不改行为）；TDD §4.5 表补行、数量改八个 | Kimi |
- **Feature-25** ✅ 2026-10-12 SourceControl 版本控制维度（用户定案：降级方案 a + 离线合并过期/最新 + MVP QueueStatusUpdate）：第九个内置维度——按 Actor 外部包的 SCC 状态十级归组（未知/未受控/冲突/已删除/新增/他人签出/已签出/已修改/过期/最新，优先级首命中制）；取包链与 Unsaved 维度同链；`GetState(Use)` 纯缓存读 + 未命中懒回填 `QueueStatusUpdate`；state-changed 无参广播 → 键级变化检测 → 有变才去抖 FullRefresh（回填风暴防护）；Provider 切换句柄随迁 + 清缓存；SVN 语义注释（IsCheckedOut=svn:lock 常年为空、Conflicted 先于 Modified、离线不分过期）。
| 2026-10-12 | **Feature-25（SourceControl 版本控制维度）**: 新增 `HwoSourceControlDimension.cpp` + 注册三处/调色板第 9 色（蓝紫）；Hierarchy 接线 state-changed/provider-changed + 独立去抖句柄，UnbindEvents 含 `IsEngineExitRequested` 防御；编译 0 错 0 警；主 Agent 审查（子代理配额耗尽）发现并修复一个 Warn——Provider 切换时 state-changed 句柄未随迁（丢失新 Provider 回填广播），已在 `OnSourceControlProviderChanged` 用 Old/New 参数完成迁移；TDD §4.5 表补行、数量改九个 | Kimi |
