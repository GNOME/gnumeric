/* File import from bonoboui to gnumeric by import-bonobo.  Do not edit.  */

/* go-dock-item.c
 *
 * Copyright (C) 1998 Ettore Perazzoli
 * Copyright (C) 1998 Elliot Lee
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 * All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/*
  @NOTATION@
*/

/*
 * NB. this may look like a GtkBin, but it contains
 * a GoDockItemGrip in addition to it's child,
 * stranger things have been done in the name of
 * bin-compat.
 */

#include <goffice/goffice-config.h>
#include <glib/gi18n.h>
#include <goffice/gui-utils/go-dock-item.h>
#include <goffice/gui-utils/go-dock-band.h>
#include <goffice/gui-utils/go-dock-item-grip.h>
#include <gnm-marshalers.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gtk/gtktoolbar.h>
#include <gtk/gtkwindow.h>

#include <glib/gi18n.h>

struct _GoDockItemPrivate {
	GtkWidget *grip;
};

enum {
  PROP_0,
  PROP_SHADOW,
  PROP_ORIENTATION,
  PROP_PREFERRED_WIDTH,
  PROP_PREFERRED_HEIGHT
};

#define DRAG_HANDLE_SIZE 10

enum {
  DOCK_DRAG_BEGIN,
  DOCK_DRAG_END,
  DOCK_DRAG_MOTION,
  DOCK_DETACH,
  ORIENTATION_CHANGED,
  LAST_SIGNAL
};

/* this function is not public, but should be exported */
void        go_dock_item_set_behavior   (GoDockItem         *dock_item,
                                             GoDockItemBehavior  behavior);


static guint     get_preferred_width   (GoDockItem *item);
static guint     get_preferred_height  (GoDockItem *item);

static void go_dock_item_set_property   (GObject            *object,
					     guint               param_id,
					     const GValue       *value,
					     GParamSpec         *pspec);
static void go_dock_item_get_property   (GObject            *object,
					     guint               param_id,
					     GValue             *value,
					     GParamSpec         *pspec);
static void go_dock_item_finalize       (GObject           *object);
static void go_dock_item_map            (GtkWidget         *widget);
static void go_dock_item_unmap          (GtkWidget         *widget);
static void go_dock_item_realize        (GtkWidget         *widget);
static void go_dock_item_unrealize      (GtkWidget         *widget);
static void go_dock_item_style_set      (GtkWidget         *widget,
					     GtkStyle          *previous_style);
static void go_dock_item_size_request   (GtkWidget         *widget,
					     GtkRequisition    *requisition);
static void go_dock_item_size_allocate  (GtkWidget         *widget,
					     GtkAllocation     *real_allocation);
static void go_dock_item_add            (GtkContainer      *container,
					     GtkWidget         *widget);
static void go_dock_item_remove         (GtkContainer      *container,
					     GtkWidget         *widget);
static void go_dock_item_forall         (GtkContainer     *container,
					     gboolean          include_internals,
					     GtkCallback       callback,
					     gpointer          callback_data);
static void go_dock_item_paint          (GtkWidget         *widget,
					     GdkEventExpose    *event);
static gint go_dock_item_expose         (GtkWidget         *widget,
					     GdkEventExpose    *event);
static gint go_dock_item_button_changed (GtkWidget         *widget,
					     GdkEventButton    *event);
static gint go_dock_item_motion         (GtkWidget         *widget,
					     GdkEventMotion    *event);
static gint go_dock_item_delete_event   (GtkWidget         *widget,
					     GdkEventAny       *event);

static guint        dock_item_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GoDockItem, go_dock_item, GTK_TYPE_BIN)

/* Helper functions.  */

static gboolean
check_guint_arg (GObject *object,
		 const gchar *name,
		 guint *value_return)
{
  GParamSpec *pspec;

  g_return_val_if_fail (object != NULL, FALSE);

  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object), name);
  if (pspec != NULL) {
    GValue value = { 0, };

    g_value_init (&value, G_TYPE_UINT);
    g_object_get_property (G_OBJECT (object), name, &value);
    *value_return = g_value_get_uint (&value);
    g_value_unset (&value);

    return TRUE;
  } else
    return FALSE;
}

static guint
get_preferred_width (GoDockItem *dock_item)
{
  GtkWidget *child;
  guint preferred_width;

  child = GTK_BIN (dock_item)->child;

  if (!child)
    return 0;

  if (! check_guint_arg (G_OBJECT (child), "preferred_width", &preferred_width))
    {
      GtkRequisition child_requisition;

      gtk_widget_get_child_requisition (child, &child_requisition);
      preferred_width = child_requisition.width;
    }

  if (dock_item->orientation == GTK_ORIENTATION_HORIZONTAL)
    preferred_width += GO_DOCK_ITEM_NOT_LOCKED (dock_item) ? DRAG_HANDLE_SIZE : 0;

  preferred_width += GTK_CONTAINER (dock_item)->border_width * 2;

  return preferred_width;
}

static guint
get_preferred_height (GoDockItem *dock_item)
{
  GtkWidget *child;
  guint preferred_height;

  child = GTK_BIN (dock_item)->child;

  if (!child)
    return 0;

  if (! check_guint_arg (G_OBJECT (child), "preferred_height", &preferred_height))
    {
      GtkRequisition child_requisition;

      gtk_widget_get_child_requisition (child, &child_requisition);
      preferred_height = child_requisition.height;
    }

  if (dock_item->orientation == GTK_ORIENTATION_VERTICAL)
    preferred_height += GO_DOCK_ITEM_NOT_LOCKED (dock_item) ? DRAG_HANDLE_SIZE : 0;

  preferred_height += GTK_CONTAINER (dock_item)->border_width * 2;

  return preferred_height;
}

