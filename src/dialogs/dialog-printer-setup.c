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
#include "gnumeric-sheet.h"
#include "dialogs.h"
#include "print-info.h"
#include "print.h"

#define PREVIEW_X 170
#define PREVIEW_Y 200
#define PREVIEW_MARGIN_X 20
#define PREVIEW_MARGIN_Y 20
#define PAGE_X (PREVIEW_X - PREVIEW_MARGIN_X)
#define PAGE_Y (PREVIEW_Y - PREVIEW_MARGIN_Y)

typedef struct {
	UnitName   unit;
	double     value;
	GtkSpinButton *spin;
	GtkAdjustment *adj;
} UnitInfo;

typedef struct {
	/* THe Canvas object */
	GtkWidget        *canvas;

	/* Objects in the Preview Canvas */
	GnomeCanvasItem *o_page, *o_page_shadow;
	GnomeCanvasItem *m_left, *m_right, *m_top, *m_bottom;
	GnomeCanvasItem *m_footer, m_header;

	/* Values for the scaling of the nice preview */
	int offset_x, offset_y;	/* For centering the small page preview */
	double scale;
} PreviewInfo;

typedef struct {
	Workbook         *workbook;
	GladeXML         *gui;
	PrintInformation *pi;
	GtkWidget        *dialog;

	struct {
		UnitInfo top, bottom;
		UnitInfo left, right;
		UnitInfo header, footer;
	} margins;

	const GnomePaper *paper;
	const GnomePaper *current_paper;

	PreviewInfo preview;

	GtkWidget *icon_rd;
	GtkWidget *icon_dr;
} dialog_print_info_t;

static void fetch_settings (dialog_print_info_t *dpi);

static double
unit_into_to_points (UnitInfo *ui)
{
	return unit_convert (ui->value, ui->unit, UNIT_POINTS);
}

static GtkWidget *
load_image (const char *name)
{
	GtkWidget *image;
	char *path;
	
	path = g_strconcat (GNUMERIC_ICONDIR "/", name, NULL);
	image = gnome_pixmap_new_from_file (path);
	g_free (path);

	if (image)
		gtk_widget_show (image);
	
	return image;
}

static void
preview_page_destroy (dialog_print_info_t *dpi)
{
	if (dpi->preview.o_page){
		gtk_object_unref (GTK_OBJECT (dpi->preview.o_page));
		gtk_object_unref (GTK_OBJECT (dpi->preview.o_page_shadow));
	}
}

static void
preview_page_create (dialog_print_info_t *dpi)
{
	GnomeCanvasGroup *group;
	double x1, y1, x2, y2;
	double width, height;
	PreviewInfo *pi = &dpi->preview;

	width = gnome_paper_pswidth (dpi->paper);
	height = gnome_paper_psheight (dpi->paper);

	if (width > height)
		pi->scale = PAGE_Y / height;
	else
		pi->scale = PAGE_X / width;

	pi->offset_x = (PREVIEW_X - (width * pi->scale)) / 2;
	pi->offset_y = (PREVIEW_Y - (height * pi->scale)) / 2;
	pi->offset_x = pi->offset_y = 0;
	printf ("Mas: %d %d, escale=%g\n", pi->offset_x, pi->offset_y, pi->scale);
	x1 = pi->offset_x + 0 * pi->scale;
	y1 = pi->offset_y + 0 * pi->scale;
	x2 = pi->offset_x + width * pi->scale;
	y2 = pi->offset_y + height * pi->scale;

	printf ("Valores: (%g %g) (%g %g)\n", x1, y1, x2, y2);
	group = gnome_canvas_root (GNOME_CANVAS (pi->canvas));
	pi->o_page_shadow = gnome_canvas_item_new (
		group, gnome_canvas_rect_get_type (),
		"x1",  	      	 (double) x1+2,
		"y1",  	      	 (double) y1+2,
		"x2",  	      	 (double) x2+2,
		"y2",         	 (double) y2+2,
		"fill_color",    "black",
		"outline_color", "black",
		"width_pixels",   1,
		NULL);
		
	pi->o_page = gnome_canvas_item_new (
		group, gnome_canvas_rect_get_type (),
		"x1",  	      	 (double) x1,
		"y1",  	      	 (double) y1,
		"x2",  	      	 (double) x2,
		"y2",         	 (double) y2,
		"fill_color",    "white",
		"outline_color", "black",
		"width_pixels",   1,
		NULL);
}

