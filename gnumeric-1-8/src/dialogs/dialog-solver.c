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
#include <wbc-gtk.h>
#include <workbook.h>
#include <parse-util.h>
#include <ranges.h>
#include <commands.h>
#include <widgets/gnumeric-expr-entry.h>

#include <glade/glade.h>
#include <gtk/gtk.h>
#include <goffice/utils/go-glib-extras.h>
#include <string.h>
#include <scenarios.h>

#define SOLVER_KEY            "solver-dialog"

typedef struct {
	GnmValue                *lhs_value;
	GnmValue                *rhs_value;
	SolverConstraintType type;
} constraint_t;

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
	constraint_t              *constr;
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

typedef struct {
	GtkTreeView *c_listing;
	GSList   *c_list;
	Sheet    *sheet;
} constraint_conversion_t;

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

/**
 * is_hom_row_or_col_ref:
 * @Widget:
 *
 **/
static gboolean
is_hom_row_or_col_ref (GnmExprEntry *entry_1, GnmExprEntry *entry_2,
		       Sheet *sheet)
{
        GnmValue    *input_range_1;
        GnmValue    *input_range_2;
	gboolean res;

	input_range_1 = gnm_expr_entry_parse_as_value (entry_1, sheet);
	input_range_2 = gnm_expr_entry_parse_as_value (entry_2, sheet);

        if ((input_range_1 != NULL) && (input_range_2 != NULL)) {
		res = ((input_range_1->type == VALUE_CELLRANGE)
		       && (input_range_2->type == VALUE_CELLRANGE)
		       && ((input_range_1->v_range.cell.a.col ==
			    input_range_1->v_range.cell.b.col)
			   || (input_range_1->v_range.cell.a.row ==
			       input_range_1->v_range.cell.b.row))
		       && ( input_range_1->v_range.cell.b.col -
			    input_range_1->v_range.cell.a.col
			    == input_range_2->v_range.cell.b.col -
			    input_range_2->v_range.cell.a.col)
		       && ( input_range_1->v_range.cell.b.row -
			    input_range_1->v_range.cell.a.row
			    == input_range_2->v_range.cell.b.row -
			    input_range_2->v_range.cell.a.row));
	} else {
		res = FALSE;
	}

	if (input_range_1 != NULL)
		value_release (input_range_1);
	if (input_range_2 != NULL)
		value_release (input_range_2);
	return res;
}

static gboolean
dialog_set_sec_button_sensitivity (G_GNUC_UNUSED GtkWidget *dummy,
				   SolverState *state)
{
	SolverConstraintType t = gtk_combo_box_get_active (state->type_combo);
	gboolean select_ready = (state->constr != NULL);

	gboolean ready =
		gnm_expr_entry_is_cell_ref (state->lhs.entry, state->sheet, TRUE) &&
		((t == SolverINT) ||
		 (t == SolverBOOL) ||
		 (is_hom_row_or_col_ref (state->lhs.entry, state->rhs.entry, state->sheet)));

	gtk_widget_set_sensitive (state->add_button, ready);
	gtk_widget_set_sensitive (state->change_button, select_ready && ready);
	gtk_widget_set_sensitive (state->delete_button, select_ready);

	/* Return TRUE iff the current constraint is valid.  */
	return ready;
}

static void
constraint_select_click (GtkTreeSelection *Selection,
			 SolverState    *state)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	GnmRange range;

	if (gtk_tree_selection_get_selected (Selection, &store, &iter))
		gtk_tree_model_get (store, &iter, 1, &state->constr, -1);
	else
		state->constr = NULL;
	dialog_set_sec_button_sensitivity (NULL, state);

	if (state->constr == NULL)
		return; /* Fail? */

	range_init_value (&range, state->constr->lhs_value);
	gnm_expr_entry_load_from_range (state->lhs.entry, state->sheet,&range);

	if (state->constr->type != SolverINT && state->constr->type != SolverBOOL) {
		range_init_value (&range, state->constr->rhs_value);
		gnm_expr_entry_load_from_range (state->rhs.entry,
						state->sheet, &range);
	} else
		gnm_expr_entry_load_from_text (state->rhs.entry, "");

	gtk_combo_box_set_active (state->type_combo, state->constr->type);
}

/**
 * release_constraint:
 *
 * @data:
 *
 * release the info
 **/
static void
release_constraint (constraint_t * data)
{
	if (data->lhs_value != NULL)
		value_release (data->lhs_value);
	if (data->rhs_value != NULL)
		value_release (data->rhs_value);
	g_free (data);
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

		release_constraint (state->constr);
		state->constr = NULL;

		if (gtk_tree_selection_get_selected (
			    gtk_tree_view_get_selection (state->constraint_list),
			    &store, &iter))
			gtk_list_store_remove ((GtkListStore*)store, &iter);
	}
}