static void
go_dock_item_class_init (GoDockItemClass *klass)
{
  GObjectClass *gobject_klass;
  GtkWidgetClass *widget_class;
  GtkContainerClass *container_class;

  gobject_klass = (GObjectClass *) klass;
  gobject_klass = (GObjectClass *) klass;
  widget_class = (GtkWidgetClass *) klass;
  container_class = (GtkContainerClass *) klass;

  gobject_klass->set_property = go_dock_item_set_property;
  gobject_klass->get_property = go_dock_item_get_property;

  g_object_class_install_property (
	  gobject_klass,
	  PROP_SHADOW,
	  g_param_spec_enum ("shadow",
			     _("Shadow type"),
			     _("Shadow type"),
			     GTK_TYPE_SHADOW_TYPE,
			     GTK_SHADOW_OUT,
			     (G_PARAM_READABLE |
			      G_PARAM_WRITABLE)));

  g_object_class_install_property (
	  gobject_klass,
	  PROP_ORIENTATION,
	  g_param_spec_enum ("orientation",
			     _("Orientation"),
			     _("Orientation"),
			     GTK_TYPE_ORIENTATION,
			     GTK_ORIENTATION_HORIZONTAL,
			     (G_PARAM_READABLE |
			      G_PARAM_WRITABLE)));

  g_object_class_install_property (
	  gobject_klass,
	  PROP_PREFERRED_WIDTH,
	  g_param_spec_uint ("preferred_width",
			     _("Preferred width"),
			     _("Preferred width"),
			     0, G_MAXINT, 0,
			     G_PARAM_READABLE));

  g_object_class_install_property (
	  gobject_klass,
	  PROP_PREFERRED_HEIGHT,
	  g_param_spec_uint ("preferred_height",
			     _("Preferred height"),
			     _("Preferred height"),
			     0, G_MAXINT, 0,
			     G_PARAM_READABLE));

  dock_item_signals[DOCK_DRAG_BEGIN] =
	  g_signal_new ("dock_drag_begin",
			G_TYPE_FROM_CLASS (gobject_klass),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (GoDockItemClass,
					 dock_drag_begin),
			NULL, NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);

  dock_item_signals[DOCK_DRAG_MOTION] =
	  g_signal_new ("dock_drag_motion",
			G_TYPE_FROM_CLASS (gobject_klass),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (GoDockItemClass, dock_drag_motion),
			NULL, NULL,
			gnm__VOID__INT_INT,
			G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

  dock_item_signals[DOCK_DRAG_END] =
	  g_signal_new ("dock_drag_end",
			G_TYPE_FROM_CLASS (gobject_klass),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (GoDockItemClass, dock_drag_end),
			NULL, NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);

  dock_item_signals[DOCK_DETACH] =
	  g_signal_new ("dock_detach",
			G_TYPE_FROM_CLASS (gobject_klass),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (GoDockItemClass, dock_detach),
			NULL, NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);

  dock_item_signals[ORIENTATION_CHANGED] =
	  g_signal_new ("orientation_changed",
			G_TYPE_FROM_CLASS (gobject_klass),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (GoDockItemClass, orientation_changed),
			NULL, NULL,
			g_cclosure_marshal_VOID__ENUM,
			G_TYPE_NONE, 1, GTK_TYPE_ORIENTATION);

  gobject_klass->finalize = go_dock_item_finalize;

  widget_class->map = go_dock_item_map;
  widget_class->unmap = go_dock_item_unmap;
  widget_class->realize = go_dock_item_realize;
  widget_class->unrealize = go_dock_item_unrealize;
  widget_class->style_set = go_dock_item_style_set;
  widget_class->size_request = go_dock_item_size_request;
  widget_class->size_allocate = go_dock_item_size_allocate;
  widget_class->expose_event = go_dock_item_expose;
  widget_class->button_press_event = go_dock_item_button_changed;
  widget_class->button_release_event = go_dock_item_button_changed;
  widget_class->motion_notify_event = go_dock_item_motion;
  widget_class->delete_event = go_dock_item_delete_event;

  container_class->add = go_dock_item_add;
  container_class->remove = go_dock_item_remove;
  container_class->forall = go_dock_item_forall;
}

static void
go_dock_item_init (GoDockItem *dock_item)
{
  GTK_WIDGET_UNSET_FLAGS (dock_item, GTK_NO_WINDOW);

  dock_item->_priv = g_new (GoDockItemPrivate, 1);

  dock_item->_priv->grip = go_dock_item_grip_new (dock_item);
  gtk_widget_set_parent (dock_item->_priv->grip, GTK_WIDGET (dock_item));
  gtk_widget_show (dock_item->_priv->grip);

  dock_item->bin_window = NULL;
  dock_item->float_window = NULL;
  dock_item->shadow_type = GTK_SHADOW_OUT;

  dock_item->orientation = GTK_ORIENTATION_HORIZONTAL;
  dock_item->behavior = GO_DOCK_ITEM_BEH_NORMAL;

  dock_item->float_window_mapped = FALSE;
  dock_item->is_floating = FALSE;
  dock_item->in_drag = FALSE;

  dock_item->dragoff_x = 0;
  dock_item->dragoff_y = 0;

  dock_item->float_x = 0;
  dock_item->float_y = 0;
}

static void
go_dock_item_set_property (GObject            *object,
			      guint               param_id,
			      const GValue       *value,
			      GParamSpec         *pspec)
{
  GoDockItem *dock_item;

  g_return_if_fail (object != NULL);
  g_return_if_fail (GO_IS_DOCK_ITEM (object));

  dock_item = GO_DOCK_ITEM (object);

  switch (param_id)
    {
    case PROP_SHADOW:
      go_dock_item_set_shadow_type (dock_item, g_value_get_enum (value));
      break;
    case PROP_ORIENTATION:
      go_dock_item_set_orientation (dock_item, g_value_get_enum (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}

static void
go_dock_item_get_property (GObject            *object,
			      guint               param_id,
			      GValue             *value,
			      GParamSpec         *pspec)
{
  GoDockItem *dock_item;

  g_return_if_fail (object != NULL);
  g_return_if_fail (GO_IS_DOCK_ITEM (object));

  dock_item = GO_DOCK_ITEM (object);

  switch (param_id)
    {
    case PROP_SHADOW:
      g_value_set_enum (value, go_dock_item_get_shadow_type (dock_item));
      break;
    case PROP_ORIENTATION:
      g_value_set_enum (value, go_dock_item_get_orientation (dock_item));
      break;
    case PROP_PREFERRED_HEIGHT:
      g_value_set_uint (value, get_preferred_height (dock_item));
      break;
    case PROP_PREFERRED_WIDTH:
      g_value_set_uint (value, get_preferred_width (dock_item));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}

static void
go_dock_item_finalize (GObject *object)
{
  GoDockItem *di;

  g_return_if_fail (object != NULL);
  g_return_if_fail (GO_IS_DOCK_ITEM (object));

  di = GO_DOCK_ITEM (object);

  g_free (di->name);
  di->name = NULL;

  g_free (di->_priv);
  di->_priv = NULL;

  G_OBJECT_CLASS (go_dock_item_parent_class)-> finalize (object);
}

static void
go_dock_item_map (GtkWidget *widget)
{
  GtkBin *bin;
  GoDockItem *di;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GO_IS_DOCK_ITEM (widget));

  GTK_WIDGET_SET_FLAGS (widget, GTK_MAPPED);

  bin = GTK_BIN (widget);
  di = GO_DOCK_ITEM (widget);

  gdk_window_show (di->bin_window);
  if (! di->is_floating)
    gdk_window_show (widget->window);

  if (di->is_floating && !di->float_window_mapped)
    go_dock_item_detach (di, di->float_x, di->float_y);

  if (bin->child
      && GTK_WIDGET_VISIBLE (bin->child)
      && !GTK_WIDGET_MAPPED (bin->child))
    gtk_widget_map (bin->child);

  if (di->_priv->grip
      && GTK_WIDGET_VISIBLE (di->_priv->grip)
      && !GTK_WIDGET_MAPPED (di->_priv->grip))
    gtk_widget_map (di->_priv->grip);
}

static void
go_dock_item_unmap (GtkWidget *widget)
{
  GoDockItem *di;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GO_IS_DOCK_ITEM (widget));

  GTK_WIDGET_UNSET_FLAGS (widget, GTK_MAPPED);

  di = GO_DOCK_ITEM (widget);

  gdk_window_hide (widget->window);
  if (di->float_window_mapped)
    {
      gdk_window_hide (di->float_window);
      di->float_window_mapped = FALSE;
    }

  if (di->_priv->grip)
    gtk_widget_unmap (di->_priv->grip);
}

static void
go_dock_item_realize (GtkWidget *widget)
{
  GdkWindowAttr attributes;
  GdkWindow *root_window;
  gint attributes_mask;
  GoDockItem *di;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GO_IS_DOCK_ITEM (widget));

  di = GO_DOCK_ITEM (widget);

  GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

  attributes.x = widget->allocation.x;
  attributes.y = widget->allocation.y;
  attributes.width = widget->allocation.width;
  attributes.height = widget->allocation.height;
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.visual = gtk_widget_get_visual (widget);
  attributes.colormap = gtk_widget_get_colormap (widget);
  attributes.event_mask = (gtk_widget_get_events (widget)
			   | GDK_EXPOSURE_MASK);
  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
  widget->window = gdk_window_new (gtk_widget_get_parent_window (widget), &attributes, attributes_mask);
  gdk_window_set_user_data (widget->window, widget);

  attributes.x = 0;
  attributes.y = 0;
  attributes.width = widget->allocation.width;
  attributes.height = widget->allocation.height;
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.event_mask |= (gtk_widget_get_events (widget) |
			    GDK_EXPOSURE_MASK |
			    GDK_BUTTON1_MOTION_MASK |
			    GDK_POINTER_MOTION_HINT_MASK |
			    GDK_BUTTON_PRESS_MASK |
			    GDK_BUTTON_RELEASE_MASK |
	                    GDK_KEY_PRESS_MASK);
  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
  di->bin_window = gdk_window_new (widget->window, &attributes, attributes_mask);
  gdk_window_set_user_data (di->bin_window, widget);

  if (GTK_BIN (di)->child)
    gtk_widget_set_parent_window (GTK_BIN (di)->child, di->bin_window);

  if (di->_priv->grip)
    gtk_widget_set_parent_window (di->_priv->grip, di->bin_window);

  root_window = gdk_screen_get_root_window
	  (gdk_drawable_get_screen (GDK_DRAWABLE (widget->window)));

  attributes.x = 0;
  attributes.y = 0;
  attributes.width = widget->requisition.width;
  attributes.height = widget->requisition.height;
  attributes.window_type = GDK_WINDOW_TOPLEVEL;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.visual = gtk_widget_get_visual (widget);
  attributes.colormap = gtk_widget_get_colormap (widget);
  attributes.event_mask = (gtk_widget_get_events (widget) |
			   GDK_KEY_PRESS_MASK |
			   GDK_ENTER_NOTIFY_MASK |
			   GDK_LEAVE_NOTIFY_MASK |
			   GDK_FOCUS_CHANGE_MASK |
			   GDK_STRUCTURE_MASK);
  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
  di->float_window = gdk_window_new (root_window, &attributes, attributes_mask);
  gdk_window_set_transient_for (di->float_window, gdk_window_get_toplevel (widget->window));
  gdk_window_set_user_data (di->float_window, widget);
  gdk_window_set_decorations (di->float_window, 0);

  widget->style = gtk_style_attach (widget->style, widget->window);
  gtk_style_set_background (widget->style, widget->window, GTK_WIDGET_STATE (di));
  gtk_style_set_background (widget->style, di->bin_window, GTK_WIDGET_STATE (di));
  gtk_style_set_background (widget->style, di->float_window, GTK_WIDGET_STATE (di));
  gdk_window_set_back_pixmap (widget->window, NULL, TRUE);

  if (di->is_floating)
    go_dock_item_detach (di, di->float_x, di->float_y);
}

static void
go_dock_item_unrealize (GtkWidget *widget)
{
  GoDockItem *di;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GO_IS_DOCK_ITEM (widget));

  di = GO_DOCK_ITEM (widget);

  gdk_window_set_user_data (di->bin_window, NULL);
  gdk_window_destroy (di->bin_window);
  di->bin_window = NULL;
  gdk_window_set_user_data (di->float_window, NULL);
  gdk_window_destroy (di->float_window);
  di->float_window = NULL;

  GTK_WIDGET_CLASS (go_dock_item_parent_class)->unrealize (widget);
}

static void
go_dock_item_style_set (GtkWidget *widget,
                           GtkStyle  *previous_style)
{
  GoDockItem *di;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GO_IS_DOCK_ITEM (widget));

  di = GO_DOCK_ITEM (widget);

  if (GTK_WIDGET_REALIZED (widget) &&
      !GTK_WIDGET_NO_WINDOW (widget))
    {
      gtk_style_set_background (widget->style, widget->window,
                                widget->state);
      gtk_style_set_background (widget->style, di->bin_window, widget->state);
      gtk_style_set_background (widget->style, di->float_window, widget->state);
      if (GTK_WIDGET_DRAWABLE (widget))
	gdk_window_clear (widget->window);
    }
}

