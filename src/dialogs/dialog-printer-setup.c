/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * dialog-printer-setup.c: Printer setup dialog box
 *
 * Author:
 *  Wayne Schuller (k_wayne@linuxpower.org)
 *  Miguel de Icaza (miguel@gnu.org)
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
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <print-info.h>
#include <print.h>
#include <ranges.h>
#include <sheet.h>
#include <workbook.h>
#include <workbook-edit.h>
#include <style.h>

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>

#define PREVIEW_X 170
#define PREVIEW_Y 170
#define PREVIEW_MARGIN_X 20
#define PREVIEW_MARGIN_Y 20
#define PAGE_X (PREVIEW_X - PREVIEW_MARGIN_X)
#define PAGE_Y (PREVIEW_Y - PREVIEW_MARGIN_Y)

#define MARGIN_COLOR_DEFAULT "gray"
#define MARGIN_COLOR_ACTIVE "black"

#define HF_PREVIEW_X 350
#define HF_PREVIEW_Y 50
#define HF_PREVIEW_SHADOW 2
#define HF_PREVIEW_PADDING 5
#define HF_PREVIEW_MARGIN 15

#define EPSILON 1e-5		/* Same as in gtkspinbutton.c */

#define PRINTER_SETUP_KEY "printer-setup-dialog"

/* FIXME: Now that we have added a header/footer sample
 * preview widget, we should rename the preview widget for the margins
 * to be more specific.
 * eg: PreviewInfo should become MarginPreviewInfo.
 */
typedef struct {
	/* The Canvas object */
	GtkWidget        *canvas;

	/* Objects in the Preview Canvas */
	GnomeCanvasItem  *group;

	/* Values for the scaling of the nice preview */
	int offset_x, offset_y;	/* For centering the small page preview */
	double scale;
} PreviewInfo;

typedef struct {
	/* The Canvas object for the header/footer sample preview */
	GtkWidget        *canvas;

	/* Objects in the Preview Canvas */
	GnomeCanvasItem  *left;
	GnomeCanvasItem  *middle;
	GnomeCanvasItem  *right;

} HFPreviewInfo;

typedef enum {
	MARGIN_NONE,
	MARGIN_LEFT,
	MARGIN_RIGHT,
	MARGIN_TOP,
	MARGIN_BOTTOM,
	MARGIN_HEADER,
	MARGIN_FOOTER
} MarginOrientation;

typedef struct {
	UnitName   unit;
	double     value;
	GtkSpinButton *spin;
	GtkAdjustment *adj;

	GnomeCanvasItem *line;
	MarginOrientation orientation;
	double bound_x1, bound_y1, bound_x2, bound_y2;
	PreviewInfo *pi;
} UnitInfo;

typedef struct {
	WorkbookControlGUI  *wbcg;
	Sheet            *sheet;
	GladeXML         *gui;
	PrintInformation *pi;
	GtkWidget        *dialog;

	struct {
		UnitInfo top, bottom;
		UnitInfo left, right;
		UnitInfo header, footer;
	} margins;

	GList *conversion_listeners;

	GnomePrintPaper const *paper;
	GnomePrintPaper const *current_paper;
	PrintOrientation orientation;
	PrintOrientation current_orientation;

	PreviewInfo preview;

	GtkWidget *icon_rd;
	GtkWidget *icon_dr;
	GnumericExprEntry *area_entry;
	GnumericExprEntry *top_entry;
	GnumericExprEntry *left_entry;

	/* The header and footer data. */
	PrintHF *header;
	PrintHF *footer;

	/* The header and footer customize dialogs. */
	GtkWidget *customize_header;
	GtkWidget *customize_footer;

	/* The header and footer preview widgets. */
	HFPreviewInfo *pi_header;
	HFPreviewInfo *pi_footer;
} PrinterSetupState;

typedef struct
{
    PrinterSetupState *state;
    UnitInfo *target;
} UnitInfo_cbdata;

static void fetch_settings (PrinterSetupState *state);
static void printer_setup_state_free (PrinterSetupState *state);
static void do_hf_customize (gboolean header, PrinterSetupState *state);

#if 0
static double
unit_into_to_points (UnitInfo *ui)
{
	return unit_convert (ui->value, ui->unit, UNIT_POINTS);
}
#endif

/**
 * spin_button_set_bound
 * @spin           spinbutton
 * @space_to_grow  how much higher value may go
 * @space_unit     unit space_to_grow_is measured in
 * @spin_unit      unit spinbutton uses
 *
 * Allow the value in spin button to increase by at most space_to_grow.
 * If space_to_grow is negative, e.g. after paper size change,
 * spin_button_set_bound adjusts the margin and returns a less
 *  negative space_to_grow.
 */
static double
spin_button_set_bound (GtkSpinButton *spin, double space_to_grow,
		       UnitName space_unit, UnitName spin_unit)
{
	GtkAdjustment *adjustment;

	g_return_val_if_fail (GTK_IS_SPIN_BUTTON (spin), 1); /* Arbitrary */
	adjustment = gtk_spin_button_get_adjustment (spin);
	g_return_val_if_fail (GTK_IS_ADJUSTMENT (adjustment), 1); /* Ditto */

	space_to_grow = unit_convert (space_to_grow, space_unit, spin_unit);

	if (space_to_grow + EPSILON < 0) {

		double shrink = MIN (-space_to_grow, (double)adjustment->value);

		space_to_grow += shrink;
		adjustment->upper = adjustment->value - shrink;
		gtk_adjustment_changed (adjustment);
		adjustment->value = adjustment->upper;
		gtk_adjustment_value_changed (adjustment);
	} else {
		adjustment->upper = adjustment->value + space_to_grow;
		gtk_adjustment_changed (adjustment);
	}

	return unit_convert (space_to_grow, spin_unit, space_unit);
}

/**
 * get_paper_width
 * @state :
 *
 * Return paper width in points, taking page orientation into account.
 */
static double
get_paper_pswidth (PrinterSetupState *state)
{
	g_return_val_if_fail (state->paper != NULL, 1.);
	if (state->orientation == PRINT_ORIENT_VERTICAL)
		return state->paper->width;
	else
		return state->paper->height;
}

/**
 * get_paper_psheight
 * @state :
 *
 * Return paper height in points, taking page orientation into account.
 */
