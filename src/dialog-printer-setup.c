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

typedef struct {
	Workbook         *workbook;
	GladeXML         *gui;
	PrintInformation *pi;
	GtkWidget        *dialog;
	GtkWidget        *margin_preview;
} dialog_print_info_t;

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
do_setup_main_dialog (dialog_print_info_t *dpi)
{
	dpi->margin_preview = gnome_canvas_new ();
}

static void
paper_size_changed (GtkEntry *entry, dialog_print_info_t *dpi)
{
	char *text;
	GnomePaper *paper;
	
	text = gtk_entry_get_text (entry);

	dpi->pi->paper = gnome_paper_with_name (text);
	g_assert (dpi->pi->paper);

	margin_preview_update (dpi);
	
	g_free (text);
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

	gtk_entry_set_text (combo->entry, dpi->pi->paper_name);
}

static void
do_setup_main_dialog (dialog_print_info_t *dpi)
{
	/*
	 * 1. Moves the whole thing into a GnomeDialog, needed until
	 *    we get GnomeDialog support in Glade.
	 */
	dpi->dialog = gnome_dialog_new (
		_("Print setup"),
		GNOME_STOCK_BUTTON_OK,
		GNOME_STOCK_BUTTON_CANCEL,
		NULL);

	gtk_widget_reparent (
		glade_xml_get_widget (dpi->gui, "print-setup-notebook"),
		GNOME_DIALOG (dpi->dialog)->vbox);

	gtk_widget_unref (glade_xml_get_widget (dpi->gui, "print-setup"));
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
