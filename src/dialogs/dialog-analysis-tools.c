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


static void dialog_correlation_tool(Workbook *wb, Sheet *sheet);
static void dialog_covariance_tool(Workbook *wb, Sheet *sheet);
static void dialog_descriptive_stat_tool(Workbook *wb, Sheet *sheet);
static void dialog_ztest_tool(Workbook *wb, Sheet *sheet);
static void dialog_sampling_tool(Workbook *wb, Sheet *sheet);
static void dialog_ttest_paired_tool(Workbook *wb, Sheet *sheet);
static void dialog_ttest_eq_tool(Workbook *wb, Sheet *sheet);
static void dialog_ttest_neq_tool(Workbook *wb, Sheet *sheet);
static void dialog_ftest_tool(Workbook *wb, Sheet *sheet);


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
        { { "Correlation", NULL },
	  dialog_correlation_tool },
        { { "Covariance", NULL },
	  dialog_covariance_tool },
        { { "Descriptive Statistics", NULL },
	  dialog_descriptive_stat_tool },
        { { "F-Test: Two-Sample for Variances", NULL }, 
	  dialog_ftest_tool },
        { { "Sampling", NULL },
	  dialog_sampling_tool },
        { { "t-Test: Paired Two Sample for Means", NULL }, 
	  dialog_ttest_paired_tool },
        { { "t-Test: Two-Sample Assuming Equal Variances", NULL }, 
	  dialog_ttest_eq_tool },
        { { "t-Test: Two-Sample Assuming Unequal Variances", NULL }, 
	  dialog_ttest_neq_tool },
        { { "z-Test: Two Sample for Means", NULL },
	  dialog_ztest_tool },
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

static GtkWidget *
new_frame(char *name, GtkWidget *target_box)
{
        GtkWidget *frame, *box;

	frame = gtk_frame_new(name);
	box = gtk_vbox_new(FALSE, 0);
	gtk_container_add (GTK_CONTAINER (frame), box);
	gtk_box_pack_start (GTK_BOX (target_box), frame, FALSE, FALSE, 0);

	return box;
}

void
error_in_entry(Workbook *wb, GtkWidget *entry, char *err_str)
{
        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR, err_str);

	gtk_widget_grab_focus (entry);
	gtk_entry_set_position(GTK_ENTRY (entry), 0);
	gtk_entry_select_region(GTK_ENTRY (entry), 0, 
				GTK_ENTRY(entry)->text_length);
}

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

static int
parse_output(int output, Sheet *sheet,
	     GtkWidget *entry, Workbook *wb, data_analysis_output_t *dao)
{
        char  *text;
	Range range;

	text = gtk_entry_get_text (GTK_ENTRY (entry));
	if (output == 1 && !parse_range (text, &range.start_col,
			  &range.start_row,
			  &range.end_col,
			  &range.end_row)) {
	        error_in_entry(wb, entry, 
			       "You should introduce a valid cell range "
			       "in 'Into a Range:'");
		return 1;
	}

	switch (output) {
	case 0:
	        dao->type = NewSheetOutput;
		break;
	case 1:
	        dao->type = RangeOutput;
		dao->start_col = range.start_col;
		dao->start_row = range.start_row;
		dao->cols = range.end_col-range.start_col+1;
		dao->rows = range.end_row-range.start_row+1;
		dao->sheet = sheet;
		break;
	}

	return 0;
}

static GtkWidget *
add_output_frame(GtkWidget *box, GSList **output_ops)
{
        GtkWidget *r, *hbox, *output_range_entry;

        box = new_frame("Output:", box);
	*output_ops = NULL;
	r = gtk_radio_button_new_with_label(*output_ops, "New Sheet");
	*output_ops = GTK_RADIO_BUTTON (r)->group;
	gtk_box_pack_start_defaults (GTK_BOX (box), r);
	hbox = gtk_hbox_new (FALSE, 0);
	r = gtk_radio_button_new_with_label(*output_ops,
					    "Into a Range:");
	*output_ops = GTK_RADIO_BUTTON (r)->group;
	gtk_box_pack_start_defaults (GTK_BOX (hbox), r);
	output_range_entry = gtk_entry_new_with_max_length (20);
	gtk_box_pack_start_defaults (GTK_BOX (hbox), 
				     output_range_entry);
	gtk_box_pack_start_defaults (GTK_BOX (box), hbox);

	return output_range_entry;
}

