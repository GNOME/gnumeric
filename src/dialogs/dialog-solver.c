/*
 * dialog-solver.c:
 *
 * Author:
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 *
 * (C) Copyright 2000, 2002 by Jukka-Pekka Iivonen <iivonen@iki.fi>
 * (C) Copyright 2009-2013 Morten Welinder (terra@gnome.org)
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
#include <func.h>
#include <dialogs/tool-dialogs.h>
#include <value.h>
#include <cell.h>
#include <sheet.h>
#include <sheet-view.h>
#include <expr.h>
#include <wbc-gtk.h>
#include <workbook.h>
#include <parse-util.h>
#include <ranges.h>
#include <commands.h>
#include <clipboard.h>
#include <selection.h>
#include <application.h>
#include <tools/gnm-solver.h>
#include <widgets/gnm-expr-entry.h>

#include <goffice/goffice.h>
#include <string.h>
#include <tools/scenarios.h>

#define SOLVER_KEY            "solver-dialog"

typedef struct {
	int                 ref_count;
	GtkBuilder          *gui;
	GtkWidget           *dialog;
	GtkWidget           *notebook;
	GnmExprEntry	    *target_entry;
	GnmExprEntry	    *change_cell_entry;
	GtkWidget           *max_iter_entry;
	GtkWidget           *max_time_entry;
	GtkWidget           *gradient_order_entry;
	GtkWidget           *solve_button;
	GtkWidget           *close_button;
	GtkWidget           *stop_button;
	GtkWidget           *help_button;
	GtkWidget           *add_button;
	GtkWidget           *change_button;
	GtkWidget           *delete_button;
	GtkWidget           *scenario_name_entry;
	struct {
		GnmExprEntry*entry;
		GtkWidget   *label;
	} lhs, rhs;
	GtkComboBox         *type_combo;
	GtkComboBox         *algorithm_combo;
	GtkTreeView         *constraint_list;
	GnmSolverConstraint *constr;
	GtkWidget           *warning_dialog;

	struct {
		GnmSolver   *solver;
		GtkWidget   *timer_widget;
		guint       timer_source;
		GtkWidget   *status_widget;
		GtkWidget   *problem_status_widget;
		GtkWidget   *objective_value_widget;
		guint       obj_val_source;
		GtkWidget   *spinner;
		guint        in_main;
	} run;

	Sheet		    *sheet;
	WBCGtk              *wbcg;

	GnmSolverParameters *orig_params;
} SolverState;


static char const * const problem_type_group[] = {
	"min_button",
	"max_button",
	"equal_to_button",
	NULL
};

static char const * const model_type_group[] = {
	"lp_model_button",
	"qp_model_button",
	"nlp_model_button",
	NULL
};

static void
constraint_fill (GnmSolverConstraint *c, SolverState *state)
{
	Sheet *sheet = state->sheet;

	c->type = gtk_combo_box_get_active (state->type_combo);

	gnm_solver_constraint_set_lhs
		(c,
		 gnm_expr_entry_parse_as_value (state->lhs.entry, sheet));

	gnm_solver_constraint_set_rhs
		(c,
		 gnm_solver_constraint_has_rhs (c)
		 ? gnm_expr_entry_parse_as_value (state->rhs.entry, sheet)
		 : NULL);
}

static gboolean
dialog_set_sec_button_sensitivity (G_GNUC_UNUSED GtkWidget *dummy,
				   SolverState *state)
{
	gboolean select_ready = (state->constr != NULL);
	GnmSolverConstraint *test = gnm_solver_constraint_new (NULL);
	gboolean ready, has_rhs;
	GnmSolverParameters const *param = state->sheet->solver_parameters;

	constraint_fill (test, state);
	ready = gnm_solver_constraint_valid (test, param);
	has_rhs = gnm_solver_constraint_has_rhs (test);
	gnm_solver_constraint_free (test);

	gtk_widget_set_sensitive (state->add_button, ready);
	gtk_widget_set_sensitive (state->change_button, select_ready && ready);
	gtk_widget_set_sensitive (state->delete_button, select_ready);
	gtk_widget_set_sensitive (GTK_WIDGET (state->rhs.entry), has_rhs);
	gtk_widget_set_sensitive (GTK_WIDGET (state->rhs.label), has_rhs);

	/* Return %TRUE iff the current constraint is valid.  */
	return ready;
}

