/*
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <gui-util.h>
#include <commands.h>
#include <print-info.h>
#include <print.h>
#include <ranges.h>
#include <sheet.h>
#include <sheet-object-graph.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <style.h>
#include <gnumeric-conf.h>

#include <gdk/gdkkeysyms.h>
#include <goffice/goffice.h>
#include <string.h>

#define PREVIEW_MARGIN_X 20
#define PREVIEW_MARGIN_Y 20

#define MARGIN_COLOR_DEFAULT 0xa9a9a9ff /* dark gray */
#define MARGIN_COLOR_ACTIVE GO_COLOR_RED

/* FIXME: do not hardcode pixel counts.  */
#define HF_PREVIEW_X 350
#define HF_PREVIEW_Y 75
#define HF_PREVIEW_SHADOW 2
#define HF_PREVIEW_PADDING 5
#define HF_PREVIEW_MARGIN 10

#define PRINTER_SETUP_KEY "printer-setup-dialog"

#define PAPER_PAGE 0
#define SCALE_PAGE 1
#define HF_PAGE 2

#define HF_TAG_NAME "field_tag"

typedef struct {
	/* The Canvas object */
	GtkWidget        *canvas;

	/* Objects in the Preview Canvas */
	GocItem  *group;

	/* Values for the scaling of the nice preview */
	int width, height;
	int offset_x, offset_y;	/* For centering the small page preview */
	double scale;
} MarginPreviewInfo;

typedef struct {
	/* The available width and height for the nice preview page */
	guint height, width;
} MarginPreviewPageAvailableSize;

typedef struct {
	/* The Canvas object for the header/footer sample preview */
	GtkWidget        *canvas;

	/* Objects in the Preview Canvas */
	GocItem		*left;
	GocItem		*middle;
	GocItem		*right;

} HFPreviewInfo;


typedef struct _PrinterSetupState PrinterSetupState;
typedef struct {
	double     value;
	GtkSpinButton *spin;
	GocItem *line;
	double bound_x1, bound_y1, bound_x2, bound_y2;
	MarginPreviewInfo *pi;
	PrinterSetupState *state;
} UnitInfo;

struct _PrinterSetupState {
	WBCGtk  *wbcg;
	Sheet            *sheet;
	GtkBuilder       *gui;
	GnmPrintInformation *pi;
	GtkWidget        *dialog;
	GtkWidget        *sheet_selector;

	GtkWidget        *scale_percent_radio;
	GtkWidget        *scale_fit_to_radio;
	GtkWidget        *scale_no_radio;

	GtkWidget        *portrait_radio;
	GtkWidget        *landscape_radio;
	GtkWidget        *rev_portrait_radio;
	GtkWidget        *rev_landscape_radio;

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

	/* The header and footer data. */
	GnmPrintHF *header;
	GnmPrintHF *footer;

	/* The header and footer customize dialogs. */
	GtkWidget *customize_header;
	GtkWidget *customize_footer;

	/* The header and footer preview widgets. */
	HFPreviewInfo *pi_header;
	HFPreviewInfo *pi_footer;

	/* Error Display */
	struct {
		GtkListStore    *store;
		GtkWidget	*combo;
	} error_display;

	/* Comment Display */
	struct {
		GtkListStore    *store;
		GtkWidget	*combo;
	} comment_display;
};

typedef struct _HFCustomizeState HFCustomizeState;

typedef struct _HFDTFormatState HFDTFormatState;
struct _HFDTFormatState {
	GtkWidget        *dialog;
	GtkBuilder       *gui;
	HFCustomizeState *hf_state;
	char             *format_string;
	GtkWidget        *format_sel;
};

struct _HFCustomizeState {
	GtkWidget        *dialog;
	GtkBuilder       *gui;
	PrinterSetupState *printer_setup_state;
	GnmPrintHF          **hf;
	gboolean         is_header;
	GtkTextBuffer    *left_buffer;
	GtkTextBuffer    *middle_buffer;
	GtkTextBuffer    *right_buffer;
	GList            *marks;
};

typedef enum {
	HF_FIELD_UNKNOWN,
	HF_FIELD_FILE,
	HF_FIELD_PATH,
	HF_FIELD_DATE,
	HF_FIELD_TIME,
	HF_FIELD_PAGE,
	HF_FIELD_PAGES,
	HF_FIELD_SHEET,
	HF_FIELD_CELL
} HFFieldType;

typedef struct _HFMarkInfo HFMarkInfo;
struct _HFMarkInfo {
	GtkTextMark *mark;
	HFFieldType type;
	char *options;
};

static void dialog_gtk_printer_setup_cb (PrinterSetupState *state);
static void fetch_settings (PrinterSetupState *state);
static void do_update_page (PrinterSetupState *state);
static void do_fetch_margins (PrinterSetupState *state);
static void do_hf_customize (gboolean header, PrinterSetupState *state);
static char *do_hf_dt_format_customize (gboolean date, HFCustomizeState *hf_state);


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
		g_object_unref (state->preview.group);
		state->preview.group = NULL;
	}
}

static void
move_line (GocItem *item,
	   double x1, double y1,
	   double x2, double y2)
{
	if (item != NULL)
		goc_item_set (item,
			     "x0", floor (x1) + .5,
			     "y0", floor (y1) + .5,
			     "x1", floor (x2) + .5,
			     "y1", floor (y2) + .5,
			     NULL);
}