static void
dialog_correlation_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box, *group_box, *groupped_label;
	static GtkWidget *range_entry, *r, *output_range_entry, *hbox;
	static GSList    *group_ops, *output_ops;

	data_analysis_output_t  dao;

	char  *text;
	int   selection;
	static Range range;
	int   i=0, output, size;

	if (!dialog) {
	        dialog = new_dialog("Correlation", wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);
		group_box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame("Input:", box);

		range_entry = hbox_pack_label_and_entry
		  ("Input Range:", "", 20, box);

		group_ops = NULL;
		groupped_label = gtk_label_new ("Groupped By:");
		gtk_box_pack_start_defaults (GTK_BOX (box), groupped_label);
		for (i = 0; groupped_ops [i]; i++) {
			r = gtk_radio_button_new_with_label (group_ops,
							     _(groupped_ops[i])
							     );
			group_ops = GTK_RADIO_BUTTON (r)->group;
			gtk_box_pack_start_defaults (GTK_BOX (group_box), r);
		}
		gtk_box_pack_start_defaults (GTK_BOX (box), group_box);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (range_entry);

correlation_dialog_loop:

	selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	if (selection == 1) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	i = gtk_radio_group_get_selected (group_ops);
	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start_col,
			  &range.start_row,
			  &range.end_col,
			  &range.end_row)) {
	        error_in_entry(wb, range_entry, 
			       "You should introduce a valid cell range "
			       "in 'Range:'");
		goto correlation_dialog_loop;
	}

	if (parse_output(output, sheet, output_range_entry, wb, &dao))
	        goto correlation_dialog_loop;

	if (correlation_tool (wb, sheet, &range, !i, &dao))
	        goto correlation_dialog_loop;

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}

static void
dialog_covariance_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box, *group_box, *groupped_label;
	static GtkWidget *range_entry, *output_range_entry;
	static GSList    *group_ops, *output_ops;

	data_analysis_output_t  dao;

	char  *text;
	int   selection, output;
	static Range range;
	int   i=0, size;

	if (!dialog) {
	        dialog = new_dialog("Covariance", wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);
		group_box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame("Input:", box);

		range_entry = hbox_pack_label_and_entry
		  ("Input Range:", "", 20, box);

		group_ops = NULL;
		groupped_label = gtk_label_new ("Groupped By:");
		gtk_box_pack_start_defaults (GTK_BOX (box), groupped_label);
		for (i = 0; groupped_ops [i]; i++) {
		        GtkWidget *r;

			r = gtk_radio_button_new_with_label (group_ops,
							     _(groupped_ops[i])
							     );
			group_ops = GTK_RADIO_BUTTON (r)->group;
			gtk_box_pack_start_defaults (GTK_BOX (group_box), r);
		}
		gtk_box_pack_start_defaults (GTK_BOX (box), group_box);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (range_entry);

covariance_dialog_loop:

	selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	if (selection == 1) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	i = gtk_radio_group_get_selected (group_ops);
	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start_col,
			  &range.start_row,
			  &range.end_col,
			  &range.end_row)) {
	        error_in_entry(wb, range_entry, 
			       "You should introduce a valid cell range "
			       "in 'Range:'");
		goto covariance_dialog_loop;
	}

	if (parse_output(output, sheet, output_range_entry, wb, &dao))
	        goto covariance_dialog_loop;

	if (covariance_tool (wb, sheet, &range, !i, &dao))
	        goto covariance_dialog_loop;

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}

