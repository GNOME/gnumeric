/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet-object-graphic.c: Implements the drawing object manipulation for Gnumeric
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "sheet-object-graphic.h"

#include "sheet-control-gui.h"
#include "gnumeric-canvas.h"
#include "gnumeric-pane.h"
#include "str.h"
#include "gui-util.h"
#include "style-color.h"
#include "sheet-object-impl.h"
#include "workbook-edit.h"

#include <libgnome/gnome-i18n.h>
#include <gdk/gdkkeysyms.h>
#include <gsf/gsf-impl-utils.h>
#include <gal/widgets/e-colors.h>
#include <gal/widgets/widget-color-combo.h>
#include <math.h>

#define SHEET_OBJECT_CONFIG_KEY "sheet-object-arrow-key"

/* These are persisted */
typedef enum {
	SHEET_OBJECT_LINE	= 1,
	SHEET_OBJECT_ARROW	= 2,
	SHEET_OBJECT_BOX	= 101,
	SHEET_OBJECT_OVAL	= 102,
} SheetObjectGraphicType;

#define SHEET_OBJECT_GRAPHIC(o)       (G_TYPE_CHECK_INSTANCE_CAST((o), SHEET_OBJECT_GRAPHIC_TYPE, SheetObjectGraphic))
#define SHEET_OBJECT_GRAPHIC_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_OBJECT_GRAPHIC_TYPE, SheetObjectGraphicClass))

typedef struct {
	SheetObject  sheet_object;
	StyleColor  *fill_color;
	double       width;
	double       a, b, c;
	SheetObjectGraphicType type;
} SheetObjectGraphic;
typedef struct {
	SheetObjectClass parent_class;
} SheetObjectGraphicClass;
static SheetObjectClass *sheet_object_graphic_parent_class;

/**
 * sheet_object_graphic_fill_color_set :
 * @so :
 * @color :
 *
 * Absorb the colour reference.
 */
void
sheet_object_graphic_fill_color_set (SheetObject *so, StyleColor *color)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);
	GdkColor *gdk = (color != NULL) ? &color->color : NULL;
	GList *l;

	g_return_if_fail (sog != NULL);

	style_color_unref (sog->fill_color);
	sog->fill_color = color;

	for (l = so->realized_list; l; l = l->next)
		gnome_canvas_item_set (l->data, "fill_color_gdk", gdk, NULL);
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

	sog = g_object_new (SHEET_OBJECT_GRAPHIC_TYPE, NULL);
	sog->type = is_arrow ? SHEET_OBJECT_ARROW : SHEET_OBJECT_LINE;

	return SHEET_OBJECT (sog);
}

static void
sheet_object_graphic_finalize (GObject *object)
{
	SheetObjectGraphic *sog;

	sog = SHEET_OBJECT_GRAPHIC (object);
	style_color_unref (sog->fill_color);

	G_OBJECT_CLASS (sheet_object_graphic_parent_class)->finalize (object);
}

