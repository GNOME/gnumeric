/*
 * dialog-solver.c: 
 *
 * Author:
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 */

#include <config.h>
#include <gnome.h>
#include <string.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "func.h"
#include "utils.h"

GtkWidget *
hbox_pack_label_and_entry(char *str, char *default_str,
			  int entry_len, GtkWidget *vbox);

/* Different constraint types */
static const char *constraint_strs[] = {
        N_("<="),
/*	N_("="),    */
	N_(">="),
/*	N_("int"),
	N_("bool"), */
	NULL
};

#if 0
static struct {
	const char *name;
	int  disables_second_group;
} paste_types [] = {
	{ N_("All"),      0 },
	{ N_("Formulas"), 0 },
	{ N_("Values"),   0 },
	{ N_("Formats"),  1 },
	{ NULL, 0 }
};
#endif

static const char *equal_ops [] = {
	N_("Max"),
	N_("Min"),
	NULL
};

static const char *estimate_ops [] = {
	N_("Tangent"),
	N_("Quadratic"),
	NULL
};

static const char *derivative_ops [] = {
	N_("Forward"),
	N_("Central"),
	NULL
};

static const char *search_ops [] = {
	N_("Newton"),
	N_("Conjugate"),
	NULL
};

static const char *check_button_left_ops [] = {
	N_("Assume Linear Model"),
	N_("Assume Non-Negative"),
	NULL
};

static const char *check_button_right_ops [] = {
	N_("Use Automatic Scaling"),
	N_("Show Iteration Results"),
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
		
		r = gtk_radio_button_new_with_label (group_ops, _(ops [i]));
		group_ops = GTK_RADIO_BUTTON (r)->group;
		gtk_box_pack_start_defaults (GTK_BOX (fv), r);
	}
	
	gtk_box_pack_start_defaults (GTK_BOX (hbox), f);

	return group_ops;
}

static void
add_check_buttons (GtkWidget *box, const char *ops[])
{
        GtkWidget *button;
	int       i;

	for (i = 0; ops[i]; i++) {
	        button = gtk_check_button_new_with_label (ops[i]);
		gtk_box_pack_start (GTK_BOX (box), button, TRUE, TRUE, 0);
	}
}


GList *
add_strings_to_glist (const char *strs[])
{
        int   i;
	GList *list = NULL;

	for (i=0; strs[i] != NULL; i++) {
	        list = g_list_append (list, (gpointer) strs[i]);
	}
	return list;
}


typedef struct {
        GSList   *constraints;
        GtkCList *clist;
        Sheet    *sheet;
} constraint_dialog_t;


static void
dialog_solver_options (Workbook *wb, Sheet *sheet)

{
	GtkWidget *dialog;
	GtkWidget *radio_buttons, *check_buttons;
	GtkWidget *check_buttons_left, *check_buttons_right;
	GSList *group_estimates, *group_derivatives, *group_search;

	dialog = gnome_dialog_new (_("Gnumeric Solver Options"),
				   GNOME_STOCK_BUTTON_OK,
				   GNOME_STOCK_BUTTON_CANCEL,
				   NULL);
	gnome_dialog_set_parent (GNOME_DIALOG (dialog), 
				 GTK_WINDOW (wb->toplevel));

	check_buttons_left = gtk_vbox_new (TRUE, 0);
	check_buttons_right = gtk_vbox_new (TRUE, 0);
	check_buttons = gtk_hbox_new(TRUE, 0);
	radio_buttons = gtk_hbox_new (TRUE, 0);

	add_check_buttons(check_buttons_left, check_button_left_ops);
	add_check_buttons(check_buttons_right, check_button_right_ops);

	group_estimates = add_radio_buttons(radio_buttons, 
					    "Estimates", estimate_ops);
	group_derivatives = add_radio_buttons(radio_buttons,
					      "Derivatives", derivative_ops);
	group_search = add_radio_buttons(radio_buttons,
					 "Search", search_ops);

	gtk_box_pack_start (GTK_BOX (check_buttons), 
			    check_buttons_left, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (check_buttons), 
			    check_buttons_right, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), 
			    check_buttons, TRUE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), 
			    radio_buttons, TRUE, TRUE, 0);

	gtk_widget_show_all (dialog);

	/* Run the dialog */
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gnome_dialog_run (GNOME_DIALOG (dialog));

	sheet->solver_parameters.options.assume_linear_model = 1;
	sheet->solver_parameters.options.assume_non_negative = 0;
	sheet->solver_parameters.options.automatic_scaling = 0;
	sheet->solver_parameters.options.show_iteration_results = 0;

	gtk_object_unref (GTK_OBJECT (dialog));
}