static void
go_dock_item_size_request (GtkWidget      *widget,
                              GtkRequisition *requisition)
{
  GtkBin *bin;
  GoDockItem *dock_item;
  GtkRequisition child_requisition;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GO_IS_DOCK_ITEM (widget));
  g_return_if_fail (requisition != NULL);

  bin = GTK_BIN (widget);
  dock_item = GO_DOCK_ITEM (widget);

  /* If our child is not visible, we still request its size, since
     we won't have any useful hint for our size otherwise.  */
  if (bin->child != NULL)
    gtk_widget_size_request (bin->child, &child_requisition);
  else
    {
      child_requisition.width = 0;
      child_requisition.height = 0;
    }

  if (dock_item->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      requisition->width =
        GO_DOCK_ITEM_NOT_LOCKED (dock_item) ? DRAG_HANDLE_SIZE : 0;
      if (bin->child != NULL)
        {
          requisition->width += child_requisition.width;
          requisition->height = child_requisition.height;
        }
      else
        requisition->height = 0;
    }
  else
    {
      requisition->height =
        GO_DOCK_ITEM_NOT_LOCKED (dock_item) ? DRAG_HANDLE_SIZE : 0;
      if (bin->child != NULL)
        {
          requisition->width = child_requisition.width;
          requisition->height += child_requisition.height;
        }
      else
        requisition->width = 0;
    }

  requisition->width += GTK_CONTAINER (widget)->border_width * 2;
  requisition->height += GTK_CONTAINER (widget)->border_width * 2;
}

