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


static descriptive_stat_tool_t ds;

void dummy_fun (Workbook *wb, Sheet *sheet)
{}

typedef void (*tool_fun_ptr_t)(Workbook *wb, Sheet *sheet);

typedef struct {
        char *col1;
        char *col2;
} tool_name_t;

typedef struct {
        tool_name_t     name;
        tool_fun_ptr_t  fun;
} tool_list_t;

typedef struct {
        const char    *name;
       	GtkSignalFunc fun;
        gboolean      entry_flag;
        const char    *default_entry;
} check_button_t;


tool_list_t tools[] = {
        { { "Correlation", NULL }, dummy_fun },
        { { "Covariance", NULL }, dummy_fun },
        { { "Descriptive Statistics", NULL }, dummy_fun },
	{ { NULL, NULL }, NULL }
};

void summary_stat_signal_fun()
{
        ds.summary_statistics = !ds.summary_statistics;
}

void confidence_signal_fun()
{
        ds.confidence_level = !ds.confidence_level;
}

void kth_largest_signal_fun()
{
        ds.kth_largest = !ds.kth_largest;
}

void kth_smallest_signal_fun()
{
        ds.kth_smallest = !ds.kth_smallest;
}

static check_button_t desc_stat_buttons[] = {
        { N_("Summary Statistics"), summary_stat_signal_fun, FALSE,
	  N_("") },
        { N_("Confidence Level for Mean"), confidence_signal_fun, TRUE,
	  N_("0.95") },
	{ N_("Kth Largest:"), kth_largest_signal_fun, TRUE,
	  N_("1") },
	{ N_("Kth Smallest:"), kth_smallest_signal_fun, TRUE,
	  N_("1") },
        { NULL, NULL }
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

static void
add_check_buttons (GtkWidget *box, check_button_t *cbs)
{
	static gboolean do_transpose = FALSE;
        GtkWidget *button;
	int       i;

	for (i = 0; cbs[i].name; i++) {
	        GtkWidget *hbox, *entry;

		hbox = gtk_hbox_new (FALSE, 0);
	        button = gtk_check_button_new_with_label (cbs[i].name);
		gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
		if (cbs[i].entry_flag) {
		        entry = gtk_entry_new_with_max_length (20);
			gtk_entry_set_text (GTK_ENTRY (entry),
					    cbs[i].default_entry);
			gtk_box_pack_start (GTK_BOX (hbox), entry, 
					    TRUE, TRUE, 0);
			ds.entry[i] = entry;
		}

		gtk_box_pack_start (GTK_BOX (box), hbox, TRUE, TRUE, 0);
		gtk_signal_connect (GTK_OBJECT (button), "toggled",
				    GTK_SIGNAL_FUNC (cbs[i].fun),
				    &do_transpose);
	}
}

void tool_dialog_range(Workbook *wb, Sheet *sheet, int ti)
{
        static GtkWidget *dialog[8];
	static GtkWidget *box, *hbox_x, *group_box;
	static GtkWidget *input_range, *groupped_label;
	static GtkWidget *input_range_label;
	static GtkWidget *check_buttons;
	static GSList    *group_ops;
	static GtkWidget *r;

	data_analysis_output_t  dao;

	char  *text;
	int   selection;
	static Range range_input;
	int   i;

	if (!dialog[ti]) {
		dialog[ti] = gnome_dialog_new (_(tools[ti].name.col1),
					   _("OK"),
					   GNOME_STOCK_BUTTON_CANCEL,
					   NULL);

                gnome_dialog_close_hides (GNOME_DIALOG (dialog[ti]), TRUE);
                gnome_dialog_set_parent (GNOME_DIALOG (dialog[ti]),
                                         GTK_WINDOW (wb->toplevel));
		box = gtk_vbox_new (FALSE, 0);
		hbox_x = gtk_hbox_new (FALSE, 0);
		group_box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (box), hbox_x);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog[ti])->vbox), box);

		input_range = gtk_entry_new_with_max_length (20);
		input_range_label = gtk_label_new ("Input Range:");

		gtk_box_pack_start_defaults (GTK_BOX (hbox_x), 
					     input_range_label);
		gtk_box_pack_start_defaults (GTK_BOX (hbox_x),
					     input_range);
		groupped_label = gtk_label_new ("Groupped By:");
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog[ti])->vbox), 
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
						      (dialog[ti])->vbox), 
					     group_box);

		if (ti == 2) {
		        check_buttons = gtk_vbox_new (FALSE, 0);
		        add_check_buttons(check_buttons, desc_stat_buttons);
			gtk_box_pack_start (GTK_BOX (GNOME_DIALOG
						     (dialog[ti])->vbox), 
					    check_buttons, TRUE, TRUE, 0);
		}

		gtk_widget_show_all (dialog[ti]);
	} else
		gtk_widget_show_all (dialog[ti]);

        gtk_widget_grab_focus (input_range);

tool_dialog_loop:

	selection = gnome_dialog_run (GNOME_DIALOG (dialog[ti]));
	if (selection == 1) {
	        gnome_dialog_close (GNOME_DIALOG (dialog[ti]));
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
		goto tool_dialog_loop;
	}

	/* TODO: radio buttos for outputs */
	dao.type = NewSheetOutput;

	switch (ti) {
	case 0:
	        correlation_tool (wb, sheet, &range_input, !i, &dao);
		break;
	case 1:
		covariance_tool (wb, sheet, &range_input, !i, &dao);
		break;
	case 2:
		text = gtk_entry_get_text (GTK_ENTRY (ds.entry[1]));
		ds.c_level = atof(text);
		text = gtk_entry_get_text (GTK_ENTRY (ds.entry[2]));
		ds.k_largest = atoi(text);
		text = gtk_entry_get_text (GTK_ENTRY (ds.entry[3]));
		ds.k_smallest = atoi(text);

	        descriptive_stat_tool(wb, sheet, &range_input, !i, &ds, &dao);
		break;
	}

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog[ti]));
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
		tool_dialog_range(wb, sheet, selected_row);
	}
}