static GocItem *
make_line (GocGroup *g, double x1, double y1, double x2, double y2)
{

	GocItem *item;
	GOStyle *style;

	item = goc_item_new (
		g, goc_line_get_type (),
		"x0", floor (x1) + .5,
		"y0", floor (y1) + .5,
		"x1", floor (x2) + .5,
		"y1", floor (y2) + .5,
		NULL);
	style = go_styled_object_get_style (GO_STYLED_OBJECT (item));
	style->line.width = 1.;
	style->line.color = MARGIN_COLOR_DEFAULT;

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
	GocGroup *g = GOC_GROUP (uinfo->state->preview.group);

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
margin_preview_page_available_size (PrinterSetupState *state,
				    MarginPreviewPageAvailableSize *available_size)
{
	GList *children, *l;
	GtkWidget *grid;
	unsigned *widths, *heights;
	GtkRequisition requisition;
	unsigned top, left, width, height, i;
	unsigned nrows, ncols, first_col, first_row;

	/* Reset available size to zero*/
	available_size->width = 0;
	available_size->height = 0;

	grid = go_gtk_builder_get_widget (state->gui, "paper-selector-grid");
	gtk_container_child_get (GTK_CONTAINER (grid),
	                         go_gtk_builder_get_widget (state->gui, "container-paper-sample"),
				 "top-attach", &first_row,
				 "left-attach", &first_col,
				 "width", &ncols,
				 "height", &nrows,
				 NULL);
	widths = g_new0 (guint, ncols);
	heights = g_new0 (guint, nrows);

	/* Iterate through all child widgets in the grid */
	children = gtk_container_get_children (GTK_CONTAINER (grid));
	for (l = children; l; l = l->next) {
		GtkWidget *child_widget = l->data;

		/* Determine which cells the align widget spans across */
		gtk_container_child_get (GTK_CONTAINER (grid), GTK_WIDGET (child_widget),
					 "top-attach", &top,
					 "left-attach", &left,
					 "width", &width,
					 "height", &height,
					 NULL);

		/* Determine the requisition size for the widget */
		gtk_widget_get_preferred_size (GTK_WIDGET(child_widget), &requisition, NULL);

		/* Find largest widget in each table column */
		/* Exclude widgets that expand across more than one grid cells
		 * or are not in a relevant column */
		if (left >= first_col && width == 1 && left < first_col + ncols) {
			if ((guint) requisition.width > widths[left - first_col]) {
				widths[left - first_col] = (guint) requisition.width;
			}
		}

		/* Find largest widget in each table row */
		/* Exclude widgets that expand across more than one grid cells
		 * or are not in a relevant column */
		if (top >= first_row && height == 1 && top < first_row + nrows) {
			if ((guint) requisition.height > heights[top - first_row]) {
				heights[top - first_row] = (guint) requisition.height;
			}
		}
	}
	g_list_free (children);

	/* Calculate width of container widget using maximum */
	/* widget widths from above */
	for (i = 0; i <  ncols; i++){
		available_size->width = available_size->width + widths[i];
	}

	/* Calculate height of container widget using maximum */
	/* widget heights from above */
	for (i = 0; i < nrows; i++){
		available_size->height = available_size->height + heights[i];
	}

	g_free(widths);
	g_free(heights);

	/* Account for the spacing between table cells */
	available_size->width = available_size->width +
		gtk_grid_get_column_spacing (GTK_GRID (grid)) * (ncols - 1);
	available_size->height = available_size->height +
		gtk_grid_get_row_spacing (GTK_GRID (grid)) * (nrows - 1);
}

static void
margin_preview_page_create (PrinterSetupState *state)
{
	double x1, y1, x2, y2;
	double width, height;
	MarginPreviewInfo *pi = &state->preview;
	GOStyle *style;
	MarginPreviewPageAvailableSize margin_available_size;

	margin_preview_page_available_size (state, &margin_available_size);
	gtk_widget_set_size_request (state->preview.canvas, margin_available_size.width, margin_available_size.height);

	width = state->width;
	height = state->height;
	x1 = (margin_available_size.width - PREVIEW_MARGIN_X) / width;
	x2 = (margin_available_size.height - PREVIEW_MARGIN_Y) / height;
	pi->scale = MIN (x1, x2);

	pi->offset_x = (margin_available_size.width - (width  * pi->scale)) / 2;
	pi->offset_y = (margin_available_size.height - (height * pi->scale)) / 2;
	x1 = pi->offset_x;
	y1 = pi->offset_y;
	x2 = pi->offset_x + width * pi->scale;
	y2 = pi->offset_y + height * pi->scale;

	pi->group = goc_item_new (
		goc_canvas_get_root (GOC_CANVAS (pi->canvas)),
		goc_group_get_type (),
		"x", 0.0,
		"y", 0.0,
		NULL);

	style = go_styled_object_get_style (
	        GO_STYLED_OBJECT (goc_item_new (GOC_GROUP (pi->group),
			GOC_TYPE_RECTANGLE,
			"x",		 (double) x1+2,
			"y",		 (double) y1+2,
			"width",	 (double) x2-x1,
			"height",	 (double) y2-y1,
			NULL)));
	style->fill.pattern.back = GO_COLOR_BLACK;
	style->line.color = GO_COLOR_BLACK;
	style->line.width = 1.;

	style = go_styled_object_get_style (
		GO_STYLED_OBJECT (goc_item_new (GOC_GROUP (pi->group),
			GOC_TYPE_RECTANGLE,
			"x",		 (double) x1,
			"y",		 (double) y1,
			"width",	 (double) x2-x1,
			"height",	 (double) y2-y1,
			NULL)));
	style->fill.pattern.back = GO_COLOR_WHITE;
	style->line.color = GO_COLOR_BLACK;
	style->line.width = 1.;

	goc_item_invalidate (pi->group);

	draw_margins (state, x1, y1, x2, y2);
}

static void
canvas_update (PrinterSetupState *state)
{
	margin_preview_page_destroy (state);
	margin_preview_page_create (state);
}


static gboolean
cb_spin_activated (UnitInfo *target)
{
	GOStyle *style = go_styled_object_get_style (GO_STYLED_OBJECT (target->line));
	style->fill.pattern.back = MARGIN_COLOR_ACTIVE,
	goc_item_invalidate (target->line);
	return FALSE;
}

static gboolean
cb_spin_deactivated (UnitInfo *target)
{
	GOStyle *style = go_styled_object_get_style (GO_STYLED_OBJECT (target->line));
	style->fill.pattern.back = MARGIN_COLOR_DEFAULT,
	goc_item_invalidate (target->line);
	return FALSE;
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
	target->spin = GTK_SPIN_BUTTON (go_gtk_builder_get_widget (state->gui, spin_name));
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
cb_unit_selector_changed (G_GNUC_UNUSED GtkComboBox *widget,
			  PrinterSetupState *state)
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
		GtkTreeIter *a, GtkTreeIter *b,
		G_GNUC_UNUSED gpointer user_data)
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


/*
 * Header and footer are stored with Excel semantics, but displayed with
 * more natural semantics. In Excel, both top margin and header are measured
 * from top of sheet. The Gnumeric user interface presents header as the
 * band between top margin and the print area. Bottom margin and footer are
 * handled likewise. See illustration at top of src/print.c
 */
static void
do_setup_margin (PrinterSetupState *state)
{
	GtkWidget *grid, *container;
	MarginPreviewPageAvailableSize margin_available_size;

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
	grid = go_gtk_builder_get_widget (state->gui, "paper-selector-grid");
	gtk_widget_set_hexpand (state->unit_selector, TRUE);
	gtk_grid_attach (GTK_GRID (grid), state->unit_selector, 3, 8, 1, 1);

	g_signal_connect (G_OBJECT (state->unit_selector), "changed",
			  G_CALLBACK (cb_unit_selector_changed), state);
	gtk_widget_show (state->unit_selector);

	margin_preview_page_available_size (state, &margin_available_size);

	g_return_if_fail (state && state->pi);

	state->preview.canvas = GTK_WIDGET (g_object_new (GOC_TYPE_CANVAS, NULL));
	gtk_widget_set_size_request (state->preview.canvas, margin_available_size.width, margin_available_size.height);
	gtk_widget_show (state->preview.canvas);

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

	state->check_center_h = go_gtk_builder_get_widget (state->gui,
						      "check_center_h");
	state->check_center_v = go_gtk_builder_get_widget (state->gui,
						      "check_center_v");
	gtk_toggle_button_set_active (
				      GTK_TOGGLE_BUTTON (state->check_center_v),
				      state->pi->center_vertically == 1);
	gtk_toggle_button_set_active (
				      GTK_TOGGLE_BUTTON (state->check_center_h),
				      state->pi->center_horizontally == 1);

	container = go_gtk_builder_get_widget (state->gui,
					       "container-paper-sample");
	gtk_container_add (GTK_CONTAINER (container), state->preview.canvas);

}

/* Display the header or footer preview in the print setup dialog.
 * Use the canvas widget in the HFPreviewInfo struct.
 *
 */
static void
display_hf_preview (PrinterSetupState *state, gboolean header)
{
	gchar *text = NULL;
	GnmPrintHF *sample = NULL;
	GnmPrintHFRenderInfo *hfi;
	HFPreviewInfo *pi;

	g_return_if_fail (state != NULL);

	hfi = gnm_print_hf_render_info_new ();

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

	text = gnm_print_hf_format_render (sample->left_format, hfi, HF_RENDER_PRINT);
	goc_item_set (pi->left, "text", text ? text : "", NULL);
	g_free (text);

	text = gnm_print_hf_format_render (sample->middle_format, hfi, HF_RENDER_PRINT);
	goc_item_set (pi->middle, "text", text ? text : "", NULL);
	g_free (text);

	text  = gnm_print_hf_format_render (sample->right_format, hfi, HF_RENDER_PRINT);
	goc_item_set (pi->right, "text", text ? text : "", NULL);
	g_free (text);

	gnm_print_hf_render_info_destroy (hfi);
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
	GList *selection = g_list_nth (gnm_print_hf_formats,
		gtk_combo_box_get_active (menu));
	GnmPrintHF *format = (selection)? selection->data: NULL;

	if (format == NULL) {
		do_header_customize (state);
	} else {
		gnm_print_hf_free (state->header);
		state->header = gnm_print_hf_copy (format);
	}

		display_hf_preview (state, TRUE);
}

static void
footer_changed (GtkComboBox *menu, PrinterSetupState *state)
{
	GList *selection = g_list_nth (gnm_print_hf_formats,
		gtk_combo_box_get_active (menu));
	GnmPrintHF *format = (selection)? selection->data: NULL;

	if (format == NULL) {
		do_footer_customize (state);
	} else {
		gnm_print_hf_free (state->footer);
		state->footer = gnm_print_hf_copy (format);
	}

	display_hf_preview (state, FALSE);
}

static char *
create_hf_name (char const *left, char const *middle, char const *right)
{
		char *this, *res;

		res = g_strdup_printf ("%s%s%s%s%s",
				       left," \xe2\x90\xa3 ", middle, " \xe2\x90\xa3 ", right);

		this = res;
		while (*this) {
			if (*this == '\n') {
				char *newstring;
				*this = 0;
				newstring =  g_strconcat (res, "\xe2\x90\xa4", this + 1, NULL);
				this = newstring + (this - res);
				g_free (res);
				res = newstring;
			} else
				this = g_utf8_find_next_char (this, NULL);
		}

		return res;
}

static void
append_hf_item (GtkListStore *store, GnmPrintHF *format, GnmPrintHFRenderInfo *hfi)
{
	GtkTreeIter iter;
	char *left, *middle, *right;
	char *res;

	left   = gnm_print_hf_format_render (format->left_format, hfi, HF_RENDER_PRINT);
	middle = gnm_print_hf_format_render (format->middle_format, hfi, HF_RENDER_PRINT);
	right  = gnm_print_hf_format_render (format->right_format, hfi, HF_RENDER_PRINT);

	res = create_hf_name (left, middle, right);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
			    0, res,
			    -1);

	g_free (res);
	g_free (left);
	g_free (middle);
	g_free (right);

}

/*
 * Fills one of the GtkCombos for headers or footers with the list
 * of existing header/footer formats
 */
static void
fill_hf (PrinterSetupState *state, GtkComboBox *om, GCallback callback, gboolean header)
{
	GList *l;
	GnmPrintHFRenderInfo *hfi;
	GtkListStore *store;
	GnmPrintHF *select = header ? state->header : state->footer;
	int i, idx = -1;

	hfi = gnm_print_hf_render_info_new ();
	hfi->page = 1;
	hfi->pages = 99;

	store = gtk_list_store_new (1, G_TYPE_STRING);
	gtk_combo_box_set_model (om, GTK_TREE_MODEL (store));
	g_object_unref (store);

	for (i = 0, l = gnm_print_hf_formats; l; l = l->next, i++) {
		GnmPrintHF *format = l->data;

		if (gnm_print_hf_same (format, select))
			idx = i;

		append_hf_item (store, format, hfi);
	}

	if (idx < 0)
		g_critical ("Current format is not registered!");

	gtk_combo_box_set_active (om, idx);
	g_signal_connect (G_OBJECT (om), "changed", callback, state);

	gnm_print_hf_render_info_destroy (hfi);
}

static void
do_setup_hf_menus (PrinterSetupState *state)
{
	GtkComboBox *header;
	GtkComboBox *footer;

	g_return_if_fail (state != NULL);

	header = GTK_COMBO_BOX (go_gtk_builder_get_widget (state->gui, "option-menu-header"));
	footer = GTK_COMBO_BOX (go_gtk_builder_get_widget (state->gui, "option-menu-footer"));

	if (state->header)
		gnm_print_hf_register (state->header);
	if (state->footer)
		gnm_print_hf_register (state->footer);

	if (state->header)
		fill_hf (state, header, G_CALLBACK (header_changed), TRUE);
	if (state->footer)
		fill_hf (state, footer, G_CALLBACK (footer_changed), FALSE);
}

/*************  Header Footer Customization *********** Start *************/

static void
hf_delete_tag_cb (HFCustomizeState *hf_state)
{
	GtkWidget* focus;

	focus = gtk_window_get_focus (GTK_WINDOW (hf_state->dialog));

	if (GTK_IS_TEXT_VIEW (focus)) {
		GtkTextBuffer *textbuffer;
		GtkTextTag    *tag;
		GtkTextIter   start;
		GtkTextIter   end;

		textbuffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (focus));
		tag = gtk_text_tag_table_lookup (gtk_text_buffer_get_tag_table (textbuffer),
						 HF_TAG_NAME);
		gtk_text_buffer_get_selection_bounds (textbuffer, &start, &end);

		if (gtk_text_iter_has_tag (&start, tag) && !gtk_text_iter_begins_tag (&start, tag))
			gtk_text_iter_backward_to_tag_toggle (&start, tag);
		if (gtk_text_iter_has_tag (&end, tag) && !gtk_text_iter_toggles_tag (&end, tag))
			gtk_text_iter_forward_to_tag_toggle (&end, tag);


		gtk_text_buffer_delete (textbuffer, &start, &end);
	}
}