static void
constraint_select_click (GtkTreeSelection *Selection,
			 SolverState * state)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	GnmSolverConstraint const *c;
	GnmValue const *lhs, *rhs;

	if (gtk_tree_selection_get_selected (Selection, &store, &iter))
		gtk_tree_model_get (store, &iter, 1, &state->constr, -1);
	else
		state->constr = NULL;
	dialog_set_sec_button_sensitivity (NULL, state);

	if (state->constr == NULL)
		return; /* Fail? */
	c = state->constr;

	lhs = gnm_solver_constraint_get_lhs (c);
	if (lhs) {
		GnmExprTop const *texpr =
			gnm_expr_top_new_constant (value_dup (lhs));
		GnmParsePos pp;

		gnm_expr_entry_load_from_expr
			(state->lhs.entry,
			 texpr,
			 parse_pos_init_sheet (&pp, state->sheet));
		gnm_expr_top_unref (texpr);
	} else
		gnm_expr_entry_load_from_text (state->lhs.entry, "");

	rhs = gnm_solver_constraint_get_rhs (c);
	if (rhs && gnm_solver_constraint_has_rhs (c)) {
		GnmExprTop const *texpr =
			gnm_expr_top_new_constant (value_dup (rhs));
		GnmParsePos pp;

		gnm_expr_entry_load_from_expr
			(state->rhs.entry,
			 texpr,
			 parse_pos_init_sheet (&pp, state->sheet));
		gnm_expr_top_unref (texpr);
	} else
		gnm_expr_entry_load_from_text (state->rhs.entry, "");

	gtk_combo_box_set_active (state->type_combo, c->type);
}

/**
 * cb_dialog_delete_clicked:
 * @button:
 * @state:
 *
 *
 **/
static void
cb_dialog_delete_clicked (G_GNUC_UNUSED GtkWidget *button, SolverState *state)
{
	if (state->constr != NULL) {
		GtkTreeIter iter;
		GtkTreeModel *store;
		GnmSolverParameters *param = state->sheet->solver_parameters;

		param->constraints =
			g_slist_remove (param->constraints, state->constr);
		gnm_solver_constraint_free (state->constr);
		state->constr = NULL;

		if (gtk_tree_selection_get_selected (
			    gtk_tree_view_get_selection (state->constraint_list),
			    &store, &iter))
			gtk_list_store_remove ((GtkListStore*)store, &iter);
	}
}

static void
constraint_fill_row (SolverState *state, GtkListStore *store, GtkTreeIter *iter)
{
	char         *text;
	GnmSolverConstraint *c = state->constr;

	constraint_fill (c, state);

	text = gnm_solver_constraint_as_str (c, state->sheet);
	gtk_list_store_set (store, iter, 0, text, 1, c, -1);
	g_free (text);
	gtk_tree_selection_select_iter (gtk_tree_view_get_selection (state->constraint_list), iter);
}

static void
cb_dialog_add_clicked (SolverState *state)
{
	if (dialog_set_sec_button_sensitivity (NULL, state)) {
		GtkTreeIter   iter;
		GtkListStore *store = GTK_LIST_STORE (gtk_tree_view_get_model (state->constraint_list));
		GnmSolverParameters *param = state->sheet->solver_parameters;

		gtk_list_store_append (store, &iter);
		state->constr = gnm_solver_constraint_new (state->sheet);
		constraint_fill_row (state, store, &iter);
		param->constraints =
			g_slist_append (param->constraints, state->constr);
	}
}

static void
cb_dialog_change_clicked (GtkWidget *button, SolverState *state)
{
	if (state->constr != NULL) {
		GtkTreeIter iter;
		GtkTreeModel *store;

		if (gtk_tree_selection_get_selected (
			    gtk_tree_view_get_selection (state->constraint_list),
			    &store, &iter))
			constraint_fill_row (state, (GtkListStore *)store, &iter);
	}
}

static void
dialog_set_main_button_sensitivity (G_GNUC_UNUSED GtkWidget *dummy,
				    SolverState *state)
{
	gboolean ready;

	ready = gnm_expr_entry_is_cell_ref (state->target_entry, state->sheet,
					    FALSE)
		&& gnm_expr_entry_is_cell_ref (state->change_cell_entry,
					       state->sheet, TRUE);
	gtk_widget_set_sensitive (state->solve_button, ready);
}

static gboolean
fill_algorithm_combo (SolverState *state, GnmSolverModelType type)
{
	GtkListStore *store =
		gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_POINTER);
	GSList *solvers, *l;
	int sel = 0, i;
	GnmSolverParameters *param =state->sheet->solver_parameters;

	gtk_combo_box_set_model (state->algorithm_combo, GTK_TREE_MODEL (store));

	l = NULL;
	for (solvers = gnm_solver_db_get (); solvers; solvers = solvers->next) {
		GnmSolverFactory *entry = solvers->data;
		if (type != entry->type)
			continue;
		l = g_slist_prepend (l, entry);
	}
	solvers = g_slist_reverse (l);

	gtk_widget_set_sensitive (GTK_WIDGET (state->solve_button),
				  solvers != NULL);
	if (!solvers)
		return FALSE;

	for (l = solvers, i = 0; l; l = l->next, i++) {
		GnmSolverFactory *factory = l->data;
		GtkTreeIter iter;

		if (param->options.algorithm == factory)
			sel = i;

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
				    0, factory->name,
				    1, factory,
				    -1);
	}
	g_slist_free (solvers);

	gtk_combo_box_set_active (state->algorithm_combo, sel);

	g_object_unref (store);

	return TRUE;
}

static void
cb_dialog_model_type_clicked (G_GNUC_UNUSED GtkWidget *button,
			      SolverState *state)
{
	GnmSolverModelType type;
	gboolean any;

	type = gnm_gui_group_value (state->gui, model_type_group);
	any = fill_algorithm_combo (state, type);

	if (!any) {
		go_gtk_notice_nonmodal_dialog
			(GTK_WINDOW (state->dialog),
			 &(state->warning_dialog),
			 GTK_MESSAGE_INFO,
			 _("Looking for a subject for your thesis? "
			   "Maybe you would like to write a solver for "
			   "Gnumeric?"));
	}
}

