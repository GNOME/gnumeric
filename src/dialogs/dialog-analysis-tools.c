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

static void
dummy_fun (Workbook *wb, Sheet *sheet)
{
	/* Nothing.  */
}

static void dialog_ztest_tool(Workbook *wb, Sheet *sheet);
static void dialog_ttest_paired_tool(Workbook *wb, Sheet *sheet);
static void dialog_ttest_eq_tool(Workbook *wb, Sheet *sheet);
static void dialog_ttest_neq_tool(Workbook *wb, Sheet *sheet);


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
        { { "Sampling", NULL }, dummy_fun },
        { { "t-Test: Paired Two Sample for Means", NULL }, 
	  dialog_ttest_paired_tool },
        { { "t-Test: Two-Sample Assuming Equal Variances", NULL }, 
	  dialog_ttest_eq_tool },
        { { "t-Test: Two-Sample Assuming Unequal Variances", NULL }, 
	  dialog_ttest_neq_tool },
        { { "z-Test: Two Sample for Means", NULL }, dialog_ztest_tool },
	{ { NULL, NULL }, NULL }
};

static void
summary_stat_signal_fun()
{
        ds.summary_statistics = !ds.summary_statistics;
}

static void
confidence_signal_fun()
{
        ds.confidence_level = !ds.confidence_level;
}

static void
kth_largest_signal_fun()
{
        ds.kth_largest = !ds.kth_largest;
}

static void
kth_smallest_signal_fun()
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

static char *sample_method_ops [] = {
        N_("Periodic"),
        N_("Random"),
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

static void
tool_dialog_range(Workbook *wb, Sheet *sheet, int ti)
{
        static GtkWidget *dialog[8];
	static GtkWidget *box, *hbox_x, *group_box;
	static GtkWidget *input_range, *groupped_label;
	static GtkWidget *input_range_label;
	static GtkWidget *check_buttons;
	static GSList    *group_ops, *sampling_ops;
	static GtkWidget *r;
	static GtkWidget *sampling_entry[2];

	data_analysis_output_t  dao;

	char  *text;
	int   selection;
	static Range range_input;
	int   i=0, size;

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

		group_ops = NULL;
		if (ti == 3)
		        goto skip_groupped;
		groupped_label = gtk_label_new ("Groupped By:");
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog[ti])->vbox), 
					     groupped_label);
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

	skip_groupped:

		/* Tool specific buttons and entries */
		if (ti == 2) {
		        check_buttons = gtk_vbox_new (FALSE, 0);
		        add_check_buttons(check_buttons, desc_stat_buttons);
			gtk_box_pack_start (GTK_BOX (GNOME_DIALOG
						     (dialog[ti])->vbox), 
					    check_buttons, TRUE, TRUE, 0);
		} else if (ti == 3) {
		        GtkWidget *sampling_label =
			  gtk_label_new ("Sampling Method:");
			GtkWidget *sampling_box;

			gtk_box_pack_start_defaults
			  (GTK_BOX (GNOME_DIALOG(dialog[ti])->vbox), 
			   sampling_label);

		        sampling_box = gtk_vbox_new (FALSE, 0);
			sampling_ops = NULL;
			for (i = 0; sample_method_ops [i]; i++) {
			        GtkWidget *hbox = gtk_hbox_new (FALSE, 0);
			        GtkWidget *vbox = gtk_vbox_new (FALSE, 0);
				GtkWidget *label;

				if (i==0)
				        label = gtk_label_new ("Period:");
				else
				        label =
					  gtk_label_new ("Number of Samples:");
				sampling_entry[i] =
				  gtk_entry_new_with_max_length (20);
			        r = gtk_radio_button_new_with_label
				  (sampling_ops, _(sample_method_ops[i]));
				sampling_ops = GTK_RADIO_BUTTON (r)->group;
				gtk_box_pack_start_defaults (GTK_BOX (hbox),
							     label);
				gtk_box_pack_start_defaults(GTK_BOX (hbox),
							    sampling_entry[i]);
				gtk_box_pack_start_defaults (GTK_BOX (vbox),
							     r);
				gtk_box_pack_start_defaults (GTK_BOX (vbox),
							     hbox);
				gtk_box_pack_start_defaults
				  (GTK_BOX (GNOME_DIALOG(dialog[ti])->vbox),
				   vbox);
			}
			gtk_box_pack_start_defaults
			  (GTK_BOX (GNOME_DIALOG(dialog[ti])->vbox), 
			   sampling_box);
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

	if (ti != 3)
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
	case 3:
	        i = gtk_radio_group_get_selected(sampling_ops);
		text = gtk_entry_get_text (GTK_ENTRY (sampling_entry[i]));
		size = atoi(text);
	        sampling_tool (wb, sheet, &range_input, !i, size, &dao);
		break;
	}

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog[ti]));
}

