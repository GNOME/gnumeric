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
#include <math.h>
#include "gnumeric.h"
#include "sheet-control-gui.h"
#include "gnumeric-canvas.h"
#include "str.h"
#include "gnumeric-util.h"
#include "sheet-object-graphic.h"
#include "sheet-object-impl.h"

#include <gal/util/e-util.h>
#include <gal/widgets/e-colors.h>
#include <gal/widgets/widget-color-combo.h>
#include <math.h>

/* These are persisted */
typedef enum {
	SHEET_OBJECT_LINE	= 1,
	SHEET_OBJECT_ARROW	= 2,
	SHEET_OBJECT_BOX	= 101,
	SHEET_OBJECT_OVAL	= 102,
} SheetObjectGraphicType;

#define SHEET_OBJECT_GRAPHIC_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_GRAPHIC_TYPE))

typedef struct {
	SheetObject  parent_object;
	GdkColor    *fill_color;
	double       width;
	double       a, b, c;
	SheetObjectGraphicType type;
} SheetObjectGraphic;
typedef struct {
	SheetObjectClass parent_class;
} SheetObjectGraphicClass;
static SheetObjectClass *sheet_object_graphic_parent_class;

static void
sheet_object_graphic_fill_color_set (SheetObjectGraphic *sog,
				     GdkColor *color)
{
	SheetObject *so = SHEET_OBJECT (sog);
	GList *l;

	sog->fill_color = color;

	for (l = so->realized_list; l; l = l->next)
		gnome_canvas_item_set (l->data, "fill_color_gdk", color, NULL);
}

static void
sheet_object_graphic_width_set (SheetObjectGraphic *sog, double width)
{
	SheetObject *so = SHEET_OBJECT (sog);
	GList *l;

	sog->width = width;
	for (l = so->realized_list; l; l = l->next)
		gnome_canvas_item_set (l->data, "width_units", width,
				       NULL);
}

static void
sheet_object_graphic_abc_set (SheetObjectGraphic *sog, double a, double b,
			      double c)
{
	SheetObject *so = SHEET_OBJECT (sog);
	GList *l;

	sog->a = a;
	sog->b = b;
	sog->c = c;
	for (l = so->realized_list; l; l = l->next)
		gnome_canvas_item_set (l->data, "arrow_shape_a", a,
						"arrow_shape_b", b,
						"arrow_shape_c", c, NULL);
}

SheetObject *
sheet_object_line_new (gboolean is_arrow)
{
	SheetObjectGraphic *sog;

	sog = gtk_type_new (SHEET_OBJECT_GRAPHIC_TYPE);
	sog->type = is_arrow ? SHEET_OBJECT_ARROW : SHEET_OBJECT_LINE;

	return SHEET_OBJECT (sog);
}

static void
sheet_object_graphic_destroy (GtkObject *object)
{
	SheetObjectGraphic *sog;
	
	sog = SHEET_OBJECT_GRAPHIC (object);

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
			"fill_color_gdk", sog->fill_color,
			"width_units", sog->width,
			NULL);
		break;

	case SHEET_OBJECT_ARROW:
		item = gnome_canvas_item_new (
			gcanvas->object_group,
			gnome_canvas_line_get_type (),
			"fill_color_gdk", sog->fill_color,
			"width_units", sog->width,
			"arrow_shape_a", sog->a,
			"arrow_shape_b", sog->b,
			"arrow_shape_c", sog->c,
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
	SheetObjectGraphic *sog;
	double width, a, b, c;
	int tmp = 0;
	GdkColor *color = NULL;

	g_return_val_if_fail (IS_SHEET_OBJECT_GRAPHIC (so), TRUE);
	sog = SHEET_OBJECT_GRAPHIC (so);

	color = xml_get_value_color (tree, "FillColor");
	sheet_object_graphic_fill_color_set (sog, color);

	if (xml_get_value_int (tree, "Type", &tmp))
		sog->type = tmp;

	xml_get_value_double (tree, "Width", &width);
	sheet_object_graphic_width_set (sog, width);

	if (xml_get_value_double (tree, "ArrowShapeA", &a) &&
	    xml_get_value_double (tree, "ArrowShapeB", &b) &&
	    xml_get_value_double (tree, "ArrowShapeC", &c))
		sheet_object_graphic_abc_set (sog, a, b, c);

	return FALSE;
}

