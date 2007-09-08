/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * dialog-printer-setup.c: Printer setup dialog box
 *
 * Authors:
 *  Wayne Schuller (k_wayne@linuxpower.org)
 *  Miguel de Icaza (miguel@gnu.org)
 *  Andreas J. Guelzow (aguelzow@pyrshep.ca)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <gtk/gtk.h>

#include "dialogs.h"
#include "help.h"

#include <gui-util.h>
#include <commands.h>
#include <print-info.h>
#include <print.h>
#include <ranges.h>
#include <sheet.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <style.h>
#include <gnumeric-gconf.h>

#include <goffice/cut-n-paste/foocanvas/foo-canvas.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-util.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-line.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-text.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-rect-ellipse.h>
#include <glade/glade.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtktable.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkbox.h>
#include <gtk/gtkcombobox.h>
#include <gtk/gtkcelllayout.h>
#include <gtk/gtkcellrenderertext.h>
/*#include <gtk/gtkmenuitem.h>*/

/* FIXME: do not hardcode pixel counts.  */
#define PREVIEW_X 170
#define PREVIEW_Y 170
#define PREVIEW_MARGIN_X 20
#define PREVIEW_MARGIN_Y 20
#define PAGE_X (PREVIEW_X - PREVIEW_MARGIN_X)
#define PAGE_Y (PREVIEW_Y - PREVIEW_MARGIN_Y)

#define MARGIN_COLOR_DEFAULT "darkgray"
#define MARGIN_COLOR_ACTIVE "red"

#define HF_PREVIEW_X 350
#define HF_PREVIEW_Y 50
#define HF_PREVIEW_SHADOW 2
#define HF_PREVIEW_PADDING 5
#define HF_PREVIEW_MARGIN 15

#define PRINTER_SETUP_KEY "printer-setup-dialog"

#define PAPER_PAGE 0
#define SCALE_PAGE 1
#define HF_PAGE 2

typedef struct {
	/* The Canvas object */
	GtkWidget        *canvas;

	/* Objects in the Preview Canvas */
	FooCanvasItem  *group;

	/* Values for the scaling of the nice preview */
	int offset_x, offset_y;	/* For centering the small page preview */
	double scale;
} MarginPreviewInfo;

#if 0

typedef struct {
	/* The Canvas object for the header/footer sample preview */
	GtkWidget        *canvas;

	/* Objects in the Preview Canvas */
	FooCanvasItem  *left;
	FooCanvasItem  *middle;
	FooCanvasItem  *right;

} HFPreviewInfo;

#endif

typedef struct _PrinterSetupState PrinterSetupState;
typedef struct {
	double     value;
	GtkSpinButton *spin;
/* 	GtkAdjustment *adj; */

	FooCanvasItem *line;
	double bound_x1, bound_y1, bound_x2, bound_y2;
	MarginPreviewInfo *pi;
	PrinterSetupState *state;
} UnitInfo;

struct _PrinterSetupState {
	WBCGtk  *wbcg;
	Sheet            *sheet;
	GladeXML         *gui;
	PrintInformation *pi;
	GtkWidget        *dialog;
	GtkWidget        *sheet_selector;

	GtkWidget        *scale_percent_radio;
	GtkWidget        *scale_fit_to_radio;
	GtkWidget        *scale_no_radio;
	

 	GtkWidget        *unit_selector;
	GtkTreeModel     *unit_model;
	GtkUnit		  display_unit;

	struct {
		UnitInfo top, bottom, left, right;
		UnitInfo header, footer;
	} margins;

	MarginPreviewInfo preview;

	double height, width;

	GtkWidget * check_center_v;
	GtkWidget * check_center_h;	

	GtkWidget *icon_rd;
	GtkWidget *icon_dr;
	GnmExprEntry *area_entry;
	GnmExprEntry *top_entry;
	GnmExprEntry *left_entry;

/* 	/\* The header and footer data. *\/ */
/* 	PrintHF *header; */
/* 	PrintHF *footer; */

/* 	/\* The header and footer customize dialogs. *\/ */
/* 	GtkWidget *customize_header; */
/* 	GtkWidget *customize_footer; */

/* 	/\* The header and footer preview widgets. *\/ */
/* 	HFPreviewInfo *pi_header; */
/* 	HFPreviewInfo *pi_footer; */
};

static void dialog_gtk_printer_setup_cb (PrinterSetupState *state);
static void fetch_settings (PrinterSetupState *state);
static void do_update_page (PrinterSetupState *state);
static void do_fetch_margins (PrinterSetupState *state);
#if 0
static void do_hf_customize (gboolean header, PrinterSetupState *state);
#endif

static double
get_conversion_factor (GtkUnit unit)
{
	switch (unit) {
	case GTK_UNIT_MM:
		return (72./25.4);
	case GTK_UNIT_INCH:
		return 72.;
	case GTK_UNIT_PIXEL:
	case GTK_UNIT_POINTS:
	default:
		return 1.0;
	}
}

static void
margin_preview_page_destroy (PrinterSetupState *state)
{
	if (state->preview.group) {
		gtk_object_destroy (GTK_OBJECT (state->preview.group));
		state->preview.group = NULL;
	}
}

static void
move_line (FooCanvasItem *item,
	   double x1, double y1,
	   double x2, double y2)
{
	if (item != NULL) {
		FooCanvasPoints *points;

		points = foo_canvas_points_new (2);
		points->coords[0] = x1;
		points->coords[1] = y1;
		points->coords[2] = x2;
		points->coords[3] = y2;
		
		foo_canvas_item_set (item,
				     "points", points,
				     NULL);
		foo_canvas_points_unref (points);
	}
}

static FooCanvasItem *
make_line (FooCanvasGroup *g, double x1, double y1, double x2, double y2)
{
	FooCanvasPoints *points;
	FooCanvasItem *item;

	points = foo_canvas_points_new (2);
	points->coords[0] = x1;
	points->coords[1] = y1;
	points->coords[2] = x2;
	points->coords[3] = y2;

	item = foo_canvas_item_new (
		FOO_CANVAS_GROUP (g), foo_canvas_line_get_type (),
		"points", points,
		"width-pixels", 1,
		"fill-color",   MARGIN_COLOR_DEFAULT,
		NULL);
	foo_canvas_points_unref (points);

	return item;
}

static void
draw_margin_top (UnitInfo *uinfo)
{
	double x1, x2, y1;

	x1 = uinfo->bound_x1;
	x2 = uinfo->bound_x2;
	y1 = uinfo->bound_y1;

	y1 += uinfo->pi->scale * uinfo->value;
	move_line (uinfo->line, x1, y1, x2, y1);
}

static void
draw_margin_bottom (UnitInfo *uinfo)
{
	double x1, x2, y2;

	x1 = uinfo->bound_x1;
	x2 = uinfo->bound_x2;
	y2 = uinfo->bound_y2;

	y2 -= uinfo->pi->scale * uinfo->value;
	move_line (uinfo->line, x1, y2, x2, y2);
}

static void
draw_margin_left (UnitInfo *uinfo)
{
	double x1, y1, y2;

	x1 = uinfo->bound_x1;
	y1 = uinfo->bound_y1;
	y2 = uinfo->bound_y2;

	x1 += uinfo->pi->scale * uinfo->value;
	move_line (uinfo->line, x1, y1, x1, y2);
}

static void
draw_margin_right (UnitInfo *uinfo)
{
	double y1, x2, y2;

	y1 = uinfo->bound_y1;
	x2 = uinfo->bound_x2;
	y2 = uinfo->bound_y2;

	x2 -= uinfo->pi->scale * uinfo->value;
	move_line (uinfo->line, x2, y1, x2, y2);
}

static void
draw_margin_header (UnitInfo *uinfo)
{
	double x1, y1, x2;
	double outside = uinfo->pi->scale *
		uinfo->state->margins.top.value;
	double inside = uinfo->pi->scale * uinfo->value;

	x1 = uinfo->bound_x1;
	y1 = uinfo->bound_y1;
	x2 = uinfo->bound_x2;

	if (inside < 1.0)
		inside = 1.0; 
	y1 += outside + inside ;
	
	move_line (uinfo->line, x1, y1, x2, y1);
}