static void
hf_insert_hf_stock_tag (HFCustomizeState *hf_state, GtkTextBuffer *buffer,
			HFFieldType type, const char *options)
{
	GtkTextIter iter;
	gchar const *icon_name;
	GdkPixbuf* pix;
	GtkTextMark *new_mark;
	HFMarkInfo *mark_info;

	switch (type) {
	case HF_FIELD_FILE:
		icon_name = GTK_STOCK_FILE;
		break;
	case HF_FIELD_PATH:
		icon_name = "folder";
		break;
	case HF_FIELD_PAGE:
		icon_name = "gnumeric-pagesetup-hf-page";
		break;
	case HF_FIELD_PAGES:
		icon_name = "gnumeric-pagesetup-hf-pages";
		break;
	case HF_FIELD_DATE:
		icon_name = "gnumeric-pagesetup-hf-date";
		break;
	case HF_FIELD_TIME:
		icon_name = "gnumeric-pagesetup-hf-time";
		break;
	case HF_FIELD_SHEET:
		icon_name = "gnumeric-pagesetup-hf-sheet";
		break;
	case HF_FIELD_CELL:
		icon_name = "gnumeric-pagesetup-hf-cell";
		break;
	default:
		return;
	}

	hf_delete_tag_cb (hf_state);

	if (gtk_text_buffer_insert_interactive_at_cursor
	    (buffer, "", -1, TRUE)) {
		GtkWidget *w = GTK_WIDGET (wbcg_toplevel (hf_state->printer_setup_state->wbcg));

		gtk_text_buffer_get_iter_at_mark
			(buffer, &iter, gtk_text_buffer_get_insert (buffer));

		pix = go_gtk_widget_render_icon_pixbuf (w, icon_name, GTK_ICON_SIZE_BUTTON);
		gtk_text_buffer_insert_pixbuf (buffer, &iter, pix);
		g_object_unref (pix);
		gtk_text_iter_backward_char (&iter);
		new_mark = gtk_text_buffer_create_mark (buffer, NULL,
							&iter, FALSE);
		g_object_ref (new_mark);

		mark_info = g_new0 (HFMarkInfo, 1);
		mark_info->mark = new_mark;
		mark_info->type = type;
		mark_info->options = g_strdup (options);
		hf_state->marks = g_list_append (hf_state->marks, mark_info);
	}
}

static void
hf_insert_hf_tag (HFCustomizeState *hf_state, HFFieldType type,
		  const char *options)
{
	GtkWidget* focus;

	focus = gtk_window_get_focus (GTK_WINDOW (hf_state->dialog));

	if (GTK_IS_TEXT_VIEW (focus)) {
		GtkTextBuffer *buffer =
			gtk_text_view_get_buffer (GTK_TEXT_VIEW (focus));
		hf_insert_hf_stock_tag (hf_state, buffer, type, options);
	}
}



static void
hf_insert_date_cb (GtkWidget *widget, HFCustomizeState *hf_state)
{

	hf_insert_hf_tag (hf_state, HF_FIELD_DATE,
			  g_object_get_data (G_OBJECT (widget), "options"));
}

static void
hf_insert_custom_date_cb (G_GNUC_UNUSED GtkWidget *widget,
			  HFCustomizeState *hf_state)
{
	char *format;

	format = do_hf_dt_format_customize (TRUE, hf_state);
	if (format != NULL) {
		hf_insert_hf_tag (hf_state, HF_FIELD_DATE, format);
		g_free (format);
	}
}

static void
hf_insert_time_cb (GtkWidget *widget, HFCustomizeState *hf_state)
{
	hf_insert_hf_tag (hf_state, HF_FIELD_TIME,
			  g_object_get_data (G_OBJECT (widget), "options"));
}

static void
hf_insert_custom_time_cb (G_GNUC_UNUSED GtkWidget *widget,
			  HFCustomizeState *hf_state)
{
	char *format;

	format = do_hf_dt_format_customize (FALSE, hf_state);
	if (format != NULL) {
		hf_insert_hf_tag (hf_state, HF_FIELD_TIME, format);
		g_free (format);
	}
}

static void
hf_insert_cell_cb (GtkWidget *widget, HFCustomizeState *hf_state)
{
	const char *options;
	options = g_object_get_data (G_OBJECT (widget), "options");
	if (options == NULL)
		options = "A1";
	hf_insert_hf_tag (hf_state, HF_FIELD_CELL, options);
}

static void
hf_insert_page_cb (HFCustomizeState *hf_state)
{
	hf_insert_hf_tag (hf_state, HF_FIELD_PAGE, NULL);
}

static void
hf_insert_pages_cb (HFCustomizeState *hf_state)
{
	hf_insert_hf_tag (hf_state, HF_FIELD_PAGES, NULL);
}

static void
hf_insert_sheet_cb (HFCustomizeState *hf_state)
{
	hf_insert_hf_tag (hf_state, HF_FIELD_SHEET, NULL);
}

static void
hf_insert_file_cb (HFCustomizeState *hf_state)
{
	hf_insert_hf_tag (hf_state, HF_FIELD_FILE, NULL);
}

static void
hf_insert_path_cb (HFCustomizeState *hf_state)
{
	hf_insert_hf_tag (hf_state, HF_FIELD_PATH, NULL);
}

static void
buffer_delete_range_cb (GtkTextBuffer *textbuffer,
			GtkTextIter   *start,
			GtkTextIter   *end,
			HFCustomizeState *hf_state)
{
	GtkTextTag    *tag;
	GtkTextIter   iter;
	GList *l = hf_state->marks;

	tag = gtk_text_tag_table_lookup (gtk_text_buffer_get_tag_table (textbuffer),
					 HF_TAG_NAME);
	gtk_text_iter_order (start, end);

	if (gtk_text_iter_has_tag (start, tag) && !gtk_text_iter_begins_tag (start, tag))
		gtk_text_iter_backward_to_tag_toggle (start, tag);
	if (gtk_text_iter_has_tag (end, tag) && !gtk_text_iter_toggles_tag (end, tag))
		gtk_text_iter_forward_to_tag_toggle (end, tag);

	/* Deleting all of our marks from this range */
	while (l) {
		HFMarkInfo *info = l->data;

		if (gtk_text_mark_get_buffer (info->mark) == textbuffer) {
			gtk_text_buffer_get_iter_at_mark (textbuffer, &iter, info->mark);
			if (gtk_text_iter_in_range (&iter, start, end))
				gtk_text_buffer_delete_mark (textbuffer, info->mark);
		}
		l = l->next;
	}
}

static int
mark_info_compare (HFMarkInfo *a, HFMarkInfo *b)
{
	GtkTextIter iter_a, iter_b;
	GtkTextBuffer *buffer;

	buffer = gtk_text_mark_get_buffer (a->mark);
	gtk_text_buffer_get_iter_at_mark (buffer, &iter_a, a->mark);
	gtk_text_buffer_get_iter_at_mark (buffer, &iter_b, b->mark);

	return gtk_text_iter_compare (&iter_a, &iter_b);
}

static void
append_tag_descriptor (GString* string, HFFieldType type, char const *options)
{
	char const *code;

	switch (type) {
		case HF_FIELD_FILE:
			code = "&[FILE";
			break;
		case HF_FIELD_PATH:
			code = "&[PATH";
			break;
		case HF_FIELD_DATE:
			code = "&[DATE";
			break;
		case HF_FIELD_TIME:
			code = "&[TIME";
			break;
		case HF_FIELD_PAGE:
			code = "&[PAGE";
			break;
		case HF_FIELD_PAGES:
			code = "&[PAGES";
			break;
		case HF_FIELD_SHEET:
			code = "&[TAB";
			break;
		case HF_FIELD_CELL:
			code = "&[CELL";
			break;
		default:
			return;
	}

	g_string_append (string, code);
	if (options) {
		g_string_append_c (string, ':');
		g_string_append (string, options);
	}
	g_string_append_c (string, ']');
}

static char *
text_get (HFCustomizeState *hf_state, GtkTextBuffer *buffer)
{
	GtkTextIter start;
	GtkTextIter end;
	GList *l, *sorted_marks = NULL;
	gchar *text;
	GString* string;

	string = g_string_new ("");

	l = hf_state->marks;
	while (l) {
		HFMarkInfo *m = l->data;
		if (gtk_text_mark_get_buffer (m->mark) == buffer)
			sorted_marks = g_list_insert_sorted
				(sorted_marks, m, (GCompareFunc) mark_info_compare);
		l = l->next;
	}

	gtk_text_buffer_get_bounds (buffer, &start, &end);

	l = sorted_marks;
	while (l) {
		HFMarkInfo *m = l->data;
		GtkTextIter mark_position;

		gtk_text_buffer_get_iter_at_mark (buffer, &mark_position, m->mark);
		text = gtk_text_buffer_get_text (buffer, &start, &mark_position, FALSE);
		g_string_append (string, text);
		g_free (text);
		append_tag_descriptor (string, m->type, m->options);
		start = mark_position;
		l = l->next;
	}
	g_list_free (sorted_marks);
	text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
	g_string_append (string, text);
	g_free (text);

	return g_string_free (string, FALSE);
}

static void
hf_customize_apply (HFCustomizeState *hf_state)
{
	char *left_format, *right_format, *middle_format;

	g_return_if_fail (hf_state != NULL);

	left_format   = text_get (hf_state, hf_state->left_buffer);
	middle_format = text_get (hf_state, hf_state->middle_buffer);
	right_format  = text_get (hf_state, hf_state->right_buffer);

	gnm_print_hf_free (*(hf_state->hf));
	*(hf_state->hf) = gnm_print_hf_new (left_format, middle_format, right_format);

	g_free (left_format);
	g_free (middle_format);
	g_free (right_format);

	gnm_print_hf_register (*(hf_state->hf));
	do_setup_hf_menus (hf_state->printer_setup_state);
	display_hf_preview (hf_state->printer_setup_state, hf_state->is_header);

	gtk_text_buffer_set_modified (hf_state->left_buffer, FALSE);
	gtk_text_buffer_set_modified (hf_state->middle_buffer, FALSE);
	gtk_text_buffer_set_modified (hf_state->right_buffer, FALSE);

	gtk_widget_set_sensitive (go_gtk_builder_get_widget (hf_state->gui, "apply_button"), FALSE);
	gtk_widget_set_sensitive (go_gtk_builder_get_widget (hf_state->gui, "ok_button"), FALSE);
}


static void
hf_customize_ok (HFCustomizeState *hf_state)
{
	hf_customize_apply (hf_state);
	gtk_widget_destroy (hf_state->dialog);
}

static gboolean
cb_hf_changed (GtkBuilder *gui)
{
	gtk_widget_set_sensitive (go_gtk_builder_get_widget (gui, "apply_button"), TRUE);
	gtk_widget_set_sensitive (go_gtk_builder_get_widget (gui, "ok_button"), TRUE);
	return FALSE;
}


static void
add_named_tags (GtkTextBuffer *buffer)
{
	GtkTextTag *tag;

	tag = gtk_text_tag_new (HF_TAG_NAME);
	g_object_set(tag,
		     "editable", FALSE,
		     "underline", TRUE,
		     "underline-set", TRUE,
		     "weight", PANGO_WEIGHT_BOLD,
		     "weight-set", TRUE,
		     "stretch", PANGO_STRETCH_CONDENSED,
		     "stretch-set", TRUE,
		     NULL);

	gtk_text_tag_table_add (gtk_text_buffer_get_tag_table (buffer), tag);
}

