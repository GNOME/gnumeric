/*
 * dialog-solver.c:
 *
 * Author:
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 *
 * (C) Copyright 2000 by Jukka-Pekka Iivonen <iivonen@iki.fi>
 */

#include <config.h>
#include <gnome.h>
#include <string.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "utils-dialog.h"
#include "func.h"
#include "tools.h"
#include "value.h"
#include "cell.h"
#include "sheet.h"
#include "workbook.h"
#include "parse-util.h"
#include "utils-dialog.h"

/* Different constraint types */
static char const * constraint_strs[] = {
        N_("<="),
	N_(">="),
	N_("="),
	N_("Int"),
/*	N_("Bool"), */
	NULL
};

typedef struct {
        GtkWidget *dialog;
        GSList    *constraints;
        GtkCList  *clist;
        Sheet     *sheet;
	gint	   selected_row;

        WorkbookControlGUI  *wbcg;
} constraint_dialog_t;

static void
linearmodel_toggled (GtkWidget *widget, Sheet *sheet)
{
        sheet->solver_parameters.options.assume_linear_model =
	        GTK_TOGGLE_BUTTON (widget)->active;
}

static void
nonnegative_toggled (GtkWidget *widget, Sheet *sheet)
{
        sheet->solver_parameters.options.assume_non_negative =
	        GTK_TOGGLE_BUTTON (widget)->active;
}

static void
dialog_solver_options (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	GladeXML  *gui;
	GtkWidget *dia;
	GtkWidget *linearmodel;
	GtkWidget *nonnegative;
	gint      v, old_lm, old_nn;

	gui = gnumeric_glade_xml_new (wbcg, "solver-options.glade");
        if (gui == NULL)
                return;

	old_lm = sheet->solver_parameters.options.assume_linear_model;
	old_nn = sheet->solver_parameters.options.assume_non_negative;

	dia = glade_xml_get_widget (gui, "SolverOptions");
	if (!dia) {
		printf ("Corrupt file solver-options.glade\n");
		return;
	}

	linearmodel = glade_xml_get_widget (gui, "linearmodel");
        if (old_lm)
	        gtk_toggle_button_set_active ((GtkToggleButton *)
					      linearmodel, old_lm);
	gtk_signal_connect (GTK_OBJECT (linearmodel), "toggled",
			    GTK_SIGNAL_FUNC (linearmodel_toggled), sheet);

	nonnegative = glade_xml_get_widget (gui, "nonnegative");
	if (old_nn)
	        gtk_toggle_button_set_active ((GtkToggleButton *)
					      nonnegative, old_nn);
	gtk_signal_connect (GTK_OBJECT (nonnegative), "toggled",
			    GTK_SIGNAL_FUNC (nonnegative_toggled), sheet);

	gtk_widget_set_sensitive (linearmodel, FALSE);

	/* Run the dialog */
	gtk_window_set_modal (GTK_WINDOW (dia), TRUE);
	v = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dia));

	if (v != 0) {
	        sheet->solver_parameters.options.assume_linear_model = old_lm;
		sheet->solver_parameters.options.assume_non_negative = old_nn;
	}

	if (v != -1)
		gtk_object_destroy (GTK_OBJECT (dia));

	gtk_object_unref (GTK_OBJECT (gui));
}

static int
add_constraint (constraint_dialog_t *constraint_dialog,
		int lhs_col, int lhs_row, int rhs_col, int rhs_row,
		int cols, int rows, const char *type_str)
{
	SolverConstraint *constraint;
	char             *constraint_str[2] = { NULL, NULL };
	gint             row;

	constraint = g_new (SolverConstraint, 1);
	constraint->lhs.col = lhs_col;
	constraint->lhs.row = lhs_row;
	constraint->rhs.col = rhs_col;
	constraint->rhs.row = rhs_row;
	constraint->cols = cols;
	constraint->rows = rows;
	constraint->str = constraint_str[0] =
	  write_constraint_str (lhs_col, lhs_row, rhs_col, rhs_row,
				type_str, cols, rows);
	constraint->type = g_strdup (type_str);

	row = gtk_clist_append (constraint_dialog->clist, constraint_str);
	constraint_dialog->constraints =
	        g_slist_append(constraint_dialog->constraints,
			       (gpointer) constraint);
	gtk_clist_set_row_data (constraint_dialog->clist, row,
				(gpointer) constraint);

	return 0;
}

