/*
 * dialog-goal-seek.c: 
 *
 * Author:
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 *
 */


#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "dialogs.h"
#include "cell.h"
#include "expr.h"


#define MAX_CELL_NAME_LEN  20


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
        /* Not implemented yet */

        *f_flag = 0;
        return 0;
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
		gnome_dialog_set_parent (GNOME_DIALOG (dialog), GTK_WINDOW (wb->toplevel));
		
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
					 _("You should introduce a valid cell name in 'By changing cell'"));
			gtk_widget_grab_focus (change_entry);
			gtk_entry_set_position(GTK_ENTRY (change_entry), 0);
			gtk_entry_select_region(GTK_ENTRY (change_entry), 0, 
						GTK_ENTRY(change_entry)->text_length);
			goto dialog_loop;
		}
		value = gnumeric_goal_seek(wb, sheet,
					   set_cell_col, set_cell_row,
					   target_value,
					   change_cell_col, change_cell_row,
					   &f_flag);
		if (f_flag) {
		        /* Goal seeking found a solution */
		  
		} else {
	                gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
					 _("Goal seek did not find a solution!"));
		}
	}

	gnome_dialog_close (GNOME_DIALOG (dialog));
}