static gboolean
check_hf_tag (char const *unknown_tag, char const *known_tag, gchar **options, gint length)
{
	int len;
	char const *closing = unknown_tag + length;

	if (0 != g_ascii_strncasecmp (unknown_tag, "&[", 2))
		return FALSE;
	unknown_tag += 2;
	len = strlen (known_tag);
	if (0 != g_ascii_strncasecmp (unknown_tag, known_tag, len))
		return FALSE;
	unknown_tag += len;
	if (*unknown_tag == ']')
		return TRUE;
	if (*unknown_tag != ':')
		return FALSE;
	unknown_tag++;
	len = closing - unknown_tag - 1;
	if ((len > 0) && (options != NULL)) {
		*options = g_strndup (unknown_tag, len);
	}
	return TRUE;
}

static gboolean
is_known_tag (HFCustomizeState* hf_state, GtkTextBuffer *buffer, char const *tag, gint length)
{
	gchar *options = NULL;

	if (check_hf_tag (tag, "FILE", &options, length))
		hf_insert_hf_stock_tag (hf_state, buffer, HF_FIELD_FILE, options);
	else if (check_hf_tag (tag, "PATH", &options, length))
		hf_insert_hf_stock_tag (hf_state, buffer, HF_FIELD_PATH, options);
	else if (check_hf_tag (tag, "PAGES", &options, length))
		hf_insert_hf_stock_tag (hf_state, buffer, HF_FIELD_PAGES, options);
	else if (check_hf_tag (tag, "PAGE", &options, length))
		hf_insert_hf_stock_tag (hf_state, buffer, HF_FIELD_PAGE, options);
	else if (check_hf_tag (tag, "TAB", &options, length))
		hf_insert_hf_stock_tag (hf_state, buffer, HF_FIELD_SHEET, options);
	else if (check_hf_tag (tag, "DATE", &options, length))
		hf_insert_hf_stock_tag (hf_state, buffer, HF_FIELD_DATE, options);
	else if (check_hf_tag (tag, "TIME", &options, length))
		hf_insert_hf_stock_tag (hf_state, buffer, HF_FIELD_TIME, options);
	else if (check_hf_tag (tag, "CELL", &options, length))
		hf_insert_hf_stock_tag (hf_state, buffer, HF_FIELD_CELL, options);
	else
		return FALSE;

	g_free (options);

	return TRUE;
}

static void
add_text_to_buffer (HFCustomizeState* hf_state, GtkTextBuffer *buffer, char const *text)
{
	gchar const *here = text, *end;
	gunichar closing = g_utf8_get_char ("]");
	gunichar ambersand = g_utf8_get_char ("&");
	GtkTextIter iter;

	g_return_if_fail (here != NULL);

	while (*here) {
		if (here[0] == '&' && here[1] == '[') {
			end = g_utf8_strchr (here, -1, closing);
			if (end == NULL) {
				gtk_text_buffer_insert (buffer, &iter, here, -1);
				break;
			} else {
				if (!is_known_tag ( hf_state, buffer, here, end - here + 1)) {
					gtk_text_buffer_get_end_iter (buffer, &iter);
					gtk_text_buffer_insert_with_tags_by_name
						(buffer, &iter, here, end - here + 1,
						 HF_TAG_NAME, NULL);
				}
				here = end + 1;
			}
		} else {
			end = g_utf8_strchr (g_utf8_find_next_char (here, NULL),
					     -1, ambersand);
			gtk_text_buffer_get_end_iter (buffer, &iter);
			if (end == NULL) {
				gtk_text_buffer_insert (buffer, &iter, here, -1);
				break;
			} else {
				gtk_text_buffer_insert (buffer, &iter, here, end - here);
				here = end;
			}
		}
	}

	gtk_text_buffer_set_modified (buffer, FALSE);
}

static void
free_hf_mark_info (HFMarkInfo *info)
{
	if (info->mark)
		g_object_unref (info->mark);
	g_free (info->options);
	g_free (info);
}

static void
free_hf_state (HFCustomizeState *hf_state)
{
	g_return_if_fail (hf_state != NULL);

	g_list_free_full (hf_state->marks, (GDestroyNotify) free_hf_mark_info);
	g_free (hf_state);
}

static void
hf_attach_insert_date_menu (GtkMenuToolButton *button, HFCustomizeState* hf_state)
{
	GtkWidget *menu = NULL;
	GtkWidget *item = NULL;

	g_signal_connect
		(G_OBJECT (button),
		 "clicked", G_CALLBACK (hf_insert_date_cb), hf_state);

	menu = gtk_menu_new ();

	item = gtk_menu_item_new_with_label (_("Default date format"));
	g_signal_connect
		(G_OBJECT (item),
		 "activate", G_CALLBACK (hf_insert_date_cb), hf_state);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	item = gtk_menu_item_new_with_label (_("Custom date format"));
	g_signal_connect
		(G_OBJECT (item),
		 "activate", G_CALLBACK (hf_insert_custom_date_cb), hf_state);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	item = gtk_menu_item_new_with_label (("YYYY/MM/DD"));
	g_signal_connect
		(G_OBJECT (item),
		 "activate", G_CALLBACK (hf_insert_date_cb), hf_state);
	g_object_set_data_full (G_OBJECT (item), "options", g_strdup("YYYY/MM/DD"), g_free);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	gtk_menu_tool_button_set_menu (button, menu);
	gtk_widget_show_all (menu);
}

static void
hf_attach_insert_time_menu (GtkMenuToolButton *button, HFCustomizeState* hf_state)
{
	GtkWidget *menu = NULL;
	GtkWidget *item = NULL;

	g_signal_connect
		(G_OBJECT (button),
		 "clicked", G_CALLBACK (hf_insert_time_cb), hf_state);

	menu = gtk_menu_new ();

	item = gtk_menu_item_new_with_label (_("Default time format"));
	g_signal_connect
		(G_OBJECT (item),
		 "activate", G_CALLBACK (hf_insert_time_cb), hf_state);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	item = gtk_menu_item_new_with_label (_("Custom time format"));
	g_signal_connect
		(G_OBJECT (item),
		 "activate", G_CALLBACK (hf_insert_custom_time_cb), hf_state);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	item = gtk_menu_item_new_with_label (("HH:MM:SS"));
	g_signal_connect
		(G_OBJECT (item),
		 "activate", G_CALLBACK (hf_insert_time_cb), hf_state);
	g_object_set_data_full (G_OBJECT (item), "options", g_strdup("HH:MM:SS"), g_free);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	gtk_menu_tool_button_set_menu (button, menu);
	gtk_widget_show_all (menu);
}

static void
hf_attach_insert_cell_menu (GtkMenuToolButton *button, HFCustomizeState* hf_state)
{
	GtkWidget *menu = NULL;
	GtkWidget *item = NULL;

	g_signal_connect
		(G_OBJECT (button),
		 "clicked", G_CALLBACK (hf_insert_cell_cb), hf_state);

	menu = gtk_menu_new ();

	item = gtk_menu_item_new_with_label (_("A1 (first cell of the page area)"));
	g_signal_connect
		(G_OBJECT (item),
		 "activate", G_CALLBACK (hf_insert_cell_cb), hf_state);
	g_object_set_data_full (G_OBJECT (item), "options", g_strdup("A1"), g_free);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	item = gtk_menu_item_new_with_label (_("$A$1 (first cell of this worksheet)"));
	g_signal_connect
		(G_OBJECT (item),
		 "activate", G_CALLBACK (hf_insert_cell_cb), hf_state);
	g_object_set_data_full (G_OBJECT (item), "options", g_strdup("$A$1"), g_free);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	item = gtk_menu_item_new_with_label (_("First Printed Cell Of The Page"));
	g_signal_connect
		(G_OBJECT (item),
		 "activate", G_CALLBACK (hf_insert_cell_cb), hf_state);
	g_object_set_data_full (G_OBJECT (item), "options", g_strdup("rep|A1"), g_free);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	gtk_menu_tool_button_set_menu (button, menu);
	gtk_widget_show_all (menu);
}

static void
cb_hf_destroyed (GObject *obj) {
	g_object_set_data (obj, "gui", NULL);
}

/*
 * Open up a DIALOG to allow the user to customize the header
 * or the footer.
 */
