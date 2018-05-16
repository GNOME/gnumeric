/*
 * dialog-fill-series.c: Fill according to a linear or exponential serie.
 *
 * Authors: Jukka-Pekka Iivonen (jiivonen@hutcs.cs.hut.fi)
 *          Andreas J. Guelzow (aguelzow@taliesin.ca)
 *
 * Copyright (C) 2003  Jukka-Pekka Iivonen (jiivonen@hutcs.cs.hut.fi)
 * Copyright (C) Andreas J. Guelzow (aguelzow@taliesin.ca)
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
#include <dialogs/tool-dialogs.h>

#include <gui-util.h>
#include <selection.h>
#include <workbook.h>
#include <sheet.h>
#include <sheet-view.h>
#include <commands.h>
#include <ranges.h>
#include <cmd-edit.h>
#include <wbc-gtk.h>
#include <command-context.h>
#include <value.h>
#include <selection.h>
#include <rendered-value.h>
#include <cell.h>
#include <widgets/gnm-dao.h>

#include <dialogs/dao-gui-utils.h>

#include <tools/fill-series.h>

#define FILL_SERIES_KEY "fill-series-dialog"

typedef struct {
	GnmGenericToolState base;
	GtkWidget          *start_entry;
	GtkWidget          *stop_entry;
	GtkWidget          *step_entry;
	GtkWidget          *date_steps_type;
} FillSeriesState;

static void
cb_fill_series_update_sensitivity (G_GNUC_UNUSED GtkWidget *dummy,
				   FillSeriesState *state)
{
	gboolean   ready;
	gboolean   step, stop;
	gnm_float  a_float;

	step = !entry_to_float (GTK_ENTRY (state->step_entry),
				&a_float, FALSE);
	stop = !entry_to_float (GTK_ENTRY (state->stop_entry),
				&a_float,FALSE);

	ready = gnm_dao_is_ready (GNM_DAO (state->base.gdao)) &&
		!entry_to_float (GTK_ENTRY (state->start_entry),
				 &a_float,
				 FALSE) &&
		((gnm_dao_is_finite (GNM_DAO (state->base.gdao))
		  && (step || stop)) ||
		 (step && stop));

	gtk_widget_set_sensitive (state->base.ok_button, ready);
}

static void
cb_fill_series_ok_clicked (G_GNUC_UNUSED GtkWidget *button,
			   FillSeriesState *state)
{
	GtkWidget       *radio;
	fill_series_t           *fs;
	data_analysis_output_t  *dao;

	fs = g_new0 (fill_series_t, 1);
	dao  = parse_output ((GnmGenericToolState *)state, NULL);

	/* Read the `Series in' radio buttons. */
	radio = go_gtk_builder_get_widget (state->base.gui, "series_in_rows");
	fs->series_in_rows = ! gnm_gtk_radio_group_get_selected
	        (gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio)));

	/* Read the `Type' radio buttons. */
	radio = go_gtk_builder_get_widget (state->base.gui, "type_linear");
	fs->type = gnm_gtk_radio_group_get_selected
	        (gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio)));

	/* Read the `Date unit' radio buttons. */
	radio = go_gtk_builder_get_widget (state->base.gui, "unit_day");
	fs->date_unit = gnm_gtk_radio_group_get_selected
	        (gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio)));

	fs->is_step_set = ! entry_to_float (GTK_ENTRY (state->step_entry),
					    &fs->step_value, TRUE);
	fs->is_stop_set = ! entry_to_float (GTK_ENTRY (state->stop_entry),
					    &fs->stop_value, TRUE);
	entry_to_float (GTK_ENTRY (state->start_entry),
			&fs->start_value, TRUE);

	if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg),
				state->base.sheet,
				dao, fs, fill_series_engine, TRUE))
		gtk_widget_destroy (state->base.dialog);
}

static void
cb_type_button_clicked (G_GNUC_UNUSED GtkWidget *button,
			FillSeriesState *state)
{
	GtkWidget          *radio;
	fill_series_type_t type;


	/* Read the `Type' radio buttons. */
	radio = go_gtk_builder_get_widget (state->base.gui, "type_linear");
	type = gnm_gtk_radio_group_get_selected (gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio)));

	if (type == FillSeriesTypeDate)
		gtk_widget_set_sensitive (state->date_steps_type, TRUE);
	else
		gtk_widget_set_sensitive (state->date_steps_type, FALSE);
}