static void
canvas_update (dialog_print_info_t *dpi)
{
	if (dpi->current_paper != dpi->paper){
		preview_page_destroy (dpi);
		dpi->current_paper = dpi->paper;
		preview_page_create (dpi);
	}

}

static void
do_convert (UnitInfo *target, UnitName new_unit)
{
	if (target->unit == new_unit)
		return;

	target->value = unit_convert (target->value, target->unit, new_unit);
	target->unit = new_unit;

	gtk_spin_button_set_value (target->spin, target->value);
}

static void
convert_to_pt (GtkWidget *widget, UnitInfo *target)
{
	do_convert (target, UNIT_POINTS);
}

static void
convert_to_mm (GtkWidget *widget, UnitInfo *target)
{
	do_convert (target, UNIT_MILLIMITER);
}

static void
convert_to_cm (GtkWidget *widget, UnitInfo *target)
{
	do_convert (target, UNIT_CENTIMETER);
}

static void
convert_to_in (GtkWidget *widget, UnitInfo *target)
{
	do_convert (target, UNIT_INCH);
}

static void
add_unit (GtkWidget *menu, int i, void (*convert)(GtkWidget *, UnitInfo *), void *data)
{
	GtkWidget *item;

	item = gtk_menu_item_new_with_label (_(unit_name_get_short_name (i)));
	gtk_widget_show (item);
	gtk_menu_append (GTK_MENU (menu), item);

	gtk_signal_connect (
		GTK_OBJECT (item), "activate",
		GTK_SIGNAL_FUNC (convert), data);
}

static void
unit_changed (GtkSpinButton *spin_button, UnitInfo *target)
{
	
}

static GtkWidget *
unit_editor_new (UnitInfo *target, PrintUnit init)
{
	GtkWidget *box, *om, *menu;
	
	box = gtk_hbox_new (0, 0);

	target->unit = init.desired_display;
	target->value = unit_convert (init.points, UNIT_POINTS, init.desired_display);

	target->adj = GTK_ADJUSTMENT (gtk_adjustment_new (
		target->value,
		0.0, 1000.0, 0.1, 1.0, 1.0));
	target->spin = GTK_SPIN_BUTTON (gtk_spin_button_new (target->adj, 1, 1));
	gtk_signal_connect (
		GTK_OBJECT (target->spin), "changed",
		GTK_SIGNAL_FUNC (unit_changed), target);
	
	gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (target->spin), TRUE, TRUE, 0);
	om = gtk_option_menu_new ();
	gtk_box_pack_start (GTK_BOX (box), om, FALSE, FALSE, 0);
	gtk_widget_show_all (box);

	menu = gtk_menu_new ();

	add_unit (menu, UNIT_POINTS, convert_to_pt, target);
	add_unit (menu, UNIT_MILLIMITER, convert_to_mm, target);
	add_unit (menu, UNIT_CENTIMETER, convert_to_cm, target);
	add_unit (menu, UNIT_INCH, convert_to_in, target);

	gtk_menu_set_active (GTK_MENU (menu), target->unit);
	gtk_option_menu_set_menu (GTK_OPTION_MENU (om), menu);
	gtk_option_menu_set_history (GTK_OPTION_MENU (om), init.desired_display);

	return box;
}

static void
tattach (GtkTable *table, int x, int y, PrintUnit init, UnitInfo *target)
{
	GtkWidget *w;
	
	w = unit_editor_new (target, init);
	gtk_table_attach (
		table, w, x, x+1, y, y+1,
		GTK_FILL | GTK_EXPAND, 0, 0, 0);
}

