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
#include "workbook.h"
#include "utils.h"
#include "utils-dialog.h"

/* Different constraint types */
static const char *constraint_strs[] = {
        N_("<="),
	N_(">="),
	N_("="),
/*	N_("int"),
	N_("bool"), */
	NULL
};



typedef struct {
        GtkWidget *dialog;
        GSList    *constraints;
        GtkCList  *clist;
        Sheet     *sheet;
        Workbook  *wb;
} constraint_dialog_t;


static void
linearmodel_toggled(GtkWidget *widget, Sheet *sheet)
{
        sheet->solver_parameters.options.assume_linear_model = 
	        GTK_TOGGLE_BUTTON (widget)->active;
}

static void
nonnegative_toggled(GtkWidget *widget, Sheet *sheet)
{
        sheet->solver_parameters.options.assume_non_negative = 
	        GTK_TOGGLE_BUTTON (widget)->active;
}

static void
dialog_solver_options (Workbook *wb, Sheet *sheet)

{
	GladeXML  *gui = glade_xml_new (GNUMERIC_GLADEDIR 
					"/solver-options.glade",
					NULL);
	GtkWidget *dia;
	GtkWidget *linearmodel;
	GtkWidget *nonnegative;
	gint      v, old_lm, old_nn;

	old_lm = sheet->solver_parameters.options.assume_linear_model;
	old_nn = sheet->solver_parameters.options.assume_non_negative;

	if (!gui) {
		printf ("Could not find solver-options.glade\n");
		return;
	}
	
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
	gtk_widget_set_sensitive (nonnegative, FALSE);

	/* Run the dialog */
	gtk_window_set_modal (GTK_WINDOW (dia), TRUE);
	v = gnumeric_dialog_run (wb, GNOME_DIALOG (dia));

	if (v == 1) {
	        sheet->solver_parameters.options.assume_linear_model = old_lm;
		sheet->solver_parameters.options.assume_non_negative = old_nn;
	}

	if (v != -1)
		gtk_object_destroy (GTK_OBJECT (dia));

	gtk_object_unref (GTK_OBJECT (gui));
}


static int
add_constraint(constraint_dialog_t *constraint_dialog,
	       int lhs_col, int lhs_row, int rhs_col, int rhs_row,
	       char *type_str)
{
	SolverConstraint *constraint;
	char             constraint_buf[512];
	char             *constraint_str[2] = { constraint_buf, NULL };
	gint             row;

	sprintf(constraint_buf, "%s %s ", cell_name (lhs_col, lhs_row),
		type_str);
	strcat(constraint_buf, cell_name(rhs_col, rhs_row));

	constraint = g_new (SolverConstraint, 1);
	constraint->lhs_col = lhs_col;
	constraint->lhs_row = lhs_row;
	constraint->rhs_col = rhs_col;
	constraint->rhs_row = rhs_row;

	row = gtk_clist_append (constraint_dialog->clist, constraint_str);
	constraint->type = g_malloc (strlen (type_str) + 1);
	strcpy (constraint->type, type_str);
	constraint->str = g_malloc (strlen (constraint_buf)+1);
	strcpy (constraint->str, constraint_buf);
	constraint_dialog->constraints = 
	        g_slist_append(constraint_dialog->constraints,
			       (gpointer) constraint);
	gtk_clist_set_row_data (constraint_dialog->clist, row, 
				(gpointer) constraint);

	return 0;
}

/* 'Constraint Add' button clicked */
static void
constr_add_click (constraint_dialog_t *constraint_dialog)
{
	GladeXML  *gui = glade_xml_new (GNUMERIC_GLADEDIR "/solver.glade",
					NULL);
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
	char         *type_str;

	if (!gui) {
		printf ("Could not find solver.glade\n");
		return;
	}
	
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

	gtk_combo_set_popdown_strings (GTK_COMBO (type_entry),
				       constraint_type_strs);

	gtk_widget_hide (constraint_dialog->dialog);

add_dialog:

	/* Run the dialog */
	gtk_widget_grab_focus (lhs_entry);
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	selection = gnumeric_dialog_run (constraint_dialog->wb,
					 GNOME_DIALOG (dialog));
	
	if (selection == 1) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		gtk_widget_show (constraint_dialog->dialog);
		return;
	}
	
	lhs_text = gtk_entry_get_text (GTK_ENTRY (lhs_entry));
	if (!parse_cell_name (lhs_text, &lhs_col, &lhs_row)) {
		gtk_widget_grab_focus (lhs_entry);
		gtk_entry_set_position(GTK_ENTRY (lhs_entry), 0);
		gtk_entry_select_region(GTK_ENTRY (lhs_entry), 0, 
					GTK_ENTRY(lhs_entry)->text_length);
		goto add_dialog;
	}
	rhs_text = gtk_entry_get_text (GTK_ENTRY (rhs_entry));
	if (!parse_cell_name (rhs_text, &rhs_col, &rhs_row)) {
		gtk_widget_grab_focus (rhs_entry);
		gtk_entry_set_position(GTK_ENTRY (rhs_entry), 0);
		gtk_entry_select_region(GTK_ENTRY (rhs_entry), 0, 
					GTK_ENTRY(rhs_entry)->text_length);
		goto add_dialog;
	}

	type_str = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(type_entry)->entry));

	if (add_constraint(constraint_dialog, lhs_col,
			   lhs_row, rhs_col, rhs_row,
			   type_str))
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

