/*
 * dialog-printer-setup.c: Printer setup dialog box
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "print-info.h"
#include "print.h"
#include "ranges.h"
#include "workbook.h"
#include "utils-dialog.h"

#define PREVIEW_X 170
#define PREVIEW_Y 170
#define PREVIEW_MARGIN_X 20
#define PREVIEW_MARGIN_Y 20
#define PAGE_X (PREVIEW_X - PREVIEW_MARGIN_X)
#define PAGE_Y (PREVIEW_Y - PREVIEW_MARGIN_Y)

#define MARGIN_COLOR_DEFAULT "gray"
#define MARGIN_COLOR_ACTIVE "black"

#define EPSILON 1e-5		/* Same as in gtkspinbutton.c */

typedef struct {
	/* The Canvas object */
	GtkWidget        *canvas;

	/* Objects in the Preview Canvas */
	GnomeCanvasItem  *group;

	/* Values for the scaling of the nice preview */
	int offset_x, offset_y;	/* For centering the small page preview */
	double scale;
} PreviewInfo;

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

	const GnomePaper *paper;
	const GnomePaper *current_paper;
	PrintOrientation orientation;
	PrintOrientation current_orientation;

	PreviewInfo preview;

	GtkWidget *icon_rd;
	GtkWidget *icon_dr;

	/*
	 * The header/footers formats
	 */
	PrintHF *header;
	PrintHF *footer;
} dialog_print_info_t;

typedef struct
{
    dialog_print_info_t *dpi;
    UnitInfo *target;
} UnitInfo_cbdata;

static void fetch_settings (dialog_print_info_t *dpi);

#if 0
static double
unit_into_to_points (UnitInfo *ui)
{
	return unit_convert (ui->value, ui->unit, UNIT_POINTS);
}
#endif

/*
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
		double shrink = MIN (-space_to_grow, adjustment->value);

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

/*
 * get_paper_width
 * @dpi  print info
 *
 * Return paper width in points, taking page orientation into account.
 */
static double
get_paper_pswidth (dialog_print_info_t *dpi)
{
	if (dpi->orientation == PRINT_ORIENT_VERTICAL)
		return gnome_paper_pswidth (dpi->paper);
	else
		return gnome_paper_psheight (dpi->paper);
}

/*
 * get_paper_psheight
 * @dpi  print info
 *
 * Return paper height in points, taking page orientation into account.
 */
static double
get_paper_psheight (dialog_print_info_t *dpi)
{
	if (dpi->orientation == PRINT_ORIENT_VERTICAL)
		return gnome_paper_psheight (dpi->paper);
	else
		return gnome_paper_pswidth (dpi->paper);
}

/*
 * get_printable_width
 * @dpi  print info
 * @unit unit
 *
 * Return page width minus margins in specified unit.
 */
static double
get_printable_width (dialog_print_info_t *dpi, UnitName unit)
{
	return unit_convert (get_paper_pswidth (dpi), UNIT_POINTS, unit)
		- unit_convert (dpi->margins.left.value,
				dpi->margins.left.unit, unit)
		- unit_convert (dpi->margins.right.value,
				dpi->margins.right.unit, unit);
}

/*
 * get_printable_height
 * @dpi  print info
 * @unit unit
 *
 * Return page height minus margins, header and footer in specified unit.
 */
static double
get_printable_height (dialog_print_info_t *dpi, UnitName unit)
{
	return unit_convert (get_paper_psheight (dpi), UNIT_POINTS, unit)
		- unit_convert (dpi->margins.top.value,
				dpi->margins.top.unit, unit)
		- unit_convert (dpi->margins.bottom.value,
				dpi->margins.bottom.unit, unit)
		- unit_convert (dpi->margins.header.value,
				dpi->margins.header.unit, unit)
		- unit_convert (dpi->margins.footer.value,
				dpi->margins.footer.unit, unit);
}

/*
 * set_horizontal_bounds
 * @dpi           print info
 * @margin_fixed  margin to remain unchanged
 * @unit          unit
 *
 * Set the upper bounds for left and right margins.
 * If margin_fixed is one of those margins, it kept unchanged. This is to
 * avoid the possibility of an endless loop.
 */
static void set_horizontal_bounds (dialog_print_info_t *dpi,
				   MarginOrientation margin_fixed,
				   UnitName unit)
{
	double printable_width = get_printable_width (dpi, unit);

	if (margin_fixed != MARGIN_LEFT)
		printable_width
			= spin_button_set_bound (dpi->margins.left.spin,
						 printable_width,
						 unit,
						 dpi->margins.left.unit);

	if (margin_fixed != MARGIN_RIGHT)
		printable_width
			= spin_button_set_bound (dpi->margins.right.spin,
						 printable_width,
						 unit,
						 dpi->margins.right.unit);
}

/*
 * set_vertical_bounds
 * @dpi           print info
 * @margin_fixed  margin to remain unchanged
 * @unit          unit
 *
 * Set the upper bounds for top/bottom margins, headers and footers.
 * If margin_fixed is one of those margins, it kept unchanged. This is to
 * avoid the possibility of an endless loop.
 */