static void
draw_margin_footer (UnitInfo *uinfo)
{
	double x1, x2, y2;
	double outside = uinfo->pi->scale *
		uinfo->state->margins.bottom.value;
	double inside = uinfo->pi->scale * uinfo->value;
	
	x1 = uinfo->bound_x1;
	x2 = uinfo->bound_x2;
	y2 = uinfo->bound_y2;

	if (inside < 1.0)
		inside = 1.0; 
	y2 -= outside + inside ;
	move_line (uinfo->line, x1, y2, x2, y2);
}

static void
create_margin (UnitInfo *uinfo,
	       double x1, double y1,
	       double x2, double y2)
{
	FooCanvasGroup *g = FOO_CANVAS_GROUP (uinfo->state->preview.group);

	uinfo->line = make_line (g, x1 + 8, y1, x1 + 8, y2);
	uinfo->bound_x1 = x1;
	uinfo->bound_y1 = y1;
	uinfo->bound_x2 = x2;
	uinfo->bound_y2 = y2;
}

static void
draw_margins (PrinterSetupState *state, double x1, double y1, double x2, double y2)
{
	/* Margins */
	create_margin (&state->margins.left, x1, y1, x2, y2);
	create_margin (&state->margins.right, x1, y1, x2, y2);
	create_margin (&state->margins.top, x1, y1, x2, y2);
	create_margin (&state->margins.bottom, x1, y1, x2, y2);

	/* Headers & footers */
	create_margin (&state->margins.header, x1, y1, x2, y2);
	create_margin (&state->margins.footer, x1, y1, x2, y2);

	/* Margins */
	draw_margin_left (&state->margins.left);
	draw_margin_right (&state->margins.right);
	draw_margin_top (&state->margins.top);
	draw_margin_bottom (&state->margins.bottom);

	/* Headers & footers */
	draw_margin_header (&state->margins.header);
	draw_margin_footer (&state->margins.footer);
}


static void
margin_preview_page_create (PrinterSetupState *state)
{
	double x1, y1, x2, y2;
	double width, height;
	MarginPreviewInfo *pi = &state->preview;

	width = state->width;
	height = state->height;

	if (width < height)
		pi->scale = PAGE_Y / height;
	else
		pi->scale = PAGE_X / width;

	pi->offset_x = (PREVIEW_X - (width  * pi->scale)) / 2;
	pi->offset_y = (PREVIEW_Y - (height * pi->scale)) / 2;
	x1 = pi->offset_x + 0 * pi->scale;
	y1 = pi->offset_y + 0 * pi->scale;
	x2 = pi->offset_x + width * pi->scale;
	y2 = pi->offset_y + height * pi->scale;

	pi->group = foo_canvas_item_new (
		foo_canvas_root (FOO_CANVAS (pi->canvas)),
		foo_canvas_group_get_type (),
		"x", 0.0,
		"y", 0.0,
		NULL);

	foo_canvas_item_new (FOO_CANVAS_GROUP (pi->group),
		FOO_TYPE_CANVAS_RECT,
		"x1",  	      	 (double) x1+2,
		"y1",  	      	 (double) y1+2,
		"x2",  	      	 (double) x2+2,
		"y2",         	 (double) y2+2,
		"fill-color",    "black",
		"outline-color", "black",
		"width-pixels",   1,
		NULL);

	foo_canvas_item_new (FOO_CANVAS_GROUP (pi->group),
		FOO_TYPE_CANVAS_RECT,
		"x1",  	      	 (double) x1,
		"y1",  	      	 (double) y1,
		"x2",  	      	 (double) x2,
		"y2",         	 (double) y2,
		"fill-color",    "white",
		"outline-color", "black",
		"width-pixels",   1,
		NULL);

	draw_margins (state, x1, y1, x2, y2);
}

static void
canvas_update (PrinterSetupState *state)
{
/* 	guchar *unit_txt; */

	margin_preview_page_destroy (state);
	margin_preview_page_create (state);

/* 	unit_txt = gnome_print_config_get (state->gp_config, GNOME_PRINT_KEY_PREFERED_UNIT); */
/* 	if (unit_txt) { */
/* 		GnomePrintUnitSelector *sel = */
/* 			GNOME_PRINT_UNIT_SELECTOR (state->unit_selector); */
/* 		const GnomePrintUnit *unit = */
/* 			gnome_print_unit_get_by_abbreviation (unit_txt); */

/* 		g_free (unit_txt); */
/* 		gnome_print_unit_selector_set_unit (sel, unit); */
/* 		spin_button_adapt_to_unit (state->margins.header.spin, unit); */
/* 		spin_button_adapt_to_unit (state->margins.footer.spin, unit); */
/* 	} */
}


static void
notebook_flipped (G_GNUC_UNUSED GtkNotebook *notebook,
		  G_GNUC_UNUSED GtkNotebookPage *page,
		  gint page_num,
		  PrinterSetupState *state)
{
}


static gboolean
cb_spin_activated (UnitInfo *target)
{
	foo_canvas_item_set (target->line,
			     "fill-color", MARGIN_COLOR_ACTIVE,
			     NULL);
	return FALSE;
}

static gboolean
cb_spin_deactivated (UnitInfo *target)
{
	foo_canvas_item_set (target->line,
			     "fill-color", MARGIN_COLOR_DEFAULT,
			     NULL);
	return FALSE;
}

static void
configure_bounds_header (PrinterSetupState *state)
{
	double max = state->height - state->margins.top.value
		- state->margins.footer.value - state->margins.bottom.value;
	gtk_spin_button_set_range (state->margins.header.spin, 0., max);
}

static void
configure_bounds_footer (PrinterSetupState *state)
{
	double max = state->height - state->margins.header.value
		- state->margins.top.value - state->margins.bottom.value;
	gtk_spin_button_set_range (state->margins.footer.spin, 0., max);
}

static void
configure_bounds_bottom (PrinterSetupState *state)
{
	double max = state->height - state->margins.header.value
		- state->margins.footer.value - state->margins.top.value;
	gtk_spin_button_set_range (state->margins.bottom.spin, 0., max);
}

static void
configure_bounds_top (PrinterSetupState *state)
{
	double max = state->height - state->margins.header.value
		- state->margins.footer.value - state->margins.bottom.value;
	gtk_spin_button_set_range (state->margins.top.spin, 0., max);
}

static void
configure_bounds_left (PrinterSetupState *state)
{
	double max = state->width - state->margins.right.value;
	gtk_spin_button_set_range (state->margins.left.spin, 0., max);
}

static void
configure_bounds_right (PrinterSetupState *state)
{
	double max = state->width - state->margins.left.value;
	gtk_spin_button_set_range (state->margins.right.spin, 0., max);
}

static void
value_changed_header_cb (gpointer user_data)
{
	UnitInfo *target = user_data;

	target->value = gtk_spin_button_get_value (target->spin);

	configure_bounds_top (target->state);
	configure_bounds_bottom (target->state);
	configure_bounds_footer (target->state);

	draw_margin_header (target);
}

static void
value_changed_footer_cb (gpointer user_data)
{
	UnitInfo *target = user_data;

	target->value = gtk_spin_button_get_value (target->spin);

	configure_bounds_top (target->state);
	configure_bounds_bottom (target->state);
	configure_bounds_header (target->state);

	draw_margin_footer (target);
}

static void
value_changed_top_cb (gpointer user_data)
{
	UnitInfo *target = user_data;

	target->value = gtk_spin_button_get_value (target->spin);

	configure_bounds_header (target->state);
	configure_bounds_bottom (target->state);
	configure_bounds_footer (target->state);

	draw_margin_top (target);
	draw_margin_header (&target->state->margins.header);
}


static void
value_changed_bottom_cb (gpointer user_data)
{
	UnitInfo *target = user_data;

	target->value = gtk_spin_button_get_value (target->spin);

	configure_bounds_header (target->state);
	configure_bounds_top (target->state);
	configure_bounds_footer (target->state);

	draw_margin_bottom (target);
	draw_margin_footer (&target->state->margins.footer);
}

static void
value_changed_left_cb (gpointer user_data)
{
	UnitInfo *target = user_data;

	target->value = gtk_spin_button_get_value (target->spin);

	configure_bounds_right (target->state);

	draw_margin_left (target);
}

static void
value_changed_right_cb (gpointer user_data)
{
	UnitInfo *target = user_data;

	target->value = gtk_spin_button_get_value (target->spin);

	configure_bounds_left (target->state);

	draw_margin_right (target);
}

