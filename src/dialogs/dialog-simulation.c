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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 **/
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <sheet.h>
#include <cell.h>
#include <ranges.h>
#include <gui-util.h>
#include <dialogs/tool-dialogs.h>
#include <dialogs/dao-gui-utils.h>
#include <value.h>
#include <wbc-gtk.h>

#include <widgets/gnm-expr-entry.h>
#include <widgets/gnm-dao.h>
#include <tools/simulation.h>

#include <string.h>

#define SIMULATION_KEY         "simulation-dialog"

typedef GnmGenericToolState SimulationState;

static GtkTextBuffer   *results_buffer;
static int             results_sim_index;
static simulation_t    *current_sim;

/**
 * simulation_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity
 **/
static void
simulation_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				  SimulationState *state)
{
        GnmValue *input_range  = NULL;
        GnmValue *output_vars  = NULL;

        input_range = gnm_expr_entry_parse_as_value (
		GNM_EXPR_ENTRY (state->input_entry), state->sheet);
	if (input_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->warning),
				    _("The input variable range is invalid."));
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

	if (!gnm_dao_is_ready (GNM_DAO (state->gdao))) {
			gtk_label_set_text (GTK_LABEL (state->warning),
					    _("The output range is invalid."));
			gtk_widget_set_sensitive (state->ok_button, FALSE);
			return;
	}

	gtk_label_set_text (GTK_LABEL (state->warning), "");
	gtk_widget_set_sensitive (state->ok_button, TRUE);
	return;
}

static gboolean
prepare_ranges (simulation_t *sim)
{
	int i, n, base_col, base_row;

	if (!VALUE_IS_CELLRANGE (sim->inputs) ||
	    !VALUE_IS_CELLRANGE (sim->outputs))
		return TRUE;

	sim->ref_inputs  = gnm_rangeref_dup (value_get_rangeref (sim->inputs));
	sim->ref_outputs = gnm_rangeref_dup (value_get_rangeref (sim->outputs));

	sim->n_input_vars =
		(abs (sim->ref_inputs->a.col - sim->ref_inputs->b.col) + 1) *
		(abs (sim->ref_inputs->a.row - sim->ref_inputs->b.row) + 1);
	sim->n_output_vars =
		(abs (sim->ref_outputs->a.col - sim->ref_outputs->b.col) + 1) *
		(abs (sim->ref_outputs->a.row - sim->ref_outputs->b.row) + 1);
	sim->n_vars = sim->n_input_vars + sim->n_output_vars;

	/* Get the intput cells into a list. */
	sim->list_inputs = NULL;
	base_col = MIN (sim->ref_inputs->a.col, sim->ref_inputs->b.col);
	base_row = MIN (sim->ref_inputs->a.row, sim->ref_inputs->b.row);
	for (i  = base_col;
	     i <= MAX (sim->ref_inputs->a.col, sim->ref_inputs->b.col); i++) {
		for (n = base_row;
		     n<= MAX (sim->ref_inputs->a.row, sim->ref_inputs->b.row);
		     n++) {
			GnmCell *cell = sheet_cell_fetch (sim->ref_inputs->a.sheet, i, n);
			sim->list_inputs = g_slist_append (sim->list_inputs, cell);
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
			GnmCell *cell = sheet_cell_fetch (sim->ref_outputs->a.sheet, i, n);
			sim->list_outputs = g_slist_append (sim->list_outputs, cell);
		}
	}

	return FALSE;
}