static void set_vertical_bounds (dialog_print_info_t *dpi,
				 MarginOrientation margin_fixed,
				 UnitName unit)
{
	double printable_height = get_printable_height (dpi, unit);

	if (margin_fixed != MARGIN_TOP)
		printable_height
			= spin_button_set_bound (dpi->margins.top.spin,
						 printable_height,
						 unit,
						 dpi->margins.top.unit);

	if (margin_fixed != MARGIN_BOTTOM)
		printable_height
			= spin_button_set_bound (dpi->margins.bottom.spin,
						 printable_height,
						 unit,
						 dpi->margins.bottom.unit);

	if (margin_fixed != MARGIN_HEADER)
		printable_height
			= spin_button_set_bound (dpi->margins.header.spin,
						 printable_height,
						 unit,
						 dpi->margins.header.unit);

	if (margin_fixed != MARGIN_FOOTER)
		printable_height
			= spin_button_set_bound (dpi->margins.footer.spin,
						 printable_height,
						 unit,
						 dpi->margins.footer.unit);
}

static void
preview_page_destroy (dialog_print_info_t *dpi)
{
	if (dpi->preview.group) {
		gtk_object_destroy (GTK_OBJECT (dpi->preview.group));

		dpi->preview.group = NULL;
	}
}

static void
move_line (GnomeCanvasItem *item,
	   double x1, double y1,
	   double x2, double y2)
{
	GnomeCanvasPoints *points;

	points = gnome_canvas_points_new (2);
	points->coords [0] = x1;
	points->coords [1] = y1;
	points->coords [2] = x2;
	points->coords [3] = y2;

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
	points->coords [0] = x1;
	points->coords [1] = y1;
	points->coords [2] = x2;
	points->coords [3] = y2;

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
draw_margin (UnitInfo *uinfo,
	     dialog_print_info_t *dpi)
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
		draw_margin (&dpi->margins.header, dpi);
		break;
	case MARGIN_BOTTOM:
		y2 -= uinfo->pi->scale * val;
		if (y2 < y1)
			y2 = y1;
		else
			y1 = y2;
		draw_margin (&dpi->margins.footer, dpi);
		break;
	case MARGIN_HEADER:
		y1 += (uinfo->pi->scale * unit_convert (dpi->margins.top.value,
							dpi->margins.top.unit,
							UNIT_POINTS)
		       + uinfo->pi->scale * val);
		y2 = y1;
		break;
	case MARGIN_FOOTER:
		y2 -= (uinfo->pi->scale * unit_convert (dpi->margins.bottom.value,
							dpi->margins.bottom.unit,
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
create_margin (dialog_print_info_t *dpi,
	       UnitInfo *uinfo,
	       MarginOrientation orientation,
	       double x1, double y1,
	       double x2, double y2)
{
	GnomeCanvasGroup *g = GNOME_CANVAS_GROUP (dpi->preview.group);

	uinfo->pi = &dpi->preview;
	uinfo->line = make_line (g, x1 + 8, y1, x1 + 8, y2);
	uinfo->orientation = orientation;
	uinfo->bound_x1 = x1;
	uinfo->bound_y1 = y1;
	uinfo->bound_x2 = x2;
	uinfo->bound_y2 = y2;

	draw_margin (uinfo, dpi);
}

static void
draw_margins (dialog_print_info_t *dpi, double x1, double y1, double x2, double y2)
{
	/* Headers & footers */
	create_margin (dpi, &dpi->margins.header, MARGIN_HEADER,
		       x1, y1, x2, y2);
	create_margin (dpi, &dpi->margins.footer, MARGIN_FOOTER,
		       x1, y1, x2, y2);

	/* Margins */
	create_margin (dpi, &dpi->margins.left, MARGIN_LEFT,
		       x1, y1, x2, y2);
	create_margin (dpi, &dpi->margins.right, MARGIN_RIGHT,
		       x1, y1, x2, y2);
	create_margin (dpi, &dpi->margins.top, MARGIN_TOP,
		       x1, y1, x2, y2);
	create_margin (dpi, &dpi->margins.bottom, MARGIN_BOTTOM,
		       x1, y1, x2, y2);
}

static void
preview_page_create (dialog_print_info_t *dpi)
{
	double x1, y1, x2, y2;
	double width, height;
	PreviewInfo *pi = &dpi->preview;

	width  = get_paper_pswidth  (dpi);
	height = get_paper_psheight (dpi);

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

	gnome_canvas_item_new (
		GNOME_CANVAS_GROUP (pi->group),
		gnome_canvas_rect_get_type (),
		"x1",  	      	 (double) x1+2,
		"y1",  	      	 (double) y1+2,
		"x2",  	      	 (double) x2+2,
		"y2",         	 (double) y2+2,
		"fill_color",    "black",
		"outline_color", "black",
		"width_pixels",   1,
		NULL);

	gnome_canvas_item_new (
		GNOME_CANVAS_GROUP (pi->group),
		gnome_canvas_rect_get_type (),
		"x1",  	      	 (double) x1,
		"y1",  	      	 (double) y1,
		"x2",  	      	 (double) x2,
		"y2",         	 (double) y2,
		"fill_color",    "white",
		"outline_color", "black",
		"width_pixels",   1,
		NULL);

	draw_margins (dpi, x1, y1, x2, y2);
}

static void
canvas_update (dialog_print_info_t *dpi)
{
	if (dpi->current_paper != dpi->paper ||
	    dpi->current_orientation != dpi->orientation) {
		preview_page_destroy (dpi);
		dpi->current_paper = dpi->paper;
		dpi->current_orientation = dpi->orientation;
		preview_page_create (dpi);
		/* FIXME: Introduce dpi->displayed_unit? */
		set_horizontal_bounds (dpi, MARGIN_NONE,
				       dpi->margins.top.unit);
		set_vertical_bounds (dpi, MARGIN_NONE,
				     dpi->margins.top.unit);
	}
}

/*
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
	dialog_print_info_t *dpi;
	GList *l;

	dpi = gtk_object_get_data (GTK_OBJECT (item), "dialog-print-info");
	g_return_if_fail (dpi != NULL);

	for (l = dpi->conversion_listeners; l; l = l->next)
		(convert) ((UnitInfo *) l->data);
}

static void
add_unit (GtkWidget *menu, int i, dialog_print_info_t *dpi,
	  void (*convert)(UnitInfo *))
{
	GtkWidget *item;

	item = gtk_menu_item_new_with_label (unit_name_get_short_name (i, TRUE));
	gtk_widget_show (item);
	gtk_menu_append (GTK_MENU (menu), item);

	gtk_object_set_data (GTK_OBJECT (item),
			     "dialog-print-info", (gpointer) dpi);
	gtk_signal_connect (
		GTK_OBJECT (item), "activate",
		GTK_SIGNAL_FUNC (listeners_convert), (gpointer) convert);
}


static void
unit_changed (GtkSpinButton *spin_button, UnitInfo_cbdata *data)
{
	data->target->value = data->target->adj->value;

	switch (data->target->orientation) {
	case MARGIN_LEFT:
	case MARGIN_RIGHT:
		set_horizontal_bounds (data->dpi,
				       data->target->orientation,
				       data->target->unit);
		break;
	default:
		set_vertical_bounds (data->dpi,
				     data->target->orientation,
				     data->target->unit);
	}

	/* Adjust the display to the current values */
	draw_margin (data->target, data->dpi);
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
unit_editor_configure (UnitInfo *target, dialog_print_info_t *dpi,
		       char *spin_name, double init_points, UnitName unit)
{
	GtkSpinButton *spin;
	UnitInfo_cbdata *cbdata;

	spin = GTK_SPIN_BUTTON (glade_xml_get_widget (dpi->gui, spin_name));

	target->unit = unit;
	target->value = unit_convert (init_points, UNIT_POINTS, target->unit);

	target->adj = GTK_ADJUSTMENT (gtk_adjustment_new (
		target->value,
		0.0, 1000.0, 0.5, 1.0, 1.0));
	target->spin = spin;
	gtk_spin_button_configure (spin, target->adj, 1, 1);
	gnome_dialog_editable_enters (GNOME_DIALOG (dpi->dialog),
				      GTK_EDITABLE (spin));
	gtk_widget_set_usize (GTK_WIDGET (target->spin), 60, 0);
	spin_button_adapt_to_unit (target->spin, unit);

	cbdata = g_new (UnitInfo_cbdata, 1);
	cbdata->dpi = dpi;
	cbdata->target = target;
	gtk_signal_connect (
		GTK_OBJECT (target->spin), "focus_in_event",
		unit_activated, cbdata);
	gtk_signal_connect (
		GTK_OBJECT (target->spin), "focus_out_event",
		unit_deactivated, cbdata);
	gtk_signal_connect_full (
		GTK_OBJECT (target->spin), "changed",
		GTK_SIGNAL_FUNC (unit_changed),
		NULL,
		cbdata,
		(GtkDestroyNotify)g_free,
		FALSE, FALSE);
	dpi->conversion_listeners
		= g_list_append (dpi->conversion_listeners, (gpointer) target);
}

/*
 * Each margin is stored with a unit. We use the top margin unit for display
 * and ignore desired display for the others.
 *
 * Header and footer are stored with Excel semantics, but displayed with
 * more natural semantics. In Excel, both top margin and header are measured
 * from top of sheet. The Gnumeric user interface presents header as the
 * band between top margin and the print area. Bottom margin and footer are
 * handled likewise.
 */
static void
do_setup_margin (dialog_print_info_t *dpi)
{
	GtkWidget *option_menu, *menu;
	GtkBox *container;
	PrintMargins *pm;
	UnitName displayed_unit;

	g_return_if_fail (dpi && dpi->pi);

	pm = &dpi->pi->margins;
	displayed_unit = pm->top.desired_display;

	dpi->preview.canvas = gnome_canvas_new ();
	gnome_canvas_set_scroll_region (
		GNOME_CANVAS (dpi->preview.canvas),
		0.0, 0.0, PREVIEW_X, PREVIEW_Y);
	gtk_widget_set_usize (dpi->preview.canvas, PREVIEW_X, PREVIEW_Y);
	gtk_widget_show (dpi->preview.canvas);

	option_menu = glade_xml_get_widget (dpi->gui, "option-menu-units");
	/* Remove the menu built with glade
	   - it is there only to get the option menu properly sized */
	gtk_option_menu_remove_menu (GTK_OPTION_MENU (option_menu));
	menu = gtk_menu_new ();

	add_unit (menu, UNIT_POINTS, dpi, convert_to_pt);
	add_unit (menu, UNIT_MILLIMETER, dpi, convert_to_mm);
	add_unit (menu, UNIT_CENTIMETER, dpi, convert_to_cm);
	add_unit (menu, UNIT_INCH, dpi, convert_to_in);

	gtk_option_menu_set_menu (GTK_OPTION_MENU (option_menu), menu);
	gtk_option_menu_set_history (GTK_OPTION_MENU (option_menu),
				     displayed_unit);

	unit_editor_configure (&dpi->margins.top, dpi, "spin-top",
			       pm->top.points,
			       displayed_unit);
	unit_editor_configure (&dpi->margins.header, dpi, "spin-header",
			       MAX (pm->header.points - pm->top.points, 0),
			       displayed_unit);
	unit_editor_configure (&dpi->margins.left, dpi, "spin-left",
			       pm->left.points, displayed_unit);
	unit_editor_configure (&dpi->margins.right, dpi, "spin-right",
			       pm->right.points, displayed_unit);
	unit_editor_configure (&dpi->margins.bottom, dpi, "spin-bottom",
			       pm->bottom.points, displayed_unit);
	unit_editor_configure (&dpi->margins.footer, dpi, "spin-footer",
			       MAX (pm->footer.points - pm->bottom.points, 0),
			       displayed_unit);

	container = GTK_BOX (glade_xml_get_widget (dpi->gui,
						   "container-margin-page"));
	gtk_box_pack_start (container, dpi->preview.canvas, TRUE, TRUE, 0);

	if (dpi->pi->center_vertically)
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (
				glade_xml_get_widget (dpi->gui, "center-vertical")),
			TRUE);

	if (dpi->pi->center_horizontally)
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (
				glade_xml_get_widget (dpi->gui, "center-horizontal")),
			TRUE);
}

static void
header_changed (GtkObject *object, dialog_print_info_t *dpi)
{
	PrintHF *format = gtk_object_get_user_data (object);

	print_hf_free (dpi->header);
	dpi->header = print_hf_copy (format);
}

static void
footer_changed (GtkObject *object, dialog_print_info_t *dpi)
{
	PrintHF *format = gtk_object_get_user_data (object);

	print_hf_free (dpi->footer);
	dpi->footer = print_hf_copy (format);
}

/*
 * Fills one of the GtkCombos for headers or footers with the list
 * of existing header/footer formats
 */
static void
fill_hf (dialog_print_info_t *dpi, GtkOptionMenu *om, GtkSignalFunc callback, PrintHF *select)
{
	GList *l;
	HFRenderInfo *hfi;
	GtkWidget *menu;
	int i, idx = 0;

	hfi = hf_render_info_new ();
	hfi->page = 1;
	hfi->pages = 1;

	menu = gtk_menu_new ();

	for (i = 0, l = hf_formats; l; l = l->next, i++) {
		GtkWidget *li;
		PrintHF *format = l->data;
		char *left, *middle, *right;
		char *res;

		if (print_hf_same (format, select))
			idx = i;

		left   = hf_format_render (format->left_format, hfi, HF_RENDER_PRINT);
		middle = hf_format_render (format->middle_format, hfi, HF_RENDER_PRINT);
		right  = hf_format_render (format->right_format, hfi, HF_RENDER_PRINT);

		res = g_strdup_printf (
			"%s%s%s%s%s",
			left, *left ? "," : "",
			right, *right ? "," : "",
			middle);

		li = gtk_menu_item_new_with_label (res);
		gtk_widget_show (li);
		gtk_container_add (GTK_CONTAINER (menu), li);
		gtk_object_set_user_data (GTK_OBJECT (li), format);
		gtk_signal_connect (GTK_OBJECT (li), "activate", callback, dpi);

		g_free (res);
		g_free (left);
		g_free (middle);
		g_free (right);
	}
	gtk_option_menu_set_menu (om, menu);
	gtk_option_menu_set_history (om, idx);

	hf_render_info_destroy (hfi);
}

static void
text_insert (GtkText *text_widget, const char *text)
{
	int len = strlen (text);
	gint pos = 0;

	gtk_editable_insert_text (GTK_EDITABLE (text_widget), text, len, &pos);
}

static char *
text_get (GtkText *text_widget)
{
	return gtk_editable_get_chars (GTK_EDITABLE (text_widget), 0, -1);
}

static PrintHF *
do_hf_config (const char *title, PrintHF **config, WorkbookControlGUI *wbcg)
{
	GladeXML *gui;
	GtkText *left, *middle, *right;
	GtkWidget *dialog;
	PrintHF *ret = NULL;
	int v;

	gui = gnumeric_glade_xml_new (wbcg, "hf-config.glade");
        if (gui == NULL)
                return NULL;

	left   = GTK_TEXT (glade_xml_get_widget (gui, "left-format"));
	middle = GTK_TEXT (glade_xml_get_widget (gui, "center-format"));
	right  = GTK_TEXT (glade_xml_get_widget (gui, "right-format"));
	dialog = glade_xml_get_widget (gui, "hf-config");

	text_insert (left, (*config)->left_format);
	text_insert (middle, (*config)->middle_format);
	text_insert (right, (*config)->right_format);

	v = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));

	if (v == 0) {
		char *left_format, *right_format, *middle_format;

		left_format   = text_get (left);
		middle_format = text_get (middle);
		right_format  = text_get (right);

		print_hf_free (*config);
		*config = print_hf_new (left_format, middle_format, right_format);

		g_free (left_format);
		g_free (middle_format);
		g_free (right_format);

		ret = print_hf_register (*config);
	}

	if (v != -1)
		gtk_object_destroy (GTK_OBJECT (dialog));

	gtk_object_unref (GTK_OBJECT (gui));

	return ret;
}