static void
margin_spin_configure (UnitInfo *target, PrinterSetupState *state,
		       const char *spin_name,
		       void (*value_changed_cb) (gpointer user_data))
{
	target->value = 0.;
	target->pi = &state->preview;
	target->spin = GTK_SPIN_BUTTON (glade_xml_get_widget (state->gui, spin_name));
	target->state = state;
	gtk_spin_button_set_update_policy (target->spin, GTK_UPDATE_IF_VALID);
	g_signal_connect_swapped (G_OBJECT (target->spin),
		"value_changed",
		G_CALLBACK (value_changed_cb), target);
	g_signal_connect_swapped (G_OBJECT (target->spin),
                "focus_in_event",
                G_CALLBACK (cb_spin_activated), target);
	g_signal_connect_swapped (G_OBJECT (target->spin),
		"focus_out_event",
		G_CALLBACK (cb_spin_deactivated), target);
}

static void
cb_unit_selector_changed (GtkComboBox *widget, PrinterSetupState *state)
{
	GtkTreeIter iter;
	GtkUnit unit;
	
	g_return_if_fail (state != NULL);

	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (state->unit_selector), &iter)) {
		gtk_tree_model_get (state->unit_model, &iter, 1, &unit, -1);

		do_fetch_margins (state);
		state->display_unit = unit;
		do_update_page (state);
	}
}

static gint
unit_sort_func (GtkTreeModel *model,
		GtkTreeIter *a, GtkTreeIter *b, gpointer user_data)
{
	char *str_a;
	char *str_b;
	gint result;

	gtk_tree_model_get (model, a, 0, &str_a, -1);
	gtk_tree_model_get (model, b, 0, &str_b, -1);
	
	result = g_utf8_collate (str_a, str_b);

	g_free (str_a);
	g_free (str_b);
	return result;
}


/**
 * Header and footer are stored with Excel semantics, but displayed with
 * more natural semantics. In Excel, both top margin and header are measured
 * from top of sheet. The Gnumeric user interface presents header as the
 * band between top margin and the print area. Bottom margin and footer are
 * handled likewise. See illustration at top of src/print.c
 */
static void
do_setup_margin (PrinterSetupState *state)
{
 	GtkWidget *table; 
	GtkBox *container;

	g_return_if_fail (state && state->pi);

	state->preview.canvas = foo_canvas_new ();
	foo_canvas_set_scroll_region (
		FOO_CANVAS (state->preview.canvas),
		0.0, 0.0, PREVIEW_X, PREVIEW_Y);
	gtk_widget_set_size_request (state->preview.canvas, PREVIEW_X, PREVIEW_Y);
	gtk_widget_show (state->preview.canvas);

	{
		GtkListStore *list_store;
		GtkTreeIter iter;
		GtkTreeIter current;
		GtkCellRenderer *text_renderer;
  
		list_store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);

		gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter, 0, (_("points")), 1, GTK_UNIT_POINTS, -1);
		if (GTK_UNIT_POINTS == state->display_unit)
			current = iter;
		gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter, 0, (_("inches")), 1, GTK_UNIT_INCH, -1);
		if (GTK_UNIT_INCH == state->display_unit)
			current = iter;
		gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter, 0, (_("mm")), 1, GTK_UNIT_MM, -1);
		if (GTK_UNIT_MM == state->display_unit)
			current = iter;

		gtk_tree_sortable_set_default_sort_func
			(GTK_TREE_SORTABLE (list_store),
			 unit_sort_func, NULL, NULL);
		gtk_tree_sortable_set_sort_column_id
			(GTK_TREE_SORTABLE (list_store),
			 GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
			 GTK_SORT_ASCENDING);

		state->unit_selector = gtk_combo_box_new_with_model (GTK_TREE_MODEL (list_store));
		state->unit_model    = GTK_TREE_MODEL (list_store);
		text_renderer = gtk_cell_renderer_text_new ();
		gtk_cell_layout_pack_start (GTK_CELL_LAYOUT(state->unit_selector), 
					    text_renderer, TRUE);
		gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT(state->unit_selector),
					       text_renderer, "text", 0);  
 
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (state->unit_selector), &current);
	}
	table = glade_xml_get_widget (state->gui, "table-paper-selector");
	gtk_table_attach (GTK_TABLE (table), state->unit_selector, 3, 4, 8, 9,
	                                GTK_FILL | GTK_EXPAND, 0, 0, 0);

 	g_signal_connect (G_OBJECT (state->unit_selector), "changed", 
 			  G_CALLBACK (cb_unit_selector_changed), state); 
 	gtk_widget_show (state->unit_selector);

	margin_spin_configure  (&state->margins.header, state, "spin-header",
				value_changed_header_cb);
	margin_spin_configure  (&state->margins.footer, state, "spin-footer",
				value_changed_footer_cb);
	margin_spin_configure  (&state->margins.top, state, "spin-top",
				value_changed_top_cb);
	margin_spin_configure  (&state->margins.bottom, state, "spin-bottom",
				value_changed_bottom_cb);
	margin_spin_configure  (&state->margins.left, state, "spin-left",
				value_changed_left_cb);
	margin_spin_configure  (&state->margins.right, state, "spin-right",
				value_changed_right_cb);

	state->check_center_h = glade_xml_get_widget (state->gui,
						      "check_center_h");
	state->check_center_v = glade_xml_get_widget (state->gui,
						      "check_center_v");
	gtk_toggle_button_set_active (
				      GTK_TOGGLE_BUTTON (state->check_center_v),
				      state->pi->center_vertically == 1);
	gtk_toggle_button_set_active (
				      GTK_TOGGLE_BUTTON (state->check_center_h),
				      state->pi->center_horizontally == 1);

	container = GTK_BOX (glade_xml_get_widget (state->gui,
						   "container-paper-sample"));
	gtk_box_pack_start (container, state->preview.canvas, TRUE, TRUE, 0);

}

#if 0

/* Display the header or footer preview in the print setup dialog.
 * Use the canvas widget in the HFPreviewInfo struct.
 *
 */
static void
display_hf_preview (PrinterSetupState *state, gboolean header)
{
	gchar *text = NULL;
	PrintHF *sample = NULL;
	HFRenderInfo *hfi;
	HFPreviewInfo *pi;

	g_return_if_fail (state != NULL);

	hfi = hf_render_info_new ();

	hfi->page = 1;
	hfi->pages = 99;
	hfi->sheet = state->sheet;

	if (header) {
		pi = state->pi_header;
		sample = state->header;
	} else {
		pi = state->pi_footer;
		sample = state->footer;
	}

	text = hf_format_render (sample->left_format, hfi, HF_RENDER_PRINT);
	foo_canvas_item_set (pi->left, "text", text ? text : "", NULL);
	g_free (text);

	text = hf_format_render (sample->middle_format, hfi, HF_RENDER_PRINT);
	foo_canvas_item_set (pi->middle, "text", text ? text : "", NULL);
	g_free (text);

	text  = hf_format_render (sample->right_format, hfi, HF_RENDER_PRINT);
	foo_canvas_item_set (pi->right, "text", text ? text : "", NULL);
	g_free (text);

	hf_render_info_destroy (hfi);
}

static void
do_header_customize (PrinterSetupState *state)
{
	do_hf_customize (TRUE, state);
}

static void
do_footer_customize (PrinterSetupState *state)
{
	do_hf_customize (FALSE, state);
}

static void
header_changed (GtkComboBox *menu, PrinterSetupState *state)
{
	GList *selection = g_list_nth (hf_formats,
		gtk_combo_box_get_active (menu));
	PrintHF *format = (selection)? selection->data: NULL;

	if (format == NULL) {
		do_header_customize (state);
	} else {
		print_hf_free (state->header);
		state->header = print_hf_copy (format);
	}
	
		display_hf_preview (state, TRUE);
}

static void
footer_changed (GtkComboBox *menu, PrinterSetupState *state)
{
	GList *selection = g_list_nth (hf_formats,
		gtk_combo_box_get_active (menu));
	PrintHF *format = (selection)? selection->data: NULL;

	if (format == NULL) {
		do_footer_customize (state);
	} else {
		print_hf_free (state->footer);
		state->footer = print_hf_copy (format);
	}

	display_hf_preview (state, FALSE);
}

/*
 * Fills one of the GtkCombos for headers or footers with the list
 * of existing header/footer formats
 */
