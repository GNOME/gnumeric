/*
 * dialog-solver.c:
 *
 * Author:
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 *
 * (C) Copyright 2000 by Jukka-Pekka Iivonen <iivonen@iki.fi>
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <utils-dialog.h>
#include <func.h>
#include <tools.h>
#include <value.h>
#include <cell.h>
#include <sheet.h>
#include <workbook-edit.h>
#include <workbook.h>
#include <parse-util.h>
#include <ranges.h>
#include <utils-dialog.h>
#include <widgets/gnumeric-expr-entry.h>

#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-help.h>
#include <string.h>
#include <glade/glade.h>

#define SOLVER_KEY            "solver-dialog"

typedef struct {
	GladeXML  *gui;
	GtkWidget *dialog;
	GnumericExprEntry *target_entry;
	GnumericExprEntry *change_cell_entry;
	GtkWidget *solve_button;
	GtkWidget *cancel_button;
	GtkWidget *close_button;
	GtkWidget *help_button;
	GtkWidget *add_button;
	GtkWidget *change_button;
	GtkWidget *delete_button;
	GnumericExprEntry *lhs_entry;  
	GnumericExprEntry *rhs_entry;
	GtkOptionMenu *type_combo;
	GtkCList *constraint_list;
	char *helpfile;
	gint selected_row;
	gnum_float ov_target;
	GSList *ov;
	GSList *ov_stack;
	GSList *ov_cell_stack;
	
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
} SolverState;



/* Different constraint types */
static char const * constraint_strs_untranslated[] = {
        N_("<="),
	N_(">="),
	N_("="),
 /* xgettext: Int == Integer constraint for the linear program */
	N_("Int"),
/*	N_("Bool"), */
	NULL
};

typedef struct {
	GtkCList *c_listing;
	GSList *c_list;
	Sheet *sheet;
} constraint_conversion_t;

typedef enum {
	CONSTRAINT_LESS_EQUAL = 0,
	CONSTRAINT_GREATER_EQUAL = 1,
	CONSTRAINT_EQUAL = 2,
	CONSTRAINT_INTEGER = 3,
	CONSTRAINT_BOOLEAN = 4,
	CONSTRAINT_NOSUCH_TYPE = 3
} constraint_type_t;

typedef struct {
	Value * lhs_value;
	Value * rhs_value;
	constraint_type_t type;
} constraint_t;

static const char *problem_type_group[] = {
	"min_button",
	"max_button",
	"equal_to_button",
	0
};

/**
 * is_hom_row_or_col_ref:
 * @Widget:
 *
 **/