static void
do_setup_hf_menus (dialog_print_info_t *dpi, PrintHF *header_sel, PrintHF *footer_sel)
{
	GtkOptionMenu *header = GTK_OPTION_MENU (glade_xml_get_widget (dpi->gui, "option-menu-header"));
	GtkOptionMenu *footer = GTK_OPTION_MENU (glade_xml_get_widget (dpi->gui, "option-menu-footer"));

	if (header_sel)
		fill_hf (dpi, header, GTK_SIGNAL_FUNC (header_changed), header_sel);
	if (footer_sel)
		fill_hf (dpi, footer, GTK_SIGNAL_FUNC (footer_changed), footer_sel);
}

static void
do_header_config (GtkWidget *button, dialog_print_info_t *dpi)
{
	PrintHF *hf;

	hf = do_hf_config (_("Custom header configuration"),
			   &dpi->header, dpi->wbcg);

	if (hf)
		do_setup_hf_menus (dpi, hf, NULL);
}

static void
do_footer_config (GtkWidget *button, dialog_print_info_t *dpi)
{
	PrintHF *hf;

	hf = do_hf_config (_("Custom footer configuration"),
			   &dpi->footer, dpi->wbcg);

	if (hf)
		do_setup_hf_menus (dpi, NULL, hf);
}