static void
unref_state (SolverState *state)
{
	state->ref_count--;
	if (state->ref_count > 0)
		return;

	if (state->orig_params)
		g_object_unref (state->orig_params);
	g_free (state);
}

static GOUndo *
set_params (Sheet *sheet, GnmSolverParameters *params)
{
	return go_undo_binary_new
		(sheet, g_object_ref (params),
		 (GOUndoBinaryFunc)gnm_sheet_set_solver_params,
		 NULL, g_object_unref);
}

#define GET_BOOL_ENTRY(name_, field_)					\
	do {								\
		GtkWidget *w_ = go_gtk_builder_get_widget (state->gui, (name_)); \
		param->field_ = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w_)); \
	} while (0)

static void
extract_settings (SolverState *state)
{
	GnmSolverParameters *param = state->sheet->solver_parameters;
	GtkTreeIter iter;
	GnmValue *target_range;
	GnmValue *input_range;
	GnmSolverFactory *factory = NULL;

	target_range = gnm_expr_entry_parse_as_value (state->target_entry,
						      state->sheet);
	input_range = gnm_expr_entry_parse_as_value (state->change_cell_entry,
						     state->sheet);

	gnm_solver_param_set_input (param, input_range);

	gnm_solver_param_set_target (param,
				     target_range
				     ? &target_range->v_range.cell.a
				     : NULL);

	param->problem_type =
		gnm_gui_group_value (state->gui, problem_type_group);
	param->options.model_type =
		gnm_gui_group_value (state->gui, model_type_group);

	if (gtk_combo_box_get_active_iter (state->algorithm_combo, &iter)) {
		gtk_tree_model_get (gtk_combo_box_get_model (state->algorithm_combo),
				    &iter, 1, &factory, -1);
		gnm_solver_param_set_algorithm (param, factory);
	} else
		gnm_solver_param_set_algorithm (param, NULL);

	param->options.max_iter = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->max_iter_entry));
	param->options.max_time_sec = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->max_time_entry));
	param->options.gradient_order = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->gradient_order_entry));

	GET_BOOL_ENTRY ("autoscale_button", options.automatic_scaling);
	GET_BOOL_ENTRY ("non_neg_button", options.assume_non_negative);
	GET_BOOL_ENTRY ("all_int_button", options.assume_discrete);
	GET_BOOL_ENTRY ("program", options.program_report);
	GET_BOOL_ENTRY ("sensitivity", options.sensitivity_report);

	g_free (param->options.scenario_name);
	param->options.scenario_name = g_strdup
		(gtk_entry_get_text (GTK_ENTRY (state->scenario_name_entry)));

	GET_BOOL_ENTRY ("optimal_scenario", options.add_scenario);

	value_release (target_range);
}

#undef GET_BOOL_ENTRY

static void
check_for_changed_options (SolverState *state)
{
	Sheet *sheet = state->sheet;

	if (!gnm_solver_param_equal (sheet->solver_parameters,
				     state->orig_params)) {
		GOUndo *undo = set_params (sheet, state->orig_params);
		GOUndo *redo = set_params (sheet, sheet->solver_parameters);
		cmd_generic (GNM_WBC (state->wbcg),
			     _("Changing solver parameters"),
			     undo, redo);

		g_object_unref (state->orig_params);
		state->orig_params =
			gnm_solver_param_dup (sheet->solver_parameters,
					      sheet);
	}
}

static void
cb_dialog_solver_destroy (SolverState *state)
{
	g_return_if_fail (state != NULL);

	if (state->run.solver) {
		gnm_solver_stop (state->run.solver, NULL);
		g_object_set (state->run.solver, "result", NULL, NULL);
	}

	extract_settings (state);

	check_for_changed_options (state);

	if (state->gui != NULL) {
		g_object_unref (state->gui);
		state->gui = NULL;
	}

	wbcg_edit_finish (state->wbcg, WBC_EDIT_REJECT, NULL);

	state->dialog = NULL;
}

