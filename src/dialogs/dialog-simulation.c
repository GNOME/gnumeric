/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-simulation.c:
 *
 * Authors:
 *        Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 **/
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <sheet.h>
#include <cell.h>
#include <ranges.h>
#include <gui-util.h>
#include <tool-dialogs.h>
#include <dao-gui-utils.h>
#include <value.h>
#include <workbook-edit.h>

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>
#include <widgets/gnumeric-expr-entry.h>
#include "simulation.h"


#define SIMULATION_KEY         "simulation-dialog"

typedef GenericToolState SimulationState;

/**
 * simulation_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity
 **/
static void
simulation_update_sensitivity_cb (GtkWidget *dummy,
				       SimulationState *state)
{
        Value *output_range = NULL;
        Value *input_range  = NULL;
        Value *output_vars  = NULL;

	int i;

        input_range = gnm_expr_entry_parse_as_value (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);
	if (input_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->warning),
				    _("The list range is invalid."));
		gtk_widget_set_sensitive (state->ok_button, FALSE);
		return;
	} else
		value_release (input_range);

	output_vars =  gnm_expr_entry_parse_as_value
		(state->input_entry_2, state->sheet);
	if (output_vars == NULL) {
		gtk_label_set_text (GTK_LABEL (state->warning),
				    _("The output variable range is invalid."));
		gtk_widget_set_sensitive (state->ok_button, FALSE);
		return;
	} else
		value_release (output_vars);

	i = gnumeric_glade_group_value (state->gui, output_group);
	if (i == 2) {
		output_range = gnm_expr_entry_parse_as_value
			(GNUMERIC_EXPR_ENTRY (state->output_entry),
			 state->sheet);
		if (output_range == NULL) {
			gtk_label_set_text (GTK_LABEL (state->warning),
					    _("The output range is invalid."));
			gtk_widget_set_sensitive (state->ok_button, FALSE);
			return;
		} else
			value_release (output_range);
	}

	gtk_label_set_text (GTK_LABEL (state->warning), "");
	gtk_widget_set_sensitive (state->ok_button, TRUE);
	return;
}

static gboolean
prepare_ranges (simulation_t *sim)
{
	int i, n, base_col, base_row;

	if (sim->inputs->type != VALUE_CELLRANGE ||
	    sim->outputs->type != VALUE_CELLRANGE)
		return TRUE;

	sim->ref_inputs  = value_to_rangeref (sim->inputs, FALSE);
	sim->ref_outputs = value_to_rangeref (sim->outputs, FALSE);

	sim->n_input_vars =
		(abs (sim->ref_inputs->a.col - sim->ref_inputs->b.col) + 1) *
		(abs (sim->ref_inputs->a.row - sim->ref_inputs->b.row) + 1);
	sim->n_output_vars =
		(abs (sim->ref_outputs->a.col - sim->ref_outputs->b.col) + 1) *
		(abs (sim->ref_outputs->a.row - sim->ref_outputs->b.row) + 1);

	/* Get the intput cells into a list. */
	sim->list_inputs = NULL;
	base_col = MIN (sim->ref_inputs->a.col, sim->ref_inputs->b.col);
	base_row = MIN (sim->ref_inputs->a.row, sim->ref_inputs->b.row);
	for (i  = base_col;
	     i <= MAX (sim->ref_inputs->a.col, sim->ref_inputs->b.col); i++) {
		for (n = base_row;
		     n<= MAX (sim->ref_inputs->a.row, sim->ref_inputs->b.row);
		     n++) {
			Cell *cell;

			cell = sheet_cell_fetch (sim->ref_inputs->a.sheet,
						 i, n);
			sim->list_inputs = g_slist_append (sim->list_inputs,
							   cell);
		}
	}

	/* Get the output cells into a list. */
	sim->list_outputs = NULL;
	base_col = MIN (sim->ref_outputs->a.col, sim->ref_outputs->b.col);
	base_row = MIN (sim->ref_outputs->a.row, sim->ref_outputs->b.row);
	for (i  = base_col;
	     i <= MAX (sim->ref_outputs->a.col, sim->ref_outputs->b.col); i++) {
		for (n = base_row;
		     n<= MAX (sim->ref_outputs->a.row, sim->ref_outputs->b.row);
		     n++) {
			Cell *cell;

			cell = sheet_cell_fetch (sim->ref_outputs->a.sheet,
						 i, n);
			sim->list_outputs = g_slist_append (sim->list_outputs,
							    cell);
		}
	}

	return FALSE;
}

/**
 * simulation_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the advanced_filter.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
simulation_ok_clicked_cb (GtkWidget *button, SimulationState *state)
{
	data_analysis_output_t  dao;
	char                    *text;
	GtkWidget               *w;
	gchar                   *err;
	gboolean                unique;
	simulation_t            sim;

	sim.inputs = gnm_expr_entry_parse_as_value
		(GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

	sim.outputs = gnm_expr_entry_parse_as_value
		(state->input_entry_2, state->sheet);

        parse_output ((GenericToolState *) state, &dao);

	w = glade_xml_get_widget (state->gui, "unique-button");
	unique = (1 == gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)));

	if (prepare_ranges (&sim)) {
		err = (gchar *) N_("Invalid variable range was given");
		goto out;
	}

	w = glade_xml_get_widget (state->gui, "iterations");
	sim.n_iterations = gtk_spin_button_get_value (GTK_SPIN_BUTTON (w));

	w = glade_xml_get_widget (state->gui, "first_round");
	sim.first_round = gtk_spin_button_get_value (GTK_SPIN_BUTTON (w)) - 1;

	w = glade_xml_get_widget (state->gui, "last_round");
	sim.last_round = gtk_spin_button_get_value (GTK_SPIN_BUTTON (w)) - 1;

	err = simulation_tool (WORKBOOK_CONTROL (state->wbcg),
			       &dao, &sim);
 out:
	value_release (sim.inputs);
	value_release (sim.outputs);

	if (err != NULL)
		error_in_entry ((GenericToolState *) state,
				GTK_WIDGET (state->input_entry_2), err);
	return;
}

/**
 * dialog_simulation:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
void
dialog_simulation (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        SimulationState *state;
	WorkbookControl *wbc;

	g_return_if_fail (wbcg != NULL);

	wbc = WORKBOOK_CONTROL (wbcg);

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, SIMULATION_KEY))
		return;

	state = g_new (SimulationState, 1);
	if (dialog_tool_init (state, wbcg, wb_control_cur_sheet (wbc),
			      "simulation.html",
			      "simulation.glade", "Simulation",
			      _("_Inputs:"), _("_Outputs::"),
			      _("Could not create the Simulation dialog."),
			      SIMULATION_KEY,
			      G_CALLBACK (simulation_ok_clicked_cb),
			      G_CALLBACK (simulation_update_sensitivity_cb),
			      0))
		return;

	simulation_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);

        return;
}
