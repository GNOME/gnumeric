/* File import from libegg to gnumeric by import-egg.  Do not edit.  */

#ifndef EGG_SEPARATOR_TOOL_ITEM_H
#define EGG_SEPARATOR_TOOL_ITEM_H

#include "eggtoolitem.h"

#define EGG_TYPE_SEPARATOR_TOOL_ITEM            (egg_separator_tool_item_get_type ())
#define EGG_SEPARATOR_TOOL_ITEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EGG_TYPE_SEPARATOR_TOOL_ITEM, EggSeparatorToolItem))
#define EGG_SEPARATOR_TOOL_ITEM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EGG_TYPE_SEPARATOR_TOOL_ITEM, EggSeparatorToolItemClass))
#define EGG_IS_SEPARATOR_TOOL_ITEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EGG_TYPE_SEPARATOR_TOOL_ITEM))
#define EGG_IS_SEPARATOR_TOOL_ITEM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EGG_TYPE_SEPARATOR_TOOL_ITEM))
#define EGG_SEPARATOR_TOOL_ITEM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), EGG_TYPE_SEPARATOR_TOOL_ITEM, EggSeparatorToolItemClass))

typedef struct _EggSeparatorToolItem      EggSeparatorToolItem;
typedef struct _EggSeparatorToolItemClass EggSeparatorToolItemClass;

struct _EggSeparatorToolItem
{
  EggToolItem parent;
};

struct _EggSeparatorToolItemClass
{
  EggToolItemClass parent_class;
};

GType        egg_separator_tool_item_get_type (void);
EggToolItem *egg_separator_tool_item_new      (void);

#endif