/**
 * dialog_fill_series_tool_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static void
dialog_fill_series_tool_init (FillSeriesState *state)
{
	GtkWidget   *radio;
	char const  *button;
	GnmRange const *sel;
	gboolean prefer_rows = FALSE;

	sel = selection_first_range (state->base.sv, NULL, NULL);

	/* Set the sensitivity of Unit day. */
	radio = go_gtk_builder_get_widget (state->base.gui,
				      "type_date");
	g_signal_connect (G_OBJECT (radio), "clicked",
			  G_CALLBACK (cb_type_button_clicked), state);

	state->stop_entry = go_gtk_builder_get_widget (state->base.gui, "stop_entry");
	g_signal_connect_after (G_OBJECT (state->stop_entry),
		"changed",
		G_CALLBACK (cb_fill_series_update_sensitivity), state);
	state->step_entry = go_gtk_builder_get_widget (state->base.gui, "step_entry");
	g_signal_connect_after (G_OBJECT (state->step_entry),
		"changed",
		G_CALLBACK (cb_fill_series_update_sensitivity), state);
	state->start_entry = go_gtk_builder_get_widget (state->base.gui, "start_entry");
	g_signal_connect_after (G_OBJECT (state->start_entry),
		"changed",
		G_CALLBACK (cb_fill_series_update_sensitivity), state);


	state->date_steps_type  = go_gtk_builder_get_widget (state->base.gui,
							"table-date-unit");
	gtk_widget_set_sensitive (state->date_steps_type, FALSE);

	button = (sel == NULL ||
		  (prefer_rows =
		   (range_width (sel) >= range_height (sel))))
		? "series_in_rows"
		: "series_in_cols";
	radio = go_gtk_builder_get_widget (state->base.gui, button);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);

	if (sel != NULL) {
		GnmCell *cell_start;
		GnmCell *cell_end;

		dialog_tool_preset_to_range (&state->base);

		cell_start = sheet_cell_get (state->base.sheet,
				       sel->start.col, sel->start.row);
		if (cell_start) {
			char *content = gnm_cell_get_rendered_text (cell_start);
			if (content) {
				gtk_entry_set_text (GTK_ENTRY (state->start_entry),
						    content);
				g_free (content);
			}
		}
		cell_end = prefer_rows ?
			sheet_cell_get (state->base.sheet,
					sel->end.col, sel->start.row) :
			sheet_cell_get (state->base.sheet,
					sel->start.col, sel->end.row);
		if (cell_end) {
			char *content = gnm_cell_get_rendered_text (cell_end);
			if (content) {
				gtk_entry_set_text (GTK_ENTRY (state->stop_entry),
						    content);
				g_free (content);
			}
		}
		if (cell_start && cell_end) {
			float_to_entry (GTK_ENTRY(state->step_entry),
					(value_get_as_float(cell_end->value) -
					 value_get_as_float(cell_start->value))
					/ (prefer_rows ?
					   (sel->end.col-sel->start.col) :
					   (sel->end.row-sel->start.row)));
		}
	}

	cb_fill_series_update_sensitivity (NULL, state);
}

void
dialog_fill_series (WBCGtk *wbcg)
{
	FillSeriesState *state;
	WorkbookControl *wbc = GNM_WBC (wbcg);
	SheetView       *sv = wb_control_cur_sheet_view (wbc);

	g_return_if_fail (wbcg != NULL);

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, FILL_SERIES_KEY)) {
		return;
	}

	state = g_new (FillSeriesState, 1);

	if (dialog_tool_init ((GnmGenericToolState *)state, wbcg, sv_sheet (sv),
			      GNUMERIC_HELP_LINK_FILL_SERIES,
			      "res:ui/fill-series.ui", "Fill_series",
			      _("Could not create the Fill Series dialog."),
			      FILL_SERIES_KEY,
			      G_CALLBACK (cb_fill_series_ok_clicked), NULL,
			      G_CALLBACK (cb_fill_series_update_sensitivity),
			      0))
		return;

	gnm_dao_set_put (GNM_DAO (state->base.gdao), FALSE, FALSE);
	dialog_fill_series_tool_init (state);
	gtk_widget_show (state->base.dialog);

	return;
}
