/* vim: set sw=8: */

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
#include "sheet-control-gui.h"
#include "str.h"
#include "sheet.h"
#include "gnumeric-util.h"
#include "gnumeric-type-util.h"
#include "sheet-object-graphic.h"
#include "sheet-object-impl.h"

typedef enum {
	SHEET_OBJECT_LINE,
	SHEET_OBJECT_ARROW,
	SHEET_OBJECT_BOX,
	SHEET_OBJECT_OVAL,
} SheetObjectGraphicType;

#define SHEET_OBJECT_GRAPHIC_TYPE     (sheet_object_graphic_get_type ())
#define SHEET_OBJECT_GRAPHIC(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_GRAPHIC_TYPE, SheetObjectGraphic))
#define SHEET_OBJECT_GRAPHIC_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_GRAPHIC_TYPE))
#define IS_SHEET_GRAPHIC_OBJECT(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_GRAPHIC_TYPE))

typedef struct {
	SheetObject     parent_object;
	String            *color;
	int               width;
	SheetObjectGraphicType type;
} SheetObjectGraphic;
typedef struct {
	SheetObjectClass parent_class;
} SheetObjectGraphicClass;
static SheetObjectClass *sheet_object_graphic_parent_class;
static GtkType sheet_object_graphic_get_type (void);

static void
sheet_object_graphic_color_set (SheetObjectGraphic *sog, char const *color)
{
	SheetObject *so = SHEET_OBJECT (sog);
	GList *l;

	String *tmp = string_get (color);
	if (sog->color)
		string_unref (sog->color);
	sog->color = tmp;

	for (l = so->realized_list; l; l = l->next)
		gnome_canvas_item_set (l->data, "fill_color", tmp->str, NULL);
}

static void
sheet_object_graphic_width_set (SheetObjectGraphic *sog, int width)
{
	SheetObject *so = SHEET_OBJECT (sog);
	GList *l;

	sog->width = width;
	for (l = so->realized_list; l; l = l->next)
		gnome_canvas_item_set (l->data, "width_units", width, NULL);
}

SheetObject *
sheet_object_line_new (Sheet *sheet, gboolean is_arrow)
{
	SheetObjectGraphic *sog;
	SheetObject *so;

	so = gtk_type_new (sheet_object_graphic_get_type ());
	sheet_object_construct (so, sheet, 40, 40);

	sog = SHEET_OBJECT_GRAPHIC (so);
	sog->type  = is_arrow ? SHEET_OBJECT_ARROW : SHEET_OBJECT_LINE;
	sog->color = string_get ("black");
	sog->width = 1;

	return SHEET_OBJECT (sog);
}

static void
sheet_object_graphic_destroy (GtkObject *object)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (object);

	string_unref (sog->color);
	GTK_OBJECT_CLASS (sheet_object_graphic_parent_class)->destroy (object);
}

static GtkObject *
sheet_object_graphic_new_view (SheetObject *so, SheetControlGUI *scg)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);
	GnomeCanvasItem *item = NULL;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);
	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), NULL);

	switch (sog->type) {
	case SHEET_OBJECT_LINE:
		item = gnome_canvas_item_new (
			scg->object_group,
			gnome_canvas_line_get_type (),
			"fill_color",    sog->color->str,
			"width_units",   (double) sog->width,
			NULL);
		break;

	case SHEET_OBJECT_ARROW:
		item = gnome_canvas_item_new (
			scg->object_group,
			gnome_canvas_line_get_type (),
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

	scg_object_register (so, item);
	return GTK_OBJECT (item);
}

static void
sheet_object_graphic_update_bounds (SheetObject *so, GtkObject *view,
				    SheetControlGUI *scg)
{
	GnomeCanvasPoints *points = gnome_canvas_points_new (2);
	scg_object_view_position (scg, so, points->coords);
	gnome_canvas_item_set (GNOME_CANVAS_ITEM (view),
			       "points", points,
			       NULL);
	gnome_canvas_points_free (points);
}

static gboolean
sheet_object_graphic_read_xml (CommandContext *cc,
			       SheetObject *so, xmlNodePtr tree)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);
	char *color = xmlGetProp (tree, "Color");
	int width;

	if (color != NULL) {
		sheet_object_graphic_color_set (sog, color);
		xmlFree (color);
	}
	if (xml_get_value_int (tree, "Width", &width))
		sheet_object_graphic_width_set (sog, width);

	return TRUE;
}

