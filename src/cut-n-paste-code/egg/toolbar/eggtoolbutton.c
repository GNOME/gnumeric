/* File import from libegg to gnumeric by import-egg.  Do not edit.  */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>

#include "eggtoolbutton.h"

#ifndef _
#  define _(s) (s)
#endif

enum {
  PROP_0,
  PROP_LABEL,
};

static void egg_tool_button_init       (EggToolButton *toolbutton);
static void egg_tool_button_class_init (EggToolButtonClass *class);

static void egg_tool_button_set_property (GObject         *object,
					  guint            prop_id,
					  const GValue    *value,
					  GParamSpec      *pspec);
static void egg_tool_button_get_property (GObject         *object,
					  guint            prop_id,
					  GValue          *value,
					  GParamSpec      *pspec);

static void egg_tool_button_show_all (GtkWidget *widget);

static GtkWidget *egg_tool_button_create_menu_proxy (EggToolItem *item);
static void egg_tool_button_set_orientation (EggToolItem *tool_item, GtkOrientation orientation);
static void egg_tool_button_set_toolbar_style (EggToolItem *tool_item, GtkToolbarStyle style);
static void egg_tool_button_set_relief_style (EggToolItem *tool_item, GtkReliefStyle style);

GType
egg_tool_button_get_type (void)
{
  static GtkType type = 0;

  if (!type)
    {
      static const GTypeInfo type_info =
	{
	  sizeof (EggToolButtonClass),
	  (GBaseInitFunc) NULL,
	  (GBaseFinalizeFunc) NULL,
	  (GClassInitFunc) egg_tool_button_class_init,
	  (GClassFinalizeFunc) NULL,
	  NULL,
	  sizeof (EggToolButton),
	  0, /* n_preallocs */
	  (GInstanceInitFunc) egg_tool_button_init,
	};

      type = g_type_register_static (EGG_TYPE_TOOL_ITEM,
				     "EggToolButton",
				     &type_info, 0);
    }
  return type;
}

static void
egg_tool_button_class_init (EggToolButtonClass *klass)
{
  GObjectClass *object_class;
  GtkWidgetClass *widget_class;
  EggToolItemClass *tool_item_class;

  object_class = (GObjectClass *)klass;
  widget_class = (GtkWidgetClass *)klass;
  tool_item_class = (EggToolItemClass *)klass;

  object_class->set_property = egg_tool_button_set_property;
  object_class->get_property = egg_tool_button_get_property;

  widget_class->show_all = egg_tool_button_show_all;

  tool_item_class->create_menu_proxy = egg_tool_button_create_menu_proxy;
  tool_item_class->set_orientation = egg_tool_button_set_orientation;
  tool_item_class->set_toolbar_style = egg_tool_button_set_toolbar_style;
  tool_item_class->set_relief_style = egg_tool_button_set_relief_style;
}

static void
egg_tool_button_init (EggToolButton *toolbutton)
{
  EggToolItem *toolitem = EGG_TOOL_ITEM (toolbutton);

  toolitem->homogeneous = TRUE;
}

