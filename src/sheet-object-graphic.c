/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

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
#include "gnumeric-sheet.h"
#include "str.h"
#include "gnumeric-util.h"
#include "sheet-object-graphic.h"
#include "sheet-object-impl.h"

#include <gal/util/e-util.h>

/* These are persisted */
typedef enum {
	SHEET_OBJECT_LINE	= 1,
	SHEET_OBJECT_ARROW	= 2,
	SHEET_OBJECT_BOX	= 101,
	SHEET_OBJECT_OVAL	= 102,
} SheetObjectGraphicType;

#define SHEET_OBJECT_GRAPHIC_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_GRAPHIC_TYPE))

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
sheet_object_line_new (gboolean is_arrow)
{
	SheetObjectGraphic *sog;
	SheetObject *so;

	so = gtk_type_new (sheet_object_graphic_get_type ());
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
	/* FIXME : this is bogus */
	GnumericCanvas *gcanvas = scg_pane (scg, 0);
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);
	GnomeCanvasItem *item = NULL;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);
	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), NULL);

	switch (sog->type) {
	case SHEET_OBJECT_LINE:
		item = gnome_canvas_item_new (
			gcanvas->object_group,
			gnome_canvas_line_get_type (),
			"fill_color",    sog->color->str,
			"width_units",   (double) sog->width,
			NULL);
		break;

	case SHEET_OBJECT_ARROW:
		item = gnome_canvas_item_new (
			gcanvas->object_group,
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
sheet_object_graphic_read_xml (SheetObject *so,
			       XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);
	char *color = xmlGetProp (tree, "Color");
	int tmp;

	if (xml_get_value_int (tree, "Type", &tmp))
		sog->type = tmp;
	if (xml_get_value_int (tree, "Width", &tmp))
		sheet_object_graphic_width_set (sog, tmp);
	if (xml_get_value_int (tree, "Direction", &tmp))
		so->direction = tmp;
	if (color != NULL) {
		sheet_object_graphic_color_set (sog, color);
		xmlFree (color);
	}

	return FALSE;
}

static gboolean
sheet_object_graphic_write_xml (SheetObject const *so,
				XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);

	xml_set_value_int (tree, "Type", sog->type);
	xml_set_value_int (tree, "Width", sog->width);
	xml_set_value_cstr (tree, "Color", sog->color->str);
	if (so->direction != SO_DIR_UNKNOWN)
		xml_set_value_int (tree, "Direction", so->direction);

	return FALSE;
}

static SheetObject *
sheet_object_graphic_clone (SheetObject const *so, Sheet *sheet)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);
	SheetObjectGraphic *new_sog;

	new_sog = SHEET_OBJECT_GRAPHIC (gtk_type_new (GTK_OBJECT_TYPE (sog)));

	new_sog->type  = sog->type;
	new_sog->width = sog->width;
	new_sog->color = sog->color ? string_ref (sog->color) : NULL;

	return SHEET_OBJECT (new_sog);
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
	sheet_object_class->write_xml	  = sheet_object_graphic_write_xml;
	sheet_object_class->print         = sheet_object_graphic_print;
	sheet_object_class->clone         = sheet_object_graphic_clone;
	sheet_object_class->rubber_band_directly = TRUE;
}

static void
sheet_object_graphic_init (GtkObject *obj)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (obj);
	SheetObject *so = SHEET_OBJECT (obj);
	
	sog->type = SHEET_OBJECT_LINE;
	so->direction = SO_DIR_NONE_MASK;
}

E_MAKE_TYPE (sheet_object_graphic, "SheetObjectGraphic", SheetObjectGraphic,
	     sheet_object_graphic_class_init, sheet_object_graphic_init,
	     SHEET_OBJECT_TYPE);

/************************************************************************/

/*
 * SheetObjectFilled
 *
 * Derivative of SheetObjectGraphic, with filled parameter
 */
#define SHEET_OBJECT_FILLED_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_FILLED_TYPE, SheetObjectFilledClass))

typedef struct {
	SheetObjectGraphic parent_object;

	String      *fill_color;
	int         pattern;
} SheetObjectFilled;

typedef struct {
	SheetObjectGraphicClass parent_class;
} SheetObjectFilledClass;

static SheetObjectGraphicClass *sheet_object_filled_parent_class;

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
sheet_object_box_new (gboolean is_oval)
{
	SheetObject *so;
	SheetObjectFilled *sof;
	SheetObjectGraphic *sog;

	so = gtk_type_new (sheet_object_filled_get_type ());
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
	/* FIXME : this is bogus */
	GnumericCanvas *gcanvas = scg_pane (scg, 0);
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
		gcanvas->object_group, type,
		"fill_color",    sof->fill_color ? sof->fill_color->str : NULL,
		"outline_color", sog->color->str,
		"width_units",   (double) sog->width,
		NULL);

	scg_object_register (so, item);
	return GTK_OBJECT (item);
}

static gboolean
sheet_object_filled_read_xml (SheetObject *so,
			      XmlParseContext const *ctxt, xmlNodePtr tree)
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

	return sheet_object_graphic_read_xml (so, ctxt, tree);
}

static gboolean
sheet_object_filled_write_xml (SheetObject const *so, 
			       XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectFilled *sof = SHEET_OBJECT_FILLED (so);

	if (sof->fill_color != NULL)
		xml_set_value_cstr (tree, "FillColor", sof->fill_color->str);
	xml_set_value_int (tree, "Pattern", sof->pattern);
	return sheet_object_graphic_write_xml (so, ctxt, tree);
}

static SheetObject *
sheet_object_filled_clone (SheetObject const *so, Sheet *sheet)
{
	SheetObjectFilled *sof = SHEET_OBJECT_FILLED (so);
	SheetObjectFilled *new_sof;
	SheetObject *new_so;

	new_so = sheet_object_graphic_clone (so, sheet);
	new_sof = SHEET_OBJECT_FILLED (new_so);

	new_sof->pattern    = sof->pattern;
	new_sof->fill_color = sof->fill_color ? string_ref (sof->fill_color) : NULL;

	return SHEET_OBJECT (new_sof);
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
	sheet_object_class->write_xml	  = sheet_object_filled_write_xml;
	sheet_object_class->clone         = sheet_object_filled_clone;
	sheet_object_class->rubber_band_directly = TRUE;
}

static void
sheet_object_filled_init (GtkObject *obj)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (obj);
	SheetObject *so = SHEET_OBJECT (obj);
	sog->type = SHEET_OBJECT_BOX;
	so->direction = SO_DIR_UNKNOWN;
}

E_MAKE_TYPE (sheet_object_filled, "SheetObjectFilled", SheetObjectFilled,
	     sheet_object_filled_class_init, sheet_object_filled_init,
	     SHEET_OBJECT_GRAPHIC_TYPE);