static void
sheet_object_graphic_print (SheetObject const *so, SheetObjectPrintInfo const *pi)
{
	double coords [4];

	sheet_object_position_pts (so, coords);
#if 0
	GnomePrintContext *ctx;
	double x, y;

	if (so->type == SHEET_OBJECT_ARROW) {
		static gboolean warned = FALSE;
		g_warning ("FIXME: I print arrows as lines");
		warned = TRUE;
	}

	ctx = GNOME_PRINT_CONTEXT (bonobo_print_data_get_meta (pi->pd));

	gnome_print_gsave (ctx);

	x = coords [0];
	y = coords [1];
	gnome_print_moveto (ctx,
			    pi->print_x + (x - pi->x) * pi->print_x_scale,
			    pi->print_y - (y - pi->y) * pi->print_y_scale);

	x = coords [2];
	y = coords [3];
	gnome_print_lineto (ctx,
			    pi->print_x + (x - pi->x) * pi->print_x_scale,
			    pi->print_y - (y - pi->y) * pi->print_y_scale);

	gnome_print_stroke (ctx);
	gnome_print_grestore (ctx);
#endif
}

static void
sheet_object_graphic_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	sheet_object_graphic_parent_class = gtk_type_class (sheet_object_get_type ());

	/* Object class method overrides */
	object_class->destroy = sheet_object_graphic_destroy;

	/* SheetObject class method overrides */
	sheet_object_class->update_bounds = sheet_object_graphic_update_bounds;
	sheet_object_class->new_view	  = sheet_object_graphic_new_view;
	sheet_object_class->read_xml	  = sheet_object_graphic_read_xml;
	sheet_object_class->print         = sheet_object_graphic_print;
}

GNUMERIC_MAKE_TYPE (sheet_object_graphic,
		    "SheetObjectGraphic", SheetObjectGraphic,
		    sheet_object_graphic_class_init, NULL,
		    sheet_object_get_type ())

/************************************************************************/

/*
 * SheetObjectFilled
 *
 * Derivative of SheetObjectGraphic, with filled parameter
 */
#define SHEET_OBJECT_FILLED_TYPE     (sheet_object_filled_get_type ())
#define SHEET_OBJECT_FILLED(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_FILLED_TYPE, SheetObjectFilled))
#define SHEET_OBJECT_FILLED_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_FILLED_TYPE, SheetObjectFilledClass))
#define IS_SHEET_FILLED_OBJECT(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_FILLED_TYPE))

typedef struct {
	SheetObjectGraphic parent_object;

	String      *fill_color;
	int         pattern;
} SheetObjectFilled;

typedef struct {
	SheetObjectGraphicClass parent_class;
} SheetObjectFilledClass;

static SheetObjectGraphicClass *sheet_object_filled_parent_class;
static GtkType sheet_object_filled_get_type (void);

static void
sheet_object_filled_color_set (SheetObjectFilled *sof, char const *fill_color)
{
	String *tmp = string_get (fill_color);
	SheetObject *so = SHEET_OBJECT (sof);
	GList *l;

	if (sof->fill_color)
		string_unref (sof->fill_color);
	sof->fill_color = tmp;
	for (l = so->realized_list; l; l = l->next)
		gnome_canvas_item_set (l->data, "fill_color", tmp->str, NULL);
}

static void
sheet_object_filled_pattern_set (SheetObjectFilled *sof, int pattern)
{
#if 0
	SheetObject *so = SHEET_OBJECT (sof);
	GList *l;
#endif

	sof->pattern = pattern;
	/* TODO : what was supposed to go here */
}

SheetObject *
sheet_object_box_new (Sheet *sheet, gboolean is_oval)
{
	SheetObjectFilled *sof;
	SheetObjectGraphic *sog;
	SheetObject *so;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	so = gtk_type_new (sheet_object_filled_get_type ());
	sheet_object_construct (so, sheet, 40, 40);

	sog = SHEET_OBJECT_GRAPHIC (so);
	sog->type = is_oval ? SHEET_OBJECT_OVAL : SHEET_OBJECT_BOX;
	sog->width = 1;
	sog->color = string_get ("black");

	sof = SHEET_OBJECT_FILLED (so);
	sof->pattern = 0;
	sof->fill_color = NULL;

	return so;
}