static void
add_constraint(constraint_dialog_t *constraint_dialog,
	       int lhs_col, int lhs_row, int rhs_col, int rhs_row,
	       char *type_str)
{
	SolverConstraint *constraint;
	char             constraint_buf[512];
	char             *constraint_str[2] = { constraint_buf, NULL };

	sprintf(constraint_buf, "%s %s ", 
		cell_name (lhs_col, lhs_row),
		type_str);
	strcat(constraint_buf, cell_name(rhs_col, rhs_row));

	gtk_clist_append (constraint_dialog->clist, constraint_str);
	constraint = g_new (SolverConstraint, 1);
	constraint->lhs = sheet_cell_fetch (constraint_dialog->sheet,
					    lhs_col, lhs_row);
	constraint->rhs = sheet_cell_fetch (constraint_dialog->sheet,
					    rhs_col, rhs_row);

	constraint->type = g_malloc (strlen (type_str) + 1);
	strcpy (constraint->type, type_str);
	constraint->str = g_malloc (strlen (constraint_buf)+1);
	strcpy (constraint->str, constraint_buf);
	constraint_dialog->constraints = 
	        g_slist_append(constraint_dialog->constraints,
			       (gpointer) constraint);
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

	if (!dialog) {
	        GtkWidget *box;

	        constraint_type_strs = add_strings_to_glist (constraint_strs);

	        dialog = gnome_dialog_new (_("Add Constraint"),
					   GNOME_STOCK_BUTTON_OK,
					   GNOME_STOCK_BUTTON_CANCEL,
					   _("Add"),
					   NULL);

		gnome_dialog_close_hides (GNOME_DIALOG (dialog), TRUE);
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
	selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	
	if (selection == 1) {
add_constraint(constraint_dialog, 0, 8, 2, 8, "<=");
add_constraint(constraint_dialog, 0, 9, 2, 9, ">=");
add_constraint(constraint_dialog, 0, 10, 2, 10, ">=");
add_constraint(constraint_dialog, 0, 11, 2, 11, ">=");
	        gnome_dialog_close (GNOME_DIALOG (dialog));
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

	add_constraint(constraint_dialog, lhs_col, lhs_row, rhs_col, rhs_row,
		       type_str);

	gtk_entry_set_text (GTK_ENTRY (lhs_entry), "");
	gtk_entry_set_text (GTK_ENTRY (rhs_entry), "");

	if (selection == 2)
	        goto add_dialog;

	gnome_dialog_close (GNOME_DIALOG (dialog));
}


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
        printf("Delete: Not implemented yet.\n");
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

	constraint_dialog_t constraint_dialog;

	const char *text, *target_entry_str;
	int      selection, sel_equal_to;
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
						"Equal to:", equal_ops);

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
		gtk_clist_set_column_title (GTK_CLIST (constraint_list),
					    0, "Subject to the Constraints:");
		gtk_clist_column_titles_passive (GTK_CLIST (constraint_list));
		gtk_clist_column_titles_show (GTK_CLIST (constraint_list));
		gtk_clist_clear (GTK_CLIST (constraint_list));

		/* Constraint buttons */
		constr_add_button = gtk_button_new_with_label ("Add");
		constr_change_button = gtk_button_new_with_label ("Change");
		constr_delete_button = gtk_button_new_with_label ("Delete");
		constr_button_box = gtk_vbox_new (FALSE, 0);
		constraint_dialog.constraints = NULL;
		constraint_dialog.clist = GTK_CLIST (constraint_list);
		constraint_dialog.sheet = sheet;

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
	selection = gnome_dialog_run (GNOME_DIALOG (dialog));

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
	        if (1 || sheet->solver_parameters.options.assume_linear_model)
		        solver_simplex(wb, sheet);
		else
		        ; /* NLP not implemented yet */
		break;
	case 2:  /* Options */
	default:
	        break;
	}

	gnome_dialog_close (GNOME_DIALOG (dialog));
}