static void
dialog_sampling_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box, *sampling_box, *sampling_label;
	static GtkWidget *range_entry, *output_range_entry, *sampling_entry[2];
	static GSList    *sampling_ops, *output_ops;

	data_analysis_output_t  dao;

	char  *text;
	int   selection, output;
	static Range range;
	int   i=0, size;

	if (!dialog) {
	        dialog = new_dialog("Sampling", wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame("Input:", box);

		range_entry = hbox_pack_label_and_entry
		  ("Input Range:", "", 20, box);

		sampling_label = gtk_label_new ("Sampling Method:");

		gtk_box_pack_start_defaults(GTK_BOX (box), sampling_label);

		sampling_box = gtk_vbox_new (FALSE, 0);
		sampling_ops = NULL;
		for (i = 0; sample_method_ops [i]; i++) {
		        GtkWidget *hbox = gtk_hbox_new (FALSE, 0);
			GtkWidget *vbox = gtk_vbox_new (FALSE, 0);
			GtkWidget *label, *r;

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
			gtk_box_pack_start_defaults(GTK_BOX (box), vbox);
		}
		gtk_box_pack_start_defaults(GTK_BOX (box), sampling_box);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (range_entry);

sampling_dialog_loop:

	selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	if (selection == 1) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start_col,
			  &range.start_row,
			  &range.end_col,
			  &range.end_row)) {
	        error_in_entry(wb, range_entry, 
			       "You should introduce a valid cell range "
			       "in 'Range:'");
		goto sampling_dialog_loop;
	}

	i = gtk_radio_group_get_selected(sampling_ops);
	output = gtk_radio_group_get_selected (output_ops);

	if (parse_output(output, sheet, output_range_entry, wb, &dao))
	        goto sampling_dialog_loop;

	text = gtk_entry_get_text (GTK_ENTRY (sampling_entry[i]));
	size = atoi(text);

	if (sampling_tool (wb, sheet, &range, !i, size, &dao))
	        goto sampling_dialog_loop;

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}

static void
dialog_descriptive_stat_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box, *group_box, *groupped_label;
	static GtkWidget *range_entry, *output_range_entry;
	static GtkWidget *check_buttons;
	static GSList    *group_ops, *output_ops;

	data_analysis_output_t  dao;

	char  *text;
	int   selection, output;
	static Range range;
	int   i=0, size;

	if (!dialog) {
	        dialog = new_dialog("Descriptive Statistics", wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);
		group_box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame("Input:", box);

		range_entry = hbox_pack_label_and_entry
		  ("Input Range:", "", 20, box);

		group_ops = NULL;
		groupped_label = gtk_label_new ("Groupped By:");
		gtk_box_pack_start_defaults (GTK_BOX (box), groupped_label);
		for (i = 0; groupped_ops [i]; i++) {
		        GtkWidget *r;

			r = gtk_radio_button_new_with_label (group_ops,
							     _(groupped_ops[i])
							     );
			group_ops = GTK_RADIO_BUTTON (r)->group;
			gtk_box_pack_start_defaults (GTK_BOX (group_box), r);
		}
		gtk_box_pack_start_defaults (GTK_BOX (box), group_box);

		check_buttons = gtk_vbox_new (FALSE, 0);
		add_check_buttons(check_buttons, desc_stat_buttons);
		gtk_box_pack_start (GTK_BOX (GNOME_DIALOG
					     (dialog)->vbox), 
				    check_buttons, TRUE, TRUE, 0);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (range_entry);

stat_dialog_loop:

	selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	if (selection == 1) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	i = gtk_radio_group_get_selected (group_ops);
	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start_col,
			  &range.start_row,
			  &range.end_col,
			  &range.end_row)) {
	        error_in_entry(wb, range_entry, 
			       "You should introduce a valid cell range "
			       "in 'Range:'");
		goto stat_dialog_loop;
	}

	if (parse_output(output, sheet, output_range_entry, wb, &dao))
	        goto stat_dialog_loop;

	text = gtk_entry_get_text (GTK_ENTRY (ds.entry[1]));
	ds.c_level = atof(text);
	text = gtk_entry_get_text (GTK_ENTRY (ds.entry[2]));
	ds.k_largest = atoi(text);
	text = gtk_entry_get_text (GTK_ENTRY (ds.entry[3]));
	ds.k_smallest = atoi(text);

	if (descriptive_stat_tool(wb, sheet, &range, !i, &ds, &dao))
	        goto stat_dialog_loop;

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}