static  constraint_t*
constraint_fill_row (SolverState *state, GtkListStore *store, GtkTreeIter *iter)
{
	char         *text;
	constraint_t *the_constraint = g_new (constraint_t, 1);

	the_constraint->lhs_value = gnm_expr_entry_parse_as_value
		(state->lhs.entry, state->sheet);
	the_constraint->type = gtk_combo_box_get_active
		(state->type_combo);
	if ((the_constraint->type != SolverINT) &&
	    (the_constraint->type != SolverBOOL)) {
		the_constraint->rhs_value = gnm_expr_entry_parse_as_value
			(state->rhs.entry, state->sheet);

/* FIXME: We are dropping cross sheet references!! */
		text = write_constraint_str
			(the_constraint->lhs_value->v_range.cell.a.col,
			 the_constraint->lhs_value->v_range.cell.a.row,
			 the_constraint->rhs_value->v_range.cell.a.col,
			 the_constraint->rhs_value->v_range.cell.a.row,
			 the_constraint->type,
			 the_constraint->lhs_value->v_range.cell.b.col -
			 the_constraint->lhs_value->v_range.cell.a.col + 1,
			 the_constraint->lhs_value->v_range.cell.b.row -
			 the_constraint->lhs_value->v_range.cell.a.row + 1);
	} else {
		the_constraint->rhs_value = NULL;
/* FIXME: We are dropping cross sheet references!! */
		text = write_constraint_str
			(the_constraint->lhs_value->v_range.cell.a.col,
			 the_constraint->lhs_value->v_range.cell.a.row,
			 0, 0,
			 the_constraint->type,
			 the_constraint->lhs_value->v_range.cell.b.col -
			 the_constraint->lhs_value->v_range.cell.a.col + 1,
			 the_constraint->lhs_value->v_range.cell.b.row -
			 the_constraint->lhs_value->v_range.cell.a.row + 1);
	}
	gtk_list_store_set (store, iter, 0, text, 1, the_constraint, -1);
	g_free (text);
	gtk_tree_selection_select_iter (gtk_tree_view_get_selection (state->constraint_list), iter);
	return the_constraint;
}

static void
cb_dialog_add_clicked (SolverState *state)
{
	if (dialog_set_sec_button_sensitivity (NULL, state)) {
		GtkTreeIter   iter;
		GtkListStore *store = GTK_LIST_STORE (gtk_tree_view_get_model (state->constraint_list));

		gtk_list_store_append (store, &iter);
		constraint_fill_row (state, store, &iter);
	}
}

static void
cb_dialog_change_clicked (GtkWidget *button, SolverState *state)
{
	if (state->constr != NULL) {
		GtkTreeIter iter;
		GtkTreeModel *store;

		release_constraint (state->constr);
		state->constr = NULL;

		if (gtk_tree_selection_get_selected (
			    gtk_tree_view_get_selection (state->constraint_list),
			    &store, &iter))
			state->constr = constraint_fill_row (state, (GtkListStore *)store, &iter);
	}
}

static void
dialog_set_main_button_sensitivity (G_GNUC_UNUSED GtkWidget *dummy,
				    SolverState *state)
{
	gboolean ready = FALSE;

	ready = gnm_expr_entry_is_cell_ref (state->target_entry, state->sheet,
					    FALSE)
		&& gnm_expr_entry_is_cell_ref (state->change_cell_entry,
					       state->sheet, TRUE);
	gtk_widget_set_sensitive (state->solve_button, TRUE);
}