static void
cb_dialog_close_clicked (G_GNUC_UNUSED GtkWidget *button,
			 SolverState *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_stop_solver (SolverState *state)
{
	GnmSolver *sol = state->run.solver;

	switch (sol->status) {
	case GNM_SOLVER_STATUS_RUNNING: {
		gboolean ok = gnm_solver_stop (sol, NULL);
		if (!ok) {
			g_warning ("Failed to stop solver!");
		}
		g_object_set (sol, "result", NULL, NULL);
		break;
	}

	default:
		break;
	}
}

static void
remove_timer_source (SolverState *state)
{
	if (state->run.timer_source) {
		g_source_remove (state->run.timer_source);
		state->run.timer_source = 0;
	}
}

static void
remove_objective_value_source (SolverState *state)
{
	if (state->run.obj_val_source) {
		g_source_remove (state->run.obj_val_source);
		state->run.obj_val_source = 0;
	}
}

static void
update_obj_value (SolverState *state)
{
	GnmSolver *sol = state->run.solver;
	GnmSolverResult *r = sol->result;
	char *valtxt;
	const char *txt;

	switch (r ? r->quality : GNM_SOLVER_RESULT_NONE) {
	default:
	case GNM_SOLVER_RESULT_NONE:
		txt = "";
		break;

	case GNM_SOLVER_RESULT_FEASIBLE:
		txt = _("Feasible");
		break;

	case GNM_SOLVER_RESULT_OPTIMAL:
		txt = _("Optimal");
		break;

	case GNM_SOLVER_RESULT_INFEASIBLE:
		txt = _("Infeasible");
		break;

	case GNM_SOLVER_RESULT_UNBOUNDED:
		txt = _("Unbounded");
		break;
	}
	gtk_label_set_text (GTK_LABEL (state->run.problem_status_widget), txt);

	if (gnm_solver_has_solution (sol)) {
		txt = valtxt = gnm_format_value (go_format_general (),
						 r->value);
	} else {
		valtxt = NULL;
		txt = "";
	}

	gtk_label_set_text (GTK_LABEL (state->run.objective_value_widget),
			    txt);
	g_free (valtxt);

	remove_objective_value_source (state);
}

static void
cb_notify_status (SolverState *state)
{
	GnmSolver *sol = state->run.solver;
	const char *text;
	gboolean finished = gnm_solver_finished (sol);
	gboolean running = FALSE;

	switch (sol->status) {
	case GNM_SOLVER_STATUS_READY:
		text = _("Ready");
		break;
	case GNM_SOLVER_STATUS_PREPARING:
		text = _("Preparing");
		break;
	case GNM_SOLVER_STATUS_PREPARED:
		text = _("Prepared");
		break;
	case GNM_SOLVER_STATUS_RUNNING:
		text = _("Running");
		running = TRUE;
		break;
	case GNM_SOLVER_STATUS_DONE:
		text = _("Done");
		break;
	default:
	case GNM_SOLVER_STATUS_ERROR:
		text = _("Error");
		break;
	case GNM_SOLVER_STATUS_CANCELLED:
		text = _("Cancelled");
		break;
	}

	if (sol->reason) {
		char *text2 = g_strconcat (text,
					   " (", sol->reason, ")",
					   NULL);
		gtk_label_set_text (GTK_LABEL (state->run.status_widget),
				    text2);
		g_free (text2);
	} else {
		gtk_label_set_text (GTK_LABEL (state->run.status_widget), text);
	}

	gtk_widget_set_visible (state->run.spinner, running);
	gtk_widget_set_visible (state->stop_button, !finished);
	gtk_widget_set_sensitive (state->solve_button, finished);
	gtk_widget_set_sensitive (state->close_button, finished);

	if (state->run.obj_val_source)
		update_obj_value (state);

	if (finished) {
		remove_timer_source (state);
		if (state->run.in_main)
			gtk_main_quit ();
	}
}

static gboolean
cb_obj_val_tick (SolverState *state)
{
	state->run.obj_val_source = 0;
	update_obj_value (state);
	return FALSE;
}

static void
cb_notify_result (SolverState *state)
{
	if (state->run.obj_val_source == 0)
		state->run.obj_val_source = g_timeout_add
			(100, (GSourceFunc)cb_obj_val_tick, state);
}

static gboolean
cb_timer_tick (SolverState *state)
{
	GnmSolver *sol = state->run.solver;
	double dsecs = gnm_solver_elapsed (sol);
	int secs = (int)CLAMP (dsecs, 0, INT_MAX);
	int hh = secs / 3600;
	int mm = secs / 60 % 60;
	int ss = secs % 60;
	char *txt = hh
		? g_strdup_printf ("%02d:%02d:%02d", hh, mm, ss)
		: g_strdup_printf ("%02d:%02d", mm, ss);

	gtk_label_set_text (GTK_LABEL (state->run.timer_widget), txt);
	g_free (txt);

	if (gnm_solver_check_timeout (sol)) {
		cb_notify_status (state);
	}

	return TRUE;
}

static void
create_report (GnmSolver *sol, SolverState *state)
{
	Sheet *sheet = state->sheet;
	char *base = g_strdup_printf (_("%s %%s Report"), sheet->name_unquoted);
	gnm_solver_create_report (sol, base);
	g_free (base);
}


static GnmSolverResult *
run_solver (SolverState *state, GnmSolverParameters *param)
{
	GError *err = NULL;
	gboolean ok;
	GnmSheetRange sr;
	GOUndo *undo = NULL;
	GnmSolver *sol = NULL;
	GnmValue const *vinput;
	GtkWindow *top = GTK_WINDOW (gtk_widget_get_toplevel (state->dialog));
	GnmSolverResult *res = NULL;

	state->ref_count++;

	sol = gnm_solver_factory_functional (param->options.algorithm,
					     state->wbcg)
		? gnm_solver_factory_create (param->options.algorithm, param)
		: NULL;
	if (!sol) {
		go_gtk_notice_dialog (top, GTK_MESSAGE_ERROR,
				      _("The chosen solver is not functional."));
		goto fail;
	}

	gtk_notebook_set_current_page (GTK_NOTEBOOK (state->notebook), -1);

	state->run.solver = sol;

	vinput = gnm_solver_param_get_input (param);
	gnm_sheet_range_from_value (&sr, vinput);
	if (!sr.sheet) sr.sheet = param->sheet;
	undo = clipboard_copy_range_undo (sr.sheet, &sr.range);

	g_signal_connect_swapped (G_OBJECT (sol),
				  "notify::status",
				  G_CALLBACK (cb_notify_status),
				  state);
	g_signal_connect_swapped (G_OBJECT (sol),
				  "notify::reason",
				  G_CALLBACK (cb_notify_status),
				  state);
	cb_notify_status (state);

	g_signal_connect_swapped (G_OBJECT (sol),
				  "notify::result",
				  G_CALLBACK (cb_notify_result),
				  state);
	cb_notify_result (state);

	state->run.timer_source = g_timeout_add_seconds
		(1, (GSourceFunc)cb_timer_tick, state);
	cb_timer_tick (state);

	/* ---------------------------------------- */

	ok = gnm_solver_start (sol,
			       GNM_WBC (state->wbcg),
			       &err);
	if (ok) {
		state->run.in_main++;
		go_cmd_context_set_sensitive (GO_CMD_CONTEXT (state->wbcg), FALSE);
		gtk_main ();
		go_cmd_context_set_sensitive (GO_CMD_CONTEXT (state->wbcg), TRUE);
		state->run.in_main--;
		ok = gnm_solver_has_solution (sol);
	} else if (err) {
		gnm_solver_set_reason (sol, err->message);
	}
	g_clear_error (&err);

	remove_objective_value_source (state);
	remove_timer_source (state);

	/* ---------------------------------------- */

	if (ok) {
		GOUndo *redo;

		gnm_solver_store_result (sol);
		redo = clipboard_copy_range_undo (sr.sheet, &sr.range);

		if (param->options.program_report ||
		    param->options.sensitivity_report) {
			Workbook *wb = param->sheet->workbook;
			GOUndo *undo_report, *redo_report;

			undo_report = go_undo_binary_new
				(wb,
				 workbook_sheet_state_new (wb),
				 (GOUndoBinaryFunc)workbook_sheet_state_restore,
				 NULL,
				 (GFreeFunc)workbook_sheet_state_unref);
			undo = go_undo_combine (undo, undo_report);

			create_report (sol, state);

			redo_report = go_undo_binary_new
				(wb,
				 workbook_sheet_state_new (wb),
				 (GOUndoBinaryFunc)workbook_sheet_state_restore,
				 NULL,
				 (GFreeFunc)workbook_sheet_state_unref);
			redo = go_undo_combine (redo, redo_report);
		}

		cmd_generic (GNM_WBC (state->wbcg),
			     _("Running solver"),
			     undo, redo);
		res = g_object_ref (sol->result);
		undo = redo = NULL;
	}

fail:
	if (undo)
		g_object_unref (undo);

	if (state->run.solver) {
		g_object_unref (state->run.solver);
		state->run.solver = NULL;
	}

	unref_state (state);

	return res;
}


static void
solver_add_scenario (SolverState *state, GnmSolverResult *res, gchar const *name)
{
	GnmSolverParameters *param = state->sheet->solver_parameters;
	GnmValue const *vinput;
	GnmScenario *sc;
	GnmSheetRange sr;
	WorkbookControl *wbc = GNM_WBC (state->wbcg);

	vinput = gnm_solver_param_get_input (param);
	gnm_sheet_range_from_value (&sr, vinput);

	sc = gnm_sheet_scenario_new (param->sheet, name);
	switch (res->quality) {
	case GNM_SOLVER_RESULT_OPTIMAL:
		gnm_scenario_set_comment
			(sc, _("Optimal solution created by solver.\n"));
		break;
	case GNM_SOLVER_RESULT_FEASIBLE:
		gnm_scenario_set_comment
			(sc, _("Feasible solution created by solver.\n"));
		break;
	default:
		break;
	}
	gnm_scenario_add_area (sc, &sr);

	cmd_scenario_add (wbc, sc, sc->sheet);
}

/**
 * cb_dialog_solve_clicked:
 * @button:
 * @state:
 *
 *
 **/
static void
cb_dialog_solve_clicked (G_GNUC_UNUSED GtkWidget *button,
			 SolverState *state)
{
	GnmSolverResult *res;
	GnmSolverParameters *param = state->sheet->solver_parameters;
	GError *err = NULL;

	if (state->warning_dialog != NULL) {
		gtk_widget_destroy (state->warning_dialog);
		state->warning_dialog = NULL;
	}

	extract_settings (state);

	if (!gnm_solver_param_valid (param, &err)) {
		GtkWidget *top = gtk_widget_get_toplevel (state->dialog);
		go_gtk_notice_dialog (GTK_WINDOW (top), GTK_MESSAGE_ERROR,
				      "%s", err->message);
		goto out;
	}

	check_for_changed_options (state);

	res = run_solver (state, param);

	gnm_app_recalc ();

	if (res != NULL) {
		if ((res->quality == GNM_SOLVER_RESULT_OPTIMAL ||
		     res->quality == GNM_SOLVER_RESULT_FEASIBLE) &&
		    param->options.add_scenario)
			solver_add_scenario (state, res,
					     param->options.scenario_name);

		g_object_unref (res);
	} else if (err) {
		go_gtk_notice_nonmodal_dialog
			(GTK_WINDOW (state->dialog),
			 &(state->warning_dialog),
			 GTK_MESSAGE_ERROR,
			 "%s", err->message);
	}

 out:
	if (err)
		g_error_free (err);
}

#define INIT_BOOL_ENTRY(name_, field_)					\
do {									\
	GtkWidget *w_ = go_gtk_builder_get_widget (state->gui, (name_));	\
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w_),		\
				      param->field_);			\
} while (0)