static void
do_setup_hf (dialog_print_info_t *dpi)
{
	dpi->header = print_hf_copy (dpi->pi->header ? dpi->pi->header :
				     hf_formats->data);
	dpi->footer = print_hf_copy (dpi->pi->footer ? dpi->pi->footer :
				     hf_formats->data);

	do_setup_hf_menus (dpi, dpi->header, dpi->footer);

	gtk_signal_connect (
		GTK_OBJECT (glade_xml_get_widget (dpi->gui, "configure-header")),
		"clicked", GTK_SIGNAL_FUNC (do_header_config), dpi);

	gtk_signal_connect (
		GTK_OBJECT (glade_xml_get_widget (dpi->gui, "configure-footer")),
		"clicked", GTK_SIGNAL_FUNC (do_footer_config), dpi);
}

static void
display_order_icon (GtkToggleButton *toggle, dialog_print_info_t *dpi)
{
	GtkWidget *show, *hide;

	if (toggle->active){
		show = dpi->icon_rd;
		hide = dpi->icon_dr;
	} else {
		hide = dpi->icon_rd;
		show = dpi->icon_dr;
	}

	gtk_widget_show (show);
	gtk_widget_hide (hide);
}

static void
do_setup_page_info (dialog_print_info_t *dpi)
{
	GtkWidget *divisions = glade_xml_get_widget (dpi->gui, "check-print-divisions");
	GtkWidget *bw        = glade_xml_get_widget (dpi->gui, "check-black-white");
	GtkWidget *titles    = glade_xml_get_widget (dpi->gui, "check-print-titles");
	GtkWidget *order_rd  = glade_xml_get_widget (dpi->gui, "radio-order-right");
	GtkWidget *order_dr  = glade_xml_get_widget (dpi->gui, "radio-order-down");
	GtkWidget *table     = glade_xml_get_widget (dpi->gui, "page-order-table");
	GtkEntry *entry_area, *entry_top, *entry_left;
	GtkCombo *comments_combo;
	GtkWidget *order;

	dpi->icon_rd = gnumeric_load_image ("right-down.png");
	dpi->icon_dr = gnumeric_load_image ("down-right.png");

	gtk_widget_hide (dpi->icon_dr);
	gtk_widget_hide (dpi->icon_rd);

	gtk_table_attach (
		GTK_TABLE (table), dpi->icon_rd,
		1, 2, 0, 2, GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);
	gtk_table_attach (
		GTK_TABLE (table), dpi->icon_dr,
		1, 2, 0, 2, GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);

	gtk_signal_connect (GTK_OBJECT (order_rd), "toggled", GTK_SIGNAL_FUNC (display_order_icon), dpi);

	if (dpi->pi->print_line_divisions)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (divisions), TRUE);

	if (dpi->pi->print_black_and_white)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (bw), TRUE);

	if (dpi->pi->print_titles)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (titles), TRUE);

	if (dpi->pi->print_order == PRINT_ORDER_DOWN_THEN_RIGHT)
		order = order_dr;
	else
		order = order_rd;

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (order), TRUE);
	display_order_icon (GTK_TOGGLE_BUTTON (order_rd), dpi);

	entry_area = GTK_ENTRY (glade_xml_get_widget (dpi->gui,
						      "print-area-entry"));
	entry_top  = GTK_ENTRY (glade_xml_get_widget (dpi->gui,
						      "repeat-rows-entry"));
	entry_left = GTK_ENTRY (glade_xml_get_widget (dpi->gui,
						      "repeat-cols-entry"));
	gnome_dialog_editable_enters (GNOME_DIALOG (dpi->dialog),
				      GTK_EDITABLE (entry_area));
	gnome_dialog_editable_enters (GNOME_DIALOG (dpi->dialog),
				      GTK_EDITABLE (entry_top));
	gnome_dialog_editable_enters (GNOME_DIALOG (dpi->dialog),
				      GTK_EDITABLE (entry_left));
	comments_combo = GTK_COMBO (glade_xml_get_widget (dpi->gui,
							  "comments-combo"));
	gnumeric_combo_enters (GTK_WINDOW (dpi->dialog), comments_combo);

	if (dpi->pi->repeat_top.use){
		char *s;

		s = value_cellrange_get_as_string ((Value *)&dpi->pi->repeat_top.range, FALSE);
		gtk_entry_set_text (entry_top, s);
		g_free (s);
	}

	if (dpi->pi->repeat_left.use){
		char *s;

		s = value_cellrange_get_as_string ((Value *)&dpi->pi->repeat_left.range, FALSE);
		gtk_entry_set_text (entry_left, s);
		g_free (s);
	}
}