static void
do_hf_customize (gboolean header, PrinterSetupState *state)
{
	GtkBuilder *gui;
	GtkTextView *left, *middle, *right;
	GtkTextBuffer *left_buffer, *middle_buffer, *right_buffer;

	GtkWidget *dialog;
	HFCustomizeState* hf_state;
	GtkToolButton *button;

	/* Check if this dialog isn't already created. */
	if (header)
		dialog = state->customize_header;
	else
		dialog = state->customize_footer;

	if (dialog != NULL) {
		gdk_window_show (gtk_widget_get_window (dialog));
		gdk_window_raise (gtk_widget_get_window (dialog));
		return;
	}

	gui = gnm_gtk_builder_load ("res:ui/hf-config.ui", NULL, GO_CMD_CONTEXT (state->wbcg));
        if (gui == NULL)
                return;

	hf_state = g_new0 (HFCustomizeState, 1);
	hf_state->gui = gui;
	hf_state->printer_setup_state = state;
	hf_state->is_header = header;

	left   = GTK_TEXT_VIEW (go_gtk_builder_get_widget (gui, "left-format"));
	middle = GTK_TEXT_VIEW (go_gtk_builder_get_widget (gui, "middle-format"));
	right  = GTK_TEXT_VIEW (go_gtk_builder_get_widget (gui, "right-format"));

	dialog = go_gtk_builder_get_widget (gui, "hf-config");
	hf_state->dialog = dialog;

	if (header) {
		hf_state->hf = &state->header;
		state->customize_header = dialog;
		gtk_window_set_title (GTK_WINDOW (dialog), _("Custom header configuration"));

	} else {
		hf_state->hf = &state->footer;
		state->customize_footer = dialog;
		gtk_window_set_title (GTK_WINDOW (dialog), _("Custom footer configuration"));
	}

	hf_state->left_buffer = left_buffer = gtk_text_view_get_buffer (left);
	hf_state->middle_buffer = middle_buffer = gtk_text_view_get_buffer (middle);
	hf_state->right_buffer = right_buffer = gtk_text_view_get_buffer (right);

	add_named_tags (left_buffer);
	add_named_tags (middle_buffer);
	add_named_tags (right_buffer);

	add_text_to_buffer (hf_state, left_buffer, (*(hf_state->hf))->left_format);
	add_text_to_buffer (hf_state, middle_buffer, (*(hf_state->hf))->middle_format);
	add_text_to_buffer (hf_state, right_buffer, (*(hf_state->hf))->right_format);

	g_signal_connect (G_OBJECT (left_buffer), "delete-range",
			  G_CALLBACK (buffer_delete_range_cb), hf_state);
	g_signal_connect (G_OBJECT (middle_buffer), "delete-range",
			  G_CALLBACK (buffer_delete_range_cb), hf_state);
	g_signal_connect (G_OBJECT (right_buffer), "delete-range",
			  G_CALLBACK (buffer_delete_range_cb), hf_state);

	g_signal_connect_swapped (G_OBJECT (go_gtk_builder_get_widget (gui, "apply_button")),
		"clicked", G_CALLBACK (hf_customize_apply), hf_state);
	g_signal_connect_swapped (G_OBJECT (go_gtk_builder_get_widget (gui, "ok_button")),
		"clicked", G_CALLBACK (hf_customize_ok), hf_state);
	g_signal_connect_swapped
		(G_OBJECT (go_gtk_builder_get_widget (gui, "cancel_button")),
		 "clicked", G_CALLBACK (gtk_widget_destroy), dialog);
	gtk_widget_set_sensitive (go_gtk_builder_get_widget (gui, "apply_button"), FALSE);
	gtk_widget_set_sensitive (go_gtk_builder_get_widget (gui, "ok_button"), FALSE);

	if (header)
		g_signal_connect (G_OBJECT (dialog), "destroy",
				  G_CALLBACK (gtk_widget_destroyed),
				  &state->customize_header);
	else
		g_signal_connect (G_OBJECT (dialog), "destroy",
				  G_CALLBACK (gtk_widget_destroyed),
				  &state->customize_footer);


	g_object_set_data_full (G_OBJECT (dialog),
		"hfstate", hf_state, (GDestroyNotify) free_hf_state);

	/* Setup bindings to mark when the entries are modified. */
	g_signal_connect_swapped (G_OBJECT (left_buffer),
		"modified-changed", G_CALLBACK (cb_hf_changed), gui);
	g_signal_connect_swapped (G_OBJECT (middle_buffer),
		"modified-changed", G_CALLBACK (cb_hf_changed), gui);
	g_signal_connect_swapped (G_OBJECT (right_buffer),
		"modified-changed", G_CALLBACK (cb_hf_changed), gui);

	gnm_init_help_button (go_gtk_builder_get_widget (gui, "help_button"),
		header  ? GNUMERIC_HELP_LINK_PRINTER_SETUP_HEADER_CUSTOMIZATION
			: GNUMERIC_HELP_LINK_PRINTER_SETUP_FOOTER_CUSTOMIZATION);

	g_signal_connect_swapped
		(G_OBJECT (go_gtk_builder_get_widget (gui, "delete-button")),
		 "clicked", G_CALLBACK (hf_delete_tag_cb), hf_state);

	button = GTK_TOOL_BUTTON (go_gtk_builder_get_widget (gui, "insert-date-button"));
	gtk_tool_button_set_icon_name (button, "gnumeric-pagesetup-hf-date");
	hf_attach_insert_date_menu (GTK_MENU_TOOL_BUTTON (button), hf_state);

	button = GTK_TOOL_BUTTON (go_gtk_builder_get_widget (gui, "insert-page-button"));
	gtk_tool_button_set_icon_name (button, "gnumeric-pagesetup-hf-page");
	g_signal_connect_swapped
		(G_OBJECT (button),
		 "clicked", G_CALLBACK (hf_insert_page_cb), hf_state);

	button = GTK_TOOL_BUTTON (go_gtk_builder_get_widget (gui, "insert-pages-button"));
	gtk_tool_button_set_icon_name (button, "gnumeric-pagesetup-hf-pages");
	g_signal_connect_swapped
		(G_OBJECT (button),
		 "clicked", G_CALLBACK (hf_insert_pages_cb), hf_state);

	button = GTK_TOOL_BUTTON (go_gtk_builder_get_widget (gui, "insert-sheet-button"));
	gtk_tool_button_set_icon_name (button, "gnumeric-pagesetup-hf-sheet");
	g_signal_connect_swapped
		(G_OBJECT (button),
		 "clicked", G_CALLBACK (hf_insert_sheet_cb), hf_state);

	button = GTK_TOOL_BUTTON (go_gtk_builder_get_widget (gui, "insert-time-button"));
	gtk_tool_button_set_icon_name (button, "gnumeric-pagesetup-hf-time");
	hf_attach_insert_time_menu (GTK_MENU_TOOL_BUTTON (button), hf_state);

	g_signal_connect_swapped
		(G_OBJECT (go_gtk_builder_get_widget (gui, "insert-file-button")),
		 "clicked", G_CALLBACK (hf_insert_file_cb), hf_state);

	g_signal_connect_swapped
		(G_OBJECT (go_gtk_builder_get_widget (gui, "insert-path-button")),
		 "clicked", G_CALLBACK (hf_insert_path_cb), hf_state);

	button = GTK_TOOL_BUTTON (go_gtk_builder_get_widget (gui, "insert-cell-button"));
	gtk_tool_button_set_icon_name (button, "gnumeric-pagesetup-hf-cell");
	hf_attach_insert_cell_menu (GTK_MENU_TOOL_BUTTON (button), hf_state);


	/* Let them begin typing into the first entry widget. */
	gtk_widget_grab_focus (GTK_WIDGET (left));

	gtk_window_set_transient_for (GTK_WINDOW (dialog),
				      GTK_WINDOW (state->dialog));

	/* The following would cause the dialog to be below the page setup dialog: */
/*	go_gtk_window_set_transient (GTK_WINDOW (dialog), GTK_WINDOW (state->dialog)); */

	g_object_set_data_full (G_OBJECT (dialog), "gui", gui, g_object_unref);
	g_signal_connect (G_OBJECT (dialog), "destroy", G_CALLBACK (cb_hf_destroyed), NULL);

	gtk_widget_show_all (dialog);
}

/*************  Header Footer Customization *********** End *************/

/*************  Date/Time Format Customization ******** Start ***********/

static void
hf_dt_customize_ok (HFDTFormatState *hf_dt_state)
{
	GOFormat const *format =
		go_format_sel_get_fmt (GO_FORMAT_SEL (hf_dt_state->format_sel));
	hf_dt_state->format_string = g_strdup (go_format_as_XL (format));
}

static char *
do_hf_dt_format_customize (gboolean date, HFCustomizeState *hf_state)
{
	GtkBuilder *gui;

	GtkWidget *dialog, *format_sel, *grid;
	HFDTFormatState* hf_dt_state;
	gint result;
	char *result_string = NULL;

	gui = gnm_gtk_builder_load ("res:ui/hf-dt-format.ui", NULL,
	                         GO_CMD_CONTEXT (hf_state->printer_setup_state->wbcg));
        if (gui == NULL)
                return NULL;

	hf_dt_state = g_new0 (HFDTFormatState, 1);
	hf_dt_state->gui = gui;
	hf_dt_state->hf_state = hf_state;
	hf_dt_state->format_string = NULL;

	dialog = go_gtk_builder_get_widget (gui, "hf-dt-format");
	hf_dt_state->dialog = dialog;

	if (date) {
		gtk_window_set_title (GTK_WINDOW (dialog), _("Date format selection"));
	} else {
		gtk_window_set_title (GTK_WINDOW (dialog), _("Time format selection"));
	}

	g_signal_connect_swapped (G_OBJECT (go_gtk_builder_get_widget (gui, "ok_button")),
		"clicked", G_CALLBACK (hf_dt_customize_ok), hf_dt_state);

	g_object_set_data_full (G_OBJECT (dialog),
		"hfdtstate", hf_dt_state, (GDestroyNotify) g_free);

	gnm_init_help_button (go_gtk_builder_get_widget (gui, "help_button"),
		GNUMERIC_HELP_LINK_PRINTER_SETUP_GENERAL);

	grid = go_gtk_builder_get_widget (gui, "layout-grid");
	if (grid == NULL) { /* how can this happen? */
		gtk_widget_destroy (dialog);
		return NULL;
	}
	hf_dt_state->format_sel = format_sel = go_format_sel_new_full (TRUE);
	go_format_sel_set_style_format
		(GO_FORMAT_SEL (format_sel),
		 date ? go_format_default_date () : go_format_default_time ());

	gtk_widget_show_all (dialog);
	gtk_grid_attach (GTK_GRID (grid), format_sel, 0, 1, 2, 1);
	gtk_widget_show (format_sel);

	result = gtk_dialog_run (GTK_DIALOG (dialog));
	switch (result)
	{
	case GTK_RESPONSE_OK:
		result_string = hf_dt_state->format_string;
		break;
	default:
		gtk_widget_destroy (dialog);
		return NULL;
	}
	gtk_widget_destroy (dialog);
	g_object_unref (hf_dt_state->gui);
	hf_dt_state->gui = NULL;
	return result_string;
}



/*************  Date/Time Format Customization ******** End *************/

/* header/footer_preview_event
 * If the user double clicks on a header/footer preview canvas, we will
 * open up the dialog to modify the header or footer.
 * They can also do this from the option menu.
 */
