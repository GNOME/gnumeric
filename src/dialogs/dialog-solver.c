/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-solver.c:
 *
 * Author:
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 *
 * (C) Copyright 2000, 2002 by Jukka-Pekka Iivonen <iivonen@iki.fi>
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
#include <solver.h>
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
	GtkWidget           *cancel_button;
	GtkWidget           *close_button;
	GtkWidget           *add_button;
	GtkWidget           *change_button;
	GtkWidget           *delete_button;
	GtkWidget           *model_button;
	GtkWidget           *scenario_name_entry;
	struct {
		GnmExprEntry	*entry;
		GtkWidget	*label;
	} lhs, rhs;
	GtkComboBox       *type_combo;
	GtkComboBox       *algorithm_combo;
	GtkTreeView            *constraint_list;
	SolverConstraint              *constr;
	gnm_float          ov_target;
	GSList              *ov;
	GSList              *ov_stack;
	GSList              *ov_cell_stack;
	GtkWidget           *warning_dialog;

	gboolean             cancelled;

	Sheet		    *sheet;
	Workbook            *wb;
	WBCGtk  *wbcg;
} SolverState;


typedef struct {
	char const          *name;
	SolverAlgorithmType alg;
	SolverModelType     type;
} algorithm_def_t;

static algorithm_def_t const algorithm_defs [] = {
	{ N_("Simplex (LP Solve)"), LPSolve, SolverLPModel },
	{ N_("Revised Simplex (GLPK 4.9)"), GLPKSimplex, SolverLPModel },
	{ N_("< Not available >"), QPDummy, SolverQPModel },
	{ NULL, 0, 0 }
};

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

static GList *lp_alg_name_list = NULL;
static GList *qp_alg_name_list = NULL;

static void
constraint_fill (SolverConstraint *c, SolverState *state)
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
	SolverConstraint *test = gnm_solver_constraint_new (NULL);
	gboolean ready, has_rhs;

	constraint_fill (test, state);
	ready = gnm_solver_constraint_valid (test);
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
	SolverConstraint const *c;
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
		SolverParameters *param = state->sheet->solver_parameters;

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
	SolverConstraint *c = state->constr;

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
		SolverParameters *param = state->sheet->solver_parameters;

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

static void
cb_dialog_model_type_clicked (G_GNUC_UNUSED GtkWidget *button,
			      SolverState *state)
{
	SolverModelType type;
	GtkListStore *store;
	GtkTreeIter iter;
	GList *l = NULL;

	type = gnumeric_glade_group_value (state->gui, model_type_group);
	store = gtk_list_store_new (1, G_TYPE_STRING);
	gtk_combo_box_set_model (state->algorithm_combo, GTK_TREE_MODEL (store));
	switch (type) {
	case SolverLPModel:
		l = lp_alg_name_list;
		gtk_widget_set_sensitive (GTK_WIDGET (state->solve_button),
					  TRUE);
		break;
	case SolverQPModel:
		l = qp_alg_name_list;
		gtk_widget_set_sensitive (GTK_WIDGET (state->solve_button),
					  FALSE);
		go_gtk_notice_nonmodal_dialog ((GtkWindow *) state->dialog,
					  &(state->warning_dialog),
					  GTK_MESSAGE_INFO,
					  _("Looking for a subject for your "
					    "thesis? Maybe you would like to "
					    "write a QP solver for "
					    "Gnumeric?"));
		break;
	default:
		break;
	}
	while (l) {
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
					0, l->data,
					-1);
		l = l->next;
	}
	gtk_combo_box_set_active (state->algorithm_combo ,0);
}

static void
free_original_values (GSList *ov)
{
	go_slist_free_custom (ov, g_free);
}

static void
cb_dialog_solver_destroy (SolverState *state)
{
	g_return_if_fail (state != NULL);

	if (state->ov_cell_stack != NULL &&
	    !state->cancelled &&
	    !cmd_solver (WORKBOOK_CONTROL(state->wbcg), state->ov_cell_stack,
			 state->ov_stack, NULL))
	{
		state->ov_cell_stack = NULL;
		state->ov_stack = NULL;
	}

	if (state->ov_stack != NULL) {
		go_slist_free_custom (state->ov_stack,
				      (GFreeFunc)free_original_values);
		state->ov_stack = NULL;
		g_slist_free (state->ov_cell_stack);
		state->ov_cell_stack = NULL;
	}

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	wbcg_edit_finish (state->wbcg, WBC_EDIT_REJECT, NULL);

	state->dialog = NULL;
	g_free (state);
}