/**
 * dialog_solver_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_solver_init (SolverState *state)
{
	GtkGrid *grid;
	GnmSolverParameters *param;
	GtkCellRenderer *renderer;
	GtkListStore *store;
	GtkTreeViewColumn *column;
	GSList *cl;
	GnmCell *target_cell;
	GnmValue const *input;
	int i;

	param = state->sheet->solver_parameters;

	state->gui = gnm_gtk_builder_load ("res:ui/solver.ui", NULL, GO_CMD_CONTEXT (state->wbcg));
        if (state->gui == NULL)
                return TRUE;

	state->dialog = go_gtk_builder_get_widget (state->gui, "Solver");
        if (state->dialog == NULL)
                return TRUE;

	state->notebook = go_gtk_builder_get_widget (state->gui, "solver_notebook");

	/*  buttons  */
	state->solve_button  = go_gtk_builder_get_widget (state->gui, "solvebutton");
	g_signal_connect (G_OBJECT (state->solve_button), "clicked",
			  G_CALLBACK (cb_dialog_solve_clicked), state);

	state->close_button  = go_gtk_builder_get_widget (state->gui, "closebutton");
	g_signal_connect (G_OBJECT (state->close_button), "clicked",
			  G_CALLBACK (cb_dialog_close_clicked), state);

	state->help_button = go_gtk_builder_get_widget (state->gui, "helpbutton");
	gnm_init_help_button (state->help_button, GNUMERIC_HELP_LINK_SOLVER);

	state->add_button  = go_gtk_builder_get_widget (state->gui, "addbutton");
	gtk_button_set_alignment (GTK_BUTTON (state->add_button), 0.5, .5);
	g_signal_connect_swapped (G_OBJECT (state->add_button), "clicked",
		G_CALLBACK (cb_dialog_add_clicked), state);

	state->change_button = go_gtk_builder_get_widget (state->gui,
						     "changebutton");
	g_signal_connect (G_OBJECT (state->change_button), "clicked",
			  G_CALLBACK (cb_dialog_change_clicked), state);

	state->delete_button = go_gtk_builder_get_widget (state->gui,
						     "deletebutton");
	gtk_button_set_alignment (GTK_BUTTON (state->delete_button), 0.5, .5);
	g_signal_connect (G_OBJECT (state->delete_button), "clicked",
			  G_CALLBACK (cb_dialog_delete_clicked), state);

	state->stop_button = go_gtk_builder_get_widget (state->gui, "stopbutton");
	g_signal_connect_swapped (G_OBJECT (state->stop_button),
				  "clicked", G_CALLBACK (cb_stop_solver),
				  state);

	/* target_entry */
	grid = GTK_GRID (go_gtk_builder_get_widget (state->gui,
						 "parameter-grid"));
	state->target_entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->target_entry,
		GNM_EE_SINGLE_RANGE |
		GNM_EE_FORCE_ABS_REF |
		GNM_EE_SHEET_OPTIONAL, GNM_EE_MASK);
	gtk_widget_set_hexpand (GTK_WIDGET (state->target_entry), TRUE);
	gtk_grid_attach (grid, GTK_WIDGET (state->target_entry), 1, 0, 2, 1);
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->target_entry));
	gtk_widget_show (GTK_WIDGET (state->target_entry));
	g_signal_connect_after (G_OBJECT (state->target_entry),	"changed",
			G_CALLBACK (dialog_set_main_button_sensitivity),
				state);

	/* change_cell_entry */
	state->change_cell_entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->change_cell_entry,
		GNM_EE_SINGLE_RANGE |
		GNM_EE_FORCE_ABS_REF |
		GNM_EE_SHEET_OPTIONAL, GNM_EE_MASK);
	gtk_widget_set_hexpand (GTK_WIDGET (state->change_cell_entry), TRUE);
	gtk_grid_attach (grid,
	                 GTK_WIDGET (state->change_cell_entry), 1, 2, 2, 1);
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->change_cell_entry));
	gtk_widget_show (GTK_WIDGET (state->change_cell_entry));
	g_signal_connect_after (G_OBJECT (state->change_cell_entry), "changed",
		G_CALLBACK (dialog_set_main_button_sensitivity), state);

	/* Algorithm */
	state->algorithm_combo = GTK_COMBO_BOX
		(go_gtk_builder_get_widget (state->gui, "algorithm_combo"));
	renderer = (GtkCellRenderer*) gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (state->algorithm_combo), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (state->algorithm_combo), renderer,
					"text", 0,
					NULL);
	fill_algorithm_combo (state, param->options.model_type);

	for (i = 0; model_type_group[i]; i++) {
		const char *bname = model_type_group[i];
		GtkWidget *w = go_gtk_builder_get_widget(state->gui, bname);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w),
					      param->options.model_type ==
					      (GnmSolverModelType)i);
		g_signal_connect (G_OBJECT (w), "clicked",
				  G_CALLBACK (cb_dialog_model_type_clicked), state);
	}

	/* Options */
	state->max_iter_entry = go_gtk_builder_get_widget (state->gui,
						      "max_iter_entry");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->max_iter_entry),
				   param->options.max_iter);

	state->max_time_entry = go_gtk_builder_get_widget (state->gui,
						      "max_time_entry");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->max_time_entry),
				   param->options.max_time_sec);

	state->gradient_order_entry = go_gtk_builder_get_widget (state->gui,
								 "gradient_order_entry");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->gradient_order_entry),
				   param->options.gradient_order);

	/* lhs_entry */
	grid = GTK_GRID (go_gtk_builder_get_widget (state->gui,
	                                            "constraints-grid"));
	state->lhs.entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->lhs.entry,
		GNM_EE_SINGLE_RANGE |
		GNM_EE_FORCE_ABS_REF |
		GNM_EE_SHEET_OPTIONAL, GNM_EE_MASK);
	gtk_widget_set_hexpand (GTK_WIDGET (state->lhs.entry), TRUE);
	gtk_grid_attach (grid, GTK_WIDGET (state->lhs.entry), 0, 4, 1, 1);
	state->lhs.label = go_gtk_builder_get_widget (state->gui, "lhs_label");
	gtk_label_set_mnemonic_widget (GTK_LABEL (state->lhs.label),
		GTK_WIDGET (state->lhs.entry));
	gtk_widget_show (GTK_WIDGET (state->lhs.entry));
	g_signal_connect_after (G_OBJECT (state->lhs.entry),
		"changed",
		G_CALLBACK (dialog_set_sec_button_sensitivity), state);
	g_signal_connect_swapped (
		gnm_expr_entry_get_entry (GNM_EXPR_ENTRY (state->lhs.entry)),
		"activate", G_CALLBACK (cb_dialog_add_clicked), state);

	/* rhs_entry */
	state->rhs.entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->rhs.entry,
				  GNM_EE_SINGLE_RANGE |
				  GNM_EE_FORCE_ABS_REF |
				  GNM_EE_SHEET_OPTIONAL |
				  GNM_EE_CONSTANT_ALLOWED,
				  GNM_EE_MASK);
	gtk_widget_set_hexpand (GTK_WIDGET (state->rhs.entry), TRUE);
	gtk_grid_attach (grid, GTK_WIDGET (state->rhs.entry), 2, 4, 1, 1);
	gtk_widget_show (GTK_WIDGET (state->rhs.entry));
	state->rhs.label = go_gtk_builder_get_widget (state->gui, "rhs_label");
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (state->rhs.label), GTK_WIDGET (state->rhs.entry));
	g_signal_connect_after (G_OBJECT (state->rhs.entry),
		"changed",
		G_CALLBACK (dialog_set_sec_button_sensitivity), state);
	g_signal_connect_swapped (
		gnm_expr_entry_get_entry (GNM_EXPR_ENTRY (state->rhs.entry)),
		"activate", G_CALLBACK (cb_dialog_add_clicked), state);

	/* type_menu */
	state->type_combo = GTK_COMBO_BOX
		(go_gtk_builder_get_widget (state->gui, "type_menu"));
	gtk_combo_box_set_active (state->type_combo, 0);
	g_signal_connect (state->type_combo, "changed",
			  G_CALLBACK (dialog_set_sec_button_sensitivity),
			  state);

	/* constraint_list */
	state->constraint_list = GTK_TREE_VIEW (go_gtk_builder_get_widget
					    (state->gui, "constraint_list"));

	state->constr = NULL;
	g_signal_connect (G_OBJECT (gtk_tree_view_get_selection (state->constraint_list)), "changed",
			  G_CALLBACK (constraint_select_click), state);
	gtk_tree_view_set_reorderable (state->constraint_list, TRUE);
	store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_POINTER);
	gtk_tree_view_set_model (state->constraint_list, GTK_TREE_MODEL(store));
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (
			_("Subject to the Constraints:"),
			renderer, "text", 0, NULL);
	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_append_column (state->constraint_list, column);

	{
		GtkWidget *w = GTK_WIDGET (state->constraint_list);
		int width, height, vsep;
		PangoLayout *layout =
			gtk_widget_create_pango_layout (w, "Mg19");

		gtk_widget_style_get (w,
				      "vertical_separator", &vsep,
				      NULL);

		pango_layout_get_pixel_size (layout, &width, &height);
		gtk_widget_set_size_request (w,
					     -1,
					     (2 * height + vsep) * (4 + 1));
		g_object_unref (layout);
	}