/* 'Constraint Add' button clicked */
static void
constr_add_click (GtkWidget *widget, constraint_dialog_t *constraint_dialog)
{
	GladeXML     *gui;
	GtkWidget    *dialog;
	GtkWidget    *lhs_entry;
	GtkWidget    *rhs_entry;
	GtkWidget    *type_entry;
	GtkWidget    *combo_entry;
	GList        *constraint_type_strs;

	int          selection;
	char         *lhs_text, *rhs_text;
	int          rhs_col, rhs_row;
	int          lhs_col, lhs_row;
	int          lhs_cols, lhs_rows;
	int          rhs_cols, rhs_rows;
	char         *type_str;

	gui = gnumeric_glade_xml_new (constraint_dialog->wbcg, "solver.glade");
        if (gui == NULL)
                return;

	constraint_type_strs = add_strings_to_glist (constraint_strs);

	dialog = glade_xml_get_widget (gui, "SolverAddConstraint");
	lhs_entry = glade_xml_get_widget (gui, "entry1");
	rhs_entry = glade_xml_get_widget (gui, "entry2");
	type_entry = glade_xml_get_widget (gui, "combo1");
	combo_entry = glade_xml_get_widget (gui, "combo-entry1");

	if (!dialog || !lhs_entry || !rhs_entry ||
	    !type_entry || !combo_entry) {
		printf ("Corrupt file solver.glade\n");
		return;
	}

	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (lhs_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (combo_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (rhs_entry));

	gtk_widget_set_sensitive (combo_entry, FALSE);

	gtk_combo_set_popdown_strings (GTK_COMBO (type_entry),
				       constraint_type_strs);

	gtk_widget_hide (constraint_dialog->dialog);

add_dialog:

	/* Run the dialog */
	gtk_widget_grab_focus (lhs_entry);
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	selection = gnumeric_dialog_run (constraint_dialog->wbcg,
					 GNOME_DIALOG (dialog));

	if (selection == -1 || selection == GNOME_CANCEL) {
		if (selection == -1)
			gtk_object_destroy (GTK_OBJECT (gui));
		else
			gnome_dialog_close (GNOME_DIALOG (dialog));
		gtk_widget_show (constraint_dialog->dialog);
		return;
	}

	lhs_text = gtk_entry_get_text (GTK_ENTRY (lhs_entry));
	if (!parse_cell_name_or_range (lhs_text, &lhs_col, &lhs_row,
				       &lhs_cols, &lhs_rows, TRUE)) {
		gtk_widget_grab_focus (lhs_entry);
		gtk_entry_set_position(GTK_ENTRY (lhs_entry), 0);
		gtk_entry_select_region(GTK_ENTRY (lhs_entry), 0,
					GTK_ENTRY(lhs_entry)->text_length);
		goto add_dialog;
	}

	type_str = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(type_entry)->entry));

	rhs_text = gtk_entry_get_text (GTK_ENTRY (rhs_entry));
	if ((strcmp (type_str, "Int") != 0 &&
	     strcmp (type_str, "Bool") != 0) &&
	    !parse_cell_name_or_range (rhs_text, &rhs_col, &rhs_row,
				       &rhs_cols, &rhs_rows, TRUE)) {
		gtk_widget_grab_focus (rhs_entry);
		gtk_entry_set_position(GTK_ENTRY (rhs_entry), 0);
		gtk_entry_select_region(GTK_ENTRY (rhs_entry), 0,
					GTK_ENTRY(rhs_entry)->text_length);
		goto add_dialog;
	}

	if ((strcmp (type_str, "Int") != 0 &&
	     strcmp (type_str, "Bool") != 0)) {

	        if (lhs_cols != rhs_cols || lhs_rows != rhs_rows) {
		        gnumeric_notice (constraint_dialog->wbcg,
					 GNOME_MESSAGE_BOX_ERROR,
					 _("The constraints having cell "
					   "ranges in LHS and RHS should have "
					   "the same number of columns and "
					   "rows in both sides."));
			goto add_dialog;
		}

		if (lhs_cols != 1 && lhs_rows != 1) {
		        gnumeric_notice (constraint_dialog->wbcg,
					 GNOME_MESSAGE_BOX_ERROR,
					 _("The cell range in LHS or RHS "
					   "should have only one column or "
					   "row."));
			goto add_dialog;
		}

	}

	if (add_constraint(constraint_dialog, lhs_col, lhs_row, rhs_col,
			   rhs_row, lhs_cols, lhs_rows, type_str))
	        goto add_dialog;

	gtk_entry_set_text (GTK_ENTRY (lhs_entry), "");
	gtk_entry_set_text (GTK_ENTRY (rhs_entry), "");

	if (selection == 2)
	        goto add_dialog;

	if (selection != -1)
		gtk_object_destroy (GTK_OBJECT (dialog));

	gtk_object_unref (GTK_OBJECT (gui));
	gtk_widget_show (constraint_dialog->dialog);
}

