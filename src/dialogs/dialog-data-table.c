/*
 * dialog-data-table.c: Create a Data Table
 *
 * Copyright (C) 2006-2007 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <expr.h>
#include <selection.h>
#include <sheet.h>
#include <sheet-view.h>
#include <workbook.h>
#include <workbook-control.h>
#include <wbc-gtk.h>
#include <gui-util.h>
#include <parse-util.h>
#include <commands.h>
#include <ranges.h>
#include <widgets/gnm-expr-entry.h>
#include <glib/gi18n.h>

#define DIALOG_DATA_TABLE_KEY "dialog-data-table"

typedef struct {
	GtkBuilder	*gui;
	GtkWidget	*dialog;
	GnmExprEntry	*row_entry, *col_entry;

	WBCGtk	*wbcg;
	Sheet	*sheet;
	GnmRange input_range;
} GnmDialogDataTable;

static void
cb_data_table_destroy (GnmDialogDataTable *state)
{
	if (state->gui != NULL)
		g_object_unref (state->gui);
	g_free (state);
}

static GnmExprEntry *
init_entry (GnmDialogDataTable *state, int row)
{
	GnmExprEntry *gee = gnm_expr_entry_new (state->wbcg, TRUE);
	GtkWidget *grid = go_gtk_builder_get_widget (state->gui, "table-grid");

	g_return_val_if_fail (grid != NULL, NULL);

	gnm_expr_entry_set_flags (gee,
		GNM_EE_SINGLE_RANGE | GNM_EE_SHEET_OPTIONAL | GNM_EE_FORCE_REL_REF,
		GNM_EE_MASK);
	g_object_set (G_OBJECT (gee), "with-icon", TRUE, NULL);
	gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (gee), 1, row, 1, 1);
	return gee;
}

static void
cb_data_table_response (GtkWidget *dialog, gint response_id, GnmDialogDataTable *state)
{
	if (response_id == GTK_RESPONSE_HELP)
		return;
	if (response_id == GTK_RESPONSE_OK)
		cmd_create_data_table (GNM_WBC (state->wbcg),
			state->sheet, &state->input_range,
			gnm_expr_entry_get_text	(state->col_entry),
			gnm_expr_entry_get_text (state->row_entry));
	gtk_widget_destroy (dialog);
}

static gboolean
data_table_init (GnmDialogDataTable *state, WBCGtk *wbcg)
{
	state->gui = gnm_gtk_builder_load ("res:ui/data-table.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (state->gui == NULL)
                return TRUE;

	state->dialog = go_gtk_builder_get_widget (state->gui, "DataTable");
	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog),
					   state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	state->row_entry = init_entry (state, 0);
	state->col_entry = init_entry (state, 1);

	g_signal_connect (G_OBJECT (state->dialog), "response",
		G_CALLBACK (cb_data_table_response), state);
	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help"),
		GNUMERIC_HELP_LINK_DATA_TABLE);

	/* a candidate for merging into attach guru */
	gnm_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
		DIALOG_DATA_TABLE_KEY);
	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
		GTK_WINDOW (state->dialog));

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_data_table_destroy);

	gtk_widget_show_all (GTK_WIDGET (state->dialog));

	return FALSE;
}

void
dialog_data_table (WBCGtk *wbcg)
{
	GnmDialogDataTable *state;
	GnmRange const	*r;
	GnmRange	 input_range;
	SheetView	*sv;
	Sheet		*sheet;

	g_return_if_fail (wbcg != NULL);

	if (wbc_gtk_get_guru (wbcg) ||
	    gnm_dialog_raise_if_exists (wbcg, DIALOG_DATA_TABLE_KEY))
		return;

	sv = wb_control_cur_sheet_view (GNM_WBC (wbcg));
	r = selection_first_range (sv, GO_CMD_CONTEXT (wbcg), _("Create Data Table"));
	if (NULL == r)
		return;
	if (range_width	(r) <= 1 || range_height (r) <= 1) {
		GError *msg = g_error_new (go_error_invalid(), 0,
			_("The selection must have more than 1 column and row to create a Data Table."));
		go_cmd_context_error (GO_CMD_CONTEXT (wbcg), msg);
		g_error_free (msg);
		return;
	}
	input_range = *r;
	input_range.start.col++;
	input_range.start.row++;
	sheet = sv_sheet (sv);
	if (sheet_range_splits_region (sheet, &input_range, NULL,
				       GO_CMD_CONTEXT (wbcg), _("Data Table")))
		return;
	if (cmd_cell_range_is_locked_effective
	    (sheet, &input_range, GNM_WBC (wbcg),
					   _("Data Table")))
		return;


	state = g_new0 (GnmDialogDataTable, 1);
	state->wbcg  = wbcg;
	state->sheet = sheet;
	state->input_range = input_range;
	if (data_table_init (state, wbcg)) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
			_("Could not create the Data Table definition dialog."));
		g_free (state);
	}
}
