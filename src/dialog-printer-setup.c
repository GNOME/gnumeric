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
} dialog_print_info_t;

static GtkWidget *
load_image (const char *name)
{
	GtkWidget *image;
	char *path;
	
	path = gnome_unconditional_pixmap_file (name);
	image = gnome_pixmap_new_from_file (path);
	g_free (path);
	
	return image;
}

static void
do_setup_page (dialog_print_info_t *dpi)
{
	GtkTable *table;
	PrintInformation *pi = dpi->pi;
	GladeXML *gui = dpi->gui;
	char *toggle;
	
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
	if (pi->scaling->type == PERCENTAGE)
		scale = "scale-percent-radio";
	else
		scale = "scale-size-fit-radio";

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, toggle)), 1);

	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (glade_xml_get_widget (
			gui, "scale-percent-spin", pi->scaling->percentage)));

	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (glade_xml_get_widget (
			gui, "scale-width-spin", pi->scaling->dim.cols)));
	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (glade_xml_get_widget (
			gui, "scale-height-spin", pi->scaling->dim.rows)));
}

void
dialog_printer_setup (Workbook *wb)
{
	dialog_print_info_t *dpi;
	GtkWidget *gui;
	int i;

	gui = glade_xml_new ("print.glade", "print-setup");
	if (!gui){
		g_error ("Could not load print.glade");
		return;
	}
	
	/*
	 * Keep track of open print configuration windows for this workbook
	 */
	if (wb->print_info->print_config_dialog_data)
		return;

	dpi = g_new (dialog_print_info_t, 1);
	dpi->workbook = wb;
	dpi->gui = gui;
	dpi->pi = wb->print_info;
	
	wb->print_info->print_config_dialog_data = dpi;

	do_setup_page (dpi);
}