static void
restore_original_values (GSList *input_cells, GSList *ov)
{
        while (ov != NULL) {
		char const *str = ov->data;
		GnmCell *cell = input_cells->data;

		sheet_cell_set_text (cell, str, NULL);
		ov = ov->next;
		input_cells = input_cells->next;
	}
}

static void
cb_dialog_cancel_clicked (G_GNUC_UNUSED GtkWidget *button, SolverState *state)
{
	if (state->ov_stack != NULL) {
		GSList *cells = state->ov_cell_stack;
		GSList *ov = state->ov_stack;
		while (cells != NULL && ov != NULL) {
			restore_original_values (cells->data,
						 ov->data);
			cells = cells->next;
			ov = ov ->next;
		}
		go_slist_free_custom (state->ov_stack,
				      (GFreeFunc)free_original_values);
		state->ov_stack = NULL;
		g_slist_free (state->ov_cell_stack);
		state->ov_cell_stack = NULL;
		workbook_recalc (state->sheet->workbook);
	}
	state->cancelled = TRUE;

	gtk_widget_destroy (state->dialog);
}

static void
cb_dialog_close_clicked (G_GNUC_UNUSED GtkWidget *button,
			 SolverState *state)
{
	state->cancelled = FALSE;
	gtk_widget_destroy (state->dialog);
}

static gchar const *
check_int_constraints (GnmValue *input_range, SolverState *state)
{
	GtkTreeModel *store;
	GtkTreeIter iter;

	store = gtk_tree_view_get_model (state->constraint_list);
	if (gtk_tree_model_get_iter_first (store, &iter))
		do {
			SolverConstraint const *a_constraint;
			gchar *text;

			gtk_tree_model_get (store, &iter, 0, &text, 1, &a_constraint, -1);
			if (a_constraint == NULL) {
				g_free (text);
				break;
			}

			if ((a_constraint->type != SolverINT) &&
			    (a_constraint->type != SolverBOOL)) {
				g_free (text);
				continue;
			}

#if 0
			if (!global_range_contained (state->sheet,
						     a_constraint->lhs,
						     input_range))
				return text;
#endif

			g_free (text);
		} while (gtk_tree_model_iter_next (store, &iter));
	return NULL;
}

/**
 * save_original_values:
 * @input_cells:
 *
 *
 *
 **/
static GSList *
save_original_values (GSList *input_cells)
{
        GSList *ov = NULL;

	while (input_cells != NULL) {
		GnmCell *cell = input_cells->data;
		char *str;

		str = value_get_as_string (cell->value);
		ov = g_slist_append (ov, str);

		input_cells = input_cells->next;
	}

	return ov;
}


/* Returns FALSE if the reports deleted the current sheet
 * and forced the dialog to die */
