/*
 * dialog-goal-seek.c: 
 *
 * Author:
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 */


#include <config.h>
#include <gnome.h>
#include <math.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "dialogs.h"
#include "cell.h"
#include "expr.h"
#include "eval.h"


#define MAX_CELL_NAME_LEN  20


static int test_cell_with_value (Cell *set_cell, Cell *change_cell,
				 float_t x, float_t *value)
{
        char buf[256];
	sprintf(buf, "%f", (float) x);

        cell_set_text(change_cell, buf);
	cell_eval(set_cell);

	if (set_cell->value)
	        *value = value_get_as_double (set_cell->value);
	else
	        return -1;
	return 0;
}


/* Goal seek will find a value for a given cell.  User specifies a cell (C1), 
 * its target value, and another cell (C2) whose value is to be determined.
 * If the seek is successful, the value in C1 matches the target value and
 * 'f_flag' is set to 1 (otherwise 0).  The function returns the value of C2.
 */
static float_t
gnumeric_goal_seek (Workbook *wb, Sheet *sheet,
		    int      set_cell_col, int set_cell_row,       /* C1 */
		    float_t  target_value,
		    int      change_cell_col, int change_cell_row, /* C2 */
		    int      *f_flag)
{
        const int max_iterations = 10000;
	const int left = 1;
	const int right = 2;
	const float_t accuracy_limit = 0.0003;

        Cell    *target_cell;
	Cell    *change_cell;
        float_t a, b;
	float_t x1, x2, x;
	float_t step = 1;
	float_t initial_value;
	float_t test;
	int     i, dir;

	/* Get the cell pointers */
	target_cell = sheet_cell_get (sheet, set_cell_col, set_cell_row);
	if (target_cell == NULL) {
	        target_cell = sheet_cell_new (sheet,
					      set_cell_col, set_cell_row);
		cell_set_text (target_cell, "");
	}
	change_cell = sheet_cell_get (sheet, change_cell_col, change_cell_row);
	if (change_cell == NULL) {
	        change_cell = sheet_cell_new(sheet,
					     change_cell_col, change_cell_row);
		cell_set_text (change_cell, "");
	}

	initial_value = value_get_as_double (change_cell->value);

	/* Check if a linear problem */
	if (test_cell_with_value (target_cell, change_cell,
				  initial_value, &x1))
	        goto non_linear;

	if (test_cell_with_value (target_cell, change_cell, 
				  initial_value + step, &x2))
	        goto non_linear;

	b = (x2 - x1) / step;
	if (test_cell_with_value (target_cell, change_cell, 0, &a))
	        goto non_linear;

	if (b != 0) {
	        x = (target_value - a) / b;
		if (test_cell_with_value (target_cell, change_cell, x, &x))
		        goto non_linear;
		if (x == target_value) {
		        /* Goal found for a linear problem */
		        *f_flag = 1;
			return x;
		}
	}

non_linear:

        /* This is a very unscientific method for non-linear problem solving */
	step = log(rand());
	dir = 0;
	x = initial_value;
	for (i=0; i<max_iterations; i++) {
	        if (rand() % 1000 == 0) {
			step = log(rand());
			if (rand() % 2)
			        x -= step;
			else
			        x += step;
		}
	        if (test_cell_with_value (target_cell, change_cell,
					  x, &test)) {
		        if (rand() % 2) {
			        step = log(rand());
			        if (rand() % 2)
				        x -= step;
				else
				        x += step;
			}
			continue;
		}
	        if (fabs(target_value - test) < accuracy_limit) {
		        *f_flag = 1;
			return x;
		}
		if (target_value < test) {
		        if (dir == right)
			       step *= 0.7;
			x -= step;
			dir = left;
		} else {
		        if (dir == left)
			       step *= 0.7;
			x += step;
			dir = right;
		}
	}
        *f_flag = 0;
        return 0;
}


