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

#define PREVIEW_X 170
#define PREVIEW_Y 200

typedef struct {
	UnitName   unit;
	double     value;
	GtkSpinButton *spin;
	GtkAdjustment *adj;
} UnitInfo;

typedef struct {
	Workbook         *workbook;
	GladeXML         *gui;
	PrintInformation *pi;
	GtkWidget        *dialog;
	GtkWidget        *margin_preview;

	struct {
		UnitInfo top, bottom;
		UnitInfo left, right;
		UnitInfo header, footer;
	} margins;

	const GnomePaper *paper;
} dialog_print_info_t;

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
	
	path = gnome_unconditional_pixmap_file (name);
	image = gnome_pixmap_new_from_file (path);
	g_free (path);

	if (image)
		gtk_widget_show (image);
	
	return image;
}

static void
margin_preview_update (dialog_print_info_t *dpi)
{
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

	item = gtk_menu_item_new_with_label (unit_name_get_short_name (i));
	gtk_widget_show (item);
	gtk_menu_append (GTK_MENU (menu), item);

	gtk_signal_connect (
		GTK_OBJECT (item), "activate",
		GTK_SIGNAL_FUNC (convert), data);
}

static GtkWidget *
unit_editor_new (UnitInfo *target, PrintUnit init)
{
	GtkWidget *box, *om, *menu;
	int i;
	
	box = gtk_hbox_new (0, 0);

	target->unit = init.desired_display;
	target->value = unit_convert (init.points, UNIT_POINTS, init.desired_display);

	target->adj = gtk_adjustment_new (
		target->value,
		0.0, 1000.0, 0.1, 1.0, 1.0);
	target->spin = GTK_SPIN_BUTTON (gtk_spin_button_new (target->adj, 1, 1));
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
remove_placeholder_callback (GtkWidget *widget, gpointer data)
{
	gtk_container_remove (GTK_CONTAINER (widget->parent), widget);
}

/*
 * This routine removes a placeholder inserted
 * by libglade from a container
 */
static void
remove_placeholders (GtkContainer *container)
{
	g_return_if_fail (GTK_IS_CONTAINER (container));

	gtk_container_foreach (container, remove_placeholder_callback, NULL);
}

static void
do_setup_margin (dialog_print_info_t *dpi)
{
	GtkTable *table;
	PrintMargins *pm = &dpi->pi->margins;
	GtkWidget *container;
	
	dpi->margin_preview = gnome_canvas_new ();
	gtk_widget_set_usize (dpi->margin_preview, PREVIEW_X, PREVIEW_Y);
	gtk_widget_show (dpi->margin_preview);
	
	table = GTK_TABLE (glade_xml_get_widget (dpi->gui, "margin-table"));

	tattach (table, 1, 1, pm->top,    &dpi->margins.top);
	tattach (table, 2, 1, pm->header, &dpi->margins.header);
	tattach (table, 0, 4, pm->left,   &dpi->margins.left);
	tattach (table, 2, 4, pm->right,  &dpi->margins.right);
	tattach (table, 1, 7, pm->bottom, &dpi->margins.bottom);
	tattach (table, 2, 7, pm->footer, &dpi->margins.footer);

	container = glade_xml_get_widget (dpi->gui, "container-margin-page");
	remove_placeholders (GTK_CONTAINER (container));
	gtk_box_pack_start (GTK_BOX (container), dpi->margin_preview, TRUE, TRUE, 0);

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
do_setup_page_info (dialog_print_info_t *dpi)
{
	GtkWidget *divisions = glade_xml_get_widget (dpi->gui, "check-print-divisions");
	GtkWidget *bw        = glade_xml_get_widget (dpi->gui, "check-black-white");
	GtkWidget *titles    = glade_xml_get_widget (dpi->gui, "check-print-titles");
	GtkWidget *order     = glade_xml_get_widget (dpi->gui, "radio-order-right");
	
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

	dpi->paper = dpi->pi->paper;
	gtk_entry_set_text (GTK_ENTRY (combo->entry), gnome_paper_name (dpi->pi->paper));
}

static void
do_setup_main_dialog (dialog_print_info_t *dpi)
{
	GtkWidget *notebook;
	
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
		do_fetch_page (dpi);
		do_fetch_margins (dpi);
		do_fetch_hf (dpi);
		do_fetch_page_info (dpi);
	}

	if (v != -1)
		gnome_dialog_close (GNOME_DIALOG (dpi->dialog));
	
	dialog_print_info_destroy (dpi);
}
