/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet-object-graphic.c: Implements the drawing object manipulation for Gnumeric
 *
 * Authors:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Jody Goldberg (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
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
#include "dialogs/help.h"
#include "xml-io.h"
#include <goffice/gui-utils/go-combo-color.h>
#include <goffice/gui-utils/go-combo-box.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtklabel.h>
#include <gtk/gtktable.h>
#include <gtk/gtkspinbutton.h>
#include <gsf/gsf-impl-utils.h>
#include <libfoocanvas/foo-canvas-line.h>
#include <libfoocanvas/foo-canvas-rect-ellipse.h>
#include <libfoocanvas/foo-canvas-polygon.h>
#include <libfoocanvas/foo-canvas-text.h>

#include <libart_lgpl/art_affine.h>
#include <math.h>

#define SHEET_OBJECT_CONFIG_KEY "sheet-object-arrow-key"

/* These are persisted */
typedef enum {
	SHEET_OBJECT_LINE	= 1,
	SHEET_OBJECT_ARROW	= 2,
	SHEET_OBJECT_BOX	= 101,
	SHEET_OBJECT_OVAL	= 102
} SheetObjectGraphicType;

#define IS_SHEET_OBJECT_GRAPHIC(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_GRAPHIC_TYPE))
#define SHEET_OBJECT_GRAPHIC(o)       (G_TYPE_CHECK_INSTANCE_CAST((o), SHEET_OBJECT_GRAPHIC_TYPE, SheetObjectGraphic))
#define SHEET_OBJECT_GRAPHIC_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_OBJECT_GRAPHIC_TYPE, SheetObjectGraphicClass))

typedef struct {
	SheetObject  sheet_object;
	GnmColor  *fill_color;
	double       width;
	double       a, b, c;
	SheetObjectGraphicType type;
} SheetObjectGraphic;
typedef struct {
	SheetObjectClass parent_class;
	FooCanvasItem *(*get_graphic) (FooCanvasItem *item);
} SheetObjectGraphicClass;
static SheetObjectClass *sheet_object_graphic_parent_class;

static FooCanvasItem *
sheet_object_graphic_get_graphic (SheetObject *so, FooCanvasItem *item)
{
	SheetObjectGraphicClass *sog_class = SHEET_OBJECT_GRAPHIC_CLASS (G_OBJECT_GET_CLASS (so));
	if (sog_class->get_graphic != NULL)
		return (*sog_class->get_graphic) (item);
	return item;
}

/**
 * gnm_so_graphic_set_fill_color :
 * @so :
 * @color :
 *
 * Absorb the colour reference.
 */
void
gnm_so_graphic_set_fill_color (SheetObject *so, GnmColor *color)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);
	GdkColor *gdk = (color != NULL) ? &color->color : NULL;
	GList *l;

	g_return_if_fail (sog != NULL);

	style_color_unref (sog->fill_color);
	sog->fill_color = color;

	for (l = so->realized_list; l; l = l->next)
		foo_canvas_item_set (sheet_object_graphic_get_graphic (so, l->data),
			"fill_color_gdk", gdk,
			NULL);
}