static GtkWidget *
hbox_pack_label_and_entry(char *str, char *default_str,
			  int entry_len, GtkWidget *vbox)
{
        GtkWidget *box, *label, *entry;

        box = gtk_hbox_new (FALSE, 0);
	entry = gtk_entry_new_with_max_length (entry_len);
	label = gtk_label_new (str);
	gtk_entry_set_text (GTK_ENTRY (entry), default_str);

	gtk_box_pack_start_defaults (GTK_BOX (box), label);
	gtk_box_pack_start_defaults (GTK_BOX (box), entry);
	gtk_box_pack_start_defaults (GTK_BOX (vbox), box);

	return entry;
}

static GtkWidget *
new_dialog(char *name, GtkWidget *win)
{
        GtkWidget *dialog;

        dialog = gnome_dialog_new (_(name),
				   _("OK"),
				   GNOME_STOCK_BUTTON_CANCEL,
				   NULL);

	gnome_dialog_close_hides (GNOME_DIALOG (dialog), TRUE);
	gnome_dialog_set_parent (GNOME_DIALOG (dialog), GTK_WINDOW (win));

	return dialog;
}

static void
dialog_ztest_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box;
	static GtkWidget *range1_entry, *range2_entry;
	static GtkWidget *known_var1_entry, *known_var2_entry;
	static GtkWidget *mean_diff_entry, *alpha_entry;

	data_analysis_output_t  dao;
	float_t mean_diff, alpha, var1, var2;

	char  *text;
	int   selection;
	static Range range_input1, range_input2;
	int   i=0, size;

	if (!dialog) {
	        dialog = new_dialog("z-Test: Two Sample for Means",
				    wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		range1_entry = hbox_pack_label_and_entry
		  ("Variable 1 Range:", "", 20, box);

		range2_entry = hbox_pack_label_and_entry
		  ("Variable 2 Range:", "", 20, box);

		mean_diff_entry = hbox_pack_label_and_entry
		  ("Hypothesized Mean Difference:", "0", 20, box);

		known_var1_entry = hbox_pack_label_and_entry
		  ("Variable 1 Variance (known):", "", 20, box);

		known_var2_entry = hbox_pack_label_and_entry
		  ("Variable 2 Variance (known):", "", 20, box);

		alpha_entry = hbox_pack_label_and_entry("Alpha:", "0.95",
							20, box);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (range1_entry);

ztest_dialog_loop:

	selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	if (selection == 1) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start_col,
			  &range_input1.start_row,
			  &range_input1.end_col,
			  &range_input1.end_row)) {
	        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell range "
				   "in 'Variable 1:'"));
		gtk_widget_grab_focus (range1_entry);
		gtk_entry_set_position(GTK_ENTRY (range1_entry), 0);
		gtk_entry_select_region(GTK_ENTRY (range1_entry), 0, 
				GTK_ENTRY(range1_entry)->text_length);
		goto ztest_dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start_col,
			  &range_input2.start_row,
			  &range_input2.end_col,
			  &range_input2.end_row)) {
	        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell range "
				   "in 'Variable 2:'"));
		gtk_widget_grab_focus (range2_entry);
		gtk_entry_set_position(GTK_ENTRY (range2_entry), 0);
		gtk_entry_select_region(GTK_ENTRY (range2_entry), 0, 
				GTK_ENTRY(range2_entry)->text_length);
		goto ztest_dialog_loop;
	}
	text = gtk_entry_get_text (GTK_ENTRY (mean_diff_entry));
	mean_diff = atof(text);

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	text = gtk_entry_get_text (GTK_ENTRY (known_var1_entry));
	var1 = atof(text);

	text = gtk_entry_get_text (GTK_ENTRY (known_var2_entry));
	var2 = atof(text);

	/* TODO: radio buttos for outputs */
	dao.type = NewSheetOutput;

	ztest_tool (wb, sheet, &range_input1, &range_input2, mean_diff,
		    var1, var2, alpha, &dao);

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}