static gint selected_row = -1;

/* 'Constraint Change' button clicked */
static void
constr_change_click (constraint_dialog_t *data)
{
	SolverConstraint *constraint;

	GladeXML  *gui = glade_xml_new (GNUMERIC_GLADEDIR "/solver.glade",
					NULL);
	GtkWidget *dia;
	GtkWidget *lhs_entry;
	GtkWidget *rhs_entry;
	GtkWidget *type_combo;
	GtkWidget *combo_entry;
	GList     *constraint_type_strs;
	gint      v;
	gchar     *txt, *entry;
	int       col, row;

	if (selected_row < 0)
	        return;

	if (!gui) {
		printf ("Could not find solver.glade\n");
		return;
	}
	
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

	gtk_combo_set_popdown_strings (GTK_COMBO (type_combo),
				       constraint_type_strs);

	constraint = (SolverConstraint *)
	        gtk_clist_get_row_data (data->clist, selected_row);
	
	gtk_entry_set_text (GTK_ENTRY (lhs_entry), 
			    cell_name (constraint->lhs_col, 
				       constraint->lhs_row));
	gtk_entry_set_text (GTK_ENTRY (rhs_entry), 
			    cell_name (constraint->rhs_col, 
				       constraint->rhs_row));
	gtk_entry_set_text (GTK_ENTRY (combo_entry), constraint->type);
	gtk_widget_hide (data->dialog);
	gtk_entry_set_position(GTK_ENTRY (lhs_entry), 0);
	gtk_entry_select_region(GTK_ENTRY (lhs_entry), 0, 
				GTK_ENTRY(lhs_entry)->text_length);

	/* Run the dialog */
	gtk_widget_grab_focus (lhs_entry);
	gtk_window_set_modal (GTK_WINDOW (dia), TRUE);
loop:
	v = gnumeric_dialog_run (data->wb, GNOME_DIALOG (dia));

	if (v == 0) {
	        gchar buf[512];
	        gchar *constraint_str[2] = { buf, NULL };
	        entry = gtk_entry_get_text (GTK_ENTRY (lhs_entry));
		txt = (gchar *) cell_name (constraint->lhs_col,
						     constraint->lhs_row);
		if (strcmp (entry, txt) != 0) {
		        if (!parse_cell_name (entry, &col, &row)) {
			        gtk_widget_grab_focus (lhs_entry);
				gtk_entry_set_position(GTK_ENTRY (lhs_entry),
						       0);
				gtk_entry_select_region(GTK_ENTRY (lhs_entry),
							0, 
							GTK_ENTRY(lhs_entry)->
							  text_length);
				goto loop;
			}
			constraint->lhs_col = col;
			constraint->lhs_row = row;
		}
		entry = gtk_entry_get_text (GTK_ENTRY (rhs_entry));
		txt = (gchar *) cell_name (constraint->rhs_col,
					   constraint->rhs_row);
		if (strcmp (entry, txt) != 0) {
		        if (!parse_cell_name (entry, &col, &row)) {
			        gtk_widget_grab_focus (rhs_entry);
				gtk_entry_set_position(GTK_ENTRY (rhs_entry),
						       0);
				gtk_entry_select_region(GTK_ENTRY (rhs_entry),
							0, 
							GTK_ENTRY(rhs_entry)->
							  text_length);
				goto loop;
			}
			constraint->rhs_col = col;
			constraint->rhs_row = row;
		}
		entry = gtk_entry_get_text (GTK_ENTRY (combo_entry));
		g_free (constraint->type);
		constraint->type = g_malloc (strlen (entry) + 1);
		strcpy (constraint->type, entry);
		g_free (constraint->str);

		sprintf(buf, "%s %s ", cell_name (constraint->lhs_col,
						  constraint->lhs_row), entry);
		strcat (buf, cell_name(constraint->rhs_col,
				       constraint->rhs_row));

		constraint->str = g_malloc (strlen (buf)+1);
		strcpy (constraint->str, buf);
	        gtk_clist_remove (data->clist, selected_row);
	        gtk_clist_insert (data->clist, selected_row, constraint_str);
		gtk_clist_set_row_data (data->clist, selected_row, 
					(gpointer) constraint);
	}

	if (v != -1)
		gtk_object_destroy (GTK_OBJECT (dia));

	gtk_object_unref (GTK_OBJECT (gui));
	gtk_widget_show (data->dialog);
}


