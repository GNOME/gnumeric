/*
 * dialog-insert-cells.c: Insert a number of cells.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "selection.h"
#include "dialogs.h"
#include "workbook.h"
#include "sheet.h"
#include "commands.h"
#include "ranges.h"
#include "cmd-edit.h"

#define GLADE_FILE "insert-cells.glade"

static void
dialog_insert_cells_impl (WorkbookControlGUI *wbcg, GladeXML *gui,
			  Sheet *sheet, Range const *sel)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GtkWidget *dialog, *radio_0;
	int  cols, rows;
	int i, res;

	dialog = glade_xml_get_widget (gui, "Insert_cells");
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

		cols = sel->end.col - sel->start.col + 1;
		rows = sel->end.row - sel->start.row + 1;

		if (i == 0)
			cmd_shift_rows (wbc, sheet,
					sel->start.col,
					sel->start.row,
					sel->end.row, cols);
		else if (i == 1)
			cmd_shift_cols (wbc, sheet,
					sel->start.col,
					sel->end.col,
					sel->start.row, rows);
		else if (i == 2)
			cmd_insert_rows (wbc, sheet,
					 sel->start.row, rows);
		else if (i == 3)
			cmd_insert_cols (wbc, sheet,
					 sel->start.col, cols);
	}

	/* If user closed the dialog with prejudice, it's already destroyed */
	if (res >= 0)
		gnome_dialog_close (GNOME_DIALOG (dialog));
}

void
dialog_insert_cells (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GladeXML  *gui;
	Range const *sel;
	int  cols, rows;

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (!(sel = selection_first_range (sheet, wbc, _("Insert"))))
		return;
	cols = sel->end.col - sel->start.col + 1;
	rows = sel->end.row - sel->start.row + 1;

	if (range_is_full (sel, FALSE)) {
		cmd_insert_cols (wbc, sheet, sel->start.col, cols);
		return;
	}
	if (range_is_full (sel, FALSE)) {
		cmd_insert_rows (wbc, sheet, sel->start.row, rows);
		return;
	}

	gui = gnumeric_glade_xml_new (wbcg, GLADE_FILE);
        if (gui == NULL)
                return;

	/* Wrapper to ensure the libglade object gets removed on error */
	dialog_insert_cells_impl (wbcg, gui, sheet, sel);

	gtk_object_unref (GTK_OBJECT (gui));
}
