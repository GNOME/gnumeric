/* File import from libegg to gnumeric by import-egg.  Do not edit.  */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>

#include "eggtoggletoolbutton.h"

#ifndef _
#  define _(s) (s)
#endif

enum {
  TOGGLED,
  LAST_SIGNAL
};

static void egg_toggle_tool_button_init       (EggToggleToolButton *self);
static void egg_toggle_tool_button_class_init (EggToggleToolButtonClass*class);
static void button_toggled (GtkWidget *widget, EggToggleToolButton *button);

GType
egg_toggle_tool_button_get_type (void)
{
  static GType type = 0;

  if (!type)
    {
      static const GTypeInfo type_info =
	{
	  sizeof (EggToggleToolButtonClass),
	  (GBaseInitFunc) 0,
	  (GBaseFinalizeFunc) 0,
	  (GClassInitFunc) egg_toggle_tool_button_class_init,
	  (GClassFinalizeFunc) 0,
	  NULL,
	  sizeof (EggToggleToolButton),
	  0, /* n_preallocs */
	  (GInstanceInitFunc) egg_toggle_tool_button_init
	};

      type = g_type_register_static (EGG_TYPE_TOOL_BUTTON,
				     "EggToggleToolButton", &type_info, 0);
    }
  return type;
}

static GObjectClass *parent_class = NULL;
static guint         toggle_signals[LAST_SIGNAL] = { 0 };

static GtkWidget *egg_toggle_tool_button_create_menu_proxy (EggToolItem *self);

static void
egg_toggle_tool_button_class_init (EggToggleToolButtonClass *class)
{
  EggToolItemClass *toolitem_class;
  EggToolButtonClass *toolbutton_class;

  parent_class = g_type_class_peek_parent (class);
  toolitem_class = (EggToolItemClass *)class;
  toolbutton_class = (EggToolButtonClass *)class;

  toolitem_class->create_menu_proxy = egg_toggle_tool_button_create_menu_proxy;
  toolbutton_class->button_type = GTK_TYPE_TOGGLE_BUTTON;

  toggle_signals[TOGGLED] =
    g_signal_new ("toggled",
		  G_OBJECT_CLASS_TYPE (class),
		  G_SIGNAL_RUN_FIRST,
		  G_STRUCT_OFFSET (EggToggleToolButtonClass, toggled),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);
}

static void
egg_toggle_tool_button_init (EggToggleToolButton *self)
{
  g_signal_connect_object (EGG_TOOL_BUTTON (self)->button, "toggled",
			   G_CALLBACK (button_toggled), self, 0);
}

static GtkWidget *
egg_toggle_tool_button_create_menu_proxy (EggToolItem *item)
{
  EggToggleToolButton *self = EGG_TOGGLE_TOOL_BUTTON (item);
  GtkWidget *menu_item;
  const char *label;

  label = gtk_label_get_text (GTK_LABEL (EGG_TOOL_BUTTON (self)->label));

  menu_item = gtk_check_menu_item_new_with_mnemonic (label);
  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu_item),
				  self->active);
  g_signal_connect_object (menu_item, "activate",
			   G_CALLBACK (gtk_button_clicked),
			   EGG_TOOL_BUTTON (self)->button,
			   G_CONNECT_SWAPPED);

  return menu_item;
}

static void
button_toggled (GtkWidget *widget, EggToggleToolButton *self)
{
  gboolean toggle_active;

  toggle_active = GTK_TOGGLE_BUTTON (widget)->active;
  if (toggle_active != self->active)
    {
      self->active = toggle_active;
      g_signal_emit (G_OBJECT (self), toggle_signals[TOGGLED], 0);
    }
}


EggToolItem *
egg_toggle_tool_button_new_from_stock (const gchar *stock_id)
{
  EggToolButton *self;

  self = g_object_new (EGG_TYPE_TOGGLE_TOOL_BUTTON,
		       "stock_id", stock_id,
		       "use_underline", TRUE,
		       NULL);

  return EGG_TOOL_ITEM (self);
}

void
egg_toggle_tool_button_toggled (EggToggleToolButton *self)
{
  g_return_if_fail (EGG_IS_TOGGLE_TOOL_BUTTON (self));

  gtk_toggle_button_toggled (GTK_TOGGLE_BUTTON(EGG_TOOL_BUTTON(self)->button));
}

void
egg_toggle_tool_button_set_active (EggToggleToolButton *self,
				   gboolean is_active)
{
  g_return_if_fail (EGG_IS_TOGGLE_TOOL_BUTTON (self));

  is_active = is_active != FALSE;

  if (self->active != is_active)
    gtk_button_clicked (GTK_BUTTON (EGG_TOOL_BUTTON (self)->button));
}

gboolean
egg_toggle_tool_button_get_active (EggToggleToolButton *self)
{
  g_return_val_if_fail (EGG_IS_TOGGLE_TOOL_BUTTON (self), FALSE);

  return self->active;
}