static void
fill_hf (PrinterSetupState *state, GtkComboBox *om, GCallback callback, gboolean header)
{
	GList *l;
	HFRenderInfo *hfi;
	GtkListStore *store;
	GtkTreeIter iter;
	char *res;
	PrintHF *select = header ? state->header : state->footer;
	int i, idx = 0;

	hfi = hf_render_info_new ();
	hfi->page = 1;
	hfi->pages = 1;

	store = gtk_list_store_new (1, G_TYPE_STRING);
	gtk_combo_box_set_model (om, GTK_TREE_MODEL (store));

	for (i = 0, l = hf_formats; l; l = l->next, i++) {
		PrintHF *format = l->data;
		char *left, *middle, *right;

		if (print_hf_same (format, select))
			idx = i;

		left   = hf_format_render (format->left_format, hfi, HF_RENDER_PRINT);
		middle = hf_format_render (format->middle_format, hfi, HF_RENDER_PRINT);
		right  = hf_format_render (format->right_format, hfi, HF_RENDER_PRINT);

		res = g_strdup_printf (
			"%s%s%s%s%s",
			left, (*left && (*middle || *right)) ? ", " : "",
			middle, (*middle && *right) ? ", " : "",
			right);

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
					0, res,
					-1);

		g_free (res);
		g_free (left);
		g_free (middle);
		g_free (right);
	}

	/* Add menu option to customize the header/footer. */
	if (header)
		res = g_strdup_printf (_("Customize header"));
	else
		res = g_strdup_printf (_("Customize footer"));
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
				0, res,
				-1);
	g_free (res);

	gtk_combo_box_set_active (om, idx);
	g_signal_connect (G_OBJECT (om), "changed", callback, state);

	hf_render_info_destroy (hfi);
}

static void
do_setup_hf_menus (PrinterSetupState *state)
{
	GtkComboBox *header;
	GtkComboBox *footer;

	g_return_if_fail (state != NULL);

	header = GTK_COMBO_BOX (glade_xml_get_widget (state->gui, "option-menu-header"));
	footer = GTK_COMBO_BOX (glade_xml_get_widget (state->gui, "option-menu-footer"));

	if (state->header)
		fill_hf (state, header, G_CALLBACK (header_changed), TRUE);
	if (state->footer)
		fill_hf (state, footer, G_CALLBACK (footer_changed), FALSE);
}

static char *
text_get (GtkEditable *text_widget)
{
	return gtk_editable_get_chars (GTK_EDITABLE (text_widget), 0, -1);
}

static void
hf_customize_apply (GtkWidget *dialog)
{
	GladeXML *gui;
	GtkEntry *left, *middle, *right;
	char *left_format, *right_format, *middle_format;
	PrintHF **config = NULL;
	gboolean header;
	PrinterSetupState *state;

	g_return_if_fail (dialog != NULL);

	gui = glade_get_widget_tree (GTK_WIDGET (dialog));

        if (gui == NULL)
                return;

	left   = GTK_ENTRY (glade_xml_get_widget (gui, "left-format"));
	middle = GTK_ENTRY (glade_xml_get_widget (gui, "middle-format"));
	right  = GTK_ENTRY (glade_xml_get_widget (gui, "right-format"));

	left_format   = text_get (GTK_EDITABLE (left));
	middle_format = text_get (GTK_EDITABLE (middle));
	right_format  = text_get (GTK_EDITABLE (right));

	header = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (dialog), "header"));
	state = g_object_get_data (G_OBJECT (dialog), "state");

	if (header)
		config = &state->header;
	else
		config = &state->footer;

	print_hf_free (*config);
	*config = print_hf_new (left_format, middle_format, right_format);

	g_free (left_format);
	g_free (middle_format);
	g_free (right_format);

	print_hf_register (*config);

	do_setup_hf_menus (state);
	display_hf_preview (state, header);

	gtk_widget_set_sensitive (glade_xml_get_widget (gui, "apply_button"), FALSE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gui, "ok_button"), FALSE);
}


static void
hf_customize_ok (GtkWidget *dialog)
{
	hf_customize_apply (dialog);
	gtk_widget_destroy (dialog);
}

static gboolean
cb_hf_changed (GladeXML *gui)
{
	gtk_widget_set_sensitive (glade_xml_get_widget (gui, "apply_button"), TRUE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gui, "ok_button"), TRUE);
	return FALSE;
}

/*
 * Open up a DIALOG to allow the user to customize the header
 * or the footer.
 */
static void
do_hf_customize (gboolean header, PrinterSetupState *state)
{
	GladeXML *gui;
	GtkEntry *left, *middle, *right;
	GtkWidget *dialog;
	PrintHF **config = NULL;

	/* Check if this dialog isn't already created. */
	if (header)
		dialog = state->customize_header;
	else
		dialog = state->customize_footer;

	if (dialog != NULL) {
		gdk_window_show (dialog->window);
		gdk_window_raise (dialog->window);
		return;
	}

	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (state->wbcg),
		"hf-config.glade", NULL, NULL);
        if (gui == NULL)
                return;

	left   = GTK_ENTRY (glade_xml_get_widget (gui, "left-format"));
	middle = GTK_ENTRY (glade_xml_get_widget (gui, "middle-format"));
	right  = GTK_ENTRY (glade_xml_get_widget (gui, "right-format"));
	dialog = glade_xml_get_widget (gui, "hf-config");

	if (header) {
		config = &state->header;
		state->customize_header = dialog;
		gtk_window_set_title (GTK_WINDOW (dialog), _("Custom header configuration"));

	} else {
		config = &state->footer;
		state->customize_footer = dialog;
		gtk_window_set_title (GTK_WINDOW (dialog), _("Custom footer configuration"));
	}

	gtk_entry_set_text (left, (*config)->left_format);
	gtk_entry_set_text (middle, (*config)->middle_format);
	gtk_entry_set_text (right, (*config)->right_format);

	gnumeric_editable_enters (GTK_WINDOW (dialog), GTK_WIDGET (left));
	gnumeric_editable_enters (GTK_WINDOW (dialog), GTK_WIDGET (middle));
	gnumeric_editable_enters (GTK_WINDOW (dialog), GTK_WIDGET (right));

	g_signal_connect_swapped (G_OBJECT (glade_xml_get_widget (gui, "apply_button")),
		"clicked", G_CALLBACK (hf_customize_apply), dialog);
	g_signal_connect_swapped (G_OBJECT (glade_xml_get_widget (gui, "ok_button")),
		"clicked", G_CALLBACK (hf_customize_ok), dialog);
	g_signal_connect_swapped (G_OBJECT (glade_xml_get_widget (gui, "cancel_button")),
		"clicked", G_CALLBACK (gtk_widget_destroy), dialog);
	gtk_widget_set_sensitive (glade_xml_get_widget (gui, "apply_button"), FALSE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gui, "ok_button"), FALSE);

	if (header)
		g_signal_connect (G_OBJECT (dialog), "destroy",
				  G_CALLBACK (gtk_widget_destroyed), &state->customize_header);
	else
		g_signal_connect (G_OBJECT (dialog), "destroy",
				  G_CALLBACK (gtk_widget_destroyed), &state->customize_footer);


	/* Remember whether it is customizing header or footer. */
	g_object_set_data (G_OBJECT (dialog), "header", GINT_TO_POINTER (header));
	g_object_set_data (G_OBJECT (dialog), "state", state);

	/* Setup bindings to mark when the entries are modified. */
	g_signal_connect_swapped (G_OBJECT (left),
		"changed", G_CALLBACK (cb_hf_changed), gui);
	g_signal_connect_swapped (G_OBJECT (middle),
		"changed", G_CALLBACK (cb_hf_changed), gui);
	g_signal_connect_swapped (G_OBJECT (right),
		"changed", G_CALLBACK (cb_hf_changed), gui);


	gnumeric_init_help_button (glade_xml_get_widget (gui, "help_button"),
		header  ? GNUMERIC_HELP_LINK_PRINTER_SETUP_HEADER_CUSTOMIZATION
			: GNUMERIC_HELP_LINK_PRINTER_SETUP_FOOTER_CUSTOMIZATION);

	/* Let them begin typing into the first entry widget. */
	gtk_widget_grab_focus (GTK_WIDGET (left));

	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (state->dialog));
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ON_PARENT);

	gtk_widget_show (dialog);
}

/* header/footer_preview_event
 * If the user double clicks on a header/footer preview canvas, we will
 * open up the dialog to modify the header or footer.
 * They can also do this from the option menu.
 */
