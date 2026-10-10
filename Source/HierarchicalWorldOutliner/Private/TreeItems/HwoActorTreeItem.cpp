// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "TreeItems/HwoActorTreeItem.h"

// Parent 指回基类 Type，引擎内置列按 FActorTreeItem 类型匹配的分支对本节点依然生效
const FSceneOutlinerTreeItemType FHwoActorTreeItem::Type(&FActorTreeItem::Type);
