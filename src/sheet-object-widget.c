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
#include "sheet-object-widget.h"

static SheetObject *sheet_object_widget_parent_class;

static GnomeCanvasItem *
sheet_object_widget_realize (SheetObject *so, SheetView *sheet_view)
{
	SheetObjectWidget *sow;
	GnomeCanvasItem *item;
	GtkWidget *view_widget;
	double *c;

	c = so->bbox_points->coords;	
	sow = SHEET_OBJECT_WIDGET (so);
	view_widget = sow->realize (sow, sow->realize_closure);
	item = gnome_canvas_item_new (
		sheet_view->object_group,
		gnome_canvas_widget_get_type (),
		"widget", view_widget,
		"x",      MIN (c [0], c [2]),
		"y",      MIN (c [1], c [3]),
		"width",  fabs (c [0] - c [2]),
		"height", fabs (c [1] - c [3]),
		"size_pixels", FALSE,
		NULL);
	return item;
}

static void
sheet_object_widget_set_coords (SheetObject *so,
				gdouble x1, gdouble y1,
				gdouble x2, gdouble y2)
{
	GList *l;
	double *c;

	c = so->bbox_points->coords;

	c [0] = MIN (x1, x2);
	c [1] = MIN (y1, y2);
	c [2] = MAX (x1, x2);
	c [3] = MAX (y1, y2);

	for (l = so->realized_list; l; l = l->next){
		GnomeCanvasItem *item = l->data;

		gnome_canvas_item_set (
			item,
			"x",      c [0],
			"y",      c [1],
			"width",  fabs (x2-x1),
			"height", fabs (y2-y1),
			NULL);
	}
}

static void
sheet_object_widget_update (SheetObject *so, gdouble to_x, gdouble to_y)
{
	double x1, x2, y1, y2;
	double *c;

	c = so->bbox_points->coords;
	
	x1 = MIN (c [0], to_x);
	x2 = MAX (c [0], to_x);
	y1 = MIN (c [1], to_y);
	y2 = MAX (c [1], to_y);

	sheet_object_widget_set_coords (so, x1, y1, x2, y2);
}

/*
 * This implemenation moves the widget rather than
 * destroying/updating/creating the views
 */
static void
sheet_object_widget_update_coords (SheetObject *so, 
				   gdouble x1d, gdouble y1d,
				   gdouble x2d, gdouble y2d)
{
	double *c = so->bbox_points->coords;
	gdouble x1, y1, x2, y2;
	
	/* Update coordinates */
	c [0] += x1d;
	c [1] += y1d;
	c [2] += x2d;
	c [3] += y2d;

	/* Normalize it */
	x1 = MIN (c [0], c [2]);
	y1 = MIN (c [1], c [3]);
	x2 = MAX (c [0], c [2]);
	y2 = MAX (c [1], c [3]);

	sheet_object_widget_set_coords (so, x1, y1, x2, y2);
}

static void
sheet_object_widget_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	sheet_object_widget_parent_class = gtk_type_class (sheet_object_get_type ());

	/* SheetObject class method overrides */
	sheet_object_class->realize = sheet_object_widget_realize;
	sheet_object_class->update = sheet_object_widget_update;
	sheet_object_class->update_coords = sheet_object_widget_update_coords;
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
	g_return_if_fail (x1 <= x2);
	g_return_if_fail (y1 <= y2);
	g_return_if_fail (realize != NULL);

	so = SHEET_OBJECT (sow);
	
	sheet_object_construct (so, sheet);

	so->bbox_points->coords [0] = x1;
	so->bbox_points->coords [1] = y1;
	so->bbox_points->coords [2] = x2;
	so->bbox_points->coords [3] = y2;

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
	g_return_val_if_fail (x1 <= x2, NULL);
	g_return_val_if_fail (y1 <= y2, NULL);
	g_return_val_if_fail (realize != NULL, NULL);
	
	sow = gtk_type_new (sheet_object_widget_get_type ());
	
	sheet_object_widget_construct (sow, sheet, x1, y1, x2, y2, realize, realize_closure);

	return SHEET_OBJECT (sow);
}
	
