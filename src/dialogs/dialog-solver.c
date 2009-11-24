/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-solver.c:
 *
 * Author:
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 *
 * (C) Copyright 2000, 2002 by Jukka-Pekka Iivonen <iivonen@iki.fi>
 * (C) Copyright 2009 Morten Welinder (terra@gnome.org)
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
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <gui-util.h>
#include <func.h>
#include <tool-dialogs.h>
#include <value.h>
#include <cell.h>
#include <sheet.h>
#include <expr.h>
#include <wbc-gtk.h>
#include <workbook.h>
#include <parse-util.h>
#include <ranges.h>
#include <commands.h>
#include <clipboard.h>
#include <tools/gnm-solver.h>
#include <widgets/gnumeric-expr-entry.h>

#include <glade/glade.h>
#include <gtk/gtk.h>
#include <goffice/goffice.h>
#include <string.h>
#include <scenarios.h>

#define SOLVER_KEY            "solver-dialog"

typedef struct {
	GladeXML            *gui;
	GtkWidget           *dialog;
	GnmExprEntry	    *target_entry;
	GnmExprEntry	    *change_cell_entry;
	GtkWidget           *max_iter_entry;
	GtkWidget           *max_time_entry;
	GtkWidget           *solve_button;
	GtkWidget           *close_button;
	GtkWidget           *add_button;
	GtkWidget           *change_button;
	GtkWidget           *delete_button;
	GtkWidget           *model_button;
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
		GtkDialog   *dialog;
		GtkWidget   *timer_widget;
		guint       timer_source;
		time_t      timer_start;
		GtkWidget   *status_widget;
		GtkWidget   *result_widget;
		GtkWidget   *stop_button;
		GtkWidget   *ok_button;
		gulong       sig_notify_result, sig_notify_status;
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

	/* Return TRUE iff the current constraint is valid.  */
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

	return TRUE;
}

static void
cb_dialog_model_type_clicked (G_GNUC_UNUSED GtkWidget *button,
			      SolverState *state)
{
	GnmSolverModelType type;
	gboolean any;

	type = gnumeric_glade_group_value (state->gui, model_type_group);
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
free_state (SolverState *state)
{
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
do {									\
	GtkWidget *w_ = glade_xml_get_widget (state->gui, (name_));	\
	param->field_ = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w_)); \
} while (0)