static void
dialog_ttest_paired_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box;
	static GtkWidget *range1_entry, *range2_entry;
	static GtkWidget *mean_diff_entry, *alpha_entry;

	data_analysis_output_t  dao;
	float_t mean_diff, alpha;

	char  *text;
	int   selection;
	static Range range_input1, range_input2;
	int   i=0, size;

	if (!dialog) {
	        dialog = new_dialog("t-Test: Paired Two Sample for Means",
				    wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		range1_entry = hbox_pack_label_and_entry
		  ("Variable 1 Range:", "", 20, box);

		range2_entry = hbox_pack_label_and_entry
		  ("Variable 2 Range:", "", 20, box);

		mean_diff_entry = hbox_pack_label_and_entry
		  ("Hypothesized Mean Difference:", "0", 20, box);

		alpha_entry = hbox_pack_label_and_entry("Alpha:", "0.95",
							20, box);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (range1_entry);

ttest_dialog_loop:

	selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	if (selection == 1) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start_col,
			  &range_input1.start_row,
			  &range_input1.end_col,
			  &range_input1.end_row)) {
	        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell range "
				   "in 'Variable 1:'"));
		gtk_widget_grab_focus (range1_entry);
		gtk_entry_set_position(GTK_ENTRY (range1_entry), 0);
		gtk_entry_select_region(GTK_ENTRY (range1_entry), 0, 
				GTK_ENTRY(range1_entry)->text_length);
		goto ttest_dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start_col,
			  &range_input2.start_row,
			  &range_input2.end_col,
			  &range_input2.end_row)) {
	        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell range "
				   "in 'Variable 2:'"));
		gtk_widget_grab_focus (range2_entry);
		gtk_entry_set_position(GTK_ENTRY (range2_entry), 0);
		gtk_entry_select_region(GTK_ENTRY (range2_entry), 0, 
				GTK_ENTRY(range2_entry)->text_length);
		goto ttest_dialog_loop;
	}
	text = gtk_entry_get_text (GTK_ENTRY (mean_diff_entry));
	mean_diff = atof(text);

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	/* TODO: radio buttos for outputs */
	dao.type = NewSheetOutput;

	ttest_paired_tool (wb, sheet, &range_input1, &range_input2, mean_diff,
			   alpha, &dao);

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}