static gboolean
sheet_object_graphic_write_xml (SheetObject const *so,
				XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectGraphic *sog;
	
	g_return_val_if_fail (IS_SHEET_OBJECT_GRAPHIC (so), TRUE);
	sog = SHEET_OBJECT_GRAPHIC (so);

	xml_set_value_color (tree, "FillColor", sog->fill_color);
	xml_set_value_int (tree, "Type", sog->type);
	xml_set_value_double (tree, "Width", sog->width, -1);

	if (sog->type == SHEET_OBJECT_ARROW) {
		xml_set_value_double (tree, "ArrowShapeA", sog->a, -1);
		xml_set_value_double (tree, "ArrowShapeB", sog->b, -1);
		xml_set_value_double (tree, "ArrowShapeC", sog->c, -1);
	}

	return FALSE;
}

static SheetObject *
sheet_object_graphic_clone (SheetObject const *so, Sheet *sheet)
{
	SheetObjectGraphic *sog;
	SheetObjectGraphic *new_sog;

	g_return_val_if_fail (IS_SHEET_OBJECT_GRAPHIC (so), NULL);
	sog = SHEET_OBJECT_GRAPHIC (so);

	new_sog = SHEET_OBJECT_GRAPHIC (gtk_type_new (GTK_OBJECT_TYPE (sog)));

	new_sog->type  = sog->type;
	new_sog->width = sog->width;
	new_sog->fill_color = sog->fill_color;
	new_sog->a = sog->a;
	new_sog->b = sog->b;
	new_sog->c = sog->c;

	return SHEET_OBJECT (new_sog);
}

static void
sheet_object_graphic_print (SheetObject const *so, GnomePrintContext *ctx,
			    double base_x, double base_y)
{
	SheetObjectGraphic *sog;
	double coords [4];
	double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;

	g_return_if_fail (IS_SHEET_OBJECT_GRAPHIC (so));
	g_return_if_fail (GNOME_IS_PRINT_CONTEXT (ctx));
	sog = SHEET_OBJECT_GRAPHIC (so);

	sheet_object_position_pts (so, coords);

	gnome_print_gsave (ctx);

	if (sog->fill_color) {
		switch (so->direction) {
		case SO_DIR_UP_RIGHT:
		case SO_DIR_DOWN_RIGHT:
			x1 = base_x;
			x2 = base_x + (coords [2] - coords [0]);
			break;
		case SO_DIR_UP_LEFT:
		case SO_DIR_DOWN_LEFT:
			x1 = base_x + (coords [2] - coords [0]);
			x2 = base_x;
			break;
		default:
			g_warning ("Cannot guess direction!");
			gnome_print_grestore (ctx);
			return;
		}

		switch (so->direction) {
		case SO_DIR_UP_LEFT:
		case SO_DIR_UP_RIGHT:
			y1 = base_y;
			y2 = base_y + (coords [3] - coords [1]);
			break;
		case SO_DIR_DOWN_LEFT:
		case SO_DIR_DOWN_RIGHT:
			y1 = base_y + (coords [3] - coords [1]);
			y2 = base_y;
			break;
		default:
			g_warning ("Cannot guess direction!");
			gnome_print_grestore (ctx);
			return;
		}

		gnome_print_setrgbcolor (ctx,
					 (double) sog->fill_color->red,
					 (double) sog->fill_color->green,
					 (double) sog->fill_color->blue);

		if (sog->type == SHEET_OBJECT_ARROW) {
			double phi;

			phi = atan2 (y2 - y1, x2 - x1) - M_PI_2;

			gnome_print_gsave (ctx);
			gnome_print_translate (ctx, x2, y2);
			gnome_print_rotate (ctx, phi / (2 * M_PI) * 360);
			gnome_print_setlinewidth (ctx, 1.0);
			gnome_print_newpath (ctx);
			gnome_print_moveto (ctx, 0.0, 0.0); 
			gnome_print_lineto (ctx, -sog->c, -sog->b);
			gnome_print_lineto (ctx, 0.0, -sog->a);
			gnome_print_lineto (ctx, sog->c, -sog->b);
			gnome_print_closepath (ctx);
			gnome_print_fill (ctx);
			gnome_print_grestore (ctx);

			/*
			 * Make the line shorter so that the arrow won't be
			 * on top of a (perhaps quite fat) line.
			 */
			x2 += sog->a * sin (phi);
			y2 -= sog->a * cos (phi);
		}

		gnome_print_setlinewidth (ctx, sog->width);
		gnome_print_newpath (ctx);
		gnome_print_moveto (ctx, x1, y1);
		gnome_print_lineto (ctx, x2, y2);
		gnome_print_stroke (ctx);
	}

	gnome_print_grestore (ctx);
}

