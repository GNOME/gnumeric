/*
 * dialog-solver.c: 
 *
 * Author:
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
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
#include "utils.h"
#include "utils-dialog.h"

/* Different constraint types */
static const char *constraint_strs[] = {
        N_("<="),
/*	N_(">="),
	N_("="),
	N_("int"),
	N_("bool"), */
	NULL
};


static const char *equal_ops [] = {
	N_("Max"),
	N_("Min"),
	NULL
};


static GSList *
add_radio_buttons (GtkWidget *hbox, const char *title, const char *ops[])
{
        GtkWidget *f, *fv;
	GSList    *group_ops = NULL;
	int       i;

	f  = gtk_frame_new (_(title));
	fv = gtk_vbox_new (TRUE, 0);
	gtk_container_add (GTK_CONTAINER (f), fv);

	for (i = 0; ops [i]; i++) {
		GtkWidget *r;
		
		r = gtk_radio_button_new_with_label (group_ops, ops [i]);
		group_ops = GTK_RADIO_BUTTON (r)->group;
		gtk_box_pack_start_defaults (GTK_BOX (fv), r);
	}
	
	gtk_box_pack_start_defaults (GTK_BOX (hbox), f);

	return group_ops;
}

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

	gnome_dialog_set_parent (GNOME_DIALOG (dia),
				 GTK_WINDOW (wb->toplevel));

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
	gnumeric_dialog_run (wb, GNOME_DIALOG (dia));

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

	sprintf(constraint_buf, "%s %s ", 
		cell_name (lhs_col, lhs_row),
		type_str);
	strcat(constraint_buf, cell_name(rhs_col, rhs_row));

	constraint = g_new (SolverConstraint, 1);
	constraint->lhs = sheet_cell_get (constraint_dialog->sheet,
					  lhs_col, lhs_row);
	if (constraint->lhs == NULL) {
	cell_error:
 	        gnumeric_notice (constraint_dialog->wb,
				 GNOME_MESSAGE_BOX_ERROR,
				 _("You gave a cell reference that contain "
				   "no data."));
	        return 1;
	}
	constraint->rhs = sheet_cell_get (constraint_dialog->sheet,
					  rhs_col, rhs_row);
	if (constraint->rhs == NULL)
	        goto cell_error;

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
constr_add_click (gpointer data)
{
	static GtkWidget    *dialog;
	static GtkWidget    *lhs_entry;
	static GtkWidget    *rhs_entry;
	static GtkWidget    *type_entry;
	static GList        *constraint_type_strs;

        constraint_dialog_t *constraint_dialog = (constraint_dialog_t *) data;
	int                 selection;
	char                *lhs_text, *rhs_text;
	int                 rhs_col, rhs_row;
	int                 lhs_col, lhs_row;
	char                *type_str;

	gtk_widget_hide (constraint_dialog->dialog);
	if (!dialog) {
	        GtkWidget *box;

	        constraint_type_strs = add_strings_to_glist (constraint_strs);

	        dialog = gnome_dialog_new (_("Add Constraint"),
					   GNOME_STOCK_BUTTON_OK,
					   GNOME_STOCK_BUTTON_CANCEL,
					   _("Add"),
					   NULL);

		gnome_dialog_close_hides (GNOME_DIALOG (dialog), TRUE);
		gnome_dialog_set_parent (GNOME_DIALOG (dialog),
					 GTK_WINDOW 
					 (constraint_dialog->wb->toplevel));
		box = gtk_hbox_new (FALSE, 0);

		lhs_entry = hbox_pack_label_and_entry
		  ("Left Hand Side:", "", 20, box);

		type_entry = gtk_combo_new ();
		gtk_combo_set_popdown_strings (GTK_COMBO (type_entry),
					       constraint_type_strs);
		gtk_box_pack_start_defaults (GTK_BOX (box), type_entry);

		rhs_entry = hbox_pack_label_and_entry
		  ("Right Hand Side:", "", 20, box);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show (dialog);

add_dialog:

	gtk_widget_grab_focus (lhs_entry);

	/* Run the dialog */
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

	gnome_dialog_close (GNOME_DIALOG (dialog));
	gtk_widget_show (constraint_dialog->dialog);
}

static gint selected_row = -1;

/* 'Constraint Change' button clicked */
static void
constr_change_click (gpointer data)
{
        printf("Change: Not implemented yet.\n");
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

void
dialog_solver (Workbook *wb, Sheet *sheet)

{
	static GtkWidget *dialog;
	static GtkWidget *target_entry, *input_entry;
	static GtkWidget *radio_buttons;
	static GtkWidget *constraint_list;
	static GtkWidget *constraint_box, *scrolled_win;
	static GtkWidget *constr_add_button;
	static GtkWidget *constr_change_button;
	static GtkWidget *constr_delete_button;
	static GtkWidget *constr_button_box;
	static GSList    *group_equal;

	static constraint_dialog_t constraint_dialog;

	const char *text, *target_entry_str;
	int      selection, sel_equal_to, res;
	Cell     *target_cell;
	CellList *input_cells;
	int      target_cell_col, target_cell_row;
	int      error_flag;

	target_entry_str = cell_name (sheet->cursor_col, sheet->cursor_row);

	if (!dialog) {
		GtkWidget *box;

		dialog = gnome_dialog_new (_("Gnumeric Solver Parameters"),
					   _("Solve"),
					   GNOME_STOCK_BUTTON_CANCEL,
					   _("Options..."),
					   NULL);
		gnome_dialog_close_hides (GNOME_DIALOG (dialog), TRUE);
		gnome_dialog_set_parent (GNOME_DIALOG (dialog),
					 GTK_WINDOW (wb->toplevel));

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults
		  (GTK_BOX(GNOME_DIALOG(dialog)->vbox), box);

		/* 'Set Target Cell' entry */
		target_entry = hbox_pack_label_and_entry
		  ("Set Target Cell:", "", 20, box);
		gtk_entry_set_text (GTK_ENTRY (target_entry),
				    target_entry_str);
		gtk_entry_select_region (GTK_ENTRY (target_entry),
                                  0, GTK_ENTRY(target_entry)->text_length);
       
		/* Radio buttons for problem type selection */
		radio_buttons = gtk_hbox_new (TRUE, 0);
		group_equal = add_radio_buttons(radio_buttons,
						_("Equal to:"), equal_ops);

		gtk_box_pack_start (GTK_BOX (box), 
				    radio_buttons, TRUE, TRUE, 0);

		/* 'By Changeing Cells' entry */
		input_entry = hbox_pack_label_and_entry
		  ("By Changing Cells:", "", 20, box);

		/* Constraints list */
		scrolled_win = gtk_scrolled_window_new (NULL, NULL);
		gtk_container_set_border_width (GTK_CONTAINER (scrolled_win),
						5);
		gtk_widget_set_usize (scrolled_win, 230, 160);
		gtk_scrolled_window_set_policy
		  (GTK_SCROLLED_WINDOW (scrolled_win),
		   GTK_POLICY_AUTOMATIC,
		   GTK_POLICY_AUTOMATIC);

		constraint_box = gtk_hbox_new (FALSE, 0);
		constraint_list = gtk_clist_new (1);
		gtk_scrolled_window_add_with_viewport
		  (GTK_SCROLLED_WINDOW (scrolled_win), constraint_list);
		gtk_clist_set_selection_mode (GTK_CLIST (constraint_list),
					      GTK_SELECTION_SINGLE);
		gtk_clist_set_column_title (GTK_CLIST (constraint_list), 0,
					    _("Subject to the Constraints:"));
		gtk_clist_column_titles_passive (GTK_CLIST (constraint_list));
		gtk_clist_column_titles_show (GTK_CLIST (constraint_list));
		gtk_clist_clear (GTK_CLIST (constraint_list));
		gtk_signal_connect(GTK_OBJECT(constraint_list),
				   "select_row",
				   GTK_SIGNAL_FUNC(constraint_select_click),
				   GTK_OBJECT (&constraint_dialog));

		/* Constraint buttons */
		constr_add_button = gtk_button_new_with_label (_("Add"));
		constr_change_button = gtk_button_new_with_label (_("Change"));
		constr_delete_button = gtk_button_new_with_label (_("Delete"));
		constr_button_box = gtk_vbox_new (FALSE, 0);
		constraint_dialog.dialog = dialog;
		constraint_dialog.constraints = NULL;
		constraint_dialog.clist = GTK_CLIST (constraint_list);
		constraint_dialog.sheet = sheet;
		constraint_dialog.wb = wb;

		gtk_signal_connect_object (GTK_OBJECT (constr_add_button),
					   "clicked",
					   GTK_SIGNAL_FUNC (constr_add_click),
					   GTK_OBJECT (&constraint_dialog));
		gtk_signal_connect_object (GTK_OBJECT (constr_change_button),
					   "clicked",
					   GTK_SIGNAL_FUNC(constr_change_click),
					   GTK_OBJECT(&constraint_dialog));
		gtk_signal_connect_object (GTK_OBJECT (constr_delete_button),
					   "clicked",
					   GTK_SIGNAL_FUNC(constr_delete_click),
					   GTK_OBJECT (&constraint_dialog));
		gtk_box_pack_start_defaults (GTK_BOX (constr_button_box),
					     constr_add_button);
		gtk_box_pack_start_defaults (GTK_BOX (constr_button_box),
					     constr_change_button);
		gtk_box_pack_start_defaults (GTK_BOX (constr_button_box),
					     constr_delete_button);
		
		/* Pack the constraint setting into a box */
		gtk_box_pack_start_defaults (GTK_BOX (constraint_box),
					     scrolled_win);
		gtk_box_pack_start_defaults (GTK_BOX (constraint_box),
					     constr_button_box);

		gtk_box_pack_start_defaults (GTK_BOX (box), constraint_box);

		gtk_widget_show_all (dialog);
	} else {
	        gtk_entry_set_text (GTK_ENTRY (target_entry),target_entry_str);
		gtk_entry_select_region (GTK_ENTRY (target_entry),
					 0, 
					 GTK_ENTRY(target_entry)->text_length);
		gtk_widget_show (dialog);
	}

	gtk_widget_grab_focus (target_entry);

main_dialog:
	
	/* Run the dialog */
	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));

	switch (selection) {
	case 1:
   	        /* Cancel */
	        gnome_dialog_close (GNOME_DIALOG (dialog));
	        return;
	case 2:
	        gnome_dialog_close (GNOME_DIALOG (dialog));
	        dialog_solver_options(wb, sheet);
		goto main_dialog;
	default:
	        break;
	}

	sel_equal_to = gtk_radio_group_get_selected (group_equal);

	if (sel_equal_to)
	        sheet->solver_parameters.problem_type = SolverMinimize;
	else
	        sheet->solver_parameters.problem_type = SolverMaximize;

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
	sheet->solver_parameters.constraints = constraint_dialog.constraints;

	switch (selection) {
	case 0:  /* Solve */
	        if (1 ||sheet->solver_parameters.options.assume_linear_model) {
		        res = solver_simplex(wb, sheet);
			if (res == SIMPLEX_UNBOUNDED) {
			        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
						 _("The problem is unbounded "
						   "and cannot be solved"));
				break;
			}
		} else
		        ; /* NLP not implemented yet */
		break;
	case 2:  /* Options */
	default:
	        break;
	}

	gnome_dialog_close (GNOME_DIALOG (dialog));
}