/*
 * This callback switches the orientation of the preview page.
 */
static void
orientation_changed (GtkToggleButton *landscape_bt, dialog_print_info_t *dpi)
{
	if (gtk_toggle_button_get_active (landscape_bt))
		dpi->orientation = PRINT_ORIENT_HORIZONTAL;
	else
		dpi->orientation = PRINT_ORIENT_VERTICAL;

	canvas_update (dpi);
}

static void
paper_size_changed (GtkEntry *entry, dialog_print_info_t *dpi)
{
	char *text;

	text = gtk_entry_get_text (entry);

	dpi->paper = gnome_paper_with_name (text);
	canvas_update (dpi);
}

static void
do_setup_page (dialog_print_info_t *dpi)
{
	PrintInformation *pi = dpi->pi;
	GtkWidget *image;
	GtkWidget *scale_percent_spin, *scale_width_spin, *scale_height_spin;
	GtkCombo *first_page_combo, *paper_size_combo;
	GtkTable *table;
	GladeXML *gui;
	char *toggle;

	gui = dpi->gui;
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
	dpi->orientation = dpi->pi->orientation;
	if (pi->orientation == PRINT_ORIENT_VERTICAL)
		toggle = "vertical-radio";
	else
		toggle = "horizontal-radio";

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, toggle)), 1);
	gtk_signal_connect (
		GTK_OBJECT (glade_xml_get_widget (gui, "horizontal-radio")),
		"toggled", GTK_SIGNAL_FUNC (orientation_changed), dpi);

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
	gnome_dialog_editable_enters (GNOME_DIALOG (dpi->dialog),
				      GTK_EDITABLE (scale_percent_spin));

	scale_width_spin = glade_xml_get_widget (gui, "scale-width-spin");
	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (scale_width_spin), pi->scaling.dim.cols);
	gnome_dialog_editable_enters (GNOME_DIALOG (dpi->dialog),
				      GTK_EDITABLE (scale_width_spin));

	scale_height_spin = glade_xml_get_widget (gui, "scale-height-spin");
	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (scale_height_spin), pi->scaling.dim.rows);
	gnome_dialog_editable_enters (GNOME_DIALOG (dpi->dialog),
				      GTK_EDITABLE (scale_height_spin));
	/*
	 * Fill the paper sizes
	 */
	paper_size_combo =
		GTK_COMBO (glade_xml_get_widget (gui, "paper-size-combo"));
	gtk_combo_set_value_in_list (paper_size_combo, TRUE, 0);
	gtk_combo_set_popdown_strings (paper_size_combo,
				       gnome_paper_name_list ());
	gtk_signal_connect (GTK_OBJECT (paper_size_combo->entry), "changed",
			    paper_size_changed, dpi);
	gnumeric_combo_enters (GTK_WINDOW (dpi->dialog), paper_size_combo);

	if (dpi->pi->paper == NULL)
		dpi->pi->paper = gnome_paper_with_name (gnome_paper_name_default ());
	dpi->paper = dpi->pi->paper;

	gtk_entry_set_text (GTK_ENTRY (paper_size_combo->entry),
			    gnome_paper_name (dpi->pi->paper));
	first_page_combo =
		GTK_COMBO (glade_xml_get_widget (gui, "first-page-combo"));
	gnumeric_combo_enters (GTK_WINDOW (dpi->dialog), first_page_combo);
}

