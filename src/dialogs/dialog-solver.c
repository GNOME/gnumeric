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
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <func.h>
#include <solver.h>
#include <tool-dialogs.h>
#include <value.h>
#include <cell.h>
#include <sheet.h>
#include <workbook-edit.h>
#include <workbook.h>
#include <parse-util.h>
#include <ranges.h>
#include <widgets/gnumeric-expr-entry.h>

#include <glade/glade.h>
#include <string.h>

#define SOLVER_KEY            "solver-dialog"

typedef struct {
	GladeXML            *gui;
	GtkWidget           *dialog;
	GnumericExprEntry   *target_entry;
	GnumericExprEntry   *change_cell_entry;
	GtkWidget           *max_iter_entry;
	GtkWidget           *max_time_entry;
	GtkWidget           *solve_button;
	GtkWidget           *cancel_button;
	GtkWidget           *close_button;
	GtkWidget           *add_button;
	GtkWidget           *change_button;
	GtkWidget           *delete_button;
	GtkWidget           *model_button;
	GnumericExprEntry   *lhs_entry;
	GnumericExprEntry   *rhs_entry;
	GtkOptionMenu       *type_combo;
	GtkOptionMenu       *algorithm_combo;
	GtkCList            *constraint_list;
	gint                selected_row;
	gnm_float          ov_target;
	GSList              *ov;
	GSList              *ov_stack;
	GSList              *ov_cell_stack;
	GtkWidget           *warning_dialog;

	Sheet	            *sheet;
	Workbook            *wb;
	WorkbookControlGUI  *wbcg;
} SolverState;


#if 0
/* Different constraint types */
static char const * constraint_strs_untranslated[] = {
        N_("<="),
	N_(">="),
	N_("="),
 /* xgettext: Int == Integer constraint for the linear program */
	N_("Int"),
 /* xgettext: Bool == Boolean (or binary) constraint for the linear program */
	N_("Bool"),
	NULL
};
#endif



typedef struct {
	char const          *name;
	SolverAlgorithmType alg;
	SolverModelType     type;
} algorithm_def_t;

static algorithm_def_t algorithm_defs [] = {
	{ N_("Revised Simplex (GLPK 3.2)"), GLPKSimplex, SolverLPModel },
	{ N_("Simplex (LP Solve 3.2)"), LPSolve, SolverLPModel },
	{ N_("< Not available >"), QPDummy, SolverQPModel },
	{ NULL, 0, 0 }
};

typedef struct {
	GtkCList *c_listing;
	GSList   *c_list;
	Sheet    *sheet;
} constraint_conversion_t;


typedef struct {
	Value                *lhs_value;
	Value                *rhs_value;
	SolverConstraintType type;
} constraint_t;

static const char *problem_type_group[] = {
	"min_button",
	"max_button",
	"equal_to_button",
	0
};

static const char *model_type_group[] = {
	"lp_model_button",
	"qp_model_button",
	"nlp_model_button",
	0
};

static GList *lp_alg_name_list = NULL;
static GList *qp_alg_name_list = NULL;

/**
 * is_hom_row_or_col_ref:
 * @Widget:
 *
 **/
