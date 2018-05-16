/*
 * dialog-row-height.c:  Sets the magnification factor
 *
 * Author:
 *        Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * (c) Copyright 2002 Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
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
#include <sheet-view.h>
#include <application.h>
#include <workbook-cmd-format.h>


#define ROW_HEIGHT_DIALOG_KEY "row-height-dialog"

typedef struct {
	GtkBuilder         *gui;
	WBCGtk *wbcg;
	Sheet              *sheet;
	SheetView	   *sv;
	GtkWidget          *dialog;
	GtkWidget          *ok_button;
	GtkWidget          *apply_button;
	GtkWidget          *cancel_button;
	GtkWidget          *default_check;
	GtkWidget          *description;
	GtkWidget          *points;
	GtkSpinButton      *spin;

	gboolean           set_default_value;

	gint               orig_value;
	gboolean           orig_is_default;
	gboolean           orig_some_default;
	gboolean           orig_all_equal;
	gboolean           adjusting;
} RowHeightState;

static void
dialog_row_height_update_points (RowHeightState *state)
{
	gint value = gtk_spin_button_get_value_as_int (state->spin);
	double size_points = value *  72./gnm_app_display_dpi_get (TRUE);
	gchar *pts;

	pts = g_strdup_printf ("%.2f",size_points);
	gtk_label_set_text (GTK_LABEL (state->points), pts);
	g_free (pts);
}

static void
dialog_row_height_button_sensitivity (RowHeightState *state)
{
	gint value = gtk_spin_button_get_value_as_int  (state->spin);
	gboolean use_default = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->default_check));
	gboolean changed_info;

	if (state->set_default_value) {
		changed_info = (state->orig_value != value);
	} else {
		changed_info = (((!state->orig_all_equal || (state->orig_value != value)
				  || state->orig_some_default) && !use_default)
				|| (use_default && !state->orig_is_default));

	}
	gtk_widget_set_sensitive (state->ok_button, changed_info);
	gtk_widget_set_sensitive (state->apply_button, changed_info);

	dialog_row_height_update_points (state);
}

static void
cb_dialog_row_height_destroy (RowHeightState *state)
{
	if (state->gui != NULL)
		g_object_unref (state->gui);
	g_free (state);
}

static void
cb_dialog_row_height_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
				     RowHeightState *state)
{
	gtk_widget_destroy (state->dialog);
}


static gint
dialog_row_height_set_value (gint value, RowHeightState *state)
{
	gint adj_value = value/state->sheet->last_zoom_factor_used + 0.5;
	gtk_spin_button_set_value (state->spin, adj_value);
	return adj_value;
}

static void
dialog_row_height_load_value (RowHeightState *state)
{
	GSList *l;
	gint value = 0;
	state->orig_is_default = TRUE;
	state->orig_some_default = FALSE;
	state->orig_all_equal = TRUE;

	state->adjusting = TRUE;
	if (state->set_default_value) {
		value = sheet_row_get_default_size_pixels (state->sheet);
	} else {
		for (l = state->sv->selections; l; l = l->next){
			GnmRange *ss = l->data;
			int row;

			for (row = ss->start.row; row <= ss->end.row; row++){
				ColRowInfo const *ri = sheet_row_get_info (state->sheet, row);
				if (ri->hard_size)
					state->orig_is_default = FALSE;
				else
					state->orig_some_default = TRUE;
				if (value == 0)
					value = ri->size_pixels;
				else if (value != ri->size_pixels){
					/* Values differ, so let the user enter the data */
					state->orig_all_equal = FALSE;
				}
			}
		}
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->default_check),
					      state->orig_is_default);
	}
	state->orig_value = dialog_row_height_set_value (value, state);
	dialog_row_height_button_sensitivity (state);
	state->adjusting = FALSE;
}

static void
cb_dialog_row_height_value_changed (G_GNUC_UNUSED GtkSpinButton *spinbutton,
				    RowHeightState *state)
{
	if (!state->adjusting) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->default_check), FALSE);
		dialog_row_height_button_sensitivity (state);
	}
}

static void
cb_dialog_row_height_default_check_toggled (GtkToggleButton *togglebutton, RowHeightState *state)
{
	if (!state->adjusting) {
		if (gtk_toggle_button_get_active (togglebutton)) {
			state->adjusting = TRUE;
			dialog_row_height_set_value (sheet_row_get_default_size_pixels (state->sheet),
						     state);
			state->adjusting = FALSE;
		}
		dialog_row_height_button_sensitivity (state);
	}
}