static GObject *
sheet_object_graphic_new_view (SheetObject *so, SheetControl *sc, gpointer key)
{
	GnumericCanvas *gcanvas = ((GnumericPane *)key)->gcanvas;
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);
	GnomeCanvasItem *item = NULL;
	GdkColor *fill_color;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);
	g_return_val_if_fail (IS_SHEET_CONTROL (sc), NULL);
	g_return_val_if_fail (gcanvas != NULL, NULL);

	fill_color = (sog->fill_color != NULL) ? &sog->fill_color->color : NULL;

	gnome_canvas_item_raise_to_top (GNOME_CANVAS_ITEM (gcanvas->sheet_object_group));

	switch (sog->type) {
	case SHEET_OBJECT_LINE:
		item = gnome_canvas_item_new (
			gcanvas->sheet_object_group,
			gnome_canvas_line_get_type (),
			"fill_color_gdk", fill_color,
			"width_units", sog->width,
			NULL);
		break;

	case SHEET_OBJECT_ARROW:
		item = gnome_canvas_item_new (
			gcanvas->sheet_object_group,
			gnome_canvas_line_get_type (),
			"fill_color_gdk", fill_color,
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

	gnm_pane_object_register (so, item);
	return G_OBJECT (item);
}

static void
sheet_object_graphic_update_bounds (SheetObject *so, GObject *view_obj)
{
	GnomeCanvasPoints *points = gnome_canvas_points_new (2);
	GnomeCanvasItem   *view = GNOME_CANVAS_ITEM (view_obj);
	SheetControlGUI	  *scg  =
		SHEET_CONTROL_GUI (sheet_object_view_control (view_obj));

	scg_object_view_position (scg, so, points->coords);
	gnome_canvas_item_set (view, "points", points, NULL);
	gnome_canvas_points_free (points);

	if (so->is_visible)
		gnome_canvas_item_show (view);
	else
		gnome_canvas_item_hide (view);
}

static gboolean
sheet_object_graphic_read_xml (SheetObject *so,
			       XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectGraphic *sog;
	double width, a, b, c;
	int tmp = 0;

	g_return_val_if_fail (IS_SHEET_OBJECT_GRAPHIC (so), TRUE);
	sog = SHEET_OBJECT_GRAPHIC (so);

	sheet_object_graphic_fill_color_set (so,
		xml_node_get_color (tree, "FillColor"));

	if (xml_node_get_int (tree, "Type", &tmp))
		sog->type = tmp;

	xml_node_get_double (tree, "Width", &width);
	sheet_object_graphic_width_set (sog, width);

	if (xml_node_get_double (tree, "ArrowShapeA", &a) &&
	    xml_node_get_double (tree, "ArrowShapeB", &b) &&
	    xml_node_get_double (tree, "ArrowShapeC", &c))
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

	if (sog->fill_color)
		xml_node_set_color (tree, "FillColor", sog->fill_color);
	xml_node_set_int (tree, "Type", sog->type);
	xml_node_set_double (tree, "Width", sog->width, -1);

	if (sog->type == SHEET_OBJECT_ARROW) {
		xml_node_set_double (tree, "ArrowShapeA", sog->a, -1);
		xml_node_set_double (tree, "ArrowShapeB", sog->b, -1);
		xml_node_set_double (tree, "ArrowShapeC", sog->c, -1);
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

	new_sog = g_object_new (G_OBJECT_TYPE (so), NULL);

	new_sog->type  = sog->type;
	new_sog->width = sog->width;
	new_sog->fill_color = style_color_ref (sog->fill_color);
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

	sheet_object_position_pts_get (so, coords);

	gnome_print_gsave (ctx);

	if (sog->fill_color) {
		switch (so->anchor.direction) {
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

		switch (so->anchor.direction) {
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
			sog->fill_color->red   / (double) 0xffff,
			sog->fill_color->green / (double) 0xffff,
			sog->fill_color->blue  / (double) 0xffff);

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
	GladeXML           *gui;
	GtkWidget          *dialog;
	GtkWidget *canvas;
	GnomeCanvasItem *arrow;
	GtkWidget *fill_color_combo;
	GtkSpinButton *spin_arrow_tip;
	GtkSpinButton *spin_arrow_length;
	GtkSpinButton *spin_arrow_width;
	GtkSpinButton *spin_line_width;

	StyleColor *fill_color;
	double width;
	double a, b, c;                   /* Only for arrows */

	WorkbookControlGUI *wbcg;
	SheetObjectGraphic *sog;
	Sheet		   *sheet;
} DialogGraphicData;



static gboolean
cb_dialog_graphic_config_destroy (GtkObject *w, DialogGraphicData *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);

	return FALSE;
}

static void
cb_dialog_graphic_config_ok_clicked (GtkWidget *button, DialogGraphicData *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_dialog_graphic_config_cancel_clicked (GtkWidget *button, DialogGraphicData *state)
{
	SheetObject *so = SHEET_OBJECT (state->sog);

	sheet_object_graphic_width_set (state->sog, state->width);
	sheet_object_graphic_fill_color_set (so,
					     state->fill_color);
	
	if (state->sog->type == SHEET_OBJECT_ARROW)
		sheet_object_graphic_abc_set (state->sog,
					      state->a, state->b, state->c);
	gtk_widget_destroy (state->dialog);
}

static void
cb_adjustment_value_changed (GtkAdjustment *adj, DialogGraphicData *state)
{
	sheet_object_graphic_width_set (state->sog,
					gtk_spin_button_get_adjustment (
						      state->spin_line_width)->value);
	gnome_canvas_item_set (state->arrow,
		"width_units", (double) gtk_spin_button_get_adjustment (
						      state->spin_line_width)->value,
		NULL);

	if (state->sog->type == SHEET_OBJECT_ARROW) {
		sheet_object_graphic_abc_set (state->sog,
					      gtk_spin_button_get_adjustment (
						      state->spin_arrow_tip)->value,
					      gtk_spin_button_get_adjustment (
						      state->spin_arrow_length)->value,
					      gtk_spin_button_get_adjustment (
						      state->spin_arrow_width)->value);

		gnome_canvas_item_set (state->arrow,
				       "arrow_shape_a", (double) gtk_spin_button_get_adjustment (
					       state->spin_arrow_tip)->value,
				       "arrow_shape_b", (double) gtk_spin_button_get_adjustment (
					       state->spin_arrow_length)->value,
				       "arrow_shape_c", (double) gtk_spin_button_get_adjustment (
					       state->spin_arrow_width)->value,
				       NULL);
	}
}

static void
cb_fill_color_changed (ColorCombo *color_combo, GdkColor *color,
		       gboolean is_custom, gboolean by_user, gboolean is_default,
		       DialogGraphicData *state)
{
	SheetObject *so = SHEET_OBJECT (state->sog);
	sheet_object_graphic_fill_color_set (so, color_combo_get_style_color (
						     state->fill_color_combo));
	
	gnome_canvas_item_set (state->arrow, "fill_color_gdk", color, NULL);
}

static void
sheet_object_graphic_user_config (SheetObject *so, SheetControl *sc)
{
	SheetObjectGraphic *sog= SHEET_OBJECT_GRAPHIC (so);
	WorkbookControlGUI *wbcg = scg_get_wbcg (SHEET_CONTROL_GUI (sc));
	DialogGraphicData *state;
	GnomeCanvasPoints *points;
	GtkWidget *table;

	g_return_if_fail (sog != NULL);

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, SHEET_OBJECT_CONFIG_KEY))
		return;

	state = g_new0 (DialogGraphicData, 1);
	state->sog = sog;
	state->wbcg = wbcg;
	state->sheet = sc_sheet	(sc);

	sog = SHEET_OBJECT_GRAPHIC (so);
	state->gui = gnumeric_glade_xml_new (wbcg, "so-arrow.glade");
	state->dialog = glade_xml_get_widget (state->gui, "SO-Arrow");

 	table = glade_xml_get_widget (state->gui, "table");
	state->canvas = gnome_canvas_new ();
	gtk_table_attach_defaults (GTK_TABLE (table), state->canvas,
				   2, 3, 0, (sog->type != SHEET_OBJECT_ARROW) ? 2 : 5);
	gtk_widget_show (GTK_WIDGET (state->canvas));

	state->fill_color_combo = color_combo_new (NULL, NULL, NULL,
					color_group_fetch ("color", so));
	color_combo_set_color (COLOR_COMBO (state->fill_color_combo),
			       sog->fill_color ? &sog->fill_color->color : NULL);
	state->fill_color = style_color_ref (sog->fill_color);
	gtk_table_attach_defaults (GTK_TABLE (table),
				   state->fill_color_combo, 1, 2, 0, 1);
	gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (state->fill_color_combo), 
					GTK_RELIEF_NORMAL);
	color_combo_box_set_preview_relief (COLOR_COMBO (state->fill_color_combo), 
					    GTK_RELIEF_NORMAL);
	gtk_widget_show (GTK_WIDGET (state->fill_color_combo));

	state->spin_arrow_tip = GTK_SPIN_BUTTON (glade_xml_get_widget (
						    state->gui, "spin_arrow_tip"));
	state->spin_arrow_length = GTK_SPIN_BUTTON (glade_xml_get_widget (
						       state->gui, "spin_arrow_length"));
	state->spin_arrow_width = GTK_SPIN_BUTTON (glade_xml_get_widget (
						      state->gui, "spin_arrow_width"));

	state->spin_line_width = GTK_SPIN_BUTTON (glade_xml_get_widget (
						     state->gui, "spin_line_width"));
	state->width = sog->width;
	gtk_spin_button_set_value (state->spin_line_width, state->width);
	state->a = sog->a;
	state->b = sog->b;
	state->c = sog->c;
	gtk_spin_button_set_value (state->spin_arrow_tip, state->a);
	gtk_spin_button_set_value (state->spin_arrow_length, state->b);
	gtk_spin_button_set_value (state->spin_arrow_width, state->c);

	if (sog->type != SHEET_OBJECT_ARROW) {
		gtk_widget_hide (glade_xml_get_widget (state->gui, "label_arrow_tip"));
		gtk_widget_hide (glade_xml_get_widget (state->gui, "label_arrow_length"));
		gtk_widget_hide (glade_xml_get_widget (state->gui, "label_arrow_width"));
		gtk_widget_hide (GTK_WIDGET (state->spin_arrow_tip));
		gtk_widget_hide (GTK_WIDGET (state->spin_arrow_length));
		gtk_widget_hide (GTK_WIDGET (state->spin_arrow_width));
	} 
	gtk_widget_show (state->dialog);

	points = gnome_canvas_points_new (2);
	points->coords [0] = state->canvas->allocation.width / 4.0;
	points->coords [1] = 5.0;
	points->coords [2] = state->canvas->allocation.width - points->coords [0];
	points->coords [3] = state->canvas->allocation.height - points->coords [1];

	if (sog->type != SHEET_OBJECT_ARROW)
		state->arrow = gnome_canvas_item_new (
			gnome_canvas_root (GNOME_CANVAS (state->canvas)),
			GNOME_TYPE_CANVAS_LINE, "points", points,
			"fill_color_gdk", sog->fill_color, NULL);
	else
		state->arrow = gnome_canvas_item_new (
				gnome_canvas_root (GNOME_CANVAS (state->canvas)),
				GNOME_TYPE_CANVAS_LINE, "points", points,
				"fill_color_gdk", sog->fill_color,
				"first_arrowhead", TRUE, NULL);

	gnome_canvas_points_free (points);
	gnome_canvas_set_scroll_region (GNOME_CANVAS (state->canvas),
					0., 0., state->canvas->allocation.width,
					state->canvas->allocation.height);
	cb_adjustment_value_changed (NULL, state);
	g_signal_connect (G_OBJECT 
			  (gtk_spin_button_get_adjustment (state->spin_arrow_tip)),
			  "value_changed",
			  G_CALLBACK (cb_adjustment_value_changed), state);
	g_signal_connect (G_OBJECT 
			  (gtk_spin_button_get_adjustment (state->spin_arrow_length)),
			  "value_changed",
			  G_CALLBACK (cb_adjustment_value_changed), state);
	g_signal_connect (G_OBJECT
			  (gtk_spin_button_get_adjustment (state->spin_arrow_width)),
			  "value_changed",
			  G_CALLBACK (cb_adjustment_value_changed), state);
	g_signal_connect (G_OBJECT (state->fill_color_combo),
			  "color_changed",
			  G_CALLBACK (cb_fill_color_changed), state);
	g_signal_connect (G_OBJECT
			  (gtk_spin_button_get_adjustment (state->spin_line_width)),
			  "value_changed",
			  G_CALLBACK (cb_adjustment_value_changed), state);
	g_signal_connect (G_OBJECT (state->dialog),
			  "destroy",
			  G_CALLBACK (cb_dialog_graphic_config_destroy), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "ok_button")),
			  "clicked",
			  G_CALLBACK (cb_dialog_graphic_config_ok_clicked), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "cancel_button")),
			  "clicked",
			  G_CALLBACK (cb_dialog_graphic_config_cancel_clicked), state);
	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		(sog->type != SHEET_OBJECT_ARROW) ? "so-line.html" : "so-arrow.html");
	
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SHEET_OBJECT_CONFIG_KEY);

	wbcg_edit_attach_guru (state->wbcg, state->dialog);

}