static gboolean
header_preview_event (G_GNUC_UNUSED GocCanvas *canvas,
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
footer_preview_event (G_GNUC_UNUSED GocCanvas *canvas,
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
	GnmStyle *style;
	gdouble width = HF_PREVIEW_X;
	gdouble height = HF_PREVIEW_Y;
	gdouble shadow = HF_PREVIEW_SHADOW;
	gdouble padding = HF_PREVIEW_PADDING;
	gdouble margin = HF_PREVIEW_MARGIN;
	gdouble bottom_margin = height - margin;
	GOStyle *gostyle;

	pi = g_new (HFPreviewInfo, 1);

	if (header)
		state->pi_header = pi;
	else
		state->pi_footer = pi;

	pi->canvas = GTK_WIDGET (g_object_new (GOC_TYPE_CANVAS, NULL));

 	gostyle = go_styled_object_get_style (
		GO_STYLED_OBJECT (goc_item_new (goc_canvas_get_root (GOC_CANVAS (pi->canvas)),
			GOC_TYPE_RECTANGLE,
			"x",		1. + shadow,
			"y",		(header ? shadow : 0.),
			"width",	width,
			"height",	height + (header ? -shadow: shadow),
			NULL)));
	gostyle->fill.pattern.back = GO_COLOR_BLACK;
	gostyle->line.width = 0.;
	gostyle->line.color = 0;

	gostyle = go_styled_object_get_style (
		GO_STYLED_OBJECT (goc_item_new (goc_canvas_get_root (GOC_CANVAS (pi->canvas)),
			GOC_TYPE_RECTANGLE,
		        "x",		1.0,
			"y",		(header? 1.0: 0.),
			"width",	width,
			"height",	height,
			NULL)));
	gostyle->fill.pattern.back = GO_COLOR_WHITE;
	gostyle->line.width = 0.;
	gostyle->line.color = 0;

	style = gnm_conf_get_printer_decoration_font ();
	font_desc = pango_font_description_new ();
	pango_font_description_set_family (font_desc, gnm_style_get_font_name (style));
	pango_font_description_set_style
		(font_desc, gnm_style_get_font_italic (style) ?
		 PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
	pango_font_description_set_variant (font_desc, PANGO_VARIANT_NORMAL);
	pango_font_description_set_weight (font_desc, PANGO_WEIGHT_NORMAL);
	pango_font_description_set_size (font_desc, 8 * PANGO_SCALE);
	gnm_style_unref (style);

	pi->left = goc_item_new (
		goc_canvas_get_root (GOC_CANVAS (pi->canvas)),
		goc_text_get_type (),
		"x",		padding,
		"y",		header ? margin : bottom_margin,
		"anchor",	header ? GO_ANCHOR_NORTH_WEST : GO_ANCHOR_SOUTH_WEST,
		"text",		"Left",
		NULL);
	gostyle = go_styled_object_get_style (GO_STYLED_OBJECT (pi->left));
	go_style_set_font_desc (gostyle, pango_font_description_copy (font_desc));

	pi->middle = goc_item_new (
		goc_canvas_get_root (GOC_CANVAS (pi->canvas)),
		goc_text_get_type (),
		"x",		width / 2,
		"y",		header ? margin : bottom_margin,
		"anchor",	header ? GO_ANCHOR_NORTH : GO_ANCHOR_SOUTH,
		"text",		"Center",
		NULL);
	gostyle = go_styled_object_get_style (GO_STYLED_OBJECT (pi->left));
	go_style_set_font_desc (gostyle, pango_font_description_copy (font_desc));

	pi->right =  goc_item_new (
		goc_canvas_get_root (GOC_CANVAS (pi->canvas)),
		goc_text_get_type (),
		"x",		width - padding,
		"y",		header ? margin : bottom_margin,
		"anchor",	header ? GO_ANCHOR_NORTH_EAST : GO_ANCHOR_SOUTH_EAST,
		"text",		"Right",
		NULL);
	gostyle = go_styled_object_get_style (GO_STYLED_OBJECT (pi->left));
	go_style_set_font_desc (gostyle, pango_font_description_copy (font_desc));

	pango_font_description_free (font_desc);

	gtk_widget_show_all (pi->canvas);

	if (header) {
		g_signal_connect (G_OBJECT (pi->canvas),
			"event",
			G_CALLBACK (header_preview_event), state);
		wid = go_gtk_builder_get_widget (state->gui, "container-header-sample");
	} else {
		g_signal_connect (G_OBJECT (pi->canvas),
			"event",
			G_CALLBACK (footer_preview_event), state);
		wid = go_gtk_builder_get_widget (state->gui, "container-footer-sample");
	}
	gtk_widget_set_size_request (pi->canvas, width + shadow + 1, height + (header ? 1: shadow));

	gtk_container_add (GTK_CONTAINER (wid), GTK_WIDGET (pi->canvas));
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
	GtkWidget *w;

	g_return_if_fail (state != NULL);

	header = GTK_COMBO_BOX (go_gtk_builder_get_widget (state->gui, "option-menu-header"));
	renderer = (GtkCellRenderer*) gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (header), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (header), renderer,
                                        "text", 0,
                                        NULL);
	footer = GTK_COMBO_BOX (go_gtk_builder_get_widget (state->gui, "option-menu-footer"));
	renderer = (GtkCellRenderer*) gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (footer), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (footer), renderer,
                                        "text", 0,
                                        NULL);

	state->header = gnm_print_hf_copy (state->pi->header ? state->pi->header :
				     gnm_print_hf_formats->data);
	state->footer = gnm_print_hf_copy (state->pi->footer ? state->pi->footer :
				     gnm_print_hf_formats->data);

	do_setup_hf_menus (state);

	w = go_gtk_builder_get_widget (state->gui, "configure-header-button");
	g_signal_connect_swapped (G_OBJECT (w),
		"clicked",
		G_CALLBACK (do_header_customize), state);
	w = go_gtk_builder_get_widget (state->gui, "configure-footer-button");
	g_signal_connect_swapped (G_OBJECT (w),
		"clicked",
		G_CALLBACK (do_footer_customize), state);


	create_hf_preview_canvas (state, TRUE);
	create_hf_preview_canvas (state, FALSE);

	display_hf_preview (state, TRUE);
	display_hf_preview (state, FALSE);

}

static void
display_order_icon (GtkToggleButton *toggle, PrinterSetupState *state)
{
	GtkWidget *show, *hide;

	if (gtk_toggle_button_get_active (toggle)){
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
	GnmRange *print_area;

	print_area = sheet_get_nominal_printarea
		(wb_control_cur_sheet
		 (GNM_WBC (state->wbcg)));
	if (print_area != NULL)
		gnm_expr_entry_load_from_range
			(state->area_entry,
			 wb_control_cur_sheet (GNM_WBC (state->wbcg)),
			 print_area);
	else
		gnm_expr_entry_load_from_text
			(state->area_entry, "");
	g_free (print_area);
}

static void
do_setup_error_display (PrinterSetupState *state)
{
	static struct {
		char const *label;
		guint  type;
	} display_types[] = {
		{N_("Print as displayed"), GNM_PRINT_ERRORS_AS_DISPLAYED},
		{N_("Print as spaces"),    GNM_PRINT_ERRORS_AS_BLANK},
		{N_("Print as dashes"),    GNM_PRINT_ERRORS_AS_DASHES},
		{N_("Print as #N/A"),      GNM_PRINT_ERRORS_AS_NA}
	};

	gint i;
	GtkCellRenderer  *cell;
	gint item = GNM_PRINT_ERRORS_AS_DISPLAYED;
	GtkTreeIter iter;

	for (i = 0; i < (int)G_N_ELEMENTS (display_types); i++) {
		gtk_list_store_insert_with_values (state->error_display.store,
						   NULL, G_MAXINT,
                                                   0, _(display_types[i].label),
						   1, display_types[i].type,
						   -1);
		if (display_types[i].type == state->pi->error_display)
			item = i;
	}
	cell = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(state->error_display.combo), cell, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(state->error_display.combo),
				       cell, "text", 0, NULL);
	if (gtk_tree_model_iter_nth_child
	    (GTK_TREE_MODEL (state->error_display.store), &iter, NULL, item))
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (state->error_display.combo), &iter);
}

static void
do_setup_comment_display (PrinterSetupState *state)
{
	static struct {
		char const *label;
		guint  type;
	} display_types[] = {
		{N_("Do not print"),   GNM_PRINT_COMMENTS_NONE},
		{N_("Print in place"), GNM_PRINT_COMMENTS_IN_PLACE},
		{N_("Print at end"),   GNM_PRINT_COMMENTS_AT_END}
	};

	gint i;
	GtkCellRenderer  *cell;
	gint item = GNM_PRINT_COMMENTS_NONE;
	GtkTreeIter iter;

	for (i = 0; i < (int)G_N_ELEMENTS (display_types); i++) {
		gtk_list_store_insert_with_values (state->comment_display.store,
						   NULL, G_MAXINT,
                                                   0, _(display_types[i].label),
						   1, display_types[i].type,
						   -1);
		if (display_types[i].type == state->pi->comment_placement)
			item = i;
	}
	cell = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(state->comment_display.combo), cell, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(state->comment_display.combo), cell, "text", 0, NULL);
	if (gtk_tree_model_iter_nth_child
	    (GTK_TREE_MODEL (state->comment_display.store), &iter, NULL, item))
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (state->comment_display.combo), &iter);

	gtk_widget_set_sensitive (state->comment_display.combo, FALSE);
}

static void
do_setup_page_area (PrinterSetupState *state)
{
	GtkWidget *pa_grid   = go_gtk_builder_get_widget (state->gui,
						     "print-area-grid");
	GtkWidget *repeat_grid = go_gtk_builder_get_widget (state->gui,
							"area-grid");

	state->area_entry = gnm_expr_entry_new (state->wbcg, FALSE);
	gnm_expr_entry_set_flags (state->area_entry,
		GNM_EE_SHEET_OPTIONAL,
		GNM_EE_SHEET_OPTIONAL);
	gtk_container_add (GTK_CONTAINER (pa_grid),
	                   GTK_WIDGET (state->area_entry));
	gtk_widget_set_hexpand (GTK_WIDGET (state->area_entry), TRUE);
	gtk_widget_show (GTK_WIDGET (state->area_entry));

	state->top_entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->top_entry,
		GNM_EE_SINGLE_RANGE | GNM_EE_FULL_ROW | GNM_EE_SHEET_OPTIONAL,
		GNM_EE_MASK);
	gtk_widget_set_hexpand (GTK_WIDGET (state->top_entry), TRUE);
	gtk_grid_attach (GTK_GRID (repeat_grid),
			 GTK_WIDGET (state->top_entry), 1, 2, 1, 1);
	gtk_widget_show (GTK_WIDGET (state->top_entry));

	state->left_entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->left_entry,
		GNM_EE_SINGLE_RANGE | GNM_EE_FULL_COL | GNM_EE_SHEET_OPTIONAL,
		GNM_EE_MASK);
	gtk_widget_set_hexpand (GTK_WIDGET (state->left_entry), TRUE);
	gtk_grid_attach (GTK_GRID (repeat_grid),
			 GTK_WIDGET (state->left_entry), 1, 3, 1, 1);
	gtk_widget_show (GTK_WIDGET (state->left_entry));


	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (gnm_expr_entry_get_entry (state->area_entry)));
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (gnm_expr_entry_get_entry (state->top_entry)));
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (gnm_expr_entry_get_entry (state->left_entry)));

	gnm_expr_entry_load_from_text (state->top_entry,
				       state->pi->repeat_top);
	gnm_expr_entry_load_from_text (state->left_entry,
				       state->pi->repeat_left);

	load_print_area (state);
}