static gboolean
is_hom_row_or_col_ref (GnumericExprEntry *entry_1, GnumericExprEntry *entry_2, Sheet *sheet)
{
	char const *text;
        Value *input_range_1;
        Value *input_range_2;

	gboolean res;
	
	text = gtk_entry_get_text (GTK_ENTRY (entry_1));
	input_range_1 = global_range_parse (sheet,text);
	text = gtk_entry_get_text (GTK_ENTRY (entry_2));
	input_range_2 = global_range_parse (sheet,text);
 
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
 * is_cell_ref:
 * @Widget:
 *
 **/
static gboolean
is_cell_ref (GnumericExprEntry *entry, Sheet *sheet, gboolean allow_multiple_cell)
{
	char const *text;
        Value *input_range;
	gboolean res;
	
	text = gtk_entry_get_text (GTK_ENTRY (entry));
	input_range = global_range_parse (sheet,text);
 
        if (input_range != NULL) {
		res = ((input_range->type == VALUE_CELLRANGE)  
		       && ( allow_multiple_cell || 
			    ((input_range->v_range.cell.a.col == 
			     input_range->v_range.cell.b.col)
			    && (input_range->v_range.cell.a.row == 
				input_range->v_range.cell.b.row))));
		value_release (input_range);
	} else {
		res = FALSE;
	}
	return res;
}

/**
 * dialog_set_sec_button_sensitivity:
 * @dummy:
 * @state:
 *
 **/
static void
dialog_set_sec_button_sensitivity (GtkWidget *dummy, SolverState *state)
{
	gboolean ready;
	gboolean select_ready;
	
	select_ready = (state->selected_row > -1);
	ready = is_cell_ref (state->lhs_entry, state->sheet, TRUE) &&
		((gnumeric_option_menu_get_selected_index (state->type_combo) 
		  == CONSTRAINT_INTEGER) 
		 || (gnumeric_option_menu_get_selected_index (state->type_combo) 
		     == CONSTRAINT_BOOLEAN)
		 || (is_hom_row_or_col_ref (state->lhs_entry, state->rhs_entry, state->sheet)));

	gtk_widget_set_sensitive (state->add_button, ready);
	gtk_widget_set_sensitive (state->change_button, select_ready && ready);
	gtk_widget_set_sensitive (state->delete_button, select_ready);

	return;
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
constraint_select_click (GtkWidget      *clist,
			 gint           row,
			 gint           column,
			 GdkEventButton *event,
			 SolverState *state)
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
constraint_unselect_click (GtkWidget      *clist,
			 gint           row,
			 gint           column,
			 GdkEventButton *event,
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
cb_dialog_delete_clicked (GtkWidget *button, SolverState *state)
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
cb_dialog_add_clicked (GtkWidget *button, SolverState *state)
{
	char const *text;
	gint selection;
	char *texts[2] = {NULL, NULL};
	constraint_t *the_constraint = g_new (constraint_t, 1);

	text = gtk_entry_get_text (GTK_ENTRY (state->lhs_entry));
	the_constraint->lhs_value =  global_range_parse (state->sheet,text);
	the_constraint->type = gnumeric_option_menu_get_selected_index (state->type_combo);
	if ((the_constraint->type != CONSTRAINT_INTEGER) && 
	    (the_constraint->type != CONSTRAINT_BOOLEAN)) {
		text = gtk_entry_get_text (GTK_ENTRY (state->rhs_entry));
		the_constraint->rhs_value =  global_range_parse (state->sheet,text);
/* FIXMEE: We are dropping cross sheet references!! */
		texts[0] = write_constraint_str (the_constraint->lhs_value->v_range.cell.a.col, 
					     the_constraint->lhs_value->v_range.cell.a.row,
					     the_constraint->rhs_value->v_range.cell.a.col, 
					     the_constraint->rhs_value->v_range.cell.a.row,
					     constraint_strs_untranslated[the_constraint->type],
					     the_constraint->lhs_value->v_range.cell.b.col -
					     the_constraint->lhs_value->v_range.cell.a.col + 1,
					     the_constraint->lhs_value->v_range.cell.b.row -
					     the_constraint->lhs_value->v_range.cell.a.row + 1);
	} else {
		the_constraint->rhs_value = NULL;
/* FIXME: We are dropping cross sheet references!! */
		texts[0] = write_constraint_str (the_constraint->lhs_value->v_range.cell.a.col, 
					     the_constraint->lhs_value->v_range.cell.a.row,
					     0, 0,
					     constraint_strs_untranslated[the_constraint->type],
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
	return;
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
dialog_set_main_button_sensitivity (GtkWidget *dummy, SolverState *state)
{
	gboolean ready = FALSE;

	ready = is_cell_ref (state->target_entry, state->sheet, FALSE)
		&& is_cell_ref (state->change_cell_entry, state->sheet, TRUE);
	gtk_widget_set_sensitive (state->solve_button, ready);
	return;
}

/**
 * cb_dialog_set_rhs_sensitivity:
 * @dummy:
 * @state:
 *
 **/
static void
cb_dialog_set_rhs_sensitivity (GtkWidget *dummy, SolverState *state)
{
	return;

/* FIXME: We would like to disable the rhs when appropriate. */
/* Unfortunately this confuses the widget:                   */

	if ((gnumeric_option_menu_get_selected_index (state->type_combo) 
		  == CONSTRAINT_INTEGER) 
		 || (gnumeric_option_menu_get_selected_index (state->type_combo) 
		     == CONSTRAINT_BOOLEAN)) {
		gtk_widget_set_sensitive (GTK_WIDGET (state->rhs_entry), FALSE);
	} else {
		gtk_widget_set_sensitive (GTK_WIDGET (state->rhs_entry), TRUE);
	}

	return;
}



/**
 * dialog_set_focus:
 * @window:
 * @focus_widget:
 * @state:
 *
 **/
static void
dialog_set_focus (GtkWidget *window, GtkWidget *focus_widget,
			SolverState *state)
{
	if (IS_GNUMERIC_EXPR_ENTRY (focus_widget)) {
		wbcg_set_entry (state->wbcg,
				    GNUMERIC_EXPR_ENTRY (focus_widget));
		gnumeric_expr_entry_set_absolute (GNUMERIC_EXPR_ENTRY (focus_widget));
	} else
		wbcg_set_entry (state->wbcg, NULL);
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
free_original_values (GSList *ov, gpointer user_data)
{
	GSList *tmp;
	for (tmp = ov; tmp; tmp = tmp->next)
		g_free (tmp->data);
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
		g_slist_foreach (state->ov_stack, (GFunc)free_original_values, NULL);
		g_slist_free (state->ov_stack);
		state->ov_stack = NULL;
		g_slist_free (state->ov_cell_stack);
		state->ov_cell_stack = NULL;
	}
	
	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		gtk_object_unref (GTK_OBJECT (state->gui));
		state->gui = NULL;
	}

	wbcg_edit_finish (state->wbcg, FALSE);

	state->dialog = NULL;

	g_free (state);

	return FALSE;
}

/**
 * dialog_help_cb:
 * @button:
 * @state:
 *
 * Provide help.
 **/
static void
dialog_help_cb (GtkWidget *button, SolverState *state)
{
	if (state->helpfile != NULL) {
		GnomeHelpMenuEntry help_ref;
		help_ref.name = "gnumeric";
		help_ref.path = state->helpfile;
		gnome_help_display (NULL, &help_ref);		
	}
	return;
}


/**
 * restore_original_values:
 * @input_cells:
 * @ov:
 *
 **/
static void
restore_original_values (CellList *input_cells, GSList *ov)
{
        while (ov != NULL) {
	        const char *str = ov->data;
	        Cell *cell = (Cell *)input_cells->data;

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
cb_dialog_cancel_clicked (GtkWidget *button, SolverState *state)
{
	if (state->ov_stack != NULL) {
		GSList *cells = state->ov_cell_stack;
		GSList *ov = state->ov_stack;
		while (cells != NULL && ov != NULL) {
			restore_original_values ((CellList *)cells->data, 
						 (GSList *)ov->data);
			cells = cells->next;
			ov = ov ->next;
		}
		g_slist_foreach (state->ov_stack, (GFunc)free_original_values, NULL);
		g_slist_free (state->ov_cell_stack);
		g_slist_free (state->ov_stack);
		state->ov_cell_stack = NULL;
		state->ov_stack = NULL;
		workbook_recalc (state->sheet->workbook);
	}
	
	gtk_widget_destroy (state->dialog);
	return;
}

/**
 * cb_dialog_close_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_dialog_close_clicked (GtkWidget *button, SolverState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
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
	*the_list = g_list_prepend (*the_list, cell);
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
	constraint_t * a_constraint;

	for (i = 0;;i++) {
		a_constraint = (constraint_t * ) gtk_clist_get_row_data (constraint_list, i);
		if (a_constraint == NULL)
			break;
		if ((a_constraint->type != CONSTRAINT_INTEGER) && 
		    (a_constraint->type != CONSTRAINT_BOOLEAN))
			continue;
		if (!global_range_contained (a_constraint->lhs_value, input_range))
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
convert_constraint_format (constraint_conversion_t * conv)
{
	gint i;
	constraint_t * a_constraint;
	SolverConstraint *engine_constraint;

	for (i = 0;;i++) {
		a_constraint = (constraint_t * ) gtk_clist_get_row_data (conv->c_listing, i);
		if (a_constraint == NULL)
			break;
		engine_constraint = g_new (SolverConstraint,1);
		engine_constraint->lhs.col  = a_constraint->lhs_value->v_range.cell.a.col;
		engine_constraint->lhs.row  = a_constraint->lhs_value->v_range.cell.a.row;
		engine_constraint->rows  = a_constraint->lhs_value->v_range.cell.b.row 
			- a_constraint->lhs_value->v_range.cell.a.row +1;
		engine_constraint->cols  = a_constraint->lhs_value->v_range.cell.b.col 
			- a_constraint->lhs_value->v_range.cell.a.col +1;
		engine_constraint->type = g_strdup (constraint_strs_untranslated
						    [a_constraint->type]);
		if ((a_constraint->type == CONSTRAINT_INTEGER) 
		    || (a_constraint->type == CONSTRAINT_BOOLEAN)) {
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
			constraint_strs_untranslated[a_constraint->type], 
			engine_constraint->cols,
			engine_constraint->rows);
		conv->c_list = g_slist_prepend (conv->c_list, engine_constraint);
	}
	return;	
}

/*
 *  get_constraint_type:
 *  @type_str: 
 *  
 */
static constraint_type_t
get_constraint_type (const char* type_str) 
{
	constraint_type_t i;
	
	for (i=0; constraint_strs_untranslated[i] != NULL; i++)
		if (strcmp (type_str, constraint_strs_untranslated[i]) == 0)
			return i;
	return CONSTRAINT_NOSUCH_TYPE;
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
	constraint_t * a_constraint;
	GSList *engine_constraint_list = conv->c_list;
	SolverConstraint *engine_constraint;
	gchar *text[2] = {NULL, NULL};
	Range r;

	while (engine_constraint_list != NULL) {
		engine_constraint = (SolverConstraint *) engine_constraint_list->data;
		a_constraint = g_new(constraint_t,1);
		r.start.col = engine_constraint->lhs.col;
		r.start.row = engine_constraint->lhs.row;
		r.end.col = r.start.col + engine_constraint->cols - 1;
		r.end.row = r.start.row + engine_constraint->rows - 1;
		a_constraint->lhs_value = value_new_cellrange_r (conv->sheet, &r);
		r.start.col = engine_constraint->rhs.col;
		r.start.row = engine_constraint->rhs.row;
		r.end.col = r.start.col + engine_constraint->cols - 1;
		r.end.row = r.start.row + engine_constraint->rows - 1;
		a_constraint->rhs_value = value_new_cellrange_r (conv->sheet, &r);
		a_constraint->type = get_constraint_type (engine_constraint->type);
		text[0] = engine_constraint->str;
		gtk_clist_set_row_data_full(conv->c_listing,
					    gtk_clist_prepend (conv->c_listing, text),
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
save_original_values (CellList *input_cells)
{
        GSList *ov = NULL;

	while (input_cells != NULL) {
	        Cell *cell = (Cell *) input_cells->data;
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
cb_destroy (gpointer data, gpointer user_data)
{
	g_free (data);
}



/**
 * cb_dialog_solve_clicked:
 * @button:
 * @state:
 *
 * 
 **/
static void
cb_dialog_solve_clicked (GtkWidget *button, SolverState *state)
{
	char const         *text;
	Value              *target_range;
	Value              *input_range;
        CellList           *input_cells = NULL;
	Value              *result;
	EvalPos            *pos;
	CellPos            cellpos = {0, 0};
	gint               i;
	gboolean           answer, sensitivity, limits;

	pos = g_new (EvalPos, 1);
	pos = eval_pos_init(pos, state->sheet, &cellpos);

	text = gtk_entry_get_text (GTK_ENTRY (state->target_entry));
	target_range = global_range_parse (state->sheet, text);
	text = gtk_entry_get_text (GTK_ENTRY (state->change_cell_entry));
	if (state->sheet->solver_parameters.input_entry_str != NULL)
		g_free(state->sheet->solver_parameters.input_entry_str);
	state->sheet->solver_parameters.input_entry_str = g_strdup(text);
	input_range = global_range_parse (state->sheet, text);

	state->sheet->solver_parameters.target_cell = sheet_cell_fetch (state->sheet, 
					target_range->v_range.cell.a.col, 
					target_range->v_range.cell.a.row );
	result = workbook_foreach_cell_in_range (pos, input_range,
		FALSE, grab_cells, &input_cells);
	
	state->sheet->solver_parameters.input_cells = input_cells;

	state->sheet->solver_parameters.problem_type = 
		gnumeric_glade_group_value (state->gui, problem_type_group);
	state->sheet->solver_parameters.options.assume_linear_model =  
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (
			glade_xml_get_widget (state->gui, "lin_model_button")));
	state->sheet->solver_parameters.options.assume_non_negative =  
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (
			glade_xml_get_widget (state->gui, "non_neg_button")));
	answer = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget (state->gui, "answer")));
	sensitivity = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget (state->gui, "sensitivity")));
	limits = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget (state->gui, "limits")));


	i = check_int_constraints (input_range, state->constraint_list);
	
	if (i == -1) {
		constraint_conversion_t conv = {NULL, NULL, NULL};
		conv.sheet = state->sheet;
		conv.c_listing = state->constraint_list;
		convert_constraint_format (&conv);
		if (state->sheet->solver_parameters.constraints != NULL) {
			g_slist_foreach (state->sheet->solver_parameters.constraints,
                                             cb_destroy, NULL);
			g_slist_free (state->sheet->solver_parameters.constraints);
			state->sheet->solver_parameters.constraints = NULL;
		}
		state->sheet->solver_parameters.constraints = conv.c_list;

		state->ov_target = value_get_as_float 
			(state->sheet->solver_parameters.target_cell->value);
		state->ov = save_original_values (input_cells);
		state->ov_stack = g_slist_prepend (state->ov_stack, state->ov);
		state->ov_cell_stack = g_slist_prepend (state->ov_cell_stack, input_cells);

	        if (state->sheet->solver_parameters.options.assume_linear_model) {
			int res;
			gnum_float  *opt_x, *sh_pr;
			gboolean ilp;
			

		        res = solver_lp (WORKBOOK_CONTROL (state->wbcg),
					 state->sheet, &opt_x, &sh_pr, &ilp);
			workbook_recalc (state->sheet->workbook);
			if (res == SOLVER_LP_OPTIMAL) {
				gnumeric_notice (state->wbcg, 
						 GNOME_MESSAGE_BOX_INFO, 
						 _("Solver found an optimal solution. All "
						   "constraints and optimality conditions "
						   "are satisfied.\n"));
			} else {
				char *str = "";
				if (res == SOLVER_LP_UNBOUNDED) {
					str = g_strdup_printf
						(_("The Target Cell value does "
						   "not converge!"));
				} else {
					str = g_strdup_printf
						(_("Solver was not successful:"
						   " %i"), res);
				}
				gnumeric_notice (state->wbcg, GNOME_MESSAGE_BOX_WARNING, str);
				g_free (str);
			
			}

			solver_lp_reports (WORKBOOK_CONTROL (state->wbcg),
					   state->sheet, state->ov, state->ov_target,
					   opt_x, sh_pr,
					   answer, sensitivity, limits);

		} else {
		        printf ("NLP not implemented yet!\n");
		}		
	} else {
		char *str;
		char *s;
		
		gtk_clist_get_text (state->constraint_list, i, 0, &s);
		str = g_strdup_printf
			(_("Constraint `%s' is for a cell that "
			   "is not an input cell."), s);
		gnumeric_notice (state->wbcg, GNOME_MESSAGE_BOX_ERROR, str);
		g_free (str);
	}


	if (target_range != NULL)
		value_release (target_range);
	if (input_range != NULL)
		value_release (input_range);

					      
	return;
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
	GtkTable *table;
	constraint_conversion_t conv;

	state->gui = gnumeric_glade_xml_new (state->wbcg, "solver.glade");
        if (state->gui == NULL)
                return TRUE;

	state->dialog = glade_xml_get_widget (state->gui, "Solver");
        if (state->dialog == NULL)
                return TRUE;


/*  buttons  */
	state->solve_button  = glade_xml_get_widget (state->gui, "solvebutton");
	gtk_signal_connect (GTK_OBJECT (state->solve_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_dialog_solve_clicked),
			    state);

	state->close_button  = glade_xml_get_widget (state->gui, "closebutton");
	gtk_signal_connect (GTK_OBJECT (state->close_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_dialog_close_clicked),
			    state);

	state->cancel_button  = glade_xml_get_widget (state->gui, "cancelbutton");
	gtk_signal_connect (GTK_OBJECT (state->cancel_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_dialog_cancel_clicked),
			    state);

	state->help_button     = glade_xml_get_widget (state->gui, "helpbutton");
	gtk_signal_connect (GTK_OBJECT (state->help_button), "clicked",
			    GTK_SIGNAL_FUNC (dialog_help_cb), state);

	state->add_button  = glade_xml_get_widget (state->gui, "addbutton");
	gtk_signal_connect (GTK_OBJECT (state->add_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_dialog_add_clicked),
			    state);

	state->change_button  = glade_xml_get_widget (state->gui, "changebutton");
	gtk_signal_connect (GTK_OBJECT (state->change_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_dialog_change_clicked),
			    state);

	state->delete_button  = glade_xml_get_widget (state->gui, "deletebutton");
	gtk_signal_connect (GTK_OBJECT (state->delete_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_dialog_delete_clicked),
			    state);



	/* target_entry */
	table = GTK_TABLE (glade_xml_get_widget (state->gui, "parameter_table"));
	state->target_entry = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags (state->target_entry,
				       GNUM_EE_SINGLE_RANGE | GNUM_EE_SHEET_OPTIONAL, 
				       GNUM_EE_MASK);
        gnumeric_expr_entry_set_scg (state->target_entry, 
				    wb_control_gui_cur_sheet (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->target_entry),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->target_entry));
	gtk_widget_show (GTK_WIDGET (state->target_entry));
	gtk_signal_connect_after (GTK_OBJECT (state->target_entry), "changed",
				  GTK_SIGNAL_FUNC (dialog_set_main_button_sensitivity), state);
	

	/* change_cell_entry */
	table = GTK_TABLE (glade_xml_get_widget (state->gui, "parameter_table"));
	state->change_cell_entry = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags (state->change_cell_entry,
				       GNUM_EE_SINGLE_RANGE | GNUM_EE_SHEET_OPTIONAL, 
				       GNUM_EE_MASK);
        gnumeric_expr_entry_set_scg (state->change_cell_entry, 
				     wb_control_gui_cur_sheet (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->change_cell_entry),
			  1, 2, 2, 3,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->change_cell_entry));
	gtk_widget_show (GTK_WIDGET (state->change_cell_entry));
	gtk_signal_connect_after (GTK_OBJECT (state->change_cell_entry), "changed",
				  GTK_SIGNAL_FUNC (dialog_set_main_button_sensitivity), state);


/* lhs_entry */
	table = GTK_TABLE (glade_xml_get_widget (state->gui, "edit-table"));
	state->lhs_entry = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags (state->lhs_entry,
                                      GNUM_EE_SINGLE_RANGE | GNUM_EE_SHEET_OPTIONAL, 
                                      GNUM_EE_MASK);
        gnumeric_expr_entry_set_scg (state->lhs_entry, 
				     wb_control_gui_cur_sheet (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->lhs_entry),
			  0, 1, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->lhs_entry));
	gtk_widget_show (GTK_WIDGET (state->lhs_entry));
	gtk_signal_connect_after (GTK_OBJECT (state->lhs_entry), "changed",
				  GTK_SIGNAL_FUNC (dialog_set_sec_button_sensitivity), state);


/* rhs_entry */
	table = GTK_TABLE (glade_xml_get_widget (state->gui, "edit-table"));
	state->rhs_entry = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags (state->rhs_entry,
				       GNUM_EE_SINGLE_RANGE | GNUM_EE_SHEET_OPTIONAL, 
				       GNUM_EE_MASK);
        gnumeric_expr_entry_set_scg (state->rhs_entry, 
				     wb_control_gui_cur_sheet (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->rhs_entry),
			  2, 3, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->rhs_entry));
	gtk_widget_show (GTK_WIDGET (state->rhs_entry));
	gtk_signal_connect_after (GTK_OBJECT (state->rhs_entry), "changed",
				  GTK_SIGNAL_FUNC (dialog_set_sec_button_sensitivity), state);

/* type_menu */
	state->type_combo = GTK_OPTION_MENU (glade_xml_get_widget (state->gui, "type_menu"));
	gtk_signal_connect (GTK_OBJECT (gtk_option_menu_get_menu (state->type_combo)), 
			    "selection-done",
			    GTK_SIGNAL_FUNC (dialog_set_sec_button_sensitivity), state);
	gtk_signal_connect (GTK_OBJECT (gtk_option_menu_get_menu (state->type_combo)), 
			    "selection-done",
			    GTK_SIGNAL_FUNC (cb_dialog_set_rhs_sensitivity), state);

/* constraint_list */
	state->constraint_list = GTK_CLIST (glade_xml_get_widget (state->gui, "constraint_list"));
	state->selected_row = -1;
	gtk_signal_connect (GTK_OBJECT (state->constraint_list), "select-row",
				  GTK_SIGNAL_FUNC (constraint_select_click), state);
	gtk_signal_connect (GTK_OBJECT (state->constraint_list), "unselect-row",
				  GTK_SIGNAL_FUNC (constraint_unselect_click), state);
	gtk_clist_set_reorderable (state->constraint_list, TRUE);
	gtk_clist_set_use_drag_icons (state->constraint_list, TRUE);



	
/* dialog */
	wbcg_edit_attach_guru (state->wbcg, state->dialog);

	gtk_signal_connect (GTK_OBJECT (state->dialog), "set-focus",
			    GTK_SIGNAL_FUNC (dialog_set_focus), state);

	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (dialog_destroy), state);

