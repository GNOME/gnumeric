/* File import from libegg to gnumeric by import-egg.  Do not edit.  */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>

#include "eggtoolbar.h"

#define DEFAULT_IPADDING 0
#define DEFAULT_SPACE_SIZE  5
#define DEFAULT_SPACE_STYLE GTK_TOOLBAR_SPACE_LINE

#define SPACE_LINE_DIVISION 10
#define SPACE_LINE_START    3
#define SPACE_LINE_END      7

#define TOOLBAR_ITEM_VISIBLE(item) \
(GTK_WIDGET_VISIBLE (item) && ((toolbar->orientation == GTK_ORIENTATION_HORIZONTAL && item->visible_horizontal) || \
 (toolbar->orientation == GTK_ORIENTATION_VERTICAL && item->visible_vertical)))

#ifndef _
#  define _(s) (s)
#endif

enum {
  ORIENTATION_CHANGED,
  STYLE_CHANGED,
  LAST_SIGNAL
};

enum {
  PROP_0,
  PROP_ORIENTATION,
  PROP_TOOLBAR_STYLE,
  PROP_SHOW_ARROW
};

static void egg_toolbar_init       (EggToolbar *toolbar);
static void egg_toolbar_class_init (EggToolbarClass *klass);

static void egg_toolbar_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void egg_toolbar_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gint egg_toolbar_expose (GtkWidget *widget, GdkEventExpose *event);
static void egg_toolbar_size_request (GtkWidget *widget, GtkRequisition *requisition);
static void egg_toolbar_size_allocate (GtkWidget *widget, GtkAllocation *allocation);
static void egg_toolbar_style_set (GtkWidget *widget, GtkStyle *prev_style);

static gboolean egg_toolbar_focus (GtkWidget *widget, GtkDirectionType dir);

static void egg_toolbar_add (GtkContainer *container, GtkWidget *widget);
static void egg_toolbar_remove (GtkContainer *container, GtkWidget *widget);
static void egg_toolbar_forall (GtkContainer *container, gboolean include_internals, GtkCallback callback, gpointer callback_data);
static GType egg_toolbar_child_type (GtkContainer *container);

static void egg_toolbar_real_orientation_changed (EggToolbar *toolbar, GtkOrientation orientation);
static void egg_toolbar_real_style_changed (EggToolbar *toolbar, GtkToolbarStyle style);

static void egg_toolbar_button_press (GtkWidget *button, GdkEventButton *event, EggToolbar *toolbar);
static void egg_toolbar_update_button_relief (EggToolbar *toolbar);

static GtkReliefStyle get_button_relief (EggToolbar *toolbar);
static gint get_space_size (EggToolbar *toolbar);
static GtkToolbarSpaceStyle get_space_style (EggToolbar *toolbar);

static GtkContainerClass *parent_class = NULL;
static guint toolbar_signals [LAST_SIGNAL] = { 0 };

GType
egg_toolbar_get_type (void)
{
  static GtkType type = 0;

  if (!type)
    {
      static const GTypeInfo type_info =
	{
	  sizeof (EggToolbarClass),
	  (GBaseInitFunc) NULL,
	  (GBaseFinalizeFunc) NULL,
	  (GClassInitFunc) egg_toolbar_class_init,
	  (GClassFinalizeFunc) NULL,
	  NULL,
	  sizeof (EggToolbar),
	  0, /* n_preallocs */
	  (GInstanceInitFunc) egg_toolbar_init,
	};

      type = g_type_register_static (GTK_TYPE_CONTAINER,
				     "EggToolbar",
				     &type_info, 0);
    }

  return type;
}