static void
sheet_object_graphic_class_init (GObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class;

	sheet_object_graphic_parent_class = gtk_type_class (SHEET_OBJECT_TYPE);

	/* Object class method overrides */
	object_class->finalize = sheet_object_graphic_finalize;

	/* SheetObject class method overrides */
	sheet_object_class = SHEET_OBJECT_CLASS (object_class);
	sheet_object_class->new_view	  = sheet_object_graphic_new_view;
	sheet_object_class->update_bounds = sheet_object_graphic_update_bounds;
	sheet_object_class->read_xml	  = sheet_object_graphic_read_xml;
	sheet_object_class->write_xml	  = sheet_object_graphic_write_xml;
	sheet_object_class->clone         = sheet_object_graphic_clone;
	sheet_object_class->user_config   = sheet_object_graphic_user_config;
	sheet_object_class->print         = sheet_object_graphic_print;
	sheet_object_class->rubber_band_directly = TRUE;
}

static void
sheet_object_graphic_init (GObject *obj)
{
	SheetObjectGraphic *sog;
	SheetObject *so;

	sog = SHEET_OBJECT_GRAPHIC (obj);
	sog->fill_color = style_color_new_name ("black");
	sog->width = 1.0;
	sog->a = 8.0;
	sog->b = 10.0;
	sog->c = 3.0;

	so = SHEET_OBJECT (obj);
	so->anchor.direction = SO_DIR_NONE_MASK;
}