static gboolean
solver_reporting (SolverState *state, SolverResults *res)
{
	SolverOptions *opt = &res->param->options;
	gchar         *err = NULL;

	g_object_add_weak_pointer (G_OBJECT (state->dialog), (gpointer)&state);
	switch (res->status) {
	case SolverOptimal :
		go_gtk_notice_nonmodal_dialog
			((GtkWindow *) state->dialog,
			 &(state->warning_dialog),
			 GTK_MESSAGE_INFO,
			 _("Solver found an optimal solution.  All "
			   "constraints and optimality conditions are "
			   "satisfied.\n"));
		if ((opt->sensitivity_report || opt->limits_report)
		    && res->ilp_flag)
			go_gtk_notice_nonmodal_dialog
				((GtkWindow *) state->dialog,
				 &(state->warning_dialog),
				 GTK_MESSAGE_INFO,
				 _("Neither sensitivity nor limits report are "
				   "meaningful if the program has "
				   "integer constraints.  These reports "
				   "will not be created."));
		err = solver_reports (WORKBOOK_CONTROL(state->wbcg),
				      state->sheet, res,
				      opt->answer_report,
				      opt->sensitivity_report,
				      opt->limits_report,
				      opt->performance_report,
				      opt->program_report,
				      opt->dual_program_report);
		break;
	case SolverUnbounded :
		go_gtk_notice_nonmodal_dialog
			((GtkWindow *) state->dialog,
			 &(state->warning_dialog),
			 GTK_MESSAGE_WARNING,
			 _("The Target Cell value specified does not "
			   "converge!  The program is unbounded."));
		err = solver_reports (WORKBOOK_CONTROL(state->wbcg),
				      state->sheet, res,
				      FALSE, FALSE, FALSE,
				      opt->performance_report,
				      opt->program_report,
				      opt->dual_program_report);
		break;
	case SolverInfeasible :
		go_gtk_notice_nonmodal_dialog
			((GtkWindow *) state->dialog,
			 &(state->warning_dialog),
			 GTK_MESSAGE_WARNING,
			 _("A feasible solution could not be found.  "
			   "All specified constraints cannot be met "
			   "simultaneously. "));
		err = solver_reports (WORKBOOK_CONTROL(state->wbcg),
				      state->sheet, res,
				      FALSE, FALSE, FALSE,
				      opt->performance_report,
				      opt->program_report,
				      opt->dual_program_report);
		break;
	case SolverMaxIterExc :
		go_gtk_notice_nonmodal_dialog
			((GtkWindow *) state->dialog,
			 &(state->warning_dialog),
			 GTK_MESSAGE_ERROR,
			 _("The maximum number of iterations exceeded. "
			   "The optimal value could not be found."));
		err = solver_reports (WORKBOOK_CONTROL(state->wbcg),
				      state->sheet, res,
				      FALSE, FALSE, FALSE,
				      opt->performance_report,
				      opt->program_report,
				      opt->dual_program_report);
		break;
	case SolverMaxTimeExc :
		go_gtk_notice_nonmodal_dialog
			((GtkWindow *) state->dialog,
			 &(state->warning_dialog),
			 GTK_MESSAGE_ERROR,
			 SOLVER_MAX_TIME_ERR);
		err = solver_reports (WORKBOOK_CONTROL(state->wbcg),
				      state->sheet, res,
				      FALSE, FALSE, FALSE,
				      opt->performance_report,
				      opt->program_report,
				      opt->dual_program_report);
		break;
	default:
		go_gtk_notice_nonmodal_dialog
			((GtkWindow *) state->dialog,
			 &(state->warning_dialog),
			 GTK_MESSAGE_ERROR,
			 _("Unknown error."));
		break;
	}
	if (NULL != state)
		g_object_remove_weak_pointer (G_OBJECT (state->dialog), (gpointer)&state);

	if (err)
		go_gtk_notice_nonmodal_dialog (state ? ((GtkWindow *) state->dialog) : NULL,
			 &(state->warning_dialog), GTK_MESSAGE_ERROR, err);

	return state != NULL;
}

