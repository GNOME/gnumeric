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



static void dialog_correlation_tool(Workbook *wb, Sheet *sheet);
static void dialog_covariance_tool(Workbook *wb, Sheet *sheet);
static void dialog_descriptive_stat_tool(Workbook *wb, Sheet *sheet);
static void dialog_ztest_tool(Workbook *wb, Sheet *sheet);
static void dialog_ranking_tool(Workbook *wb, Sheet *sheet);
static void dialog_sampling_tool(Workbook *wb, Sheet *sheet);
static void dialog_ttest_paired_tool(Workbook *wb, Sheet *sheet);
static void dialog_ttest_eq_tool(Workbook *wb, Sheet *sheet);
static void dialog_ttest_neq_tool(Workbook *wb, Sheet *sheet);
static void dialog_ftest_tool(Workbook *wb, Sheet *sheet);
static void dialog_average_tool(Workbook *wb, Sheet *sheet);
static void dialog_random_tool(Workbook *wb, Sheet *sheet);
static void dialog_regression_tool(Workbook *wb, Sheet *sheet);
static void dialog_anova_single_factor_tool(Workbook *wb, Sheet *sheet);
static void dialog_anova_two_factor_without_r_tool(Workbook *wb, Sheet *sheet);


static descriptive_stat_tool_t ds;
static int                     label_row_flag, standard_errors_flag;
static random_distribution_t   distribution = DiscreteDistribution;



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

typedef struct {
        GtkWidget *dialog;
        GtkWidget *frame;
        GtkWidget *discrete_box, *uniform_box, *normal_box, *poisson_box;
        GtkWidget *bernoulli_box;
        GtkWidget *combo;
} random_tool_callback_t;