static void
go_dock_item_size_allocate (GtkWidget     *widget,
				GtkAllocation *allocation)
{
  GtkBin *bin;
  GoDockItem *di;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GO_IS_DOCK_ITEM (widget));
  g_return_if_fail (allocation != NULL);

  bin = GTK_BIN (widget);
  di = GO_DOCK_ITEM (widget);

  widget->allocation = *allocation;

  if (GTK_WIDGET_REALIZED (widget))
    gdk_window_move_resize (widget->window,
                            widget->allocation.x,
                            widget->allocation.y,
                            widget->allocation.width,
                            widget->allocation.height);

  if (bin->child && GTK_WIDGET_VISIBLE (bin->child))
    {
      GtkWidget *child;
      GtkAllocation child_allocation;
      int border_width;

      child = bin->child;
      border_width = GTK_CONTAINER (widget)->border_width;

      child_allocation.x = border_width;
      child_allocation.y = border_width;

      if (GO_DOCK_ITEM_NOT_LOCKED(di))
        {
          GtkAllocation grip_alloc = *allocation;

	  grip_alloc.x = grip_alloc.y = 0;

          if (di->orientation != GTK_ORIENTATION_HORIZONTAL) {
            grip_alloc.height = DRAG_HANDLE_SIZE;
            child_allocation.y += DRAG_HANDLE_SIZE;
          } else {
            grip_alloc.width = DRAG_HANDLE_SIZE;
	    if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_LTR) {
	      child_allocation.x += DRAG_HANDLE_SIZE;
	    } else {
	      GtkRequisition child_requisition;
	      gtk_widget_get_child_requisition (child, &child_requisition);
	      grip_alloc.x = child_requisition.width;
	    }
	  }

	  gtk_widget_size_allocate (di->_priv->grip, &grip_alloc);
        }

      if (di->is_floating)
	{
          GtkRequisition child_requisition;
	  guint float_width;
	  guint float_height;

	  gtk_widget_get_child_requisition (child, &child_requisition);

          child_allocation.width = child_requisition.width;
          child_allocation.height = child_requisition.height;

	  float_width = child_allocation.width + 2 * border_width;
	  float_height = child_allocation.height + 2 * border_width;

	  if (di->orientation == GTK_ORIENTATION_HORIZONTAL)
	    float_width += DRAG_HANDLE_SIZE;
	  else
	    float_height += DRAG_HANDLE_SIZE;

	  if (GTK_WIDGET_REALIZED (di))
	    {
	      gdk_window_resize (di->float_window,
				 float_width,
				 float_height);
	      gdk_window_move_resize (di->bin_window,
				      0,
				      0,
				      float_width,
				      float_height);
	    }
	}
      else
	{
	  child_allocation.width = MAX (1, (int) widget->allocation.width - 2 * border_width);
	  child_allocation.height = MAX (1, (int) widget->allocation.height - 2 * border_width);

          if (GO_DOCK_ITEM_NOT_LOCKED (di))
            {
              if (di->orientation == GTK_ORIENTATION_HORIZONTAL)
		child_allocation.width = MAX ((int) child_allocation.width - DRAG_HANDLE_SIZE, 1);
              else
		child_allocation.height = MAX ((int) child_allocation.height - DRAG_HANDLE_SIZE, 1);
            }

	  if (GTK_WIDGET_REALIZED (di))
	    gdk_window_move_resize (di->bin_window,
				    0,
				    0,
				    widget->allocation.width,
				    widget->allocation.height);
	}

      gtk_widget_size_allocate (bin->child, &child_allocation);
    }
}