static void
do_print_cb (GtkWidget *w, dialog_print_info_t *dpi)
{
	fetch_settings (dpi);
	sheet_print (dpi->wbcg, dpi->sheet, FALSE, PRINT_ACTIVE_SHEET);
}

static void
do_print_preview_cb (GtkWidget *w, dialog_print_info_t *dpi)
{
	fetch_settings (dpi);
	sheet_print (dpi->wbcg, dpi->sheet, TRUE, PRINT_ACTIVE_SHEET);

	/*
	 * We close the dialog here, because the dialog box
	 * has a grab and a main-loop which prohibits us from
	 * using the main preview window until this dialog
	 * is closed.
	 *
	 * If you want to change the sheet_print api to run its
	 * own main loop or anything like that, you can remove
	 * this, but you have to make sure that the print-preview
	 * window is funcional
	 */
	gnome_dialog_close (GNOME_DIALOG (dpi->dialog));
}

static void
do_setup_main_dialog (dialog_print_info_t *dpi)
{
#if 0
	int i;
#endif

	g_return_if_fail (dpi != NULL);
	g_return_if_fail (dpi->sheet != NULL);
	g_return_if_fail (dpi->wbcg != NULL);

	dpi->dialog = glade_xml_get_widget (dpi->gui, "print-setup");

#if 0
	for (i = 1; i < 5; i++) {
		GtkWidget *w;
		char *print = g_strdup_printf ("print-%d", i);
		char *preview = g_strdup_printf ("preview-%d", i);

		w = glade_xml_get_widget (dpi->gui, print);
		gtk_signal_connect (GTK_OBJECT (w), "clicked",
				    GTK_SIGNAL_FUNC (do_print_cb), dpi);
		w = glade_xml_get_widget (dpi->gui, preview);
		gtk_signal_connect (GTK_OBJECT (w), "clicked",
				    GTK_SIGNAL_FUNC (do_print_preview_cb), dpi);
		g_free (print);
		g_free (preview);
	}

	/*
	 * Hide non-functional buttons for now
	 */
	for (i = 1; i < 5; i++) {
		char *options = g_strdup_printf ("options-%d", i);
		GtkWidget *w;

		w = glade_xml_get_widget (dpi->gui, options);
		gtk_widget_hide (w);

		g_free (options);
	}
#else
	{
		GtkWidget *w;

		w = glade_xml_get_widget (dpi->gui, "print");
		gtk_signal_connect (GTK_OBJECT (w), "clicked",
				    GTK_SIGNAL_FUNC (do_print_cb), dpi);
		w = glade_xml_get_widget (dpi->gui, "preview");
		gtk_signal_connect (GTK_OBJECT (w), "clicked",
				    GTK_SIGNAL_FUNC
				    (do_print_preview_cb), dpi);

		/*
		 * Hide non-functional buttons for now
		 */
		w = glade_xml_get_widget (dpi->gui, "options");
		gtk_widget_hide (w);
	}


#endif
}