tool_list_t tools[] = {
        { { "Anova: Single Factor", NULL },
	  dialog_anova_single_factor_tool },
        { { "Anova: Two-Factor Without Replication", NULL },
	  dialog_anova_two_factor_without_r_tool },
        { { "Correlation", NULL },
	  dialog_correlation_tool },
        { { "Covariance", NULL },
	  dialog_covariance_tool },
        { { "Descriptive Statistics", NULL },
	  dialog_descriptive_stat_tool },
        { { "F-Test: Two-Sample for Variances", NULL }, 
	  dialog_ftest_tool },
        { { "Moving Average", NULL },
	  dialog_average_tool },
        { { "Random Number Generation", NULL },
	  dialog_random_tool },
        { { "Rank and Percentile", NULL },
	  dialog_ranking_tool },
        { { "Regression", NULL },
	  dialog_regression_tool },
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

/* Distribution strings for Random Number Generator */
static const char *distribution_strs[] = {
        N_("Discrete"),
        N_("Normal"),
        N_("Bernoulli"),
        N_("Uniform"),
        NULL
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

static void
first_row_label_signal_fun()
{
        label_row_flag = !label_row_flag;
}

static void
standard_errors_signal_fun()
{
        standard_errors_flag = !standard_errors_flag;
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

static check_button_t first_row_label_button[] = {
        { N_("Labels in First Row"), first_row_label_signal_fun, FALSE,
	  N_("") },
        { NULL, NULL }
};

static check_button_t label_button[] = {
        { N_("Labels"), first_row_label_signal_fun, FALSE,
	  N_("") },
        { NULL, NULL }
};

static check_button_t standard_errors_button[] = {
        { N_("Standard Errors"), standard_errors_signal_fun, FALSE,
	  N_("") },
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

static void
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
	if (output == 2 && !parse_range (text, &range.start_col,
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
	        dao->type = NewWorkbookOutput;
		break;
	case 2:
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

        box = new_frame("Output options:", box);
	*output_ops = NULL;
	r = gtk_radio_button_new_with_label(*output_ops, "New Sheet");
	*output_ops = GTK_RADIO_BUTTON (r)->group;
	gtk_box_pack_start_defaults (GTK_BOX (box), r);
	hbox = gtk_hbox_new (FALSE, 0);
	r = gtk_radio_button_new_with_label(*output_ops, "New Workbook");
	*output_ops = GTK_RADIO_BUTTON (r)->group;
	gtk_box_pack_start_defaults (GTK_BOX (box), r);
	hbox = gtk_hbox_new (FALSE, 0);
	r = gtk_radio_button_new_with_label(*output_ops,
					    "Output Range:");
	*output_ops = GTK_RADIO_BUTTON (r)->group;
	gtk_box_pack_start_defaults (GTK_BOX (hbox), r);
	output_range_entry = gtk_entry_new_with_max_length (20);
	gtk_box_pack_start_defaults (GTK_BOX (hbox), 
				     output_range_entry);
	gtk_box_pack_start_defaults (GTK_BOX (box), hbox);

	return output_range_entry;
}

static GSList *
add_groupped_by(GtkWidget *box)
{
        GtkWidget *r, *groupped_label, *group_box, *hbox;
	GSList    *group_ops;
	int       i;

	group_ops = NULL;
	groupped_label = gtk_label_new ("Groupped By:");
	hbox = gtk_hbox_new (FALSE, 0);
	group_box = gtk_vbox_new (FALSE, 0);
	gtk_box_pack_start_defaults (GTK_BOX (hbox), groupped_label);
	for (i = 0; groupped_ops [i]; i++) {
	        r = gtk_radio_button_new_with_label (group_ops,
						     _(groupped_ops[i]));
		group_ops = GTK_RADIO_BUTTON (r)->group;
		gtk_box_pack_start_defaults (GTK_BOX (group_box), r);
	}
	gtk_box_pack_start_defaults (GTK_BOX (hbox), group_box);
	gtk_box_pack_start_defaults (GTK_BOX (box), hbox);

	return group_ops;
}


static void
dialog_correlation_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box;
	static GtkWidget *range_entry, *output_range_entry;
	static GSList    *group_ops, *output_ops;
	static int       labels = 0;

	data_analysis_output_t  dao;

	char  *text;
	int   selection;
	static Range range;
	int   i=0, output;

	label_row_flag = labels;

	if (!dialog) {
	        dialog = new_dialog("Correlation", wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame("Input:", box);

		range_entry = hbox_pack_label_and_entry
		  ("Input Range:", "", 20, box);

		group_ops = add_groupped_by(box);

		add_check_buttons(box, label_button);

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

	labels = label_row_flag;
	dao.labels_flag = labels;

	if (correlation_tool (wb, sheet, &range, !i, &dao))
	        goto correlation_dialog_loop;

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}

static void
dialog_covariance_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box;
	static GtkWidget *range_entry, *output_range_entry;
	static GSList    *group_ops, *output_ops;
	static int       labels = 0;

	data_analysis_output_t  dao;

	char  *text;
	int   selection, output;
	static Range range;
	int   i=0;

	label_row_flag = labels;

	if (!dialog) {
	        dialog = new_dialog("Covariance", wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame("Input:", box);

		range_entry = hbox_pack_label_and_entry
		  ("Input Range:", "", 20, box);

		group_ops = add_groupped_by(box);
		add_check_buttons(box, label_button);

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

	labels = label_row_flag;
	dao.labels_flag = labels;

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
        static GtkWidget *dialog, *box;
	static GtkWidget *range_entry, *output_range_entry;
	static GtkWidget *check_buttons;
	static GSList    *group_ops, *output_ops;
	static int       labels = 0;

	data_analysis_output_t  dao;

	char  *text;
	int   selection, output;
	static Range range;
	int   i=0;

	label_row_flag = labels;

	if (!dialog) {
	        dialog = new_dialog("Descriptive Statistics", wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame("Input:", box);

		range_entry = hbox_pack_label_and_entry
		  ("Input Range:", "", 20, box);

		group_ops = add_groupped_by(box);
		add_check_buttons(box, first_row_label_button);

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

	labels = label_row_flag;
	if (labels)
	        range.start_row++;

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
	static int       labels = 0;

	data_analysis_output_t  dao;
	float_t mean_diff, alpha, var1, var2;

	char  *text;
	int   selection, output;
	static Range range_input1, range_input2;

	label_row_flag = labels;

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
		add_check_buttons(box, first_row_label_button);

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

	labels = label_row_flag;
	dao.labels_flag = labels;

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
	static int       labels = 0;

	data_analysis_output_t  dao;
	float_t mean_diff, alpha;

	char  *text;
	int   selection, output;
	static Range range_input1, range_input2;

	label_row_flag = labels;

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
		add_check_buttons(box, first_row_label_button);

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

	labels = label_row_flag;
	dao.labels_flag = labels;

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
	static int       labels = 0;

	data_analysis_output_t  dao;
	float_t mean_diff, alpha;

	char  *text;
	int   selection, output;
	static Range range_input1, range_input2;

	label_row_flag = labels;

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
		add_check_buttons(box, first_row_label_button);

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

	labels = label_row_flag;
	dao.labels_flag = labels;

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
	static int       labels = 0;

	data_analysis_output_t  dao;
	float_t mean_diff, alpha;

	char  *text;
	int   selection, output;
	static Range range_input1, range_input2;

	label_row_flag = labels;

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
		add_check_buttons(box, first_row_label_button);

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

	labels = label_row_flag;
	dao.labels_flag = labels;

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
	static int       labels = 0;

	data_analysis_output_t  dao;
	float_t alpha;

	char  *text;
	int   selection, output;
	static Range range_input1, range_input2;

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

		add_check_buttons(vbox, first_row_label_button);

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

	labels = label_row_flag;
	dao.labels_flag = labels;

	if (ftest_tool (wb, sheet, &range_input1, &range_input2, alpha, &dao))
	        goto ftest_dialog_loop;

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}


static GtkWidget *
pack_label_and_entry(char *str, char *default_str,
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


static void
distribution_callback(GtkWidget *widget, random_tool_callback_t *p)
{
        char *text;

	switch (distribution) {
	case UniformDistribution:
	        gtk_widget_hide (p->uniform_box);
		break;
	case BernoulliDistribution:
	        gtk_widget_hide (p->bernoulli_box);
		break;
	case NormalDistribution:
	        gtk_widget_hide (p->normal_box);
		break;
	case DiscreteDistribution:
	        gtk_widget_hide (p->discrete_box);
		break;
	default:
	        break;
	}

        text = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(p->combo)->entry));

	if (strcmp(text, "Uniform") == 0) {
	        distribution = UniformDistribution;
		gtk_widget_show (p->uniform_box);
	} else if (strcmp(text, "Bernoulli") == 0) {
	        distribution = BernoulliDistribution;
		gtk_widget_show (p->bernoulli_box);
	} else if (strcmp(text, "Normal") == 0) {
	        distribution = NormalDistribution;
		gtk_widget_show (p->normal_box);
	} else if (strcmp(text, "Discrete") == 0) {
	        distribution = DiscreteDistribution;
		gtk_widget_show (p->discrete_box);
	}
}

static void
dialog_random_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box, *param_box, *distribution_combo;
	static GtkWidget *vars_entry, *count_entry, *output_range_entry;
	static GtkWidget *discrete_range_entry;
	static GtkWidget *uniform_upper_entry, *uniform_lower_entry;
	static GtkWidget *normal_mean_entry, *normal_stdev_entry;
	static GtkWidget *poisson_lambda_entry;
	static GtkWidget *bernoulli_p_entry;

	static GSList    *output_ops;
	static GList     *distribution_type_strs;

	int    vars, count;
	random_tool_t    param;
	data_analysis_output_t  dao;

	static random_tool_callback_t callback_data;

	char  *text;
	int   selection;
	int   output;

	if (!dialog) {
	        dialog = new_dialog("Random Number Generation", wb->toplevel);

	        distribution_type_strs =
		  add_strings_to_glist (distribution_strs);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		vars_entry = hbox_pack_label_and_entry
		  ("Number of Variables:", "", 20, box);
	
		count_entry = hbox_pack_label_and_entry
		  ("Number of Random Numbers:", "", 20, box);

		distribution_combo = gtk_combo_new ();
		gtk_combo_set_popdown_strings (GTK_COMBO (distribution_combo),
					       distribution_type_strs);
		gtk_editable_set_editable
		  (GTK_EDITABLE (GTK_COMBO(distribution_combo)->entry), FALSE);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox),
					     distribution_combo);

		param_box = new_frame("Parameters:", box);

		callback_data.dialog = dialog;
		callback_data.frame = param_box;
		callback_data.combo = distribution_combo;

		gtk_signal_connect
		  (GTK_OBJECT(GTK_COMBO(distribution_combo)->entry),
		   "changed", GTK_SIGNAL_FUNC (distribution_callback),
		   &callback_data);

		callback_data.discrete_box = gtk_vbox_new (FALSE, 0);
		discrete_range_entry = pack_label_and_entry
		  ("Value and Probability Input Range:", "", 20,
		   callback_data.discrete_box);

		callback_data.uniform_box = gtk_vbox_new (FALSE, 0);
		uniform_lower_entry = 
		  pack_label_and_entry("Between:", "0", 20,
				       callback_data.uniform_box);
		uniform_upper_entry = 
		  pack_label_and_entry("And:", "1", 20, 
				       callback_data.uniform_box);

		callback_data.normal_box = gtk_vbox_new (FALSE, 0);
		normal_mean_entry = pack_label_and_entry
		  ("Mean = ", "0", 20, callback_data.normal_box);
		normal_stdev_entry = pack_label_and_entry
		  ("Standard Deviation = ", "1", 20, callback_data.normal_box);

		callback_data.poisson_box = gtk_vbox_new (FALSE, 0);
		poisson_lambda_entry = pack_label_and_entry
		  ("Lambda", "0", 20, callback_data.poisson_box);

		callback_data.bernoulli_box = gtk_vbox_new (FALSE, 0);
		bernoulli_p_entry = pack_label_and_entry
		  ("p Value", "0", 20, callback_data.bernoulli_box);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

		gtk_container_add(GTK_CONTAINER(param_box),
				  callback_data.discrete_box);

		gtk_container_add(GTK_CONTAINER(param_box),
				  callback_data.bernoulli_box);

		gtk_container_add(GTK_CONTAINER(param_box),
				  callback_data.uniform_box);
		gtk_container_add(GTK_CONTAINER(param_box),
				  callback_data.normal_box);

		gtk_widget_show_all (dialog);
		gtk_widget_hide (callback_data.uniform_box);
		gtk_widget_hide (callback_data.normal_box);
		gtk_widget_hide (callback_data.bernoulli_box);
	} else {
		gtk_widget_show_all (dialog);
		switch (distribution) {
		case DiscreteDistribution:
		        gtk_widget_hide (callback_data.uniform_box);
			gtk_widget_hide (callback_data.normal_box);
			gtk_widget_hide (callback_data.bernoulli_box);
			break;
		case NormalDistribution:
		        gtk_widget_hide (callback_data.discrete_box);
			gtk_widget_hide (callback_data.uniform_box);
			gtk_widget_hide (callback_data.bernoulli_box);
			break;
		case BernoulliDistribution:
		        gtk_widget_hide (callback_data.discrete_box);
			gtk_widget_hide (callback_data.uniform_box);
			gtk_widget_hide (callback_data.normal_box);
			break;
		case UniformDistribution:
		        gtk_widget_hide (callback_data.discrete_box);
			gtk_widget_hide (callback_data.normal_box);
			gtk_widget_hide (callback_data.bernoulli_box);
			break;
		default:
		        break;
		}
	}

        gtk_widget_grab_focus (vars_entry);

random_dialog_loop:

	selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	if (selection == 1) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (vars_entry));
	vars = atoi(text);

	text = gtk_entry_get_text (GTK_ENTRY (count_entry));
	count = atoi(text);

        text = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO
					    (distribution_combo)->entry));
	if (strcmp(text, "Uniform") == 0) {
	        distribution = UniformDistribution;
		text = gtk_entry_get_text (GTK_ENTRY (uniform_lower_entry));
		param.uniform.lower_limit = atof(text);
		text = gtk_entry_get_text (GTK_ENTRY (uniform_upper_entry));
		param.uniform.upper_limit = atof(text);
	} else if (strcmp(text, "Normal") == 0) {
	        distribution = NormalDistribution;
		text = gtk_entry_get_text (GTK_ENTRY (normal_mean_entry));
		param.normal.mean = atof(text);
		text = gtk_entry_get_text (GTK_ENTRY (normal_stdev_entry));
		param.normal.stdev = atof(text);
	} else if (strcmp(text, "Bernoulli") == 0) {
	        distribution = BernoulliDistribution;
		text = gtk_entry_get_text (GTK_ENTRY (bernoulli_p_entry));
		param.bernoulli.p = atof(text);
	} else if (strcmp(text, "Discrete") == 0) {
	        distribution = DiscreteDistribution;
		text = gtk_entry_get_text (GTK_ENTRY (discrete_range_entry));
		if (!parse_range (text, &param.discrete.start_col,
			  &param.discrete.start_row,
			  &param.discrete.end_col,
			  &param.discrete.end_row)) {
		        error_in_entry(wb, discrete_range_entry, 
				       "You should introduce a valid cell "
				       "range in 'Value and Probability Input "
				       "Range:'");
			goto random_dialog_loop;
		}
	} else
	        distribution = UniformDistribution;

	if (parse_output(output, sheet, output_range_entry, wb, &dao))
	        goto random_dialog_loop;

	if (random_tool (wb, sheet, vars, count, distribution, &param, &dao))
	        goto random_dialog_loop;

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}

static void
dialog_regression_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box, *vbox;
	static GtkWidget *range1_entry, *range2_entry, *output_range_entry;
	static GtkWidget *alpha_entry;
	static GSList    *output_ops;
	static int       labels = 0;

	data_analysis_output_t  dao;
	float_t alpha;

	char  *text;
	int   selection, output;
	static Range range_input1, range_input2;

	if (!dialog) {
	        dialog = new_dialog("Regression", wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);
		vbox = new_frame("Input:", box);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		range1_entry = hbox_pack_label_and_entry
		  ("Input Y Range:", "", 20, vbox);

		range2_entry = hbox_pack_label_and_entry
		  ("Input X Range:", "", 20, vbox);

		alpha_entry = hbox_pack_label_and_entry("Confidence Level:",
							"0.95", 20, vbox);

		add_check_buttons(vbox, first_row_label_button);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (range1_entry);

dialog_loop:

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
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start_col,
			  &range_input2.start_row,
			  &range_input2.end_col,
			  &range_input2.end_row)) {
	        error_in_entry(wb, range2_entry, 
			       "You should introduce a valid cell range "
			       "in 'Variable 2:'");
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	if (parse_output(output, sheet, output_range_entry, wb, &dao))
	        goto dialog_loop;

	labels = label_row_flag;
	if (labels) {
	        range_input1.start_row++;
	        range_input2.start_row++;
	}

	if (regression_tool (wb, sheet, &range_input1,
			     &range_input2, alpha, &dao))
	        goto dialog_loop;

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}

static void
dialog_average_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box, *vbox;
	static GtkWidget *range_entry, *output_range_entry;
	static GtkWidget *interval_entry;
	static GSList    *output_ops;
	static int       labels = 0;

	data_analysis_output_t  dao;
	int interval;

	char  *text;
	int   selection, output;
	static Range range;

	if (!dialog) {
	        dialog = new_dialog("Moving Average", wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);
		vbox = new_frame("Input:", box);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		range_entry = hbox_pack_label_and_entry
		  ("Input Range:", "", 20, vbox);

		interval_entry = hbox_pack_label_and_entry("Interval:", "3",
							   20, vbox);

		add_check_buttons(vbox, first_row_label_button);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		add_check_buttons(box, standard_errors_button);

		output_range_entry = add_output_frame(box, &output_ops);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (range_entry);

dialog_loop:

	selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	if (selection == 1) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start_col,
			  &range.start_row,
			  &range.end_col,
			  &range.end_row)) {
	        error_in_entry(wb, range_entry, 
			       "You should introduce a valid cell range "
			       "in 'Range Input:'");
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (interval_entry));
	interval = atoi(text);

	if (parse_output(output, sheet, output_range_entry, wb, &dao))
	        goto dialog_loop;

	labels = label_row_flag;
	if (labels) {
	        range.start_row++;
	        range.start_row++;
	}

	if (average_tool (wb, sheet, &range, interval,
			  standard_errors_flag, &dao))
	        goto dialog_loop;

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}