static void
dialog_ttest_eq_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box;
	static GtkWidget *range1_entry, *range2_entry;
	static GtkWidget *mean_diff_entry, *alpha_entry;

	data_analysis_output_t  dao;
	float_t mean_diff, alpha;

	char  *text;
	int   selection;
	static Range range_input1, range_input2;
	int   i=0, size;

	if (!dialog) {
	        dialog = new_dialog("t-Test: Two-Sample Assuming "
				    "Equal Variances",
				    wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		range1_entry = hbox_pack_label_and_entry
		  ("Variable 1 Range:", "", 20, box);

		range2_entry = hbox_pack_label_and_entry
		  ("Variable 2 Range:", "", 20, box);

		mean_diff_entry = hbox_pack_label_and_entry
		  ("Hypothesized Mean Difference:", "0", 20, box);

		alpha_entry = hbox_pack_label_and_entry("Alpha:", "0.95",
							20, box);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (range1_entry);

ttest_dialog_loop:

	selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	if (selection == 1) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start_col,
			  &range_input1.start_row,
			  &range_input1.end_col,
			  &range_input1.end_row)) {
	        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell range "
				   "in 'Variable 1:'"));
		gtk_widget_grab_focus (range1_entry);
		gtk_entry_set_position(GTK_ENTRY (range1_entry), 0);
		gtk_entry_select_region(GTK_ENTRY (range1_entry), 0, 
				GTK_ENTRY(range1_entry)->text_length);
		goto ttest_dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start_col,
			  &range_input2.start_row,
			  &range_input2.end_col,
			  &range_input2.end_row)) {
	        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell range "
				   "in 'Variable 2:'"));
		gtk_widget_grab_focus (range2_entry);
		gtk_entry_set_position(GTK_ENTRY (range2_entry), 0);
		gtk_entry_select_region(GTK_ENTRY (range2_entry), 0, 
				GTK_ENTRY(range2_entry)->text_length);
		goto ttest_dialog_loop;
	}
	text = gtk_entry_get_text (GTK_ENTRY (mean_diff_entry));
	mean_diff = atof(text);

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	/* TODO: radio buttos for outputs */
	dao.type = NewSheetOutput;

	ttest_eq_var_tool (wb, sheet, &range_input1, &range_input2, mean_diff,
			   alpha, &dao);

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}

static void
dialog_ttest_neq_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box;
	static GtkWidget *range1_entry, *range2_entry;
	static GtkWidget *mean_diff_entry, *alpha_entry;

	data_analysis_output_t  dao;
	float_t mean_diff, alpha;

	char  *text;
	int   selection;
	static Range range_input1, range_input2;
	int   i=0, size;

	if (!dialog) {
	        dialog = new_dialog("t-Test: Two-Sample Assuming "
				    "Unequal Variances",
				    wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		range1_entry = hbox_pack_label_and_entry
		  ("Variable 1 Range:", "", 20, box);

		range2_entry = hbox_pack_label_and_entry
		  ("Variable 2 Range:", "", 20, box);

		mean_diff_entry = hbox_pack_label_and_entry
		  ("Hypothesized Mean Difference:", "0", 20, box);

		alpha_entry = hbox_pack_label_and_entry("Alpha:", "0.95",
							20, box);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (range1_entry);

ttest_dialog_loop:

	selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	if (selection == 1) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start_col,
			  &range_input1.start_row,
			  &range_input1.end_col,
			  &range_input1.end_row)) {
	        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell range "
				   "in 'Variable 1:'"));
		gtk_widget_grab_focus (range1_entry);
		gtk_entry_set_position(GTK_ENTRY (range1_entry), 0);
		gtk_entry_select_region(GTK_ENTRY (range1_entry), 0, 
				GTK_ENTRY(range1_entry)->text_length);
		goto ttest_dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start_col,
			  &range_input2.start_row,
			  &range_input2.end_col,
			  &range_input2.end_row)) {
	        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell range "
				   "in 'Variable 2:'"));
		gtk_widget_grab_focus (range2_entry);
		gtk_entry_set_position(GTK_ENTRY (range2_entry), 0);
		gtk_entry_select_region(GTK_ENTRY (range2_entry), 0, 
				GTK_ENTRY(range2_entry)->text_length);
		goto ttest_dialog_loop;
	}
	text = gtk_entry_get_text (GTK_ENTRY (mean_diff_entry));
	mean_diff = atof(text);

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	/* TODO: radio buttos for outputs */
	dao.type = NewSheetOutput;

	ttest_neq_var_tool (wb, sheet, &range_input1, &range_input2, mean_diff,
			    alpha, &dao);

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}


static void
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

	/* Run the dialog */
	selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	gnome_dialog_close (GNOME_DIALOG (dialog));

	if (selection == 0) {
	        g_return_if_fail (tools[selected_row].fun != NULL);
		if (selected_row >= 4)
		  tools[selected_row].fun(wb, sheet);
		else
		  tool_dialog_range(wb, sheet, selected_row);
	}
}