static void
go_dock_item_paint (GtkWidget      *widget,
			GdkEventExpose *event)
{
  GtkBin *bin;
  GoDockItem *di;

  bin = GTK_BIN (widget);
  di = GO_DOCK_ITEM (widget);

  if (!event)
    gtk_paint_box(widget->style,
                  di->bin_window,
                  GTK_WIDGET_STATE (widget),
                  di->shadow_type,
                  NULL, widget,
                  "dockitem_bin",
                  0, 0, -1, -1);
  else
    gtk_paint_box(widget->style,
                  di->bin_window,
                  GTK_WIDGET_STATE (widget),
                  di->shadow_type,
                  &event->area, widget,
                  "dockitem_bin",
                  0, 0, -1, -1);

  if (GO_DOCK_ITEM_NOT_LOCKED (di))
      gtk_container_propagate_expose (
	      GTK_CONTAINER (di), di->_priv->grip, event);
}

static gint
go_dock_item_expose (GtkWidget      *widget,
			 GdkEventExpose *event)
{
  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (GO_IS_DOCK_ITEM (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  if (GTK_WIDGET_DRAWABLE (widget) && event->window != widget->window)
    {
      go_dock_item_paint (widget, event);

      if (GTK_WIDGET_CLASS (go_dock_item_parent_class)->expose_event)
	      return GTK_WIDGET_CLASS (go_dock_item_parent_class)->expose_event (widget, event);
    }

  return FALSE;
}

static void
go_dock_item_drag_end (GoDockItem *di)
{
  gdk_display_pointer_ungrab
	  (gtk_widget_get_display (GTK_WIDGET (di)),
	   GDK_CURRENT_TIME);

  di->in_drag = FALSE;

  g_signal_emit (di, dock_item_signals [DOCK_DRAG_END], 0);
}

static gint
go_dock_item_button_changed (GtkWidget      *widget,
                                GdkEventButton *event)
{
  GoDockItem *di;
  gboolean event_handled;

  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (GO_IS_DOCK_ITEM (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  di = GO_DOCK_ITEM (widget);

  if (event->window != di->bin_window)
    return FALSE;

  if (!GO_DOCK_ITEM_NOT_LOCKED(widget))
    return FALSE;

  event_handled = FALSE;

  if (event->button == 1 && event->type == GDK_BUTTON_PRESS)
    {
      GtkWidget *child;
      gboolean in_handle;

      child = GTK_BIN (di)->child;

      switch (di->orientation)
	{
	case GTK_ORIENTATION_HORIZONTAL:
	  if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_LTR)
	    in_handle = event->x < DRAG_HANDLE_SIZE;
	  else
	    in_handle = event->x > widget->allocation.width - DRAG_HANDLE_SIZE;
	  break;
	case GTK_ORIENTATION_VERTICAL:
	  in_handle = event->y < DRAG_HANDLE_SIZE;
	  break;
	default:
	  in_handle = FALSE;
	  break;
	}

      if (!child)
	{
	  in_handle = FALSE;
	  event_handled = TRUE;
	}

      if (in_handle)
	{
	  di->dragoff_x = event->x;
	  di->dragoff_y = event->y;

          go_dock_item_grab_pointer (di);

          g_signal_emit (widget, dock_item_signals[DOCK_DRAG_BEGIN], 0);

	  event_handled = TRUE;
	}
    }
  else if (event->type == GDK_BUTTON_RELEASE && di->in_drag)
    {
      go_dock_item_drag_end (di);
      event_handled = TRUE;
    }

  return event_handled;
}

static gint
go_dock_item_motion (GtkWidget      *widget,
			 GdkEventMotion *event)
{
  GoDockItem *di;
  GdkWindow *root_window;
  gint new_x, new_y;

  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (GO_IS_DOCK_ITEM (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  di = GO_DOCK_ITEM (widget);
  if (!di->in_drag)
    return FALSE;

  if (event->window != di->bin_window)
    return FALSE;

  root_window = gdk_screen_get_root_window
	  (gdk_drawable_get_screen (GDK_DRAWABLE (event->window)));

  gdk_window_get_pointer (root_window, &new_x, &new_y, NULL);

  new_x -= di->dragoff_x;
  new_y -= di->dragoff_y;

  g_signal_emit (widget, dock_item_signals[DOCK_DRAG_MOTION], 0,
		 new_x, new_y);

  return TRUE;
}

static void
go_dock_item_add (GtkContainer *container,
                     GtkWidget    *widget)
{
  GoDockItem *dock_item;
  GParamSpec *pspec;

  g_return_if_fail (GO_IS_DOCK_ITEM (container));
  g_return_if_fail (GTK_BIN (container)->child == NULL);
  g_return_if_fail (widget->parent == NULL);

  dock_item = GO_DOCK_ITEM (container);

  gtk_widget_set_parent_window (widget, dock_item->bin_window);
  GTK_CONTAINER_CLASS (go_dock_item_parent_class)->add (container, widget);

  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (widget),
					"orientation");
  if (pspec != NULL) {
    GValue value = { 0, };

    g_value_init (&value, GTK_TYPE_ORIENTATION);
    g_value_set_enum (&value, dock_item->orientation);
    g_object_set_property (G_OBJECT (widget), "orientation", &value);
    g_value_unset (&value);
  }
}

static void
go_dock_item_set_floating (GoDockItem *item, gboolean val)
{
  item->is_floating = val;

  /* If there is a child and it supports the 'is_floating' flag
   * set that too.
   */
  if (item->bin.child != NULL &&
      g_object_class_find_property (G_OBJECT_GET_CLASS (item->bin.child),
				    "is_floating") != NULL) {
    GValue value = { 0, };
    g_value_init (&value, G_TYPE_BOOLEAN);
    g_value_set_boolean (&value, val);
    g_object_set_property (G_OBJECT (item->bin.child), "is_floating", &value);
    g_value_unset (&value);
  }
}

static void
go_dock_item_remove (GtkContainer *container,
			 GtkWidget    *widget)
{
  GoDockItem *di;

  g_return_if_fail (GO_IS_DOCK_ITEM (container));

  di = GO_DOCK_ITEM (container);

  if (widget == di->_priv->grip) {
	  gboolean grip_was_visible;

	  grip_was_visible = GTK_WIDGET_VISIBLE (widget);

	  gtk_widget_unparent (widget);
	  di->_priv->grip = NULL;

	  if (grip_was_visible)
		  gtk_widget_queue_resize (GTK_WIDGET (di));
	  return;
  }

  g_return_if_fail (GTK_BIN (container)->child == widget);

  if (di->is_floating)
    {
      go_dock_item_set_floating (di, FALSE);
      if (GTK_WIDGET_REALIZED (di))
	{
	  gdk_window_hide (di->float_window);
	  gdk_window_reparent (di->bin_window, GTK_WIDGET (di)->window, 0, 0);
          gdk_window_show (widget->window);
	}
      di->float_window_mapped = FALSE;
    }
  if (di->in_drag)
    go_dock_item_drag_end (di);

  GTK_CONTAINER_CLASS (go_dock_item_parent_class)->remove (container, widget);
}

static void
go_dock_item_forall (GtkContainer *container,
			 gboolean      include_internals,
			 GtkCallback   callback,
			 gpointer      callback_data)
{
  GtkBin *bin = (GtkBin *) container;
  GoDockItem *di = (GoDockItem *) container;

  g_return_if_fail (callback != NULL);

  if (di->_priv->grip)
    callback (di->_priv->grip, callback_data);

  if (bin->child)
    callback (bin->child, callback_data);
}

static gint
go_dock_item_delete_event (GtkWidget *widget,
                              GdkEventAny  *event)
{
  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (GO_IS_DOCK_ITEM (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  return TRUE;
}

/**
 * go_dock_item_construct:
 * @new: a #GoDockItem.
 * @name: Name for the new item
 * @behavior: Behavior for the new item
 *
 * Description: Constructs the @new GoDockItem named @name, with the
 * specified @behavior.
 *
 * Returns: A new GoDockItem widget.
 **/
void
go_dock_item_construct (GoDockItem *new,
			   const gchar *name,
			   GoDockItemBehavior behavior)
{
  g_return_if_fail (new != NULL);
  g_return_if_fail (GO_IS_DOCK_ITEM (new));

  new->name = g_strdup (name);
  new->behavior = behavior;

  if (behavior & GO_DOCK_ITEM_BEH_LOCKED)
    {
      gtk_widget_hide (new->_priv->grip);
      GTK_WIDGET_UNSET_FLAGS (new->_priv->grip, GTK_CAN_FOCUS);
    }
}

/**
 * go_dock_item_new:
 * @name: Name for the new item
 * @behavior: Behavior for the new item
 *
 * Description: Create a new GoDockItem named @name, with the
 * specified @behavior.
 *
 * Returns: A new GoDockItem widget.
 **/
GtkWidget *
go_dock_item_new (const gchar *name,
		      GoDockItemBehavior behavior)
{
  GoDockItem *new;

  new = GO_DOCK_ITEM (g_object_new (go_dock_item_get_type (), NULL));

  go_dock_item_construct (new, name, behavior);

  return GTK_WIDGET (new);
}

/**
 * go_dock_item_get_child:
 * @item: A GoDockItem widget
 *
 * Description: Retrieve the child of @item.
 *
 * Returns: The child of @item.
 **/
GtkWidget *
go_dock_item_get_child (GoDockItem *item)
{
  return GTK_BIN (item)->child;
}

/**
 * go_dock_item_get_name:
 * @item: A GoDockItem widget.
 *
 * Description: Retrieve the name of @item.
 *
 * Return value: The name of @item as a malloc()ed zero-terminated
 * string.
 **/
gchar *
go_dock_item_get_name (GoDockItem *item)
{
  return g_strdup (item->name);
}

/**
 * go_dock_item_set_shadow_type:
 * @dock_item: A GoDockItem widget
 * @type: The shadow type for @dock_item
 *
 * Description: Set the shadow type for @dock_item.
 **/
void
go_dock_item_set_shadow_type (GoDockItem *dock_item,
				  GtkShadowType   type)
{
  g_return_if_fail (GO_IS_DOCK_ITEM (dock_item));

  if (dock_item->shadow_type != type)
    {
      dock_item->shadow_type = type;

      if (GTK_WIDGET_DRAWABLE (dock_item))
        gtk_widget_queue_draw (GTK_WIDGET (dock_item));
      gtk_widget_queue_resize (GTK_WIDGET (dock_item));
    }
}

/**
 * go_dock_item_get_shadow_type:
 * @dock_item: A GoDockItem widget.
 *
 * Description: Retrieve the shadow type of @dock_item.
 *
 * Returns: @dock_item's shadow type.
 **/
GtkShadowType
go_dock_item_get_shadow_type (GoDockItem  *dock_item)
{
  g_return_val_if_fail (dock_item != NULL, GTK_SHADOW_OUT);
  g_return_val_if_fail (GO_IS_DOCK_ITEM (dock_item), GTK_SHADOW_OUT);

  return dock_item->shadow_type;
}

/**
 * go_dock_item_set_orientation:
 * @dock_item: A GoDockItem widget
 * @orientation: New orientation for @dock_item
 *
 * Description: Set the orientation for @dock_item.
 *
 * Returns: %TRUE if the operation succeeds, %FALSE if it fails.
 **/
gboolean
go_dock_item_set_orientation (GoDockItem *dock_item,
				  GtkOrientation  orientation)
{
  g_return_val_if_fail (dock_item != NULL, FALSE);
  g_return_val_if_fail (GO_IS_DOCK_ITEM (dock_item), FALSE);

  if (dock_item->orientation != orientation)
    {
      if ((orientation == GTK_ORIENTATION_VERTICAL
           && (dock_item->behavior & GO_DOCK_ITEM_BEH_NEVER_VERTICAL))
          || (orientation == GTK_ORIENTATION_HORIZONTAL
              && (dock_item->behavior & GO_DOCK_ITEM_BEH_NEVER_HORIZONTAL)))
        return FALSE;

      dock_item->orientation = orientation;

      if (dock_item->bin.child != NULL) {
	GValue value = { 0, };

	g_value_init (&value, GTK_TYPE_ORIENTATION);
	g_value_set_enum (&value, orientation);
	g_object_set_property (G_OBJECT (dock_item->bin.child),
			       "orientation", &value);
	g_value_unset (&value);
      }
      if (GTK_WIDGET_DRAWABLE (dock_item))
        gtk_widget_queue_draw (GTK_WIDGET (dock_item));
      gtk_widget_queue_resize (GTK_WIDGET (dock_item));

      g_signal_emit (dock_item, dock_item_signals[ORIENTATION_CHANGED], 0, orientation);
    }

  return TRUE;
}

/**
 * go_dock_item_get_orientation:
 * @dock_item: A GoDockItem widget.
 *
 * Description: Retrieve the orientation of @dock_item.
 *
 * Returns: The current orientation of @dock_item.
 **/
GtkOrientation
go_dock_item_get_orientation (GoDockItem *dock_item)
{
  g_return_val_if_fail (GO_IS_DOCK_ITEM (dock_item),
                        GTK_ORIENTATION_HORIZONTAL);

  return dock_item->orientation;
}

/**
 * go_dock_item_set_behavior:
 * @dock_item: A GoDockItem widget.
 * @behavior: New behavior for @dock_item
 *
 * Description: Set the behavior for @dock_item.
 */
void
go_dock_item_set_behavior (GoDockItem         *dock_item,
                               GoDockItemBehavior  behavior)
{
  g_return_if_fail (GO_IS_DOCK_ITEM (dock_item));

  if (dock_item->behavior == behavior)
    return;

  dock_item->behavior = behavior;

  if (behavior & GO_DOCK_ITEM_BEH_LOCKED)
    go_dock_item_set_locked (dock_item, TRUE);

  if (behavior & GO_DOCK_ITEM_BEH_NEVER_FLOATING &&
      dock_item->is_floating)
    go_dock_item_unfloat (dock_item);

  if (behavior & GO_DOCK_ITEM_BEH_NEVER_VERTICAL &&
      dock_item->orientation == GTK_ORIENTATION_VERTICAL)
    go_dock_item_set_orientation (dock_item, GTK_ORIENTATION_HORIZONTAL);

  if (behavior & GO_DOCK_ITEM_BEH_NEVER_HORIZONTAL &&
      dock_item->orientation == GTK_ORIENTATION_HORIZONTAL)
    go_dock_item_set_orientation (dock_item, GTK_ORIENTATION_VERTICAL);

  gtk_widget_queue_resize (GTK_WIDGET (dock_item));
}

/**
 * go_dock_item_get_behavior:
 * @dock_item: A GoDockItem widget.
 *
 * Description: Retrieve the behavior of @dock_item.
 *
 * Returns: The behavior of @dock_item.
 **/
GoDockItemBehavior
go_dock_item_get_behavior (GoDockItem *dock_item)
{
  g_return_val_if_fail (GO_IS_DOCK_ITEM (dock_item),
                        GO_DOCK_ITEM_BEH_NORMAL);

  return dock_item->behavior;
}

/* Private interface.  */

void
go_dock_item_set_locked (GoDockItem *dock_item,
			     gboolean        locked)
{
  g_return_if_fail (GO_IS_DOCK_ITEM (dock_item));

  if (locked)
    {
      if (!GO_DOCK_ITEM_NOT_LOCKED (dock_item))
        return;

      dock_item->behavior |= GO_DOCK_ITEM_BEH_LOCKED;
      gtk_widget_hide (dock_item->_priv->grip);
    }
  else
    {
      if (GO_DOCK_ITEM_NOT_LOCKED (dock_item))
        return;

      dock_item->behavior &= ~GO_DOCK_ITEM_BEH_LOCKED;
      gtk_widget_show (dock_item->_priv->grip);
    }
}

void
go_dock_item_grab_pointer (GoDockItem *item)
{
  GdkCursor *fleur;

  g_assert (GO_IS_DOCK_ITEM (item));

  item->in_drag = TRUE;

  fleur = gdk_cursor_new_for_display
	  (gtk_widget_get_display (GTK_WIDGET (item)),
	   GDK_FLEUR);

  /* Hm, not sure this is the right thing to do, but it seems to work.  */
  while (gdk_pointer_grab (item->bin_window,
                           FALSE,
                           (GDK_BUTTON1_MOTION_MASK |
                            GDK_POINTER_MOTION_HINT_MASK |
                            GDK_BUTTON_RELEASE_MASK),
                           NULL,
                           fleur,
                           GDK_CURRENT_TIME) != 0);

  gdk_cursor_unref (fleur);
}

gboolean
go_dock_item_detach (GoDockItem *item, gint x, gint y)
{
  GtkAllocation allocation;
  GtkRequisition requisition;

  if (item->behavior & GO_DOCK_ITEM_BEH_NEVER_FLOATING)
    return FALSE;

  item->float_x = x;
  item->float_y = y;

  go_dock_item_set_floating (item, TRUE);

  if (! GTK_WIDGET_REALIZED (item))
    return TRUE;

  gtk_widget_size_request (GTK_WIDGET (item), &requisition);

  gdk_window_move_resize (item->float_window,
                          x, y, requisition.width, requisition.height);

  gdk_window_reparent (item->bin_window, item->float_window, 0, 0);

/*  gdk_window_set_hints (item->float_window, x, y, 0, 0, 0, 0, GDK_HINT_POS); */

  gdk_window_show (item->float_window);
  item->float_window_mapped = TRUE;

  allocation.x = allocation.y = 0;
  allocation.width = requisition.width;
  allocation.height = requisition.height;
  gtk_widget_size_allocate (GTK_WIDGET (item), &allocation);

  gdk_window_hide (GTK_WIDGET (item)->window);
  gtk_widget_queue_draw (GTK_WIDGET (item));

  gdk_window_set_transient_for(item->float_window,
                               gdk_window_get_toplevel(GTK_WIDGET (item)->window));

  return TRUE;
}

void
go_dock_item_unfloat (GoDockItem *item)
{
  gdk_window_move_resize (GTK_WIDGET (item)->window, -1, -1, 0, 0);

  gdk_window_hide (item->float_window);

  gdk_window_reparent (item->bin_window, GTK_WIDGET (item)->window, 0, 0);
  gdk_window_show (GTK_WIDGET (item)->window);

  item->float_window_mapped = FALSE;
  go_dock_item_set_floating (item, FALSE);

  gtk_widget_queue_resize (GTK_WIDGET (item));
}

void
go_dock_item_attach (GoDockItem *item,
			 GtkWidget *parent,
			 gint x, gint y)
{
  if (GTK_WIDGET (item)->parent != GTK_WIDGET (parent))
    {
      gdk_window_move_resize (GTK_WIDGET (item)->window, -1, -1, 0, 0);
      gtk_widget_reparent (GTK_WIDGET (item), parent);
      go_dock_item_unfloat (item);
      go_dock_item_grab_pointer (item);
    }
}

void
go_dock_item_drag_floating (GoDockItem *item, gint x, gint y)
{
  if (item->is_floating)
    {
      gdk_window_move (item->float_window, x, y);
      gdk_window_raise (item->float_window);

      item->float_x = x;
      item->float_y = y;
    }
}

void
go_dock_item_handle_size_request (GoDockItem *item,
                                     GtkRequisition *requisition)
{
  GtkBin *bin;
  GtkContainer *container;

  bin = GTK_BIN (item);
  container = GTK_CONTAINER (item);

  if (bin->child != NULL)
    gtk_widget_size_request (bin->child, requisition);

  if (item->orientation == GTK_ORIENTATION_HORIZONTAL)
    requisition->width += DRAG_HANDLE_SIZE;
  else
    requisition->height += DRAG_HANDLE_SIZE;

  requisition->width += container->border_width * 2;
  requisition->height += container->border_width * 2;
}

void
go_dock_item_get_floating_position (GoDockItem *item,
					gint *x, gint *y)
{
  if (GTK_WIDGET_REALIZED (item) && item->is_floating)
    gdk_window_get_position (item->float_window, x, y);
  else
    {
      *x = item->float_x;
      *y = item->float_y;
    }
}

GtkWidget *
go_dock_item_get_grip (GoDockItem *item)
{
  g_return_val_if_fail (GO_IS_DOCK_ITEM (item), NULL);

  if (item->behavior & GO_DOCK_ITEM_BEH_LOCKED)
    return NULL;
  else
    return item->_priv->grip;
}

