/* File import from libegg to gnumeric by import-egg.  Do not edit.  */

#ifndef EGG_TOOL_ITEM_H
#define EGG_TOOL_ITEM_H

#include <gtk/gtk.h>

#define EGG_TYPE_TOOL_ITEM            (egg_tool_item_get_type ())
#define EGG_TOOL_ITEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EGG_TYPE_TOOL_ITEM, EggToolItem))
#define EGG_TOOL_ITEM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EGG_TYPE_TOOL_ITEM, EggToolItemClass))
#define EGG_IS_TOOL_ITEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EGG_TYPE_TOOL_ITEM))
#define EGG_IS_TOOL_ITEM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EGG_TYPE_TOOL_ITEM))
#define EGG_TOOL_ITEM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), EGG_TYPE_TOOL_ITEM, EggToolItemClass))

typedef struct _EggToolItem      EggToolItem;
typedef struct _EggToolItemClass EggToolItemClass;

struct _EggToolItem
{
  GtkBin parent;

  GtkOrientation orientation;
  GtkIconSize icon_size;
  GtkToolbarStyle style;

  guint visible_horizontal : 1;
  guint visible_vertical : 1;
  guint homogeneous : 1;
  guint expandable : 1;
  guint pack_end : 1;
};

struct _EggToolItemClass
{
  GtkBinClass parent_class;

  void       (* clicked)             (EggToolItem    *tool_item);
  GtkWidget *(* create_menu_proxy)   (EggToolItem    *tool_item);
  void       (* set_orientation)     (EggToolItem    *tool_item,
				      GtkOrientation  orientation);
  void       (* set_icon_size)       (EggToolItem    *tool_item,
				      GtkIconSize     icon_size);
  void       (* set_toolbar_style)   (EggToolItem    *tool_item,
				      GtkToolbarStyle style);
  void       (* set_relief_style)    (EggToolItem    *tool_item,
				      GtkReliefStyle  relief_style);
};

GType      egg_tool_item_get_type (void);
GtkWidget *egg_tool_item_new      (void);

void egg_tool_item_set_orientation (EggToolItem *tool_item, GtkOrientation orientation);
void egg_tool_item_set_icon_size (EggToolItem *tool_item, GtkIconSize icon_size);
void egg_tool_item_set_toolbar_style (EggToolItem *tool_item, GtkToolbarStyle style);
void egg_tool_item_set_relief_style (EggToolItem *tool_item, GtkReliefStyle style);

void egg_tool_item_set_homogeneous (EggToolItem *tool_item, gboolean homogeneous);
void egg_tool_item_set_expandable (EggToolItem *tool_item, gboolean expandable);
void egg_tool_item_set_pack_end (EggToolItem *tool_item, gboolean pack_end);

#endif