static void
extract_settings (SolverState *state)
{
	GnmSolverParameters *param = state->sheet->solver_parameters;
	GtkTreeIter iter;
	GnmCell *target_cell;
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
	target_cell = gnm_solver_param_get_target_cell (param);

	param->problem_type =
		gnumeric_glade_group_value (state->gui, problem_type_group);
	param->options.model_type =
		gnumeric_glade_group_value (state->gui, model_type_group);

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

	GET_BOOL_ENTRY ("autoscale_button", options.automatic_scaling);
	GET_BOOL_ENTRY ("non_neg_button", options.assume_non_negative);
	GET_BOOL_ENTRY ("all_int_button", options.assume_discrete);
	GET_BOOL_ENTRY ("program", options.program_report);

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
		cmd_solver (WORKBOOK_CONTROL (state->wbcg),
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

	extract_settings (state);

	check_for_changed_options (state);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
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
cb_notify_status (SolverState *state)
{
	GnmSolver *sol = state->run.solver;
	const char *text;
	gboolean finished = gnm_solver_finished (sol);
	gboolean ok_ok = finished;

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
		if (sol->result) {
			GnmSolverResultQuality q = sol->result->quality;
			if (q == GNM_SOLVER_RESULT_FEASIBLE ||
			    q == GNM_SOLVER_RESULT_OPTIMAL)
				ok_ok = TRUE;
		}
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

	gtk_label_set_text (GTK_LABEL (state->run.status_widget), text);

	if (finished) {
		if (state->run.timer_source) {
			g_source_remove (state->run.timer_source);
			state->run.timer_source = 0;
		}
	}

	gtk_widget_set_sensitive (state->run.stop_button, !finished);
	gtk_widget_set_sensitive (state->run.ok_button, ok_ok);
}

static void
cb_notify_result (SolverState *state)
{
	GnmSolver *sol = state->run.solver;
	GnmSolverResult *r;
	char *txt;

	cb_notify_status (state);

	r = sol->result;
	switch (r ? r->quality : GNM_SOLVER_RESULT_NONE) {
	default:
	case GNM_SOLVER_RESULT_NONE:
		txt = g_strdup ("");
		break;

	case GNM_SOLVER_RESULT_FEASIBLE: {
		char *valtxt = gnm_format_value (go_format_general (),
						 r->value);
		txt = g_strdup_printf (_("Feasible: %s"), valtxt);
		g_free (valtxt);
		break;
	}

	case GNM_SOLVER_RESULT_OPTIMAL: {
		char *valtxt = gnm_format_value (go_format_general (),
						 r->value);
		txt = g_strdup_printf (_("Optimal: %s"), valtxt);
		g_free (valtxt);
		break;
	}

	case GNM_SOLVER_RESULT_INFEASIBLE:
		txt = g_strdup (_("Infeasible"));
		break;

	case GNM_SOLVER_RESULT_UNBOUNDED:
		txt = g_strdup (_("Unbounded"));
		break;
	}

	gtk_label_set_text (GTK_LABEL (state->run.result_widget), txt);
	g_free (txt);
}


static gboolean
cb_timer_tick (SolverState *state)
{
	int secs = time (NULL) - state->run.timer_start;
	int hh = secs / 3600;
	int mm = secs / 60 % 60;
	int ss = secs % 60;
	char *txt = hh
		? g_strdup_printf ("%02d:%02d:%02d", hh, mm, ss)
		: g_strdup_printf ("%02d:%02d", mm, ss);

	gtk_label_set_text (GTK_LABEL (state->run.timer_widget), txt);
	g_free (txt);

	return TRUE;
}

static GnmSolverResult *
run_solver (SolverState *state, GnmSolverParameters *param)
{
	GtkDialog *dialog;
	GtkWidget *hbox;
	int dialog_res;
	GError *err = NULL;
	gboolean ok;
	GnmSheetRange sr;
	GOUndo *undo = NULL;
	GnmSolver *sol = NULL;
	GnmValue const *vinput;
	GtkWindow *top = GTK_WINDOW (gtk_widget_get_toplevel (state->dialog));
	GnmSolverResult *res = NULL;

	sol = param->options.algorithm
		? gnm_solver_factory_create (param->options.algorithm, param)
		: NULL;
	if (!sol) {
		go_gtk_notice_dialog (top, GTK_MESSAGE_ERROR,
				      _("No suitable solver available."));
		goto fail;
	}

	state->run.solver = sol;

	vinput = gnm_solver_param_get_input (param);
	gnm_sheet_range_from_value (&sr, vinput);
	if (!sr.sheet) sr.sheet = param->sheet;
	undo = clipboard_copy_range_undo (sr.sheet, &sr.range);

	dialog = (GtkDialog *)gtk_dialog_new_with_buttons
		(_("Running Solver"),
		 wbcg_toplevel (state->wbcg), 0,
		 NULL);
	state->run.stop_button =
		go_gtk_dialog_add_button (dialog,
					  _("Stop"),
					  GTK_STOCK_STOP,
					  GTK_RESPONSE_NO);
	go_widget_set_tooltip_text
		(state->run.stop_button,
		 _("Stop the running solver"));
	g_signal_connect_swapped (G_OBJECT (state->run.stop_button),
				  "clicked", G_CALLBACK (cb_stop_solver),
				  state);

	state->run.ok_button =
		go_gtk_dialog_add_button (dialog,
					  _("OK"),
					  GTK_STOCK_OK,
					  GTK_RESPONSE_YES);

	hbox = gtk_hbox_new (FALSE, 2);

	state->run.timer_widget = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (hbox), state->run.timer_widget,
			    TRUE, TRUE, 0);

	state->run.status_widget = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (hbox), state->run.status_widget,
			    TRUE, TRUE, 0);

	state->run.result_widget = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (hbox), state->run.result_widget,
			    TRUE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (dialog->vbox), hbox, TRUE, TRUE, 0);
	gtk_widget_show_all (GTK_WIDGET (dialog));

	state->run.sig_notify_result =
		g_signal_connect_swapped (G_OBJECT (sol),
					  "notify::status",
					  G_CALLBACK (cb_notify_status),
					  state);
	cb_notify_status (state);

	state->run.sig_notify_status =
		g_signal_connect_swapped (G_OBJECT (sol),
					  "notify::result",
					  G_CALLBACK (cb_notify_result),
					  state);
	cb_notify_result (state);

	state->run.dialog = g_object_ref (dialog);
	g_object_ref (state->run.timer_widget);
	g_object_ref (state->run.status_widget);
	state->run.timer_source = g_timeout_add_seconds
		(1, (GSourceFunc)cb_timer_tick, state);
	state->run.timer_start = time (NULL);
	cb_timer_tick (state);

	/* ---------------------------------------- */

	ok = gnm_solver_start (sol,
			       WORKBOOK_CONTROL (state->wbcg),
			       &err);
	if (ok) {
		dialog_res = go_gtk_dialog_run (dialog, top);
		if (dialog_res == GTK_RESPONSE_YES && !sol->result)
			dialog_res = GTK_RESPONSE_DELETE_EVENT;
	} else {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		go_gtk_notice_dialog (top, GTK_MESSAGE_ERROR,
				      "%s", err->message);
		dialog_res = GTK_RESPONSE_DELETE_EVENT;
	}

	g_signal_handler_disconnect (G_OBJECT (sol),
				     state->run.sig_notify_result);
	g_signal_handler_disconnect (G_OBJECT (sol),
				     state->run.sig_notify_status);

	if (sol->status == GNM_SOLVER_STATUS_RUNNING)
		gnm_solver_stop (sol, NULL);

	/* ---------------------------------------- */

	gtk_widget_destroy (GTK_WIDGET (state->run.dialog));
	if (state->run.timer_source) {
		g_source_remove (state->run.timer_source);
		state->run.timer_source = 0;
	}
	g_object_unref (state->run.status_widget);
	g_object_unref (state->run.timer_widget);
	g_object_unref (state->run.dialog);

	switch (dialog_res) {
	default:
	case GTK_RESPONSE_NO:
	case GTK_RESPONSE_DELETE_EVENT:
		break;

	case GTK_RESPONSE_YES: {
		GOUndo *redo;

		gnm_solver_store_result (sol);
		redo = clipboard_copy_range_undo (sr.sheet, &sr.range);
		cmd_solver (WORKBOOK_CONTROL (state->wbcg),
			    _("Running solver"),
			    undo, redo);
		res = g_object_ref (sol->result);
		undo = redo = NULL;
		break;
	}
	}