typedef struct
{
	SheetObjectGraphic *sog;
	GtkWidget *canvas;
	GnomeCanvasItem *arrow;
	GtkWidget *fill_color_combo;
	GtkObject *adj_width;
	GtkObject *adj_a, *adj_b, *adj_c; /* Only for arrows */
	GdkColor *fill_color;
	double width;
	double a, b, c;                   /* Only for arrows */
} DialogGraphicData;

static gboolean
cb_dialog_graphic_close (GnomeDialog *dialog, DialogGraphicData *data)
{
	g_free (data);

	return (FALSE);
}

static void
cb_dialog_graphic_clicked (GnomeDialog *dialog, int button,
			   DialogGraphicData *data)
{
	GdkColor *color;

	switch (button) {
	case 0: /* Ok */
	case 1: /* Apply */
		color = color_combo_get_color (
				COLOR_COMBO (data->fill_color_combo));
		sheet_object_graphic_fill_color_set (data->sog, color);
		sheet_object_graphic_width_set (data->sog,
				GTK_ADJUSTMENT (data->adj_width)->value);
		if (data->sog->type == SHEET_OBJECT_ARROW)
			sheet_object_graphic_abc_set (data->sog, 
				GTK_ADJUSTMENT (data->adj_a)->value,
				GTK_ADJUSTMENT (data->adj_b)->value,
				GTK_ADJUSTMENT (data->adj_c)->value);
		if (button == 0)
			gnome_dialog_close (dialog);
		break;
	case 2: /* Cancel */
		sheet_object_graphic_fill_color_set (data->sog,
						     data->fill_color);
		sheet_object_graphic_width_set (data->sog, data->width);
		if (data->sog->type == SHEET_OBJECT_ARROW)
			sheet_object_graphic_abc_set (data->sog, data->a,
						      data->b, data->c);
		gnome_dialog_close (dialog);
		break;
	default:
		g_warning ("Unhandled button %i.", button);
		break;
	}
}

static void
cb_adjustment_value_changed (GtkAdjustment *adj, DialogGraphicData *data)
{
	gnome_canvas_item_set (data->arrow,
		"arrow_shape_a", (double) GTK_ADJUSTMENT (data->adj_a)->value,
		"arrow_shape_b", (double) GTK_ADJUSTMENT (data->adj_b)->value,
		"arrow_shape_c", (double) GTK_ADJUSTMENT (data->adj_c)->value,
		"width_units", (double) GTK_ADJUSTMENT (data->adj_width)->value,
		NULL);
}

static void
cb_fill_color_changed (ColorCombo *color_combo, GdkColor *color,
		       gboolean by_user, DialogGraphicData *data)
{
	gnome_canvas_item_set (data->arrow, "fill_color_gdk", color, NULL);
}