GSF_CLASS (SheetObjectGraphic, sheet_object_graphic,
	   sheet_object_graphic_class_init, sheet_object_graphic_init,
	   SHEET_OBJECT_TYPE);

/************************************************************************/

/*
 * SheetObjectFilled
 *
 * Derivative of SheetObjectGraphic, with filled parameter
 */
#define SHEET_OBJECT_FILLED(o)       (G_TYPE_CHECK_INSTANCE_CAST((o), SHEET_OBJECT_FILLED_TYPE, SheetObjectFilled))
#define SHEET_OBJECT_FILLED_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_OBJECT_FILLED_TYPE, SheetObjectFilledClass))

typedef struct {
	SheetObjectGraphic sheet_object_graphic;

	StyleColor *outline_color;
} SheetObjectFilled;

typedef struct {
	SheetObjectGraphicClass parent_class;
} SheetObjectFilledClass;

static SheetObjectGraphicClass *sheet_object_filled_parent_class;

void
sheet_object_filled_outline_color_set (SheetObject *so, StyleColor *color)
{
	SheetObjectFilled *sof = SHEET_OBJECT_FILLED (so);
	GdkColor *gdk = (color != NULL) ? &color->color : NULL;
	GList *l;

	g_return_if_fail (sof != NULL);

	style_color_unref (sof->outline_color);
	sof->outline_color = color;

	for (l = so->realized_list; l; l = l->next)
		gnome_canvas_item_set (l->data, "outline_color_gdk", gdk, NULL);
}