fail:
	if (undo)
		g_object_unref (undo);

	if (state->run.solver) {
		g_object_unref (state->run.solver);
		state->run.solver = NULL;
	}

	return res;
}


static void
solver_add_scenario (SolverState *state, GnmSolverResult *res, gchar const *name)
{
	GnmSolverParameters *param = state->sheet->solver_parameters;
	GnmValue         *input_range;
	gchar const      *comment = _("Optimal solution created by solver.\n");
	GnmScenario       *scenario;

	input_range = gnm_expr_entry_parse_as_value (state->change_cell_entry,
						     state->sheet);

	scenario_add_new (name, input_range,
			  value_peek_string (gnm_solver_param_get_input (param)),
			  comment, state->sheet, &scenario);
	scenario_add (state->sheet, scenario);
	value_release (input_range);
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

	workbook_recalc (state->sheet->workbook);

	if (res != NULL) {
		if (res->quality == GNM_SOLVER_RESULT_OPTIMAL &&
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
	GtkWidget *w_ = glade_xml_get_widget (state->gui, (name_));	\
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w_),		\
				      param->field_);			\
} while (0)


/**
 * dialog_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_init (SolverState *state)
{
	GtkTable *table;
	GnmSolverParameters *param;
	GtkCellRenderer *renderer;
	GtkListStore *store;
	GtkTreeViewColumn *column;
	GSList *cl;
	GnmCell *target_cell;
	GnmValue const *input;

	param = state->sheet->solver_parameters;

	state->gui = gnm_glade_xml_new (GO_CMD_CONTEXT (state->wbcg),
		"solver.glade", NULL, NULL);
        if (state->gui == NULL)
                return TRUE;

	state->dialog = glade_xml_get_widget (state->gui, "Solver");
        if (state->dialog == NULL)
                return TRUE;

/*  buttons  */
	state->solve_button  = glade_xml_get_widget (state->gui, "solvebutton");
	g_signal_connect (G_OBJECT (state->solve_button), "clicked",
			  G_CALLBACK (cb_dialog_solve_clicked), state);

	state->close_button  = glade_xml_get_widget (state->gui, "closebutton");
	g_signal_connect (G_OBJECT (state->close_button), "clicked",
			  G_CALLBACK (cb_dialog_close_clicked), state);

	gnumeric_init_help_button (glade_xml_get_widget (state->gui,
							 "helpbutton"),
				   GNUMERIC_HELP_LINK_SOLVER);

	state->add_button  = glade_xml_get_widget (state->gui, "addbutton");
	gtk_button_set_alignment (GTK_BUTTON (state->add_button), 0.5, .5);
	g_signal_connect_swapped (G_OBJECT (state->add_button), "clicked",
		G_CALLBACK (cb_dialog_add_clicked), state);

	state->change_button = glade_xml_get_widget (state->gui,
						     "changebutton");
	g_signal_connect (G_OBJECT (state->change_button), "clicked",
			  G_CALLBACK (cb_dialog_change_clicked), state);

	state->delete_button = glade_xml_get_widget (state->gui,
						     "deletebutton");
	gtk_button_set_alignment (GTK_BUTTON (state->delete_button), 0.5, .5);
	g_signal_connect (G_OBJECT (state->delete_button), "clicked",
			  G_CALLBACK (cb_dialog_delete_clicked), state);

	/* target_entry */
	table = GTK_TABLE (glade_xml_get_widget (state->gui,
						 "parameter_table"));
	state->target_entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->target_entry,
		GNM_EE_SINGLE_RANGE |
		GNM_EE_FORCE_ABS_REF |
		GNM_EE_SHEET_OPTIONAL, GNM_EE_MASK);
	gtk_table_attach (table, GTK_WIDGET (state->target_entry),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0, 0, 0);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->target_entry));
	gtk_widget_show (GTK_WIDGET (state->target_entry));
	g_signal_connect_after (G_OBJECT (state->target_entry),	"changed",
			G_CALLBACK (dialog_set_main_button_sensitivity),
				state);

	/* change_cell_entry */
	table = GTK_TABLE (glade_xml_get_widget (state->gui,
						 "parameter_table"));
	state->change_cell_entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->change_cell_entry,
		GNM_EE_SINGLE_RANGE |
		GNM_EE_FORCE_ABS_REF |
		GNM_EE_SHEET_OPTIONAL, GNM_EE_MASK);
	gtk_table_attach (table, GTK_WIDGET (state->change_cell_entry),
			  1, 2, 2, 3,
			  GTK_EXPAND | GTK_FILL, 0, 0, 0);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->change_cell_entry));
	gtk_widget_show (GTK_WIDGET (state->change_cell_entry));
	g_signal_connect_after (G_OBJECT (state->change_cell_entry), "changed",
		G_CALLBACK (dialog_set_main_button_sensitivity), state);

	/* Algorithm */
	state->algorithm_combo = GTK_COMBO_BOX
		(glade_xml_get_widget (state->gui, "algorithm_combo"));
	renderer = (GtkCellRenderer*) gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (state->algorithm_combo), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (state->algorithm_combo), renderer,
					"text", 0,
					NULL);
	fill_algorithm_combo (state, param->options.model_type);

	state->model_button =
		glade_xml_get_widget(state->gui, "lp_model_button");
	g_signal_connect (G_OBJECT (state->model_button), "clicked",
			  G_CALLBACK (cb_dialog_model_type_clicked), state);

	/* Options */
	state->max_iter_entry = glade_xml_get_widget (state->gui,
						      "max_iter_entry");
	{
		char *txt = g_strdup_printf ("%d", param->options.max_iter);
		gtk_entry_set_text (GTK_ENTRY (state->max_iter_entry), txt);
		g_free (txt);
	}

	state->max_time_entry = glade_xml_get_widget (state->gui,
						      "max_time_entry");
	{
		char *txt = g_strdup_printf ("%d", param->options.max_time_sec);
		gtk_entry_set_text (GTK_ENTRY (state->max_time_entry), txt);
		g_free (txt);
	}