/* 'Constraint Delete' button clicked */
static void
constr_delete_click (gpointer data)
{
        constraint_dialog_t *constraint_dialog = (constraint_dialog_t *) data;
	gpointer            p;

	if (selected_row >= 0) {
	        p = gtk_clist_get_row_data (constraint_dialog->clist,
					    selected_row);
		constraint_dialog->constraints = 
		        g_slist_remove (constraint_dialog->constraints, p);

	        gtk_clist_remove (constraint_dialog->clist, selected_row);
	}
}

static void
constraint_select_click (GtkWidget      *clist,
			 gint           row,
			 gint           column,
			 GdkEventButton *event,
			 gpointer       data)
{
        selected_row = row;
}

static void
max_toggled(GtkWidget *widget, SolverParameters *data)
{
        if (GTK_TOGGLE_BUTTON (widget)->active) {
	        data->problem_type = SolverMaximize;
	}
}

static void
min_toggled(GtkWidget *widget, SolverParameters *data)
{
        if (GTK_TOGGLE_BUTTON (widget)->active) {

	        data->problem_type = SolverMinimize;
	}
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
dialog_results (Workbook *wb, int res,
		gboolean *answer, gboolean *sensitivity, gboolean *limits)
{
	GladeXML  *gui = glade_xml_new (GNUMERIC_GLADEDIR "/solver.glade",
					NULL);
	GtkWidget *dialog;
	GtkWidget *label;
	GtkWidget *checkbutton1;
	GtkWidget *checkbutton2;
	GtkWidget *checkbutton3;
	GtkWidget *radiobutton3;
	GtkWidget *radiobutton4;
	gchar     *label_txt = "";
        gboolean  answer_s, sensitivity_s, limits_s;
	gboolean  keep_solver_solution;
	int       selection;

	answer_s = sensitivity_s = limits_s = FALSE;

	switch (res){
	case SIMPLEX_DONE:
	        answer_s = sensitivity_s = TRUE;
	        label_txt = "Solver found an optimal solution. All "
		  "constraints and \noptimality conditions are satisfied.\n";
		break;
	case SIMPLEX_UNBOUNDED:
		label_txt = "The Target Cell value does not converge.\n";
	        break;
	default:
	        break;
	}

	if (!gui) {
		printf ("Could not find solver.glade\n");
		return FALSE;
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
	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));

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
	        const char *str = (char *) ov->data;
	        Cell *cell = (Cell *) input_cells->data;

		cell_set_text (cell, str);
		ov = ov->next;
		input_cells = input_cells->next;
	}
}


void
dialog_solver (Workbook *wb, Sheet *sheet)