static void
dialog_ztest_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box;
	static GtkWidget *range1_entry, *range2_entry, *output_range_entry;
	static GtkWidget *known_var1_entry, *known_var2_entry;
	static GtkWidget *mean_diff_entry, *alpha_entry;
	static GSList    *output_ops;

	data_analysis_output_t  dao;
	float_t mean_diff, alpha, var1, var2;

	char  *text;
	int   selection, output;
	static Range range_input1, range_input2;
	int   i=0, size;

	if (!dialog) {
	        dialog = new_dialog("z-Test: Two Sample for Means",
				    wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame("Input:", box);

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

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

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

	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start_col,
			  &range_input1.start_row,
			  &range_input1.end_col,
			  &range_input1.end_row)) {
	        error_in_entry(wb, range1_entry, 
			       "You should introduce a valid cell range "
			       "in 'Variable 1:'");
		goto ztest_dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start_col,
			  &range_input2.start_row,
			  &range_input2.end_col,
			  &range_input2.end_row)) {
	        error_in_entry(wb, range2_entry, 
			       "You should introduce a valid cell range "
			       "in 'Variable 2:'");
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

	if (parse_output(output, sheet, output_range_entry, wb, &dao))
	        goto ztest_dialog_loop;

	if (ztest_tool (wb, sheet, &range_input1, &range_input2, mean_diff,
		    var1, var2, alpha, &dao))
	        goto ztest_dialog_loop;

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}

static void
dialog_ttest_paired_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box;
	static GtkWidget *range1_entry, *range2_entry, *output_range_entry;
	static GtkWidget *mean_diff_entry, *alpha_entry;
	static GSList    *output_ops;

	data_analysis_output_t  dao;
	float_t mean_diff, alpha;

	char  *text;
	int   selection, output;
	static Range range_input1, range_input2;
	int   i=0, size;

	if (!dialog) {
	        dialog = new_dialog("t-Test: Paired Two Sample for Means",
				    wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame("Input:", box);

		range1_entry = hbox_pack_label_and_entry
		  ("Variable 1 Range:", "", 20, box);

		range2_entry = hbox_pack_label_and_entry
		  ("Variable 2 Range:", "", 20, box);

		mean_diff_entry = hbox_pack_label_and_entry
		  ("Hypothesized Mean Difference:", "0", 20, box);

		alpha_entry = hbox_pack_label_and_entry("Alpha:", "0.95",
							20, box);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

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

	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start_col,
			  &range_input1.start_row,
			  &range_input1.end_col,
			  &range_input1.end_row)) {
	        error_in_entry(wb, range1_entry, 
			       "You should introduce a valid cell range "
			       "in 'Variable 1:'");
		goto ttest_dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start_col,
			  &range_input2.start_row,
			  &range_input2.end_col,
			  &range_input2.end_row)) {
	        error_in_entry(wb, range2_entry, 
			       "You should introduce a valid cell range "
			       "in 'Variable 2:'");
		goto ttest_dialog_loop;
	}
	text = gtk_entry_get_text (GTK_ENTRY (mean_diff_entry));
	mean_diff = atof(text);

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	if (parse_output(output, sheet, output_range_entry, wb, &dao))
	        goto ttest_dialog_loop;

	if (ttest_paired_tool (wb, sheet, &range_input1, &range_input2,
			       mean_diff, alpha, &dao))
	        goto ttest_dialog_loop;

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}

static void
dialog_ttest_eq_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box;
	static GtkWidget *range1_entry, *range2_entry, *output_range_entry;
	static GtkWidget *mean_diff_entry, *alpha_entry;
	static GSList    *output_ops;

	data_analysis_output_t  dao;
	float_t mean_diff, alpha;

	char  *text;
	int   selection, output;
	static Range range_input1, range_input2;
	int   i=0, size;

	if (!dialog) {
	        dialog = new_dialog("t-Test: Two-Sample Assuming "
				    "Equal Variances",
				    wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame("Input:", box);

		range1_entry = hbox_pack_label_and_entry
		  ("Variable 1 Range:", "", 20, box);

		range2_entry = hbox_pack_label_and_entry
		  ("Variable 2 Range:", "", 20, box);

		mean_diff_entry = hbox_pack_label_and_entry
		  ("Hypothesized Mean Difference:", "0", 20, box);

		alpha_entry = hbox_pack_label_and_entry("Alpha:", "0.95",
							20, box);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

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

	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start_col,
			  &range_input1.start_row,
			  &range_input1.end_col,
			  &range_input1.end_row)) {
	        error_in_entry(wb, range1_entry, 
			       "You should introduce a valid cell range "
			       "in 'Variable 1:'");
		goto ttest_dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start_col,
			  &range_input2.start_row,
			  &range_input2.end_col,
			  &range_input2.end_row)) {
	        error_in_entry(wb, range2_entry, 
			       "You should introduce a valid cell range "
			       "in 'Variable 2:'");
		goto ttest_dialog_loop;
	}
	text = gtk_entry_get_text (GTK_ENTRY (mean_diff_entry));
	mean_diff = atof(text);

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	if (parse_output(output, sheet, output_range_entry, wb, &dao))
	        goto ttest_dialog_loop;

	if (ttest_eq_var_tool (wb, sheet, &range_input1, &range_input2,
			       mean_diff, alpha, &dao))
	        goto ttest_dialog_loop;

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}