/* lhs_entry */
	table = GTK_TABLE (glade_xml_get_widget (state->gui, "edit-table"));
	state->lhs.entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->lhs.entry,
		GNM_EE_SINGLE_RANGE |
		GNM_EE_FORCE_ABS_REF |
		GNM_EE_SHEET_OPTIONAL, GNM_EE_MASK);
	gtk_table_attach (table, GTK_WIDGET (state->lhs.entry),
			  0, 1, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	state->lhs.label = glade_xml_get_widget (state->gui, "lhs_label");
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
	table = GTK_TABLE (glade_xml_get_widget (state->gui, "edit-table"));
	state->rhs.entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->rhs.entry,
				  GNM_EE_SINGLE_RANGE |
				  GNM_EE_FORCE_ABS_REF |
				  GNM_EE_SHEET_OPTIONAL |
				  GNM_EE_CONSTANT_ALLOWED,
				  GNM_EE_MASK);
	gtk_table_attach (table, GTK_WIDGET (state->rhs.entry),
			  2, 3, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0, 0, 0);
	gtk_widget_show (GTK_WIDGET (state->rhs.entry));
	state->rhs.label = glade_xml_get_widget (state->gui, "rhs_label");
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
		(glade_xml_get_widget (state->gui, "type_menu"));
	gtk_combo_box_set_active (state->type_combo, 0);
	g_signal_connect (state->type_combo, "changed",
			  G_CALLBACK (dialog_set_sec_button_sensitivity),
			  state);