void
gnm_so_graphic_set_width (SheetObject *so, double width)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);
	GList *l;

	sog->width = width;
	for (l = so->realized_list; l; l = l->next)
		foo_canvas_item_set (sheet_object_graphic_get_graphic (so, l->data),
			"width_units", width,
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
		foo_canvas_item_set (sheet_object_graphic_get_graphic (so, l->data),
			"arrow_shape_a", a,
			"arrow_shape_b", b,
			"arrow_shape_c", c,
			NULL);
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
	GnmCanvas *gcanvas = ((GnumericPane *)key)->gcanvas;
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);
	FooCanvasItem *item = NULL;
	GdkColor *fill_color;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);
	g_return_val_if_fail (IS_SHEET_CONTROL (sc), NULL);
	g_return_val_if_fail (gcanvas != NULL, NULL);

	fill_color = (sog->fill_color != NULL) ? &sog->fill_color->color : NULL;

	foo_canvas_item_raise_to_top (FOO_CANVAS_ITEM (gcanvas->sheet_object_group));

	switch (sog->type) {
	case SHEET_OBJECT_LINE:
		item = foo_canvas_item_new (
			gcanvas->sheet_object_group,
			foo_canvas_line_get_type (),
			"fill_color_gdk", fill_color,
			"width_units", sog->width,
			NULL);
		break;

	case SHEET_OBJECT_ARROW:
		item = foo_canvas_item_new (
			gcanvas->sheet_object_group,
			foo_canvas_line_get_type (),
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
	FooCanvasPoints *points = foo_canvas_points_new (2);
	FooCanvasItem   *view = FOO_CANVAS_ITEM (view_obj);
	SheetControlGUI	  *scg  =
		SHEET_CONTROL_GUI (sheet_object_view_control (view_obj));

	scg_object_view_position (scg, so, points->coords);
	foo_canvas_item_set (view, "points", points, NULL);
	foo_canvas_points_free (points);

	if (so->is_visible)
		foo_canvas_item_show (view);
	else
		foo_canvas_item_hide (view);
}

static gboolean
sheet_object_graphic_read_xml_dom (SheetObject *so, char const *typename,
				   XmlParseContext const *ctxt,
				   xmlNodePtr tree)
{
	SheetObjectGraphic *sog;
	double width, a, b, c;
	int tmp = 0;

	g_return_val_if_fail (IS_SHEET_OBJECT_GRAPHIC (so), TRUE);
	sog = SHEET_OBJECT_GRAPHIC (so);

	gnm_so_graphic_set_fill_color (so,
		xml_node_get_color (tree, "FillColor"));

	if (xml_node_get_int (tree, "Type", &tmp))
		sog->type = tmp;

	xml_node_get_double (tree, "Width", &width);
	gnm_so_graphic_set_width (so, width);

	if (xml_node_get_double (tree, "ArrowShapeA", &a) &&
	    xml_node_get_double (tree, "ArrowShapeB", &b) &&
	    xml_node_get_double (tree, "ArrowShapeC", &c))
		sheet_object_graphic_abc_set (sog, a, b, c);

	return FALSE;
}

static gboolean
sheet_object_graphic_write_xml_dom (SheetObject const *so,
				    XmlParseContext const *ctxt,
				    xmlNodePtr tree)
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
			    double width, double height)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);
	double x1, y1, x2, y2;

	if (sog->fill_color == NULL)
		return;

	switch (so->anchor.direction) {
	case SO_DIR_UP_RIGHT:
	case SO_DIR_DOWN_RIGHT:
		x1 = 0.;
		x2 = width;
		break;
	case SO_DIR_UP_LEFT:
	case SO_DIR_DOWN_LEFT:
		x1 = width;
		x2 = 0.;
		break;
	default:
		g_warning ("Cannot guess direction!");
		return;
	}

	switch (so->anchor.direction) {
	case SO_DIR_UP_LEFT:
	case SO_DIR_UP_RIGHT:
		y1 = -height;
		y2 = 0.;
		break;
	case SO_DIR_DOWN_LEFT:
	case SO_DIR_DOWN_RIGHT:
		y1 = 0.;
		y2 = -height;
		break;
	default:
		g_warning ("Cannot guess direction!");
		return;
	}

	gnome_print_setrgbcolor (ctx,
		sog->fill_color->color.red   / (double) 0xffff,
		sog->fill_color->color.green / (double) 0xffff,
		sog->fill_color->color.blue  / (double) 0xffff);

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

typedef struct {
	GladeXML           *gui;
	GtkWidget          *dialog;
	GtkWidget	*canvas;
	FooCanvasItem	*arrow;
	GtkSpinButton	*spin_arrow_tip;
	GtkSpinButton	*spin_arrow_length;
	GtkSpinButton	*spin_arrow_width;
	GtkSpinButton	*spin_line_width;

	/* Store the initial values */
	GnmColor	*fill_color;
	double		 width, a, b, c;

	WorkbookControlGUI *wbcg;
	SheetObjectGraphic *sog;
} DialogGraphicData;

static void
cb_dialog_graphic_config_destroy (DialogGraphicData *state)
{
	wbcg_edit_detach_guru (state->wbcg);
	if (state->fill_color != NULL) {
		style_color_unref (state->fill_color);
		state->fill_color = NULL;
	}
	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}
	state->dialog = NULL;
	g_free (state);
}

static void
cb_dialog_graphic_config_cancel_clicked (GtkWidget *button, DialogGraphicData *state)
{
	SheetObject *so = SHEET_OBJECT (state->sog);

	gnm_so_graphic_set_width (so, state->width);
	gnm_so_graphic_set_fill_color (so,
		state->fill_color);
	state->fill_color = NULL;

	if (state->sog->type == SHEET_OBJECT_ARROW)
		sheet_object_graphic_abc_set (state->sog,
					      state->a, state->b, state->c);
	gtk_widget_destroy (state->dialog);
}

