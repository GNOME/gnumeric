/*
 * sheet-object-graphic.c: Implements the drawing object manipulation for Gnumeric
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include <libgnomeprint/gnome-print.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "sheet-object-graphic.h"

static SheetObjectClass        *sheet_object_graphic_parent_class;
static SheetObjectGraphicClass *sheet_object_filled_parent_class;

static void
sheet_object_graphic_destroy (GtkObject *object)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (object);

	string_unref (sog->color);
	GTK_OBJECT_CLASS (sheet_object_graphic_parent_class)->destroy (object);
}

static void
sheet_object_graphic_print (SheetObject *so, SheetObjectPrintInfo *pi)
{
	double x, y;

	if (so->type == SHEET_OBJECT_ARROW) {
		static gboolean warned = FALSE;
		g_warning ("FIXME: I print arrows as lines");
		warned = TRUE;
	}

	/* Gnome print uses a strange co-ordinate system */
	gnome_print_gsave (pi->pc);

	x = so->bbox_points->coords [0];
	y = so->bbox_points->coords [1];
	gnome_print_moveto (pi->pc,
			    pi->print_x + (x - pi->x) * pi->print_x_scale,
			    pi->print_y - (y - pi->y) * pi->print_y_scale);

	x = so->bbox_points->coords [2];
	y = so->bbox_points->coords [3];
	gnome_print_lineto (pi->pc,
			    pi->print_x + (x - pi->x) * pi->print_x_scale,
			    pi->print_y - (y - pi->y) * pi->print_y_scale);

	gnome_print_stroke (pi->pc);

	gnome_print_grestore (pi->pc);
}

static GnomeCanvasItem *
sheet_object_graphic_realize (SheetObject *so, SheetView *sheet_view)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);
	GnomeCanvasItem *item = NULL;
	
	g_return_val_if_fail (so != NULL, NULL);
	g_return_val_if_fail (sheet_view != NULL, NULL);

	switch (sog->type) {
	case SHEET_OBJECT_LINE:
		item = gnome_canvas_item_new (
			sheet_view->object_group,
			gnome_canvas_line_get_type (),
			"points",        so->bbox_points,
			"fill_color",    sog->color->str,
			"width_units",   (double) sog->width,
			NULL);
		break;

	case SHEET_OBJECT_ARROW:
		item = gnome_canvas_item_new (
			sheet_view->object_group,
			gnome_canvas_line_get_type (),
			"points",        so->bbox_points,
			"fill_color",    sog->color->str,
			"width_units",   (double) sog->width,
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
sheet_object_graphic_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	sheet_object_graphic_parent_class = gtk_type_class (sheet_object_get_type ());

	/* Object class method overrides */
	object_class->destroy = sheet_object_graphic_destroy;

	/* SheetObject class method overrides */
	sheet_object_class->realize = sheet_object_graphic_realize;
	sheet_object_class->print   = sheet_object_graphic_print;
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
	sheet_object_set_bounds (so, x1, y1, x2, y2);

	sog->type  = is_arrow ? SHEET_OBJECT_ARROW : SHEET_OBJECT_LINE;
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
	double x1, y1, x2, y2;
	GtkType type;
	
	g_return_val_if_fail (so != NULL, NULL);
	g_return_val_if_fail (sheet_view != NULL, NULL);

	switch (sog->type){
	case SHEET_OBJECT_BOX:
		type = gnome_canvas_rect_get_type ();
		break;

	case SHEET_OBJECT_OVAL:
		type = gnome_canvas_ellipse_get_type ();
		break;

	default:
		type = 0;
		g_assert_not_reached ();
	}

	sheet_object_get_bounds (so, &x1, &y1, &x2, &y2);

	item = gnome_canvas_item_new (
		sheet_view->object_group,
		type,
		"x1",            x1,
		"y1",            y1,
		"x2",            x2,
		"y2",            y2,
		"fill_color",    sof->fill_color ? sof->fill_color->str : NULL,
		"outline_color", sog->color->str,
		"width_units",   (double) sog->width,
		NULL);

	return item;
}

static void
sheet_object_filled_update_bounds (SheetObject *so)
{
	GList *l;
	double x1, y1, x2, y2;

	sheet_object_get_bounds (so, &x1, &y1, &x2, &y2);

	for (l = so->realized_list; l; l = l->next){
		GnomeCanvasItem *item = l->data;

		gnome_canvas_item_set (
			item,
			"x1", x1, "y1", y1,
			"x2", x2, "y2", y2,
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
	sheet_object_class->update_bounds = sheet_object_filled_update_bounds;
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
	sheet_object_construct  (so, sheet);
	sheet_object_set_bounds (so, x1, y1, x2, y2);

	sof = SHEET_OBJECT_FILLED (so);
	sog = SHEET_OBJECT_GRAPHIC (so);

	sog->type = type;
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