static void
cb_dialog_row_height_apply_clicked (G_GNUC_UNUSED GtkWidget *button,
				    RowHeightState *state)
{
	gint value = gtk_spin_button_get_value_as_int (state->spin);
	int size_pixels = (int)(value * state->sheet->last_zoom_factor_used + 0.5);
	gboolean use_default = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->default_check));

	if (state->set_default_value) {
		double points = value * 72./gnm_app_display_dpi_get (TRUE);
		cmd_colrow_std_size (GNM_WBC (state->wbcg),
				     state->sheet, FALSE, points);
		dialog_row_height_load_value (state);
	} else {
		if (use_default)
			size_pixels = 0;

		workbook_cmd_resize_selected_colrow (GNM_WBC (state->wbcg),
			state->sheet, FALSE, size_pixels);
		dialog_row_height_load_value (state);
	}
}

static void
cb_dialog_row_height_ok_clicked (GtkWidget *button, RowHeightState *state)
{
	cb_dialog_row_height_apply_clicked (button, state);
	gtk_widget_destroy (state->dialog);
}


static void
dialog_row_height_set_mode (gboolean set_default, RowHeightState *state)
{
	state->set_default_value = set_default;

	if (set_default) {
		gtk_widget_hide (state->default_check);
		gtk_label_set_text (GTK_LABEL (state->description),
				    _("Set standard/default row height"));
	} else {
		char *text;
		char *name = g_markup_escape_text (state->sheet->name_unquoted, -1);
		gtk_widget_show (state->default_check);
		text = g_strdup_printf (_("Set row height of selection on "
					  "<span style='italic' weight='bold'>%s</span>"),
					name);
		gtk_label_set_markup (GTK_LABEL (state->description), text);
		g_free (text);
		g_free (name);
	}

}

void
dialog_row_height (WBCGtk *wbcg, gboolean use_default)
{
	RowHeightState *state;

	g_return_if_fail (wbcg != NULL);

	if (gnm_dialog_raise_if_exists (wbcg, ROW_HEIGHT_DIALOG_KEY))
		return;

	state = g_new (RowHeightState, 1);
	state->wbcg  = wbcg;
	state->sv = wb_control_cur_sheet_view (GNM_WBC (wbcg));
	state->sheet = sv_sheet (state->sv);
	state->adjusting = FALSE;
	state->gui = gnm_gtk_builder_load ("res:ui/row-height.ui", NULL, GO_CMD_CONTEXT (wbcg));
	g_return_if_fail (state->gui != NULL);

	state->dialog = go_gtk_builder_get_widget (state->gui, "dialog");

	state->description = GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "description"));
	state->points = GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "pts-label"));

	state->spin  = GTK_SPIN_BUTTON (go_gtk_builder_get_widget (state->gui, "spin"));
	gtk_adjustment_set_lower (gtk_spin_button_get_adjustment (state->spin),
				  GNM_ROW_MARGIN + GNM_ROW_MARGIN + 1);
	g_signal_connect (G_OBJECT (state->spin),
		"value-changed",
		G_CALLBACK (cb_dialog_row_height_value_changed), state);

	state->default_check  = GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "default_check"));
	g_signal_connect (G_OBJECT (state->default_check),
		"clicked",
		G_CALLBACK (cb_dialog_row_height_default_check_toggled), state);

	state->ok_button = go_gtk_builder_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_dialog_row_height_ok_clicked), state);
	state->apply_button = go_gtk_builder_get_widget (state->gui, "apply_button");
	g_signal_connect (G_OBJECT (state->apply_button),
		"clicked",
		G_CALLBACK (cb_dialog_row_height_apply_clicked), state);

	state->cancel_button = go_gtk_builder_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (state->cancel_button),
		"clicked",
		G_CALLBACK (cb_dialog_row_height_cancel_clicked), state);

	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_ROW_HEIGHT);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_dialog_row_height_destroy);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog),
					   state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	dialog_row_height_set_mode (use_default, state);
	dialog_row_height_load_value (state);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	gnm_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       ROW_HEIGHT_DIALOG_KEY);
	gtk_widget_show (state->dialog);
}