SheetObject *
sheet_object_box_new (gboolean is_oval)
{
	SheetObjectFilled *sof;
	SheetObjectGraphic *sog;

	sof = g_object_new (SHEET_OBJECT_FILLED_TYPE, NULL);

	sog = SHEET_OBJECT_GRAPHIC (sof);
	sog->type = is_oval ? SHEET_OBJECT_OVAL : SHEET_OBJECT_BOX;

	return SHEET_OBJECT (sof);
}

static void
sheet_object_filled_finalize (GObject *object)
{
	SheetObjectFilled *sof;

	sof = SHEET_OBJECT_FILLED (object);
	style_color_unref (sof->outline_color);

	G_OBJECT_CLASS (sheet_object_filled_parent_class)->finalize (object);
}

static void
sheet_object_filled_update_bounds (SheetObject *so, GObject *view)
{
	double coords [4];
	SheetControlGUI	  *scg  =
		SHEET_CONTROL_GUI (sheet_object_view_control (view));

	scg_object_view_position (scg, so, coords);

	gnome_canvas_item_set (GNOME_CANVAS_ITEM (view),
		"x1", MIN (coords [0], coords [2]),
		"x2", MAX (coords [0], coords [2]),
		"y1", MIN (coords [1], coords [3]),
		"y2", MAX (coords [1], coords [3]),
		NULL);

	if (so->is_visible)
		gnome_canvas_item_show (GNOME_CANVAS_ITEM (view));
	else
		gnome_canvas_item_hide (GNOME_CANVAS_ITEM (view));
}

static GObject *
sheet_object_filled_new_view (SheetObject *so, SheetControl *sc, gpointer key)
{
	GnumericCanvas *gcanvas = ((GnumericPane *)key)->gcanvas;
	SheetObjectGraphic *sog;
	SheetObjectFilled  *sof;
	GnomeCanvasItem *item;
	GdkColor *fill_color, *outline_color;

	g_return_val_if_fail (IS_SHEET_OBJECT_FILLED (so), NULL);
	g_return_val_if_fail (IS_SHEET_CONTROL (sc), NULL);
	g_return_val_if_fail (gcanvas != NULL, NULL);

	sof = SHEET_OBJECT_FILLED (so);
	sog = SHEET_OBJECT_GRAPHIC (so);

	fill_color = (sog->fill_color != NULL) ? &sog->fill_color->color : NULL;
	outline_color = (sof->outline_color != NULL) ? &sof->outline_color->color : NULL;

	gnome_canvas_item_raise_to_top (GNOME_CANVAS_ITEM (gcanvas->sheet_object_group));

	item = gnome_canvas_item_new (gcanvas->sheet_object_group,
		(sog->type == SHEET_OBJECT_OVAL) ?
					GNOME_TYPE_CANVAS_ELLIPSE :
					GNOME_TYPE_CANVAS_RECT,
		"fill_color_gdk",	fill_color,
		"outline_color_gdk",	outline_color,
		"width_units",		sog->width,
		NULL);

	gnm_pane_object_register (so, item);
	return G_OBJECT (item);
}

static gboolean
sheet_object_filled_read_xml (SheetObject *so,
			      XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectFilled *sof;

	g_return_val_if_fail (IS_SHEET_OBJECT_FILLED (so), TRUE);
	sof = SHEET_OBJECT_FILLED (so);

	sheet_object_filled_outline_color_set (so,
		xml_node_get_color (tree, "OutlineColor"));

	return sheet_object_graphic_read_xml (so, ctxt, tree);
}

static gboolean
sheet_object_filled_write_xml (SheetObject const *so,
			       XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectFilled *sof;

	g_return_val_if_fail (IS_SHEET_OBJECT_FILLED (so), TRUE);
	sof = SHEET_OBJECT_FILLED (so);

	if (sof->outline_color)
		xml_node_set_color (tree, "OutlineColor", sof->outline_color);

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
	GladeXML           *gui;
	GtkWidget          *dialog;

	GtkWidget *fill_color_combo;
	GtkWidget *outline_color_combo;
	GtkSpinButton *spin_border_width;

	StyleColor *outline_color;
	StyleColor *fill_color;
	double width;

	WorkbookControlGUI *wbcg;
	SheetObjectFilled *sof;
	Sheet		   *sheet;
} DialogFilledData;

