/* File import from libegg to gnumeric by import-egg.  Do not edit.  */

#ifndef EGG_TOOL_BUTTON_H
#define EGG_TOOL_BUTTON_H

#include "eggtoolitem.h"

#define EGG_TYPE_TOOL_BUTTON            (egg_tool_button_get_type ())
#define EGG_TOOL_BUTTON(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EGG_TYPE_TOOL_BUTTON, EggToolButton))
#define EGG_TOOL_BUTTON_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EGG_TYPE_TOOL_BUTTON, EggToolButtonClass))
#define EGG_IS_TOOL_BUTTON(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EGG_TYPE_TOOL_BUTTON))
#define EGG_IS_TOOL_BUTTON_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EGG_TYPE_TOOL_BUTTON))
#define EGG_TOOL_BUTTON_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), EGG_TYPE_TOOL_BUTTON, EggToolButtonClass))

typedef struct _EggToolButton      EggToolButton;
typedef struct _EggToolButtonClass EggToolButtonClass;

struct _EggToolButton
{
  EggToolItem parent;

  /*< private >*/
  GtkWidget *button;
  GtkWidget *box;
  GtkWidget *label;
  GtkWidget *icon;

  gchar *stock_id;
  guint label_set : 1;
  guint icon_set : 1;
};

struct _EggToolButtonClass
{
  EggToolItemClass parent_class;

  GType button_type;
};

GType        egg_tool_button_get_type       (void);
EggToolItem *egg_tool_button_new_from_stock (const gchar *stock_id);

#endif
