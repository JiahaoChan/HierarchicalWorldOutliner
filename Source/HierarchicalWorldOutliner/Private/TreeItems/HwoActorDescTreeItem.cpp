// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "TreeItems/HwoActorDescTreeItem.h"

// Parent 指回基类 Type，引擎按基类类型匹配的内置行为对本节点依然生效
const FSceneOutlinerTreeItemType FHwoActorDescTreeItem::Type(&FActorDescTreeItem::Type);