static void
egg_toolbar_class_init (EggToolbarClass *klass)
{
  GObjectClass *gobject_class;
  GtkWidgetClass *widget_class;
  GtkContainerClass *container_class;

  gobject_class = (GObjectClass *)klass;
  widget_class = (GtkWidgetClass *)klass;
  container_class = (GtkContainerClass *)klass;

  gobject_class->set_property = egg_toolbar_set_property;
  gobject_class->get_property = egg_toolbar_get_property;

  widget_class->expose_event = egg_toolbar_expose;
  widget_class->size_request = egg_toolbar_size_request;
  widget_class->size_allocate = egg_toolbar_size_allocate;
  widget_class->style_set = egg_toolbar_style_set;
  widget_class->focus = egg_toolbar_focus;

  container_class->add    = egg_toolbar_add;
  container_class->remove = egg_toolbar_remove;
  container_class->forall = egg_toolbar_forall;
  container_class->child_type = egg_toolbar_child_type;

  klass->orientation_changed = egg_toolbar_real_orientation_changed;
  klass->style_changed = egg_toolbar_real_style_changed;

  g_object_class_install_property (gobject_class,
				   PROP_ORIENTATION,
				   g_param_spec_enum ("orientation",
 						      _("Orientation"),
 						      _("The orientation of the toolbar"),
 						      GTK_TYPE_ORIENTATION,
 						      GTK_ORIENTATION_HORIZONTAL,
 						      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
				   PROP_TOOLBAR_STYLE,
				   g_param_spec_enum ("toolbar_style",
 						      _("Toolbar Style"),
 						      _("How to draw the toolbar"),
 						      GTK_TYPE_TOOLBAR_STYLE,
 						      GTK_TOOLBAR_ICONS,
 						      G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class,
				   PROP_SHOW_ARROW,
				   g_param_spec_boolean ("show_arrow",
							 _("Show Arrow"),
							 _("If an arrow should be shown if the toolbar doesn't fit"),
							 FALSE,
							 G_PARAM_READWRITE));

  gtk_widget_class_install_style_property (widget_class,
					   g_param_spec_int ("space_size",
							     _("Spacer size"),
							     _("Size of spacers"),
							     0,
							     G_MAXINT,
                                                             DEFAULT_SPACE_SIZE,
							     G_PARAM_READABLE));

  gtk_widget_class_install_style_property (widget_class,
					   g_param_spec_int ("internal_padding",
							     _("Internal padding"),
							     _("Amount of border space between the toolbar shadow and the buttons"),
							     0,
							     G_MAXINT,
                                                             DEFAULT_IPADDING,
                                                             G_PARAM_READABLE));

  gtk_widget_class_install_style_property (widget_class,
					   g_param_spec_enum ("space_style",
							     _("Space style"),
							     _("Whether spacers are vertical lines or just blank"),
                                                              GTK_TYPE_TOOLBAR_SPACE_STYLE,
                                                              DEFAULT_SPACE_STYLE,
                                                              G_PARAM_READABLE));

  gtk_widget_class_install_style_property (widget_class,
					   g_param_spec_enum ("button_relief",
							      _("Button relief"),
							      _("Type of bevel around toolbar buttons"),
                                                              GTK_TYPE_RELIEF_STYLE,
                                                              GTK_RELIEF_NONE,
                                                              G_PARAM_READABLE));
  gtk_widget_class_install_style_property (widget_class,
                                           g_param_spec_enum ("shadow_type",
                                                              _("Shadow type"),
                                                              _("Style of bevel around the toolbar"),
                                                              GTK_TYPE_SHADOW_TYPE,
                                                              GTK_SHADOW_OUT,
                                                              G_PARAM_READABLE));

  toolbar_signals[ORIENTATION_CHANGED] =
    g_signal_new ("orientation_changed",
		  G_OBJECT_CLASS_TYPE (klass),
		  G_SIGNAL_RUN_FIRST,
		  G_STRUCT_OFFSET (EggToolbarClass, orientation_changed),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__ENUM,
		  G_TYPE_NONE, 1,
		  GTK_TYPE_ORIENTATION);
  toolbar_signals[STYLE_CHANGED] =
    g_signal_new ("style_changed",
		  G_OBJECT_CLASS_TYPE (klass),
		  G_SIGNAL_RUN_FIRST,
		  G_STRUCT_OFFSET (EggToolbarClass, style_changed),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__ENUM,
		  G_TYPE_NONE, 1,
		  GTK_TYPE_TOOLBAR_STYLE);
}

static void
egg_toolbar_init (EggToolbar *toolbar)
{
  GTK_WIDGET_SET_FLAGS (toolbar, GTK_NO_WINDOW);
  GTK_WIDGET_UNSET_FLAGS (toolbar, GTK_CAN_FOCUS);

  toolbar->orientation = GTK_ORIENTATION_HORIZONTAL;
  toolbar->style = GTK_TOOLBAR_ICONS;
  toolbar->button = gtk_toggle_button_new ();
  g_signal_connect (toolbar->button, "button_press_event",
		    G_CALLBACK (egg_toolbar_button_press), toolbar);

  gtk_button_set_relief (GTK_BUTTON (toolbar->button), get_button_relief (toolbar));
  GTK_WIDGET_UNSET_FLAGS (toolbar->button, GTK_CAN_FOCUS);
  toolbar->arrow = gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_NONE);
  gtk_widget_show (toolbar->arrow);
  gtk_container_add (GTK_CONTAINER (toolbar->button), toolbar->arrow);

  gtk_widget_set_parent (toolbar->button, GTK_WIDGET (toolbar));
}

static void
egg_toolbar_set_property (GObject     *object,
			  guint        prop_id,
			  const GValue *value,
			  GParamSpec   *pspec)
{
  EggToolbar *toolbar = EGG_TOOLBAR (object);

  switch (prop_id)
    {
    case PROP_ORIENTATION:
      egg_toolbar_set_orientation (toolbar, g_value_get_enum (value));
      break;
    case PROP_TOOLBAR_STYLE:
      egg_toolbar_set_style (toolbar, g_value_get_enum (value));
      break;
    case PROP_SHOW_ARROW:
      egg_toolbar_set_show_arrow (toolbar, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
egg_toolbar_get_property (GObject    *object,
			  guint       prop_id,
			  GValue     *value,
			  GParamSpec *pspec)
{
  EggToolbar *toolbar = EGG_TOOLBAR (object);

  switch (prop_id)
    {
    case PROP_ORIENTATION:
      g_value_set_enum (value, toolbar->orientation);
      break;
    case PROP_TOOLBAR_STYLE:
      g_value_set_enum (value, toolbar->style);
      break;
    case PROP_SHOW_ARROW:
      g_value_set_boolean (value, toolbar->show_arrow);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
egg_toolbar_paint_space_line (GtkWidget    *widget,
			      GdkRectangle *area,
			      EggToolItem  *item)
{
  EggToolbar *toolbar;
  GtkAllocation *allocation;
  gint space_size;

  g_return_if_fail (GTK_BIN (item)->child == NULL);

  toolbar = EGG_TOOLBAR (widget);

  allocation = &GTK_WIDGET (item)->allocation;
  space_size = get_space_size (toolbar);

  if (toolbar->orientation == GTK_ORIENTATION_HORIZONTAL)
    gtk_paint_vline (widget->style, widget->window,
		     GTK_WIDGET_STATE (widget), area, widget,
		     "toolbar",
		     allocation->y +  allocation->height *
		     SPACE_LINE_START / SPACE_LINE_DIVISION,
		     allocation->y + allocation->height *
		     SPACE_LINE_END / SPACE_LINE_DIVISION,
		     allocation->x + (space_size-widget->style->xthickness)/2);
  else if (toolbar->orientation == GTK_ORIENTATION_VERTICAL)
    gtk_paint_hline (widget->style, widget->window,
		     GTK_WIDGET_STATE (widget), area, widget,
		     "toolbar",
		     allocation->x + allocation->width *
		     SPACE_LINE_START / SPACE_LINE_DIVISION,
		     allocation->x + allocation->width *
		     SPACE_LINE_END / SPACE_LINE_DIVISION,
		     allocation->y + (space_size-widget->style->ythickness)/2);
}

static gint
egg_toolbar_expose (GtkWidget      *widget,
		    GdkEventExpose *event)
{
  EggToolbar *toolbar = EGG_TOOLBAR (widget);
  GList *items;
  gint border_width;

  border_width = GTK_CONTAINER (widget)->border_width;

  if (GTK_WIDGET_DRAWABLE (widget))
    {
      GtkShadowType shadow_type;

      gtk_widget_style_get (widget, "shadow_type", &shadow_type, NULL);

      gtk_paint_box (widget->style,
		     widget->window,
                     GTK_WIDGET_STATE (widget),
                     shadow_type,
		     &event->area, widget, "toolbar",
		     widget->allocation.x + border_width,
                     widget->allocation.y + border_width,
		     widget->allocation.width - border_width,
                     widget->allocation.height - border_width);

    }

  items = toolbar->items;
  while (items)
    {
      EggToolItem *item = EGG_TOOL_ITEM (items->data);

      if (GTK_BIN (item)->child)
	gtk_container_propagate_expose (GTK_CONTAINER (widget),
					GTK_WIDGET (item),
					event);
      else if (GTK_WIDGET_MAPPED (item) && get_space_style (toolbar) == GTK_TOOLBAR_SPACE_LINE)
	egg_toolbar_paint_space_line (widget, &event->area, item);

      items = items->next;
    }

  gtk_container_propagate_expose (GTK_CONTAINER (widget),
				  toolbar->button,
				  event);

  return FALSE;
}

static void
egg_toolbar_size_request (GtkWidget      *widget,
			  GtkRequisition *requisition)
{
  EggToolbar *toolbar = EGG_TOOLBAR (widget);
  GList *items;
  gint nbuttons, ipadding;
  gint button_maxw, button_maxh;
  gint total_button_maxw, total_button_maxh;
  gint space_size;
  GtkRequisition child_requisition;

  requisition->width = GTK_CONTAINER (toolbar)->border_width * 2;
  requisition->height = GTK_CONTAINER (toolbar)->border_width * 2;
  nbuttons = 0;
  button_maxw = 0;
  button_maxh = 0;
  total_button_maxw = 0;
  total_button_maxh = 0;
  items = toolbar->items;
  space_size = get_space_size (toolbar);

  if (toolbar->show_arrow)
    {
      /* When we enable the arrow we only want to be the
       * size of the arrows plus the size of any items that
       * are pack-end.
       */

      items = toolbar->items;

      while (items)
	{
	  EggToolItem *item = EGG_TOOL_ITEM (items->data);

	  if (TOOLBAR_ITEM_VISIBLE (item))
	    {
	      gtk_widget_size_request (GTK_WIDGET (item), &child_requisition);

	      total_button_maxw = MAX (total_button_maxw, child_requisition.width);
	      total_button_maxh = MAX (total_button_maxh, child_requisition.height);

	      if (item->homogeneous)
		{
		  if (item->pack_end)
		    nbuttons++;
		  button_maxw = MAX (button_maxw, child_requisition.width);
		  button_maxh = MAX (button_maxh, child_requisition.height);
		}
	      else if (item->pack_end)
		{
		  if (toolbar->orientation == GTK_ORIENTATION_HORIZONTAL)
		    requisition->width += child_requisition.width;
		  else
		    requisition->height += child_requisition.height;
		}
	      if (toolbar->orientation == GTK_ORIENTATION_HORIZONTAL)
		requisition->height = MAX (requisition->height, child_requisition.height);
	      else
		requisition->width = MAX (requisition->width, child_requisition.width);
	    }

	  items = items->next;
	}

      /* Add the arrow */
      gtk_widget_size_request (toolbar->button, &child_requisition);

      if (toolbar->orientation == GTK_ORIENTATION_HORIZONTAL)
	{
	  requisition->width += child_requisition.width;
	  requisition->height = MAX (requisition->height, child_requisition.height);
	}
      else
	{
	  requisition->height += child_requisition.height;
	  requisition->width = MAX (requisition->width, child_requisition.width);
	}
    }
  else
    {
      items = toolbar->items;

      while (items)
	{
	  EggToolItem *item = EGG_TOOL_ITEM (items->data);

	  if (!TOOLBAR_ITEM_VISIBLE (item))
	    {
	      items = items->next;
	      continue;
	    }

	  if (!GTK_BIN (item)->child)
	    {
	      if (toolbar->orientation == GTK_ORIENTATION_HORIZONTAL)
		requisition->width += space_size;
	      else
		requisition->height += space_size;
	    }
	  else
	    {
	      gtk_widget_size_request (GTK_WIDGET (item), &child_requisition);

	      total_button_maxw = MAX (total_button_maxw, child_requisition.width);
	      total_button_maxh = MAX (total_button_maxh, child_requisition.height);

	      if (item->homogeneous)
		{
		  nbuttons++;
		  button_maxw = MAX (button_maxw, child_requisition.width);
		  button_maxh = MAX (button_maxh, child_requisition.height);
		}
	      else
		{
		  if (toolbar->orientation == GTK_ORIENTATION_HORIZONTAL)
		    {
		      requisition->width += child_requisition.width;
		      requisition->height = MAX (requisition->height, child_requisition.height);
		    }
		  else
		    {
		      requisition->height += child_requisition.height;
		      requisition->width = MAX (requisition->width, child_requisition.width);
		    }
		}
	    }

	  items = items->next;
	}
    }

  if (toolbar->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      requisition->width += nbuttons * button_maxw;
      requisition->height = MAX (requisition->height, button_maxh);
    }
  else
    {
      requisition->width = MAX (requisition->width, button_maxw);
      requisition->height += nbuttons * button_maxh;
    }

  /* Extra spacing */
  gtk_widget_style_get (widget, "internal_padding", &ipadding, NULL);

  requisition->width += 2 * ipadding;
  requisition->height += 2 * ipadding;

  toolbar->total_button_maxw = total_button_maxw;
  toolbar->total_button_maxh = total_button_maxh;
  toolbar->button_maxw = button_maxw;
  toolbar->button_maxh = button_maxh;
}

static void
egg_toolbar_size_allocate (GtkWidget     *widget,
			   GtkAllocation *allocation)
{
  EggToolbar *toolbar = EGG_TOOLBAR (widget);
  GList *items;
  GtkAllocation child_allocation;
  gint ipadding, space_size;
  gint border_width, edge_position;
  gint available_width, available_height;
  gint available_size, total_size;
  GtkRequisition child_requisition;
  gint remaining_size;
  gint number_expandable, expandable_size;
  gboolean first_expandable;

  widget->allocation = *allocation;
  border_width = GTK_CONTAINER (widget)->border_width;
  total_size = 0;
  number_expandable = 0;
  space_size = get_space_size (toolbar);

  gtk_widget_style_get (widget, "internal_padding", &ipadding, NULL);
  border_width += ipadding;

  available_width  = allocation->width  - 2 * border_width;
  available_height = allocation->height - 2 * border_width;
  if (toolbar->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      edge_position = allocation->x + allocation->width - border_width;
      available_size = available_width;
    }
  else
    {
      edge_position = allocation->y + allocation->height - border_width;
      available_size = available_height;
    }

  items = g_list_last (toolbar->items);

  while (items)
    {
      EggToolItem *item = EGG_TOOL_ITEM (items->data);

      if (!item->pack_end || !TOOLBAR_ITEM_VISIBLE (item))
	{
	  items = items->prev;
	  continue;
	}

      if (!GTK_BIN (item)->child)
	{
	  if (toolbar->orientation == GTK_ORIENTATION_HORIZONTAL)
	    {
	      child_allocation.width = space_size;
	      child_allocation.height = available_height;
	      child_allocation.x = edge_position - child_allocation.width;
	      child_allocation.y = allocation->y + (allocation->height - child_allocation.height) / 2;

	      gtk_widget_size_allocate (GTK_WIDGET (item), &child_allocation);

	      edge_position -= child_allocation.width;
	      available_size -= child_allocation.width;
	    }
	  else
	    {
	      child_allocation.width = available_width;
	      child_allocation.height = space_size;
	      child_allocation.x = allocation->x + (allocation->width - child_allocation.width) / 2;
	      child_allocation.y = edge_position - child_allocation.height;

	      gtk_widget_size_allocate (GTK_WIDGET (item), &child_allocation);

	      edge_position -= child_allocation.height;
	      available_size -= child_allocation.height;
	    }
	}
      else
	{
	  gtk_widget_get_child_requisition (GTK_WIDGET (item), &child_requisition);

	  if (toolbar->orientation == GTK_ORIENTATION_HORIZONTAL)
	    {
	      if (item->homogeneous)
		child_allocation.width = toolbar->button_maxw;
	      else
		child_allocation.width = child_requisition.width;
	      child_allocation.height = available_height;
	      child_allocation.y = allocation->y + (allocation->height - child_allocation.height) / 2;
	      child_allocation.x = edge_position - child_allocation.width;

	      gtk_widget_size_allocate (GTK_WIDGET (item), &child_allocation);

	      edge_position -= child_allocation.width;
	      available_size -= child_allocation.width;
	    }
	  else
	    {
	      if (item->homogeneous)
		child_allocation.height = toolbar->button_maxh;
	      else
		child_allocation.height = child_requisition.height;

	      child_allocation.width = available_width;
	      child_allocation.x = allocation->x + (allocation->width - child_allocation.width) / 2;
	      child_allocation.y = edge_position - child_allocation.height;

	      gtk_widget_size_allocate (GTK_WIDGET (item), &child_allocation);

	      edge_position -= child_allocation.height;
	      available_size -= child_allocation.height;
	    }
	}

      items = items->prev;
    }

  /* Now go through the items and see if they fit */
  items = toolbar->items;

  while (items)
    {
      EggToolItem *item = EGG_TOOL_ITEM (items->data);

      if (item->pack_end || !TOOLBAR_ITEM_VISIBLE (item))
	{
	  items = items->next;
	  continue;
	}

      if (item->expandable)
	number_expandable += 1;

      if (!GTK_BIN (item)->child)
	{
	  total_size += space_size;
	}
      else
	{
	  if (toolbar->orientation == GTK_ORIENTATION_HORIZONTAL)
	    {
	      gtk_widget_get_child_requisition (GTK_WIDGET (item), &child_requisition);

	      if (item->homogeneous)
		total_size += toolbar->button_maxw;
	      else
		total_size += child_requisition.width;
	    }
	  else
	    {
	      gtk_widget_get_child_requisition (GTK_WIDGET (item), &child_requisition);

	      if (item->homogeneous)
		total_size += toolbar->button_maxh;
	      else
		total_size += child_requisition.height;
	    }
	}
      items = items->next;
    }

  /* Check if we need to allocate and show the arrow */
  if (available_size < total_size)
    {
      if (toolbar->orientation == GTK_ORIENTATION_HORIZONTAL)
	{
	  gtk_widget_get_child_requisition (toolbar->button, &child_requisition);
	  available_size -= child_requisition.width;

	  child_allocation.width = child_requisition.width;
	  child_allocation.height = toolbar->total_button_maxh;
	  child_allocation.y = allocation->y + (allocation->height - child_allocation.height) / 2;
	  child_allocation.x = edge_position - child_allocation.width;
	}
      else
	{
	  gtk_widget_get_child_requisition (toolbar->button, &child_requisition);
	  available_size -= child_requisition.width;

	  child_allocation.height = child_requisition.height;
	  child_allocation.width = toolbar->total_button_maxw;
	  child_allocation.x = allocation->x + (allocation->width - child_allocation.width) / 2;
	  child_allocation.y = edge_position - child_allocation.height;
	}

      gtk_widget_size_allocate (toolbar->button, &child_allocation);
      gtk_widget_show (toolbar->button);
    }
  else
    gtk_widget_hide (toolbar->button);

  /* Finally allocate the remaining items */
  items = toolbar->items;
  child_allocation.x = allocation->x + border_width;
  child_allocation.y = allocation->y + border_width;
  remaining_size = MAX (0, available_size - total_size);
  total_size = 0;
  first_expandable = TRUE;

  while (items)
    {
      EggToolItem *item = EGG_TOOL_ITEM (items->data);

      if (item->pack_end || !TOOLBAR_ITEM_VISIBLE (item))
	{
	  items = items->next;
	  continue;
	}

      if (!GTK_BIN (item)->child)
	{
	  if (toolbar->orientation == GTK_ORIENTATION_HORIZONTAL)
	    {
	      child_allocation.width = space_size;
	      child_allocation.height = available_height;
	      child_allocation.y = allocation->y + (allocation->height - child_allocation.height) / 2;
	      total_size += child_allocation.width;

	      if (total_size > available_size)
		break;

	      gtk_widget_size_allocate (GTK_WIDGET (item), &child_allocation);
	      gtk_widget_map (GTK_WIDGET (item));

	      child_allocation.x += child_allocation.width;
	    }
	  else
	    {
	      child_allocation.width = available_width;
	      child_allocation.height = space_size;
	      child_allocation.x = allocation->x + (allocation->width - child_allocation.width) / 2;
	      total_size += child_allocation.height;

	      if (total_size > available_size)
		break;

	      gtk_widget_size_allocate (GTK_WIDGET (item), &child_allocation);
	      gtk_widget_map (GTK_WIDGET (item));

	      child_allocation.y += child_allocation.height;
	    }
	}
      else
	{
	  gtk_widget_get_child_requisition (GTK_WIDGET (item), &child_requisition);

	  if (item->expandable)
	    {
	      expandable_size = remaining_size / number_expandable;

	      if (first_expandable)
		{
		  expandable_size += remaining_size % number_expandable;
		  first_expandable = FALSE;
		}
	    }
	  else
	    expandable_size = 0;

	  if (toolbar->orientation == GTK_ORIENTATION_HORIZONTAL)
	    {
	      if (item->homogeneous)
		child_allocation.width = toolbar->button_maxw;
	      else
		child_allocation.width = child_requisition.width;

	      child_allocation.height = available_height;
	      child_allocation.width += expandable_size;
	      child_allocation.y = allocation->y + (allocation->height - child_allocation.height) / 2;
	      total_size += child_allocation.width;
	    }
	  else
	    {
	      if (item->homogeneous)
		child_allocation.height = toolbar->button_maxh;
	      else
		child_allocation.height = child_requisition.height;

	      child_allocation.width = available_width;
	      child_allocation.height += expandable_size;
	      child_allocation.x = allocation->x + (allocation->width - child_allocation.width) / 2;
	      total_size += child_allocation.height;
	    }

	  if (total_size > available_size)
	    break;

	  gtk_widget_size_allocate (GTK_WIDGET (item), &child_allocation);
	  gtk_widget_map (GTK_WIDGET (item));

	  if (toolbar->orientation == GTK_ORIENTATION_HORIZONTAL)
	    child_allocation.x += child_allocation.width;
	  else
	    child_allocation.y += child_allocation.height;

	}

      items = items->next;
    }

  /* Unmap the remaining items */
  toolbar->first_non_fitting_item = items;
  while (items)
    {
      EggToolItem *item = EGG_TOOL_ITEM (items->data);

      gtk_widget_unmap (GTK_WIDGET (item));
      items = items->next;
    }
}

static void
egg_toolbar_style_set (GtkWidget *widget,
		       GtkStyle  *prev_style)
{
  if (prev_style)
    egg_toolbar_update_button_relief (EGG_TOOLBAR (widget));
}

static gboolean
egg_toolbar_focus (GtkWidget       *widget,
		   GtkDirectionType dir)
{
  /* Focus can't go in toolbars */

  return FALSE;
}

static void
egg_toolbar_add (GtkContainer *container,
		 GtkWidget *widget)
{
  g_return_if_fail (EGG_IS_TOOLBAR (container));
  g_return_if_fail (EGG_IS_TOOL_ITEM (widget));

  egg_toolbar_insert_item (EGG_TOOLBAR (container),
			   EGG_TOOL_ITEM (widget),
			   -1);
}

static void
egg_toolbar_remove (GtkContainer *container,
		    GtkWidget    *widget)
{
  EggToolbar *toolbar;
  GList *tmp;

  g_return_if_fail (EGG_IS_TOOLBAR (container));
  g_return_if_fail (EGG_IS_TOOL_ITEM (widget));

  toolbar = EGG_TOOLBAR (container);
  for (tmp = toolbar->items; tmp != NULL; tmp = tmp->next)
    {
      GtkWidget *child = tmp->data;

      if (child == widget)
	{
	  gboolean was_visible;

	  was_visible = GTK_WIDGET_VISIBLE (widget);
	  gtk_widget_unparent (widget);

	  toolbar->items = g_list_remove_link (toolbar->items, tmp);
	  toolbar->num_children--;

	  if (was_visible && GTK_WIDGET_VISIBLE (container))
	    gtk_widget_queue_resize (GTK_WIDGET (container));

	  break;
	}
    }

}

static void
egg_toolbar_forall (GtkContainer *container,
		    gboolean	  include_internals,
		    GtkCallback   callback,
		    gpointer      callback_data)
{
  EggToolbar *toolbar;
  GList *items;

  g_return_if_fail (EGG_IS_TOOLBAR (container));
  g_return_if_fail (callback != NULL);

  /* FIXME: I'm not sure we can do this without breaking something */
  toolbar = EGG_TOOLBAR (container);
  items = toolbar->items;

  while (items)
    {
      EggToolItem *item = EGG_TOOL_ITEM (items->data);

      items = items->next;
      (*callback) (GTK_WIDGET (item), callback_data);
    }

  if (include_internals)
    (* callback) (toolbar->button, callback_data);
}

static GType
egg_toolbar_child_type (GtkContainer *container)
{
  return EGG_TYPE_TOOL_ITEM;
}

static void
egg_toolbar_real_orientation_changed (EggToolbar    *toolbar,
				      GtkOrientation orientation)
{
  GList *items;

  if (toolbar->orientation != orientation)
    {
      toolbar->orientation = orientation;

      items = toolbar->items;
      while (items)
	{
	  EggToolItem *item = EGG_TOOL_ITEM (items->data);

	  egg_tool_item_set_orientation (item, orientation);

	  items = items->next;
	}

      if (orientation == GTK_ORIENTATION_HORIZONTAL)
	gtk_arrow_set (GTK_ARROW (toolbar->arrow), GTK_ARROW_DOWN, GTK_SHADOW_NONE);
      else
	gtk_arrow_set (GTK_ARROW (toolbar->arrow), GTK_ARROW_RIGHT, GTK_SHADOW_NONE);

      gtk_widget_queue_resize (GTK_WIDGET (toolbar));
      g_object_notify (G_OBJECT (toolbar), "orientation");
    }
}

static void
egg_toolbar_real_style_changed (EggToolbar     *toolbar,
				GtkToolbarStyle style)
{
  GList *items;

  if (toolbar->style != style)
    {
      toolbar->style = style;

      items = toolbar->items;

      while (items)
	{
	  EggToolItem *item = EGG_TOOL_ITEM (items->data);

	  egg_tool_item_set_toolbar_style (item, style);

	  items = items->next;
	}

      gtk_widget_queue_resize (GTK_WIDGET (toolbar));
      g_object_notify (G_OBJECT (toolbar), "toolbar_style");
    }
}

static void
menu_position_func (GtkMenu  *menu,
		    gint     *x,
		    gint     *y,
		    gboolean *push_in,
		    gpointer  user_data)
{
  EggToolbar *toolbar = EGG_TOOLBAR (user_data);
  GtkRequisition req;

  gdk_window_get_origin (GTK_BUTTON (toolbar->button)->event_window, x, y);
  gtk_widget_size_request (GTK_WIDGET (menu), &req);

  if (toolbar->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      *y += toolbar->button->allocation.height;
      *x += toolbar->button->allocation.width - req.width;
    }
  else
    {
      *x += toolbar->button->allocation.width;
      *y += toolbar->button->allocation.height - req.height;
    }

  *push_in = TRUE;
}

static void
menu_deactivated (GtkWidget *menu, GtkWidget *button)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), FALSE);
}

static void
egg_toolbar_button_press (GtkWidget      *button,
			  GdkEventButton *event,
			  EggToolbar     *toolbar)
{
  GtkWidget *menu;
  GtkWidget *menu_item;
  GList *items;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);

  menu = gtk_menu_new ();
  g_signal_connect (menu, "deactivate", G_CALLBACK (menu_deactivated), button);

  items = toolbar->first_non_fitting_item;
  while (items)
    {
      EggToolItem *item = EGG_TOOL_ITEM (items->data);

      if (TOOLBAR_ITEM_VISIBLE (item) && !item->pack_end)
	{
	  menu_item = NULL;
	  g_signal_emit_by_name (item, "create_menu_proxy", &menu_item);

	  if (menu_item)
	    {
	      gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	    }
	}
      items = items->next;
    }

  gtk_widget_show_all (menu);

  gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
		  menu_position_func, toolbar,
		  event->button, event->time);
}

static void
egg_toolbar_update_button_relief (EggToolbar *toolbar)
{
  GtkReliefStyle relief;
  GList *items;

  relief = get_button_relief (toolbar);

  items = toolbar->items;
  while (items)
    {
      EggToolItem *item = EGG_TOOL_ITEM (items->data);

      egg_tool_item_set_relief_style (item, relief);

      items = items->next;
    }

  gtk_button_set_relief (GTK_BUTTON (toolbar->button), relief);
}

static GtkReliefStyle
get_button_relief (EggToolbar *toolbar)
{
  GtkReliefStyle button_relief = GTK_RELIEF_NORMAL;

  gtk_widget_ensure_style (GTK_WIDGET (toolbar));

  gtk_widget_style_get (GTK_WIDGET (toolbar),
                        "button_relief", &button_relief,
                        NULL);

  return button_relief;
}

static gint
get_space_size (EggToolbar *toolbar)
{
  gint space_size = DEFAULT_SPACE_SIZE;

  gtk_widget_style_get (GTK_WIDGET (toolbar),
                        "space_size", &space_size,
                        NULL);

  return space_size;
}

static GtkToolbarSpaceStyle
get_space_style (EggToolbar *toolbar)
{
  GtkToolbarSpaceStyle space_style = DEFAULT_SPACE_STYLE;

  gtk_widget_style_get (GTK_WIDGET (toolbar),
                        "space_style", &space_style,
                        NULL);


  return space_style;
}

GtkWidget *
egg_toolbar_new (void)
{
  EggToolbar *toolbar;

  toolbar = g_object_new (EGG_TYPE_TOOLBAR, NULL);

  return GTK_WIDGET (toolbar);
}

void
egg_toolbar_insert_item (EggToolbar  *toolbar,
			 EggToolItem *item,
			 gint pos)
{
  g_return_if_fail (EGG_IS_TOOLBAR (toolbar));
  g_return_if_fail (EGG_IS_TOOL_ITEM (item));

  toolbar->items = g_list_insert (toolbar->items, item, pos);
  toolbar->num_children++;

  egg_tool_item_set_orientation (item, toolbar->orientation);
  egg_tool_item_set_toolbar_style (item, toolbar->style);
  egg_tool_item_set_relief_style (item, get_button_relief (toolbar));

  gtk_widget_set_parent (GTK_WIDGET (item), GTK_WIDGET (toolbar));
  GTK_WIDGET_UNSET_FLAGS (item, GTK_CAN_FOCUS);
}

void
egg_toolbar_set_orientation (EggToolbar    *toolbar,
			     GtkOrientation orientation)
{
  g_return_if_fail (EGG_IS_TOOLBAR (toolbar));

  g_signal_emit (toolbar, toolbar_signals[ORIENTATION_CHANGED], 0, orientation);
}

void
egg_toolbar_set_style (EggToolbar     *toolbar,
		       GtkToolbarStyle style)
{
  g_return_if_fail (EGG_IS_TOOLBAR (toolbar));

  g_signal_emit (toolbar, toolbar_signals[STYLE_CHANGED], 0, style);
}

void
egg_toolbar_set_show_arrow (EggToolbar *toolbar,
			    gboolean    show_arrow)
{
  g_return_if_fail (EGG_IS_TOOLBAR (toolbar));

  if ((toolbar->show_arrow && show_arrow) |
      (!toolbar->show_arrow & !show_arrow))
    return;

  toolbar->show_arrow = show_arrow;

  if (!toolbar->show_arrow)
    gtk_widget_hide (toolbar->button);

  gtk_widget_queue_resize (GTK_WIDGET (toolbar));
  g_object_notify (G_OBJECT (toolbar), "show_arrow");
}