static gboolean
header_preview_event (G_GNUC_UNUSED FooCanvas *canvas,
		      GdkEvent *event, PrinterSetupState *state)
{
	if (event == NULL ||
	    event->type != GDK_2BUTTON_PRESS ||
	    event->button.button != 1)
		return FALSE;
	do_hf_customize (TRUE, state);
	return TRUE;
}

static gboolean
footer_preview_event (G_GNUC_UNUSED FooCanvas *canvas,
		      GdkEvent *event, PrinterSetupState *state)
{
	if (event == NULL ||
	    event->type != GDK_2BUTTON_PRESS ||
	    event->button.button != 1)
		return FALSE;
	do_hf_customize (FALSE, state);
	return TRUE;
}

/* create_hf_preview_canvas
 * Creates the canvas to do a header or footer preview in the print setup.
 *
 */
static void
create_hf_preview_canvas (PrinterSetupState *state, gboolean header)
{
	GtkWidget *wid;
	HFPreviewInfo *pi;
	PangoFontDescription *font_desc;
	gdouble width = HF_PREVIEW_X;
	gdouble height = HF_PREVIEW_Y;
	gdouble shadow = HF_PREVIEW_SHADOW;
	gdouble padding = HF_PREVIEW_PADDING;
	gdouble margin = HF_PREVIEW_MARGIN;
	gdouble bottom_margin = height - margin;

	pi = g_new (HFPreviewInfo, 1);

	if (header)
		state->pi_header = pi;
	else
		state->pi_footer = pi;

	pi->canvas = foo_canvas_new ();

	foo_canvas_set_scroll_region (FOO_CANVAS (pi->canvas), 0.0, 0.0, width, width);

        foo_canvas_item_new (foo_canvas_root (FOO_CANVAS (pi->canvas)),
		FOO_TYPE_CANVAS_RECT,
		"x1",		shadow,
		"y1",		(header ? shadow : 0),
		"x2",		width + shadow,
		"y2",		height + (header ? 0 : shadow),
		"fill-color",	"black",
		NULL);

        foo_canvas_item_new (foo_canvas_root (FOO_CANVAS (pi->canvas)),
		FOO_TYPE_CANVAS_RECT,
		"x1",		0.0,
		"y1",		0.0,
		"x2",		width,
		"y2",		height,
		"fill-color",	"white",
		NULL);

	/* Use the Gnumeric default font. */
	font_desc = pango_font_description_new ();
	pango_font_description_set_family (font_desc, DEFAULT_FONT);
	pango_font_description_set_style (font_desc, PANGO_STYLE_NORMAL);
	pango_font_description_set_variant (font_desc, PANGO_VARIANT_NORMAL);
	pango_font_description_set_weight (font_desc, PANGO_WEIGHT_NORMAL);
	pango_font_description_set_size (font_desc, 8 * PANGO_SCALE);

	pi->left = foo_canvas_item_new (
		foo_canvas_root (FOO_CANVAS (pi->canvas)),
		foo_canvas_text_get_type (),
		"x",		padding,
		"y",		header ? margin : bottom_margin,
		"anchor",	GTK_ANCHOR_WEST,
		"font-desc",	font_desc,
		"fill-color",	"black",
		"text",		"Left",
		NULL);

	pi->middle = foo_canvas_item_new (
		foo_canvas_root (FOO_CANVAS (pi->canvas)),
		foo_canvas_text_get_type (),
		"x",		width / 2,
		"y",		header ? margin : bottom_margin,
		"anchor",	GTK_ANCHOR_CENTER,
		"font-desc",	font_desc,
		"fill-color",	"black",
		"text",		"Center",
		NULL);

	pi->right = foo_canvas_item_new (
		foo_canvas_root (FOO_CANVAS (pi->canvas)),
		foo_canvas_text_get_type (),
		"x",		width - padding,
		"y",		header ? margin : bottom_margin,
		"anchor",	GTK_ANCHOR_EAST,
		"font-desc",    font_desc,
		"fill-color",	"black",
		"text",		"Right",
		NULL);

	pango_font_description_free (font_desc);

	gtk_widget_show_all (pi->canvas);

	if (header) {
		g_signal_connect (G_OBJECT (pi->canvas),
			"event",
			G_CALLBACK (header_preview_event), state);
		wid = glade_xml_get_widget (state->gui, "container-header-sample");
	} else {
		g_signal_connect (G_OBJECT (pi->canvas),
			"event",
			G_CALLBACK (footer_preview_event), state);
		wid = glade_xml_get_widget (state->gui, "container-footer-sample");
	}
	gtk_widget_set_size_request (wid, width, height);


	gtk_box_pack_start (GTK_BOX (wid), GTK_WIDGET (pi->canvas), TRUE, TRUE, 0);
}


/*
 * Setup the widgets for the header and footer tab.
 *
 * Creates the two preview canvas's for the header and footer.
 */
static void
do_setup_hf (PrinterSetupState *state)
{
	GtkComboBox *header;
	GtkComboBox *footer;
	GtkCellRenderer *renderer;

	g_return_if_fail (state != NULL);

	header = GTK_COMBO_BOX (glade_xml_get_widget (state->gui, "option-menu-header"));
	renderer = (GtkCellRenderer*) gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (header), renderer, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (header), renderer,
                                        "text", 0,
                                        NULL);
	footer = GTK_COMBO_BOX (glade_xml_get_widget (state->gui, "option-menu-footer"));
	renderer = (GtkCellRenderer*) gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (footer), renderer, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (footer), renderer,
                                        "text", 0,
                                        NULL);

	state->header = print_hf_copy (state->pi->header ? state->pi->header :
				     hf_formats->data);
	state->footer = print_hf_copy (state->pi->footer ? state->pi->footer :
				     hf_formats->data);

	do_setup_hf_menus (state);

	create_hf_preview_canvas (state, TRUE);
	create_hf_preview_canvas (state, FALSE);

	display_hf_preview (state, TRUE);
	display_hf_preview (state, FALSE);
}
#endif

static void
display_order_icon (GtkToggleButton *toggle, PrinterSetupState *state)
{
	GtkWidget *show, *hide;

	if (toggle->active){
		show = state->icon_rd;
		hide = state->icon_dr;
	} else {
		hide = state->icon_rd;
		show = state->icon_dr;
	}

	gtk_widget_show (show);
	gtk_widget_hide (hide);
}

static void
load_print_area (PrinterSetupState *state)
{
	GnmRange print_area;
	
	print_area = sheet_get_nominal_printarea
		(wb_control_cur_sheet
		 (WORKBOOK_CONTROL (state->wbcg)));
	gnm_expr_entry_load_from_range
		(state->area_entry,
		 wb_control_cur_sheet (WORKBOOK_CONTROL (state->wbcg)),
		 &print_area);
}