static void
egg_tool_button_set_property (GObject         *object,
			      guint            prop_id,
			      const GValue    *value,
			      GParamSpec      *pspec)
{
  EggToolButton *button = EGG_TOOL_BUTTON (object);

  switch (prop_id)
    {
    case PROP_LABEL:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
egg_tool_button_get_property (GObject         *object,
			      guint            prop_id,
			      GValue          *value,
			      GParamSpec      *pspec)
{
  EggToolButton *button = EGG_TOOL_BUTTON (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
egg_tool_button_show_all (GtkWidget *widget)
{
  EggToolButton *button = EGG_TOOL_BUTTON (widget);

  switch (EGG_TOOL_ITEM (widget)->style)
    {
    case GTK_TOOLBAR_ICONS:
      if (button->icon)
	gtk_widget_show_all (button->icon);
      gtk_widget_show (button->button);
      gtk_widget_show (GTK_BIN (button->button)->child);
      break;
    case GTK_TOOLBAR_TEXT:
      gtk_widget_show_all (button->label);
      gtk_widget_show (button->button);
      gtk_widget_show (GTK_BIN (button->button)->child);
      break;
    case GTK_TOOLBAR_BOTH:
    case GTK_TOOLBAR_BOTH_HORIZ:
      gtk_widget_show_all (button->button);
    }

  gtk_widget_show (button);
}

static GtkWidget *
egg_tool_button_create_menu_proxy (EggToolItem *item)
{
  EggToolButton *tool_button = EGG_TOOL_BUTTON (item);
  GtkWidget *menu_item;
  GtkWidget *image;
  const char *label;

  label = gtk_label_get_text (GTK_LABEL (tool_button->label));

  menu_item = gtk_image_menu_item_new_with_label (label);

  if (GTK_IS_IMAGE (tool_button->icon))
    {
      image = gtk_image_new ();

      if (GTK_IMAGE (tool_button->icon)->storage_type ==
	  GTK_IMAGE_STOCK)
	{
	  gchar *stock_id;

	  gtk_image_get_stock (GTK_IMAGE (tool_button->icon),
			       &stock_id, NULL);
	  gtk_image_set_from_stock (GTK_IMAGE (image), stock_id, GTK_ICON_SIZE_MENU);
	  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), image);
	}
      else
	{
	  g_warning ("FIXME: Add more cases here");
	}
    }
  return menu_item;
}

static void
egg_tool_button_set_orientation (EggToolItem   *tool_item,
				 GtkOrientation orientation)
{
  if (tool_item->orientation != orientation)
    {
      tool_item->orientation = orientation;
      g_print ("set orientation!!!: %d\n", orientation);
    }
}

static void
egg_tool_button_set_toolbar_style (EggToolItem    *tool_item,
				   GtkToolbarStyle style)
{
  EggToolButton *button = EGG_TOOL_BUTTON (tool_item);
  GtkWidget *child;

  if (tool_item->style != style)
    {
      tool_item->style = style;

      switch (tool_item->style)
	{
	case GTK_TOOLBAR_ICONS:
	  gtk_widget_hide (button->label);
	  if (button->icon)
	    gtk_widget_show (button->icon);
	  break;
	case GTK_TOOLBAR_TEXT:
	  gtk_widget_show (button->label);
	  if (button->icon)
	    gtk_widget_hide (button->icon);
	  break;
	case GTK_TOOLBAR_BOTH:
	  child = GTK_BIN (button->button)->child;

	  if (GTK_IS_HBOX (child))
	    {
	      GtkWidget *vbox;

              vbox = gtk_vbox_new (FALSE, 0);
	      gtk_widget_show (vbox);

	      if (button->icon)
		{
		  g_object_ref (button->icon);
		  gtk_container_remove (GTK_CONTAINER (child), button->icon);
		  gtk_box_pack_start (GTK_BOX (vbox), button->icon, FALSE, FALSE, 0);
		  g_object_unref (button->icon);
		}

	      g_object_ref (button->label);
	      gtk_container_remove (GTK_CONTAINER (child), button->label);
	      gtk_box_pack_start (GTK_BOX (vbox), button->label, TRUE, TRUE, 0);
	      g_object_unref (button->label);

	      gtk_container_remove (GTK_CONTAINER (button->button), child);
	      gtk_container_add (GTK_CONTAINER (button->button), vbox);
	    }

	  gtk_widget_show (button->label);
	  if (button->icon)
	    gtk_widget_show (button->icon);
	  break;
	case GTK_TOOLBAR_BOTH_HORIZ:
	  child = GTK_BIN (button->button)->child;

	  if (GTK_IS_VBOX (child))
	    {
	      GtkWidget *hbox;

              hbox = gtk_hbox_new (FALSE, 0);
	      gtk_widget_show (hbox);

	      if (button->icon)
		{
		  g_object_ref (button->icon);
		  gtk_container_remove (GTK_CONTAINER (child), button->icon);
		  gtk_box_pack_start (GTK_BOX (hbox), button->icon, FALSE, FALSE, 0);
		  g_object_unref (button->icon);
		}

	      g_object_ref (button->label);
	      gtk_container_remove (GTK_CONTAINER (child), button->label);
	      gtk_box_pack_start (GTK_BOX (hbox), button->label, TRUE, TRUE, 0);
	      g_object_unref (button->label);

	      gtk_container_remove (GTK_CONTAINER (button->button), child);
	      gtk_container_add (GTK_CONTAINER (button->button), hbox);
	    }

	  gtk_widget_show (button->label);
	  if (button->icon)
	    gtk_widget_show (button->icon);
	  break;
	}
    }
}

static void
egg_tool_button_set_relief_style (EggToolItem   *tool_item,
				  GtkReliefStyle style)
{
  gtk_button_set_relief (GTK_BUTTON (EGG_TOOL_BUTTON (tool_item)->button), style);
}


static void
button_clicked (GtkWidget *widget, EggToolButton *button)
{
  g_signal_emit_by_name (button, "clicked");
}

EggToolItem *
egg_tool_button_new_from_stock (const gchar *stock_id)
{
  EggToolButton *button;
  GtkStockItem stock_item;
  GtkWidget *vbox;

  button = g_object_new (EGG_TYPE_TOOL_BUTTON, NULL);

  if (gtk_stock_lookup (stock_id, &stock_item))
    {
      button->label = gtk_label_new_with_mnemonic (stock_item.label);
      button->icon = gtk_image_new_from_stock (stock_id,
						    EGG_TOOL_ITEM (button)->icon_size);
    }
  else
    {
      button->label = gtk_label_new_with_mnemonic (stock_id);
    }

  button->button = gtk_button_new ();
  g_signal_connect (button->button, "clicked",
		    G_CALLBACK (button_clicked), button);
  /*  gtk_button_set_relief (GTK_BUTTON (button->button), GTK_RELIEF_NONE);*/

  GTK_WIDGET_UNSET_FLAGS (button->button, GTK_CAN_FOCUS);

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (button->button), vbox);

  if (button->icon)
    gtk_box_pack_start (GTK_BOX (vbox), button->icon, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (vbox), button->label, TRUE, TRUE, 0);
  gtk_container_add (GTK_CONTAINER (button), button->button);

  gtk_widget_show_all (button->button);

  return EGG_TOOL_ITEM (button);
}