/* 'Constraint Change' button clicked */
static void
constr_change_click (GtkWidget *widget, constraint_dialog_t *state)
{
	SolverConstraint *constraint;

	GladeXML  *gui;
	GtkWidget *dia;
	GtkWidget *lhs_entry;
	GtkWidget *rhs_entry;
	GtkWidget *type_combo;
	GtkWidget *combo_entry;
	GList     *constraint_type_strs;
	gint      v;
	gchar     *txt, *entry;
	int       col, row;
	int       lhs_cols, lhs_rows;
	int       rhs_cols, rhs_rows;

	gui = gnumeric_glade_xml_new (state->wbcg, "solver.glade");
        if (gui == NULL)
                return;

	if (state->selected_row < 0)
		return;

	constraint_type_strs = add_strings_to_glist (constraint_strs);

	dia = glade_xml_get_widget (gui, "SolverChangeConstraint");
	lhs_entry = glade_xml_get_widget (gui, "entry3");
	rhs_entry = glade_xml_get_widget (gui, "entry4");
	type_combo = glade_xml_get_widget (gui, "combo2");
	combo_entry = glade_xml_get_widget (gui, "combo-entry2");

	if (!dia || !lhs_entry || !rhs_entry || !type_combo || !combo_entry) {
		printf ("Corrupt file solver.glade\n");
		return;
	}

	gnome_dialog_editable_enters (GNOME_DIALOG (dia),
				      GTK_EDITABLE (lhs_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dia),
				      GTK_EDITABLE (combo_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dia),
				      GTK_EDITABLE (rhs_entry));

	gtk_widget_set_sensitive (combo_entry, FALSE);

	gtk_combo_set_popdown_strings (GTK_COMBO (type_combo),
				       constraint_type_strs);

	constraint = (SolverConstraint *)
	        gtk_clist_get_row_data (state->clist, state->selected_row);

	g_return_if_fail (constraint != NULL);

	if (constraint->cols == 1 && constraint->rows == 1)
	        gtk_entry_set_text (GTK_ENTRY (lhs_entry),
				    cell_pos_name (&constraint->lhs));
	else {
		char *buf, *s1;

		s1 = g_strdup (cell_pos_name (&constraint->lhs));
		buf = g_strdup_printf ("%s:%s", s1,
				       cell_coord_name (constraint->lhs.col +
							constraint->cols - 1,
							constraint->lhs.row +
							constraint->rows - 1));
		g_free (s1);

		gtk_entry_set_text (GTK_ENTRY (lhs_entry), buf);

		g_free (buf);
	}

	if (strcmp (constraint->type, "Int") != 0 &&
	    strcmp (constraint->type, "Bool") != 0) {
	        if (constraint->cols == 1 && constraint->rows == 1)
		        gtk_entry_set_text (GTK_ENTRY (rhs_entry),
					    cell_pos_name (&constraint->rhs));
		else {
			char *buf, *s1;

			s1 = g_strdup (cell_pos_name (&constraint->rhs));
			buf = g_strdup_printf ("%s:%s", s1,
					       cell_coord_name (constraint->rhs.col +
								constraint->cols - 1,
								constraint->rhs.row +
								constraint->rows - 1));
			g_free (s1);

			gtk_entry_set_text (GTK_ENTRY (rhs_entry), buf);

			g_free (buf);
		}
	}

	gtk_entry_set_text (GTK_ENTRY (combo_entry), constraint->type);
	gtk_widget_hide (state->dialog);
	gtk_entry_set_position(GTK_ENTRY (lhs_entry), 0);
	gtk_entry_select_region(GTK_ENTRY (lhs_entry), 0,
				GTK_ENTRY(lhs_entry)->text_length);

	/* Run the dialog */
	gtk_widget_grab_focus (lhs_entry);
	gtk_window_set_modal (GTK_WINDOW (dia), TRUE);
loop:
	v = gnumeric_dialog_run (state->wbcg, GNOME_DIALOG (dia));

	if (v == 0) {
	        gchar *constraint_str[2] = { NULL, NULL };
	        entry = gtk_entry_get_text (GTK_ENTRY (lhs_entry));
		txt = (gchar *) cell_pos_name (&constraint->lhs);
		if (strcmp (entry, txt) != 0) {
		        if (!parse_cell_name_or_range (entry, &col, &row,
						       &lhs_cols, &lhs_rows, TRUE)) {
			        gtk_widget_grab_focus (lhs_entry);
				gtk_entry_set_position(GTK_ENTRY (lhs_entry),
						       0);
				gtk_entry_select_region(GTK_ENTRY (lhs_entry),
							0,
							GTK_ENTRY(lhs_entry)->
							  text_length);
				goto loop;
			}
			constraint->lhs.col = col;
			constraint->lhs.row = row;
		}

		entry = gtk_entry_get_text (GTK_ENTRY (combo_entry));
		constraint->type = g_strdup (entry);

		if (strcmp (constraint->type, "Int") == 0 ||
		    strcmp (constraint->type, "Bool") == 0)
		        goto skip_rhs;

		entry = gtk_entry_get_text (GTK_ENTRY (rhs_entry));
		txt = (gchar *) cell_pos_name (&constraint->rhs);
		if (strcmp (entry, txt) != 0) {
		        if (!parse_cell_name_or_range (entry, &col, &row,
						       &rhs_cols, &rhs_rows, TRUE)) {
			        gtk_widget_grab_focus (rhs_entry);
				gtk_entry_set_position(GTK_ENTRY (rhs_entry),
						       0);
				gtk_entry_select_region(GTK_ENTRY (rhs_entry),
							0,
							GTK_ENTRY(rhs_entry)->
							  text_length);
				goto loop;
			}
			constraint->rhs.col = col;
			constraint->rhs.row = row;
		}

		if (lhs_cols != rhs_cols || lhs_rows != rhs_rows) {
		        gnumeric_notice (state->wbcg,
					 GNOME_MESSAGE_BOX_ERROR,
					 _("The constraints having cell ranges"
					   " in LHS and RHS should have the "
					   "same number of columns and rows "
					   "in both sides."));
			goto loop;
		}

		if (lhs_cols != 1 && lhs_rows != 1) {
		        gnumeric_notice (state->wbcg,
					 GNOME_MESSAGE_BOX_ERROR,
					 _("The cell range in LHS or RHS "
					   "should have only one column or "
					   "row."));
			goto loop;
		}

	skip_rhs:
		constraint->cols = lhs_cols;
		constraint->rows = lhs_rows;

		constraint->str = constraint_str[0] =
			write_constraint_str (constraint->lhs.col, constraint->lhs.row,
					      constraint->rhs.col, constraint->rhs.row,
					      constraint->type,
					      constraint->cols, constraint->rows);

	        gtk_clist_remove (state->clist, state->selected_row);
	        gtk_clist_insert (state->clist, state->selected_row, constraint_str);
		gtk_clist_set_row_data (state->clist, state->selected_row,
					(gpointer) constraint);
	}

	if (v != -1)
		gtk_object_destroy (GTK_OBJECT (dia));

	gtk_object_unref (GTK_OBJECT (gui));
	gtk_widget_show (state->dialog);
}