static void
do_setup_page_info (PrinterSetupState *state)
{
	GtkWidget *pa_hbox   = glade_xml_get_widget (state->gui,
						     "print-area-hbox");
	GtkWidget *repeat_table = glade_xml_get_widget (state->gui,
							"repeat-table");
	GtkWidget *gridlines = glade_xml_get_widget (state->gui, "check-grid-lines");
	GtkWidget *onlystyles= glade_xml_get_widget (state->gui, "check-only-styles");
	GtkWidget *bw        = glade_xml_get_widget (state->gui, "check-black-white");
	GtkWidget *titles    = glade_xml_get_widget (state->gui, "check-print-titles");
	GtkWidget *order_rd  = glade_xml_get_widget (state->gui, "radio-order-right");
	GtkWidget *order_dr  = glade_xml_get_widget (state->gui, "radio-order-down");
	GtkWidget *order_table = glade_xml_get_widget (state->gui, "page-order-table");
	GtkWidget *order;

	state->area_entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->area_entry,
		GNM_EE_SHEET_OPTIONAL,
		GNM_EE_SHEET_OPTIONAL);
	gtk_box_pack_start (GTK_BOX (pa_hbox), GTK_WIDGET (state->area_entry),
			    TRUE, TRUE, 0);
	gtk_widget_show (GTK_WIDGET (state->area_entry));

	state->top_entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->top_entry,
		GNM_EE_SINGLE_RANGE | GNM_EE_FULL_ROW | GNM_EE_SHEET_OPTIONAL,
		GNM_EE_SINGLE_RANGE | GNM_EE_FULL_ROW | GNM_EE_ABS_ROW | GNM_EE_SHEET_OPTIONAL);
	gtk_table_attach (GTK_TABLE (repeat_table),
			  GTK_WIDGET (state->top_entry),
			  1, 2, 0, 1,
			  GTK_EXPAND|GTK_FILL, 0, 0, 0);
	gtk_widget_show (GTK_WIDGET (state->top_entry));

	state->left_entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->left_entry,
		GNM_EE_SINGLE_RANGE | GNM_EE_FULL_COL | GNM_EE_SHEET_OPTIONAL,
		GNM_EE_SINGLE_RANGE | GNM_EE_FULL_COL | GNM_EE_ABS_COL | GNM_EE_SHEET_OPTIONAL);
	gtk_table_attach (GTK_TABLE (repeat_table),
			  GTK_WIDGET (state->left_entry),
			  1, 2, 1, 2,
			  GTK_EXPAND|GTK_FILL, 0, 0, 0);
	gtk_widget_show (GTK_WIDGET (state->left_entry));

	state->icon_rd = gnumeric_load_image ("right-down.png");
	state->icon_dr = gnumeric_load_image ("down-right.png");

	gtk_widget_hide (state->icon_dr);
	gtk_widget_hide (state->icon_rd);

	gtk_table_attach (
		GTK_TABLE (order_table), state->icon_rd,
		2, 3, 0, 2, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach (
		GTK_TABLE (order_table), state->icon_dr,
		2, 3, 0, 2, GTK_FILL, GTK_FILL, 0, 0);

	g_signal_connect (G_OBJECT (order_rd), "toggled", G_CALLBACK (display_order_icon), state);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gridlines), 
				      state->pi->print_grid_lines);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (onlystyles), 
				      state->pi->print_even_if_only_styles);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (bw), 
				      state->pi->print_black_and_white);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (titles), 
				      state->pi->print_titles);

	order = state->pi->print_across_then_down ? order_rd : order_dr;
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (order), TRUE);
	display_order_icon (GTK_TOGGLE_BUTTON (order_rd), state);

	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (gnm_expr_entry_get_entry (state->area_entry)));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (gnm_expr_entry_get_entry (state->top_entry)));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (gnm_expr_entry_get_entry (state->left_entry)));
/* 	gnumeric_editable_enters (GTK_WINDOW (state->dialog), */
/* 		gtk_bin_get_child (GTK_BIN (glade_xml_get_widget (state->gui, "comments-combo")))); */

	if (state->pi->repeat_top.use)
		gnm_expr_entry_load_from_range (
			state->top_entry,
			wb_control_cur_sheet (WORKBOOK_CONTROL (state->wbcg)),
			&state->pi->repeat_top.range);

	if (state->pi->repeat_left.use)
		gnm_expr_entry_load_from_range (
			state->left_entry,
			wb_control_cur_sheet (WORKBOOK_CONTROL (state->wbcg)),
			&state->pi->repeat_left.range);
	load_print_area (state);
}


static void
do_update_margin (UnitInfo *margin, double value, GtkUnit unit)
{
	margin->value = value;
	gtk_spin_button_set_range (margin->spin, 0. , 2. * value);
	gtk_spin_button_set_value (margin->spin, value);
	switch (unit) {
	case GTK_UNIT_MM:
		gtk_spin_button_set_digits (margin->spin, 1);
		gtk_spin_button_set_increments (margin->spin, 1., 0.);
		break;
	case GTK_UNIT_INCH:
		gtk_spin_button_set_digits (margin->spin, 3);
		gtk_spin_button_set_increments (margin->spin, 0.125, 0.);
		break;
	case GTK_UNIT_POINTS:
		gtk_spin_button_set_digits (margin->spin, 1);
		gtk_spin_button_set_increments (margin->spin, 1., 0.);
		break;
	case GTK_UNIT_PIXEL:
		break;
	}
}

static void
do_update_page (PrinterSetupState *state)
{
	PrintInformation *pi = state->pi;
	GladeXML *gui;
	double top, bottom;
	double left, right;
	double edge_to_below_header, edge_to_above_footer;
	char *text;
	char const *format;
	double scale;

	gui = state->gui;

	gtk_label_set_text (GTK_LABEL (glade_xml_get_widget (gui,
							     "paper-type-label")),
			    print_info_get_paper_display_name (pi));
	state->height = print_info_get_paper_height (pi,state->display_unit);
	state->width  = print_info_get_paper_width  (pi,state->display_unit);
	switch (state->display_unit) {
	case GTK_UNIT_PIXEL:
		format = _("%.0f pixels wide by %.0f pixels tall");
		break;
	case GTK_UNIT_POINTS:
		format = _("%.0f points wide by %.0f points tall");
		break;
	case GTK_UNIT_INCH:
		format = _("%.1f in wide by %.1f in tall");
		break;
	case GTK_UNIT_MM:
		format = _("%.0f mm wide by %.0f mm tall");
		break;
	default:
		format = _("%.1f wide by %.1f tall");
		break;
	}
	text = g_strdup_printf (format, state->width, state->height);
	gtk_label_set_text (GTK_LABEL (glade_xml_get_widget (gui,
							     "paper-size-label")),
			    text);
	g_free (text);

	print_info_get_margins (state->pi,
				&top, &bottom,
				&left, &right,
				&edge_to_below_header,
				&edge_to_above_footer);
	scale = get_conversion_factor (state->display_unit);
	do_update_margin (&state->margins.top, top / scale,
			  state->display_unit);
	do_update_margin (&state->margins.bottom, bottom / scale,
			  state->display_unit);
	do_update_margin (&state->margins.left, left / scale,
			  state->display_unit);
	do_update_margin (&state->margins.right, right / scale,
			  state->display_unit);
	do_update_margin (&state->margins.header,
			  (edge_to_below_header - top) / scale,
			  state->display_unit);
	do_update_margin (&state->margins.footer,
			  (edge_to_above_footer - bottom) / scale,
			  state->display_unit);
	configure_bounds_top (state);
	configure_bounds_header (state);
	configure_bounds_left (state);
	configure_bounds_right (state);
	configure_bounds_footer (state);
	configure_bounds_bottom (state);

	canvas_update (state);
}


static void
do_setup_page (PrinterSetupState *state)
{
/* 	GtkTable *table; */
	GladeXML *gui;

	gui = state->gui;

 	g_signal_connect_swapped (G_OBJECT (glade_xml_get_widget (gui, "paper-button")), 
 		"clicked", 
 		G_CALLBACK (dialog_gtk_printer_setup_cb), state); 

	do_setup_margin (state);
	
	do_update_page (state);
	
}


static void
scaling_percent_changed (GtkToggleButton *toggle, PrinterSetupState *state)
{
	gboolean scale_percent = gtk_toggle_button_get_active (toggle);

	gtk_widget_set_sensitive (GTK_WIDGET (glade_xml_get_widget (state->gui, "scale-percent-spin")), scale_percent);
	gtk_widget_set_sensitive (GTK_WIDGET (glade_xml_get_widget (state->gui, "scale-percent-label")), scale_percent);
}

static void
scaling_fit_to_h_changed (GtkToggleButton *toggle, PrinterSetupState *state)
{
	gboolean scale_fit_to_h   = gtk_toggle_button_get_active (toggle);
	gtk_widget_set_sensitive 
		(GTK_WIDGET (glade_xml_get_widget (state->gui, "scale-h-spin")), 
		 scale_fit_to_h);
	gtk_widget_set_sensitive 
		(GTK_WIDGET (glade_xml_get_widget (state->gui, "fit-h-check-label")), 
		 scale_fit_to_h);
}

static void
scaling_fit_to_v_changed (GtkToggleButton *toggle, PrinterSetupState *state)
{
	gboolean scale_fit_to_v   = gtk_toggle_button_get_active (toggle);
        gtk_widget_set_sensitive
                (GTK_WIDGET (glade_xml_get_widget (state->gui, "scale-v-spin")),
                 scale_fit_to_v);
        gtk_widget_set_sensitive
                (GTK_WIDGET (glade_xml_get_widget (state->gui, "fit-v-check-label")),
                 scale_fit_to_v);
}                           