static void
cb_adjustment_value_changed (GtkAdjustment *adj, DialogGraphicData *state)
{
	SheetObject *so = SHEET_OBJECT (state->sog);
	gnm_so_graphic_set_width (so,
		gtk_spin_button_get_adjustment (state->spin_line_width)->value);
	foo_canvas_item_set (state->arrow,
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

		foo_canvas_item_set (state->arrow,
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
cb_fill_color_changed (GtkWidget *cc, GOColor color,
		       gboolean is_custom, gboolean by_user, gboolean is_default,
		       DialogGraphicData *state)
{
	gnm_so_graphic_set_fill_color (SHEET_OBJECT (state->sog),
		go_combo_color_get_style_color (cc));
	foo_canvas_item_set (state->arrow, "fill_color_gdk", color, NULL);
}

static void
sheet_object_graphic_user_config (SheetObject *so, SheetControl *sc)
{
	SheetObjectGraphic *sog= SHEET_OBJECT_GRAPHIC (so);
	WorkbookControlGUI *wbcg = scg_get_wbcg (SHEET_CONTROL_GUI (sc));
	DialogGraphicData *state;
	FooCanvasPoints *points;
	GtkWidget *table, *w;

	g_return_if_fail (sog != NULL);

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, SHEET_OBJECT_CONFIG_KEY))
		return;

	state = g_new0 (DialogGraphicData, 1);
	state->sog = sog;
	state->wbcg = wbcg;

	sog = SHEET_OBJECT_GRAPHIC (so);
	state->gui = gnm_glade_xml_new (GNM_CMD_CONTEXT (wbcg),
		"so-arrow.glade", NULL, NULL);
	state->dialog = glade_xml_get_widget (state->gui, "SO-Arrow");

 	table = glade_xml_get_widget (state->gui, "table");
	state->canvas = foo_canvas_new ();
	g_object_set (G_OBJECT (state->canvas), "can_focus", FALSE, NULL);
	gtk_table_attach_defaults (GTK_TABLE (table), state->canvas,
				   2, 3, 0, (sog->type != SHEET_OBJECT_ARROW) ? 2 : 5);
	gtk_widget_show (GTK_WIDGET (state->canvas));

	w = go_combo_color_new (NULL, NULL, 0,
		go_color_group_fetch ("color", so));
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (glade_xml_get_widget (state->gui, "label_color")), w);
	gnm_setup_label_atk (
		glade_xml_get_widget (state->gui, "label_color"), w);
	go_combo_color_set_color (GO_COMBO_COLOR (w),
		sog->fill_color ? &sog->fill_color->color : NULL);
	state->fill_color = style_color_ref (sog->fill_color);
	gtk_table_attach_defaults (GTK_TABLE (table), w, 1, 2, 0, 1);
	gtk_widget_show (GTK_WIDGET (w));
	g_signal_connect (G_OBJECT (w),
		"color_changed",
		G_CALLBACK (cb_fill_color_changed), state);

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
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->spin_line_width));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->spin_arrow_tip));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->spin_arrow_length));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->spin_arrow_width));

	if (sog->type != SHEET_OBJECT_ARROW) {
		gtk_widget_hide (glade_xml_get_widget (state->gui, "label_arrow_tip"));
		gtk_widget_hide (glade_xml_get_widget (state->gui, "label_arrow_length"));
		gtk_widget_hide (glade_xml_get_widget (state->gui, "label_arrow_width"));
		gtk_widget_hide (GTK_WIDGET (state->spin_arrow_tip));
		gtk_widget_hide (GTK_WIDGET (state->spin_arrow_length));
		gtk_widget_hide (GTK_WIDGET (state->spin_arrow_width));
	}
	gtk_widget_show (state->dialog);

	points = foo_canvas_points_new (2);
	points->coords [0] = state->canvas->allocation.width / 4.0;
	points->coords [1] = 5.0;
	points->coords [2] = state->canvas->allocation.width - points->coords [0];
	points->coords [3] = state->canvas->allocation.height - points->coords [1];

	if (sog->type != SHEET_OBJECT_ARROW)
		state->arrow = foo_canvas_item_new (
			foo_canvas_root (FOO_CANVAS (state->canvas)),
			FOO_TYPE_CANVAS_LINE, "points", points,
			"fill_color_gdk", sog->fill_color, NULL);
	else
		state->arrow = foo_canvas_item_new (
				foo_canvas_root (FOO_CANVAS (state->canvas)),
				FOO_TYPE_CANVAS_LINE, "points", points,
				"fill_color_gdk", sog->fill_color,
				"first_arrowhead", TRUE, NULL);

	foo_canvas_points_free (points);
	foo_canvas_set_scroll_region (FOO_CANVAS (state->canvas),
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
	g_signal_connect (G_OBJECT
			  (gtk_spin_button_get_adjustment (state->spin_line_width)),
			  "value_changed",
			  G_CALLBACK (cb_adjustment_value_changed), state);
	g_signal_connect_swapped (G_OBJECT (glade_xml_get_widget (state->gui, "ok_button")),
			  "clicked",
			  G_CALLBACK (gtk_widget_destroy), state->dialog);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "cancel_button")),
			  "clicked",
			  G_CALLBACK (cb_dialog_graphic_config_cancel_clicked), state);

	/* a candidate for merging into attach guru */
	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		(sog->type != SHEET_OBJECT_ARROW) ? GNUMERIC_HELP_LINK_SO_LINE
		: GNUMERIC_HELP_LINK_SO_ARROW);
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
		SHEET_OBJECT_CONFIG_KEY);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_dialog_graphic_config_destroy);
	gnumeric_non_modal_dialog (wbcg_toplevel (state->wbcg),
		GTK_WINDOW (state->dialog));
	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gtk_widget_show (state->dialog);
}

static void
sheet_object_graphic_class_init (GObjectClass *object_class)
{
	SheetObjectClass	*so_class  = SHEET_OBJECT_CLASS (object_class);
	SheetObjectGraphicClass *sog_class = SHEET_OBJECT_GRAPHIC_CLASS (object_class);

	sheet_object_graphic_parent_class = g_type_class_peek_parent (object_class);

	/* Object class method overrides */
	object_class->finalize = sheet_object_graphic_finalize;

	/* SheetObject class method overrides */
	so_class->new_view		= sheet_object_graphic_new_view;
	so_class->update_view_bounds	= sheet_object_graphic_update_bounds;
	so_class->read_xml_dom		= sheet_object_graphic_read_xml_dom;
	so_class->write_xml_dom		= sheet_object_graphic_write_xml_dom;
	so_class->clone			= sheet_object_graphic_clone;
	so_class->user_config		= sheet_object_graphic_user_config;
	so_class->print			= sheet_object_graphic_print;
	so_class->rubber_band_directly = TRUE;

	sog_class->get_graphic = NULL;
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

	GnmColor *outline_color;
	int	  outline_style;
} SheetObjectFilled;