static void
sheet_object_filled_destroy (GtkObject *object)
{
	SheetObjectFilled *sof = SHEET_OBJECT_FILLED (object);

	if (sof->fill_color)
		string_unref (sof->fill_color);

	GTK_OBJECT_CLASS (sheet_object_filled_parent_class)->destroy (object);
}

static void
sheet_object_filled_update_bounds (SheetObject *so, GtkObject *view,
				   SheetControlGUI *scg)
{
	double coords [4];
	scg_object_view_position (scg, so, coords);
	gnome_canvas_item_set (GNOME_CANVAS_ITEM (view),
		"x1", coords [0], "y1", coords [1],
		"x2", coords [2], "y2", coords [3],
		NULL);
}

static GtkObject *
sheet_object_filled_new_view (SheetObject *so, SheetControlGUI *scg)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);
	SheetObjectFilled  *sof = SHEET_OBJECT_FILLED (so);
	GnomeCanvasItem *item = NULL;
	GtkType type;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);
	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), NULL);

	switch (sog->type) {
	case SHEET_OBJECT_BOX:	type = gnome_canvas_rect_get_type (); break;
	case SHEET_OBJECT_OVAL: type = gnome_canvas_ellipse_get_type (); break;
	default:
		type = 0;
		g_assert_not_reached ();
	}

	item = gnome_canvas_item_new (
		scg->object_group, type,
		"fill_color",    sof->fill_color ? sof->fill_color->str : NULL,
		"outline_color", sog->color->str,
		"width_units",   (double) sog->width,
		NULL);

	scg_object_register (so, item);
	return GTK_OBJECT (item);
}

static gboolean
sheet_object_filled_read_xml (CommandContext *cc,
			      SheetObject *so, xmlNodePtr tree)
{
	SheetObjectFilled *sof = SHEET_OBJECT_FILLED (so);
	char *fill_color = xmlGetProp (tree, "FillColor");
	int pattern;

	if (fill_color != NULL) {
		sheet_object_filled_color_set (sof, fill_color);
		xmlFree (fill_color);
	}

	if (xml_get_value_int (tree, "Pattern", &pattern))
		sheet_object_filled_pattern_set (sof, pattern);

	return sheet_object_graphic_read_xml (cc, so, tree);
}

static void
sheet_object_filled_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);
	sheet_object_filled_parent_class = gtk_type_class (sheet_object_graphic_get_type ());

	object_class->destroy		  = sheet_object_filled_destroy;
	sheet_object_class->new_view	  = sheet_object_filled_new_view;
	sheet_object_class->update_bounds = sheet_object_filled_update_bounds;
	sheet_object_class->read_xml	  = sheet_object_filled_read_xml;
}

#if 0
	switch (sog->type) {
	case SHEET_OBJECT_BOX: {
		SheetObjectFilled *sof = SHEET_OBJECT_FILLED (object);

		cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Rectangle", NULL);
		if (sof->fill_color != NULL)
			xml_set_value_string (cur, "FillColor", sof->fill_color);
		xml_set_value_int (cur, "Pattern", sof->pattern);
		break;
	}

	case SHEET_OBJECT_OVAL: {
		SheetObjectFilled *sof = SHEET_OBJECT_FILLED (object);

		cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Ellipse", NULL);
		if (sof->fill_color != NULL)
			xml_set_value_string (cur, "FillColor", sof->fill_color);
		xml_set_value_int (cur, "Pattern", sof->pattern);
		break;
	}

	case SHEET_OBJECT_ARROW:
		cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Arrow", NULL);
		break;

	case SHEET_OBJECT_LINE:
		cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Line", NULL);
		break;

	default :
		cur = NULL;
	}
	if (!cur)
		return NULL;

	xml_set_gnome_canvas_points (cur, "Points", object->bbox_points);
	xml_set_value_int (cur, "Width", sog->width);
	xml_set_value_string (cur, "Color", sog->color);
#endif

GNUMERIC_MAKE_TYPE (sheet_object_filled,
		    "SheetObjectFilled", SheetObjectFilled,
		    sheet_object_filled_class_init, NULL,
		    sheet_object_graphic_get_type ())