static int
dialog_found_solution (int set_cell_col, int set_cell_row,
		       float_t target_value, float_t value,
		       int change_cell_col, int change_cell_row)
{
        GtkWidget *dialog;
	GtkWidget *label_box;
	GtkWidget *status_label;
	GtkWidget *found_label;
	GtkWidget *empty_label;
	GtkWidget *target_label;
	GtkWidget *current_label;

	char *name;
	char status_str[256];
	char target_str[256];
	char current_str[256];
	int  selection;

	name = cell_name (set_cell_col, set_cell_row);
	sprintf(status_str,  "Goal seeking with cell %s", name);	
	sprintf(target_str,  "Target value:   %12.2f", target_value);
	sprintf(current_str, "Current value:  %12.2f", value);

	dialog = gnome_dialog_new (_("Goal Seek Report"),
				   GNOME_STOCK_BUTTON_OK,
				   GNOME_STOCK_BUTTON_CANCEL,
				   NULL);

	status_label = gtk_label_new(status_str);
	found_label = gtk_label_new("found a solution");
	empty_label = gtk_label_new("");
	target_label = gtk_label_new(target_str);
	current_label = gtk_label_new(current_str);

	gtk_misc_set_alignment (GTK_MISC(status_label), 0,0);
	gtk_misc_set_alignment (GTK_MISC(found_label), 0,0);
	gtk_misc_set_alignment (GTK_MISC(target_label), 0,0);
	gtk_misc_set_alignment (GTK_MISC(current_label), 0,0);

	label_box = gtk_vbox_new (FALSE, 0);
	gtk_box_pack_start_defaults (GTK_BOX (label_box), status_label);
	gtk_box_pack_start_defaults (GTK_BOX (label_box), found_label);
	gtk_box_pack_start_defaults (GTK_BOX (label_box), empty_label);
	gtk_box_pack_start_defaults (GTK_BOX (label_box), target_label);
	gtk_box_pack_start_defaults (GTK_BOX (label_box), current_label);

	gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
					      (dialog)->vbox), label_box);

	gtk_widget_show_all (dialog);
        selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	gnome_dialog_close (GNOME_DIALOG (dialog));

	return selection;
}


void
dialog_goal_seek (Workbook *wb, Sheet *sheet)
{
	static GtkWidget *dialog;
        static GtkWidget *set_entry;
	static GtkWidget *set_label;
	static GtkWidget *target_entry;
	static GtkWidget *target_label;
	static GtkWidget *change_entry;
	static GtkWidget *change_label;
	static GtkWidget *label_box;
	static GtkWidget *entry_box;

	char             *set_entry_str;
	char             *text;
	int              selection;
	float_t          old_value;
	Cell             *change_cell;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	set_entry_str = cell_name (sheet->cursor_col, sheet->cursor_row);
	
	if (!dialog) {
                GtkWidget *box;

                dialog = gnome_dialog_new (_("Goal Seek..."),
                                           GNOME_STOCK_BUTTON_OK,
                                           GNOME_STOCK_BUTTON_CANCEL,
                                           NULL);
                gnome_dialog_close_hides (GNOME_DIALOG (dialog), TRUE);
		gnome_dialog_set_parent (GNOME_DIALOG (dialog),
					 GTK_WINDOW (wb->toplevel));
		
                set_entry = gtk_entry_new_with_max_length (MAX_CELL_NAME_LEN);

                target_entry = gtk_entry_new_with_max_length(MAX_CELL_NAME_LEN);

                change_entry = gtk_entry_new_with_max_length(MAX_CELL_NAME_LEN);

		set_label    = gtk_label_new("Set Cell:");
		target_label = gtk_label_new("To value:");
		change_label = gtk_label_new("By changing cell:");

		gtk_misc_set_alignment (GTK_MISC(set_label), 0,0);
		gtk_misc_set_alignment (GTK_MISC(target_label), 0,0);
		gtk_misc_set_alignment (GTK_MISC(change_label), 0,0);

                box = gtk_hbox_new (FALSE, 0);
                entry_box = gtk_vbox_new (FALSE, 0);
                label_box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (label_box), set_label);
                gtk_box_pack_start_defaults (GTK_BOX (entry_box), set_entry);
		gtk_box_pack_start_defaults (GTK_BOX (label_box), 
					     target_label);
                gtk_box_pack_start_defaults (GTK_BOX (entry_box),
					     target_entry);
		gtk_box_pack_start_defaults (GTK_BOX (label_box),
					     change_label);
                gtk_box_pack_start_defaults (GTK_BOX (entry_box),
					     change_entry);

		gtk_box_pack_start_defaults (GTK_BOX (box), label_box);
		gtk_box_pack_start_defaults (GTK_BOX (box), entry_box);

                gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		gtk_entry_set_text(GTK_ENTRY (set_entry), set_entry_str);
		gtk_entry_set_position(GTK_ENTRY (set_entry), 0);
		gtk_entry_select_region(GTK_ENTRY (set_entry), 0, 
					GTK_ENTRY(set_entry)->text_length);

                gtk_widget_show_all (box);
	} else {
		gtk_entry_set_text(GTK_ENTRY (set_entry), set_entry_str);
		gtk_entry_set_position(GTK_ENTRY (set_entry), 0);
		gtk_entry_select_region(GTK_ENTRY (set_entry), 0, 
					GTK_ENTRY(set_entry)->text_length);

	        gtk_widget_show (dialog);
	}

	gtk_widget_grab_focus (set_entry);