typedef SheetObjectGraphicClass SheetObjectFilledClass;

static SheetObjectGraphicClass *sheet_object_filled_parent_class;

void
gnm_so_filled_set_outline_style (SheetObject *so, int style)
{
	SheetObjectFilled *sof = SHEET_OBJECT_FILLED (so);
	sof->outline_style = style;
	style_color_ref (sof->outline_color);
	gnm_so_filled_set_outline_color (so, sof->outline_color);
}

void
gnm_so_filled_set_outline_color (SheetObject *so, GnmColor *color)
{
	SheetObjectFilled *sof = SHEET_OBJECT_FILLED (so);
	GdkColor *gdk = (color != NULL && sof->outline_style != 0) ? &color->color : NULL;
	GList *l;

	g_return_if_fail (sof != NULL);

	style_color_unref (sof->outline_color);
	sof->outline_color = color;

	for (l = so->realized_list; l; l = l->next)
		foo_canvas_item_set (sheet_object_graphic_get_graphic (so, l->data),
			"outline_color_gdk", gdk,
			NULL);
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

	foo_canvas_item_set (FOO_CANVAS_ITEM (view),
		"x1", MIN (coords [0], coords [2]),
		"x2", MAX (coords [0], coords [2]),
		"y1", MIN (coords [1], coords [3]),
		"y2", MAX (coords [1], coords [3]),
		NULL);

	if (so->is_visible)
		foo_canvas_item_show (FOO_CANVAS_ITEM (view));
	else
		foo_canvas_item_hide (FOO_CANVAS_ITEM (view));
}

static FooCanvasItem *
sheet_object_filled_new_view_internal (SheetObject *so, SheetControl *sc, GnmCanvas *gcanvas,
				       FooCanvasGroup *group)
{
	SheetObjectGraphic *sog;
	SheetObjectFilled  *sof;
	FooCanvasItem *item;
	GdkColor *fill_color, *outline_color;

	g_return_val_if_fail (IS_SHEET_OBJECT_FILLED (so), NULL);
	g_return_val_if_fail (IS_SHEET_CONTROL (sc), NULL);
	g_return_val_if_fail (gcanvas != NULL, NULL);

	sof = SHEET_OBJECT_FILLED (so);
	sog = SHEET_OBJECT_GRAPHIC (so);

	fill_color = (sog->fill_color != NULL) ? &sog->fill_color->color : NULL;
	outline_color = (sof->outline_color != NULL && sof->outline_style != 0)
			 ? &sof->outline_color->color : NULL;

	item = foo_canvas_item_new (group,
		(sog->type == SHEET_OBJECT_OVAL) ?
					FOO_TYPE_CANVAS_ELLIPSE :
					FOO_TYPE_CANVAS_RECT,
		"fill_color_gdk",	fill_color,
		"outline_color_gdk",	outline_color,
		"width_units",		sog->width,
		NULL);

	return item;
}

static GObject *
sheet_object_filled_new_view (SheetObject *so, SheetControl *sc, gpointer key)
{
	GnmCanvas *gcanvas = ((GnumericPane *)key)->gcanvas;
	FooCanvasItem *i = sheet_object_filled_new_view_internal (so,
				sc, gcanvas, gcanvas->sheet_object_group);

	foo_canvas_item_raise_to_top (FOO_CANVAS_ITEM (gcanvas->sheet_object_group));
	gnm_pane_object_register (so, i);
	return G_OBJECT (i);
}

static gboolean
sheet_object_filled_read_xml_dom (SheetObject *so, char const *typename,
				  XmlParseContext const *ctxt,
				  xmlNodePtr tree)
{
	SheetObjectFilled *sof;

	g_return_val_if_fail (IS_SHEET_OBJECT_FILLED (so), TRUE);
	sof = SHEET_OBJECT_FILLED (so);

	gnm_so_filled_set_outline_color (so,
		xml_node_get_color (tree, "OutlineColor"));

	return sheet_object_graphic_read_xml_dom (so, typename, ctxt, tree);
}

static gboolean
sheet_object_filled_write_xml_dom (SheetObject const *so,
				   XmlParseContext const *ctxt,
				   xmlNodePtr tree)
{
	SheetObjectFilled *sof;

	g_return_val_if_fail (IS_SHEET_OBJECT_FILLED (so), TRUE);
	sof = SHEET_OBJECT_FILLED (so);

	if (sof->outline_color)
		xml_node_set_color (tree, "OutlineColor", sof->outline_color);

	return sheet_object_graphic_write_xml_dom (so, ctxt, tree);
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

	new_sof->outline_color = style_color_ref (sof->outline_color);
	new_sof->outline_style = sof->outline_style;

	return SHEET_OBJECT (new_sof);
}

typedef struct {
	GladeXML	*gui;
	GtkWidget	*dialog;
	GtkSpinButton	*spin_border_width;

	GnmColor	*outline_color;
	GnmColor	*fill_color;
	double		 width;

	WorkbookControlGUI *wbcg;
	SheetObjectFilled  *sof;
	Sheet		   *sheet;
} DialogFilledData;