static void
do_setup_margin (dialog_print_info_t *dpi)
{
	GtkTable *table;
	PrintMargins *pm = &dpi->pi->margins;

	dpi->preview.canvas = gnome_canvas_new ();
	gnome_canvas_set_scroll_region (
		GNOME_CANVAS (dpi->preview.canvas),
		0.0, 0.0, PREVIEW_X, PREVIEW_Y);
	gtk_widget_set_usize (dpi->preview.canvas, PREVIEW_X, PREVIEW_Y);
	gtk_widget_show (dpi->preview.canvas);
	
	table = GTK_TABLE (glade_xml_get_widget (dpi->gui, "margin-table"));

	tattach (table, 1, 1, pm->top,    &dpi->margins.top);
	tattach (table, 2, 1, pm->header, &dpi->margins.header);
	tattach (table, 0, 4, pm->left,   &dpi->margins.left);
	tattach (table, 2, 4, pm->right,  &dpi->margins.right);
	tattach (table, 1, 7, pm->bottom, &dpi->margins.bottom);
	tattach (table, 2, 7, pm->footer, &dpi->margins.footer);

	gtk_table_attach (table, dpi->preview.canvas,
			  1, 2, 3, 6, GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);
	
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
do_setup_hf (dialog_print_info_t *dpi)
{
	GtkEntry *header = GTK_ENTRY (glade_xml_get_widget (dpi->gui, "header-entry"));
	GtkEntry *footer = GTK_ENTRY (glade_xml_get_widget (dpi->gui, "footer-entry"));
	
	gtk_entry_set_text (header, dpi->pi->header->middle_format);
	gtk_entry_set_text (footer, dpi->pi->footer->middle_format);
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
	GtkWidget *order     = glade_xml_get_widget (dpi->gui, "radio-order-right");
	GtkWidget *table     = glade_xml_get_widget (dpi->gui, "page-order-table");

	dpi->icon_rd = load_image ("right-down.png");
	dpi->icon_dr = load_image ("down-right.png");
	
	gtk_widget_hide (dpi->icon_dr);
	gtk_widget_hide (dpi->icon_rd);
	
	gtk_table_attach (
		GTK_TABLE (table), dpi->icon_rd,
		1, 2, 0, 2, GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);
	gtk_table_attach (
		GTK_TABLE (table), dpi->icon_dr,
		1, 2, 0, 2, GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);

	gtk_signal_connect (GTK_OBJECT (order), "toggle", GTK_SIGNAL_FUNC(display_order_icon), dpi);
	
	if (dpi->pi->print_line_divisions)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (divisions), TRUE);

	if (dpi->pi->print_black_and_white)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (bw), TRUE);

	if (dpi->pi->print_titles)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (titles), TRUE);

	if (dpi->pi->print_order)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (order), TRUE);
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
	GtkCombo *combo;
	GtkTable *table;
	GladeXML *gui;
	char *toggle;

	gui = dpi->gui;
	table = GTK_TABLE (glade_xml_get_widget (gui, "table-orient"));
	
	image = load_image ("gnumeric/orient-vertical.png");
	gtk_table_attach_defaults (table, image, 0, 1, 0, 1);
	image = load_image ("gnumeric/orient-horizontal.png");
	gtk_table_attach_defaults (table, image, 2, 3, 0, 1);

	/*
	 * Select the proper radio for orientation
	 */
	if (pi->orientation == PRINT_ORIENT_VERTICAL)
		toggle = "vertical-radio";
	else
		toggle = "horizontal-radio";

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, toggle)), 1);

	/*
	 * Set the scale
	 */
	if (pi->scaling.type == PERCENTAGE)
		toggle = "scale-percent-radio";
	else
		toggle = "scale-size-fit-radio";

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, toggle)), TRUE);

	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (glade_xml_get_widget (
			gui, "scale-percent-spin")), pi->scaling.percentage);

	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (glade_xml_get_widget (
			gui, "scale-width-spin")), pi->scaling.dim.cols);
	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (glade_xml_get_widget (
			gui, "scale-height-spin")), pi->scaling.dim.rows);

	/*
	 * Fill the paper sizes
	 */
	combo = GTK_COMBO (glade_xml_get_widget (
		gui, "paper-size-combo"));
	gtk_combo_set_value_in_list (combo, TRUE, 0);
	gtk_combo_set_popdown_strings (combo, gnome_paper_name_list ());
	gtk_signal_connect (GTK_OBJECT (combo->entry), "changed",
			    paper_size_changed, dpi);

	if (dpi->pi->paper == NULL)
		dpi->pi->paper = gnome_paper_with_name (gnome_paper_name_default ());
	dpi->paper = dpi->pi->paper;
	
	gtk_entry_set_text (GTK_ENTRY (combo->entry), gnome_paper_name (dpi->pi->paper));
}

static void
do_print_cb (GtkWidget *w, dialog_print_info_t *dpi)
{
	fetch_settings (dpi);
	workbook_print (dpi->workbook);
}