/* constraint_list */
	state->constraint_list = GTK_TREE_VIEW (glade_xml_get_widget
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

	INIT_BOOL_ENTRY ("autoscale_button", options.automatic_scaling);
	INIT_BOOL_ENTRY ("non_neg_button", options.assume_non_negative);
	INIT_BOOL_ENTRY ("all_int_button", options.assume_discrete);
	INIT_BOOL_ENTRY ("program", options.program_report);

	input = gnm_solver_param_get_input (param);
	if (input != NULL)
		gnm_expr_entry_load_from_text (state->change_cell_entry,
					       value_peek_string (input));
	target_cell = gnm_solver_param_get_target_cell (param);
	if (target_cell)
		gnm_expr_entry_load_from_text (state->target_entry,
					       cell_name (target_cell));

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "max_button")),
			param->problem_type == GNM_SOLVER_MAXIMIZE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "min_button")),
			param->problem_type == GNM_SOLVER_MINIMIZE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "lp_model_button")),
			param->options.model_type == GNM_SOLVER_LP);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "qp_model_button")),
			param->options.model_type == GNM_SOLVER_QP);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "no_scenario")),
			! param->options.add_scenario);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "optimal_scenario")),
			param->options.add_scenario);

	state->scenario_name_entry = glade_xml_get_widget
		(state->gui, "scenario_name_entry");
	gtk_entry_set_text (GTK_ENTRY (state->scenario_name_entry),
			    param->options.scenario_name);

/* Done */
	gnm_expr_entry_grab_focus (state->target_entry, FALSE);

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
				(GDestroyNotify)free_state);

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

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, SOLVER_KEY))
		return;

	state                 = g_new0 (SolverState, 1);
	state->wbcg           = wbcg;
	state->sheet          = sheet;
	state->warning_dialog = NULL;
	state->orig_params = gnm_solver_param_dup (sheet->solver_parameters,
						   sheet);

	if (dialog_init (state)) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				 _("Could not create the Solver dialog."));
		free_state (state);
		return;
	}

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog), wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SOLVER_KEY);

	gtk_widget_show (state->dialog);
}