static void
sheet_object_graphic_user_config (SheetObject *so, SheetControlGUI *scg)
{
	GtkWidget *dialog, *table, *label, *spin, *spin_a, *spin_b, *spin_c; 
	GtkTooltips *tooltips;
	WorkbookControlGUI *wbcg;
	SheetObjectGraphic *sog;
	DialogGraphicData *data;
	GnomeCanvasPoints *points;

	g_return_if_fail (IS_SHEET_OBJECT_GRAPHIC (so));
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	sog = SHEET_OBJECT_GRAPHIC (so);
	wbcg = scg_get_wbcg (scg);

	dialog = gnome_dialog_new (_("Configure line or arrow"),
				   GNOME_STOCK_BUTTON_OK,
				   GNOME_STOCK_BUTTON_APPLY,
				   GNOME_STOCK_BUTTON_CANCEL, NULL);
	gnome_dialog_set_close (GNOME_DIALOG (dialog), FALSE);
	gnome_dialog_set_parent (GNOME_DIALOG (dialog),
				 wb_control_gui_toplevel (wbcg));
	tooltips = gtk_tooltips_new ();

	table = gtk_table_new (3, 5, FALSE);
	gtk_table_set_col_spacings (GTK_TABLE (table), 10);
	gtk_table_set_row_spacings (GTK_TABLE (table), 10);
	gtk_widget_show (table);
	gtk_container_add (GTK_CONTAINER (GNOME_DIALOG (dialog)->vbox), table);

	label = gtk_label_new (_("Color"));
	gtk_widget_show (label);
	gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 0, 1);
	label = gtk_label_new (_("Border width"));
	gtk_widget_show (label);
	gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 1, 2);

	if (sog->type == SHEET_OBJECT_ARROW) {
		label = gtk_label_new (_("Arrow shape a"));
		gtk_widget_show (label);
		gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 2,3);
		label = gtk_label_new (_("Arrow shape b"));
		gtk_widget_show (label);
		gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 3,4);
		label = gtk_label_new (_("Arrow shape c"));
		gtk_widget_show (label);
		gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 4,5);
	}

	data = g_new0 (DialogGraphicData, 1);
	data->sog = sog;

	if (sog->type == SHEET_OBJECT_ARROW) {
		data->canvas = gnome_canvas_new ();
		gtk_widget_show (data->canvas);
		gtk_table_attach_defaults (GTK_TABLE (table), data->canvas,
					   2, 3, 2, 5);
	}

	data->fill_color_combo = color_combo_new (NULL, _("Transparent"), NULL,
					color_group_fetch ("color", so));
	color_combo_set_color (COLOR_COMBO (data->fill_color_combo), 
			       sog->fill_color);
	data->fill_color = sog->fill_color;
	
	data->adj_width = gtk_adjustment_new ((float) sog->width, 1.0,
					      100.0, 1.0, 5.0, 1.0);
	spin = gtk_spin_button_new (GTK_ADJUSTMENT (data->adj_width), 1, 0);
	data->width = sog->width;

	if (sog->type == SHEET_OBJECT_ARROW) {
		data->adj_a = gtk_adjustment_new (sog->a, 1., 100., 1., 5., 1.);
		data->adj_b = gtk_adjustment_new (sog->b, 1., 100., 1., 5., 1.);
		data->adj_c = gtk_adjustment_new (sog->c, 1., 100., 1., 5., 1.);
		spin_a = gtk_spin_button_new (GTK_ADJUSTMENT (data->adj_a),1,0);
		spin_b = gtk_spin_button_new (GTK_ADJUSTMENT (data->adj_b),1,0);
		spin_c = gtk_spin_button_new (GTK_ADJUSTMENT (data->adj_c),1,0);
		data->a = sog->a;
		data->b = sog->b;
		data->c = sog->c;
		gtk_tooltips_set_tip (tooltips, spin_a, _("Distance from tip "
				      "of arrowhead to the center point"),NULL);
		gtk_tooltips_set_tip (tooltips, spin_b, _("Distance from tip "
				      "of arrowhead to trailing point, "
				      "measured along shaft"), NULL);
		gtk_tooltips_set_tip (tooltips, spin_c, _("Distance of "
				      "trailing point from outside edge of "
				      "shaft"), NULL);
		gtk_widget_show (spin_a);
		gtk_widget_show (spin_b);
		gtk_widget_show (spin_c);
		gtk_table_attach_defaults (GTK_TABLE (table), spin_a, 1, 2,2,3);
		gtk_table_attach_defaults (GTK_TABLE (table), spin_b, 1, 2,3,4);
		gtk_table_attach_defaults (GTK_TABLE (table), spin_c, 1, 2,4,5);
	}

	gtk_widget_show (data->fill_color_combo);
	gtk_widget_show (spin);

	gtk_table_attach_defaults (GTK_TABLE (table),
				   data->fill_color_combo, 1, 3, 0, 1);
	gtk_table_attach_defaults (GTK_TABLE (table), spin, 1, 3, 1, 2);

	gtk_widget_show (dialog);

	if (sog->type == SHEET_OBJECT_ARROW) {
		points = gnome_canvas_points_new (2);
		points->coords [0] = data->canvas->allocation.width / 2.0;
		points->coords [1] = 0.0;
		points->coords [2] = points->coords [0];
		points->coords [3] = data->canvas->allocation.height;
		data->arrow = gnome_canvas_item_new (
				gnome_canvas_root (GNOME_CANVAS (data->canvas)),
				GNOME_TYPE_CANVAS_LINE, "points", points, 
				"fill_color_gdk", sog->fill_color,
				"first_arrowhead", TRUE, NULL);
		gnome_canvas_points_free (points);
		gnome_canvas_set_scroll_region (GNOME_CANVAS (data->canvas),
					0., 0., data->canvas->allocation.width,
					data->canvas->allocation.height);
		cb_adjustment_value_changed (NULL, data);

		gtk_signal_connect (GTK_OBJECT (data->adj_width),
				"value_changed",
				GTK_SIGNAL_FUNC (cb_adjustment_value_changed),
				data);
		gtk_signal_connect (GTK_OBJECT (data->adj_a), "value_changed",
				GTK_SIGNAL_FUNC (cb_adjustment_value_changed),
				data);
		gtk_signal_connect (GTK_OBJECT (data->adj_b), "value_changed",
				GTK_SIGNAL_FUNC (cb_adjustment_value_changed),
				data);
		gtk_signal_connect (GTK_OBJECT (data->adj_c), "value_changed",
				GTK_SIGNAL_FUNC (cb_adjustment_value_changed),
				data);
		gtk_signal_connect (GTK_OBJECT (data->fill_color_combo),
				"changed", 
				GTK_SIGNAL_FUNC (cb_fill_color_changed), data);
	}

	gtk_signal_connect (GTK_OBJECT (dialog), "clicked",
			    GTK_SIGNAL_FUNC (cb_dialog_graphic_clicked), data);
	gtk_signal_connect (GTK_OBJECT (dialog), "close",
			    GTK_SIGNAL_FUNC (cb_dialog_graphic_close), data);
}