static void
dialog_ranking_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box;
	static GtkWidget *range_entry, *output_range_entry;
	static GSList    *group_ops, *output_ops;
	static int       labels = 0;

	data_analysis_output_t  dao;

	char  *text;
	int   selection;
	static Range range;
	int   i=0, output;

	label_row_flag = labels;

	if (!dialog) {
	        dialog = new_dialog("Rank and Percentile", wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame("Input:", box);

		range_entry = hbox_pack_label_and_entry
		  ("Input Range:", "", 20, box);

		group_ops = add_groupped_by(box);

		add_check_buttons(box, label_button);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (range_entry);

dialog_loop:

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
		goto dialog_loop;
	}

	if (parse_output(output, sheet, output_range_entry, wb, &dao))
	        goto dialog_loop;

	labels = label_row_flag;
	dao.labels_flag = labels;

	if (ranking_tool (wb, sheet, &range, !i, &dao))
	        goto dialog_loop;

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}


static void
dialog_anova_single_factor_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box;
	static GtkWidget *range_entry, *output_range_entry, *alpha_entry;
	static GSList    *group_ops, *output_ops;
	static int       labels = 0;

	data_analysis_output_t  dao;

	char  *text;
	int   selection;
	static Range range;
	int   i=0, output;
	float_t alpha;

	label_row_flag = labels;

	if (!dialog) {
	        dialog = new_dialog("Anova: Single Factor", wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame("Input:", box);

		range_entry = hbox_pack_label_and_entry
		  ("Input Range:", "", 20, box);

		group_ops = add_groupped_by(box);

		alpha_entry = hbox_pack_label_and_entry("Alpha:", "0.95",
							20, box);
		add_check_buttons(box, label_button);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (range_entry);

dialog_loop:

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
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	if (parse_output(output, sheet, output_range_entry, wb, &dao))
	        goto dialog_loop;

	labels = label_row_flag;
	dao.labels_flag = labels;

	if (anova_single_factor_tool (wb, sheet, &range, !i, alpha, &dao))
	        goto dialog_loop;

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}


static void
dialog_anova_two_factor_without_r_tool(Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box;
	static GtkWidget *range_entry, *output_range_entry, *alpha_entry;
	static GSList    *output_ops;
	static int       labels = 0;

	data_analysis_output_t  dao;

	char         *text;
	int          selection;
	static Range range;
	int          output;
	float_t      alpha;

	label_row_flag = labels;

	if (!dialog) {
	        dialog = new_dialog("Anova: Two-Factor Without Replication",
				    wb->toplevel);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame("Input:", box);

		range_entry = hbox_pack_label_and_entry
		  ("Input Range:", "", 20, box);

		alpha_entry = hbox_pack_label_and_entry("Alpha:", "0.95",
							20, box);
		add_check_buttons(box, label_button);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (range_entry);

dialog_loop:

	selection = gnome_dialog_run (GNOME_DIALOG (dialog));
	if (selection == 1) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start_col,
			  &range.start_row,
			  &range.end_col,
			  &range.end_row)) {
	        error_in_entry(wb, range_entry, 
			       "You should introduce a valid cell range "
			       "in 'Range:'");
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	if (parse_output(output, sheet, output_range_entry, wb, &dao))
	        goto dialog_loop;

	labels = label_row_flag;
	dao.labels_flag = labels;

	if (anova_two_factor_without_r_tool (wb, sheet, &range, alpha, &dao))
	        goto dialog_loop;

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
	static GtkWidget *dialog, *scrolled_win;
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

		scrolled_win = gtk_scrolled_window_new (NULL, NULL);
		gtk_container_set_border_width (GTK_CONTAINER (scrolled_win),
						5);
		gtk_widget_set_usize (scrolled_win, 330, 160);
		gtk_box_pack_start (GTK_BOX (box), scrolled_win,
				    TRUE, TRUE, 0);
		gtk_scrolled_window_set_policy
		  (GTK_SCROLLED_WINDOW (scrolled_win),
		   GTK_POLICY_AUTOMATIC,
		   GTK_POLICY_AUTOMATIC);

		tool_list = gtk_clist_new (1);
		gtk_clist_set_selection_mode (GTK_CLIST (tool_list),
					      GTK_SELECTION_SINGLE);
		gtk_scrolled_window_add_with_viewport
		  (GTK_SCROLLED_WINDOW (scrolled_win), tool_list);

		for (i=0; tools[i].fun; i++)
		        gtk_clist_append (GTK_CLIST (tool_list),
					  (char **) &tools[i].name);
		
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
