/* File import from libegg to gnumeric by import-egg.  Do not edit.  */

#ifndef EGG_TOGGLE_TOOL_BUTTON_H
#define EGG_TOGGLE_TOOL_BUTTON_H

#include "eggtoolbutton.h"

#define EGG_TYPE_TOGGLE_TOOL_BUTTON            (egg_toggle_tool_button_get_type ())
#define EGG_TOGGLE_TOOL_BUTTON(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EGG_TYPE_TOGGLE_TOOL_BUTTON, EggToggleToolButton))
#define EGG_TOGGLE_TOOL_BUTTON_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EGG_TYPE_TOGGLE_TOOL_BUTTON, EggToggleToolButtonClass))
#define EGG_IS_TOGGLE_TOOL_BUTTON(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EGG_TYPE_TOGGLE_TOOL_BUTTON))
#define EGG_IS_TOGGLE_TOOL_BUTTON_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EGG_TYPE_TOGGLE_TOOL_BUTTON))
#define EGG_TOGGLE_TOOL_BUTTON_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), EGG_TYPE_TOGGLE_TOOL_BUTTON, EggToggleToolButtonClass))

typedef struct _EggToggleToolButton      EggToggleToolButton;
typedef struct _EggToggleToolButtonClass EggToggleToolButtonClass;

struct _EggToggleToolButton
{
  EggToolButton parent;

  guint active : 1;
};

struct _EggToggleToolButtonClass
{
  EggToolButtonClass parent_class;

  void (* toggled) (EggToggleToolButton *self);
};

GType        egg_toggle_tool_button_get_type       (void);
EggToolItem *egg_toggle_tool_button_new_from_stock (const gchar *stock_id);

void         egg_toggle_tool_button_toggled        (EggToggleToolButton *self);
void         egg_toggle_tool_button_set_active     (EggToggleToolButton *self,
						    gboolean is_active);
gboolean     egg_toggle_tool_button_get_active     (EggToggleToolButton *self);

#endif