static void
sheet_object_graphic_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class;

	sheet_object_graphic_parent_class = gtk_type_class (SHEET_OBJECT_TYPE);

	/* Object class method overrides */
	object_class->destroy = sheet_object_graphic_destroy;

	/* SheetObject class method overrides */
	sheet_object_class = SHEET_OBJECT_CLASS (object_class);
	sheet_object_class->update_bounds = sheet_object_graphic_update_bounds;
	sheet_object_class->new_view	  = sheet_object_graphic_new_view;
	sheet_object_class->read_xml	  = sheet_object_graphic_read_xml;
	sheet_object_class->write_xml	  = sheet_object_graphic_write_xml;
	sheet_object_class->print         = sheet_object_graphic_print;
	sheet_object_class->clone         = sheet_object_graphic_clone;
	sheet_object_class->user_config   = sheet_object_graphic_user_config;
	sheet_object_class->rubber_band_directly = TRUE;
}

static void
sheet_object_graphic_init (GtkObject *obj)
{
	SheetObjectGraphic *sog;
	SheetObject *so;
	
	sog = SHEET_OBJECT_GRAPHIC (obj);
	sog->fill_color = g_new0 (GdkColor, 1);
	e_color_alloc_gdk (sog->fill_color);
	sog->width = 1.0;
	sog->a = 8.0;
	sog->b = 10.0;
	sog->c = 3.0;

	so = SHEET_OBJECT (obj);
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
#define IS_SHEET_OBJECT_FILLED_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_FILLED_TYPE, SheetObjectFilledClass))

typedef struct {
	SheetObjectGraphic parent_object;

	GdkColor *outline_color;
} SheetObjectFilled;

typedef struct {
	SheetObjectGraphicClass parent_class;
} SheetObjectFilledClass;

static SheetObjectGraphicClass *sheet_object_filled_parent_class;

static void
sheet_object_filled_outline_color_set (SheetObjectFilled *sof, GdkColor *color)
{
	SheetObject *so = SHEET_OBJECT (sof);
	GList *l;

	sof->outline_color = color;
	for (l = so->realized_list; l; l = l->next)
		gnome_canvas_item_set (l->data, "outline_color_gdk", color,
				       NULL);
}

SheetObject *
sheet_object_box_new (gboolean is_oval)
{
	SheetObjectFilled *sof;
	SheetObjectGraphic *sog;

	sof = gtk_type_new (SHEET_OBJECT_FILLED_TYPE);

	sog = SHEET_OBJECT_GRAPHIC (sof);
	sog->type = is_oval ? SHEET_OBJECT_OVAL : SHEET_OBJECT_BOX;

	return SHEET_OBJECT (sof);
}

static void
sheet_object_filled_destroy (GtkObject *object)
{
	SheetObjectFilled *sof;
	
	sof = SHEET_OBJECT_FILLED (object);

	GTK_OBJECT_CLASS (sheet_object_filled_parent_class)->destroy (object);
}

static void
sheet_object_filled_update_bounds (SheetObject *so, GtkObject *view,
				   SheetControlGUI *scg)
{
	double coords [4];

	scg_object_view_position (scg, so, coords);
	
	gnome_canvas_item_set (GNOME_CANVAS_ITEM (view),
		"x1", MIN (coords [0], coords [2]),
		"x2", MAX (coords [0], coords [2]),
		"y1", MIN (coords [1], coords [3]),
		"y2", MAX (coords [1], coords [3]),
		NULL);
}

static GtkObject *
sheet_object_filled_new_view (SheetObject *so, SheetControlGUI *scg)
{
	/* FIXME : this is bogus */
	GnumericCanvas *gcanvas = scg_pane (scg, 0);
	SheetObjectGraphic *sog;
	SheetObjectFilled  *sof;
	GnomeCanvasItem *item;

	g_return_val_if_fail (IS_SHEET_OBJECT_FILLED (so), NULL);
	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), NULL);
	sof = SHEET_OBJECT_FILLED (so);
	sog = SHEET_OBJECT_GRAPHIC (so);

	item = gnome_canvas_item_new (
		gcanvas->object_group,
		(sog->type == SHEET_OBJECT_OVAL) ?
					gnome_canvas_ellipse_get_type () : 
					gnome_canvas_rect_get_type (),
		"fill_color_gdk", sog->fill_color,
		"outline_color_gdk", sof->outline_color,
		"width_units", sog->width, NULL);

	scg_object_register (so, item);
	return GTK_OBJECT (item);
}

