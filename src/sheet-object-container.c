/*
 * sheet-object-container.c:
 *   SheetObject for containers (Bonobo, Graphics)
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "sheet-object-container.h"

static SheetObject *sheet_object_container_parent_class;

static void
sheet_object_container_destroy (GtkObject *object)
{
	SheetObjectContainer *soc = SHEET_OBJECT_CONTAINER (object);

	g_free (soc->repoid);

	if (soc->client_site)
		gtk_object_unref (GTK_OBJECT (soc->client_site));
	
	/* Call parent's destroy method */
	GTK_OBJECT_CLASS(sheet_object_container_parent_class)->destroy (object);
}

static GnomeCanvasItem *
sheet_object_container_realize (SheetObject *so, SheetView *sheet_view)
{
	GtkWidget *w = gtk_button_new_with_label ("OBJECT");
	GnomeCanvasItem *i;
	double *c;

	c = so->bbox_points->coords;	
	i = gnome_canvas_item_new (
		sheet_view->object_group,
		gnome_canvas_widget_get_type (),
		"widget", w,
		"x",      MIN (c [0], c [2]),
		"y",      MIN (c [1], c [3]),
		"width",  fabs (c [0] - c [2]),
		"height", fabs (c [1] - c [3]),
		"size_pixels", FALSE,
		NULL);

	gtk_widget_show (w);

	return i;
}

static void
sheet_object_container_update (SheetObject *so, gdouble to_x, gdouble to_y)
{
	double x1, x2, y1, y2;
	GList *l;
	double *c;

	c = so->bbox_points->coords;
	
	x1 = MIN (c [0], to_x);
	x2 = MAX (c [0], to_x);
	y1 = MIN (c [1], to_y);
	y2 = MAX (c [1], to_y);

	c [0] = x1;
	c [1] = y1;
	c [2] = x2;
	c [3] = y2;
	
	for (l = so->realized_list; l; l = l->next){
		GnomeCanvasItem *item = l->data;

		gnome_canvas_item_set (
			item,
			"x",     x1,
			"y",     y1,
			"width", fabs (x2-x1),
			"height",    fabs (y2-y1),
			NULL);
	}

}

static void
sheet_object_container_creation_finished (SheetObject *so)
{
}

static void
sheet_object_container_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	sheet_object_container_parent_class = gtk_type_class (sheet_object_get_type ());

	/* Object class method overrides */
	object_class->destroy = sheet_object_container_destroy;
	
	/* SheetObject class method overrides */
	sheet_object_class->realize = sheet_object_container_realize;
	sheet_object_class->update = sheet_object_container_update;
	sheet_object_class->creation_finished = sheet_object_container_creation_finished;
}

GtkType
sheet_object_container_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"SheetObjectContainer",
			sizeof (SheetObjectContainer),
			sizeof (SheetObjectContainerClass),
			(GtkClassInitFunc) sheet_object_container_class_init,
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (sheet_object_get_type (), &info);
	}

	return type;
}

SheetObject *
sheet_object_graphic_new (Sheet *sheet,
			  double x1, double y1,
			  double x2, double y2)
{
	SheetObjectContainer *c;
	SheetObject *so;
	
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (x1 <= x2, NULL);
	g_return_val_if_fail (y1 <= y2, NULL);

	c = gtk_type_new (sheet_object_container_get_type ());
	so = SHEET_OBJECT (c);
	
	sheet_object_construct (so, sheet);
	so->bbox_points->coords [0] = x1;
	so->bbox_points->coords [1] = y1;
	so->bbox_points->coords [2] = x2;
	so->bbox_points->coords [3] = y2;
	c->repoid = g_strdup ("IDL:Gnumeric/Graphics:1.0");

	return SHEET_OBJECT (c);
}
			  