static gboolean
is_hom_row_or_col_ref (GnumericExprEntry *entry_1, GnumericExprEntry *entry_2,
		       Sheet *sheet)
{
        Value    *input_range_1;
        Value    *input_range_2;
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

/**
 * dialog_set_sec_button_sensitivity:
 * @dummy:
 * @state:
 *
 **/
static void
dialog_set_sec_button_sensitivity (G_GNUC_UNUSED GtkWidget *dummy,
				   SolverState *state)
{
	gboolean ready;
	gboolean select_ready;

	select_ready = (state->selected_row > -1);
	ready = gnm_expr_entry_is_cell_ref (state->lhs_entry, state->sheet,
					    TRUE) &&
		((gtk_option_menu_get_history (state->type_combo)
		  == SolverINT)
		 || (gtk_option_menu_get_history (state->type_combo)
		     == SolverBOOL)
		 || (is_hom_row_or_col_ref (state->lhs_entry, state->rhs_entry,
					    state->sheet)));

	gtk_widget_set_sensitive (state->add_button, ready);
	gtk_widget_set_sensitive (state->change_button, select_ready && ready);
	gtk_widget_set_sensitive (state->delete_button, select_ready);
}

/**
 * constraint_select_click:
 * @clist:
 * @row:
 * @column:
 * @event:
 * state:
 *
 **/

static void
constraint_select_click (G_GNUC_UNUSED GtkWidget      *clist,
			 gint           row,
			 G_GNUC_UNUSED gint           column,
			 G_GNUC_UNUSED GdkEventButton *event,
			 SolverState    *state)
{
        state->selected_row = row;
	dialog_set_sec_button_sensitivity (NULL, state);
}

/**
 * constraint_unselect_click:
 * @clist:
 * @row:
 * @column:
 * @event:
 * state:
 *
 **/

static void
constraint_unselect_click (G_GNUC_UNUSED GtkWidget      *clist,
			   G_GNUC_UNUSED gint           row,
			   G_GNUC_UNUSED gint           column,
			   G_GNUC_UNUSED GdkEventButton *event,
			   SolverState *state)
{
        state->selected_row = -1;
	dialog_set_sec_button_sensitivity (NULL, state);
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
	gtk_clist_remove (state->constraint_list, state->selected_row);
}

/**
 * cb_dialog_add_clicked:
 * @button:
 * @state:
 *
 *
 **/
static void
cb_dialog_add_clicked (G_GNUC_UNUSED GtkWidget *button,
		       SolverState *state)
{
	gint         selection;
	char         *texts[2] = {NULL, NULL};
	constraint_t *the_constraint = g_new (constraint_t, 1);

	the_constraint->lhs_value = gnm_expr_entry_parse_as_value
		(state->lhs_entry, state->sheet);
	the_constraint->type = gtk_option_menu_get_history
		(state->type_combo);
	if ((the_constraint->type != SolverINT) &&
	    (the_constraint->type != SolverBOOL)) {
		the_constraint->rhs_value = gnm_expr_entry_parse_as_value
			(state->rhs_entry, state->sheet);

/* FIXME: We are dropping cross sheet references!! */
		texts[0] = write_constraint_str
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
		texts[0] = write_constraint_str
			(the_constraint->lhs_value->v_range.cell.a.col,
			 the_constraint->lhs_value->v_range.cell.a.row,
			 0, 0,
			 the_constraint->type,
			 the_constraint->lhs_value->v_range.cell.b.col -
			 the_constraint->lhs_value->v_range.cell.a.col + 1,
			 the_constraint->lhs_value->v_range.cell.b.row -
			 the_constraint->lhs_value->v_range.cell.a.row + 1);
	}
	selection = gtk_clist_insert (state->constraint_list,
				      state->selected_row + 1, texts);
	gtk_clist_set_row_data_full (state->constraint_list, selection,
				     the_constraint,
				    (GtkDestroyNotify) release_constraint);
	g_free (texts[0]);
	gtk_clist_select_row (state->constraint_list, selection, 0 );
}

/**
 * cb_dialog_change_clicked:
 * @button:
 * @state:
 *
 *
 **/
static void
cb_dialog_change_clicked (GtkWidget *button, SolverState *state)
{
	gint old_selection = state->selected_row;

	gtk_clist_freeze (state->constraint_list);
	gtk_clist_remove (state->constraint_list, old_selection);
	state->selected_row = old_selection - 1;
      	cb_dialog_add_clicked (button, state);
	gtk_clist_select_row (state->constraint_list, old_selection, 0 );
	gtk_clist_thaw (state->constraint_list);
}

/**
 * dialog_set_main_button_sensitivity:
 * @dummy:
 * @state:
 *
 **/
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

/**
 * cb_dialog_set_rhs_sensitivity:
 * @dummy:
 * @state:
 *
 **/
static void
cb_dialog_set_rhs_sensitivity (G_GNUC_UNUSED GtkWidget *dummy,
			       SolverState *state)
{
	return;

/* FIXME: We would like to disable the rhs when appropriate. */
/* Unfortunately this confuses the widget:                   */

	if ((gtk_option_menu_get_history (state->type_combo)
		  == SolverINT)
		 || (gtk_option_menu_get_history (state->type_combo)
		     == SolverBOOL)) {
		gtk_widget_set_sensitive (GTK_WIDGET (state->rhs_entry), FALSE);
	} else {
		gtk_widget_set_sensitive (GTK_WIDGET (state->rhs_entry), TRUE);
	}
}

/**
 * cb_dialog_lp_clicked:
 * @button:
 * @state:
 *
 *
 **/
static void
cb_dialog_model_type_clicked (G_GNUC_UNUSED GtkWidget *button,
			      SolverState *state)
{
	SolverModelType type;

	type = gnumeric_glade_group_value (state->gui, model_type_group);
	switch (type) {
	case SolverLPModel:
		gtk_combo_set_popdown_strings
			(GTK_COMBO (state->algorithm_combo), lp_alg_name_list);
		gtk_widget_set_sensitive (GTK_WIDGET (state->solve_button),
					  TRUE);
		break;
	case SolverQPModel:
		gtk_combo_set_popdown_strings
			(GTK_COMBO (state->algorithm_combo), qp_alg_name_list);
		gtk_widget_set_sensitive (GTK_WIDGET (state->solve_button),
					  FALSE);
		gnumeric_notice_nonmodal ((GtkWindow *) state->dialog,
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
}

/**
 * free_original_values:
 * @ov:
 * @user_data:  (will be NULL)
 *
 * Destroy original savec values.
 *
 **/
static void
free_original_values (GSList *ov, G_GNUC_UNUSED gpointer user_data)
{
	g_slist_foreach (ov, (GFunc)g_free, NULL);
	g_slist_free (ov);
}

/**
 * dialog_destroy:
 * @window:
 * @focus_widget:
 * @state:
 *
 * Destroy the dialog and associated data structures.
 *
 **/
static gboolean
dialog_destroy (GtkObject *w, SolverState  *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	if (state->ov_stack != NULL) {
		g_slist_foreach (state->ov_stack, (GFunc)free_original_values,
				 NULL);
		g_slist_free (state->ov_stack);
		state->ov_stack = NULL;
		g_slist_free (state->ov_cell_stack);
		state->ov_cell_stack = NULL;
	}

	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	wbcg_edit_finish (state->wbcg, FALSE);

	state->dialog = NULL;

	g_free (state);

	return FALSE;
}

/**
 * restore_original_values:
 * @input_cells:
 * @ov:
 *
 **/
static void
restore_original_values (GSList *input_cells, GSList *ov)
{
        while (ov != NULL) {
	        const char *str = ov->data;
	        Cell *cell = input_cells->data;

		sheet_cell_set_text (cell, str);
		ov = ov->next;
		input_cells = input_cells->next;
	}
}


/**
 * cb_dialog_cancel_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
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
		g_slist_foreach (state->ov_stack, (GFunc)free_original_values,
				 NULL);
		g_slist_free (state->ov_cell_stack);
		g_slist_free (state->ov_stack);
		state->ov_cell_stack = NULL;
		state->ov_stack = NULL;
		workbook_recalc (state->sheet->workbook);
	}

	gtk_widget_destroy (state->dialog);
}

/**
 * cb_dialog_close_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_dialog_close_clicked (G_GNUC_UNUSED GtkWidget *button,
			 SolverState *state)
{
	gtk_widget_destroy (state->dialog);
}

/*
 *  grab_cells:
 *  @cell:
 *  @data: pointer to a data_set_t
 */
static Value *
grab_cells (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	GList **the_list = user_data;

	if (cell == NULL)
		cell = sheet_cell_fetch (sheet, col, row);
	*the_list = g_list_append (*the_list, cell);
	return NULL;
}

/*
 *  check_int_constraints:
 *  @cell:
 *  @data: pointer to a data_set_t
 */
static gint
check_int_constraints (Value *input_range, GtkCList *constraint_list)
{
	gint i;

	for (i = 0; ; i++) {
		const constraint_t *a_constraint = gtk_clist_get_row_data
			(constraint_list, i);
		if (a_constraint == NULL)
			break;
		if ((a_constraint->type != SolverINT) &&
		    (a_constraint->type != SolverBOOL))
			continue;
		if (!global_range_contained (a_constraint->lhs_value,
					     input_range))
			return i;
	}
	return -1;
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
	gint i;

	for (i = 0; ; i++) {
		SolverConstraint *engine_constraint;
		const constraint_t *a_constraint = gtk_clist_get_row_data
			(conv->c_listing, i);
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
	}
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
	GSList *engine_constraint_list = conv->c_list;
	gchar  *text[2] = {NULL, NULL};

	while (engine_constraint_list != NULL) {
		const SolverConstraint *engine_constraint =
			engine_constraint_list->data;
		Range r;
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
		text[0] = engine_constraint->str;
		gtk_clist_set_row_data_full (conv->c_listing,
					     gtk_clist_append (conv->c_listing,
							       text),
					     a_constraint,
					     (GtkDestroyNotify) release_constraint);
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
	        Cell *cell = input_cells->data;
		char *str;

		str = value_get_as_string (cell->value);
		ov = g_slist_append (ov, str);

	        input_cells = input_cells->next;
	}

	return ov;
}

/**
 * cb_destroy:
 * @data:
 * @user_data:
 *
 *
 **/
static void
cb_destroy (gpointer data, G_GNUC_UNUSED gpointer user_data)
{
	g_free (data);
}


static void
solver_reporting (SolverState *state, SolverResults *res, gchar *errmsg)
{
	SolverOptions *opt = &res->param->options;
	gchar         *err = NULL;

	switch (res->status) {
	case SolverOptimal :
		gnumeric_notice_nonmodal
			((GtkWindow *) state->dialog,
			 &(state->warning_dialog),
			 GTK_MESSAGE_INFO,
			 _("Solver found an optimal solution.  All "
			   "constraints and optimality conditions are "
			   "satisfied.\n"));
		if ((opt->sensitivity_report || opt->limits_report)
		    && res->ilp_flag)
			gnumeric_notice_nonmodal
				((GtkWindow *) state->dialog,
				 &(state->warning_dialog),
				 GTK_MESSAGE_INFO,
				 _("Sensitivity nor limits report are "
				   "not meaningful if the program has "
				   "integer constraints. These reports "
				   "will thus not be created."));
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
		gnumeric_notice_nonmodal
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
		gnumeric_notice_nonmodal
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
		gnumeric_notice_nonmodal
			((GtkWindow *) state->dialog,
			 &(state->warning_dialog),
			 GTK_MESSAGE_WARNING, 
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
		gnumeric_notice_nonmodal
			((GtkWindow *) state->dialog,
			 &(state->warning_dialog),
			 GTK_MESSAGE_WARNING, 
			 _("The maximum time exceeded. The optimal "
			   "value could not be found in given time."));
		err = solver_reports (WORKBOOK_CONTROL(state->wbcg),
				      state->sheet, res,
				      FALSE, FALSE, FALSE,
				      opt->performance_report,
				      opt->program_report,
				      opt->dual_program_report);
		break;
	default:
		gnumeric_notice_nonmodal
			((GtkWindow *) state->dialog,
			 &(state->warning_dialog),
			 GTK_MESSAGE_WARNING, errmsg);
		break;
	}
	if (err)
		gnumeric_notice_nonmodal
			((GtkWindow *) state->dialog,
			 &(state->warning_dialog), GTK_MESSAGE_WARNING, err);
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
	Value                   *target_range;
	Value                   *input_range;
        GSList			*input_cells = NULL;
	Value                   *result;
	EvalPos                 pos;
	gint                    i;
	gboolean                answer, sensitivity, limits, performance;
	gboolean                program, dual_program;
	gchar                   *errmsg = _("Unknown error.");
	SolverParameters        *param;

	param = state->sheet->solver_parameters;

	if (state->warning_dialog != NULL)
		gtk_widget_destroy (state->warning_dialog);

	target_range = gnm_expr_entry_parse_as_value (state->target_entry,
						      state->sheet);
	input_range = gnm_expr_entry_parse_as_value (state->change_cell_entry,
						     state->sheet);

	if (target_range == NULL || input_range == NULL) {
		gnumeric_notice_nonmodal
			((GtkWindow *) state->dialog,
			  &(state->warning_dialog),
			  GTK_MESSAGE_WARNING, _("You have not specified "
						 "a problem to be solved"));
		return;
	}

	if (param->input_entry_str != NULL)
		g_free (param->input_entry_str);
	param->input_entry_str = value_get_as_string (input_range);

	param->target_cell =
		sheet_cell_fetch (state->sheet,
				  target_range->v_range.cell.a.col,
				  target_range->v_range.cell.a.row );

	/* Check that the target cell type is number. */
	if (! cell_is_number (param->target_cell)) {
		gnumeric_notice_nonmodal
			((GtkWindow *) state->dialog,
			 &(state->warning_dialog),
			 GTK_MESSAGE_WARNING, _("Target cell should contain "
						"a formula."));
		return;
	}

	result = workbook_foreach_cell_in_range (
		eval_pos_init_sheet (&pos, state->sheet),
		input_range, CELL_ITER_ALL, grab_cells, &input_cells);

	param->input_cells = input_cells;

	param->problem_type =
		gnumeric_glade_group_value (state->gui, problem_type_group);
	param->options.model_type =
		gnumeric_glade_group_value (state->gui, model_type_group);

	for (i = 0; algorithm_defs [i].name; i++) {
		GtkEntry             *entry = GTK_ENTRY 
			(GTK_COMBO (state->algorithm_combo)->entry);
		G_CONST_RETURN gchar *name = gtk_entry_get_text (entry);

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
	param->options.show_iter_results = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (glade_xml_get_widget
				    (state->gui, "show_iter_button")));

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

	dual_program = FALSE;
	param->options.dual_program_report = dual_program;

	i = check_int_constraints (input_range, state->constraint_list);

	if (i != -1) {
		char *str;
		char *s;

		gtk_clist_get_text (state->constraint_list, i, 0, &s);
		str = g_strdup_printf
			(_("Constraint `%s' is for a cell that "
			   "is not an input cell."), s);
		gnumeric_notice_nonmodal ((GtkWindow *) state->dialog,
					  &(state->warning_dialog),
					  GTK_MESSAGE_ERROR, str);
		g_free (str);
		goto out;
	}

	conv.sheet     = state->sheet;
	conv.c_listing = state->constraint_list;
	convert_constraint_format (&conv);
	if (param->constraints != NULL) {
		g_slist_foreach	(param->constraints, cb_destroy, NULL);
		g_slist_free (param->constraints);
		param->constraints = NULL;
	}
	param->constraints = conv.c_list;

	state->ov_target = value_get_as_float (param->target_cell->value);
	state->ov = save_original_values (input_cells);
	state->ov_stack = g_slist_prepend (state->ov_stack, state->ov);
	state->ov_cell_stack = g_slist_prepend (state->ov_cell_stack,
						input_cells);


	res = solver (WORKBOOK_CONTROL (state->wbcg), state->sheet, &errmsg);
	workbook_recalc (state->sheet->workbook);

	if (res != NULL) {
		solver_reporting (state, res, errmsg);
		solver_results_free (res);
	} else
		gnumeric_notice_nonmodal (GTK_WINDOW (state->dialog),
			 &(state->warning_dialog),
			 GTK_MESSAGE_WARNING, errmsg);
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

	state->gui = gnm_glade_xml_new (COMMAND_CONTEXT (state->wbcg),
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
				   "solver.html");

	state->add_button  = glade_xml_get_widget (state->gui, "addbutton");
	g_signal_connect (G_OBJECT (state->add_button), "clicked",
			  G_CALLBACK (cb_dialog_add_clicked), state);

	state->change_button = glade_xml_get_widget (state->gui,
						     "changebutton");
	g_signal_connect (G_OBJECT (state->change_button), "clicked",
			  G_CALLBACK (cb_dialog_change_clicked), state);

	state->delete_button = glade_xml_get_widget (state->gui,
						     "deletebutton");
	g_signal_connect (G_OBJECT (state->delete_button), "clicked",
			  G_CALLBACK (cb_dialog_delete_clicked), state);

	/* target_entry */
	table = GTK_TABLE (glade_xml_get_widget (state->gui,
						 "parameter_table"));
	state->target_entry = gnumeric_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->target_entry,
				  GNM_EE_SINGLE_RANGE |
				  GNM_EE_SHEET_OPTIONAL, GNM_EE_MASK);
        gnm_expr_entry_set_scg (state->target_entry,
				wbcg_cur_scg (state->wbcg));
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
	state->change_cell_entry = gnumeric_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->change_cell_entry,
				  GNM_EE_SINGLE_RANGE |
				  GNM_EE_SHEET_OPTIONAL, GNM_EE_MASK);
        gnm_expr_entry_set_scg (state->change_cell_entry,
				wbcg_cur_scg (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->change_cell_entry),
			  1, 2, 2, 3,
			  GTK_EXPAND | GTK_FILL, 0, 0, 0);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->change_cell_entry));
	gtk_widget_show (GTK_WIDGET (state->change_cell_entry));
	g_signal_connect_after (G_OBJECT (state->change_cell_entry), "changed",
		G_CALLBACK (dialog_set_main_button_sensitivity), state);

	/* Algorithm */
	state->algorithm_combo = GTK_OPTION_MENU
		(glade_xml_get_widget (state->gui, "algorithm_combo"));
	switch (param->options.model_type) {
	case SolverLPModel:
		gtk_combo_set_popdown_strings
			(GTK_COMBO (state->algorithm_combo), lp_alg_name_list);
		break;
	case SolverQPModel:
		gtk_combo_set_popdown_strings
			(GTK_COMBO (state->algorithm_combo), qp_alg_name_list);
		break;
	default:
		break;
	}
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
	state->lhs_entry = gnumeric_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->lhs_entry,
				  GNM_EE_SINGLE_RANGE |
				  GNM_EE_SHEET_OPTIONAL, GNM_EE_MASK);
        gnm_expr_entry_set_scg (state->lhs_entry, wbcg_cur_scg (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->lhs_entry),
			  0, 1, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->lhs_entry));
	gtk_widget_show (GTK_WIDGET (state->lhs_entry));
	g_signal_connect_after (G_OBJECT (state->lhs_entry),
		"changed",
		G_CALLBACK (dialog_set_sec_button_sensitivity), state);

