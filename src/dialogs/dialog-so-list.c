/*
 * dialog-so-list.c: A property dialog for lists and combos
 *
 * Copyright (C) 2006 Jody Goldberg (jody@gnome.org)
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
#include <sheet-object-widget.h>
#include <workbook.h>
#include <workbook-control.h>
#include <wbc-gtk.h>
#include <gui-util.h>
#include <parse-util.h>
#include <commands.h>
#include <widgets/gnm-expr-entry.h>
#include <glib/gi18n.h>

#define DIALOG_SO_LIST_KEY "so-list"

typedef struct {
	GtkWidget	*dialog;
	GtkWidget	*as_index_radio;
	GnmExprEntry	*content_entry, *link_entry;

	WBCGtk	*wbcg;
	SheetObject		*so;
} GnmDialogSOList;

static GnmExprEntry *
init_entry (GnmDialogSOList *state, GtkBuilder *gui, int col, int row,
	    GnmExprTop const *texpr)
{
	GnmExprEntry *gee = gnm_expr_entry_new (state->wbcg, TRUE);
	GtkWidget *w = GTK_WIDGET (gee);
	GtkGrid *grid = GTK_GRID (gtk_builder_get_object (gui, "main-grid"));
	Sheet *sheet = sheet_object_get_sheet (state->so);
	GnmParsePos pp;

	g_return_val_if_fail (w != NULL, NULL);

	gtk_grid_attach (grid, w, col, row, 1, 1);
	gnm_expr_entry_set_flags (gee, GNM_EE_FORCE_ABS_REF |
				  GNM_EE_SHEET_OPTIONAL |
				  GNM_EE_SINGLE_RANGE, GNM_EE_MASK);

	parse_pos_init_sheet (&pp, sheet);
	gnm_expr_entry_load_from_expr (gee, texpr, &pp);
	return gee;
}

static void
cb_so_list_response (GtkWidget *dialog, gint response_id, GnmDialogSOList *state)
{
	if (response_id == GTK_RESPONSE_HELP)
		return;
	if (response_id == GTK_RESPONSE_OK) {
		GnmParsePos pp;
		Sheet *sheet = sheet_object_get_sheet (state->so);
		GnmExprTop const *output;
		GnmExprTop const *content;

		parse_pos_init (&pp, sheet->workbook, sheet, 0, 0);
		output = gnm_expr_entry_parse (state->link_entry,
					       &pp, NULL, FALSE, GNM_EXPR_PARSE_FORCE_ABSOLUTE_REFERENCES);
		content = gnm_expr_entry_parse (state->content_entry,
						&pp, NULL, FALSE, GNM_EXPR_PARSE_FORCE_ABSOLUTE_REFERENCES);
		cmd_so_set_links (GNM_WBC (state->wbcg), state->so, output, content,
				  gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
								(state->as_index_radio)));
	}

	gtk_widget_destroy (dialog);
}

static gboolean
so_list_init (GnmDialogSOList *state, WBCGtk *wbcg, SheetObject *so)
{
	GnmExprTop const *texpr;
	GtkBuilder *gui;

	gui = gnm_gtk_builder_load ("res:ui/so-list.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (gui == NULL)
                return TRUE;

	state->wbcg   = wbcg;
	state->so     = so;
	state->dialog = go_gtk_builder_get_widget (gui, "SOList");

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog),
					   state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	texpr = sheet_widget_list_base_get_content_link (so);
	state->content_entry = init_entry (state, gui, 1, 4, texpr);
	if (texpr) gnm_expr_top_unref (texpr);

	texpr = sheet_widget_list_base_get_result_link (so);
	state->link_entry = init_entry (state, gui, 1, 0, texpr);
	if (texpr) gnm_expr_top_unref (texpr);

	state->as_index_radio = go_gtk_builder_get_widget (gui, "as-index-radio");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->as_index_radio),
				      sheet_widget_list_base_result_type_is_index (so));

	g_signal_connect (G_OBJECT (state->dialog), "response",
		G_CALLBACK (cb_so_list_response), state);
	gnm_init_help_button (
		go_gtk_builder_get_widget (gui, "help"),
		GNUMERIC_HELP_LINK_SO_LIST);

	/* a candidate for merging into attach guru */
	gnm_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
		DIALOG_SO_LIST_KEY);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, g_free);
	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
		GTK_WINDOW (state->dialog));
	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	gtk_widget_show_all (GTK_WIDGET (state->dialog));
	g_object_unref (gui);

	return FALSE;
}

void
dialog_so_list (WBCGtk *wbcg, GObject *so)
{
	GnmDialogSOList *state;

	g_return_if_fail (wbcg != NULL);

	if (wbc_gtk_get_guru (wbcg) ||
	    gnm_dialog_raise_if_exists (wbcg, DIALOG_SO_LIST_KEY))
		return;

	state = g_new0 (GnmDialogSOList, 1);
	if (so_list_init (state, wbcg, GNM_SO (so))) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
			_("Could not create the List Property dialog."));
		g_free (state);
	}
}