static void
cb_dialog_filled_config_destroy (DialogFilledData *state)
{
	wbcg_edit_detach_guru (state->wbcg);
	if (state->outline_color != NULL) {
		style_color_unref (state->outline_color);
		state->outline_color = NULL;
	}
	if (state->fill_color != NULL) {
		style_color_unref (state->fill_color);
		state->fill_color = NULL;
	}
	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}
	state->dialog = NULL;
	g_free (state);
}

static void
cb_dialog_filled_adjustment_value_changed (GtkAdjustment *adj, DialogFilledData *state)
{
	gnm_so_graphic_set_width (SHEET_OBJECT (state->sof),
		gtk_spin_button_get_adjustment (state->spin_border_width)->value);
}

static void
cb_fillcolor_changed (GtkWidget *cc, GOColor color,
		      gboolean is_custom, gboolean by_user, gboolean is_default,
		      SheetObject *so)
{
	gnm_so_graphic_set_fill_color (so,
		go_combo_color_get_style_color (cc));
}

static void
cb_outlinecolor_changed (GtkWidget *cc, GOColor color,
			 gboolean is_custom, gboolean by_user, gboolean is_default,
			 SheetObject *so)
{
	gnm_so_filled_set_outline_color (so,
		go_combo_color_get_style_color (cc));
}

static void
cb_dialog_filled_config_ok_clicked (GtkWidget *button, DialogFilledData *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_dialog_filled_config_cancel_clicked (GtkWidget *button, DialogFilledData *state)
{
	SheetObject *so = SHEET_OBJECT (state->sof);

	gnm_so_graphic_set_width (so, state->width);
	gnm_so_graphic_set_fill_color (so,
		state->fill_color);
	state->fill_color = NULL;
	gnm_so_filled_set_outline_color (so,
		state->outline_color);
	state->outline_color = NULL;

	gtk_widget_destroy (state->dialog);
}

static void
sheet_object_filled_user_config (SheetObject *so, SheetControl *sc)
{
	SheetObjectFilled *sof = SHEET_OBJECT_FILLED (so);
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);
	WorkbookControlGUI *wbcg = scg_get_wbcg (SHEET_CONTROL_GUI (sc));
	DialogFilledData *state;
	GtkWidget *table, *w;

	g_return_if_fail (IS_SHEET_OBJECT_FILLED (so));

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, SHEET_OBJECT_CONFIG_KEY))
		return;

	state = g_new0 (DialogFilledData, 1);
	state->sof = sof;
	state->wbcg = wbcg;
	state->sheet = sc_sheet	(sc);

	state->gui = gnm_glade_xml_new (GNM_CMD_CONTEXT (wbcg),
		"so-fill.glade", NULL, NULL);
	state->dialog = glade_xml_get_widget (state->gui, "SO-Fill");

 	table = glade_xml_get_widget (state->gui, "table");

	w = go_combo_color_new (NULL, _("Transparent"), 0,
		go_color_group_fetch ("outline_color", so));
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (glade_xml_get_widget (state->gui, "border_label")), w);
	gnm_setup_label_atk (
		glade_xml_get_widget (state->gui, "border_label"), w);
	go_combo_color_set_color (GO_COMBO_COLOR (w),
		sof->outline_color ? &sof->outline_color->color : NULL);
	state->outline_color = style_color_ref (sof->outline_color);
	gtk_table_attach_defaults (GTK_TABLE (table), w, 1, 2, 0, 1);
	gtk_widget_show (GTK_WIDGET (w));
	g_signal_connect (G_OBJECT (w),
		"color_changed",
		G_CALLBACK (cb_outlinecolor_changed), so);

	w = go_combo_color_new (NULL, _("Transparent"), 0,
		go_color_group_fetch ("fill_color", so));
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (glade_xml_get_widget (state->gui, "fill_label")), w);
	gnm_setup_label_atk (
		glade_xml_get_widget (state->gui, "fill_label"), w);
	go_combo_color_set_color (GO_COMBO_COLOR (w),
		sog->fill_color ? &sog->fill_color->color : NULL);
	state->fill_color = style_color_ref (sog->fill_color);
	gtk_table_attach_defaults (GTK_TABLE (table), w, 1, 2, 1, 2);
	gtk_widget_show (GTK_WIDGET (w));
	g_signal_connect (G_OBJECT (w),
		"color_changed",
		G_CALLBACK (cb_fillcolor_changed), so);

	state->spin_border_width = GTK_SPIN_BUTTON (glade_xml_get_widget (
						     state->gui, "spin_border_width"));
	state->width = sog->width;
	gtk_spin_button_set_value (state->spin_border_width, state->width);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->spin_border_width));

	g_signal_connect (G_OBJECT
			  (gtk_spin_button_get_adjustment (state->spin_border_width)),
			  "value_changed",
			  G_CALLBACK (cb_dialog_filled_adjustment_value_changed), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "ok_button")),
			  "clicked",
			  G_CALLBACK (cb_dialog_filled_config_ok_clicked), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "cancel_button")),
			  "clicked",
			  G_CALLBACK (cb_dialog_filled_config_cancel_clicked), state);

	/* a candidate for merging into attach guru */
	gnumeric_init_help_button (glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_SO_FILLED);
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
		SHEET_OBJECT_CONFIG_KEY);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_dialog_filled_config_destroy);
	gnumeric_non_modal_dialog (wbcg_toplevel (state->wbcg),
		GTK_WINDOW (state->dialog));
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
			   double width, double height)
{
	SheetObjectFilled  *sof = SHEET_OBJECT_FILLED (so);
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (so);

	gnome_print_newpath (ctx);
	if (sog->type == SHEET_OBJECT_OVAL)
		make_ellipse (ctx, 0., width, 0., -height);
	else
		make_rect (ctx, 0., width, 0., -height);
	gnome_print_closepath (ctx);

	if (sog->fill_color) {
		gnome_print_gsave (ctx);
		gnome_print_setrgbcolor (ctx,
			sog->fill_color->color.red   / (double) 0xffff,
			sog->fill_color->color.green / (double) 0xffff,
			sog->fill_color->color.blue  / (double) 0xffff);
		gnome_print_fill (ctx);
		gnome_print_grestore (ctx);
	}
	if (sof->outline_color && sof->outline_style > 0) {
		gnome_print_setlinewidth (ctx, sog->width);
		gnome_print_setrgbcolor (ctx,
			sof->outline_color->color.red   / (double) 0xffff,
			sof->outline_color->color.green / (double) 0xffff,
			sof->outline_color->color.blue  / (double) 0xffff);
		gnome_print_stroke (ctx);
	}
}