static dialog_print_info_t *
dialog_print_info_new (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	dialog_print_info_t *dpi;
	GladeXML *gui;

	gui = gnumeric_glade_xml_new (wbcg, "print.glade");
        if (gui == NULL)
                return NULL;

	dpi = g_new0 (dialog_print_info_t, 1);
	dpi->wbcg   = wbcg;
	dpi->sheet = sheet;
	dpi->gui   = gui;
	dpi->pi    = sheet->print_info;
	dpi->current_paper = NULL;
	dpi->current_orientation = -1;

	do_setup_main_dialog (dpi);
	do_setup_margin (dpi);
	do_setup_hf (dpi);
	do_setup_page_info (dpi);
	do_setup_page (dpi);

	return dpi;
}

static void
do_fetch_page (dialog_print_info_t *dpi)
{
	GtkWidget *w;
	GladeXML *gui = dpi->gui;
	char *t;

	w = glade_xml_get_widget (gui, "vertical-radio");
	if (GTK_TOGGLE_BUTTON (w)->active)
		dpi->pi->orientation = PRINT_ORIENT_VERTICAL;
	else
		dpi->pi->orientation = PRINT_ORIENT_HORIZONTAL;

	w = glade_xml_get_widget (gui, "scale-percent-radio");
	if (GTK_TOGGLE_BUTTON (w)->active)
		dpi->pi->scaling.type = PERCENTAGE;
	else
		dpi->pi->scaling.type = SIZE_FIT;

	w = glade_xml_get_widget (gui, "scale-percent-spin");
	dpi->pi->scaling.percentage = GTK_SPIN_BUTTON (w)->adjustment->value;

	w = glade_xml_get_widget (gui, "scale-width-spin");
	dpi->pi->scaling.dim.cols = GTK_SPIN_BUTTON (w)->adjustment->value;

	w = glade_xml_get_widget (gui, "scale-height-spin");
	dpi->pi->scaling.dim.rows = GTK_SPIN_BUTTON (w)->adjustment->value;

	w = glade_xml_get_widget (gui, "paper-size-combo");
	t = gtk_entry_get_text (GTK_ENTRY (GTK_COMBO (w)->entry));

	if (gnome_paper_with_name (t) != NULL)
		dpi->pi->paper = gnome_paper_with_name (t);
}