static void
do_setup_main_dialog (dialog_print_info_t *dpi)
{
	GtkWidget *notebook;
	int i;
	
	/*
	 * Moves the whole thing into a GnomeDialog, needed until
	 * we get GnomeDialog support in Glade.
	 */
	dpi->dialog = gnome_dialog_new (
		_("Print setup"),
		GNOME_STOCK_BUTTON_OK,
		GNOME_STOCK_BUTTON_CANCEL,
		NULL);
	gtk_window_set_policy (GTK_WINDOW (dpi->dialog), FALSE, TRUE, TRUE);
		
	notebook = glade_xml_get_widget (dpi->gui, "print-setup-notebook");

	gtk_widget_reparent (notebook, GNOME_DIALOG (dpi->dialog)->vbox);

	gtk_widget_unref (glade_xml_get_widget (dpi->gui, "print-setup"));

	gtk_widget_queue_resize (notebook);

	for (i = 1; i < 5; i++){
		GtkWidget *w;
		char *s = g_strdup_printf ("print-%d", i);

		w = glade_xml_get_widget (dpi->gui, s);
		gtk_signal_connect (GTK_OBJECT (w), "clicked",
				    GTK_SIGNAL_FUNC (do_print_cb), dpi);
		g_free (s);
	}

	/*
	 * Hide non-functional buttons for now
	 */

	for (i = 1; i < 5; i++){
		char *preview = g_strdup_printf ("preview-%d", i);
		char *options = g_strdup_printf ("options-%d", i);
		GtkWidget *w;

		w = glade_xml_get_widget (dpi->gui, preview);
		gtk_widget_hide (w);
		w = glade_xml_get_widget (dpi->gui, options);
		gtk_widget_hide (w);

		g_free (preview);
		g_free (options);
	}
}

static dialog_print_info_t *
dialog_print_info_new (Workbook *wb)
{
	dialog_print_info_t *dpi;
	GladeXML *gui;
	
	gui = glade_xml_new (GNUMERIC_GLADEDIR "/print.glade", NULL);
	if (!gui){
		g_error ("Could not load print.glade");
		return NULL;
	}

	dpi = g_new (dialog_print_info_t, 1);
	dpi->workbook = wb;
	dpi->gui = gui;
	dpi->pi = wb->print_info;
	
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

static void
do_fetch_margins (dialog_print_info_t *dpi)
{
	PrintMargins *m = &dpi->pi->margins;
	GtkToggleButton *t;
	
	m->top = unit_info_to_print_unit (&dpi->margins.top);
	m->bottom = unit_info_to_print_unit (&dpi->margins.bottom);
	m->left = unit_info_to_print_unit (&dpi->margins.left);
	m->right = unit_info_to_print_unit (&dpi->margins.right);
	m->header = unit_info_to_print_unit (&dpi->margins.header);
	m->footer = unit_info_to_print_unit (&dpi->margins.footer);

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "center-horizontal"));
	dpi->pi->center_horizontally = t->active;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "center-vertical"));
	dpi->pi->center_vertically = t->active;
}

static void
do_fetch_hf (dialog_print_info_t *dpi)
{
	g_free (dpi->pi->header->middle_format);
	g_free (dpi->pi->footer->middle_format);

	dpi->pi->header->middle_format = g_strdup (gtk_entry_get_text (
		GTK_ENTRY (glade_xml_get_widget (dpi->gui, "header-entry"))));

	dpi->pi->footer->middle_format = g_strdup (gtk_entry_get_text (
		GTK_ENTRY (glade_xml_get_widget (dpi->gui, "footer-entry"))));
}

static void
do_fetch_page_info (dialog_print_info_t *dpi)
 {
	GtkToggleButton *t;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "check-print-divisions"));
	dpi->pi->print_line_divisions = t->active;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "check-black-white"));
	dpi->pi->print_black_and_white = t->active;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "check-print-titles"));
	dpi->pi->print_titles = t->active;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "radio-order-right"));
	dpi->pi->print_order = t->active;
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

	g_free (dpi);
}

void
dialog_printer_setup (Workbook *wb)
{
	dialog_print_info_t *dpi;
	int v;

	dpi = dialog_print_info_new (wb);
	if (!dpi)
		return;

	do_setup_main_dialog (dpi);
	do_setup_margin (dpi);
	do_setup_hf (dpi);
	do_setup_page_info (dpi);
	do_setup_page (dpi);

	v = gnome_dialog_run (GNOME_DIALOG (dpi->dialog));

	if (v == 0){
		fetch_settings (dpi);
		print_info_save (dpi->pi);
	}

	if (v != -1)
		gnome_dialog_close (GNOME_DIALOG (dpi->dialog));
	
	dialog_print_info_destroy (dpi);
}





