/*
 * sheet-object-graphic.c: Implements the drawing object manipulation for Gnumeric
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "dialogs.h"
#include "sheet-object-graphic.h"

static SheetObject *sheet_object_graphic_parent_class;
static SheetObjectGraphic *sheet_object_filled_parent_class;

static void
sheet_object_graphic_destroy (GtkObject *object)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (object);

	string_unref (sog->color);
	GTK_OBJECT_CLASS (sheet_object_graphic_parent_class)->destroy(object);
}

static GnomeCanvasItem *
sheet_object_graphic_realize (SheetObject *so, SheetView *sheet_view)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);
	GnomeCanvasItem *item = NULL;
	
	g_return_val_if_fail (so != NULL, NULL);
	g_return_val_if_fail (sheet_view != NULL, NULL);

	switch (sog->type){
	case SHEET_OBJECT_LINE:
		item = gnome_canvas_item_new (
			sheet_view->object_group,
			gnome_canvas_line_get_type (),
			"points",        so->bbox_points,
			"fill_color",    sog->color->str,
			"width_pixels",  sog->width,
			NULL);
		break;

	case SHEET_OBJECT_ARROW:
		item = gnome_canvas_item_new (
			sheet_view->object_group,
			gnome_canvas_line_get_type (),
			"points",        so->bbox_points,
			"fill_color",    sog->color->str,
			"width_pixels",  sog->width,
			"arrow_shape_a", 8.0,
			"arrow_shape_b", 10.0,
			"arrow_shape_c", 3.0,
			"last_arrowhead", TRUE,
			NULL);
		break;

	default:
		g_assert_not_reached ();
	}

	return item;
}

static void
sheet_object_graphic_update (SheetObject *sheet_object, gdouble to_x, gdouble to_y)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (sheet_object);
	GList *l;
	
	sheet_object->bbox_points->coords [2] = (gdouble) to_x;
	sheet_object->bbox_points->coords [3] = (gdouble) to_y;

	for (l = sheet_object->realized_list; l; l = l->next){
		GnomeCanvasItem *item = l->data;

		gnome_canvas_item_set (item, "points", sheet_object->bbox_points, NULL);
	}
}

static void
sheet_object_graphic_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	sheet_object_graphic_parent_class = gtk_type_class (sheet_object_get_type ());

	/* Object class method overrides */
	object_class->destroy = sheet_object_graphic_destroy;

	/* SheetObject class method overrides */
	sheet_object_class->realize = sheet_object_graphic_realize;
	sheet_object_class->update = sheet_object_graphic_update;
}

GtkType
sheet_object_graphic_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"SheetObjectGraphic",
			sizeof (SheetObjectGraphic),
			sizeof (SheetObjectGraphicClass),
			(GtkClassInitFunc) sheet_object_graphic_class_init,
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (sheet_object_get_type (), &info);
	}

	return type;
}

/*
 * sheet_object_create_line
 *
 * Creates a line object
 */
SheetObject *
sheet_object_create_line (Sheet *sheet, int is_arrow,
			  double x1, double y1,
			  double x2, double y2,
			  const char *color, int w)
{
	SheetObjectGraphic *sog;
	SheetObject *so;
	
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	sog = gtk_type_new (sheet_object_graphic_get_type ());
	so = SHEET_OBJECT (sog);

	sheet_object_construct (so, sheet);

	sog->type = is_arrow ? SHEET_OBJECT_ARROW : SHEET_OBJECT_LINE;
	so->bbox_points->coords [0] = (gdouble) x1;
	so->bbox_points->coords [1] = (gdouble) y1;
	so->bbox_points->coords [2] = (gdouble) x2;
	so->bbox_points->coords [3] = (gdouble) y2;
	sog->color = string_get (color);
	sog->width = w;
	
	return SHEET_OBJECT (sog);
}