/* Loading the old solver specs... from state->sheet->solver_parameters  */

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "lin_model_button")),
			state->sheet->solver_parameters.options.assume_linear_model);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "non_neg_button")),
			state->sheet->solver_parameters.options.assume_non_negative);
	if (state->sheet->solver_parameters.input_entry_str != NULL)
		gtk_entry_set_text (GTK_ENTRY(state->change_cell_entry), 
				    state->sheet->solver_parameters.input_entry_str );
	if (state->sheet->solver_parameters.target_cell != NULL) 
		gtk_entry_set_text (GTK_ENTRY(state->target_entry), 
				    cell_name(state->sheet->solver_parameters.target_cell));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "max_button")),
			state->sheet->solver_parameters.problem_type == SolverMaximize);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		glade_xml_get_widget(state->gui, "min_button")),
			state->sheet->solver_parameters.problem_type == SolverMinimize);

	conv.c_listing = state->constraint_list;
	conv.c_list = state->sheet->solver_parameters.constraints;
	conv.sheet = state->sheet;
	revert_constraint_format (&conv);

/* Done */

	gtk_widget_grab_focus (GTK_WIDGET (state->target_entry));

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

	state = g_new (SolverState, 1);
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->helpfile = "solver.html";
	state->ov = NULL;
	state->ov_stack = NULL;
	state->ov_cell_stack = NULL;

	if (dialog_init (state)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Solver dialog."));
		g_free (state);
		return;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SOLVER_KEY);

	gtk_widget_show (state->dialog);

        return;
}