static PrintUnit
unit_info_to_print_unit (UnitInfo *ui)
{
	PrintUnit u;

	u.desired_display = ui->unit;
        u.points = unit_convert (ui->value, ui->unit, UNIT_POINTS);

	return u;
}

/*
 * Header and footer are stored with Excel semantics, but displayed with
 * more natural semantics. In Excel, both top margin and header are measured
 * from top of sheet. The Gnumeric user interface presents header as the
 * band between top margin and the print area. Bottom margin and footer are
 * handled likewise.  */
static void
do_fetch_margins (dialog_print_info_t *dpi)
{
	PrintMargins *m = &dpi->pi->margins;
	GtkToggleButton *t;

	m->top    = unit_info_to_print_unit (&dpi->margins.top);
	m->bottom = unit_info_to_print_unit (&dpi->margins.bottom);
	m->left   = unit_info_to_print_unit (&dpi->margins.left);
	m->right  = unit_info_to_print_unit (&dpi->margins.right);
	m->header = unit_info_to_print_unit (&dpi->margins.header);
	m->footer = unit_info_to_print_unit (&dpi->margins.footer);

	m->header.points += m->top.points;
	m->footer.points += m->bottom.points;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "center-horizontal"));
	dpi->pi->center_horizontally = t->active;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "center-vertical"));
	dpi->pi->center_vertically = t->active;
}

static void
do_fetch_hf (dialog_print_info_t *dpi)
{
	print_hf_free (dpi->pi->header);
	print_hf_free (dpi->pi->footer);

	dpi->pi->header = print_hf_copy (dpi->header);
	dpi->pi->footer = print_hf_copy (dpi->footer);
}

static void
do_fetch_page_info (dialog_print_info_t *dpi)
{
	GtkToggleButton *t;
	Value *top_range, *left_range;
	GtkEntry *entry_top, *entry_left;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "check-print-divisions"));
	dpi->pi->print_line_divisions = t->active;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "check-black-white"));
	dpi->pi->print_black_and_white = t->active;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "check-print-titles"));
	dpi->pi->print_titles = t->active;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "radio-order-right"));
	dpi->pi->print_order = t->active ? PRINT_ORDER_RIGHT_THEN_DOWN : PRINT_ORDER_DOWN_THEN_RIGHT;

	entry_top  = GTK_ENTRY (glade_xml_get_widget (dpi->gui,
						      "repeat-rows-entry"));
	entry_left = GTK_ENTRY (glade_xml_get_widget (dpi->gui,
						      "repeat-cols-entry"));

	top_range = range_parse (NULL, gtk_entry_get_text (entry_top), TRUE);
	dpi->pi->repeat_top.use = (top_range != NULL);
	if (dpi->pi->repeat_top.use) {
		dpi->pi->repeat_top.range = top_range->v_range;
		value_release (top_range);
	}

	left_range = range_parse (NULL, gtk_entry_get_text (entry_left), TRUE);
	dpi->pi->repeat_left.use = (left_range != NULL);
	if (dpi->pi->repeat_left.use) {
		dpi->pi->repeat_left.range = left_range->v_range;
		value_release (left_range);
	}
}

static void
fetch_settings (dialog_print_info_t *dpi)
{
	do_fetch_page (dpi);
	do_fetch_margins (dpi);
	do_fetch_hf (dpi);
	do_fetch_page_info (dpi);
}

static void
dialog_print_info_destroy (dialog_print_info_t *dpi)
{
	gtk_object_unref (GTK_OBJECT (dpi->gui));

	print_hf_free (dpi->header);
	print_hf_free (dpi->footer);
	g_list_free (dpi->conversion_listeners);
	g_free (dpi);
}

void
dialog_printer_setup (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	dialog_print_info_t *dpi;
	int v;

	dpi = dialog_print_info_new (wbcg, sheet);
	if (!dpi)
		return;

	v = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dpi->dialog));

	if (v == 0) {
		fetch_settings (dpi);
		print_info_save (dpi->pi);
	}

	if (v != -1)
		gnome_dialog_close (GNOME_DIALOG (dpi->dialog));

	dialog_print_info_destroy (dpi);
}
