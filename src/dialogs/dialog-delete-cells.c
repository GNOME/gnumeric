/*
 * dialog-delete-cells.c: Delete a number of cells.
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
#include "workbook-control.h"
#include "workbook.h"
#include "sheet.h"
#include "commands.h"
#include "cmd-edit.h"

#define GLADE_FILE "delete-cells.glade"

static void
dialog_delete_cells_impl (WorkbookControlGUI *wbcg, Sheet *sheet, GladeXML  *gui)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GtkWidget *dialog, *radio_0;
	SheetSelection *ss;
	int  cols, rows;
	int i, res;

	dialog = glade_xml_get_widget (gui, "Delete_cells");
	if (dialog == NULL) {
		g_print ("Corrupt file " GLADE_FILE "\n");
		return;
	}
	radio_0 = glade_xml_get_widget (gui, "radio_0");
	g_return_if_fail (radio_0 != NULL);

	/* Bring up the dialog */
	res = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (res == GNOME_OK) {
		i = gtk_radio_group_get_selected
			(GTK_RADIO_BUTTON(radio_0)->group);

		ss = sheet->selections->data;
		cols = ss->user.end.col - ss->user.start.col + 1;
		rows = ss->user.end.row - ss->user.start.row + 1;

		if (i == 0)
			cmd_shift_rows (wbc, sheet,
					ss->user.start.col + cols, 
					ss->user.start.row, 
					ss->user.end.row, -cols);
		else if (i == 1)
			cmd_shift_cols (wbc, sheet,
					ss->user.start.col,
					ss->user.end.col, 
					ss->user.start.row + rows, -rows);
		else if (i == 2)
			cmd_delete_rows (wbc, sheet,
					   ss->user.start.row, rows);
		else if (i == 3)
			cmd_delete_cols (wbc, sheet,
					   ss->user.start.col, cols);
	}

	/* If user closed the dialog with prejudice, it's already destroyed */
	if (res >= 0)
		gnome_dialog_close (GNOME_DIALOG (dialog));
}

void
dialog_delete_cells (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GladeXML  *gui;
	SheetSelection *ss;
	int  cols, rows;

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (!selection_is_simple (wbc, sheet, _("delete cells")))
		return;

	ss = sheet->selections->data;
	cols = ss->user.end.col - ss->user.start.col + 1;
	rows = ss->user.end.row - ss->user.start.row + 1;

	/* short circuit the dialog if an entire row/column is selected */
	if (ss->user.start.row == 0 && ss->user.end.row  >= SHEET_MAX_ROWS-1) {
		cmd_delete_cols (wbc, sheet,
				 ss->user.start.col, cols);
		return;
	}
	if (ss->user.start.col == 0 && ss->user.end.col  >= SHEET_MAX_COLS-1) {
		cmd_delete_rows (wbc, sheet,
				 ss->user.start.row, rows);
		return;
	}

	gui = gnumeric_glade_xml_new (wbcg, GLADE_FILE);
        if (gui == NULL)
                return;

	/* Wrapper to ensure the libglade object gets removed on error */
	dialog_delete_cells_impl (wbcg, sheet, gui);
	
	gtk_object_unref (GTK_OBJECT (gui));
}