static void
sheet_object_filled_class_init (GObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class;

	sheet_object_filled_parent_class = g_type_class_peek_parent (object_class);

	object_class->finalize		  = sheet_object_filled_finalize;

	sheet_object_class = SHEET_OBJECT_CLASS (object_class);
	sheet_object_class->new_view	  = sheet_object_filled_new_view;
	sheet_object_class->update_view_bounds = sheet_object_filled_update_bounds;
	sheet_object_class->read_xml_dom  = sheet_object_filled_read_xml_dom;
	sheet_object_class->write_xml_dom = sheet_object_filled_write_xml_dom;
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
	sof->outline_style = 1; /* arbitrary something non-zero */
	gnm_so_graphic_set_fill_color (SHEET_OBJECT (sof),
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
	GnmColor  *fill_color;
	GnmColor  *outline_color;
	double       outline_width;
	FooCanvasPoints *points;
} SheetObjectPolygon;
typedef struct {
	SheetObjectClass parent_class;
} SheetObjectPolygonClass;
static SheetObjectClass *sheet_object_polygon_parent_class;

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
	GnmCanvas *gcanvas = ((GnumericPane *)key)->gcanvas;
	SheetObjectPolygon *sop = SHEET_OBJECT_POLYGON (so);
	FooCanvasItem *item = NULL;
	GdkColor *fill_color, *outline_color;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);
	g_return_val_if_fail (IS_SHEET_CONTROL (sc), NULL);
	g_return_val_if_fail (gcanvas != NULL, NULL);

	fill_color = (sop->fill_color != NULL) ? &sop->fill_color->color : NULL;
	outline_color = (sop->outline_color != NULL) ? &sop->outline_color->color : NULL;

	foo_canvas_item_raise_to_top (FOO_CANVAS_ITEM (gcanvas->sheet_object_group));

	item = foo_canvas_item_new (
		gcanvas->sheet_object_group,
		foo_canvas_polygon_get_type (),
		"fill_color_gdk",	fill_color,
		"outline_color_gdk",	outline_color,
		"width_units",		sop->outline_width,
		"points",		sop->points,
		/* "join_style",		GDK_JOIN_ROUND, */
		NULL);
	gnm_pane_object_register (so, item);
	return G_OBJECT (item);
}

static void
sheet_object_polygon_update_bounds (SheetObject *so, GObject *view_obj)
{
	double scale[6], translate[6], result[6];
	FooCanvasPoints *points = foo_canvas_points_new (2);
	FooCanvasItem   *view = FOO_CANVAS_ITEM (view_obj);
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

#warning FIXME without affines
	/* foo_canvas_item_affine_absolute (view, result); */
	foo_canvas_points_free (points);

	if (so->is_visible)
		foo_canvas_item_show (view);
	else
		foo_canvas_item_hide (view);
}

static gboolean
sheet_object_polygon_read_xml_dom (SheetObject *so, char const *typename,
				   XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectPolygon *sop;

	g_return_val_if_fail (IS_SHEET_OBJECT_POLYGON (so), TRUE);
	sop = SHEET_OBJECT_POLYGON (so);

	return FALSE;
}