static gboolean
cb_dialog_filled_config_destroy (GtkObject *w, DialogFilledData *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);

	return FALSE;
}

static void
cb_dialog_filled_adjustment_value_changed (GtkAdjustment *adj, DialogFilledData *state)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (state->sof);

	sheet_object_graphic_width_set (sog,
					gtk_spin_button_get_adjustment (
						      state->spin_border_width)->value);
}

static void
cb_dialog_filled_color_changed (ColorCombo *color_combo, GdkColor *color,
		       gboolean is_custom, gboolean by_user, gboolean is_default,
		       DialogFilledData *state)
{
	SheetObject *so = SHEET_OBJECT (state->sof);
	sheet_object_graphic_fill_color_set (so, color_combo_get_style_color (
						     state->fill_color_combo));
	sheet_object_filled_outline_color_set (so,
					       color_combo_get_style_color (
						       state->outline_color_combo));
}

static void
cb_dialog_filled_config_ok_clicked (GtkWidget *button, DialogFilledData *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_dialog_filled_config_cancel_clicked (GtkWidget *button, DialogFilledData *state)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (state->sof);
	SheetObject *so = SHEET_OBJECT (state->sof);

	sheet_object_graphic_width_set (sog, state->width);
	sheet_object_graphic_fill_color_set (so,
					     state->fill_color);
	sheet_object_filled_outline_color_set (so,
					       state->outline_color);
	
	gtk_widget_destroy (state->dialog);
}