/* 'Constraint Delete' button clicked */
static void
constr_delete_click (GtkWidget *widget, constraint_dialog_t *state)
{
	if (state->selected_row < 0) {
		gpointer p = gtk_clist_get_row_data (state->clist,
						     state->selected_row);
		state->constraints = g_slist_remove (state->constraints, p);
	        gtk_clist_remove (state->clist, state->selected_row);
		state->selected_row = -1;
	}
}

static void
constraint_select_click (GtkWidget      *clist,
			 gint           row,
			 gint           column,
			 GdkEventButton *event,
			 constraint_dialog_t *state)
{
        state->selected_row = row;
}

static void
max_toggled(GtkWidget *widget, SolverParameters *data)
{
        if (GTK_TOGGLE_BUTTON (widget)->active)
	        data->problem_type = SolverMaximize;
}

static void
min_toggled(GtkWidget *widget, SolverParameters *data)
{
        if (GTK_TOGGLE_BUTTON (widget)->active)
	        data->problem_type = SolverMinimize;
}

static void
original_values_toggled(GtkWidget *widget, gboolean *data)
{
        if (GTK_TOGGLE_BUTTON (widget)->active)
	        *data = FALSE;
	else
	        *data = TRUE;
}

static void
solver_values_toggled(GtkWidget *widget, gboolean *data)
{
        if (GTK_TOGGLE_BUTTON (widget)->active)
	        *data = TRUE;
	else
	        *data = FALSE;
}

