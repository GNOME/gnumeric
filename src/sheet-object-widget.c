/*
 * sheet-object-widget.c:
 *   SheetObject for adding widgets
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include <libgnorba/gnorba.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "cursors.h"
#include "sheet-object-widget.h"

static SheetObject *sheet_object_widget_parent_class;

static GnomeCanvasItem *
sheet_object_widget_realize (SheetObject *so, SheetView *sheet_view)
{
	SheetObjectWidget *sow;
	GnomeCanvasItem *item;
	GtkWidget *view_widget;
	double x1, x2, y1, y2;
	
	sheet_object_get_bounds (so, &x1, &y1, &x2, &y2);
	sow = SHEET_OBJECT_WIDGET (so);
	view_widget = sow->realize (sow, sow->realize_closure);
	item = gnome_canvas_item_new (
		sheet_view->object_group,
		gnome_canvas_widget_get_type (),
		"widget", view_widget,
		"x",      x1,
		"y",      y1,
		"width",  x2 - x1,
		"height", y2 - y1,
		"size_pixels", FALSE,
		NULL);

	sheet_object_widget_handle (so, view_widget, item);

	return item;
}

/*
 * This implemenation moves the widget rather than
 * destroying/updating/creating the views
 */
static void
sheet_object_widget_update_bounds (SheetObject *so)
{
	GList  *l;
	double x1, y1, x2, y2;

	sheet_object_get_bounds (so, &x1, &y1, &x2, &y2);

	for (l = so->realized_list; l; l = l->next){
		GnomeCanvasItem *item = l->data;

		gnome_canvas_item_set (
			item,
			"x",      x1,
			"y",      y1,
			"width",  x2 - x1,
			"height", y2 - y1,
			NULL);
	}
}

static void
sheet_object_widget_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	sheet_object_widget_parent_class = gtk_type_class (sheet_object_get_type ());

	/* SheetObject class method overrides */
	sheet_object_class->realize = sheet_object_widget_realize;
	sheet_object_class->update_bounds = sheet_object_widget_update_bounds;
}

GtkType
sheet_object_widget_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"SheetObjectWidget",
			sizeof (SheetObjectWidget),
			sizeof (SheetObjectWidgetClass),
			(GtkClassInitFunc) sheet_object_widget_class_init,
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (sheet_object_get_type (), &info);
	}
	return type;
}

void
sheet_object_widget_construct (SheetObjectWidget *sow,
			       Sheet *sheet,
			       double x1, double y1,
			       double x2, double y2,
			       SheetWidgetRealizeFunction realize,
			       void *realize_closure)
{
	SheetObject *so;
	
	g_return_if_fail (sow != NULL);
	g_return_if_fail (IS_SHEET_WIDGET_OBJECT (sow));
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (realize != NULL);

	so = SHEET_OBJECT (sow);
	
	sheet_object_construct  (so, sheet);
	so->type = SHEET_OBJECT_ACTION_CAN_PRESS;
	sheet_object_set_bounds (so, x1, y1, x2, y2);

	sow->realize = realize;
	sow->realize_closure = realize_closure;
}
			       
SheetObject *
sheet_object_widget_new (Sheet *sheet,
			 double x1, double y1,
			 double x2, double y2,
			 SheetWidgetRealizeFunction realize,
			 void *realize_closure)
{
	SheetObjectWidget *sow;
	
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (realize != NULL, NULL);
	
	sow = gtk_type_new (sheet_object_widget_get_type ());
	
	sheet_object_widget_construct (sow, sheet, x1, y1, x2, y2, realize, realize_closure);

	return SHEET_OBJECT (sow);
}
	