/* Loading the old solver specs... from param  */

	for (cl = param->constraints; cl; cl = cl->next) {
		GnmSolverConstraint const *c = cl->data;
		GtkTreeIter iter;
		char *str;

		gtk_list_store_append (store, &iter);
		str = gnm_solver_constraint_as_str (c, state->sheet);
		gtk_list_store_set (store, &iter, 0, str, 1, c, -1);
		g_free (str);
	}
	g_object_unref (store);

	INIT_BOOL_ENTRY ("autoscale_button", options.automatic_scaling);
	INIT_BOOL_ENTRY ("non_neg_button", options.assume_non_negative);
	INIT_BOOL_ENTRY ("all_int_button", options.assume_discrete);
	INIT_BOOL_ENTRY ("program", options.program_report);
	INIT_BOOL_ENTRY ("sensitivity", options.sensitivity_report);

	input = gnm_solver_param_get_input (param);
	if (input != NULL)
		gnm_expr_entry_load_from_text (state->change_cell_entry,
					       value_peek_string (input));
	target_cell = gnm_solver_param_get_target_cell (param);
	if (target_cell)
		gnm_expr_entry_load_from_text (state->target_entry,
					       cell_name (target_cell));
	else {
		SheetView *sv = wb_control_cur_sheet_view
			(GNM_WBC (state->wbcg));
		if (sv) {
			GnmRange first = {sv->edit_pos, sv->edit_pos};
			gnm_expr_entry_load_from_range (state->target_entry,
							state->sheet, &first);
		}
	}

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		go_gtk_builder_get_widget(state->gui, "max_button")),
			param->problem_type == GNM_SOLVER_MAXIMIZE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		go_gtk_builder_get_widget(state->gui, "min_button")),
			param->problem_type == GNM_SOLVER_MINIMIZE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		go_gtk_builder_get_widget(state->gui, "no_scenario")),
			! param->options.add_scenario);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		go_gtk_builder_get_widget(state->gui, "optimal_scenario")),
			param->options.add_scenario);

	state->scenario_name_entry = go_gtk_builder_get_widget
		(state->gui, "scenario_name_entry");
	gtk_entry_set_text (GTK_ENTRY (state->scenario_name_entry),
			    param->options.scenario_name);

	state->run.status_widget = go_gtk_builder_get_widget (state->gui, "solver_status_label");
	state->run.problem_status_widget = go_gtk_builder_get_widget (state->gui, "problem_status_label");
	state->run.objective_value_widget = go_gtk_builder_get_widget (state->gui, "objective_value_label");
	state->run.timer_widget = go_gtk_builder_get_widget (state->gui, "elapsed_time_label");
	state->run.spinner = go_gtk_builder_get_widget (state->gui, "run_spinner");