static void
scaling_fit_to_changed (GtkToggleButton *toggle, PrinterSetupState *state)
{
	gboolean scale_fit_to  = gtk_toggle_button_get_active (toggle);

	if (scale_fit_to) {
		scaling_fit_to_h_changed (GTK_TOGGLE_BUTTON 
					  (glade_xml_get_widget (state->gui, "fit-h-check")), state);
		scaling_fit_to_v_changed (GTK_TOGGLE_BUTTON
					  (glade_xml_get_widget (state->gui, "fit-v-check")), state);
	} else {
		gtk_widget_set_sensitive
			(GTK_WIDGET (glade_xml_get_widget (state->gui, "scale-v-spin")), FALSE);
		gtk_widget_set_sensitive
			(GTK_WIDGET (glade_xml_get_widget (state->gui, "fit-v-check-label")), FALSE);
		gtk_widget_set_sensitive
			(GTK_WIDGET (glade_xml_get_widget (state->gui, "scale-h-spin")), FALSE);
		gtk_widget_set_sensitive
			(GTK_WIDGET (glade_xml_get_widget (state->gui, "fit-h-check-label")), FALSE);
	}
	gtk_widget_set_sensitive
		(GTK_WIDGET (glade_xml_get_widget (state->gui, "fit-h-check")), scale_fit_to);
	gtk_widget_set_sensitive
		(GTK_WIDGET (glade_xml_get_widget (state->gui, "fit-v-check")), scale_fit_to);
}

static void
do_setup_scale (PrinterSetupState *state)
{
	PrintInformation *pi = state->pi;
	GtkWidget *scale_percent_spin, *scale_width_spin, *scale_height_spin;
	GladeXML *gui;

	gui = state->gui;

	state->scale_percent_radio = glade_xml_get_widget (gui, "scale-percent-radio");
	state->scale_fit_to_radio = glade_xml_get_widget (gui, "scale-fit-to-radio");
	state->scale_no_radio = glade_xml_get_widget (gui, "scale-no-radio");

	g_signal_connect (G_OBJECT (state->scale_percent_radio), "toggled",
			  G_CALLBACK (scaling_percent_changed), state);
	g_signal_connect (G_OBJECT (state->scale_fit_to_radio), "toggled",
			  G_CALLBACK (scaling_fit_to_changed), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "fit-h-check")),
			  "toggled", G_CALLBACK (scaling_fit_to_h_changed), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "fit-v-check")),
			  "toggled", G_CALLBACK (scaling_fit_to_v_changed), state);

	scaling_percent_changed (GTK_TOGGLE_BUTTON (state->scale_percent_radio), state);
	scaling_fit_to_changed (GTK_TOGGLE_BUTTON (state->scale_fit_to_radio), state);
	

 	if (pi->scaling.type == PRINT_SCALE_PERCENTAGE) { 
		if (pi->scaling.percentage.x  == 100.)
			gtk_toggle_button_set_active
				(GTK_TOGGLE_BUTTON (state->scale_no_radio), TRUE);
		else
			gtk_toggle_button_set_active
				(GTK_TOGGLE_BUTTON (state->scale_percent_radio), TRUE);
 	} else { 
 		gtk_toggle_button_set_active  
 			(GTK_TOGGLE_BUTTON (state->scale_fit_to_radio), TRUE); 
	} 
	
	scale_percent_spin = glade_xml_get_widget (gui, "scale-percent-spin");
	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (scale_percent_spin), pi->scaling.percentage.x);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				      GTK_WIDGET (scale_percent_spin));

	scale_width_spin = glade_xml_get_widget (gui, "scale-h-spin");
	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (scale_width_spin), pi->scaling.dim.cols);
	gtk_toggle_button_set_active
		(GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "fit-h-check")),
		 pi->scaling.dim.cols > 0);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				      GTK_WIDGET (scale_width_spin));

	scale_height_spin = glade_xml_get_widget (gui, "scale-v-spin");
	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (scale_height_spin), pi->scaling.dim.rows);
	gtk_toggle_button_set_active
	        (GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "fit-v-check")),
		 pi->scaling.dim.rows > 0);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				      GTK_WIDGET (scale_height_spin));
}


static Sheet *
print_setup_get_sheet (PrinterSetupState *state)
{
	GtkWidget *w = glade_xml_get_widget (state->gui, "apply-to-all");
	gboolean apply_all_sheets = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	gnm_gconf_set_all_sheets (apply_all_sheets);

	if (apply_all_sheets)
		return NULL;
	return workbook_sheet_by_index (state->sheet->workbook,
					gtk_combo_box_get_active (GTK_COMBO_BOX
								     (state->sheet_selector)));
}

static void
cb_do_print_preview (PrinterSetupState *state)
{
	PrintInformation *old_pi;

	fetch_settings (state);
	old_pi = state->sheet->print_info;
	state->sheet->print_info = state->pi;
	gnm_print_sheet (WORKBOOK_CONTROL (state->wbcg),
		state->sheet, TRUE, PRINT_ACTIVE_SHEET, NULL);
	state->sheet->print_info = old_pi;
}

static void
cb_do_print_cancel (PrinterSetupState *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_do_print_ok (PrinterSetupState *state)
{
#if 0
#warning
	/* Detach BEFORE we finish editing
	 *
	 * ?? why do we need to do this 
	 */
	wbc_gtk_detach_guru (state->wbcg);
#endif
	wbcg_edit_finish (state->wbcg, WBC_EDIT_ACCEPT, NULL);
	fetch_settings (state);
	if (gtk_toggle_button_get_active (
		    GTK_TOGGLE_BUTTON (
			    glade_xml_get_widget (state->gui, 
						  "is_default_check")))) {
		print_info_save (state->pi);
	}
	cmd_print_setup (WORKBOOK_CONTROL (state->wbcg),
		print_setup_get_sheet (state), state->pi);
	gtk_widget_destroy (state->dialog);
}

static void
cb_do_print (PrinterSetupState *state)
{
	cb_do_print_ok (state);
	gnm_print_sheet (WORKBOOK_CONTROL (state->wbcg),
		state->sheet, FALSE, PRINT_ACTIVE_SHEET, NULL);
}

static void
cb_do_print_destroy (PrinterSetupState *state)
{
	wbcg_edit_finish (state->wbcg, WBC_EDIT_REJECT, NULL);

/* 	if (state->customize_header) */
/* 		gtk_widget_destroy (state->customize_header); */

/* 	if (state->customize_footer) */
/* 		gtk_widget_destroy (state->customize_footer); */

	g_object_unref (state->gui);
/* 	g_object_unref (state->gp_config); */

/* 	print_hf_free (state->header); */
/* 	print_hf_free (state->footer); */
	print_info_free (state->pi);
/* 	g_free (state->pi_header); */
/* 	g_free (state->pi_footer); */
	g_object_unref (state->unit_model);
	g_free (state);
}

static void
cb_do_sheet_selector_toggled (GtkToggleButton *togglebutton,
			      PrinterSetupState *state)
{
	gboolean all_sheets = gtk_toggle_button_get_active (togglebutton);

	gtk_widget_set_sensitive (state->sheet_selector, !all_sheets);
}

static void
do_setup_sheet_selector (PrinterSetupState *state)
{
	GtkWidget *table, *w;
	int i, n, n_this = 0;

	g_return_if_fail (state != NULL);
	g_return_if_fail (state->sheet != NULL);

	table = glade_xml_get_widget (state->gui, "table-sheet");
	state->sheet_selector = gtk_combo_box_new_text ();
	n = workbook_sheet_count (state->sheet->workbook);
	for (i = 0 ; i < n ; i++) {
		Sheet * a_sheet = workbook_sheet_by_index (state->sheet->workbook, i);
		if (a_sheet == state->sheet)
			n_this = i;
		gtk_combo_box_append_text (GTK_COMBO_BOX (state->sheet_selector),
					a_sheet->name_unquoted);
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (state->sheet_selector), n_this);
	gtk_table_attach (GTK_TABLE (table), state->sheet_selector,
			  1, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	w = glade_xml_get_widget (state->gui, "apply-to-all");
	g_signal_connect (G_OBJECT (w),
		"toggled",
		G_CALLBACK (cb_do_sheet_selector_toggled), state);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w),
				      gnm_app_prefs->print_all_sheets);
	cb_do_sheet_selector_toggled (GTK_TOGGLE_BUTTON (w), state);
	w = glade_xml_get_widget (state->gui, "apply-to-selected");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w),
				      !gnm_app_prefs->print_all_sheets);
	gtk_widget_show_all (table);
}