static gboolean
sheet_object_filled_read_xml (SheetObject *so,
			      XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectFilled *sof;
	GdkColor *color = NULL;

	g_return_val_if_fail (IS_SHEET_OBJECT_FILLED (so), TRUE);
	sof = SHEET_OBJECT_FILLED (so);

	color = xml_get_value_color (tree, "OutlineColor");
	sheet_object_filled_outline_color_set (sof, color);

	return sheet_object_graphic_read_xml (so, ctxt, tree);
}

static gboolean
sheet_object_filled_write_xml (SheetObject const *so, 
			       XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectFilled *sof;
	
	g_return_val_if_fail (IS_SHEET_OBJECT_FILLED (so), TRUE);
	sof = SHEET_OBJECT_FILLED (so);

	xml_set_value_color (tree, "OutlineColor", sof->outline_color);

	return sheet_object_graphic_write_xml (so, ctxt, tree);
}

static SheetObject *
sheet_object_filled_clone (SheetObject const *so, Sheet *sheet)
{
	SheetObjectFilled *sof;
	SheetObjectFilled *new_sof;
	SheetObject *new_so;

	g_return_val_if_fail (IS_SHEET_OBJECT_FILLED (so), NULL);
	sof = SHEET_OBJECT_FILLED (so);

	new_so = sheet_object_graphic_clone (so, sheet);
	new_sof = SHEET_OBJECT_FILLED (new_so);

	new_sof->outline_color = sof->outline_color;

	return SHEET_OBJECT (new_sof);
}

typedef struct 
{
	SheetObjectFilled *sof;
	GtkWidget *fill_color_combo;
	GtkWidget *outline_color_combo;
	GtkObject *adj_width;
	GdkColor *outline_color;
	GdkColor *fill_color;
	double width;
} DialogFilledData;

static gboolean
cb_dialog_filled_close (GnomeDialog *dialog, DialogFilledData *data)
{
	g_free (data);

	return (FALSE);
}

static void
cb_dialog_filled_clicked (GnomeDialog *dialog, int button,
			  DialogFilledData *data)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (data->sof);
	GdkColor *color;

	switch (button) {
	case 0: /* Ok */
	case 1: /* Apply */
		color = color_combo_get_color (
				COLOR_COMBO (data->outline_color_combo));
		sheet_object_filled_outline_color_set (data->sof, color);
		color = color_combo_get_color (
				COLOR_COMBO (data->fill_color_combo));
		sheet_object_graphic_fill_color_set (sog, color);
		sheet_object_graphic_width_set (sog, 
				GTK_ADJUSTMENT (data->adj_width)->value);
		if (button == 0)
			gnome_dialog_close (dialog);
		break;
	case 2: /* Cancel */
		sheet_object_graphic_fill_color_set (sog, data->fill_color);
		sheet_object_graphic_width_set (sog, data->width);
		sheet_object_filled_outline_color_set (data->sof, 
						       data->outline_color);
		gnome_dialog_close (dialog);
		break;
	default:
		g_warning ("Unhandled button %i.", button);
		break;
	}
}

