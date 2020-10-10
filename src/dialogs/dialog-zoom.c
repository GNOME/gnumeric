/*
 * dialog-zoom.c:  Sets the magnification factor
 *
 * Author:
 *        Jody Goldberg <jody@gnome.org>
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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <gui-util.h>
#include <commands.h>
#include <workbook-control.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <sheet.h>


#define ZOOM_DIALOG_KEY "zoom-dialog"
#define ZOOM_DIALOG_FACTOR_KEY "zoom-dialog-factor"

enum {
	COL_SHEET_NAME,
	COL_SHEET_PTR
};

typedef struct {
	WBCGtk *wbcg;
	GtkWidget          *dialog;
	GtkWidget          *entry;
	GtkWidget          *ok_button;
	GtkWidget          *cancel_button;
	GtkRadioButton     *custom;
	GtkBuilder         *gui;

	GtkSpinButton  *zoom;
	GtkTreeView        *sheet_list;
	GtkListStore	   *sheet_list_model;
	GtkTreeSelection   *sheet_list_selection;
} ZoomState;

static const struct {
	char const * const name;
	gint const factor;
} buttons[] = {
	{ "radio_200", 200 },
	{ "radio_100", 100 },
	{ "radio_75",  75 },
	{ "radio_50",  50 },
	{ "radio_25",  25 },
	{ NULL, 0}
};

static void
cb_zoom_destroy (ZoomState *state)
{
	if (state->sheet_list_model) {
		g_object_unref (state->sheet_list_model);
		state->sheet_list_model = NULL;
	}

	if (state->gui != NULL) {
		g_object_unref (state->gui);
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);
}

static void
cb_zoom_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
			ZoomState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}

static void
radio_toggled (GtkToggleButton *togglebutton, ZoomState *state)
{
	gint factor;

	/* We are only interested in the new state */
	if (gtk_toggle_button_get_active (togglebutton)) {
		factor = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (togglebutton),
							     ZOOM_DIALOG_FACTOR_KEY));
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->zoom),
					   factor);
	}
}

static void
focus_to_custom (GtkToggleButton *togglebutton, ZoomState *state)
{
	if (gtk_toggle_button_get_active (togglebutton))
		gtk_widget_grab_focus (GTK_WIDGET (&state->zoom->entry));
}

static gboolean
custom_selected (G_GNUC_UNUSED GtkWidget *widget,
		 G_GNUC_UNUSED GdkEventFocus *event, ZoomState *state)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->custom), TRUE);
	return FALSE;
}

static void
cb_zoom_ok_clicked (G_GNUC_UNUSED GtkWidget *button, ZoomState *state)
{
	GSList *sheets = NULL;
	GList  *l, *tmp;

	l = gtk_tree_selection_get_selected_rows (state->sheet_list_selection, NULL);
	for (tmp = l; tmp; tmp = tmp->next) {
		GtkTreePath *path = tmp->data;
		GtkTreeIter iter;

		if (gtk_tree_model_get_iter (GTK_TREE_MODEL (state->sheet_list_model), &iter, path)) {
			Sheet *this_sheet;
			gtk_tree_model_get (GTK_TREE_MODEL (state->sheet_list_model),
					    &iter,
					    COL_SHEET_PTR, &this_sheet,
					    -1);
			sheets = g_slist_prepend (sheets, this_sheet);
		}
		gtk_tree_path_free (path);
	}
	g_list_free (l);

	if (sheets) {
		WorkbookControl *wbc = GNM_WBC (state->wbcg);
		double new_zoom = gtk_spin_button_get_value (state->zoom) / 100;
		sheets = g_slist_reverse (sheets);
		cmd_zoom (wbc, sheets, new_zoom);
	}

	gtk_widget_destroy (state->dialog);
}

