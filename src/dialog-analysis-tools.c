/*
 * dialog-analysis-tools.c: 
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
#include "utils.h"


void correlation_dialog (Workbook *wb, Sheet *sheet);
void covariance_dialog (Workbook *wb, Sheet *sheet);


typedef void (*tool_fun_ptr_t)(Workbook *wb, Sheet *sheet);

typedef struct {
        char *col1;
        char *col2;
} tool_name_t;

typedef struct {
        tool_name_t     name;
        tool_fun_ptr_t  fun;
} tool_list_t;


tool_list_t tools[] = {
        { { "Correlation", NULL }, correlation_dialog },
        { { "Covariance", NULL }, covariance_dialog },
	{ { NULL, NULL }, NULL }
};

static int selected_row;

static int
parse_range (char *text, int *start_col, int *start_row,
	     int *end_col, int *end_row)
{
        char buf[256];
        char *p;

	strcpy(buf, text);
	p = strchr(buf, ':');
	if (p == NULL)
	        return 0;
	*p = '\0';
	if (!parse_cell_name (buf, start_col, start_row))
	        return 0;
	if (!parse_cell_name (p+1, end_col, end_row))
	        return 0;
	return 1;
}

static char *groupped_ops [] = {
        N_("Columns"),
        N_("Rows"),
        NULL
};


void correlation_dialog (Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog;
	static GtkWidget *box, *hbox_x, *group_box;
	static GtkWidget *input_range, *groupped_label;
	static GtkWidget *input_range_label;
	static GSList    *group_ops;
	static GtkWidget *r;

	char  *text;
	int   selection;
	static Range range_input;
	int   i;

	if (!dialog) {
		dialog = gnome_dialog_new (_("Correlation"),
					   _("OK"),
					   GNOME_STOCK_BUTTON_CANCEL,
					   NULL);

                gnome_dialog_close_hides (GNOME_DIALOG (dialog), TRUE);
                gnome_dialog_set_parent (GNOME_DIALOG (dialog),
                                         GTK_WINDOW (wb->toplevel));
		box = gtk_vbox_new (FALSE, 0);
		hbox_x = gtk_hbox_new (FALSE, 0);
		group_box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (box), hbox_x);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		input_range = gtk_entry_new_with_max_length (20);
		input_range_label = gtk_label_new ("Input Range:");

		gtk_box_pack_start_defaults (GTK_BOX (hbox_x), 
					     input_range_label);
		gtk_box_pack_start_defaults (GTK_BOX (hbox_x),
					     input_range);
		groupped_label = gtk_label_new ("Groupped By:");
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), 
					     groupped_label);
		group_ops = NULL;
		for (i = 0; groupped_ops [i]; i++) {
			r = gtk_radio_button_new_with_label (group_ops,
							     _(groupped_ops[i])
							     );
			group_ops = GTK_RADIO_BUTTON (r)->group;
			gtk_box_pack_start_defaults (GTK_BOX (group_box), r);
		}
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), 
					     group_box);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (input_range);

correlation_dialog_loop:

	selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	if (selection == 1) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	i = gtk_radio_group_get_selected (group_ops);

	text = gtk_entry_get_text (GTK_ENTRY (input_range));
	if (!parse_range (text, &range_input.start_col,
			  &range_input.start_row,
			  &range_input.end_col,
			  &range_input.end_row)) {
	        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell range "
				   "in 'Input Range:'"));
		gtk_widget_grab_focus (input_range);
		gtk_entry_set_position(GTK_ENTRY (input_range), 0);
		gtk_entry_select_region(GTK_ENTRY (input_range), 0, 
				GTK_ENTRY(input_range)->text_length);
		goto correlation_dialog_loop;
	}

	correlation_tool (wb, sheet, &range_input, !i);

 	gnome_dialog_close (GNOME_DIALOG (dialog));
}


void covariance_dialog (Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog;
	static GtkWidget *box, *hbox_x, *group_box;
	static GtkWidget *input_range, *groupped_label;
	static GtkWidget *input_range_label;
	static GSList    *group_ops;
	static GtkWidget *r;

	char  *text;
	int   selection;
	static Range range_input;
	int   i;

	if (!dialog) {
		dialog = gnome_dialog_new (_("Covariance"),
					   _("OK"),
					   GNOME_STOCK_BUTTON_CANCEL,
					   NULL);

                gnome_dialog_close_hides (GNOME_DIALOG (dialog), TRUE);
                gnome_dialog_set_parent (GNOME_DIALOG (dialog),
                                         GTK_WINDOW (wb->toplevel));
		box = gtk_vbox_new (FALSE, 0);
		hbox_x = gtk_hbox_new (FALSE, 0);
		group_box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (box), hbox_x);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		input_range = gtk_entry_new_with_max_length (20);
		input_range_label = gtk_label_new ("Input Range:");

		gtk_box_pack_start_defaults (GTK_BOX (hbox_x), 
					     input_range_label);
		gtk_box_pack_start_defaults (GTK_BOX (hbox_x),
					     input_range);
		groupped_label = gtk_label_new ("Groupped By:");
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), 
					     groupped_label);
		group_ops = NULL;
		for (i = 0; groupped_ops [i]; i++) {
			r = gtk_radio_button_new_with_label (group_ops,
							     _(groupped_ops[i])
							     );
			group_ops = GTK_RADIO_BUTTON (r)->group;
			gtk_box_pack_start_defaults (GTK_BOX (group_box), r);
		}
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), 
					     group_box);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (input_range);

covariance_dialog_loop:

	selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	if (selection == 1) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	i = gtk_radio_group_get_selected (group_ops);

	text = gtk_entry_get_text (GTK_ENTRY (input_range));
	if (!parse_range (text, &range_input.start_col,
			  &range_input.start_row,
			  &range_input.end_col,
			  &range_input.end_row)) {
	        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell range "
				   "in 'Input Range:'"));
		gtk_widget_grab_focus (input_range);
		gtk_entry_set_position(GTK_ENTRY (input_range), 0);
		gtk_entry_select_region(GTK_ENTRY (input_range), 0, 
				GTK_ENTRY(input_range)->text_length);
		goto covariance_dialog_loop;
	}

	covariance_tool (wb, sheet, &range_input, !i);

 	gnome_dialog_close (GNOME_DIALOG (dialog));
}

void
selection_made(GtkWidget *clist, gint row, gint column,
	       GdkEventButton *event, gpointer data)
{
        selected_row = row;
}


void
dialog_data_analysis (Workbook *wb, Sheet *sheet)

{
	static GtkWidget *dialog;
	static GtkWidget *main_label;
	static GtkWidget *tool_list;

	int i, selection;

	if (!dialog) {
		GtkWidget *box;

		dialog = gnome_dialog_new (_("Data Analysis"),
					   _("OK"),
					   GNOME_STOCK_BUTTON_CANCEL,
					   NULL);
		gnome_dialog_close_hides (GNOME_DIALOG (dialog), TRUE);

		box = gtk_vbox_new (FALSE, 0);
		main_label = gtk_label_new("Analysis Tools");
                gtk_misc_set_alignment (GTK_MISC(main_label), 0,0);
 		gtk_box_pack_start_defaults (GTK_BOX (box), main_label);

		tool_list = gtk_clist_new (1);
		gtk_clist_set_selection_mode (GTK_CLIST (tool_list),
					      GTK_SELECTION_SINGLE);

		for (i=0; tools[i].fun; i++)
		        gtk_clist_append (GTK_CLIST (tool_list),
					  (char **) &tools[i].name);
		
		gtk_box_pack_start_defaults (GTK_BOX (box), tool_list);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);
		gtk_signal_connect (GTK_OBJECT(tool_list), "select_row",
				    GTK_SIGNAL_FUNC(selection_made), NULL);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show (dialog);

main_dialog:
	
	/* Run the dialog */
	selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	gnome_dialog_close (GNOME_DIALOG (dialog));

	if (selection == 0) {
	        g_return_if_fail (tools[selected_row].fun != NULL);
	        tools[selected_row].fun (wb, sheet);
	}
}