static void
solver_add_scenario (SolverState *state, SolverResults *res, gchar const *name)
{
	SolverParameters *param = res->param;
	GnmValue         *input_range;
	gchar const      *comment = _("Optimal solution created by solver.\n");
	scenario_t       *scenario;

	input_range = gnm_expr_entry_parse_as_value (state->change_cell_entry,
						     state->sheet);

	scenario_add_new (name, input_range,
			  value_peek_string (gnm_solver_param_get_input (param)),
			  comment, state->sheet, &scenario);
	scenario_add (state->sheet, scenario);
	if (input_range != NULL)
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
	SolverResults           *res;
	GnmValue                   *target_range;
	GnmValue                   *input_range;
        GSList			*input_cells;
	gint                    i;
	gboolean                answer, sensitivity, limits, performance;
	gboolean                program, dual_program;
	GError *err = NULL;
	SolverParameters        *param;
	GtkTreeIter iter;
	gchar const *name;
	GnmCell *target_cell;

	param = state->sheet->solver_parameters;

	if (state->warning_dialog != NULL)
		gtk_widget_destroy (state->warning_dialog);

	target_range = gnm_expr_entry_parse_as_value (state->target_entry,
						      state->sheet);
	input_range = gnm_expr_entry_parse_as_value (state->change_cell_entry,
						     state->sheet);

	if (target_range == NULL || input_range == NULL) {
		go_gtk_notice_nonmodal_dialog
			((GtkWindow *) state->dialog,
			  &(state->warning_dialog),
			  GTK_MESSAGE_ERROR, _("You have not specified "
					       "a problem to be solved"));
		return;
	}

	gnm_solver_param_set_input (param, value_dup (input_range));

	gnm_solver_param_set_target (param,
				     &target_range->v_range.cell.a);
	target_cell = gnm_solver_param_get_target_cell (param);

	/* Check that the target cell type is number. */
	if (!target_cell || !gnm_cell_is_number (target_cell)) {
		go_gtk_notice_nonmodal_dialog
			((GtkWindow *) state->dialog,
			 &(state->warning_dialog),
			 GTK_MESSAGE_ERROR, _("Target cell should contain "
					      "a formula."));
		return;
	}

	input_cells = gnm_solver_param_get_input_cells (param);

	param->problem_type =
		gnumeric_glade_group_value (state->gui, problem_type_group);
	param->options.model_type =
		gnumeric_glade_group_value (state->gui, model_type_group);

	gtk_combo_box_get_active_iter (state->algorithm_combo, &iter);
	gtk_tree_model_get (gtk_combo_box_get_model (state->algorithm_combo), &iter, 0, &name, -1);
	for (i = 0; algorithm_defs [i].name; i++) {
		if (param->options.model_type == algorithm_defs [i].type)
			if (strcmp (algorithm_defs [i].name, name) == 0) {
				param->options.algorithm =
					algorithm_defs [i].alg;
				break;
			}
	}

	param->options.assume_non_negative = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui,
							  "non_neg_button")));
	param->options.assume_discrete = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui,
							  "all_int_button")));
	param->options.automatic_scaling = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (glade_xml_get_widget
				    (state->gui, "autoscale_button")));

	param->options.max_iter = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->max_iter_entry));
	param->options.max_time_sec = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->max_time_entry));

	answer = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget (state->gui, "answer")));
	param->options.answer_report = answer;

	sensitivity = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget (state->gui, "sensitivity")));
	param->options.sensitivity_report = sensitivity;

	limits = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget (state->gui, "limits")));
	param->options.limits_report = limits;

	performance = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget (state->gui, "performance")));
	param->options.performance_report = performance;

	program = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget (state->gui, "program")));
	param->options.program_report = program;

	g_free (param->options.scenario_name);
	param->options.scenario_name = g_strdup
		(gtk_entry_get_text (GTK_ENTRY (state->scenario_name_entry)));

	param->options.add_scenario = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui,
							  "optimal_scenario")));

	dual_program = FALSE;
	param->options.dual_program_report = dual_program;

	name = check_int_constraints (input_range, state);
	if (name != NULL) {
		char *str;

		str = g_strdup_printf
			(_("Constraint `%s' is for a cell that "
			   "is not an input cell."), name);
		go_gtk_notice_nonmodal_dialog ((GtkWindow *) state->dialog,
					  &(state->warning_dialog),
					  GTK_MESSAGE_ERROR, str);
		g_free (str);
		goto out;
	}

	state->ov_target     = value_get_as_float (target_cell->value);
	state->ov            = save_original_values (input_cells);
	state->ov_stack      = g_slist_prepend (state->ov_stack, state->ov);
	state->ov_cell_stack = g_slist_prepend (state->ov_cell_stack,
						input_cells);


	res = solver (WORKBOOK_CONTROL (state->wbcg), state->sheet, &err);
	workbook_recalc (state->sheet->workbook);

	if (res != NULL) {
		state->cancelled = FALSE;

		/* WARNING : The dialog may be deleted by the reports
		 * solver_reporting will return FALSE if state is gone and cleared */
		if (solver_reporting (state, res) &&
		    res->status == SolverOptimal &&
		    param->options.add_scenario)
			solver_add_scenario (state, res,
					     param->options.scenario_name);

		solver_results_free (res);
	} else
		go_gtk_notice_nonmodal_dialog
			(GTK_WINDOW (state->dialog),
			 &(state->warning_dialog),
			 GTK_MESSAGE_ERROR,
			 err ? err->message : _("Unknown error."));