void
dialog_zoom (WBCGtk *wbcg, Sheet *sheet)
{
	ZoomState *state;
	GPtrArray *sheets;
	unsigned ui;
	int i, row, cur_row;
	gboolean is_custom = TRUE;
	GtkRadioButton *radio;
	GtkWidget *focus_target;
	GtkBuilder *gui;
	GtkTreeViewColumn *column;

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (sheet != NULL);

	if (gnm_dialog_raise_if_exists (wbcg, ZOOM_DIALOG_KEY))
		return;
	gui = gnm_gtk_builder_load ("res:ui/dialog-zoom.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (gui == NULL)
		return;

	state = g_new (ZoomState, 1);
	state->wbcg   = wbcg;
	state->gui    = gui;
	state->dialog = go_gtk_builder_get_widget (state->gui, "Zoom");
	g_return_if_fail (state->dialog != NULL);

	/* Get the list of sheets */
	state->sheet_list_model = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_POINTER);
	state->sheet_list = GTK_TREE_VIEW (go_gtk_builder_get_widget (state->gui, "sheet_list"));
	gtk_tree_view_set_headers_visible (state->sheet_list, FALSE);
	gtk_tree_view_set_model (state->sheet_list, GTK_TREE_MODEL (state->sheet_list_model));
	state->sheet_list_selection = gtk_tree_view_get_selection (state->sheet_list);
	gtk_tree_selection_set_mode (state->sheet_list_selection, GTK_SELECTION_MULTIPLE);

	column = gtk_tree_view_column_new_with_attributes (_("Name"),
			gtk_cell_renderer_text_new (),
			"text", 0,
			NULL);
	gtk_tree_view_column_set_sort_column_id (column, COL_SHEET_NAME);
	gtk_tree_view_append_column (GTK_TREE_VIEW (state->sheet_list), column);

	sheets = workbook_sheets (wb_control_get_workbook (GNM_WBC (wbcg)));
	cur_row = row = 0;
	for (ui = 0; ui < sheets->len; ui++) {
		GtkTreeIter iter;
		Sheet *this_sheet = g_ptr_array_index (sheets, ui);

		gtk_list_store_append (state->sheet_list_model, &iter);
		gtk_list_store_set (state->sheet_list_model,
				    &iter,
				    COL_SHEET_NAME, this_sheet->name_unquoted,
				    COL_SHEET_PTR, this_sheet,
				    -1);

		if (this_sheet == sheet)
			cur_row = row;
		row++;
	}
	g_ptr_array_unref (sheets);

	{
		GtkTreePath *path = gtk_tree_path_new_from_indices (cur_row, -1);
		gtk_tree_view_set_cursor (state->sheet_list, path, NULL, FALSE);
		gtk_tree_path_free (path);
	}

	state->zoom  = GTK_SPIN_BUTTON (go_gtk_builder_get_widget (state->gui, "zoom"));
	g_return_if_fail (state->zoom != NULL);
	state->custom = GTK_RADIO_BUTTON (go_gtk_builder_get_widget (state->gui, "radio_custom"));
	g_return_if_fail (state->custom != NULL);
	focus_target = GTK_WIDGET (state->custom);
	g_signal_connect (G_OBJECT (state->custom),
		"clicked",
		G_CALLBACK (focus_to_custom), (gpointer) state);
	g_signal_connect (G_OBJECT (state->zoom),
		"focus_in_event",
		G_CALLBACK (custom_selected), state);

	for (i = 0; buttons[i].name != NULL; i++) {
		radio  = GTK_RADIO_BUTTON (go_gtk_builder_get_widget (state->gui, buttons[i].name));
		g_return_if_fail (radio != NULL);

		g_object_set_data (G_OBJECT (radio), ZOOM_DIALOG_FACTOR_KEY,
				   GINT_TO_POINTER(buttons[i].factor));

		g_signal_connect (G_OBJECT (radio),
			"toggled",
			G_CALLBACK (radio_toggled), state);

		if (((int)(sheet->last_zoom_factor_used * 100. + .5)) == buttons[i].factor) {
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);
			is_custom = FALSE;
			focus_target = GTK_WIDGET (radio);
		}
	}

	if (is_custom) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->custom), TRUE);
		gtk_spin_button_set_value (state->zoom,
					   (int)(sheet->last_zoom_factor_used * 100. + .5));
	}
	state->ok_button = go_gtk_builder_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_zoom_ok_clicked), state);

	state->cancel_button = go_gtk_builder_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (state->cancel_button),
		"clicked",
		G_CALLBACK (cb_zoom_cancel_clicked), state);

	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (&state->zoom->entry));

	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_ZOOM);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog), wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	gnm_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       ZOOM_DIALOG_KEY);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_zoom_destroy);
	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	gtk_widget_show (state->dialog);

	gtk_widget_grab_focus (focus_target);
}