/* rhs_entry */
	table = GTK_TABLE (glade_xml_get_widget (state->gui, "edit-table"));
	state->rhs_entry = gnumeric_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->rhs_entry,
				  GNM_EE_SINGLE_RANGE |
				  GNM_EE_SHEET_OPTIONAL, GNM_EE_MASK);
        gnm_expr_entry_set_scg (state->rhs_entry, wbcg_cur_scg (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->rhs_entry),
			  2, 3, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0, 0, 0);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->rhs_entry));
	gtk_widget_show (GTK_WIDGET (state->rhs_entry));
	g_signal_connect_after (G_OBJECT (state->rhs_entry),
		"changed",
		G_CALLBACK (dialog_set_sec_button_sensitivity), state);

/* type_menu */
	state->type_combo = GTK_OPTION_MENU
		(glade_xml_get_widget (state->gui, "type_menu"));
	g_signal_connect (G_OBJECT (gtk_option_menu_get_menu
				    (state->type_combo)), "selection-done",
			  G_CALLBACK (dialog_set_sec_button_sensitivity),
			  state);
	g_signal_connect (G_OBJECT (gtk_option_menu_get_menu
				    (state->type_combo)), "selection-done",
			  G_CALLBACK (cb_dialog_set_rhs_sensitivity), state);