static double
get_paper_psheight (PrinterSetupState *state)
{
	g_return_val_if_fail (state->paper != NULL, 1.);
	if (state->orientation == PRINT_ORIENT_VERTICAL)
		return state->paper->height;
	else
		return state->paper->width;
}

/**
 * get_printable_width
 * @state  :
 * @unit unit
 *
 * Return page width minus margins in specified unit.
 */
static double
get_printable_width (PrinterSetupState *state, UnitName unit)
{
	return unit_convert (get_paper_pswidth (state), UNIT_POINTS, unit)
		- unit_convert (state->margins.left.value,
				state->margins.left.unit, unit)
		- unit_convert (state->margins.right.value,
				state->margins.right.unit, unit);
}

/**
 * get_printable_height
 * @state :
 * @unit unit
 *
 * Return page height minus margins, header and footer in specified unit.
 */
static double
get_printable_height (PrinterSetupState *state, UnitName unit)
{
	return unit_convert (get_paper_psheight (state), UNIT_POINTS, unit)
		- unit_convert (state->margins.top.value,
				state->margins.top.unit, unit)
		- unit_convert (state->margins.bottom.value,
				state->margins.bottom.unit, unit)
		- unit_convert (state->margins.header.value,
				state->margins.header.unit, unit)
		- unit_convert (state->margins.footer.value,
				state->margins.footer.unit, unit);
}

/**
 * set_horizontal_bounds
 * @state :
 * @margin_fixed  margin to remain unchanged
 * @unit          unit
 *
 * Set the upper bounds for left and right margins.
 * If margin_fixed is one of those margins, it kept unchanged. This is to
 * avoid the possibility of an endless loop.
 */
static void
set_horizontal_bounds (PrinterSetupState *state,
		       MarginOrientation margin_fixed,
		       UnitName unit)
{
	double printable_width = get_printable_width (state, unit);

	if (margin_fixed != MARGIN_LEFT)
		printable_width
			= spin_button_set_bound (state->margins.left.spin,
						 printable_width,
						 unit,
						 state->margins.left.unit);

	if (margin_fixed != MARGIN_RIGHT)
		printable_width
			= spin_button_set_bound (state->margins.right.spin,
						 printable_width,
						 unit,
						 state->margins.right.unit);
}

/**
 * set_vertical_bounds
 * @state :
 * @margin_fixed  margin to remain unchanged
 * @unit          unit
 *
 * Set the upper bounds for top/bottom margins, headers and footers.
 * If margin_fixed is one of those margins, it kept unchanged. This is to
 * avoid the possibility of an endless loop.
 */
static void
set_vertical_bounds (PrinterSetupState *state,
		     MarginOrientation margin_fixed,
		     UnitName unit)
{
	double printable_height = get_printable_height (state, unit);

	if (margin_fixed != MARGIN_TOP)
		printable_height
			= spin_button_set_bound (state->margins.top.spin,
						 printable_height,
						 unit,
						 state->margins.top.unit);

	if (margin_fixed != MARGIN_BOTTOM)
		printable_height
			= spin_button_set_bound (state->margins.bottom.spin,
						 printable_height,
						 unit,
						 state->margins.bottom.unit);

	if (margin_fixed != MARGIN_HEADER)
		printable_height
			= spin_button_set_bound (state->margins.header.spin,
						 printable_height,
						 unit,
						 state->margins.header.unit);

	if (margin_fixed != MARGIN_FOOTER)
		printable_height
			= spin_button_set_bound (state->margins.footer.spin,
						 printable_height,
						 unit,
						 state->margins.footer.unit);
}

static void
preview_page_destroy (PrinterSetupState *state)
{
	if (state->preview.group) {
		gtk_object_destroy (GTK_OBJECT (state->preview.group));

		state->preview.group = NULL;
	}
}

static void
move_line (GnomeCanvasItem *item,
	   double x1, double y1,
	   double x2, double y2)
{
	GnomeCanvasPoints *points;

	points = gnome_canvas_points_new (2);
	points->coords[0] = x1;
	points->coords[1] = y1;
	points->coords[2] = x2;
	points->coords[3] = y2;

	gnome_canvas_item_set (item,
			       "points", points,
			       NULL);
	gnome_canvas_points_unref (points);
}

static GnomeCanvasItem *
make_line (GnomeCanvasGroup *g, double x1, double y1, double x2, double y2)
{
	GnomeCanvasPoints *points;
	GnomeCanvasItem *item;

	points = gnome_canvas_points_new (2);
	points->coords[0] = x1;
	points->coords[1] = y1;
	points->coords[2] = x2;
	points->coords[3] = y2;

	item = gnome_canvas_item_new (
		GNOME_CANVAS_GROUP (g), gnome_canvas_line_get_type (),
		"points", points,
		"width_pixels", 1,
		"fill_color",   MARGIN_COLOR_DEFAULT,
		NULL);
	gnome_canvas_points_unref (points);

	return item;
}

static void
draw_margin (UnitInfo *uinfo, PrinterSetupState *state)
{
	double x1, y1, x2, y2;
	double val = unit_convert (uinfo->value,
				   uinfo->unit, UNIT_POINTS);

	x1 = uinfo->bound_x1;
	y1 = uinfo->bound_y1;
	x2 = uinfo->bound_x2;
	y2 = uinfo->bound_y2;

	switch (uinfo->orientation)
	{
	case MARGIN_LEFT:
		x1 += uinfo->pi->scale * val;
		if (x1 < x2)
			x2 = x1;
		else
			x1 = x2;
		break;
	case MARGIN_RIGHT:
		x2 -= uinfo->pi->scale * val;
		if (x2 < x1)
			x2 = x1;
		else
			x1 = x2;
		break;
	case MARGIN_TOP:
		y1 += uinfo->pi->scale * val;
		if (y1 < y2)
			y2 = y1;
		else
			y1 = y2;
		draw_margin (&state->margins.header, state);
		break;
	case MARGIN_BOTTOM:
		y2 -= uinfo->pi->scale * val;
		if (y2 < y1)
			y2 = y1;
		else
			y1 = y2;
		draw_margin (&state->margins.footer, state);
		break;
	case MARGIN_HEADER:
		y1 += (uinfo->pi->scale * unit_convert (state->margins.top.value,
							state->margins.top.unit,
							UNIT_POINTS)
		       + uinfo->pi->scale * val);
		y2 = y1;
		break;
	case MARGIN_FOOTER:
		y2 -= (uinfo->pi->scale * unit_convert (state->margins.bottom.value,
							state->margins.bottom.unit,
							UNIT_POINTS)
		       + uinfo->pi->scale * val);
		y1 = y2;
		break;
	default:
		return;
	}

	move_line (uinfo->line, x1, y1, x2, y2);
}