static void
cb_dialog_set_rhs_sensitivity (G_GNUC_UNUSED GtkWidget *dummy,
			       SolverState *state)
{
	int t = gtk_combo_box_get_active (state->type_combo);
	gboolean sensitive = (t != SolverINT && t != SolverBOOL);
	gtk_widget_set_sensitive (GTK_WIDGET (state->rhs.entry), sensitive);
	gtk_widget_set_sensitive (GTK_WIDGET (state->rhs.label), sensitive);
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
cb_dialog_solver_destroy_constraints (GtkTreeView *constraint_list)
{
	GtkTreeModel *store = gtk_tree_view_get_model (constraint_list);
	GtkTreeIter iter;

	if (gtk_tree_model_get_iter_first (store, &iter))
		do {
			constraint_t *p;
			gtk_tree_model_get (store, &iter, 1, &p, -1);
			release_constraint (p);
		} while (gtk_tree_model_iter_next (store, &iter));

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

static GnmValue *
cb_grab_cells (GnmCellIter const *iter, gpointer user)
{
	GList **the_list = user;
	GnmCell *cell;

	if (NULL == (cell = iter->cell))
		cell = sheet_cell_create (iter->pp.sheet,
			iter->pp.eval.col, iter->pp.eval.row);
	*the_list = g_list_append (*the_list, cell);
	return NULL;
}

static gchar const *
check_int_constraints (GnmValue *input_range, SolverState *state)
{
	GtkTreeModel *store;
	GtkTreeIter iter;

	store = gtk_tree_view_get_model (state->constraint_list);
	if (gtk_tree_model_get_iter_first (store, &iter))
		do {
			constraint_t const *a_constraint;
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

			if (!global_range_contained (state->sheet,
						     a_constraint->lhs_value,
						     input_range))
				return text;

			g_free (text);
		} while (gtk_tree_model_iter_next (store, &iter));
	return NULL;
}

/*
 *  convert_constraint_format:
 *  @conv:
 *
 *  We really shouldn't need this if we change the engine and mps to
 *  understand `value' based constraints.
 */
static void
convert_constraint_format (constraint_conversion_t *conv)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	constraint_t const *a_constraint;
	SolverConstraint *engine_constraint;

	store = gtk_tree_view_get_model (conv->c_listing);
	if (gtk_tree_model_get_iter_first (store, &iter))
		do {
			gtk_tree_model_get (store, &iter, 1, &a_constraint, -1);
			if (a_constraint == NULL)
				break;

			engine_constraint = g_new (SolverConstraint, 1);
			engine_constraint->lhs.col =
				a_constraint->lhs_value->v_range.cell.a.col;
			engine_constraint->lhs.row =
				a_constraint->lhs_value->v_range.cell.a.row;
			engine_constraint->rows  =
				a_constraint->lhs_value->v_range.cell.b.row
				- a_constraint->lhs_value->v_range.cell.a.row +1;
			engine_constraint->cols  =
				a_constraint->lhs_value->v_range.cell.b.col
				- a_constraint->lhs_value->v_range.cell.a.col +1;
			engine_constraint->type = a_constraint->type;
			if ((a_constraint->type == SolverINT)
				|| (a_constraint->type == SolverBOOL)) {
				engine_constraint->rhs.col  = 0;
				engine_constraint->rhs.row  = 0;
			} else {
				engine_constraint->rhs.col  =
					a_constraint->rhs_value->v_range.cell.a.col;
				engine_constraint->rhs.row  =
					a_constraint->rhs_value->v_range.cell.a.row;
			}
			engine_constraint->str = write_constraint_str (
				engine_constraint->lhs.col, engine_constraint->lhs.row,
				engine_constraint->rhs.col, engine_constraint->rhs.row,
				a_constraint->type,
				engine_constraint->cols,
				engine_constraint->rows);
			conv->c_list = g_slist_append (conv->c_list, engine_constraint);
		} while (gtk_tree_model_iter_next (store, &iter));
}

/*
 *  revert_constraint_format:
 *  @conv:
 *
 *  We really shouldn't need this if we change the engine and mps to
 *  understand `value' based constraints.
 */
static void
revert_constraint_format (constraint_conversion_t * conv)
{
	GtkTreeIter iter;
	GtkListStore *store = GTK_LIST_STORE (gtk_tree_view_get_model (conv->c_listing));
	GSList *engine_constraint_list = conv->c_list;

	while (engine_constraint_list != NULL) {
		SolverConstraint const *engine_constraint =
			engine_constraint_list->data;
		GnmRange r;
		constraint_t *a_constraint = g_new (constraint_t, 1);

		r.start.col = engine_constraint->lhs.col;
		r.start.row = engine_constraint->lhs.row;
		r.end.col = r.start.col + engine_constraint->cols - 1;
		r.end.row = r.start.row + engine_constraint->rows - 1;
		a_constraint->lhs_value = value_new_cellrange_r (conv->sheet,
								 &r);

		r.start.col = engine_constraint->rhs.col;
		r.start.row = engine_constraint->rhs.row;
		r.end.col = r.start.col + engine_constraint->cols - 1;
		r.end.row = r.start.row + engine_constraint->rows - 1;
		a_constraint->rhs_value = value_new_cellrange_r (conv->sheet,
								 &r);

		a_constraint->type = engine_constraint->type;
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, 0, engine_constraint->str, 1, a_constraint, -1);
		engine_constraint_list = engine_constraint_list->next;
	}
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
solver_reporting (SolverState *state, SolverResults *res, gchar const *errmsg)
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
			 GTK_MESSAGE_ERROR, errmsg);
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

	scenario_add_new (name, input_range, param->input_entry_str,
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
	constraint_conversion_t conv = {NULL, NULL, NULL};
	SolverResults           *res;
	GnmValue                   *target_range;
	GnmValue                   *input_range;
        GSList			*input_cells = NULL;
	GnmValue                   *result;
	GnmEvalPos                 pos;
	gint                    i;
	gboolean                answer, sensitivity, limits, performance;
	gboolean                program, dual_program;
	gchar const             *errmsg = _("Unknown error.");
	SolverParameters        *param;
	GtkTreeIter iter;
	gchar const *name;

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

	g_free (param->input_entry_str);
	param->input_entry_str = value_get_as_string (input_range);

	param->target_cell =
		sheet_cell_fetch (state->sheet,
				  target_range->v_range.cell.a.col,
				  target_range->v_range.cell.a.row );

	/* Check that the target cell type is number. */
	if (! gnm_cell_is_number (param->target_cell)) {
		go_gtk_notice_nonmodal_dialog
			((GtkWindow *) state->dialog,
			 &(state->warning_dialog),
			 GTK_MESSAGE_ERROR, _("Target cell should contain "
					      "a formula."));
		return;
	}

	result = workbook_foreach_cell_in_range (
		eval_pos_init_sheet (&pos, state->sheet),
		input_range, CELL_ITER_ALL, cb_grab_cells, &input_cells);

	param->input_cells = input_cells;

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

	conv.sheet     = state->sheet;
	conv.c_listing = state->constraint_list;
	convert_constraint_format (&conv);
	go_slist_free_custom (param->constraints,
			      (GFreeFunc)solver_constraint_destroy);
	param->constraints = conv.c_list;
	if (param->constraints == NULL) {
		go_gtk_notice_nonmodal_dialog
			((GtkWindow *) state->dialog,
			 &(state->warning_dialog),
			 GTK_MESSAGE_ERROR, _("No constraints defined for "
					      "the problem."));
		goto out;
	}
	state->ov_target     = value_get_as_float (param->target_cell->value);
	state->ov            = save_original_values (input_cells);
	state->ov_stack      = g_slist_prepend (state->ov_stack, state->ov);
	state->ov_cell_stack = g_slist_prepend (state->ov_cell_stack,
						input_cells);


	res = solver (WORKBOOK_CONTROL (state->wbcg), state->sheet, &errmsg);
	workbook_recalc (state->sheet->workbook);

	if (res != NULL) {
		state->cancelled = FALSE;

		/* WARNING : The dialog may be deleted by the reports
		 * solver_reporting will return FALSE if state is gone and cleared */
		if (solver_reporting (state, res, errmsg) &&
		    res->status == SolverOptimal && 
		    param->options.add_scenario)
			solver_add_scenario (state, res,
					     param->options.scenario_name);

		solver_results_free (res);
	} else
		go_gtk_notice_nonmodal_dialog (GTK_WINDOW (state->dialog),
					  &(state->warning_dialog),
					  GTK_MESSAGE_ERROR, errmsg);
out:
	if (target_range != NULL)
		value_release (target_range);
	if (input_range != NULL)
		value_release (input_range);
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
	constraint_conversion_t conv;
	SolverParameters        *param;
	int                     i;
	GtkCellRenderer *renderer;
	GtkListStore *store;
	GtkTreeIter iter;
	GtkTreeViewColumn *column;
	GList *l = NULL;

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
	g_signal_connect (state->type_combo, "changed",
			  G_CALLBACK (cb_dialog_set_rhs_sensitivity),
			  state);

/* constraint_list */
	state->constraint_list = GTK_TREE_VIEW (glade_xml_get_widget
					    (state->gui, "constraint_list"));
	g_signal_connect (G_OBJECT (state->constraint_list), "destroy",
			  G_CALLBACK (cb_dialog_solver_destroy_constraints),
			  NULL);

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

/* Loading the old solver specs... from param  */

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

	if (param->input_entry_str != NULL)
		gnm_expr_entry_load_from_text (state->change_cell_entry,
					       param->input_entry_str);
	if (param->target_cell != NULL)
		gnm_expr_entry_load_from_text (state->target_entry,
				    cell_name(param->target_cell));
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

	conv.c_listing = state->constraint_list;
	conv.c_list    = param->constraints;
	conv.sheet     = state->sheet;
	revert_constraint_format (&conv);

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