dialog_loop:
        selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	if (selection == 0) {
 	        int     set_cell_col, set_cell_row;
		int     change_cell_col, change_cell_row;
		float_t target_value;
		int     f_flag;
		float_t value;

		/* Check that a cell entered in 'set cell' entry */
		text = gtk_entry_get_text (GTK_ENTRY (set_entry));
		if (!parse_cell_name (text, &set_cell_col, &set_cell_row)){
	                gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
					 _("You should introduce a valid cell name in 'Set cell'"));
			gtk_widget_grab_focus (set_entry);
			gtk_entry_set_position(GTK_ENTRY (set_entry), 0);
			gtk_entry_select_region(GTK_ENTRY (set_entry), 0, 
						GTK_ENTRY(set_entry)->text_length);
			goto dialog_loop;
		}

		text = gtk_entry_get_text (GTK_ENTRY (target_entry));
		/* Add float input parsing here */
		target_value = atof(text);

		/* Check that a cell entered in 'by changing cell' entry */
		text = gtk_entry_get_text (GTK_ENTRY (change_entry));
		if (!parse_cell_name (text, &change_cell_col, &change_cell_row)){
	                gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
					 _("You should introduce a valid cell "
					   "name in 'By changing cell'"));
			gtk_widget_grab_focus (change_entry);
			gtk_entry_set_position(GTK_ENTRY (change_entry), 0);
			gtk_entry_select_region(GTK_ENTRY (change_entry), 0, 
						GTK_ENTRY(change_entry)->text_length);
			goto dialog_loop;
		}
		change_cell = sheet_cell_get (sheet,
					      change_cell_col,
					      change_cell_row);
		if (change_cell == NULL) {
		        change_cell = sheet_cell_new (sheet,
						      change_cell_col,
						      change_cell_row);
			cell_set_text (change_cell, "");
		}
		old_value = value_get_as_double(change_cell->value);
		value = gnumeric_goal_seek(wb, sheet,
					   set_cell_col, set_cell_row,
					   target_value,
					   change_cell_col, change_cell_row,
					   &f_flag);
		if (f_flag) {
		        gnome_dialog_close (GNOME_DIALOG (dialog));
			if (dialog_found_solution (set_cell_col, set_cell_row,
						   target_value, value,
						   change_cell_col, 
						   change_cell_row)) {	       
			        /* Goal seek cancelled */
			        char buf[256];
				Cell *set_cell;

				change_cell = sheet_cell_get (sheet,
							      change_cell_col,
							      change_cell_row);
				sprintf(buf, "%f", old_value);
				cell_set_text (change_cell, buf);
				set_cell = sheet_cell_get (sheet,
							   set_cell_col,
							   set_cell_row);
				cell_eval (set_cell);
			}
			return;
		  
		} else {
	                gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
					 _("Goal seek did not find a solution!"));
		}
	}

	gnome_dialog_close (GNOME_DIALOG (dialog));
}