static void
create_margin (PrinterSetupState *state,
	       UnitInfo *uinfo,
	       MarginOrientation orientation,
	       double x1, double y1,
	       double x2, double y2)
{
	GnomeCanvasGroup *g = GNOME_CANVAS_GROUP (state->preview.group);

	uinfo->pi = &state->preview;
	uinfo->line = make_line (g, x1 + 8, y1, x1 + 8, y2);
	uinfo->orientation = orientation;
	uinfo->bound_x1 = x1;
	uinfo->bound_y1 = y1;
	uinfo->bound_x2 = x2;
	uinfo->bound_y2 = y2;

	draw_margin (uinfo, state);
}

static void
draw_margins (PrinterSetupState *state, double x1, double y1, double x2, double y2)
{
	/* Headers & footers */
	create_margin (state, &state->margins.header, MARGIN_HEADER,
		       x1, y1, x2, y2);
	create_margin (state, &state->margins.footer, MARGIN_FOOTER,
		       x1, y1, x2, y2);

	/* Margins */
	create_margin (state, &state->margins.left, MARGIN_LEFT,
		       x1, y1, x2, y2);
	create_margin (state, &state->margins.right, MARGIN_RIGHT,
		       x1, y1, x2, y2);
	create_margin (state, &state->margins.top, MARGIN_TOP,
		       x1, y1, x2, y2);
	create_margin (state, &state->margins.bottom, MARGIN_BOTTOM,
		       x1, y1, x2, y2);
}

static void
preview_page_create (PrinterSetupState *state)
{
	double x1, y1, x2, y2;
	double width, height;
	PreviewInfo *pi = &state->preview;

	width  = get_paper_pswidth  (state);
	height = get_paper_psheight (state);

	if (width < height)
		pi->scale = PAGE_Y / height;
	else
		pi->scale = PAGE_X / width;

	pi->offset_x = (PREVIEW_X - (width  * pi->scale)) / 2;
	pi->offset_y = (PREVIEW_Y - (height * pi->scale)) / 2;
/*	pi->offset_x = pi->offset_y = 0; */
	x1 = pi->offset_x + 0 * pi->scale;
	y1 = pi->offset_y + 0 * pi->scale;
	x2 = pi->offset_x + width * pi->scale;
	y2 = pi->offset_y + height * pi->scale;

	pi->group = gnome_canvas_item_new (
		gnome_canvas_root (GNOME_CANVAS (pi->canvas)),
		gnome_canvas_group_get_type (),
		"x", 0.0,
		"y", 0.0,
		NULL);

	gnome_canvas_item_new (GNOME_CANVAS_GROUP (pi->group),
		GNOME_TYPE_CANVAS_RECT,
		"x1",  	      	 (double) x1+2,
		"y1",  	      	 (double) y1+2,
		"x2",  	      	 (double) x2+2,
		"y2",         	 (double) y2+2,
		"fill_color",    "black",
		"outline_color", "black",
		"width_pixels",   1,
		NULL);

	gnome_canvas_item_new (GNOME_CANVAS_GROUP (pi->group),
		GNOME_TYPE_CANVAS_RECT,
		"x1",  	      	 (double) x1,
		"y1",  	      	 (double) y1,
		"x2",  	      	 (double) x2,
		"y2",         	 (double) y2,
		"fill_color",    "white",
		"outline_color", "black",
		"width_pixels",   1,
		NULL);

	draw_margins (state, x1, y1, x2, y2);
}

static void
canvas_update (PrinterSetupState *state)
{
	if (state->current_paper != state->paper ||
	    state->current_orientation != state->orientation) {
		preview_page_destroy (state);
		state->current_paper = state->paper;
		state->current_orientation = state->orientation;
		preview_page_create (state);
		/* FIXME: Introduce state->displayed_unit? */
		set_horizontal_bounds (state, MARGIN_NONE,
				       state->margins.top.unit);
		set_vertical_bounds (state, MARGIN_NONE,
				     state->margins.top.unit);
	}
}

/**
 * spin_button_adapt_to_unit
 * @spin      spinbutton
 * @new_unit  new unit
 *
 * Select suitable increments and number of digits for the unit.
 *
 * The increments are not scaled linearly. We assume that if you are using a
 * fine grained unit - pts or mm - you want to fine tune the margin. For cm
 * and in, a useful coarse increment is used, which is a round number in that
 * unit.
 *
 * NOTE: According to docs, we should be using gtk_spin_button_configure
 * here. But as of gtk+ 1.2.7, climb_rate has no effect for that call.
 */
static void
spin_button_adapt_to_unit (GtkSpinButton *spin, UnitName new_unit)
{
	GtkAdjustment *adjustment;
	gfloat step_increment;
	guint digits;

	g_return_if_fail (GTK_IS_SPIN_BUTTON (spin));
	adjustment = gtk_spin_button_get_adjustment (spin);
	g_return_if_fail (GTK_IS_ADJUSTMENT (adjustment));

	switch (new_unit) {
	case UNIT_POINTS:
		/* Fallthrough */
	case UNIT_MILLIMETER:
		step_increment = 1.0;
		digits = 1;
		break;
	case UNIT_CENTIMETER:
		step_increment = 0.5;
		digits = 2;
		break;
	case UNIT_INCH:
	default:
		step_increment = 0.25;
		digits = 2;
		break;
	}
	adjustment->step_increment = step_increment;
	adjustment->page_increment = step_increment * 10;
	gtk_adjustment_changed (adjustment);
	gtk_spin_button_set_digits (spin, digits);
}

static void
do_convert (UnitInfo *target, UnitName new_unit)
{
	double new_value;

	if (target->unit == new_unit)
		return;
	new_value = unit_convert (target->value, target->unit, new_unit);

	spin_button_adapt_to_unit (target->spin, new_unit);
	target->adj->value = target->value = new_value;
	target->unit = new_unit;

	gtk_spin_button_set_value (target->spin, target->value);
}