static void
sheet_object_filled_user_config (SheetObject *so, SheetControlGUI *scg)
{
	WorkbookControlGUI *wbcg;
	GtkWidget *dialog, *table, *label, *spin; 
	SheetObjectFilled *sof;
	SheetObjectGraphic *sog;
	DialogFilledData *data;

	g_return_if_fail (IS_SHEET_OBJECT_FILLED (so));
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	sof = SHEET_OBJECT_FILLED (so);
	sog = SHEET_OBJECT_GRAPHIC (so);
	wbcg = scg_get_wbcg (scg);

	dialog = gnome_dialog_new (_("Configure filled object"),
				   GNOME_STOCK_BUTTON_OK,
				   GNOME_STOCK_BUTTON_APPLY,
				   GNOME_STOCK_BUTTON_CANCEL, NULL);
	gnome_dialog_set_close (GNOME_DIALOG (dialog), FALSE);
	gnome_dialog_set_parent (GNOME_DIALOG (dialog),
				 wb_control_gui_toplevel (wbcg));

	table = gtk_table_new (2, 3, FALSE);
	gtk_table_set_col_spacings (GTK_TABLE (table), 10);
	gtk_table_set_row_spacings (GTK_TABLE (table), 10);
	gtk_widget_show (table);
	gtk_container_add (GTK_CONTAINER (GNOME_DIALOG (dialog)->vbox), table);
	label = gtk_label_new (_("Outline color"));
	gtk_widget_show (label);
	gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 0, 1);
	label = gtk_label_new (_("Fill color"));
	gtk_widget_show (label);
	gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 1, 2);
	label = gtk_label_new (_("Border width"));
	gtk_widget_show (label);
	gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 2, 3);

	data = g_new0 (DialogFilledData, 1);
	data->sof = sof;

	data->outline_color_combo = color_combo_new (NULL, _("Transparent"),
				NULL, color_group_fetch ("outline_color", so));
	color_combo_set_color (COLOR_COMBO (data->outline_color_combo),
			       sof->outline_color);
	data->outline_color = sof->outline_color;

	data->fill_color_combo = color_combo_new (NULL, _("Transparent"),
				NULL, color_group_fetch ("fill_color", so));
	color_combo_set_color (COLOR_COMBO (data->fill_color_combo), 
			       sog->fill_color);
	data->fill_color = sog->fill_color;
	
	data->adj_width = gtk_adjustment_new ((float) sog->width, 1.0,
					      100.0, 1.0, 5.0, 1.0);
	spin = gtk_spin_button_new (GTK_ADJUSTMENT (data->adj_width), 1, 0);
	data->width = sog->width;

	gtk_widget_show (data->fill_color_combo);
	gtk_widget_show (data->outline_color_combo);
	gtk_widget_show (spin);

	gtk_table_attach_defaults (GTK_TABLE (table),
				   data->outline_color_combo, 1, 2, 0, 1);
	gtk_table_attach_defaults (GTK_TABLE (table),
				   data->fill_color_combo, 1, 2, 1, 2);
	gtk_table_attach_defaults (GTK_TABLE (table), spin, 1, 2, 2, 3);

	gtk_signal_connect (GTK_OBJECT (dialog), "clicked",
			    GTK_SIGNAL_FUNC (cb_dialog_filled_clicked), data);
	gtk_signal_connect (GTK_OBJECT (dialog), "close",
			    GTK_SIGNAL_FUNC (cb_dialog_filled_close), data);

	gtk_widget_show (dialog);
}

static void
make_rect (GnomePrintContext *ctx, double x1, double x2, double y1, double y2)
{
	gnome_print_moveto (ctx, x1, y1);
	gnome_print_lineto (ctx, x2, y1);
	gnome_print_lineto (ctx, x2, y2);
	gnome_print_lineto (ctx, x1, y2);
	gnome_print_lineto (ctx, x1, y1);
}

/*
 * The following lines are copy and paste from dia. The ellipse logic has been 
 * slightly adapted. I have _no_ idea what's going on...
 */

/* This constant is equal to sqrt(2)/3*(8*cos(pi/8) - 4 - 1/sqrt(2)) - 1.
 * Its use should be quite obvious.
 */
#define ELLIPSE_CTRL1 0.26521648984
/* this constant is equal to 1/sqrt(2)*(1-ELLIPSE_CTRL1).
 * Its use should also be quite obvious.
 */
#define ELLIPSE_CTRL2 0.519570402739
#define ELLIPSE_CTRL3 M_SQRT1_2
/* this constant is equal to 1/sqrt(2)*(1+ELLIPSE_CTRL1).
 * Its use should also be quite obvious.
 */
#define ELLIPSE_CTRL4 0.894643159635