out:
	value_release (target_range);
	value_release (input_range);
	if (err)
		g_error_free (err);
}


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
	GtkTable                *table;
	SolverParameters        *param;
	int                     i;
	GtkCellRenderer *renderer;
	GtkListStore *store;
	GtkTreeIter iter;
	GtkTreeViewColumn *column;
	GList *l = NULL;
	GSList *cl;
	GnmCell *target_cell;
	GnmValue const *input;

	param = state->sheet->solver_parameters;

	if (lp_alg_name_list == NULL) {
		for (i = 0; algorithm_defs [i].name; i++)
			switch (algorithm_defs [i].type) {
			case SolverLPModel:
				lp_alg_name_list = g_list_append
					(lp_alg_name_list,
					 (gpointer) algorithm_defs [i].name);
				break;
			case SolverQPModel:
				qp_alg_name_list = g_list_append
					(qp_alg_name_list,
					 (gpointer) algorithm_defs [i].name);
				break;
			default:
				break;
			}
	}

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

	state->cancel_button  = glade_xml_get_widget (state->gui,
						      "cancelbutton");
	g_signal_connect (G_OBJECT (state->cancel_button), "clicked",
			  G_CALLBACK (cb_dialog_cancel_clicked), state);

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
	store = gtk_list_store_new (1, G_TYPE_STRING);
	gtk_combo_box_set_model (state->algorithm_combo, GTK_TREE_MODEL (store));
	switch (param->options.model_type) {
	case SolverLPModel:
		l = lp_alg_name_list;
		break;
	case SolverQPModel:
		l = qp_alg_name_list;
		break;
	default:
		break;
	}
	while (l) {
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
					0, l->data,
					-1);
		l = l->next;
	}
	gtk_combo_box_set_active (state->algorithm_combo, 0);

	state->model_button =
		glade_xml_get_widget(state->gui, "lp_model_button");
	g_signal_connect (G_OBJECT (state->model_button), "clicked",
			  G_CALLBACK (cb_dialog_model_type_clicked), state);

	/* Options */
	state->max_iter_entry = glade_xml_get_widget (state->gui,
						      "max_iter_entry");
	if (state->max_iter_entry == NULL)
		return TRUE;
	gtk_entry_set_text (GTK_ENTRY (state->max_iter_entry), "200");

	state->max_time_entry = glade_xml_get_widget (state->gui,
						      "max_time_entry");
	if (state->max_time_entry == NULL)
		return TRUE;
	gtk_entry_set_text (GTK_ENTRY (state->max_time_entry), "30");

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
		GNM_EE_SHEET_OPTIONAL, GNM_EE_MASK);
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
		SolverConstraint const *c = cl->data;
		GtkTreeIter iter;
		char *str;

		gtk_list_store_append (store, &iter);
		str = gnm_solver_constraint_as_str (c, state->sheet);
		gtk_list_store_set (store, &iter, 0, str, 1, c, -1);
		g_free (str);
	}

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "non_neg_button")),
			param->options.assume_non_negative);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "all_int_button")),
			param->options.assume_discrete);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "answer")),
			param->options.answer_report);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "sensitivity")),
			param->options.sensitivity_report);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "limits")),
			param->options.limits_report);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "performance")),
			param->options.performance_report);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "program")),
			param->options.program_report);

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
			param->problem_type == SolverMaximize);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "min_button")),
			param->problem_type == SolverMinimize);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "lp_model_button")),
			param->options.model_type == SolverLPModel);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "qp_model_button")),
			param->options.model_type == SolverQPModel);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "no_scenario")),
			! param->options.add_scenario);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "optimal_scenario")),
			param->options.add_scenario);

	state->scenario_name_entry = glade_xml_get_widget
		(state->gui, "scenario_name_entry");
	if (state->scenario_name_entry == NULL)
		return TRUE;
	gtk_entry_set_text (GTK_ENTRY (state->scenario_name_entry),
			    param->options.scenario_name);

/* Done */
	gnm_expr_entry_grab_focus (state->target_entry, FALSE);

	dialog_set_main_button_sensitivity (NULL, state);
	dialog_set_sec_button_sensitivity (NULL, state);

/* dialog */
	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_dialog_solver_destroy);

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

	if (wbcg == NULL) {
		return;
	}

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, SOLVER_KEY))
		return;

	state                 = g_new (SolverState, 1);
	state->wbcg           = wbcg;
	state->wb             = wb_control_get_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet          = sheet;
	state->ov             = NULL;
	state->ov_stack       = NULL;
	state->ov_cell_stack  = NULL;
	state->warning_dialog = NULL;
	state->cancelled      = TRUE;

	if (dialog_init (state)) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				 _("Could not create the Solver dialog."));
		g_free (state);
		return;
	}

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog), wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SOLVER_KEY);

	gtk_widget_show (state->dialog);
}