static void
convert_to_pt (UnitInfo *target)
{
	do_convert (target, UNIT_POINTS);
}

static void
convert_to_mm (UnitInfo *target)
{
	do_convert (target, UNIT_MILLIMETER);
}

static void
convert_to_cm (UnitInfo *target)
{
	do_convert (target, UNIT_CENTIMETER);
}

static void
convert_to_in (UnitInfo *target)
{
	do_convert (target, UNIT_INCH);
}

static void
listeners_convert (GtkWidget *item, void (*convert) (UnitInfo *))
{
	PrinterSetupState *state;
	GList *l;

	state = gtk_object_get_data (GTK_OBJECT (item), "dialog-print-info");
	g_return_if_fail (state != NULL);

	for (l = state->conversion_listeners; l; l = l->next)
		(convert) ((UnitInfo *) l->data);
}

static void
add_unit (GtkWidget *menu, int i, PrinterSetupState *state,
	  void (*convert)(UnitInfo *))
{
	GtkWidget *item;

	item = gtk_menu_item_new_with_label (unit_name_get_short_name (i, TRUE));
	gtk_widget_show (item);
	gtk_menu_append (GTK_MENU (menu), item);

	gtk_object_set_data (GTK_OBJECT (item),
			     "dialog-print-info", (gpointer) state);
	g_signal_connect (G_OBJECT (item),
		"activate",
		G_CALLBACK (listeners_convert), convert);
}


static void
unit_changed (GtkSpinButton *spin_button, UnitInfo_cbdata *data)
{
	data->target->value = data->target->adj->value;

	switch (data->target->orientation) {
	case MARGIN_LEFT:
	case MARGIN_RIGHT:
		set_horizontal_bounds (data->state,
				       data->target->orientation,
				       data->target->unit);
		break;
	default:
		set_vertical_bounds (data->state,
				     data->target->orientation,
				     data->target->unit);
	}

	/* Adjust the display to the current values */
	draw_margin (data->target, data->state);
}

static void
unit_activated (GtkSpinButton *spin_button,
		GdkEventFocus *event,
		UnitInfo_cbdata *data)
{
	gnome_canvas_item_set (data->target->line,
			       "fill_color", MARGIN_COLOR_ACTIVE,
			       NULL);
}

static void
unit_deactivated (GtkSpinButton *spin_button,
		  GdkEventFocus *event,
		  UnitInfo_cbdata *data)
{
	gnome_canvas_item_set (data->target->line,
			       "fill_color", MARGIN_COLOR_DEFAULT,
			       NULL);
}

static void
unit_editor_configure (UnitInfo *target, PrinterSetupState *state,
		       const char *spin_name,
		       double init_points, UnitName unit)
{
	GtkSpinButton *spin;
	UnitInfo_cbdata *cbdata;

	spin = GTK_SPIN_BUTTON (glade_xml_get_widget (state->gui, spin_name));

	target->unit = unit;
	target->value = unit_convert (init_points, UNIT_POINTS, target->unit);

	target->adj = GTK_ADJUSTMENT (gtk_adjustment_new (
		target->value,
		0.0, 1000.0, 0.5, 1.0, 1.0));
	target->spin = spin;
	gtk_spin_button_configure (spin, target->adj, 1, 1);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				      GTK_WIDGET (spin));
	gtk_widget_set_usize (GTK_WIDGET (target->spin), 60, 0);
	spin_button_adapt_to_unit (target->spin, unit);

	cbdata = g_new (UnitInfo_cbdata, 1);
	cbdata->state = state;
	cbdata->target = target;
	g_signal_connect (G_OBJECT (target->spin),
		"focus_in_event",
		G_CALLBACK (unit_activated), cbdata);
	g_signal_connect (G_OBJECT (target->spin),
		"focus_out_event",
		G_CALLBACK (unit_deactivated), cbdata);
	gtk_signal_connect_full (
		GTK_OBJECT (target->spin), "changed",
		G_CALLBACK (unit_changed),
		NULL,
		cbdata,
		(GtkDestroyNotify)g_free,
		FALSE, FALSE);
	state->conversion_listeners
		= g_list_append (state->conversion_listeners, (gpointer) target);
}

/**
 * Each margin is stored with a unit. We use the top margin unit for display
 * and ignore desired display for the others.
 *
 * Header and footer are stored with Excel semantics, but displayed with
 * more natural semantics. In Excel, both top margin and header are measured
 * from top of sheet. The Gnumeric user interface presents header as the
 * band between top margin and the print area. Bottom margin and footer are
 * handled likewise. See illustration at top of src/print.c
 */