/* constraint_list */
	state->constraint_list = GTK_CLIST (glade_xml_get_widget
					    (state->gui, "constraint_list"));
	state->selected_row = -1;
	g_signal_connect (G_OBJECT (state->constraint_list), "select-row",
			  G_CALLBACK (constraint_select_click), state);
	g_signal_connect (G_OBJECT (state->constraint_list), "unselect-row",
			  G_CALLBACK (constraint_unselect_click), state);
	gtk_clist_set_reorderable (state->constraint_list, TRUE);
	gtk_clist_set_use_drag_icons (state->constraint_list, TRUE);

/* dialog */
	wbcg_edit_attach_guru (state->wbcg, state->dialog);

	g_signal_connect (G_OBJECT (state->dialog), "destroy",
			  G_CALLBACK (dialog_destroy), state);

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

	conv.c_listing = state->constraint_list;
	conv.c_list    = param->constraints;
	conv.sheet     = state->sheet;
	revert_constraint_format (&conv);

/* Done */

	gnm_expr_entry_grab_focus (state->target_entry, FALSE);

	dialog_set_main_button_sensitivity (NULL, state);
	dialog_set_sec_button_sensitivity (NULL, state);

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
dialog_solver (WorkbookControlGUI *wbcg, Sheet *sheet)
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
	state->wb             = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet          = sheet;
	state->ov             = NULL;
	state->ov_stack       = NULL;
	state->ov_cell_stack  = NULL;
	state->warning_dialog = NULL;

	if (dialog_init (state)) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
				 _("Could not create the Solver dialog."));
		g_free (state);
		return;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SOLVER_KEY);

	gtk_widget_show (state->dialog);
}
