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
  PROP_USE_UNDERLINE,
  PROP_STOCK_ID,
  PROP_ICON_SET,
  PROP_ICON_WIDGET,
};

static void egg_tool_button_init       (EggToolButton *self,
					EggToolButtonClass *class);
static void egg_tool_button_class_init (EggToolButtonClass *class);

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

static GObjectClass *parent_class = NULL;

static void egg_tool_button_set_property (GObject         *object,
					  guint            prop_id,
					  const GValue    *value,
					  GParamSpec      *pspec);
static void egg_tool_button_get_property (GObject         *object,
					  guint            prop_id,
					  GValue          *value,
					  GParamSpec      *pspec);
static void egg_tool_button_finalize     (GObject *object);

static void egg_tool_button_show_all (GtkWidget *widget);

static GtkWidget *egg_tool_button_create_menu_proxy (EggToolItem *item);
static void egg_tool_button_set_orientation (EggToolItem *tool_item, GtkOrientation orientation);
static void egg_tool_button_set_toolbar_style (EggToolItem *tool_item, GtkToolbarStyle style);
static void egg_tool_button_set_relief_style (EggToolItem *tool_item, GtkReliefStyle style);
static void button_clicked (GtkWidget *widget, EggToolButton *button);

static void
egg_tool_button_class_init (EggToolButtonClass *class)
{
  GObjectClass *object_class;
  GtkWidgetClass *widget_class;
  EggToolItemClass *tool_item_class;

  parent_class = g_type_class_peek_parent (class);
  object_class = (GObjectClass *)class;
  widget_class = (GtkWidgetClass *)class;
  tool_item_class = (EggToolItemClass *)class;

  object_class->set_property = egg_tool_button_set_property;
  object_class->get_property = egg_tool_button_get_property;
  object_class->finalize = egg_tool_button_finalize;

  widget_class->show_all = egg_tool_button_show_all;

  tool_item_class->create_menu_proxy = egg_tool_button_create_menu_proxy;
  tool_item_class->set_orientation = egg_tool_button_set_orientation;
  tool_item_class->set_toolbar_style = egg_tool_button_set_toolbar_style;
  tool_item_class->set_relief_style = egg_tool_button_set_relief_style;

  class->button_type = GTK_TYPE_BUTTON;

  g_object_class_install_property (object_class,
				   PROP_LABEL,
				   g_param_spec_string ("label",
							_("Label"),
							_("Text to show in the item."),
							NULL,
							G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
				   PROP_USE_UNDERLINE,
				   g_param_spec_boolean ("use_underline",
							 _("Use underline"),
							 _("Interpret underlines in the item label."),
							 FALSE,
							 G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
				   PROP_STOCK_ID,
				   g_param_spec_string ("stock_id",
							_("Stock Id"),
							_("The stock icon displayed on the item."),
							NULL,
							G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
				   PROP_ICON_SET,
				   g_param_spec_boxed ("icon_set",
						       _("Icon set"),
						       _("Icon set to use to draw the item's icon."),
						       GTK_TYPE_ICON_SET,
						       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
				   PROP_ICON_WIDGET,
				   g_param_spec_object ("icon_widget",
							_("Icon widget"),
							_("Icon widget to display in the item."),
							GTK_TYPE_WIDGET,
							G_PARAM_READWRITE));
}

static void
egg_tool_button_init (EggToolButton *self, EggToolButtonClass *class)
{
  EggToolItem *toolitem = EGG_TOOL_ITEM (self);

  toolitem->homogeneous = TRUE;

  /* create button */
  self->button = g_object_new (class->button_type, NULL);
  GTK_WIDGET_UNSET_FLAGS (self->button, GTK_CAN_FOCUS);
  g_signal_connect_object (self->button, "clicked",
			   G_CALLBACK (button_clicked), self, 0);

  self->box = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (self->button), self->box);
  gtk_widget_show (self->box);

#if 0
  self->icon = gtk_image_new();
  gtk_box_pack_start (GTK_BOX (self->box), self->icon, FALSE, FALSE, 0);
  gtk_widget_show (self->icon);
#endif

  self->label = gtk_label_new (NULL);
  gtk_label_set_use_underline (GTK_LABEL (self->label), TRUE);
  gtk_box_pack_end (GTK_BOX (self->box), self->label, TRUE, TRUE, 0);
  gtk_widget_show (self->label);

  gtk_container_add (GTK_CONTAINER (self), self->button);
  gtk_widget_show (self->button);
}

static void
egg_tool_button_set_property (GObject         *object,
			      guint            prop_id,
			      const GValue    *value,
			      GParamSpec      *pspec)
{
  EggToolButton *self = EGG_TOOL_BUTTON (object);
  const gchar *str;
  GtkStockItem stock_item;
  GtkIconSet *icon_set;
  GtkWidget *icon;

  switch (prop_id)
    {
    case PROP_LABEL:
      str = g_value_get_string (value);
      self->label_set = (str != NULL);
      if (str)
	gtk_label_set_label (GTK_LABEL (self->label), str);
      else if (self->stock_id)
	{
	  if (gtk_stock_lookup (self->stock_id, &stock_item))
	    {
	      gtk_label_set_label (GTK_LABEL (self->label), stock_item.label);
	    }
	}
      else
	gtk_label_set_label (GTK_LABEL (self->label), NULL);
      break;
    case PROP_USE_UNDERLINE:
      gtk_label_set_use_underline (GTK_LABEL (self->label),
				   g_value_get_boolean (value));
      break;
    case PROP_STOCK_ID:
      g_free (self->stock_id);
      self->stock_id = g_value_dup_string (value);
      if (!self->label_set)
	{
	  if (gtk_stock_lookup (self->stock_id, &stock_item))
	    gtk_label_set_label (GTK_LABEL (self->label), stock_item.label);
	}
      if (!self->icon_set)
	{
	  if (self->icon && !GTK_IS_IMAGE (self->icon))
	    {
	      gtk_container_remove (GTK_CONTAINER (self->box), self->icon);
	      self->icon = NULL;
	    }
	  if (!self->icon)
	    {
	      self->icon = gtk_image_new ();
	      gtk_box_pack_start (GTK_BOX (self->box), self->icon,
				  FALSE, FALSE, 0);
	    }
	  gtk_image_set_from_stock (GTK_IMAGE (self->icon), self->stock_id,
				    EGG_TOOL_ITEM (self)->icon_size);
	}
      break;
    case PROP_ICON_SET:
      if (self->icon && !GTK_IS_IMAGE (self->icon))
	{
	  gtk_container_remove (GTK_CONTAINER (self->box), self->icon);
	  self->icon = NULL;
	}
      if (!self->icon)
	{
	  self->icon = gtk_image_new ();
	  gtk_box_pack_start (GTK_BOX (self->box), self->icon,
			      FALSE, FALSE, 0);
	}
      icon_set = g_value_get_boxed (value);
      self->icon_set = (icon_set != NULL);
      if (!self->icon_set && self->stock_id)
	gtk_image_set_from_stock (GTK_IMAGE (self->icon), self->stock_id,
				  EGG_TOOL_ITEM (self)->icon_size);
      else
	gtk_image_set_from_icon_set (GTK_IMAGE (self->icon), icon_set,
				     EGG_TOOL_ITEM (self)->icon_size);
      break;
    case PROP_ICON_WIDGET:
      gtk_container_remove (GTK_CONTAINER (self->box), self->icon);
      self->icon = NULL;
      icon = g_value_get_object (value);
      self->icon_set = (icon != NULL);
      if (icon)
	{
	  self->icon = icon;
	  gtk_box_pack_start (GTK_BOX (self->box), self->icon,
			      FALSE, FALSE, 0);
	}
      else if (self->stock_id)
	{
	  self->icon = gtk_image_new_from_stock (self->stock_id,
					 EGG_TOOL_ITEM (self)->icon_size);
	  gtk_box_pack_start (GTK_BOX (self->box), self->icon,
			      FALSE, FALSE, 0);
	}
      break;
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
  EggToolButton *self = EGG_TOOL_BUTTON (object);

  switch (prop_id)
    {
    case PROP_LABEL:
      if (self->label_set)
	g_value_set_string (value,
			    gtk_label_get_label (GTK_LABEL (self->label)));
      break;
    case PROP_USE_UNDERLINE:
      g_value_set_boolean (value,
		gtk_label_get_use_underline (GTK_LABEL (self->label)));
      break;
    case PROP_STOCK_ID:
      g_value_set_string (value, self->stock_id);
      break;
    case PROP_ICON_SET:
      if (GTK_IS_IMAGE (self->icon) &&
	  GTK_IMAGE (self->icon)->storage_type == GTK_IMAGE_ICON_SET)
	{
	  GtkIconSet *icon_set;
	  gtk_image_get_icon_set (GTK_IMAGE (self->icon), &icon_set, NULL);
	  g_value_set_boxed (value, icon_set);
	}
      else
	g_value_set_boxed (value, NULL);
      break;
    case PROP_ICON_WIDGET:
      g_value_set_object (value, self->icon);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
egg_tool_button_finalize (GObject *object)
{
  EggToolButton *self = EGG_TOOL_BUTTON (object);

  g_free (self->stock_id);
  self->stock_id = NULL;

  parent_class->finalize (object);
}

static void
egg_tool_button_show_all (GtkWidget *widget)
{
  EggToolButton *self = EGG_TOOL_BUTTON (widget);

  switch (EGG_TOOL_ITEM (widget)->style)
    {
    case GTK_TOOLBAR_ICONS:
      if (self->icon) gtk_widget_show_all (self->icon);
      gtk_widget_hide (self->label);
      gtk_widget_show (self->box);
      gtk_widget_show (self->button);
      break;
    case GTK_TOOLBAR_TEXT:
      if (self->icon) gtk_widget_hide (self->icon);
      gtk_widget_show_all (self->label);
      gtk_widget_show (self->box);
      gtk_widget_show (self->button);
      break;
    case GTK_TOOLBAR_BOTH:
    case GTK_TOOLBAR_BOTH_HORIZ:
      gtk_widget_show_all (self->button);
    }

  gtk_widget_show (GTK_WIDGET (self));
}

static GtkWidget *
egg_tool_button_create_menu_proxy (EggToolItem *item)
{
  EggToolButton *self = EGG_TOOL_BUTTON (item);
  GtkWidget *menu_item;
  GtkWidget *image;
  const char *label;

  label = gtk_label_get_text (GTK_LABEL (self->label));

  menu_item = gtk_image_menu_item_new_with_label (label);

  if (GTK_IS_IMAGE (self->icon))
    {
      image = gtk_image_new ();

      if (GTK_IMAGE (self->icon)->storage_type == GTK_IMAGE_STOCK)
	{
	  gchar *stock_id;

	  gtk_image_get_stock (GTK_IMAGE (self->icon),
			       &stock_id, NULL);
	  gtk_image_set_from_stock (GTK_IMAGE (image), stock_id,
				    GTK_ICON_SIZE_MENU);
	}
      else if (GTK_IMAGE (self->icon)->storage_type == GTK_IMAGE_ICON_SET)
	{
	  GtkIconSet *icon_set;

	  gtk_image_get_icon_set (GTK_IMAGE (self->icon), &icon_set, NULL);
	  gtk_image_set_from_icon_set (GTK_IMAGE (image), icon_set,
				       GTK_ICON_SIZE_MENU);
	}
      else
	{
	  g_warning ("FIXME: Add more cases here");
	}
      gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), image);
    }

  g_signal_connect_object (menu_item, "activate",
			   G_CALLBACK (gtk_button_clicked),
			   EGG_TOOL_BUTTON (self)->button,
			   G_CONNECT_SWAPPED);

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
  EggToolButton *self = EGG_TOOL_BUTTON (tool_item);

  if (tool_item->style != style)
    {
      tool_item->style = style;

      switch (tool_item->style)
	{
	case GTK_TOOLBAR_ICONS:
	  gtk_widget_hide (self->label);
	  if (self->icon)
	    gtk_widget_show (self->icon);
	  break;
	case GTK_TOOLBAR_TEXT:
	  gtk_widget_show (self->label);
	  if (self->icon)
	    gtk_widget_hide (self->icon);
	  break;
	case GTK_TOOLBAR_BOTH:
	  if (GTK_IS_HBOX (self->box))
	    {
	      GtkWidget *vbox;

              vbox = gtk_vbox_new (FALSE, 0);
	      gtk_widget_show (vbox);

	      if (self->icon)
		{
		  g_object_ref (self->icon);
		  gtk_container_remove (GTK_CONTAINER (self->box), self->icon);
		  gtk_box_pack_start (GTK_BOX (vbox), self->icon,
				      FALSE, FALSE, 0);
		  g_object_unref (self->icon);
		}

	      g_object_ref (self->label);
	      gtk_container_remove (GTK_CONTAINER (self->box), self->label);
	      gtk_box_pack_start (GTK_BOX (vbox), self->label, TRUE, TRUE, 0);
	      g_object_unref (self->label);

	      gtk_container_remove (GTK_CONTAINER (self->button), self->box);
	      self->box = vbox;
	      gtk_container_add (GTK_CONTAINER (self->button), self->box);
	    }

	  gtk_widget_show (self->label);
	  if (self->icon)
	    gtk_widget_show (self->icon);
	  break;
	case GTK_TOOLBAR_BOTH_HORIZ:
	  if (GTK_IS_VBOX (self->box))
	    {
	      GtkWidget *hbox;

              hbox = gtk_hbox_new (FALSE, 0);
	      gtk_widget_show (hbox);

	      if (self->icon)
		{
		  g_object_ref (self->icon);
		  gtk_container_remove (GTK_CONTAINER (self->box), self->icon);
		  gtk_box_pack_start (GTK_BOX (hbox), self->icon, FALSE, FALSE, 0);
		  g_object_unref (self->icon);
		}

	      g_object_ref (self->label);
	      gtk_container_remove (GTK_CONTAINER (self->box), self->label);
	      gtk_box_pack_start (GTK_BOX (hbox), self->label, TRUE, TRUE, 0);
	      g_object_unref (self->label);

	      gtk_container_remove (GTK_CONTAINER (self->button), self->box);
	      self->box = hbox;
	      gtk_container_add (GTK_CONTAINER (self->button), self->box);
	    }

	  gtk_widget_show (self->label);
	  if (self->icon)
	    gtk_widget_show (self->icon);
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
button_clicked (GtkWidget *widget, EggToolButton *self)
{
  g_signal_emit_by_name (self, "clicked");
}

EggToolItem *
egg_tool_button_new_from_stock (const gchar *stock_id)
{
  EggToolButton *self;

  self = g_object_new (EGG_TYPE_TOOL_BUTTON,
		       "stock_id", stock_id,
		       "use_underline", TRUE,
		       NULL);

  return EGG_TOOL_ITEM (self);
}