static gboolean
sheet_object_polygon_write_xml_dom (SheetObject const *so,
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

	new_sop->fill_color	= style_color_ref (sop->fill_color);
	new_sop->outline_color	= style_color_ref (sop->outline_color);

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

	sheet_object_polygon_parent_class = g_type_class_peek_parent (object_class);

	/* Object class method overrides */
	object_class->finalize = sheet_object_polygon_finalize;

	/* SheetObject class method overrides */
	sheet_object_class = SHEET_OBJECT_CLASS (object_class);
	sheet_object_class->new_view	  = sheet_object_polygon_new_view;
	sheet_object_class->update_view_bounds = sheet_object_polygon_update_bounds;
	sheet_object_class->read_xml_dom  = sheet_object_polygon_read_xml_dom;
	sheet_object_class->write_xml_dom = sheet_object_polygon_write_xml_dom;
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
	sop->fill_color = style_color_new_name ("white");
	sop->outline_color = style_color_new_name ("black");
	sop->outline_width = .02;
	sop->points = foo_canvas_points_new (4);

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
gnm_so_polygon_set_points (SheetObject *so, GArray *pairs)
{
	FooCanvasPoints *points;
	unsigned i;
	GList *l;
	SheetObjectPolygon *sop;

	g_return_if_fail (IS_SHEET_OBJECT_POLYGON (so));
	g_return_if_fail (pairs != NULL);

	sop = SHEET_OBJECT_POLYGON (so);

	points = foo_canvas_points_new (pairs->len / 2);
	for (i = 0 ; i < pairs->len ; i++)
		points->coords [i] = g_array_index (pairs, double, i);
	for (l = so->realized_list; l; l = l->next)
		foo_canvas_item_set (l->data, "points", points, NULL);
	foo_canvas_points_free (sop->points);
	sop->points = points;
}

void
gnm_so_polygon_set_fill_color (SheetObject *so, GnmColor *color)
{
	SheetObjectPolygon *sop = SHEET_OBJECT_POLYGON (so);
	GdkColor *gdk = (color != NULL) ? &color->color : NULL;
	GList *l;

	g_return_if_fail (sop != NULL);

	style_color_unref (sop->fill_color);
	sop->fill_color = color;

	for (l = so->realized_list; l; l = l->next)
		foo_canvas_item_set (l->data, "fill_color_gdk", gdk, NULL);
}

void
gnm_so_polygon_set_outline_color (SheetObject *so, GnmColor *color)
{
	SheetObjectPolygon *sop = SHEET_OBJECT_POLYGON (so);
	GdkColor *gdk = (color != NULL) ? &color->color : NULL;
	GList *l;

	g_return_if_fail (sop != NULL);

	style_color_unref (sop->outline_color);
	sop->outline_color = color;

	for (l = so->realized_list; l; l = l->next)
		foo_canvas_item_set (l->data, "outline_color_gdk", gdk, NULL);
}

/************************************************************************/

typedef struct {
	SheetObjectFilled parent;

	char *label;
	PangoAttrList  *markup;
} SheetObjectText;
typedef struct {
	SheetObjectFilledClass	parent;
} SheetObjectTextClass;
static SheetObjectFilledClass *sheet_object_text_parent_class;

void
gnm_so_text_set_text (SheetObject *so, char const *str)
{
	SheetObjectText *sot = SHEET_OBJECT_TEXT (so);

	g_return_if_fail (sot != NULL);
	g_return_if_fail (str != NULL);

	if (sot->label != str) {
		GList *l;
		g_free (sot->label);
		sot->label = g_strdup (str);
		for (l = so->realized_list; l; l = l->next) {
			FooCanvasGroup *group = FOO_CANVAS_GROUP (l->data);
			foo_canvas_item_set (FOO_CANVAS_ITEM (group->item_list->next->data),
				"text", sot->label,
				NULL);
		}
	}
}

static void
sheet_object_text_init_full (SheetObjectText *sot, char const *text)
{
	sot->label = g_strdup (text);
#if 0
	gnm_so_graphic_set_fill_color (SHEET_OBJECT (sot) NULL);
	gnm_so_filled_set_outline_color (SHEET_OBJECT (sot) NULL);
#endif
}

static void
sheet_object_text_init (SheetObjectText *sot)
{
	sot->markup = NULL;
	sheet_object_text_init_full (sot, _("Label"));
}

static void
sheet_object_text_finalize (GObject *obj)
{
	SheetObjectText *sot = SHEET_OBJECT_TEXT (obj);

	if (NULL != sot->markup) {
		pango_attr_list_unref (sot->markup);
		sot->markup = NULL;
	}
	g_free (sot->label);
	sot->label = NULL;

	G_OBJECT_CLASS (sheet_object_text_parent_class)->finalize (obj);
}

static GObject *
sheet_object_text_new_view (SheetObject *so, SheetControl *sc, gpointer key)
{
	GnmCanvas *gcanvas = ((GnumericPane *)key)->gcanvas;
	SheetObjectText *sot = SHEET_OBJECT_TEXT (so);
	FooCanvasItem *text = NULL, *back = NULL;
	FooCanvasGroup *group;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);
	g_return_val_if_fail (IS_SHEET_CONTROL (sc), NULL);
	g_return_val_if_fail (gcanvas != NULL, NULL);

	foo_canvas_item_raise_to_top (FOO_CANVAS_ITEM (gcanvas->sheet_object_group));
	group = FOO_CANVAS_GROUP (foo_canvas_item_new (
			gcanvas->sheet_object_group,
			FOO_TYPE_CANVAS_GROUP,
			NULL));
	text = foo_canvas_item_new (group,
		FOO_TYPE_CANVAS_TEXT,
		"text",		sot->label,
		"anchor",	GTK_ANCHOR_NW,
		"clip",		TRUE,
		"x",		0.,
		"y",		0.,
		"attributes",	sot->markup,
		NULL);
	back = sheet_object_filled_new_view_internal (so, sc, gcanvas, group);
	foo_canvas_item_raise_to_top (text);
	gnm_pane_object_register (so, FOO_CANVAS_ITEM (group));
	return G_OBJECT (group);
}