{
	GladeXML  *gui = glade_xml_new (GNUMERIC_GLADEDIR "/solver.glade",
					NULL);
	GtkWidget *dialog;
	GtkWidget *target_entry, *input_entry;
	GtkWidget *radiobutton;
	GtkWidget *constraint_list;
	GtkWidget *constr_add_button;
	GtkWidget *constr_change_button;
	GtkWidget *constr_delete_button;
	GSList    *cur, *ov;
	gboolean  solver_solution;

	static constraint_dialog_t *constraint_dialog = NULL;
	static gchar *target_entry_str = NULL;
	static gchar *input_entry_str = NULL;

	const char *text;
	int        selection, res;
	float_t    ov_target;
	Cell       *target_cell;
	CellList   *input_cells;
	int        target_cell_col, target_cell_row;
	int        error_flag;
	gboolean   answer, sensitivity, limits;

	if (!gui) {
		printf ("Could not find solver.glade\n");
		return;
	}
	
	if (!constraint_dialog) {
	        constraint_dialog = g_new (constraint_dialog_t, 1);
		constraint_dialog->constraints = NULL;
	}

	if (!target_entry_str)
	        target_entry_str = (gchar *) cell_name (sheet->cursor_col,
							sheet->cursor_row);

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

	sheet->solver_parameters.problem_type = SolverMaximize;

	if (input_entry_str)
	        gtk_entry_set_text (GTK_ENTRY (input_entry),
				    input_entry_str);

	constraint_dialog->dialog = dialog;
	constraint_dialog->clist = GTK_CLIST (constraint_list);
	constraint_dialog->sheet = sheet;
	constraint_dialog->wb = wb;

	gtk_entry_set_text (GTK_ENTRY (target_entry),
			    target_entry_str);
	gtk_entry_select_region (GTK_ENTRY (target_entry),
				 0, GTK_ENTRY(target_entry)->text_length);
	gtk_signal_connect(GTK_OBJECT(constraint_list),
			   "select_row",
			   GTK_SIGNAL_FUNC(constraint_select_click),
			   GTK_OBJECT (constraint_dialog));

	gtk_signal_connect_object (GTK_OBJECT (constr_add_button),
				   "clicked",
				   GTK_SIGNAL_FUNC (constr_add_click),
				   GTK_OBJECT (constraint_dialog));
	gtk_signal_connect_object (GTK_OBJECT (constr_change_button),
				   "clicked",
				   GTK_SIGNAL_FUNC(constr_change_click),
				   GTK_OBJECT(constraint_dialog));
	gtk_signal_connect_object (GTK_OBJECT (constr_delete_button),
				   "clicked",
				   GTK_SIGNAL_FUNC(constr_delete_click),
				   GTK_OBJECT (constraint_dialog));

	radiobutton = glade_xml_get_widget (gui, "radiobutton1");
	gtk_signal_connect (GTK_OBJECT (radiobutton),   "toggled",
			    GTK_SIGNAL_FUNC (max_toggled),
			    &sheet->solver_parameters.problem_type);
	radiobutton = glade_xml_get_widget (gui, "radiobutton2");
	gtk_signal_connect (GTK_OBJECT (radiobutton),   "toggled",
			    GTK_SIGNAL_FUNC (min_toggled),
			    &sheet->solver_parameters.problem_type);

	for (cur=constraint_dialog->constraints; cur != NULL; cur=cur->next) {
	        SolverConstraint *c = (SolverConstraint *) cur->data;
		gchar buf[256];
	        gchar *tmp[] = { buf, NULL };

		sprintf(buf, "%s %s ", cell_name (c->lhs_col,
						  c->lhs_row), c->type);
		strcat (buf, cell_name(c->rhs_col, c->rhs_row));
	        gtk_clist_append (GTK_CLIST (constraint_list), tmp);
	}
	
	gtk_widget_grab_focus (target_entry);

main_dialog:
	
	/* Run the dialog */
	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));

	switch (selection) {
	case 1:
   	        /* Cancel */
		gtk_object_destroy (GTK_OBJECT (dialog));
	        gtk_object_unref (GTK_OBJECT (gui));
	        return;
	case 2:
	        gtk_widget_hide (dialog);
	        dialog_solver_options(wb, sheet);
		goto main_dialog;
	default:
	        break;
	}

	/* Parse target cell entry */
	text = gtk_entry_get_text (GTK_ENTRY (target_entry));
	if (!parse_cell_name (text, &target_cell_col, &target_cell_row)) {
 	        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell name "
				   "for 'Target cell'"));
		gtk_widget_grab_focus (target_entry);
		gtk_entry_set_position(GTK_ENTRY (target_entry), 0);
		gtk_entry_select_region(GTK_ENTRY (target_entry), 0, 
					GTK_ENTRY(target_entry)->text_length);
		goto main_dialog;
	}
	target_cell = sheet_cell_get (sheet, target_cell_col, target_cell_row);
	if (target_cell == NULL) {
	        target_cell = sheet_cell_new (sheet, target_cell_col,
					      target_cell_row);
		cell_set_text (target_cell, "");
	}
	ov_target = value_get_as_float (target_cell->value);

	/* Parse input cells entry */
	text = gtk_entry_get_text (GTK_ENTRY (input_entry));
	input_cells = (CellList *)
	        parse_cell_name_list (sheet, text, &error_flag);
	if (error_flag) {
 	        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
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
	sheet->solver_parameters.constraints = constraint_dialog->constraints;

	if (selection == 0) {
	        float_t *init_table, *final_table;
	        ov = save_original_values (input_cells);
	        if (sheet->solver_parameters.options.assume_linear_model) {
		        res = solver_simplex(wb, sheet, &init_table,
					     &final_table);
			gtk_widget_hide (dialog);
			answer = sensitivity = limits = FALSE;
			solver_solution = dialog_results (wb, res,
							  &answer,
							  &sensitivity,
							  &limits);
			solver_lp_reports (wb, sheet, ov, ov_target,
					   init_table, final_table,
					   answer, sensitivity, limits);
			if (! solver_solution)
			        restore_original_values (input_cells, ov);
		} else {
		        printf("NLP not implemented yet!\n");
		}
	}

	text = gtk_entry_get_text (GTK_ENTRY (target_entry));
	if (!target_entry_str)
	        g_free (target_entry_str);
	target_entry_str = g_new (gchar, strlen (text) + 1);
	strcpy (target_entry_str, text);

	text = gtk_entry_get_text (GTK_ENTRY (input_entry));
	if (!input_entry_str)
	        g_free (input_entry_str);
	input_entry_str = g_new (gchar, strlen (text) + 1);
	strcpy (input_entry_str, text);

	if (selection != -1)
		gtk_object_destroy (GTK_OBJECT (dialog));

	gtk_object_unref (GTK_OBJECT (gui));
}