static void
do_setup_margin (PrinterSetupState *state)
{
	GtkWidget *option_menu, *menu;
	GtkBox *container;
	PrintMargins *pm;
	UnitName displayed_unit;

	g_return_if_fail (state && state->pi);

	pm = &state->pi->margins;
	displayed_unit = pm->top.desired_display;

	state->preview.canvas = gnome_canvas_new ();
	gnome_canvas_set_scroll_region (
		GNOME_CANVAS (state->preview.canvas),
		0.0, 0.0, PREVIEW_X, PREVIEW_Y);
	gtk_widget_set_usize (state->preview.canvas, PREVIEW_X, PREVIEW_Y);
	gtk_widget_show (state->preview.canvas);

	option_menu = glade_xml_get_widget (state->gui, "option-menu-units");
	/* Remove the menu built with glade
	   - it is there only to get the option menu properly sized */
	gtk_option_menu_remove_menu (GTK_OPTION_MENU (option_menu));
	menu = gtk_menu_new ();

	add_unit (menu, UNIT_POINTS, state, convert_to_pt);
	add_unit (menu, UNIT_MILLIMETER, state, convert_to_mm);
	add_unit (menu, UNIT_CENTIMETER, state, convert_to_cm);
	add_unit (menu, UNIT_INCH, state, convert_to_in);

	gtk_option_menu_set_menu (GTK_OPTION_MENU (option_menu), menu);
	gtk_option_menu_set_history (GTK_OPTION_MENU (option_menu),
				     displayed_unit);

	unit_editor_configure (&state->margins.top, state, "spin-top",
			       pm->header.points,
			       displayed_unit);
	unit_editor_configure (&state->margins.header, state, "spin-header",
			       MAX (pm->top.points - pm->header.points, 0.0),
			       displayed_unit);
	unit_editor_configure (&state->margins.left, state, "spin-left",
			       pm->left.points, displayed_unit);
	unit_editor_configure (&state->margins.right, state, "spin-right",
			       pm->right.points, displayed_unit);
	unit_editor_configure (&state->margins.bottom, state, "spin-bottom",
			       pm->footer.points, displayed_unit);
	unit_editor_configure (&state->margins.footer, state, "spin-footer",
			       MAX (pm->bottom.points - pm->footer.points, 0.0),
			       displayed_unit);

	container = GTK_BOX (glade_xml_get_widget (state->gui,
						   "container-margin-page"));
	gtk_box_pack_start (container, state->preview.canvas, TRUE, TRUE, 0);

	if (state->pi->center_vertically)
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (
				glade_xml_get_widget (state->gui, "center-vertical")),
			TRUE);

	if (state->pi->center_horizontally)
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (
				glade_xml_get_widget (state->gui, "center-horizontal")),
			TRUE);
}

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

	/* FIXME: Use real values. */
	hfi->page = 1;
	hfi->pages = 1;

	if (header) {
		pi = state->pi_header;
		sample = state->header;
	} else {
		pi = state->pi_footer;
		sample = state->footer;
	}

	text = hf_format_render (sample->left_format, hfi, HF_RENDER_PRINT);
	gnome_canvas_item_set (pi->left, "text", (const gchar *) text, NULL);
	g_free (text);

	text = hf_format_render (sample->middle_format, hfi, HF_RENDER_PRINT);
	gnome_canvas_item_set (pi->middle, "text", text, NULL);
	g_free (text);

	text  = hf_format_render (sample->right_format, hfi, HF_RENDER_PRINT);
	gnome_canvas_item_set (pi->right, "text", text, NULL);
	g_free (text);

	hf_render_info_destroy (hfi);
}

static void
header_changed (GtkObject *object, PrinterSetupState *state)
{
	PrintHF *format = gtk_object_get_user_data (object);

	print_hf_free (state->header);
	state->header = print_hf_copy (format);

	display_hf_preview (state, TRUE);
}

static void
footer_changed (GtkObject *object, PrinterSetupState *state)
{
	PrintHF *format = gtk_object_get_user_data (object);

	print_hf_free (state->footer);
	state->footer = print_hf_copy (format);

	display_hf_preview (state, FALSE);
}

static void
do_header_customize (GtkWidget *button, PrinterSetupState *state)
{
	do_hf_customize (TRUE, state);
}

static void
do_footer_customize (GtkWidget *button, PrinterSetupState *state)
{
	do_hf_customize (FALSE, state);
}

/*
 * Fills one of the GtkCombos for headers or footers with the list
 * of existing header/footer formats
 */
static void
fill_hf (PrinterSetupState *state, GtkOptionMenu *om, GCallback callback, gboolean header)
{
	GList *l;
	HFRenderInfo *hfi;
	GtkWidget *menu;
	GtkWidget *li;
	char *res;
	PrintHF *select = header ? state->header : state->footer;
	int i, idx = 0;

	hfi = hf_render_info_new ();
	hfi->page = 1;
	hfi->pages = 1;

	menu = gtk_menu_new ();

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

		li = gtk_menu_item_new_with_label (res);
		gtk_widget_show (li);
		gtk_container_add (GTK_CONTAINER (menu), li);
		gtk_object_set_user_data (GTK_OBJECT (li), format);
		g_signal_connect (G_OBJECT (li), "activate", callback, state);

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
	li = gtk_menu_item_new_with_label (res);
	gtk_widget_show (li);
	gtk_container_add (GTK_CONTAINER (menu), li);
	g_signal_connect (G_OBJECT (li),
		"activate",
		header  ? G_CALLBACK (do_header_customize)
			: G_CALLBACK (do_footer_customize), state);
	g_free (res);

	gtk_option_menu_set_menu (om, menu);
	gtk_option_menu_set_history (om, idx);

	hf_render_info_destroy (hfi);
}

static void
do_setup_hf_menus (PrinterSetupState *state)
{
	GtkOptionMenu *header;
	GtkOptionMenu *footer;

	g_return_if_fail (state != NULL);

	header = GTK_OPTION_MENU (glade_xml_get_widget (state->gui, "option-menu-header"));
	footer = GTK_OPTION_MENU (glade_xml_get_widget (state->gui, "option-menu-footer"));

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
hf_customize_cancel (GtkWidget *button, GtkWidget *dialog)
{
		gtk_widget_destroy (dialog);
}

static void hf_customize_apply (GtkWidget *button, GtkWidget *dialog);

static void
hf_customize_ok (GtkWidget *button, GtkWidget *dialog)
{
	hf_customize_apply (button, dialog);
	gtk_widget_destroy (dialog);
}

static void
hf_customize_apply (GtkWidget *button, GtkWidget *dialog)
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

	header = GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (dialog), "header"));
	state = gtk_object_get_data (GTK_OBJECT (dialog), "state");

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

static gboolean 
hf_changed (GtkWidget *dummy,  GladeXML *gui)
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

	gui = gnumeric_glade_xml_new (state->wbcg, "hf-config.glade");

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

	g_signal_connect (G_OBJECT (glade_xml_get_widget (gui, "apply_button")), "clicked",
			  G_CALLBACK (hf_customize_apply), dialog);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (gui, "ok_button")), "clicked",
			  G_CALLBACK (hf_customize_ok), dialog);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (gui, "cancel_button")), "clicked",
			  G_CALLBACK (hf_customize_cancel), dialog);
	gtk_widget_set_sensitive (glade_xml_get_widget (gui, "apply_button"), FALSE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gui, "ok_button"), FALSE);	

	if (header)
		g_signal_connect (G_OBJECT (dialog), "destroy", 
				  G_CALLBACK (gtk_widget_destroyed), &state->customize_header);
	else
		g_signal_connect (G_OBJECT (dialog), "destroy", 
				  G_CALLBACK (gtk_widget_destroyed), &state->customize_footer);


	/* Remember whether it is customizing header or footer. */
	gtk_object_set_data (GTK_OBJECT (dialog), "header", GINT_TO_POINTER (header));
	gtk_object_set_data (GTK_OBJECT (dialog), "state", state);

	/* Setup bindings to mark when the entries are modified. */
	g_signal_connect (GTK_OBJECT (left), "changed",
			G_CALLBACK (hf_changed), gui);
	g_signal_connect (GTK_OBJECT (middle), "changed",
			G_CALLBACK (hf_changed), gui);
	g_signal_connect (GTK_OBJECT (right), "changed",
			G_CALLBACK (hf_changed), gui);