static void
sheet_object_filled_user_config (SheetObject *so, SheetControl *sc)
{
	SheetObjectFilled *sof = SHEET_OBJECT_FILLED (so);
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);
	WorkbookControlGUI *wbcg = scg_get_wbcg (SHEET_CONTROL_GUI (sc));
	GtkWidget *table;
	DialogFilledData *state;

	g_return_if_fail (IS_SHEET_OBJECT_FILLED (so));

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, SHEET_OBJECT_CONFIG_KEY))
		return;

	state = g_new0 (DialogFilledData, 1);
	state->sof = sof;
	state->wbcg = wbcg;
	state->sheet = sc_sheet	(sc);

	state->gui = gnumeric_glade_xml_new (wbcg, "so-fill.glade");
	state->dialog = glade_xml_get_widget (state->gui, "SO-Fill");

 	table = glade_xml_get_widget (state->gui, "table");

	state->outline_color_combo = color_combo_new (NULL, _("Transparent"),
				NULL, color_group_fetch ("outline_color", so));
	color_combo_set_color (COLOR_COMBO (state->outline_color_combo),
			       sof->outline_color ? &sof->outline_color->color : NULL);
	state->outline_color = style_color_ref (sof->outline_color);
	gtk_table_attach_defaults (GTK_TABLE (table),
				   state->outline_color_combo, 1, 2, 0, 1);
	gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (state->outline_color_combo), 
					GTK_RELIEF_NORMAL);
	color_combo_box_set_preview_relief (COLOR_COMBO (state->outline_color_combo), 
					    GTK_RELIEF_NORMAL);
	gtk_widget_show (GTK_WIDGET (state->outline_color_combo));


	state->fill_color_combo = color_combo_new (NULL, _("Transparent"),
				NULL, color_group_fetch ("fill_color", so));
	color_combo_set_color (COLOR_COMBO (state->fill_color_combo),
			       sog->fill_color ? &sog->fill_color->color : NULL);
	state->fill_color = style_color_ref (sog->fill_color);
	gtk_table_attach_defaults (GTK_TABLE (table),
				   state->fill_color_combo, 1, 2, 1, 2);
	gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (state->fill_color_combo), 
					GTK_RELIEF_NORMAL);
	color_combo_box_set_preview_relief (COLOR_COMBO (state->fill_color_combo), 
					    GTK_RELIEF_NORMAL);
	gtk_widget_show (GTK_WIDGET (state->fill_color_combo));

	state->spin_border_width = GTK_SPIN_BUTTON (glade_xml_get_widget (
						     state->gui, "spin_border_width"));
	state->width = sog->width;
	gtk_spin_button_set_value (state->spin_border_width, state->width);


	g_signal_connect (G_OBJECT (state->fill_color_combo),
			  "color_changed",
			  G_CALLBACK (cb_dialog_filled_color_changed), state);
	g_signal_connect (G_OBJECT (state->outline_color_combo),
			  "color_changed",
			  G_CALLBACK (cb_dialog_filled_color_changed), state);
	g_signal_connect (G_OBJECT
			  (gtk_spin_button_get_adjustment (state->spin_border_width)),
			  "value_changed",
			  G_CALLBACK (cb_dialog_filled_adjustment_value_changed), state);
	g_signal_connect (G_OBJECT (state->dialog),
			  "destroy",
			  G_CALLBACK (cb_dialog_filled_config_destroy), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "ok_button")),
			  "clicked",
			  G_CALLBACK (cb_dialog_filled_config_ok_clicked), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "cancel_button")),
			  "clicked",
			  G_CALLBACK (cb_dialog_filled_config_cancel_clicked), state);
	gnumeric_init_help_button (glade_xml_get_widget (state->gui, "help_button"),
				   "so-filled.html");
	
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SHEET_OBJECT_CONFIG_KEY);

	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gtk_widget_show (state->dialog);
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

	sheet_object_position_pts_get (so, coords);

	start_x = base_x;
	start_y = base_y;
	end_x = start_x + (coords [2] - coords [0]);
	end_y = start_y + (coords [3] - coords [1]);

	gnome_print_gsave (ctx);

	if (sof->outline_color) {
		gnome_print_setlinewidth (ctx, sog->width);
		gnome_print_setrgbcolor (ctx,
			sof->outline_color->red   / (double) 0xffff,
			sof->outline_color->green / (double) 0xffff,
			sof->outline_color->blue  / (double) 0xffff);
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
			sog->fill_color->red   / (double) 0xffff,
			sog->fill_color->green / (double) 0xffff,
			sog->fill_color->blue  / (double) 0xffff);
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
sheet_object_filled_class_init (GObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class;

	sheet_object_filled_parent_class = gtk_type_class (SHEET_OBJECT_GRAPHIC_TYPE);

	object_class->finalize		  = sheet_object_filled_finalize;

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
sheet_object_filled_init (GObject *obj)
{
	SheetObjectFilled *sof;

	sof = SHEET_OBJECT_FILLED (obj);
	sof->outline_color = style_color_new_name ("black");
	sheet_object_graphic_fill_color_set (SHEET_OBJECT (sof),
		style_color_new_name ("white"));
}

GSF_CLASS (SheetObjectFilled, sheet_object_filled,
	   sheet_object_filled_class_init, sheet_object_filled_init,
	   SHEET_OBJECT_GRAPHIC_TYPE);

/************************************************************************/

#define SHEET_OBJECT_POLYGON(o)       (G_TYPE_CHECK_INSTANCE_CAST((o),	SHEET_OBJECT_POLYGON_TYPE, SheetObjectPolygon))
#define SHEET_OBJECT_POLYGON_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k),	SHEET_OBJECT_POLYGON_TYPE, SheetObjectPolygonClass))

typedef struct {
	SheetObject  sheet_object;
	StyleColor  *fill_color;
	StyleColor  *outline_color;
	double       outline_width;
	GnomeCanvasPoints *points;
} SheetObjectPolygon;
typedef struct {
	SheetObjectClass parent_class;
} SheetObjectPolygonClass;
static SheetObjectClass *sheet_object_polygon_parent_class;

SheetObject *
sheet_object_polygon_new (void)
{
	return g_object_new (SHEET_OBJECT_POLYGON_TYPE, NULL);
}

static void
sheet_object_polygon_finalize (GObject *object)
{
	SheetObjectPolygon *sop = SHEET_OBJECT_POLYGON (object);
	style_color_unref (sop->fill_color);
	style_color_unref (sop->outline_color);

	G_OBJECT_CLASS (sheet_object_polygon_parent_class)->finalize (object);
}

static GObject *
sheet_object_polygon_new_view (SheetObject *so, SheetControl *sc, gpointer key)
{
	GnumericCanvas *gcanvas = ((GnumericPane *)key)->gcanvas;
	SheetObjectPolygon *sop = SHEET_OBJECT_POLYGON (so);
	GnomeCanvasItem *item = NULL;
	GdkColor *fill_color, *outline_color;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);
	g_return_val_if_fail (IS_SHEET_CONTROL (sc), NULL);
	g_return_val_if_fail (gcanvas != NULL, NULL);

	fill_color = (sop->fill_color != NULL) ? &sop->fill_color->color : NULL;
	outline_color = (sop->outline_color != NULL) ? &sop->outline_color->color : NULL;

	gnome_canvas_item_raise_to_top (GNOME_CANVAS_ITEM (gcanvas->sheet_object_group));

	item = gnome_canvas_item_new (
		gcanvas->sheet_object_group,
		gnome_canvas_line_get_type (),
		"fill_color_gdk",	fill_color,
		"width_units",		sop->outline_width,
		"points",		sop->points,
		NULL);
	gnm_pane_object_register (so, item);
	return G_OBJECT (item);
}