static void
sheet_object_text_update_bounds (SheetObject *so, GObject *view)
{
	double coords [4];
	SheetControlGUI	  *scg  =
		SHEET_CONTROL_GUI (sheet_object_view_control (view));
	FooCanvasGroup *group = FOO_CANVAS_GROUP (view);

	scg_object_view_position (scg, so, coords);

	foo_canvas_item_set (FOO_CANVAS_ITEM (group->item_list->next->data),
		"clip_width",  fabs (coords [0] - coords [2]),
		"wrap_width",  fabs (coords [0] - coords [2]),
		"clip_height", fabs (coords [1] - coords [3]),
		NULL);
	foo_canvas_item_set (FOO_CANVAS_ITEM (group->item_list->data),
		"x1", 0.,
		"y1", 0.,
		"x2", fabs (coords [0] - coords [2]),
		"y2", fabs (coords [1] - coords [3]),
		NULL);

	foo_canvas_item_set (FOO_CANVAS_ITEM (view),
		"x", MIN (coords [0], coords [2]),
		"y", MIN (coords [1], coords [3]),
		NULL);

	if (so->is_visible)
		foo_canvas_item_show (FOO_CANVAS_ITEM (view));
	else
		foo_canvas_item_hide (FOO_CANVAS_ITEM (view));
}

static gboolean
sheet_object_text_read_xml_dom (SheetObject *so, char const *typename,
				XmlParseContext const *context,
				xmlNodePtr tree)
{
	SheetObjectText *sot = SHEET_OBJECT_TEXT (so);
	gchar *label = (gchar *)xmlGetProp (tree, (xmlChar *)"Label");

	if (!label) {
		g_warning ("Could not read a SheetObjectText beacause it lacks a label property.");
		return TRUE;
	}

	sot->label = g_strdup (label);
	xmlFree (label);

	return FALSE;
}

static gboolean
sheet_object_text_write_xml_dom (SheetObject const *so,
				 XmlParseContext const *context,
				 xmlNodePtr tree)
{
	SheetObjectText *sot = SHEET_OBJECT_TEXT (so);

	xml_node_set_cstr (tree, "Label", sot->label);

#warning TODO how to persist the attribute list
	return FALSE;
}

static SheetObject *
sheet_object_text_clone (SheetObject const *src, Sheet *new_sheet)
{
	SheetObjectText const *src_sot = SHEET_OBJECT_TEXT (src);
	SheetObjectText *sot = g_object_new (SHEET_OBJECT_TEXT_TYPE, NULL);
	sheet_object_text_init_full (sot, src_sot->label);

	pango_attr_list_ref (src_sot->markup);
	sot->markup = src_sot->markup;
	return SHEET_OBJECT (sot);
}

static FooCanvasItem *
sheet_object_text_get_graphic (FooCanvasItem *item)
{
	FooCanvasGroup *group = FOO_CANVAS_GROUP (item);
	return FOO_CANVAS_ITEM (group->item_list->data);
}

static void
sheet_object_text_print (SheetObject const *so, GnomePrintContext *ctx,
			 double width, double height)
{
	SHEET_OBJECT_CLASS (sheet_object_text_parent_class)->
		print (so, ctx, width, height);
}

static void
sheet_object_text_class_init (GObjectClass *object_class)
{
	SheetObjectClass *so_class = SHEET_OBJECT_CLASS (object_class);
	SheetObjectGraphicClass *sog_class = SHEET_OBJECT_GRAPHIC_CLASS (object_class);

	sheet_object_text_parent_class = g_type_class_peek_parent (object_class);

	/* Object class method overrides */
	object_class->finalize = sheet_object_text_finalize;

	/* SheetObject class method overrides */
	so_class = SHEET_OBJECT_CLASS (object_class);
	so_class->new_view		= sheet_object_text_new_view;
	so_class->update_view_bounds	= sheet_object_text_update_bounds;
	so_class->read_xml_dom		= sheet_object_text_read_xml_dom;
	so_class->write_xml_dom		= sheet_object_text_write_xml_dom;
	so_class->clone			= sheet_object_text_clone;
	/* so_class->user_config   = NULL; inherit from parent */
	so_class->print         	= sheet_object_text_print;
	so_class->rubber_band_directly = FALSE;

	sog_class->get_graphic	= sheet_object_text_get_graphic;
}

GSF_CLASS (SheetObjectText, sheet_object_text,
	   sheet_object_text_class_init, sheet_object_text_init,
	   SHEET_OBJECT_FILLED_TYPE);

void
gnm_so_text_set_markup (SheetObject *so, PangoAttrList *markup)
{
	SheetObjectText *sot = SHEET_OBJECT_TEXT (so);
	GList *l;

	g_return_if_fail (sot != NULL);

	if (markup != NULL)
		pango_attr_list_ref (markup);
	if (sot->markup != NULL)
		pango_attr_list_unref (sot->markup);
	sot->markup = markup;

	for (l = so->realized_list; l; l = l->next)
		foo_canvas_item_set (l->data, "attributes", markup, NULL);
}