static void
update_log (SimulationState *state, simulation_t *sim)
{
	char const *txt [6] = {
		_("Simulations"), _("Iterations"), _("# Input variables"),
		_("# Output variables"), _("Runtime"), _("Run on")
	};
	GtkTreeIter  iter;
	GtkListStore *store;
	GtkTreePath  *path;
	GtkWidget    *view;
	GString      *buf;
	int          i;

	view = go_gtk_builder_get_widget (state->gui, "last-run-view");

	store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);

	for (i = 0; i < 6; i++) {
		buf = g_string_new (NULL);
		switch (i) {
		case 0:
			g_string_append_printf (buf, "%d",
						sim->last_round -
						sim->first_round + 1);
			break;
		case 1:
			g_string_append_printf (buf, "%d", sim->n_iterations);
			break;
		case 2:
			g_string_append_printf (buf, "%d", sim->n_input_vars);
			break;
		case 3:
			g_string_append_printf (buf, "%d", sim->n_output_vars);
			break;
		case 4:
			g_string_append_printf (buf, "%.2" GNM_FORMAT_g,
						(sim->end - sim->start) /
						(gnm_float) G_USEC_PER_SEC);
			break;
		case 5:
			dao_append_date (buf);
			break;
		default:
			g_string_append_printf (buf, "Error");
			break;
		}

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, 0, txt [i], 1, buf->str, -1);
		g_string_free (buf, TRUE);
	}

	path = gtk_tree_path_new_from_string ("0");
	if (gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &iter, path)) {
		;		/* Do something */
	} else {
		g_warning ("Did not get a valid iterator");
	}
	gtk_tree_path_free (path);

	gtk_tree_view_append_column
		(GTK_TREE_VIEW (view),
		 gtk_tree_view_column_new_with_attributes
		 (_("Name"),
		  gtk_cell_renderer_text_new (), "text", 0, NULL));
	gtk_tree_view_append_column
		(GTK_TREE_VIEW (view),
		 gtk_tree_view_column_new_with_attributes
		 (_("Value"),
		  gtk_cell_renderer_text_new (), "text", 1, NULL));
	gtk_tree_view_set_model (GTK_TREE_VIEW (view), GTK_TREE_MODEL (store));
	g_object_unref (store);
}

static void
update_results_view (simulation_t *sim)
{
	GString *buf;
	int     i;

	buf = g_string_new (NULL);

	g_string_append_printf (buf, "Simulation #%d\n\n", results_sim_index + 1);
	g_string_append_printf (buf, "%-20s %10s %10s %10s\n", _("Variable"),
			   _("Min"), _("Average"), _("Max"));
	for (i = 0; i < sim->n_vars; i++)
		g_string_append_printf (buf, "%-20s %10" GNM_FORMAT_g " %10"
					GNM_FORMAT_G " %10" GNM_FORMAT_g "\n",
				   sim->cellnames [i],
				   sim->stats [results_sim_index]->min [i],
				   sim->stats [results_sim_index]->mean [i],
				   sim->stats [results_sim_index]->max [i]);

	gtk_text_buffer_set_text (results_buffer, buf->str, strlen (buf->str));
	g_string_free (buf, TRUE);
}

static void
prev_button_cb (G_GNUC_UNUSED GtkWidget *button,
		SimulationState *state)
{
	GtkWidget *w;

	if (results_sim_index > current_sim->first_round)
		--results_sim_index;

	if (results_sim_index == current_sim->first_round) {
		w = go_gtk_builder_get_widget (state->gui, "prev-button");
		gtk_widget_set_sensitive (w, FALSE);
	}

	w = go_gtk_builder_get_widget (state->gui, "next-button");
	gtk_widget_set_sensitive (w, TRUE);
	update_results_view (current_sim);
}