static void
sheet_object_filled_destroy (GtkObject *object)
{
	SheetObjectFilled *sof = SHEET_OBJECT_FILLED (object);

	if (sof->fill_color)
		string_unref (sof->fill_color);
	
	GTK_OBJECT_CLASS (sheet_object_filled_parent_class)->destroy (object);
}

static GnomeCanvasItem *
sheet_object_filled_realize (SheetObject *so, SheetView *sheet_view)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);
	SheetObjectFilled  *sof = SHEET_OBJECT_FILLED (so);
	GnomeCanvasItem *item = NULL;
	double *c, x1, y1, x2, y2;
	GtkType type;
	
	g_return_val_if_fail (so != NULL, NULL);
	g_return_val_if_fail (sheet_view != NULL, NULL);

	switch (sog->type){
	case SHEET_OBJECT_RECTANGLE:
		type = gnome_canvas_rect_get_type ();
		break;

	case SHEET_OBJECT_ELLIPSE:
		type = gnome_canvas_ellipse_get_type ();
		break;

	default:
		type = 0;
		g_assert_not_reached ();
	}

	c = so->bbox_points->coords;	

	item = gnome_canvas_item_new (
		sheet_view->object_group,
		type,
		"x1",            x1 = MIN (c [0], c [2]),
		"y1",            y1 = MIN (c [1], c [3]),
		"x2",            x2 = MAX (c [0], c [2]),
		"y2",            y2 = MAX (c [1], c [3]),
		"fill_color",    sof->fill_color ? sof->fill_color->str : NULL,
		"outline_color", sog->color->str,
		"width_pixels",  sog->width,
		NULL);

	return item;
}

static void
sheet_object_filled_update (SheetObject *sheet_object, gdouble to_x, gdouble to_y)
{
	double x1, x2, y1, y2;
	double *coords = sheet_object->bbox_points->coords;
	GList *l;
	
	coords [2] = to_x;
	coords [3] = to_y;
	
	for (l = sheet_object->realized_list; l; l = l->next){
		GnomeCanvasItem *item = l->data;

		gnome_canvas_item_set (
			item,
			"x1", MIN (coords [0], to_x), 
			"y1", MIN (coords [1], to_y),
			"x2", MAX (coords [0], to_x),
			"y2", MAX (coords [1], to_y),
			NULL);
	}
}

static void
sheet_object_filled_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);
	sheet_object_filled_parent_class = gtk_type_class (sheet_object_graphic_get_type ());

	object_class->destroy = sheet_object_filled_destroy;
	sheet_object_class->realize = sheet_object_filled_realize;
	sheet_object_class->update  = sheet_object_filled_update;
}

/*
 * sheet_object_create_filled:
 *
 * Creates and initializes a filled object of type TYPE.
 */
SheetObject *
sheet_object_create_filled (Sheet *sheet, int type,
			    double x1, double y1,
			    double x2, double y2,
			    const char *fill_color,
			    const char *outline_color, int w)
{
	SheetObjectFilled *sof;
	SheetObjectGraphic *sog;
	SheetObject *so;
	
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	so = gtk_type_new (sheet_object_filled_get_type ());
	sheet_object_construct (so, sheet);
	sof = SHEET_OBJECT_FILLED (so);
	sog = SHEET_OBJECT_GRAPHIC (so);
	
	sog->type = type;
	so->bbox_points->coords [0] = x1;
	so->bbox_points->coords [1] = y1;
	so->bbox_points->coords [2] = x2;
	so->bbox_points->coords [3] = y2;
	sog->width = w;
	sof->pattern = 0;

	if (outline_color)
		sog->color = string_get (outline_color);

	if (fill_color)
		sof->fill_color = string_get (fill_color);
	return so;
}

GtkType
sheet_object_filled_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"SheetObjectFilled",
			sizeof (SheetObjectFilled),
			sizeof (SheetObjectFilledClass),
			(GtkClassInitFunc) sheet_object_filled_class_init,
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (sheet_object_graphic_get_type (), &info);
	}

	return type;
}