static void
do_setup_page_info (PrinterSetupState *state)
{
	GtkWidget *gridlines = go_gtk_builder_get_widget (state->gui, "check-grid-lines");
	GtkWidget *onlystyles= go_gtk_builder_get_widget (state->gui, "check-only-styles");
	GtkWidget *bw        = go_gtk_builder_get_widget (state->gui, "check-black-white");
	GtkWidget *titles    = go_gtk_builder_get_widget (state->gui, "check-print-titles");
	GtkWidget *do_not_print = go_gtk_builder_get_widget (state->gui, "check-do-not-print");
	GtkWidget *order_rd  = go_gtk_builder_get_widget (state->gui, "radio-order-right");
	GtkWidget *order_dr  = go_gtk_builder_get_widget (state->gui, "radio-order-down");
	GtkWidget *order_grid = go_gtk_builder_get_widget (state->gui, "page-order-grid");
	GtkWidget *order;

	state->error_display.combo = go_gtk_builder_get_widget (state->gui, "error-box");
	state->error_display.store = GTK_LIST_STORE (gtk_combo_box_get_model
							 (GTK_COMBO_BOX (state->error_display.combo)));
	do_setup_error_display (state);

	state->comment_display.combo = go_gtk_builder_get_widget (state->gui, "comments-box");
	state->comment_display.store = GTK_LIST_STORE (gtk_combo_box_get_model
							 (GTK_COMBO_BOX (state->comment_display.combo)));
	do_setup_comment_display (state);

	state->icon_rd = gtk_image_new_from_resource ("/org/gnumeric/gnumeric/images/right-down.png");
	state->icon_dr = gtk_image_new_from_resource ("/org/gnumeric/gnumeric/images/down-right.png");

	gtk_widget_hide (state->icon_dr);
	gtk_widget_hide (state->icon_rd);

	gtk_grid_attach (GTK_GRID (order_grid), state->icon_rd, 1, 0, 1, 2);
	gtk_grid_attach (GTK_GRID (order_grid), state->icon_dr, 1, 0, 1, 2);

	g_signal_connect (G_OBJECT (order_rd), "toggled", G_CALLBACK (display_order_icon), state);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gridlines),
				      state->pi->print_grid_lines);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (onlystyles),
				      state->pi->print_even_if_only_styles);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (bw),
				      state->pi->print_black_and_white);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (titles),
				      state->pi->print_titles);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (do_not_print),
				      state->pi->do_not_print);

	order = state->pi->print_across_then_down ? order_rd : order_dr;
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (order), TRUE);
	display_order_icon (GTK_TOGGLE_BUTTON (order_rd), state);
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
	GnmPrintInformation *pi = state->pi;
	GtkBuilder *gui;
	double top, bottom;
	double left, right;
	double edge_to_below_header, edge_to_above_footer;
	char *text;
	char const *format;
	double scale;

	gui = state->gui;

	gtk_label_set_text (GTK_LABEL (go_gtk_builder_get_widget (gui,
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
	gtk_label_set_text (GTK_LABEL (go_gtk_builder_get_widget (gui,
							     "paper-size-label")),
			    text);
	g_free (text);

	print_info_get_margins (state->pi,
				&top, &bottom,
				&left, &right,
				&edge_to_below_header,
				&edge_to_above_footer);
	scale = get_conversion_factor (state->display_unit);
	do_update_margin (&state->margins.header,
			  (edge_to_below_header - top) / scale,
			  state->display_unit);
	do_update_margin (&state->margins.footer,
			  (edge_to_above_footer - bottom) / scale,
			  state->display_unit);
	do_update_margin (&state->margins.top, top / scale,
			  state->display_unit);
	do_update_margin (&state->margins.bottom, bottom / scale,
			  state->display_unit);
	do_update_margin (&state->margins.left, left / scale,
			  state->display_unit);
	do_update_margin (&state->margins.right, right / scale,
			  state->display_unit);
	configure_bounds_top (state);
	configure_bounds_header (state);
	configure_bounds_left (state);
	configure_bounds_right (state);
	configure_bounds_footer (state);
	configure_bounds_bottom (state);

	canvas_update (state);

	switch (print_info_get_paper_orientation (state->pi)) {
	case GTK_PAGE_ORIENTATION_PORTRAIT:
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (state->portrait_radio), TRUE);
		break;
	case GTK_PAGE_ORIENTATION_REVERSE_PORTRAIT:
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (state->rev_portrait_radio), TRUE);
		break;
	case GTK_PAGE_ORIENTATION_LANDSCAPE:
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (state->landscape_radio), TRUE);
		break;
	default:
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (state->rev_landscape_radio), TRUE);
		break;
	}
}


static void
orientation_changed_cb (PrinterSetupState *state)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->portrait_radio)))
		print_info_set_paper_orientation (state->pi, GTK_PAGE_ORIENTATION_PORTRAIT);
	else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->rev_portrait_radio)))
		print_info_set_paper_orientation (state->pi, GTK_PAGE_ORIENTATION_REVERSE_PORTRAIT);
	else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->landscape_radio)))
		print_info_set_paper_orientation (state->pi, GTK_PAGE_ORIENTATION_LANDSCAPE);
	else
		print_info_set_paper_orientation (state->pi, GTK_PAGE_ORIENTATION_REVERSE_LANDSCAPE);
	do_update_page (state);
}

static void
do_setup_page (PrinterSetupState *state)
{
	GtkBuilder *gui;

	gui = state->gui;

	g_signal_connect_swapped (G_OBJECT (go_gtk_builder_get_widget (gui, "paper-button")),
		"clicked",
		G_CALLBACK (dialog_gtk_printer_setup_cb), state);

	state->portrait_radio = go_gtk_builder_get_widget (gui, "portrait-button");
	state->landscape_radio = go_gtk_builder_get_widget (gui, "landscape-button");
	state->rev_portrait_radio = go_gtk_builder_get_widget (gui, "r-portrait-button");
	state->rev_landscape_radio = go_gtk_builder_get_widget (gui, "r-landscape-button");

	g_signal_connect_swapped (G_OBJECT (state->portrait_radio), "toggled",
				  G_CALLBACK (orientation_changed_cb), state);
	g_signal_connect_swapped (G_OBJECT (state->rev_portrait_radio), "toggled",
				  G_CALLBACK (orientation_changed_cb), state);
	g_signal_connect_swapped (G_OBJECT (state->landscape_radio), "toggled",
				  G_CALLBACK (orientation_changed_cb), state);

	do_setup_margin (state);

	do_update_page (state);

}


static void
scaling_percent_changed (GtkToggleButton *toggle, PrinterSetupState *state)
{
	gboolean scale_percent = gtk_toggle_button_get_active (toggle);

	gtk_widget_set_sensitive (GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "scale-percent-spin")), scale_percent);
	gtk_widget_set_sensitive (GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "scale-percent-label")), scale_percent);
}

static void
scaling_fit_to_h_changed (GtkToggleButton *toggle, PrinterSetupState *state)
{
	gboolean scale_fit_to_h   = gtk_toggle_button_get_active (toggle);
	gtk_widget_set_sensitive
		(GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "scale-h-spin")),
		 scale_fit_to_h);
	gtk_widget_set_sensitive
		(GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "fit-h-check-label")),
		 scale_fit_to_h);
}

static void
scaling_fit_to_v_changed (GtkToggleButton *toggle, PrinterSetupState *state)
{
	gboolean scale_fit_to_v   = gtk_toggle_button_get_active (toggle);
        gtk_widget_set_sensitive
                (GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "scale-v-spin")),
                 scale_fit_to_v);
        gtk_widget_set_sensitive
                (GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "fit-v-check-label")),
                 scale_fit_to_v);
}

static void
scaling_fit_to_changed (GtkToggleButton *toggle, PrinterSetupState *state)
{
	gboolean scale_fit_to  = gtk_toggle_button_get_active (toggle);

	if (scale_fit_to) {
		scaling_fit_to_h_changed (GTK_TOGGLE_BUTTON
					  (go_gtk_builder_get_widget (state->gui, "fit-h-check")), state);
		scaling_fit_to_v_changed (GTK_TOGGLE_BUTTON
					  (go_gtk_builder_get_widget (state->gui, "fit-v-check")), state);
	} else {
		gtk_widget_set_sensitive
			(GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "scale-v-spin")), FALSE);
		gtk_widget_set_sensitive
			(GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "fit-v-check-label")), FALSE);
		gtk_widget_set_sensitive
			(GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "scale-h-spin")), FALSE);
		gtk_widget_set_sensitive
			(GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "fit-h-check-label")), FALSE);
	}
	gtk_widget_set_sensitive
		(GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "fit-h-check")), scale_fit_to);
	gtk_widget_set_sensitive
		(GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "fit-v-check")), scale_fit_to);
}

static void
do_setup_scale (PrinterSetupState *state)
{
	GnmPrintInformation *pi = state->pi;
	GtkWidget *scale_percent_spin, *scale_width_spin, *scale_height_spin;
	GtkBuilder *gui;

	gui = state->gui;

	state->scale_percent_radio = go_gtk_builder_get_widget (gui, "scale-percent-radio");
	state->scale_fit_to_radio = go_gtk_builder_get_widget (gui, "scale-fit-to-radio");
	state->scale_no_radio = go_gtk_builder_get_widget (gui, "scale-no-radio");

	g_signal_connect (G_OBJECT (state->scale_percent_radio), "toggled",
			  G_CALLBACK (scaling_percent_changed), state);
	g_signal_connect (G_OBJECT (state->scale_fit_to_radio), "toggled",
			  G_CALLBACK (scaling_fit_to_changed), state);
	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (state->gui, "fit-h-check")),
			  "toggled", G_CALLBACK (scaling_fit_to_h_changed), state);
	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (state->gui, "fit-v-check")),
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

	scale_percent_spin = go_gtk_builder_get_widget (gui, "scale-percent-spin");
	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (scale_percent_spin), pi->scaling.percentage.x);
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				      GTK_WIDGET (scale_percent_spin));

	scale_width_spin = go_gtk_builder_get_widget (gui, "scale-h-spin");
	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (scale_width_spin), pi->scaling.dim.cols);
	gtk_toggle_button_set_active
		(GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui, "fit-h-check")),
		 pi->scaling.dim.cols > 0);
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				      GTK_WIDGET (scale_width_spin));

	scale_height_spin = go_gtk_builder_get_widget (gui, "scale-v-spin");
	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (scale_height_spin), pi->scaling.dim.rows);
	gtk_toggle_button_set_active
		(GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui, "fit-v-check")),
		 pi->scaling.dim.rows > 0);
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				      GTK_WIDGET (scale_height_spin));
}


static Sheet *
print_setup_get_sheet (PrinterSetupState *state)
{
	GtkWidget *w = go_gtk_builder_get_widget (state->gui, "apply-to-all");
	gboolean apply_all_sheets = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	gnm_conf_set_printsetup_all_sheets (apply_all_sheets);

	if (apply_all_sheets)
		return NULL;
	return workbook_sheet_by_index (state->sheet->workbook,
					gtk_combo_box_get_active (GTK_COMBO_BOX
								     (state->sheet_selector)));
}