/* FIXME: Add correct helpfile address */
	if (header)
		gnumeric_init_help_button (
			glade_xml_get_widget (gui, "help_button"),
			"header-customization.html");
	else
		gnumeric_init_help_button (
			glade_xml_get_widget (gui, "help_button"),
			"footer-customization.html");

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
header_preview_event (GnomeCanvas *canvas, GdkEvent *event, PrinterSetupState *state)
{
	if (event == NULL ||
	    event->button.button != 1 ||
	    event->type != GDK_2BUTTON_PRESS)
		return FALSE;
	do_hf_customize (TRUE, state);
	return TRUE;
}

static gboolean
footer_preview_event (GnomeCanvas *canvas, GdkEvent *event, PrinterSetupState *state)
{
	if (event == NULL ||
	    event->button.button != 1 ||
	    event->type != GDK_2BUTTON_PRESS)
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

	pi->canvas = gnome_canvas_new ();

	gnome_canvas_set_scroll_region (GNOME_CANVAS (pi->canvas), 0.0, 0.0, width, width);

        gnome_canvas_item_new (gnome_canvas_root (GNOME_CANVAS (pi->canvas)),
		GNOME_TYPE_CANVAS_RECT,
		"x1",		shadow,
		"y1",		(header ? shadow : 0),
		"x2",		width + shadow,
		"y2",		height + (header ? 0 : shadow),
		"fill_color",	"black",
		NULL);

        gnome_canvas_item_new (gnome_canvas_root (GNOME_CANVAS (pi->canvas)),
		GNOME_TYPE_CANVAS_RECT,
		"x1",		0.0,
		"y1",		0.0,
		"x2",		width,
		"y2",		height,
		"fill_color",	"white",
		NULL);

	/* Use the Gnumeric default font. */
	font_desc = pango_font_description_new ();
	pango_font_description_set_family (font_desc, DEFAULT_FONT);
	pango_font_description_set_style (font_desc, PANGO_STYLE_NORMAL);
	pango_font_description_set_variant (font_desc, PANGO_VARIANT_NORMAL);
	pango_font_description_set_weight (font_desc, PANGO_WEIGHT_NORMAL);
	pango_font_description_set_size (font_desc, 14);

	pi->left = gnome_canvas_item_new (
		gnome_canvas_root (GNOME_CANVAS (pi->canvas)),
		gnome_canvas_text_get_type (),
		"x",		padding,
		"y",		header ? margin : bottom_margin,
		"anchor",	GTK_ANCHOR_WEST,
		"font_desc",	font_desc,
		"fill_color",	"black",
		"text",		"Left",
		NULL);

	pi->middle = gnome_canvas_item_new (
		gnome_canvas_root (GNOME_CANVAS (pi->canvas)),
		gnome_canvas_text_get_type (),
		"x",		width / 2,
		"y",		header ? margin : bottom_margin,
		"anchor",	GTK_ANCHOR_CENTER,
		"font_desc",	font_desc,
		"fill_color",	"black",
		"text",		"Center",
		NULL);

	pi->right = gnome_canvas_item_new (
		gnome_canvas_root (GNOME_CANVAS (pi->canvas)),
		gnome_canvas_text_get_type (),
		"x",		width - padding,
		"y",		header ? margin : bottom_margin,
		"anchor",	GTK_ANCHOR_EAST,
		"font_desc",    font_desc,
		"fill_color",	"black",
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
	g_return_if_fail (state != NULL);

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
	GtkCombo *comments_combo;
	GtkWidget *order;

	state->area_entry = gnumeric_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_scg (state->area_entry, wbcg_cur_scg (state->wbcg));
	gnm_expr_entry_set_flags (state->area_entry,
				       GNUM_EE_SHEET_OPTIONAL,
				       GNUM_EE_SHEET_OPTIONAL);
	gtk_box_pack_start (GTK_BOX (pa_hbox), GTK_WIDGET (state->area_entry),
			    TRUE, TRUE, 0);
	gtk_widget_show (GTK_WIDGET (state->area_entry));

	state->top_entry = gnumeric_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_scg (state->top_entry, wbcg_cur_scg (state->wbcg));
	gnm_expr_entry_set_flags (state->top_entry,
		GNUM_EE_FULL_ROW | GNUM_EE_SHEET_OPTIONAL,
		GNUM_EE_FULL_ROW | GNUM_EE_SHEET_OPTIONAL);
	gtk_table_attach (GTK_TABLE (repeat_table),
			  GTK_WIDGET (state->top_entry),
			  1, 2, 0, 1,
			  GTK_EXPAND|GTK_FILL, 0, 0, 0);
	gtk_widget_show (GTK_WIDGET (state->top_entry));

	state->left_entry = gnumeric_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_scg (state->left_entry, wbcg_cur_scg (state->wbcg));
	gnm_expr_entry_set_flags (
		state->left_entry,
		GNUM_EE_FULL_COL | GNUM_EE_SHEET_OPTIONAL,
		GNUM_EE_FULL_COL | GNUM_EE_SHEET_OPTIONAL);
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
		1, 2, 0, 2, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach (
		GTK_TABLE (order_table), state->icon_dr,
		1, 2, 0, 2, GTK_FILL, GTK_FILL, 0, 0);

	g_signal_connect (G_OBJECT (order_rd), "toggled", G_CALLBACK (display_order_icon), state);

	if (state->pi->print_grid_lines)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gridlines), TRUE);

	if (state->pi->print_even_if_only_styles)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (onlystyles), TRUE);

	if (state->pi->print_black_and_white)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (bw), TRUE);

	if (state->pi->print_titles)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (titles), TRUE);

	if (state->pi->print_order == PRINT_ORDER_DOWN_THEN_RIGHT)
		order = order_dr;
	else
		order = order_rd;

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (order), TRUE);
	display_order_icon (GTK_TOGGLE_BUTTON (order_rd), state);

	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (gnm_expr_entry_get_entry (state->area_entry)));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (gnm_expr_entry_get_entry (state->top_entry)));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (gnm_expr_entry_get_entry (state->left_entry)));
	comments_combo = GTK_COMBO (glade_xml_get_widget (state->gui,
							  "comments-combo"));
	gnumeric_combo_enters (GTK_WINDOW (state->dialog), comments_combo);

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
}