static void
report_button_toggled(GtkWidget *widget, gboolean *data)
{
	*data = GTK_TOGGLE_BUTTON (widget)->active;
}

static gboolean
dialog_results (WorkbookControlGUI *wbcg, int res, gboolean ilp,
		gboolean *answer, gboolean *sensitivity, gboolean *limits)
{
	GladeXML  *gui;
	GtkWidget *dialog;
	GtkWidget *label;
	GtkWidget *checkbutton1;
	GtkWidget *checkbutton2;
	GtkWidget *checkbutton3;
	GtkWidget *radiobutton3;
	GtkWidget *radiobutton4;
	gchar     *label_txt = "";
        gboolean  answer_s, sensitivity_s, limits_s;
	gboolean  keep_solver_solution = TRUE;
	int       selection;

	gui = gnumeric_glade_xml_new (wbcg, "solver.glade");
        if (gui == NULL)
                return FALSE;

	answer_s = sensitivity_s = limits_s = FALSE;

	switch (res){
	case SOLVER_LP_OPTIMAL:
	        if (ilp)
		        answer_s = TRUE;
		else
		        answer_s = sensitivity_s = TRUE;

	        label_txt = "Solver found an optimal solution. All "
		  "constraints and \noptimality conditions are satisfied.\n";
		break;
	case SOLVER_LP_UNBOUNDED:
		label_txt = "The Target Cell value does not converge.\n";
	        break;
	default:
	        break;
	}

	dialog = glade_xml_get_widget (gui, "SolverResults");
	label = glade_xml_get_widget (gui, "result-label");
	checkbutton1 = glade_xml_get_widget (gui, "checkbutton1");
	checkbutton2 = glade_xml_get_widget (gui, "checkbutton2");
	checkbutton3 = glade_xml_get_widget (gui, "checkbutton3");

	radiobutton3 = glade_xml_get_widget (gui, "radiobutton3");
	radiobutton4 = glade_xml_get_widget (gui, "radiobutton4");

	gtk_signal_connect (GTK_OBJECT (checkbutton1), "toggled",
			    GTK_SIGNAL_FUNC (report_button_toggled), answer);
	gtk_signal_connect (GTK_OBJECT (checkbutton2), "toggled",
			    GTK_SIGNAL_FUNC (report_button_toggled),
			    sensitivity);
	gtk_signal_connect (GTK_OBJECT (checkbutton3), "toggled",
			    GTK_SIGNAL_FUNC (report_button_toggled), limits);

	gtk_signal_connect (GTK_OBJECT (radiobutton3), "toggled",
			    GTK_SIGNAL_FUNC (solver_values_toggled),
			    &keep_solver_solution);
	gtk_signal_connect (GTK_OBJECT (radiobutton4), "toggled",
			    GTK_SIGNAL_FUNC (original_values_toggled),
			    &keep_solver_solution);

	gtk_label_set_text (GTK_LABEL (label), label_txt);

	gtk_widget_set_sensitive (checkbutton1, answer_s);
	gtk_widget_set_sensitive (checkbutton2, sensitivity_s);
	gtk_widget_set_sensitive (checkbutton3, limits_s);

	/* Run the dialog */
	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));

	if (selection != -1)
		gtk_object_destroy (GTK_OBJECT (dialog));

	gtk_object_unref (GTK_OBJECT (gui));

	return keep_solver_solution;
}

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

