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
	GtkWidget *spin;
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
} dialog_print_info_t;

static double
unit_into_to_points (UnitInfo *ui)
{
	return ui->value * units [ui->unit].factor;
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

static GtkWidget *
add_unit (GtkWidget *menu, int i, (*convert)(GtkWidget *, UnitInfo *), void *data)
{
	GtkWidget *item;

	item = gtk_menu_item_new_with_label (unit_name_get_string (i));
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
	GtkSpinButton *spin;
	int i;
	
	box = gtk_hbox_new (0, 0);

	spin = GTK_SPIN_BUTTON (gtk_spin_button_new (NULL, 1, 0));
	gtk_spin_button_set_value (spin, init.points);
	gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (spin), FALSE, FALSE, 0);
	om = gtk_option_menu_new ();
	gtk_box_pack_start (GTK_BOX (box), om, TRUE, TRUE, 0);
	gtk_widget_show_all (box);

	menu = gtk_menu_new ();

	add_unit (menu, UNIT_POINTS, convert_to_pt, target);
	add_unit (menu, UNIT_MILLIMITER, convert_to_mm, target);
	add_unit (menu, UNIT_CENTIMETER, convert_to_cm, target);
	add_unit (menu, UNIT_INCH, convert_to_inch, target);

	gtk_menu_set_active (GTK_MENU (menu), target->unit);
	gtk_option_menu_set_menu (GTK_OPTION_MENU (om), menu);
	gtk_option_menu_set_history (GTK_OPTION_MENU (om), target->unit);

	return box;
}

static void
tattach (GtkTable *table, int x, int y, PrintUnit init, UnitInfo *target)
{
	GtkWidget *w;
	
	w = unit_editor_new (target, init);
	gtk_table_attach (
		table, w, x, x+1, y, y+1,
		0, 0, 0, 0);
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
}

static void
paper_size_changed (GtkEntry *entry, dialog_print_info_t *dpi)
{
	char *text;
	GnomePaper *paper;
	
	text = gtk_entry_get_text (entry);

	paper = gnome_paper_with_name (text);
	if (paper != NULL){
		dpi->pi->paper = paper;
		margin_preview_update (dpi);
	}
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
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, toggle)), 1);

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
	
	gui = glade_xml_new ("print.glade", NULL);
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
	do_setup_page (dpi);

	v = gnome_dialog_run_and_close (GNOME_DIALOG (dpi->dialog));

	if (v == 0){
		/* fetch information from dialog */
	}
	
	dialog_print_info_destroy (dpi);
}