static void
make_ellipse (GnomePrintContext *ctx,
	      double x1, double x2, double y1, double y2)
{
	double width  = x2 - x1;
	double height = y2 - y1;
	double center_x = x1 + width / 2.0;
	double center_y = y1 + height / 2.0;
	double cw1 = ELLIPSE_CTRL1 * width / 2.0;
	double cw2 = ELLIPSE_CTRL2 * width / 2.0;
	double cw3 = ELLIPSE_CTRL3 * width / 2.0;
	double cw4 = ELLIPSE_CTRL4 * width / 2.0;
	double ch1 = ELLIPSE_CTRL1 * height / 2.0;
	double ch2 = ELLIPSE_CTRL2 * height / 2.0;
	double ch3 = ELLIPSE_CTRL3 * height / 2.0;
	double ch4 = ELLIPSE_CTRL4 * height / 2.0;

	gnome_print_moveto (ctx, x1, center_y);
	gnome_print_curveto (ctx,
			     x1,             center_y - ch1,
			     center_x - cw4, center_y - ch2,
			     center_x - cw3, center_y - ch3);
	gnome_print_curveto (ctx,
			     center_x - cw2, center_y - ch4,
			     center_x - cw1, y1,
			     center_x,       y1);
	gnome_print_curveto (ctx,
			     center_x + cw1, y1,
			     center_x + cw2, center_y - ch4,
			     center_x + cw3, center_y - ch3);
	gnome_print_curveto (ctx,
			     center_x + cw4, center_y - ch2,
			     x2,             center_y - ch1,
			     x2,             center_y);
	gnome_print_curveto (ctx,
			     x2,             center_y + ch1,
			     center_x + cw4, center_y + ch2,
			     center_x + cw3, center_y + ch3);
	gnome_print_curveto (ctx,
			     center_x + cw2, center_y + ch4,
			     center_x + cw1, y2,
			     center_x,       y2);
	gnome_print_curveto (ctx,
			     center_x - cw1, y2,
			     center_x - cw2, center_y + ch4,
			     center_x - cw3, center_y + ch3);
	gnome_print_curveto (ctx,
			     center_x - cw4, center_y + ch2,
			     x1,             center_y + ch1,
			     x1,             center_y); 
}

static void
sheet_object_filled_print (SheetObject const *so, GnomePrintContext *ctx,
			   double base_x, double base_y)
{
	SheetObjectFilled *sof;
	SheetObjectGraphic *sog;
	double coords [4];
	double start_x, start_y;
	double end_x, end_y;

	g_return_if_fail (IS_SHEET_OBJECT_FILLED (so));
	g_return_if_fail (GNOME_IS_PRINT_CONTEXT (ctx));
	sof = SHEET_OBJECT_FILLED (so);
	sog = SHEET_OBJECT_GRAPHIC (so);

	sheet_object_position_pts (so, coords);

	start_x = base_x;
	start_y = base_y;
	end_x = start_x + (coords [2] - coords [0]);
	end_y = start_y + (coords [3] - coords [1]);

	gnome_print_gsave (ctx);

	if (sof->outline_color) {
		gnome_print_setlinewidth (ctx, sog->width);
		gnome_print_setrgbcolor (ctx,
					 (double) sof->outline_color->red,
					 (double) sof->outline_color->green,
					 (double) sof->outline_color->blue); 
		gnome_print_newpath (ctx);
		if (sog->type == SHEET_OBJECT_OVAL)
			make_ellipse (ctx, start_x, end_x, start_y, end_y);
		else
			make_rect (ctx, start_x, end_x, start_y, end_y);
		gnome_print_closepath (ctx);
		gnome_print_stroke (ctx);
	}

	if (sog->fill_color) {
		gnome_print_setrgbcolor (ctx,
					 (double) sog->fill_color->red,
					 (double) sog->fill_color->green,
					 (double) sog->fill_color->blue);
		gnome_print_newpath (ctx);
		if (sog->type == SHEET_OBJECT_OVAL)
			make_ellipse (ctx, start_x, end_x, start_y, end_y);
		else
			make_rect (ctx, start_x, end_x, start_y, end_y);
		gnome_print_fill (ctx);
	}

	gnome_print_grestore (ctx);
}

static void
sheet_object_filled_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class;

	sheet_object_filled_parent_class = gtk_type_class (SHEET_OBJECT_GRAPHIC_TYPE);

	object_class->destroy		  = sheet_object_filled_destroy;

	sheet_object_class = SHEET_OBJECT_CLASS (object_class);
	sheet_object_class->new_view	  = sheet_object_filled_new_view;
	sheet_object_class->update_bounds = sheet_object_filled_update_bounds;
	sheet_object_class->read_xml	  = sheet_object_filled_read_xml;
	sheet_object_class->write_xml	  = sheet_object_filled_write_xml;
	sheet_object_class->clone         = sheet_object_filled_clone;
	sheet_object_class->user_config   = sheet_object_filled_user_config;
	sheet_object_class->print         = sheet_object_filled_print;
	sheet_object_class->rubber_band_directly = TRUE;
}

static void
sheet_object_filled_init (GtkObject *obj)
{
	SheetObjectFilled *sof;
	
	sof = SHEET_OBJECT_FILLED (obj);
	sof->outline_color = g_new0 (GdkColor, 1);
	e_color_alloc_gdk (sof->outline_color);
}

E_MAKE_TYPE (sheet_object_filled, "SheetObjectFilled", SheetObjectFilled,
	     sheet_object_filled_class_init, sheet_object_filled_init,
	     SHEET_OBJECT_GRAPHIC_TYPE);