/**
 * This callback switches the orientation of the preview page.
 */
static void
orientation_changed (GtkToggleButton *landscape_bt, PrinterSetupState *state)
{
	if (gtk_toggle_button_get_active (landscape_bt))
		state->orientation = PRINT_ORIENT_HORIZONTAL;
	else
		state->orientation = PRINT_ORIENT_VERTICAL;

	canvas_update (state);
}

static void
paper_size_changed (GtkEntry *entry, PrinterSetupState *state)
{
	char const *text = gtk_entry_get_text (entry);
	GnomePrintPaper const *new_paper = gnome_print_paper_get_by_name (text);
	if (new_paper) {
		state->paper = new_paper;
		canvas_update (state);
	}
}

static void
do_setup_page (PrinterSetupState *state)
{
	PrintInformation *pi = state->pi;
	GtkWidget *image;
	GtkWidget *scale_percent_spin, *scale_width_spin, *scale_height_spin;
	GtkCombo *first_page_combo, *paper_size_combo;
	GtkTable *table;
	GladeXML *gui;
	const char *toggle;

	gui = state->gui;
	table = GTK_TABLE (glade_xml_get_widget (gui, "table-orient"));

	image = gnumeric_load_image ("orient-vertical.png");
	gtk_widget_show (image);
	gtk_table_attach_defaults (table, image, 0, 1, 0, 1);
	image = gnumeric_load_image ("orient-horizontal.png");
	gtk_widget_show (image);
	gtk_table_attach_defaults (table, image, 2, 3, 0, 1);

	/*
	 * Select the proper radio for orientation
	 */
	state->orientation = state->pi->orientation;
	if (pi->orientation == PRINT_ORIENT_VERTICAL)
		toggle = "vertical-radio";
	else
		toggle = "horizontal-radio";

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, toggle)), 1);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (gui, "horizontal-radio")),
		"toggled",
		G_CALLBACK (orientation_changed), state);

	/*
	 * Set the scale
	 */
	if (pi->scaling.type == PERCENTAGE)
		toggle = "scale-percent-radio";
	else
		toggle = "scale-size-fit-radio";

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, toggle)), TRUE);

	scale_percent_spin = glade_xml_get_widget (gui, "scale-percent-spin");
	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (scale_percent_spin), pi->scaling.percentage);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				      GTK_WIDGET (scale_percent_spin));

	scale_width_spin = glade_xml_get_widget (gui, "scale-width-spin");
	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (scale_width_spin), pi->scaling.dim.cols);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				      GTK_WIDGET (scale_width_spin));

	scale_height_spin = glade_xml_get_widget (gui, "scale-height-spin");
	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (scale_height_spin), pi->scaling.dim.rows);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				      GTK_WIDGET (scale_height_spin));
	/*
	 * Fill the paper sizes
	 */
	paper_size_combo =
		GTK_COMBO (glade_xml_get_widget (gui, "paper-size-combo"));
	g_signal_connect (G_OBJECT (paper_size_combo->entry),
		"changed",
		G_CALLBACK (paper_size_changed), state);
	{
		GList *sizes = gnome_print_paper_get_list ();
		GList *this_size, *names = NULL;;

		for (this_size = sizes; this_size; this_size = this_size->next) { 
			names = g_list_prepend (names, 
						((GnomePrintPaper *)this_size->data)->name);
		}
		names = g_list_reverse (names);
		gtk_combo_set_popdown_strings (paper_size_combo, names);
		g_list_free (names);
		gnome_print_paper_free_list (sizes);
	}
	gtk_combo_set_value_in_list (paper_size_combo, TRUE, FALSE);
	gnumeric_combo_enters (GTK_WINDOW (state->dialog), paper_size_combo);

	if (state->pi->paper == NULL)
		state->pi->paper = gnome_print_paper_get_default ();
	state->paper = state->pi->paper;

	gtk_entry_set_text (GTK_ENTRY (paper_size_combo->entry),
			    state->pi->paper->name);
	first_page_combo =
		GTK_COMBO (glade_xml_get_widget (gui, "first-page-combo"));
	gnumeric_combo_enters (GTK_WINDOW (state->dialog), first_page_combo);
}

static void
cb_do_print (GtkWidget *w, PrinterSetupState *state)
{
	WorkbookControlGUI *wbcg;
	Sheet *sheet;

	fetch_settings (state);
	wbcg = state->wbcg;
	sheet = state->sheet;
	gtk_widget_destroy (state->dialog);
	sheet_print (wbcg, sheet, FALSE, PRINT_ACTIVE_SHEET);
}

static void
cb_do_print_preview (GtkWidget *w, PrinterSetupState *state)
{
	fetch_settings (state);
	sheet_print (state->wbcg, state->sheet, TRUE, PRINT_ACTIVE_SHEET);
}

static void
cb_do_print_cancel (GtkWidget *w, PrinterSetupState *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_do_print_ok (GtkWidget *w, PrinterSetupState *state)
{
	/* Detach BEFORE we finish editing */
	wbcg_edit_detach_guru (state->wbcg);
	wbcg_edit_finish (state->wbcg, TRUE);
	fetch_settings (state);
	print_info_save (state->pi);
	gtk_widget_destroy (state->dialog);
}

static void
cb_do_print_destroy (GtkWidget *button, PrinterSetupState *state)
{
	wbcg_edit_detach_guru (state->wbcg);
	wbcg_edit_finish (state->wbcg, FALSE);

	if (state->customize_header)
		gtk_widget_destroy (state->customize_header);

	if (state->customize_footer)
		gtk_widget_destroy (state->customize_footer);

	printer_setup_state_free (state);
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
	g_signal_connect (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_do_print_ok), state);
	w = glade_xml_get_widget (state->gui, "print");
	g_signal_connect (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_do_print), state);
	w = glade_xml_get_widget (state->gui, "preview");
	g_signal_connect (GTK_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_do_print_preview), state);
	w = glade_xml_get_widget (state->gui, "cancel");
	g_signal_connect (GTK_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_do_print_cancel), state);

	/* Hide non-functional buttons for now */
	w = glade_xml_get_widget (state->gui, "options");
	gtk_widget_hide (w);

	wbcg_edit_attach_guru (state->wbcg, state->dialog);

	/* Lifecyle management */
	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (cb_do_print_destroy), state);

}