static void
sheet_object_polygon_update_bounds (SheetObject *so, GObject *view_obj)
{
	double scale[6], translate[6], result[6];
	GnomeCanvasPoints *points = gnome_canvas_points_new (2);
	GnomeCanvasItem   *view = GNOME_CANVAS_ITEM (view_obj);
	SheetControlGUI	  *scg  =
		SHEET_CONTROL_GUI (sheet_object_view_control (view_obj));

	scg_object_view_position (scg, so, points->coords);

	art_affine_scale (scale,
		fabs (points->coords[2] - points->coords[0]),
		fabs (points->coords[3] - points->coords[1]));
	art_affine_translate (translate,
		MIN (points->coords[0], points->coords[2]),
		MIN (points->coords[1], points->coords[3]));
	art_affine_multiply (result, scale, translate);

	gnome_canvas_item_affine_absolute (view, result);
	gnome_canvas_points_free (points);

	if (so->is_visible)
		gnome_canvas_item_show (view);
	else
		gnome_canvas_item_hide (view);
}

static gboolean
sheet_object_polygon_read_xml (SheetObject *so,
			       XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectPolygon *sop;

	g_return_val_if_fail (IS_SHEET_OBJECT_POLYGON (so), TRUE);
	sop = SHEET_OBJECT_POLYGON (so);

	return FALSE;
}

static gboolean
sheet_object_polygon_write_xml (SheetObject const *so,
				XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectPolygon *sop;

	g_return_val_if_fail (IS_SHEET_OBJECT_POLYGON (so), TRUE);
	sop = SHEET_OBJECT_POLYGON (so);

	return FALSE;
}

static SheetObject *
sheet_object_polygon_clone (SheetObject const *so, Sheet *sheet)
{
	SheetObjectPolygon *sop;
	SheetObjectPolygon *new_sop;

	g_return_val_if_fail (IS_SHEET_OBJECT_POLYGON (so), NULL);
	sop = SHEET_OBJECT_POLYGON (so);

	new_sop = g_object_new (G_OBJECT_TYPE (so), NULL);

	new_sop->fill_color = style_color_ref (sop->fill_color);
	new_sop->outline_color = style_color_ref (sop->outline_color);

	return SHEET_OBJECT (new_sop);
}

static void
sheet_object_polygon_print (SheetObject const *so, GnomePrintContext *ctx,
			    double base_x, double base_y)
{
}

static void
sheet_object_polygon_class_init (GObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class;

	sheet_object_polygon_parent_class = gtk_type_class (SHEET_OBJECT_TYPE);

	/* Object class method overrides */
	object_class->finalize = sheet_object_polygon_finalize;

	/* SheetObject class method overrides */
	sheet_object_class = SHEET_OBJECT_CLASS (object_class);
	sheet_object_class->new_view	  = sheet_object_polygon_new_view;
	sheet_object_class->update_bounds = sheet_object_polygon_update_bounds;
	sheet_object_class->read_xml	  = sheet_object_polygon_read_xml;
	sheet_object_class->write_xml	  = sheet_object_polygon_write_xml;
	sheet_object_class->clone         = sheet_object_polygon_clone;
	sheet_object_class->user_config   = NULL;
	sheet_object_class->print         = sheet_object_polygon_print;
	sheet_object_class->rubber_band_directly = FALSE;
}

static void
sheet_object_polygon_init (GObject *obj)
{
	SheetObjectPolygon *sop;
	SheetObject *so;

	sop = SHEET_OBJECT_POLYGON (obj);
	sop->fill_color = style_color_new_name ("black");
	sop->outline_color = style_color_new_name ("white");
	sop->outline_width = 1.;
	sop->points = gnome_canvas_points_new (4);

	sop->points->coords[0] = 0.; sop->points->coords[1] = 0.;
	sop->points->coords[2] = 1.; sop->points->coords[3] = 0.;
	sop->points->coords[4] = 1.; sop->points->coords[5] = 1.;
	sop->points->coords[6] = 0.; sop->points->coords[7] = 1.;

	so = SHEET_OBJECT (obj);
	so->anchor.direction = SO_DIR_NONE_MASK;
}
GSF_CLASS (SheetObjectPolygon, sheet_object_polygon,
	   sheet_object_polygon_class_init, sheet_object_polygon_init,
	   SHEET_OBJECT_TYPE);

void
sheet_object_polygon_set_points (SheetObject *so, GArray *pairs)
{
	GnomeCanvasPoints *points;
	unsigned i;
	GList *l;
	SheetObjectPolygon *sop;

	g_return_if_fail (IS_SHEET_OBJECT_POLYGON (so));
	g_return_if_fail (pairs != NULL);

	sop = SHEET_OBJECT_POLYGON (so);

	points = gnome_canvas_points_new (pairs->len / 2);
	for (i = 0 ; i < pairs->len ; i++)
		points->coords [i] = g_array_index (pairs, double, i);
	for (l = so->realized_list; l; l = l->next)
		gnome_canvas_item_set (l->data, "points", points, NULL);
	gnome_canvas_points_free (sop->points);
	sop->points = points;
}