static void
next_button_cb (G_GNUC_UNUSED GtkWidget *button,
		SimulationState *state)
{
	GtkWidget *w;

	if (results_sim_index < current_sim->last_round)
		++results_sim_index;

	if (results_sim_index == current_sim->last_round) {
		w = go_gtk_builder_get_widget (state->gui, "next-button");
		gtk_widget_set_sensitive (w, FALSE);
	}

	w = go_gtk_builder_get_widget (state->gui, "prev-button");
	gtk_widget_set_sensitive (w, TRUE);
	update_results_view (current_sim);
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
simulation_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			  SimulationState *state)
{
	data_analysis_output_t  dao;
	GtkWidget               *w;
	gchar const		*err;
	static simulation_t     sim;

	simulation_tool_destroy (current_sim);

	sim.inputs = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->input_entry), state->sheet);

	sim.outputs = gnm_expr_entry_parse_as_value
		(state->input_entry_2, state->sheet);

        parse_output ((GnmGenericToolState *) state, &dao);

	if (prepare_ranges (&sim)) {
		err = (gchar *) N_("Invalid variable range was given");
		goto out;
	}

	w = go_gtk_builder_get_widget (state->gui, "iterations");
	sim.n_iterations = gtk_spin_button_get_value (GTK_SPIN_BUTTON (w));

	w = go_gtk_builder_get_widget (state->gui, "first_round");
	sim.first_round = gtk_spin_button_get_value (GTK_SPIN_BUTTON (w)) - 1;

	w = go_gtk_builder_get_widget (state->gui, "last_round");
	sim.last_round = gtk_spin_button_get_value (GTK_SPIN_BUTTON (w)) - 1;

	if (sim.first_round > sim.last_round) {
		err = (gchar *) N_("First round number should be less than or "
				   "equal to the number of the last round."
				   );
		goto out;
	}

	current_sim = &sim;

	w = go_gtk_builder_get_widget (state->gui, "max-time");
	sim.max_time = gtk_spin_button_get_value (GTK_SPIN_BUTTON (w)) - 1;

	sim.start = g_get_monotonic_time ();
	err = simulation_tool (GNM_WBC (state->wbcg), &dao, &sim);
	sim.end = g_get_monotonic_time ();

	if (err == NULL) {
		results_sim_index = sim.first_round;
		update_log (state, &sim);
		update_results_view (&sim);

		if (sim.last_round > results_sim_index) {
			w = go_gtk_builder_get_widget (state->gui, "next-button");
			gtk_widget_set_sensitive (w, TRUE);
		}
	}
 out:
	value_release (sim.inputs);
	value_release (sim.outputs);

	if (err != NULL)
		error_in_entry ((GnmGenericToolState *) state,
				GTK_WIDGET (state->input_entry_2), _(err));
	return;
}


/**
 * cb_tool_close_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_tool_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
			GnmGenericToolState *state)
{
	simulation_tool_destroy (current_sim);
	gtk_widget_destroy (state->dialog);
}

static void
init_results_view (SimulationState *state)
{
	GtkTextView     *view;
	GtkTextTagTable *tag_table;

	tag_table      = gtk_text_tag_table_new ();
	results_buffer = gtk_text_buffer_new (tag_table);
	view = GTK_TEXT_VIEW (go_gtk_builder_get_widget (state->gui,
						    "results-view"));
	gtk_text_view_set_buffer (view, results_buffer);
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
dialog_simulation (WBCGtk *wbcg, G_GNUC_UNUSED Sheet *sheet)
{
        SimulationState *state;
	WorkbookControl *wbc;
	GtkWidget       *w;

	g_return_if_fail (wbcg != NULL);

	wbc = GNM_WBC (wbcg);

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, SIMULATION_KEY))
		return;

	state = g_new (SimulationState, 1);
	if (dialog_tool_init (state, wbcg, wb_control_cur_sheet (wbc),
			      GNUMERIC_HELP_LINK_SIMULATION,
			      "res:ui/simulation.ui", "Simulation",
			      _("Could not create the Simulation dialog."),
			      SIMULATION_KEY,
			      G_CALLBACK (simulation_ok_clicked_cb),
			      G_CALLBACK (cb_tool_cancel_clicked),
			      G_CALLBACK (simulation_update_sensitivity_cb),
			      0))
		return;

	init_results_view (state);
	current_sim = NULL;

	w = go_gtk_builder_get_widget (state->gui, "prev-button");
	gtk_widget_set_sensitive (w, FALSE);
	g_signal_connect_after (G_OBJECT (w), "clicked",
				G_CALLBACK (prev_button_cb), state);
	w = go_gtk_builder_get_widget (state->gui, "next-button");
	g_signal_connect_after (G_OBJECT (w), "clicked",
				G_CALLBACK (next_button_cb), state);
	gtk_widget_set_sensitive (w, FALSE);
	w = go_gtk_builder_get_widget (state->gui, "min-button");
	gtk_widget_set_sensitive (w, FALSE);
	gtk_widget_hide (w);
	w = go_gtk_builder_get_widget (state->gui, "max-button");
	gtk_widget_set_sensitive (w, FALSE);
	gtk_widget_hide (w);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog), wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	gnm_dao_set_put (GNM_DAO (state->gdao), FALSE, FALSE);
	simulation_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);
        return;
}