/* Done */
	gnm_expr_entry_grab_focus (state->target_entry, FALSE);
	wbcg_set_entry (state->wbcg, state->target_entry);

	dialog_set_main_button_sensitivity (NULL, state);
	dialog_set_sec_button_sensitivity (NULL, state);

/* dialog */
	wbc_gtk_attach_guru (state->wbcg, state->dialog);

	g_signal_connect_swapped (G_OBJECT (state->dialog),
				  "destroy",
				  G_CALLBACK (cb_dialog_solver_destroy),
				  state);
	g_object_set_data_full (G_OBJECT (state->dialog),
				"state", state,
				(GDestroyNotify)unref_state);

	return FALSE;
}

/**
 * dialog_solver:
 * @wbcg:
 * @sheet:
 *
 * Create the dialog (guru).
 *
 **/
void
dialog_solver (WBCGtk *wbcg, Sheet *sheet)
{
        SolverState *state;
	GnmSolverParameters *old_params = sheet->solver_parameters;
	gboolean got_it;
	int pass;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, SOLVER_KEY))
		return;

	/*
	 * First time around, pick a functional algorithm preferably one we
	 * can determine is functional without asking the user anything.
	 */
	got_it = gnm_solver_factory_functional (old_params->options.algorithm,
						NULL);
	for (pass = 1; !got_it && pass <= 2; pass++) {
		GSList *l;
		WBCGtk *wbcg2 = pass == 2 ? wbcg : NULL;

		for (l = gnm_solver_db_get (); l; l = l->next) {
			GnmSolverFactory *factory = l->data;
			if (old_params->options.model_type != factory->type)
				continue;
			if (gnm_solver_factory_functional (factory, wbcg2)) {
				got_it = TRUE;
				gnm_solver_param_set_algorithm (old_params,
								factory);
				break;
			}
		}
	}

	state                 = g_new0 (SolverState, 1);
	state->ref_count      = 1;
	state->wbcg           = wbcg;
	state->sheet          = sheet;
	state->warning_dialog = NULL;
	state->orig_params = gnm_solver_param_dup (sheet->solver_parameters,
						   sheet);

	if (dialog_solver_init (state)) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				 _("Could not create the Solver dialog."));
		unref_state (state);
		return;
	}

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog), wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	gnm_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			  SOLVER_KEY);

	gtk_widget_show (state->dialog);
}