static void
free_original_values (GSList *ov)
{
	GSList *tmp;
	for (tmp = ov; tmp; tmp = tmp->next)
		g_free (tmp->data);
	g_slist_free (ov);
}



static gboolean
check_int_constraints (CellList *input_cells, GSList *constraints, char **s)
{
        CellList *cells;
	Cell     *cell;

        while (constraints) {
	        SolverConstraint *c = (SolverConstraint *) constraints->data;

		if (strcmp (c->type, "Int") == 0 ||
		    strcmp (c->type, "Bool") == 0) {
		        for (cells = input_cells; cells; cells = cells->next) {
			  cell = (Cell *) cells->data;
			  if (cell->pos.col == c->lhs.col &&
			      cell->pos.row == c->lhs.row)
			          goto ok;
			}
			*s = c->str;
			return TRUE;
		}
	ok:
		constraints = constraints->next;
	}

        return FALSE;
}

void
dialog_solver (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	GladeXML  *gui;
	GtkWidget *dialog;
	GtkWidget *target_entry, *input_entry;
	GtkWidget *radiobutton;
	GtkWidget *constraint_list;
	GtkWidget *constr_add_button;
	GtkWidget *constr_change_button;
	GtkWidget *constr_delete_button;
	GSList    *cur;
	gboolean  solver_solution;

	SolverParameters    *param;
	constraint_dialog_t *constraint_dialog;

	gchar      *target_entry_str;
	const char *text;
	int        selection, res;
	gnum_float    ov_target;
	Cell       *target_cell;
	CellList   *input_cells;
	int        target_cell_col, target_cell_row;
	int        error_flag, row;
	gboolean   answer, sensitivity, limits;

	gui = gnumeric_glade_xml_new (wbcg,
				"solver.glade");
        if (gui == NULL)
                return;

	param = &sheet->solver_parameters;

	constraint_dialog = g_new (constraint_dialog_t, 1);
	constraint_dialog->constraints = param->constraints;
	constraint_dialog->selected_row = -1;

	if (param->target_cell == NULL)
	        target_entry_str =
			g_strdup (cell_pos_name (&sheet->edit_pos));
	else
	        target_entry_str =
			g_strdup (cell_name (param->target_cell));

	dialog = glade_xml_get_widget (gui, "Solver");
	target_entry = glade_xml_get_widget (gui, "target-cell");
	input_entry = glade_xml_get_widget (gui, "input-cells");
	constraint_list = glade_xml_get_widget (gui, "clist1");

	constr_add_button = glade_xml_get_widget (gui, "add-button");
	constr_change_button = glade_xml_get_widget (gui, "change-button");
	constr_delete_button = glade_xml_get_widget (gui, "delete-button");

	if (!dialog || !target_entry || !input_entry || !constraint_list) {
		printf ("Corrupt file solver.glade\n");
		return;
	}

	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (target_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (input_entry));
	gtk_clist_column_titles_passive (GTK_CLIST (constraint_list));

	if (param->input_entry_str)
	        gtk_entry_set_text (GTK_ENTRY (input_entry),
				    param->input_entry_str);

	constraint_dialog->dialog = dialog;
	constraint_dialog->clist  = GTK_CLIST (constraint_list);
	constraint_dialog->sheet  = sheet;
	constraint_dialog->wbcg    = wbcg;

	gtk_entry_set_text (GTK_ENTRY (target_entry),
			    target_entry_str);
	g_free (target_entry_str);

	gtk_entry_select_region (GTK_ENTRY (target_entry),
				 0, GTK_ENTRY(target_entry)->text_length);
	gtk_signal_connect (GTK_OBJECT(constraint_list),
			    "select_row",
			    GTK_SIGNAL_FUNC(constraint_select_click),
			    constraint_dialog);

	gtk_signal_connect (GTK_OBJECT (constr_add_button),
			    "clicked",
			    GTK_SIGNAL_FUNC (constr_add_click),
			    constraint_dialog);
	gtk_signal_connect (GTK_OBJECT (constr_change_button),
			    "clicked",
			    GTK_SIGNAL_FUNC(constr_change_click),
			    constraint_dialog);
	gtk_signal_connect (GTK_OBJECT (constr_delete_button),
			    "clicked",
			    GTK_SIGNAL_FUNC(constr_delete_click),
			    constraint_dialog);

	radiobutton = glade_xml_get_widget (gui, "radiobutton1");
	gtk_signal_connect (GTK_OBJECT (radiobutton),   "toggled",
			    GTK_SIGNAL_FUNC (max_toggled),
			    &sheet->solver_parameters.problem_type);
	gtk_toggle_button_set_active ((GtkToggleButton *) radiobutton,
				      sheet->solver_parameters.problem_type ==
				      SolverMaximize);

	radiobutton = glade_xml_get_widget (gui, "radiobutton2");
	gtk_signal_connect (GTK_OBJECT (radiobutton),   "toggled",
			    GTK_SIGNAL_FUNC (min_toggled),
			    &sheet->solver_parameters.problem_type);
	gtk_toggle_button_set_active ((GtkToggleButton *) radiobutton,
				      sheet->solver_parameters.problem_type ==
				      SolverMinimize);

	row = 0;
	for (cur = constraint_dialog->constraints; cur != NULL; cur=cur->next) {
	        SolverConstraint *c = (SolverConstraint *) cur->data;
	        gchar *tmp[] = { NULL, NULL };

		tmp[0] = write_constraint_str (c->lhs.col, c->lhs.row,
					       c->rhs.col, c->rhs.row,
					       c->type, c->cols, c->rows);

	        gtk_clist_append (GTK_CLIST (constraint_list), tmp);
		gtk_clist_set_row_data (GTK_CLIST (constraint_list), row++,
					(gpointer) c);
		g_free (tmp[0]);
	}

	gtk_widget_grab_focus (target_entry);

main_dialog:

	/* Run the dialog */
	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));

	/* Save the changes in the constraints list anyway */
	sheet->solver_parameters.constraints = constraint_dialog->constraints;

	if (selection != -1) {
	        if (param->input_entry_str)
		        g_free (param->input_entry_str);
	        text = gtk_entry_get_text (GTK_ENTRY (input_entry));
		param->input_entry_str = g_strdup (text);
	}

	switch (selection) {
	case 1:
   	        /* Cancel */
		gtk_object_destroy (GTK_OBJECT (dialog));

		/* User close */

	case -1:
	        gtk_object_unref (GTK_OBJECT (gui));
	        return;
	case 2:
	        gtk_widget_hide (dialog);
	        dialog_solver_options (wbcg, sheet);
		goto main_dialog;


	default:
	        break;
	}

	/* Parse target cell entry */
	text = gtk_entry_get_text (GTK_ENTRY (target_entry));
	if (!parse_cell_name (text, &target_cell_col, &target_cell_row, TRUE, NULL)) {
 	        gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell name "
				   "for 'Target cell'"));
		gtk_widget_grab_focus (target_entry);
		gtk_entry_set_position(GTK_ENTRY (target_entry), 0);
		gtk_entry_select_region(GTK_ENTRY (target_entry), 0,
					GTK_ENTRY(target_entry)->text_length);
		goto main_dialog;
	}
	target_cell = sheet_cell_fetch (sheet, target_cell_col, target_cell_row);
	ov_target = value_get_as_float (target_cell->value);

	/* Parse input cells entry */
	text = gtk_entry_get_text (GTK_ENTRY (input_entry));

	input_cells = (CellList *)
	        parse_cell_name_list (sheet, text, &error_flag, TRUE);
	if (error_flag) {
 	        gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell names "
				   "in 'By changing cells'"));
		gtk_widget_grab_focus (input_entry);
		gtk_entry_set_position(GTK_ENTRY (input_entry), 0);
		gtk_entry_select_region(GTK_ENTRY (input_entry), 0,
					GTK_ENTRY(input_entry)->text_length);
		goto main_dialog;
	}

	sheet->solver_parameters.target_cell = target_cell;
	sheet->solver_parameters.input_cells = input_cells;

	if (selection == 0) {
	        gnum_float  *opt_x, *sh_pr;
		gboolean ilp;
		char     *s;
		GSList    *ov;

		if (check_int_constraints (input_cells,
					   constraint_dialog->constraints,
					   &s)) {
			char *str;

			str = g_strdup_printf
				(_("Constraint `%s' is for a cell that "
				   "is not an input cell."), s);
		        gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR, str);
			g_free (str);
			goto main_dialog;
		}
	        ov = save_original_values (input_cells);
	        if (sheet->solver_parameters.options.assume_linear_model) {
		        res = solver_lp (WORKBOOK_CONTROL (wbcg),
					 sheet, &opt_x, &sh_pr, &ilp);

			answer = sensitivity = limits = FALSE;
			gtk_widget_hide (dialog);
			solver_solution = dialog_results (wbcg, res, ilp,
							  &answer,
							  &sensitivity,
							  &limits);
			solver_lp_reports (WORKBOOK_CONTROL (wbcg),
					   sheet, ov, ov_target,
					   opt_x, sh_pr,
					   answer, sensitivity, limits);

			if (!solver_solution)
			        restore_original_values (input_cells, ov);
		} else {
		        printf("NLP not implemented yet!\n");
		}
		free_original_values (ov);
	}

	if (selection != -1)
		gtk_object_destroy (GTK_OBJECT (dialog));

	gtk_object_unref (GTK_OBJECT (gui));
}