static void
do_setup_main_dialog (PrinterSetupState *state)
{
	GtkWidget *w;

	g_return_if_fail (state != NULL);
	g_return_if_fail (state->sheet != NULL);
	g_return_if_fail (state->wbcg != NULL);

	state->dialog = glade_xml_get_widget (state->gui, "print-setup");

 	w = glade_xml_get_widget (state->gui, "ok"); 
 	g_signal_connect_swapped (G_OBJECT (w), 
 		"clicked", 
 		G_CALLBACK (cb_do_print_ok), state); 
 	w = glade_xml_get_widget (state->gui, "print"); 
 	g_signal_connect_swapped (G_OBJECT (w), 
 		"clicked", 
 		G_CALLBACK (cb_do_print), state);
	w = glade_xml_get_widget (state->gui, "preview");
	g_signal_connect_swapped (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_do_print_preview), state);
	w = glade_xml_get_widget (state->gui, "cancel");
	g_signal_connect_swapped (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_do_print_cancel), state);
	
	w = glade_xml_get_widget (state->gui, "print-setup-notebook");
	g_signal_connect (G_OBJECT (w),
		"switch-page",
		G_CALLBACK (notebook_flipped), state);
	gtk_notebook_set_current_page (GTK_NOTEBOOK (w), 0);
	
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_do_print_destroy);
	wbc_gtk_attach_guru (state->wbcg, state->dialog);

}



static PrinterSetupState *
printer_setup_state_new (WBCGtk *wbcg, Sheet *sheet)
{
	PrinterSetupState *state;
	GladeXML *gui;

	GtkWidget *page, *notebook;

	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"print.glade", NULL, NULL);
        if (gui == NULL)
                return NULL;

	state = g_new0 (PrinterSetupState, 1);
	state->wbcg  = wbcg;
	state->sheet = sheet;
	state->gui   = gui;
	state->pi    = print_info_dup (sheet->print_info);
	state->display_unit = state->pi->desired_display.top;
/* 	state->customize_header = NULL; */
/* 	state->customize_footer = NULL; */

	do_setup_main_dialog (state);
	do_setup_sheet_selector (state);
/* 	do_setup_hf (state); */
	do_setup_page_info (state);
	do_setup_page (state);
	do_setup_scale (state);

	
	notebook = glade_xml_get_widget (state->gui, "print-setup-notebook");
	if ((page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook), 2)) != NULL)
		gtk_widget_destroy (page);

	return state;
}

static void
do_fetch_page (PrinterSetupState *state)
{
	state->pi->center_horizontally = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->check_center_h));
	state->pi->center_vertically = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->check_center_v));
}

static void
do_fetch_unit (PrinterSetupState *state)
{
	if (state->display_unit != state->pi->desired_display.top) {
		state->pi->desired_display.top = state->display_unit;
		state->pi->desired_display.bottom = state->display_unit;
		state->pi->desired_display.header = state->display_unit;
		state->pi->desired_display.footer = state->display_unit;
		state->pi->desired_display.left = state->display_unit;
		state->pi->desired_display.right = state->display_unit;
	}
}

static void
do_fetch_scale (PrinterSetupState *state)
{
	GtkWidget *w;
	GladeXML *gui = state->gui;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->scale_no_radio))) {
		state->pi->scaling.percentage.x = state->pi->scaling.percentage.y = 100.;
		state->pi->scaling.type = PRINT_SCALE_PERCENTAGE;
	} else {
		w = glade_xml_get_widget (gui, "scale-percent-spin");
		state->pi->scaling.percentage.x = state->pi->scaling.percentage.y
			= gtk_spin_button_get_value (GTK_SPIN_BUTTON (w));
		state->pi->scaling.type =
			((gtk_toggle_button_get_active
			  (GTK_TOGGLE_BUTTON (state->scale_percent_radio))) ?
			   PRINT_SCALE_PERCENTAGE : PRINT_SCALE_FIT_PAGES);
	}
	w = glade_xml_get_widget (gui, "fit-h-check");
	if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)))
		state->pi->scaling.dim.cols = 0;
	else {
		w = glade_xml_get_widget (gui, "scale-h-spin");
		state->pi->scaling.dim.cols =
			gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (w));
	}
	
	w = glade_xml_get_widget (gui, "fit-v-check");
	if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)))
		state->pi->scaling.dim.rows = 0;
	else {
		w = glade_xml_get_widget (gui, "scale-v-spin");
		state->pi->scaling.dim.rows =
			gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (w));
	}
}

/**
 * Header and footer are stored with Excel semantics, but displayed with
 * more natural semantics. In Excel, both top margin and header are measured
 * from top of sheet. See illustration at start of src/print.c. The Gnumeric
 * user interface presents header as the band between top margin and the
 * print area. Bottom margin and footer are handled likewise.
 */
static void
do_fetch_margins (PrinterSetupState *state)
{
/* 	GtkToggleButton *t; */
	double header, footer, top, bottom, left, right;
	GtkPageSetup     *ps = print_info_get_page_setup (state->pi);
	double factor = get_conversion_factor (state->display_unit);

	header = state->margins.header.value;
	footer = state->margins.footer.value;
	top = state->margins.top.value;
	bottom = state->margins.bottom.value;
	left = state->margins.left.value;
	right = state->margins.right.value;

	gtk_page_setup_set_top_margin (ps, top, state->display_unit);
	gtk_page_setup_set_bottom_margin (ps, bottom, state->display_unit);
	gtk_page_setup_set_left_margin (ps, left, state->display_unit);
	gtk_page_setup_set_right_margin (ps, right, state->display_unit);
	
	header += top;
	footer += bottom;

	print_info_set_edge_to_above_footer (state->pi, footer * factor);
	print_info_set_edge_to_below_header (state->pi, header * factor);
}

#if 0

static void
do_fetch_hf (PrinterSetupState *state)
{
	print_hf_free (state->pi->header);
	print_hf_free (state->pi->footer);

	state->pi->header = print_hf_copy (state->header);
	state->pi->footer = print_hf_copy (state->footer);
}

#endif

static void
do_fetch_page_info (PrinterSetupState *state)
{
	PrintInformation *pi = state->pi;

	pi->print_grid_lines = gtk_toggle_button_get_active 
		(GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "check-grid-lines")));
	pi->print_even_if_only_styles = gtk_toggle_button_get_active 
		(GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "check-only-styles")));
	pi->print_black_and_white = gtk_toggle_button_get_active 
		(GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "check-black-white")));
	pi->print_titles = gtk_toggle_button_get_active 
		(GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "check-print-titles")));
	pi->print_across_then_down = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "radio-order-right")));
	pi->repeat_top.use = gnm_expr_entry_get_rangesel (state->top_entry,
		&pi->repeat_top.range, NULL);
	pi->repeat_left.use = gnm_expr_entry_get_rangesel (state->left_entry,
		&pi->repeat_left.range, NULL);
}

static void
fetch_settings (PrinterSetupState *state)
{
	do_fetch_page (state);
	do_fetch_scale (state);
	do_fetch_margins (state);
	do_fetch_unit (state);
/* 	do_fetch_hf (state); */
	do_fetch_page_info (state);
}


static void
dialog_printer_setup_done_cb (GtkPageSetup *page_setup,
			      gpointer data)
{
	if (page_setup) {
		PrinterSetupState *state = data;
		print_info_set_page_setup (state->pi, page_setup);
		do_update_page (state);
	}
}

static void
dialog_gtk_printer_setup_cb (PrinterSetupState *state)
{
	GtkPageSetup *page_setup = print_info_get_page_setup (state->pi);
	
	gtk_print_run_page_setup_dialog_async
		(GTK_WINDOW (state->dialog),
		 page_setup,
		 NULL,
		 dialog_printer_setup_done_cb,
		 state);

	if (page_setup)
		g_object_unref (page_setup);
}



void
dialog_printer_setup (WBCGtk *wbcg, Sheet *sheet)
{
	PrinterSetupState *state;

	/* Only one guru per workbook. */
	if (wbc_gtk_get_guru (wbcg))
		return;

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, PRINTER_SETUP_KEY))
		return;

	state = printer_setup_state_new (wbcg, sheet);
	if (!state)
		return;

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_PRINTER_SETUP_GENERAL);
	gnumeric_keyed_dialog (
		wbcg, GTK_WINDOW (state->dialog), PRINTER_SETUP_KEY);
	gtk_widget_show (state->dialog);
}