static PrinterSetupState *
printer_setup_state_new (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	PrinterSetupState *state;
	GladeXML *gui;

	gui = gnumeric_glade_xml_new (wbcg, "print.glade");
        if (gui == NULL)
                return NULL;

	state = g_new0 (PrinterSetupState, 1);
	state->wbcg  = wbcg;
	state->sheet = sheet;
	state->gui   = gui;
	state->pi    = sheet->print_info;
	state->current_paper = NULL;
	state->current_orientation = -1;
	state->customize_header = NULL;
	state->customize_footer = NULL;

	do_setup_main_dialog (state);
	do_setup_margin (state);
	do_setup_hf (state);
	do_setup_page_info (state);
	do_setup_page (state);

	return state;
}

static void
printer_setup_state_free (PrinterSetupState *state)
{
	g_object_unref (G_OBJECT (state->gui));

	print_hf_free (state->header);
	print_hf_free (state->footer);
	g_free (state->pi_header);
	g_free (state->pi_footer);
	g_list_free (state->conversion_listeners);
	state->conversion_listeners = NULL;
	g_free (state);
}

static void
do_fetch_page (PrinterSetupState *state)
{
	GtkWidget *w;
	GladeXML *gui = state->gui;
	char const *t;

	w = glade_xml_get_widget (gui, "vertical-radio");
	if (GTK_TOGGLE_BUTTON (w)->active)
		state->pi->orientation = PRINT_ORIENT_VERTICAL;
	else
		state->pi->orientation = PRINT_ORIENT_HORIZONTAL;

	w = glade_xml_get_widget (gui, "scale-percent-radio");
	if (GTK_TOGGLE_BUTTON (w)->active)
		state->pi->scaling.type = PERCENTAGE;
	else
		state->pi->scaling.type = SIZE_FIT;

	w = glade_xml_get_widget (gui, "scale-percent-spin");
	state->pi->scaling.percentage = GTK_SPIN_BUTTON (w)->adjustment->value;

	w = glade_xml_get_widget (gui, "scale-width-spin");
	state->pi->scaling.dim.cols = GTK_SPIN_BUTTON (w)->adjustment->value;

	w = glade_xml_get_widget (gui, "scale-height-spin");
	state->pi->scaling.dim.rows = GTK_SPIN_BUTTON (w)->adjustment->value;

	w = glade_xml_get_widget (gui, "paper-size-combo");
	t = gtk_entry_get_text (GTK_ENTRY (GTK_COMBO (w)->entry));

	if (gnome_print_paper_get_by_name (t) != NULL)
		state->pi->paper = gnome_print_paper_get_by_name (t);
}

static PrintUnit
unit_info_to_print_unit (UnitInfo *ui)
{
	PrintUnit u;

	u.desired_display = ui->unit;
        u.points = unit_convert (ui->value, ui->unit, UNIT_POINTS);

	return u;
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
	PrintMargins *m = &state->pi->margins;
	GtkToggleButton *t;

	m->header = unit_info_to_print_unit (&state->margins.top);
	m->footer = unit_info_to_print_unit (&state->margins.bottom);
	m->left   = unit_info_to_print_unit (&state->margins.left);
	m->right  = unit_info_to_print_unit (&state->margins.right);
	m->top    = unit_info_to_print_unit (&state->margins.header);
	m->bottom = unit_info_to_print_unit (&state->margins.footer);

	m->top.points    += m->header.points;
	m->bottom.points += m->footer.points;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "center-horizontal"));
	state->pi->center_horizontally = t->active;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "center-vertical"));
	state->pi->center_vertically = t->active;
}

static void
do_fetch_hf (PrinterSetupState *state)
{
	print_hf_free (state->pi->header);
	print_hf_free (state->pi->footer);

	state->pi->header = print_hf_copy (state->header);
	state->pi->footer = print_hf_copy (state->footer);
}

static void
do_fetch_page_info (PrinterSetupState *state)
{
	GtkToggleButton *t;
	PrintInformation *pi = state->pi;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "check-grid-lines"));
	state->pi->print_grid_lines = t->active;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "check-only-styles"));
	state->pi->print_even_if_only_styles = t->active;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "check-black-white"));
	state->pi->print_black_and_white = t->active;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "check-print-titles"));
	state->pi->print_titles = t->active;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "radio-order-right"));
	state->pi->print_order = t->active ? PRINT_ORDER_RIGHT_THEN_DOWN : PRINT_ORDER_DOWN_THEN_RIGHT;

/* FIXME: parsing should be done by the expr-entry */
	pi->repeat_top.use = parse_range (
		gnm_expr_entry_get_text (state->top_entry),
		&pi->repeat_top.range);

	pi->repeat_left.use = parse_range (
		gnm_expr_entry_get_text (state->left_entry),
		&pi->repeat_left.range);
}

static void
fetch_settings (PrinterSetupState *state)
{
	do_fetch_page (state);
	do_fetch_margins (state);
	do_fetch_hf (state);
	do_fetch_page_info (state);
}

void
dialog_printer_setup (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	PrinterSetupState *state;

	/* Only one guru per workbook. */
	if (wbcg_edit_has_guru (wbcg))
		return;

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, PRINTER_SETUP_KEY))
		return;

	state = printer_setup_state_new (wbcg, sheet);
	if (!state)
		return;

/* FIXME: Add correct helpfile address */
	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		"print-setup.html");
	gnumeric_keyed_dialog (
		wbcg, GTK_WINDOW (state->dialog), PRINTER_SETUP_KEY);
	gtk_widget_show (state->dialog);
}