static void
cb_do_print_preview (PrinterSetupState *state)
{
	GnmPrintInformation *old_pi;
	double width, height;
	GogGraph *graph = NULL;

	fetch_settings (state);
	old_pi = state->sheet->print_info;
	state->sheet->print_info = state->pi;
	if (state->sheet->sheet_type == GNM_SHEET_OBJECT) {
		graph = GOG_GRAPH (sheet_object_graph_get_gog (GNM_SO (state->sheet->sheet_objects->data)));
		if (graph) {
			double top, bottom, left, right, edge_to_below_header, edge_to_above_footer, w, h;
			gog_graph_get_size (graph, &width, &height);
			w = print_info_get_paper_width (state->pi, GTK_UNIT_POINTS);
			h = print_info_get_paper_height (state->pi, GTK_UNIT_POINTS);
			print_info_get_margins (state->pi, &top, &bottom, &left, &right, &edge_to_below_header, &edge_to_above_footer);
			w -= left + right;
			h -= top + bottom + edge_to_above_footer + edge_to_below_header;
			gog_graph_set_size (graph, w, h);
		}
	}
	gnm_print_sheet (GNM_WBC (state->wbcg),
		state->sheet, TRUE, GNM_PRINT_ACTIVE_SHEET, NULL);
	if (graph)
		gog_graph_set_size (graph, width, height);
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
	fetch_settings (state);
	if (gtk_toggle_button_get_active (
		    GTK_TOGGLE_BUTTON (
			    go_gtk_builder_get_widget (state->gui,
						  "is_default_check")))) {
		gnm_print_info_save (state->pi);
	}
	cmd_print_setup (GNM_WBC (state->wbcg),
		print_setup_get_sheet (state), state->pi);
	gtk_widget_destroy (state->dialog);
}

static void
cb_do_print (PrinterSetupState *state)
{
	Sheet *sheet = state->sheet;
	WorkbookControl *wbc = GNM_WBC (state->wbcg);

	cb_do_print_ok (state);
	gnm_print_sheet (wbc, sheet, FALSE, GNM_PRINT_SAVED_INFO, NULL);
}

static void
cb_do_print_destroy (PrinterSetupState *state)
{
	if (state->customize_header)
		gtk_widget_destroy (state->customize_header);

	if (state->customize_footer)
		gtk_widget_destroy (state->customize_footer);

	g_object_unref (state->gui);

	gnm_print_hf_free (state->header);
	gnm_print_hf_free (state->footer);
	gnm_print_info_free (state->pi);
	g_free (state->pi_header);
	g_free (state->pi_footer);
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
	GtkWidget *grid, *w;
	int i, n, n_this = 0;

	g_return_if_fail (state != NULL);
	g_return_if_fail (state->sheet != NULL);

	grid = go_gtk_builder_get_widget (state->gui, "sheet-grid");
	state->sheet_selector = gtk_combo_box_text_new ();
	n = workbook_sheet_count (state->sheet->workbook);
	for (i = 0 ; i < n ; i++) {
		Sheet * a_sheet = workbook_sheet_by_index (state->sheet->workbook, i);
		if (a_sheet == state->sheet)
			n_this = i;
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (state->sheet_selector),
					a_sheet->name_unquoted);
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (state->sheet_selector), n_this);
	gtk_widget_set_hexpand (state->sheet_selector, TRUE);
	gtk_grid_attach (GTK_GRID (grid), state->sheet_selector, 1, 1, 1, 1);
	w = go_gtk_builder_get_widget (state->gui, "apply-to-all");
	g_signal_connect (G_OBJECT (w),
		"toggled",
		G_CALLBACK (cb_do_sheet_selector_toggled), state);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w),
				      gnm_conf_get_printsetup_all_sheets ());
	cb_do_sheet_selector_toggled (GTK_TOGGLE_BUTTON (w), state);
	w = go_gtk_builder_get_widget (state->gui, "apply-to-selected");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w),
				      !gnm_conf_get_printsetup_all_sheets ());
	gtk_widget_show_all (grid);
}

static void
do_setup_main_dialog (PrinterSetupState *state)
{
	GtkWidget *w;

	g_return_if_fail (state != NULL);
	g_return_if_fail (state->sheet != NULL);
	g_return_if_fail (state->wbcg != NULL);

	state->dialog = go_gtk_builder_get_widget (state->gui, "print-setup");

	w = go_gtk_builder_get_widget (state->gui, "ok");
	g_signal_connect_swapped (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_do_print_ok), state);
	w = go_gtk_builder_get_widget (state->gui, "print");
	g_signal_connect_swapped (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_do_print), state);
	w = go_gtk_builder_get_widget (state->gui, "preview");
	g_signal_connect_swapped (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_do_print_preview), state);
	w = go_gtk_builder_get_widget (state->gui, "cancel");
	g_signal_connect_swapped (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_do_print_cancel), state);

	w = go_gtk_builder_get_widget (state->gui, "print-setup-notebook");
	gtk_notebook_set_current_page (GTK_NOTEBOOK (w), 0);

	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_do_print_destroy);
	wbc_gtk_attach_guru (state->wbcg, state->dialog);
}

static PrinterSetupState *
printer_setup_state_new (WBCGtk *wbcg, Sheet *sheet)
{
	PrinterSetupState *state;
	GtkBuilder *gui;

	gui = gnm_gtk_builder_load ("res:ui/print.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return NULL;

	state = g_new0 (PrinterSetupState, 1);
	state->wbcg  = wbcg;
	state->sheet = sheet;
	state->gui   = gui;
	state->pi    = gnm_print_info_dup (sheet->print_info);
	state->display_unit = state->pi->desired_display.top;
	state->customize_header = NULL;
	state->customize_footer = NULL;

	do_setup_main_dialog (state);
	do_setup_sheet_selector (state);
	do_setup_hf (state);
	do_setup_page_area (state);
	do_setup_page_info (state);
	do_setup_page (state);
	do_setup_scale (state);

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
	GtkBuilder *gui = state->gui;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->scale_no_radio))) {
		state->pi->scaling.percentage.x = state->pi->scaling.percentage.y = 100.;
		state->pi->scaling.type = PRINT_SCALE_PERCENTAGE;
	} else {
		w = go_gtk_builder_get_widget (gui, "scale-percent-spin");
		state->pi->scaling.percentage.x = state->pi->scaling.percentage.y
			= gtk_spin_button_get_value (GTK_SPIN_BUTTON (w));
		state->pi->scaling.type =
			((gtk_toggle_button_get_active
			  (GTK_TOGGLE_BUTTON (state->scale_percent_radio))) ?
			   PRINT_SCALE_PERCENTAGE : PRINT_SCALE_FIT_PAGES);
	}
	w = go_gtk_builder_get_widget (gui, "fit-h-check");
	if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)))
		state->pi->scaling.dim.cols = 0;
	else {
		w = go_gtk_builder_get_widget (gui, "scale-h-spin");
		state->pi->scaling.dim.cols =
			gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (w));
	}

	w = go_gtk_builder_get_widget (gui, "fit-v-check");
	if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)))
		state->pi->scaling.dim.rows = 0;
	else {
		w = go_gtk_builder_get_widget (gui, "scale-v-spin");
		state->pi->scaling.dim.rows =
			gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (w));
	}
}

/*
 * Header and footer are stored with Excel semantics, but displayed with
 * more natural semantics. In Excel, both top margin and header are measured
 * from top of sheet. See illustration at start of src/print.c. The Gnumeric
 * user interface presents header as the band between top margin and the
 * print area. Bottom margin and footer are handled likewise.
 */
static void
do_fetch_margins (PrinterSetupState *state)
{
	double header, footer, top, bottom, left, right;
	GtkPageSetup     *ps = gnm_print_info_get_page_setup (state->pi);
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

static void
do_fetch_hf (PrinterSetupState *state)
{
	gnm_print_hf_free (state->pi->header);
	gnm_print_hf_free (state->pi->footer);

	state->pi->header = gnm_print_hf_copy (state->header);
	state->pi->footer = gnm_print_hf_copy (state->footer);
}

static void
do_fetch_page_info (PrinterSetupState *state)
{
	GnmPrintInformation *pi = state->pi;
	GtkTreeIter iter;

	pi->print_grid_lines = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui, "check-grid-lines")));
	pi->print_even_if_only_styles = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui, "check-only-styles")));
	pi->print_black_and_white = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui, "check-black-white")));
	pi->print_titles = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui, "check-print-titles")));
	pi->print_across_then_down = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui, "radio-order-right")));
	pi->do_not_print = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui, "check-do-not-print")));

	g_free (pi->repeat_top);
	pi->repeat_top = g_strdup (gnm_expr_entry_get_text (state->top_entry));

	g_free (pi->repeat_left);
	pi->repeat_left = g_strdup (gnm_expr_entry_get_text (state->left_entry));

	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (state->error_display.combo), &iter))
		gtk_tree_model_get (GTK_TREE_MODEL (state->error_display.store), &iter,
				    1, &(pi->error_display),
				    -1);
	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (state->comment_display.combo), &iter))
		gtk_tree_model_get (GTK_TREE_MODEL (state->comment_display.store), &iter,
				    1, &(pi->comment_placement),
				    -1);
}

static void
fetch_settings (PrinterSetupState *state)
{
	do_fetch_page (state);
	do_fetch_scale (state);
	do_fetch_margins (state);
	do_fetch_unit (state);
	do_fetch_hf (state);
	do_fetch_page_info (state);
}


static void
dialog_printer_setup_done_cb (GtkPageSetup *page_setup,
			      gpointer data)
{
	if (page_setup) {
		PrinterSetupState *state = data;
		gnm_print_info_set_page_setup (state->pi,
			gtk_page_setup_copy (page_setup));
		do_update_page (state);
	}
}

static void
dialog_gtk_printer_setup_cb (PrinterSetupState *state)
{
	GtkPageSetup *page_setup = gnm_print_info_get_page_setup (state->pi);

	gtk_print_run_page_setup_dialog_async
		(GTK_WINDOW (state->dialog),
		 page_setup,
		 NULL,
		 dialog_printer_setup_done_cb,
		 state);
}



void
dialog_printer_setup (WBCGtk *wbcg, Sheet *sheet)
{
	PrinterSetupState *state;

	/* Only one guru per workbook. */
	if (wbc_gtk_get_guru (wbcg))
		return;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, PRINTER_SETUP_KEY))
		return;

	state = printer_setup_state_new (wbcg, sheet);
	if (!state)
		return;

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog),
					   state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_PRINTER_SETUP_GENERAL);
	gnm_keyed_dialog (
		wbcg, GTK_WINDOW (state->dialog), PRINTER_SETUP_KEY);
	gtk_widget_show (state->dialog);
}
