/*
 * dialog-insert-cells.c: Insert a number of cells.
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
#include "selection.h"
#include "dialogs.h"
#include "workbook-view.h"

#define GLADE_FILE "insert-cells.glade"

static void
dialog_insert_cells_impl (Workbook *wb, Sheet *sheet, GladeXML  *gui)
{

	GtkWidget *dialog, *radio_0;
	SheetSelection *ss;
	int  cols, rows;
	int i, res;

	dialog = glade_xml_get_widget (gui, "Insert_cells");
	if (dialog == NULL) {
		g_print ("Corrupt file " GLADE_FILE "\n");
		return;
	}
	radio_0 = glade_xml_get_widget (gui, "radio_0");
	g_return_if_fail (radio_0 != NULL);

	/* Make dialog a child of the application so that it will iconify */
	gnome_dialog_set_parent (GNOME_DIALOG (dialog), 
				    GTK_WINDOW (wb->toplevel));

	/* Bring up the dialog */
	res = gnome_dialog_run (GNOME_DIALOG (dialog));
	if (res == GNOME_OK) {
		i = gtk_radio_group_get_selected
			(GTK_RADIO_BUTTON(radio_0)->group);

		ss = sheet->selections->data;
		cols = ss->user.end.col - ss->user.start.col + 1;
		rows = ss->user.end.row - ss->user.start.row + 1;

		if (i == 0)
			sheet_shift_rows (command_context_gui(), sheet,
					  ss->user.start.col, 
					  ss->user.start.row, 
					  ss->user.end.row, cols);
		else if (i == 1)
			sheet_shift_cols (command_context_gui(), sheet,
					  ss->user.start.col,
					  ss->user.end.col, 
					  ss->user.start.row, rows);
		else if (i == 2)
			sheet_insert_rows (command_context_gui(), sheet,
					   ss->user.start.row, rows);
		else if (i == 3)
			sheet_insert_cols (command_context_gui(), sheet,
					   ss->user.start.col, cols);
	}

	/* If user closed the dialog with prejudice, it's already destroyed */
	if (res >= 0)
		gnome_dialog_close (GNOME_DIALOG (dialog));
}

void
dialog_insert_cells (Workbook *wb, Sheet *sheet)
{
	GladeXML  *gui;
	SheetSelection *ss;
	int  cols, rows;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (!selection_is_simple (command_context_gui(), sheet, _("insert cells")))
		return;

	ss = sheet->selections->data;
	cols = ss->user.end.col - ss->user.start.col + 1;
	rows = ss->user.end.row - ss->user.start.row + 1;

	/* short circuit the dialog if an entire row/column is selected */
	if (ss->user.start.row == 0 && ss->user.end.row  >= SHEET_MAX_ROWS-1) {
		sheet_insert_cols (command_context_gui(), sheet,
				   ss->user.start.col, cols);
		return;
	}
	if (ss->user.start.col == 0 && ss->user.end.col  >= SHEET_MAX_COLS-1) {
		sheet_insert_rows (command_context_gui(), sheet,
				   ss->user.start.row, rows);
		return;
	}

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/" GLADE_FILE , NULL);
	if (!gui) {
		printf ("Could not find " GLADE_FILE "\n");
		return;
	}

	/* Wrapper to ensure the libglade object gets removed on error */
	dialog_insert_cells_impl (wb, sheet, gui);
	
	gtk_object_unref (GTK_OBJECT (gui));
}