static void
dialog_ttest_neq_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box;
	static GtkWidget *range1_entry, *range2_entry, *output_range_entry;
	static GtkWidget *mean_diff_entry, *alpha_entry;
	static GSList    *output_ops;

	data_analysis_output_t  dao;
	float_t mean_diff, alpha;

	char  *text;
	int   selection, output;
	static Range range_input1, range_input2;
	int   i=0, size;

	if (!dialog) {
	        dialog = new_dialog("t-Test: Two-Sample Assuming "
				    "Unequal Variances",
				    wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame("Input:", box);

		range1_entry = hbox_pack_label_and_entry
		  ("Variable 1 Range:", "", 20, box);

		range2_entry = hbox_pack_label_and_entry
		  ("Variable 2 Range:", "", 20, box);

		mean_diff_entry = hbox_pack_label_and_entry
		  ("Hypothesized Mean Difference:", "0", 20, box);

		alpha_entry = hbox_pack_label_and_entry("Alpha:", "0.95",
							20, box);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

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

	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start_col,
			  &range_input1.start_row,
			  &range_input1.end_col,
			  &range_input1.end_row)) {
	        error_in_entry(wb, range1_entry, 
			       "You should introduce a valid cell range "
			       "in 'Variable 1:'");
		goto ttest_dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start_col,
			  &range_input2.start_row,
			  &range_input2.end_col,
			  &range_input2.end_row)) {
	        error_in_entry(wb, range2_entry, 
			       "You should introduce a valid cell range "
			       "in 'Variable 2:'");
		goto ttest_dialog_loop;
	}
	text = gtk_entry_get_text (GTK_ENTRY (mean_diff_entry));
	mean_diff = atof(text);

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	if (parse_output(output, sheet, output_range_entry, wb, &dao))
	        goto ttest_dialog_loop;

	if (ttest_neq_var_tool (wb, sheet, &range_input1, &range_input2,
				mean_diff, alpha, &dao))
	        goto ttest_dialog_loop;

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}

static void
dialog_ftest_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box, *vbox;
	static GtkWidget *range1_entry, *range2_entry, *output_range_entry;
	static GtkWidget *alpha_entry;
	static GSList    *output_ops;

	data_analysis_output_t  dao;
	float_t alpha;

	char  *text;
	int   selection, output;
	static Range range_input1, range_input2;
	int   i=0, size;

	if (!dialog) {
	        dialog = new_dialog("F-Test: Two-Sample for Variances",
				    wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);
		vbox = new_frame("Input:", box);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		range1_entry = hbox_pack_label_and_entry
		  ("Variable 1 Range:", "", 20, vbox);

		range2_entry = hbox_pack_label_and_entry
		  ("Variable 2 Range:", "", 20, vbox);

		alpha_entry = hbox_pack_label_and_entry("Alpha:", "0.95",
							20, vbox);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (range1_entry);

ftest_dialog_loop:

	selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	if (selection == 1) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start_col,
			  &range_input1.start_row,
			  &range_input1.end_col,
			  &range_input1.end_row)) {
	        error_in_entry(wb, range1_entry, 
			       "You should introduce a valid cell range "
			       "in 'Variable 1:'");
		goto ftest_dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start_col,
			  &range_input2.start_row,
			  &range_input2.end_col,
			  &range_input2.end_row)) {
	        error_in_entry(wb, range2_entry, 
			       "You should introduce a valid cell range "
			       "in 'Variable 2:'");
		goto ftest_dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	if (parse_output(output, sheet, output_range_entry, wb, &dao))
	        goto ftest_dialog_loop;

	if (ftest_tool (wb, sheet, &range_input1, &range_input2, alpha, &dao))
	        goto ftest_dialog_loop;

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
		tools[selected_row].fun(wb, sheet);
	}
}
