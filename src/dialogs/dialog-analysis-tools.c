/*
 * dialog-analysis-tools.c:
 *
 * Author:
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 *
 * (C) Copyright 2000 by Jukka-Pekka Iivonen <iivonen@iki.fi>
 */

#include <config.h>
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>
#include <string.h>
#include "gnumeric.h"
#include "workbook.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "gui-util.h"
#include "utils-dialog.h"
#include "dialogs.h"
#include "parse-util.h"
#include "utils-dialog.h"
#include "tools.h"
#include "ranges.h"
#include "selection.h"

static int dialog_correlation_tool         (WorkbookControlGUI *wbcg, Sheet *sheet);
static int dialog_covariance_tool          (WorkbookControlGUI *wbcg, Sheet *sheet);
static int dialog_descriptive_stat_tool    (WorkbookControlGUI *wbcg, Sheet *sheet);
static int dialog_ztest_tool               (WorkbookControlGUI *wbcg, Sheet *sheet);
static int dialog_ranking_tool      	   (WorkbookControlGUI *wbcg, Sheet *sheet);
static int dialog_sampling_tool     	   (WorkbookControlGUI *wbcg, Sheet *sheet);
static int dialog_ttest_paired_tool 	   (WorkbookControlGUI *wbcg, Sheet *sheet);
static int dialog_ttest_eq_tool     	   (WorkbookControlGUI *wbcg, Sheet *sheet);
static int dialog_ttest_neq_tool    	   (WorkbookControlGUI *wbcg, Sheet *sheet);
static int dialog_ftest_tool        	   (WorkbookControlGUI *wbcg, Sheet *sheet);
static int dialog_average_tool      	   (WorkbookControlGUI *wbcg, Sheet *sheet);
static int dialog_fourier_tool      	   (WorkbookControlGUI *wbcg, Sheet *sheet);
static int dialog_histogram_tool      	   (WorkbookControlGUI *wbcg, Sheet *sheet);
static int dialog_random_tool       	   (WorkbookControlGUI *wbcg, Sheet *sheet);
static int dialog_regression_tool   	   (WorkbookControlGUI *wbcg, Sheet *sheet);
static int dialog_anova_single_factor_tool (WorkbookControlGUI *wbcg, Sheet *sheet);
static int dialog_anova_two_factor_without_r_tool (WorkbookControlGUI *wbcg,
						   Sheet *sheet);
static int dialog_anova_two_factor_with_r_tool    (WorkbookControlGUI *wbcg,
						   Sheet *sheet);


static descriptive_stat_tool_t ds;
static int                     label_row_flag, label_col_flag,  intercept_flag;

typedef int (*tool_fun_ptr_t)(WorkbookControlGUI *wbcg, Sheet *sheet);

typedef struct {
        char const *    name;
        tool_fun_ptr_t  fun;
} tool_list_t;

typedef struct {
        const char    *name;
       	GtkSignalFunc fun;
        gboolean      entry_flag;
        const char    *default_value;
} check_button_t;

typedef struct {
        GtkWidget *dialog;
	GtkWidget *distribution_table;
        GtkWidget *distribution_combo;
	GtkWidget *par1_label, *par1_entry;
	GtkWidget *par2_label, *par2_entry;
	GtkAccelGroup *distribution_accel;
} random_tool_callback_t;

/* Name to show in list and parameter labels for a random distribution */
typedef struct {
	random_distribution_t dist;
	const char *name;
	const char *label1;
	const char *label2;
} DistributionStrs;

static tool_list_t tools[] = {
        { N_("Anova: Single Factor"),
	  dialog_anova_single_factor_tool },
        { N_("Anova: Two-Factor With Replication"),
	  dialog_anova_two_factor_with_r_tool },
        { N_("Anova: Two-Factor Without Replication"),
	  dialog_anova_two_factor_without_r_tool },
        { N_("Correlation"),
	  dialog_correlation_tool },
        { N_("Covariance"),
	  dialog_covariance_tool },
        { N_("Descriptive Statistics"),
	  dialog_descriptive_stat_tool },
        { N_("F-Test: Two-Sample for Variances"),
	  dialog_ftest_tool },
        { N_("Fourier Analysis"),
	  dialog_fourier_tool },
        { N_("Histogram"),
	  dialog_histogram_tool },
        { N_("Moving Average"),
	  dialog_average_tool },
        { N_("Random Number Generation"),
	  dialog_random_tool },
        { N_("Rank and Percentile"),
	  dialog_ranking_tool },
        { N_("Regression"),
	  dialog_regression_tool },
        { N_("Sampling"),
	  dialog_sampling_tool },
        { N_("t-Test: Paired Two Sample for Means"),
	  dialog_ttest_paired_tool },
        { N_("t-Test: Two-Sample Assuming Equal Variances"),
	  dialog_ttest_eq_tool },
        { N_("t-Test: Two-Sample Assuming Unequal Variances"),
	  dialog_ttest_neq_tool },
        { N_("z-Test: Two Sample for Means"),
	  dialog_ztest_tool },
	{ NULL, NULL }
};

/* Distribution strings for Random Number Generator */
static const DistributionStrs distribution_strs[] = {
        { DiscreteDistribution,
	  N_("Discrete"), N_("_Value and probability input range:"), NULL },
        { NormalDistribution,
	  N_("Normal"), N_("_Mean:"), N_("_Standard deviation:") },
     	{ PoissonDistribution,
	  N_("Poisson"), N_("_Lambda:"), NULL },
	{ ExponentialDistribution,
	  N_("Exponential"), N_("_b value:"), NULL },
	{ BinomialDistribution,
	  N_("Binomial"), N_("_p value:"), N_("N_umber of trials") },
	{ NegativeBinomialDistribution,
	  N_("Negative Binomial"), N_("_p value:"),
	  N_("N_umber of failures") },
        { BernoulliDistribution,
	  N_("Bernoulli"), N_("_p value:"), NULL },
        { UniformDistribution,
	  N_("Uniform"), N_("_Lower bound:"),  N_("_Upper bound:") },
        { 0, NULL, NULL, NULL }
};

static void
new_sheet_toggled (GtkWidget *widget, data_analysis_output_type_t *type)
{
        if (GTK_TOGGLE_BUTTON (widget)->active) {
	        *type = NewSheetOutput;
	}
}

static void
new_workbook_toggled (GtkWidget *widget, data_analysis_output_type_t *type)
{
        if (GTK_TOGGLE_BUTTON (widget)->active) {
	        *type = NewWorkbookOutput;
	}
}

static void
range_output_toggled (GtkWidget *widget, data_analysis_output_type_t *type)
{
        if (GTK_TOGGLE_BUTTON (widget)->active) {
	        *type = RangeOutput;
	}
}

static void
focus_on_entry (GtkWidget *widget, GtkWidget *entry)
{
        if (GTK_TOGGLE_BUTTON (widget)->active)
		gtk_widget_grab_focus (entry);
}

static void
columns_toggled (GtkWidget *widget, int *group)
{
        if (GTK_TOGGLE_BUTTON (widget)->active) {
	        *group = 0;
	}
}

static void
rows_toggled (GtkWidget *widget, int *group)
{
        if (GTK_TOGGLE_BUTTON (widget)->active) {
	        *group = 1;
	}
}

static gboolean
output_range_selected (GtkWidget *widget, GdkEventFocus   *event,
		 GtkWidget *output_range_button)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (output_range_button),
				      TRUE);
	return FALSE;
}

static int
set_output_option_signals (GladeXML *gui, data_analysis_output_t *dao, const char *n)
{
	GtkWidget *radiobutton;
	GtkWidget *entry;
	char      buf[256];

	sprintf(buf, "%s_radiobutton3", n);
	radiobutton = glade_xml_get_widget (gui, buf);
	if (!radiobutton) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 1;
        }
	gtk_signal_connect (GTK_OBJECT (radiobutton),   "toggled",
			    GTK_SIGNAL_FUNC (new_sheet_toggled),
			    &dao->type);
	sprintf(buf, "%s_radiobutton4", n);
	radiobutton = glade_xml_get_widget (gui, buf);
	if (!radiobutton) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 1;
        }
	gtk_signal_connect (GTK_OBJECT (radiobutton),   "toggled",
			    GTK_SIGNAL_FUNC (new_workbook_toggled),
			    &dao->type);
	sprintf(buf, "%s_radiobutton5", n);
	radiobutton = glade_xml_get_widget (gui, buf);
	if (!radiobutton) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 1;
        }
	gtk_signal_connect (GTK_OBJECT (radiobutton),   "toggled",
			    GTK_SIGNAL_FUNC (range_output_toggled),
			    &dao->type);

	sprintf(buf, "%s_output_range_entry", n);
	entry = glade_xml_get_widget (gui, buf);
	if (!entry) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 1;
        }
	gtk_signal_connect (GTK_OBJECT (radiobutton),   "toggled",
			    GTK_SIGNAL_FUNC (focus_on_entry),
			    entry);
	gtk_signal_connect (GTK_OBJECT (entry), "focus_in_event",
			    GTK_SIGNAL_FUNC (output_range_selected),
			    radiobutton);
	return 0;
}

static int
set_group_option_signals (GladeXML *gui, int *group, const char *n)
{
	GtkWidget *radiobutton;
	char      buf[256];

	sprintf(buf, "%s_radiobutton1", n);
	radiobutton = glade_xml_get_widget (gui, buf);
	if (!radiobutton) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 1;
        }
	gtk_signal_connect (GTK_OBJECT (radiobutton),   "toggled",
			    GTK_SIGNAL_FUNC (columns_toggled),
			    group);
	sprintf(buf, "%s_radiobutton2", n);
	radiobutton = glade_xml_get_widget (gui, buf);
	if (!radiobutton) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 1;
        }
	gtk_signal_connect (GTK_OBJECT (radiobutton),   "toggled",
			    GTK_SIGNAL_FUNC (rows_toggled),
			    group);

	return 0;
}

static void
checkbutton_toggled (GtkWidget *widget, gboolean *flag)
{
        *flag = GTK_TOGGLE_BUTTON (widget)->active;
}

static void
summary_stat_signal_fun ()
{
        ds.summary_statistics = !ds.summary_statistics;
}

static void
confidence_signal_fun ()
{
        ds.confidence_level = !ds.confidence_level;
}

static void
kth_largest_signal_fun ()
{
        ds.kth_largest = !ds.kth_largest;
}

static void
kth_smallest_signal_fun ()
{
        ds.kth_smallest = !ds.kth_smallest;
}

static void
first_row_label_signal_fun ()
{
        label_row_flag = !label_row_flag;
}

static void
first_col_label_signal_fun ()
{
        label_col_flag = !label_col_flag;
}

static void
force_intercept_zero_signal_fun ()
{
	intercept_flag = !intercept_flag;
}

static check_button_t desc_stat_buttons[] = {
        { N_("Summary Statistics"), summary_stat_signal_fun, FALSE,
	  "" },
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
	  "" },
        { NULL, NULL }
};

static check_button_t first_col_label_button[] = {
        { N_("Labels in First Column"), first_col_label_signal_fun, FALSE,
	  "" },
        { NULL, NULL }
};

static check_button_t force_intercept_zero_button[] = {
	{ N_("Force Intercept to Be Zero"), force_intercept_zero_signal_fun,
	  FALSE, ""},
	{ NULL, NULL }
};


static int selected_row;

/* Parses text specifying ranges into columns, sorting from left to right.
For example, the text "A5:B30,J10:J15,C1:C5" would be returned in **ranges
as the equivalent of running parse_ranges on "A5:A30" "B5:B30" "J10:J15" and
"C1:C5" in that order. */

/* FIXME: A clear candidate for a rewrite using range_list_parse.  */

static int
parse_multiple_ranges (const char *text, Range **ranges, int *dim)
{
        char *buf, *buf0;
        char *p;
	int i, last, curdim;

	i = strlen (text);
	buf = buf0 = g_new (char, i + 2);
	strcpy (buf, text);
	buf[i + 1] = 0;  /* Double terminator.  */

	curdim = 0;
	last = 0;
	*ranges = NULL;

	for (i = last; buf[i] != ',' && buf[i] != '\0'; i++)
		/* Nothing */;
	while (buf[last] != '\0') {
		Range *newranges;
		int j;
		int start_col, start_row, end_col, end_row;

		buf[i] = '\0';
		p = strchr (buf + last, ':');
		if (p == NULL)
	        	goto failure;
		*p = '\0';
		if (!parse_cell_name (buf + last, &start_col, &start_row, TRUE, NULL))
	        	goto failure;
		if (!parse_cell_name (p + 1, &end_col, &end_row, TRUE, NULL))
	        	goto failure;
		newranges = g_new (Range, curdim + end_col - start_col + 1);
		for (j = 0; j < curdim; j++)
			newranges[j] = (*ranges)[j];
		for (j = 0; j < (end_col - start_col + 1); j++) {
			/* Just want single columns */
			newranges[curdim + j].start.col = start_col + j;
			newranges[curdim + j].end.col = start_col + j;
			newranges[curdim + j].start.row = start_row;
			newranges[curdim + j].end.row = end_row;
		}
		curdim += (end_col - start_col + 1);
		if (*ranges) g_free (*ranges);
		*ranges = newranges;
		last = i + 1;
		for (i = last; buf[i] != ',' && buf[i] != '\0'; i++)
			/* Nothing */;
	}
	*dim = curdim;
	g_free (buf0);
	return 1;
failure:
	g_free (*ranges);
	g_free (buf0);
	return 0;
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
new_dialog (const char *name)
{
        GtkWidget *dialog;

        dialog = gnome_dialog_new (_(name),
				   GNOME_STOCK_BUTTON_OK,
				   GNOME_STOCK_BUTTON_CANCEL,
				   NULL);

	gnome_dialog_close_hides (GNOME_DIALOG (dialog), TRUE);
	gnome_dialog_set_default (GNOME_DIALOG(dialog), GNOME_OK);

	return dialog;
}

static GtkWidget *
new_frame (const char *name, GtkWidget *target_box)
{
        GtkWidget *frame, *box;

	frame = gtk_frame_new (name);
	box = gtk_vbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (frame), box);
	gtk_box_pack_start (GTK_BOX (target_box), frame, FALSE, FALSE, 0);

	return box;
}

static void
error_in_entry (WorkbookControlGUI *wbcg, GtkWidget *entry, const char *err_str)
{
        gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR, err_str);

	gtk_widget_grab_focus (entry);
	gtk_entry_set_position (GTK_ENTRY (entry), 0);
	gtk_entry_select_region (GTK_ENTRY (entry), 0,
				 GTK_ENTRY(entry)->text_length);
}

static void
add_check_buttons (GtkWidget *box, check_button_t *cbs)
{
	static gboolean do_transpose = FALSE;
        GtkWidget *button;
	GnomeDialog *dialog;
	int       i;

	dialog = GNOME_DIALOG (gtk_widget_get_toplevel (box));
	for (i = 0; cbs[i].name; i++) {
	        GtkWidget *hbox, *entry;

		hbox = gtk_hbox_new (FALSE, 0);
	        button = gtk_check_button_new_with_label (_(cbs[i].name));
		gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
		if (cbs[i].entry_flag) {
		        entry = gnumeric_dialog_entry_new_with_max_length (dialog, 20);
			gtk_entry_set_text (GTK_ENTRY (entry),
					    cbs[i].default_value);
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
parse_output (WorkbookControlGUI *wbcg, Sheet *sheet, int output,
	      GtkWidget *entry, data_analysis_output_t *dao)
{
        char  *text;
	Range range;

	text = gtk_entry_get_text (GTK_ENTRY (entry));
	if (output == 2 &&
	    !parse_range (text, &range.start.col, &range.start.row,
			  &range.end.col, &range.end.row)) {
	        error_in_entry (wbcg, entry,
				_("You should introduce a valid cell range "
				  "in 'Input Range:'"));
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
		dao->start_col = range.start.col;
		dao->start_row = range.start.row;
		dao->cols = range.end.col-range.start.col+1;
		dao->rows = range.end.row-range.start.row+1;
		dao->sheet = sheet;
		break;
	}

	return 0;
}

static GtkWidget *
add_output_frame (GtkWidget *box, GSList **output_ops)
{
        GtkWidget *r, *hbox, *output_range_entry;

        box = new_frame(_("Output options:"), box);
	*output_ops = NULL;
	r = gtk_radio_button_new_with_label(*output_ops, _("New Sheet"));
	*output_ops = GTK_RADIO_BUTTON (r)->group;
	gtk_box_pack_start_defaults (GTK_BOX (box), r);
	/* hbox = gtk_hbox_new (FALSE, 0); */
	r = gtk_radio_button_new_with_label(*output_ops, _("New Workbook"));
	*output_ops = GTK_RADIO_BUTTON (r)->group;
	gtk_box_pack_start_defaults (GTK_BOX (box), r);
	hbox = gtk_hbox_new (FALSE, 0);
	r = gtk_radio_button_new_with_label(*output_ops,
					    _("Output Range:"));
	*output_ops = GTK_RADIO_BUTTON (r)->group;
	gtk_box_pack_start_defaults (GTK_BOX (hbox), r);
	output_range_entry = gnumeric_dialog_entry_new_with_max_length
		(GNOME_DIALOG (gtk_widget_get_toplevel (box)), 20);
	gtk_box_pack_start_defaults (GTK_BOX (hbox),
				     output_range_entry);
	gtk_box_pack_start_defaults (GTK_BOX (box), hbox);
	gtk_signal_connect
		(GTK_OBJECT (output_range_entry),
		 "focus_in_event", GTK_SIGNAL_FUNC (output_range_selected),
		 r);

	return output_range_entry;
}

static GSList *
add_groupped_by (GtkWidget *box)
{
        GtkWidget *r, *groupped_label, *group_box, *hbox;
	GSList    *group_ops;
	int       i;

	group_ops = NULL;
	groupped_label = gtk_label_new (_("Grouped By:"));
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


/*************************************************************************
 *
 * Dialogs for the tools.
 */

static int
dialog_correlation_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GladeXML  *gui;
	GtkWidget *range_entry;
	GtkWidget *dialog;
	GtkWidget *checkbutton;
	GtkWidget *output_range_entry;

	data_analysis_output_t dao;

	gboolean labels = FALSE;
	char     *text;
	int      group, selection, x1, x2, y1, y2;
	Range    range;

	gui = gnumeric_glade_xml_new (wbcg, "analysis-tools.glade");
        if (gui == NULL)
                return 0;

	dao.type = NewSheetOutput;
	group    = 0;

	dialog = glade_xml_get_widget (gui, "Correlation");
        range_entry = glade_xml_get_widget (gui, "corr_entry1");
	checkbutton = glade_xml_get_widget (gui, "corr_checkbutton");
	output_range_entry = glade_xml_get_widget (gui,
						   "corr_output_range_entry");

        if (!dialog || !range_entry || !output_range_entry || !checkbutton) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 0;
        }

	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (output_range_entry));

	if (set_group_option_signals (gui, &group, "corr") ||
	    set_output_option_signals (gui, &dao, "corr"))
	        return 0;

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);

        gtk_widget_grab_focus (range_entry);

correlation_dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return 1;
	}

	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return 1;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry (wbcg, range_entry,
				_("You should introduce a valid cell range "
				  "in 'Input Range:'"));
		goto correlation_dialog_loop;
	}

	dao.labels_flag = labels;

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry (wbcg, output_range_entry,
					_("You should introduce a valid cell "
					  "range in 'Output Range:'"));
			goto correlation_dialog_loop;
		} else {
		        dao.start_col = x1;
		        dao.start_row = y1;
			dao.cols = x2-x1+1;
			dao.rows = y2-y1+1;
			dao.sheet = sheet;
		}
	}

	switch (correlation_tool (WORKBOOK_CONTROL (wbcg), sheet,
				  &range, !group, &dao)) {
	case 0: break;
	case 1: error_in_entry (wbcg, range_entry,
				_("Please do not include any non-numeric (or empty)"
				  " data."));
	goto correlation_dialog_loop;
	default: goto correlation_dialog_loop;
	}

	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));

	return 0;
}

static int
dialog_covariance_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GladeXML  *gui;
	GtkWidget *range_entry;
	GtkWidget *dialog;
	GtkWidget *checkbutton;
	GtkWidget *output_range_entry;

	data_analysis_output_t dao;

	gboolean labels = FALSE;
	char     *text;
	int      group, selection, x1, x2, y1, y2;
	Range    range;

	gui = gnumeric_glade_xml_new (wbcg,  "analysis-tools.glade");
        if (gui == NULL)
                return 0;

	dao.type = NewSheetOutput;
	group    = 0;

	dialog = glade_xml_get_widget (gui, "Covariance");
        range_entry = glade_xml_get_widget (gui, "cov_entry1");
	checkbutton = glade_xml_get_widget (gui, "cov_checkbutton");
	output_range_entry
		= glade_xml_get_widget (gui, "cov_output_range_entry");

        if (!dialog || !range_entry || !output_range_entry || !checkbutton) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 0;
        }
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (output_range_entry));

	if (set_group_option_signals (gui, &group, "cov") ||
	    set_output_option_signals (gui, &dao, "cov"))
	        return 0;

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);

        gtk_widget_grab_focus (range_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return 1;
	}

	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return 1;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry (wbcg, range_entry,
				_("You should introduce a valid cell range "
				  "in 'Range:'"));
		goto dialog_loop;
	}

	dao.labels_flag = labels;

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry (wbcg, output_range_entry,
					_("You should introduce a valid cell "
					  "range in 'Output Range:'"));
			goto dialog_loop;
		} else {
		        dao.start_col = x1;
		        dao.start_row = y1;
			dao.cols = x2-x1+1;
			dao.rows = y2-y1+1;
			dao.sheet = sheet;
		}
	}

	switch (covariance_tool (WORKBOOK_CONTROL (wbcg), sheet,
				  &range, !group, &dao)) {
	case 0: break;
	case 1: error_in_entry (wbcg, range_entry,
				_("Please do not include any non-numeric (or empty)"
				  " data."));
	goto dialog_loop;
	default: goto dialog_loop;
	}

	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));

	return 0;
}


static int
dialog_sampling_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
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
	        dialog = new_dialog(_("Sampling"));

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame(_("Input:"), box);

		range_entry = hbox_pack_label_and_entry
		  (dialog, box, _("Input Range:"), "", 20);

		sampling_label = gtk_label_new (_("Sampling Method:"));

		gtk_box_pack_start_defaults(GTK_BOX (box), sampling_label);

		sampling_box = gtk_vbox_new (FALSE, 0);
		sampling_ops = NULL;
		for (i = 0; sample_method_ops [i]; i++) {
		        GtkWidget *hbox = gtk_hbox_new (FALSE, 0);
			GtkWidget *vbox = gtk_vbox_new (FALSE, 0);
			GtkWidget *label, *r;

			if (i==0)
			        label = gtk_label_new (_("Period:"));
			else
			        label =
				  gtk_label_new (_("Number of Samples:"));
			sampling_entry[i] =  gnumeric_dialog_entry_new_with_max_length
				(GNOME_DIALOG (dialog), 20);
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

		gtk_widget_show_all (GNOME_DIALOG (dialog)->vbox);
	}
        gtk_widget_grab_focus (range_entry);

sampling_dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1)
		return 1;

	if (selection != 0) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return 1;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry (wbcg, range_entry,
				_("You should introduce a valid cell range "
				  "in 'Range:'"));
		goto sampling_dialog_loop;
	}

	i = gtk_radio_group_get_selected(sampling_ops);
	output = gtk_radio_group_get_selected (output_ops);

	if (parse_output (wbcg, sheet, output, output_range_entry, &dao))
	        goto sampling_dialog_loop;

	text = gtk_entry_get_text (GTK_ENTRY (sampling_entry[i]));
	size = atoi(text);

	if (sampling_tool (WORKBOOK_CONTROL (wbcg), sheet,
			   &range, !i, size, &dao))
	        goto sampling_dialog_loop;

	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));

	return 0;
}

static int
dialog_descriptive_stat_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        static GtkWidget *dialog, *box;
	static GtkWidget *range_entry, *output_range_entry;
	static GtkWidget *check_buttons;
	static GSList    *group_ops, *output_ops;
	static int       row_labels = 0;
	static int       col_labels = 0;

	data_analysis_output_t  dao;

	char  *text;
	int   selection, output;
	static Range range;
	int   i=0;

	label_row_flag = col_labels;
	label_col_flag = row_labels;

	if (!dialog) {
	        dialog = new_dialog(_("Descriptive Statistics"));

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame(_("Input:"), box);

		range_entry = hbox_pack_label_and_entry
		  (dialog, box, _("Input Range:"), "", 20);

		group_ops = add_groupped_by(box);
		add_check_buttons(box, first_row_label_button);
		add_check_buttons(box, first_col_label_button);

		check_buttons = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start (GTK_BOX (GNOME_DIALOG
					     (dialog)->vbox),
				    check_buttons, TRUE, TRUE, 0);
		add_check_buttons(check_buttons, desc_stat_buttons);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

		gtk_widget_show_all (GNOME_DIALOG (dialog)->vbox);
	}
	text = sheet_selection_to_string (sheet, FALSE);
	if (text != NULL) {
		gtk_entry_set_text(GTK_ENTRY (range_entry),text);
		g_free(text);
	}
        gtk_widget_grab_focus (range_entry);

stat_dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1)
		return 1;

	if (selection != 0) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return 1;
	}

	i = gtk_radio_group_get_selected (group_ops);
	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry (wbcg, range_entry,
				_("You should introduce a valid cell range "
				  "in 'Range:'"));
		goto stat_dialog_loop;
	}

	if (parse_output (wbcg, sheet, output, output_range_entry, &dao))
	        goto stat_dialog_loop;

	text = gtk_entry_get_text (GTK_ENTRY (ds.entry[1]));
	ds.c_level = atof(text);
	text = gtk_entry_get_text (GTK_ENTRY (ds.entry[2]));
	ds.k_largest = atoi(text);
	text = gtk_entry_get_text (GTK_ENTRY (ds.entry[3]));
	ds.k_smallest = atoi(text);

	col_labels = label_row_flag;
	row_labels = label_col_flag;

/* Note: we want to include the data labels but remove other labels */
	if (col_labels && i)
	        range.start.row++;
	if (row_labels && !i)
	        range.start.col++;
	dao.labels_flag = ((col_labels && !i) || (row_labels && i));

	if (descriptive_stat_tool (WORKBOOK_CONTROL (wbcg), sheet,
				   &range, !i, &ds, &dao))
	        goto stat_dialog_loop;

	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));

	return 0;
}

static int
dialog_ztest_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        static GtkWidget *dialog, *box;
	static GtkWidget *range1_entry, *range2_entry, *output_range_entry;
	static GtkWidget *known_var1_entry, *known_var2_entry;
	static GtkWidget *mean_diff_entry, *alpha_entry;
	static GSList    *output_ops;
	static int       labels = 0;

	data_analysis_output_t  dao;
	gnum_float mean_diff, alpha, var1, var2;

	char  *text;
	int   selection, output;
	static Range range_input1, range_input2;

	label_row_flag = labels;

	if (!dialog) {
	        dialog = new_dialog(_("z-Test: Two Sample for Means"));

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame(_("Input:"), box);

		range1_entry = hbox_pack_label_and_entry
		  (dialog, box, _("Variable 1 Range:"), "", 20);

		range2_entry = hbox_pack_label_and_entry
		  (dialog, box, _("Variable 2 Range:"), "", 20);

		mean_diff_entry = hbox_pack_label_and_entry
		  (dialog, box, _("Hypothesized Mean Difference:"), "0", 20);

		known_var1_entry = hbox_pack_label_and_entry
		  (dialog, box, _("Variable 1 Variance (known):"), "", 20);

		known_var2_entry = hbox_pack_label_and_entry
		  (dialog, box, _("Variable 2 Variance (known):"), "", 20);

		alpha_entry = hbox_pack_label_and_entry
			(dialog, box, _("Alpha:"), "0.95", 20);
		add_check_buttons(box, first_row_label_button);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

		gtk_widget_show_all (GNOME_DIALOG (dialog)->vbox);
	}
        gtk_widget_grab_focus (range1_entry);

ztest_dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1)
		return 1;

	if (selection != 0) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return 1;
	}

	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start.col,
			  &range_input1.start.row,
			  &range_input1.end.col,
			  &range_input1.end.row)) {
	        error_in_entry (wbcg, range1_entry,
				_("You should introduce a valid cell range "
				  "in 'Variable 1:'"));
		goto ztest_dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start.col,
			  &range_input2.start.row,
			  &range_input2.end.col,
			  &range_input2.end.row)) {
	        error_in_entry (wbcg, range2_entry,
				_("You should introduce a valid cell range "
				  "in 'Variable 2:'"));
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

	if (parse_output (wbcg, sheet, output, output_range_entry, &dao))
	        goto ztest_dialog_loop;

	labels = label_row_flag;
	dao.labels_flag = labels;

	if (ztest_tool (WORKBOOK_CONTROL (wbcg), sheet,
			&range_input1, &range_input2, mean_diff,
			var1, var2, alpha, &dao))
	        goto ztest_dialog_loop;

	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));

	return 0;
}

static int
dialog_ttest_paired_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GladeXML  *gui;
	GtkWidget *range1_entry, *range2_entry;
	GtkWidget *mean_diff_entry;
	GtkWidget *alpha_entry;
	GtkWidget *dialog;
	GtkWidget *checkbutton;
	GtkWidget *output_range_entry;

	data_analysis_output_t dao;

	gboolean labels = FALSE;
	gnum_float  alpha, mean_diff;
	char     *text;
	int      selection, x1, x2, y1, y2;
	Range    range_input1, range_input2;

	gui = gnumeric_glade_xml_new (wbcg,
				      "analysis-tools.glade");
        if (gui == NULL)
                return 0;

	dao.type = NewSheetOutput;

	dialog = glade_xml_get_widget (gui, "TTest1");
        range1_entry = glade_xml_get_widget (gui, "ttest1_entry1");
        range2_entry = glade_xml_get_widget (gui, "ttest1_entry2");
	mean_diff_entry = glade_xml_get_widget (gui, "ttest1_entry3");
	alpha_entry = glade_xml_get_widget (gui, "ttest1_entry4");
	checkbutton = glade_xml_get_widget (gui, "ttest1_checkbutton");
	output_range_entry
		= glade_xml_get_widget (gui, "ttest1_output_range_entry");

        if (!dialog || !range1_entry || !range2_entry || !alpha_entry ||
	    !mean_diff_entry || !output_range_entry || !checkbutton) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 0;
        }
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range1_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range2_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (mean_diff_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (alpha_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (output_range_entry));

	if (set_output_option_signals (gui, &dao, "ttest1"))
	        return 0;

	gtk_entry_set_text (GTK_ENTRY (mean_diff_entry), "0");
	gtk_entry_set_text (GTK_ENTRY (alpha_entry), "0.95");

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);


        gtk_widget_grab_focus (range1_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return 1;
	}

	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return 1;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start.col,
			  &range_input1.start.row,
			  &range_input1.end.col,
			  &range_input1.end.row)) {
	        error_in_entry (wbcg, range1_entry,
				_("You should introduce a valid cell range "
				  "in 'Variable 1:'"));
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start.col,
			  &range_input2.start.row,
			  &range_input2.end.col,
			  &range_input2.end.row)) {
	        error_in_entry (wbcg, range2_entry,
				_("You should introduce a valid cell range "
				  "in 'Variable 2:'"));
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	text = gtk_entry_get_text (GTK_ENTRY (mean_diff_entry));
	mean_diff = atof(text);

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry (wbcg, output_range_entry,
					_("You should introduce a valid cell "
					  "range in 'Output Range:'"));
			goto dialog_loop;
		} else {
		        dao.start_col = x1;
		        dao.start_row = y1;
			dao.cols = x2-x1+1;
			dao.rows = y2-y1+1;
			dao.sheet = sheet;
		}
	}

	dao.labels_flag = labels;

	if (ttest_paired_tool (WORKBOOK_CONTROL (wbcg), sheet,
			       &range_input1, &range_input2,
			       mean_diff, alpha, &dao))
	        goto dialog_loop;

	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));

	return 0;
}

static int
dialog_ttest_eq_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GladeXML  *gui;
	GtkWidget *range1_entry, *range2_entry;
	GtkWidget *mean_diff_entry;
	GtkWidget *alpha_entry;
	GtkWidget *dialog;
	GtkWidget *checkbutton;
	GtkWidget *output_range_entry;

	data_analysis_output_t dao;

	gboolean labels = FALSE;
	gnum_float  alpha, mean_diff;
	char     *text;
	int      selection, x1, x2, y1, y2;
	Range    range_input1, range_input2;

	gui = gnumeric_glade_xml_new (wbcg, "analysis-tools.glade");
        if (gui == NULL)
                return 0;

	dao.type = NewSheetOutput;

	dialog = glade_xml_get_widget (gui, "TTest2");
        range1_entry = glade_xml_get_widget (gui, "ttest2_entry1");
        range2_entry = glade_xml_get_widget (gui, "ttest2_entry2");
	mean_diff_entry = glade_xml_get_widget (gui, "ttest2_entry3");
	alpha_entry = glade_xml_get_widget (gui, "ttest2_entry4");
	checkbutton = glade_xml_get_widget (gui, "ttest2_checkbutton");
	output_range_entry
		= glade_xml_get_widget (gui, "ttest2_output_range_entry");

        if (!dialog || !range1_entry || !range2_entry || !alpha_entry ||
	    !mean_diff_entry || !output_range_entry || !checkbutton) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 0;
        }

	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range1_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range2_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (mean_diff_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (alpha_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (output_range_entry));
	if (set_output_option_signals (gui, &dao, "ttest2"))
	        return 0;

	gtk_entry_set_text (GTK_ENTRY (mean_diff_entry), "0");
	gtk_entry_set_text (GTK_ENTRY (alpha_entry), "0.95");

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);


        gtk_widget_grab_focus (range1_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return 1;
	}

	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return 1;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start.col,
			  &range_input1.start.row,
			  &range_input1.end.col,
			  &range_input1.end.row)) {
	        error_in_entry (wbcg, range1_entry,
				_("You should introduce a valid cell range "
				  "in 'Variable 1:'"));
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start.col,
			  &range_input2.start.row,
			  &range_input2.end.col,
			  &range_input2.end.row)) {
	        error_in_entry (wbcg, range2_entry,
				_("You should introduce a valid cell range "
				  "in 'Variable 2:'"));
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	text = gtk_entry_get_text (GTK_ENTRY (mean_diff_entry));
	mean_diff = atof(text);

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry (wbcg, output_range_entry,
					_("You should introduce a valid cell "
					  "range in 'Output Range:'"));
			goto dialog_loop;
		} else {
		        dao.start_col = x1;
		        dao.start_row = y1;
			dao.cols = x2-x1+1;
			dao.rows = y2-y1+1;
			dao.sheet = sheet;
		}
	}

	dao.labels_flag = labels;

	if (ttest_eq_var_tool (WORKBOOK_CONTROL (wbcg), sheet,
			       &range_input1, &range_input2,
			       mean_diff, alpha, &dao))
	        goto dialog_loop;

	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));

	return 0;
}

static int
dialog_ttest_neq_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GladeXML  *gui;
	GtkWidget *range1_entry, *range2_entry;
	GtkWidget *mean_diff_entry;
	GtkWidget *alpha_entry;
	GtkWidget *dialog;
	GtkWidget *checkbutton;
	GtkWidget *output_range_entry;

	data_analysis_output_t dao;

	gboolean labels = FALSE;
	gnum_float  alpha, mean_diff;
	char     *text;
	int      selection, x1, x2, y1, y2;
	Range    range_input1, range_input2;

	gui = gnumeric_glade_xml_new (wbcg, "analysis-tools.glade");
        if (gui == NULL)
                return 0;

	dao.type = NewSheetOutput;

	dialog = glade_xml_get_widget (gui, "TTest3");
        range1_entry = glade_xml_get_widget (gui, "ttest3_entry1");
        range2_entry = glade_xml_get_widget (gui, "ttest3_entry2");
	mean_diff_entry = glade_xml_get_widget (gui, "ttest3_entry3");
	alpha_entry = glade_xml_get_widget (gui, "ttest3_entry4");
	checkbutton = glade_xml_get_widget (gui, "ttest3_checkbutton");
	output_range_entry
		= glade_xml_get_widget (gui, "ttest3_output_range_entry");

        if (!dialog || !range1_entry || !range2_entry || !alpha_entry ||
	    !mean_diff_entry || !output_range_entry || !checkbutton) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 0;
        }

	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range1_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range2_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (mean_diff_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (alpha_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (output_range_entry));

	if (set_output_option_signals (gui, &dao, "ttest3"))
	        return 0;

	gtk_entry_set_text (GTK_ENTRY (mean_diff_entry), "0");
	gtk_entry_set_text (GTK_ENTRY (alpha_entry), "0.95");

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);


        gtk_widget_grab_focus (range1_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return 1;
	}

	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return 1;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start.col,
			  &range_input1.start.row,
			  &range_input1.end.col,
			  &range_input1.end.row)) {
	        error_in_entry (wbcg, range1_entry,
				_("You should introduce a valid cell range "
				  "in 'Variable 1:'"));
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start.col,
			  &range_input2.start.row,
			  &range_input2.end.col,
			  &range_input2.end.row)) {
	        error_in_entry (wbcg, range2_entry,
				_("You should introduce a valid cell range "
				  "in 'Variable 2:'"));
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	text = gtk_entry_get_text (GTK_ENTRY (mean_diff_entry));
	mean_diff = atof(text);

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry (wbcg, output_range_entry,
					_("You should introduce a valid cell "
					  "range in 'Output Range:'"));
			goto dialog_loop;
		} else {
		        dao.start_col = x1;
		        dao.start_row = y1;
			dao.cols = x2-x1+1;
			dao.rows = y2-y1+1;
			dao.sheet = sheet;
		}
	}

	dao.labels_flag = labels;

	if (ttest_neq_var_tool (WORKBOOK_CONTROL (wbcg), sheet,
				&range_input1, &range_input2,
				mean_diff, alpha, &dao))
	        goto dialog_loop;

	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));

	return 0;
}

static int
dialog_ftest_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GladeXML  *gui;
	GtkWidget *range1_entry, *range2_entry;
	GtkWidget *alpha_entry;
	GtkWidget *dialog;
	GtkWidget *checkbutton;
	GtkWidget *output_range_entry;

	data_analysis_output_t dao;

	gboolean labels = FALSE;
	gnum_float  alpha;
	char     *text;
	int      selection, x1, x2, y1, y2;
	Range    range_input1, range_input2;

	gui = gnumeric_glade_xml_new (wbcg, "analysis-tools.glade");
        if (gui == NULL)
                return 0;

	dao.type = NewSheetOutput;

	dialog = glade_xml_get_widget (gui, "FTest");
        range1_entry = glade_xml_get_widget (gui, "ftest_entry1");
        range2_entry = glade_xml_get_widget (gui, "ftest_entry2");
	alpha_entry = glade_xml_get_widget (gui, "ftest_entry3");
	checkbutton = glade_xml_get_widget (gui, "ftest_checkbutton");
	output_range_entry
		= glade_xml_get_widget (gui, "ftest_output_range_entry");

        if (!dialog || !range1_entry || !range2_entry ||
	    !output_range_entry || !checkbutton) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 0;
        }

	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range1_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range2_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (alpha_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (output_range_entry));

	if (set_output_option_signals (gui, &dao, "ftest"))
	        return 0;

	gtk_entry_set_text (GTK_ENTRY (alpha_entry), "0.95");

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);


        gtk_widget_grab_focus (range1_entry);

ftest_dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return 1;
	}

	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return 1;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start.col,
			  &range_input1.start.row,
			  &range_input1.end.col,
			  &range_input1.end.row)) {
	        error_in_entry (wbcg, range1_entry,
				_("You should introduce a valid cell range "
				  "in 'Variable 1:'"));
		goto ftest_dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start.col,
			  &range_input2.start.row,
			  &range_input2.end.col,
			  &range_input2.end.row)) {
	        error_in_entry (wbcg, range2_entry,
				_("You should introduce a valid cell range "
				  "in 'Variable 2:'"));
		goto ftest_dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry (wbcg, output_range_entry,
					_("You should introduce a valid cell "
					  "range in 'Output Range:'"));
			goto ftest_dialog_loop;
		} else {
		        dao.start_col = x1;
		        dao.start_row = y1;
			dao.cols = x2-x1+1;
			dao.rows = y2-y1+1;
			dao.sheet = sheet;
		}
	}

	dao.labels_flag = labels;

	if (ftest_tool (WORKBOOK_CONTROL (wbcg), sheet,
			&range_input1, &range_input2, alpha, &dao))
	        goto ftest_dialog_loop;

	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));

	return 0;
}

/*
 * distribution_strs_find
 * @dist  Distribution enum
 *
 * Find the strings record, given distribution enum.
 * Returns pointer to strings record.
 */
static const DistributionStrs *
distribution_strs_find (random_distribution_t dist)
{
	int i;

	for (i = 0; distribution_strs[i].name != NULL; i++)
		if (distribution_strs[i].dist == dist)
			return &distribution_strs[i];

	return &distribution_strs[0];
}

/*
 * distribution_parbox_config
 * @p     Callback data
 * @dist  Distribution
 *
 * Configure parameter widgets given random distribution.
 *
 * Set labels and accelerators, and hide/show entry fields as needed.
 */
static void
distribution_parbox_config (random_tool_callback_t *p,
			    random_distribution_t dist)
{
	guint par1_key = 0, par2_key = 0;
	const DistributionStrs *ds = distribution_strs_find (dist);

	if (p->distribution_accel)
		gtk_window_remove_accel_group (GTK_WINDOW (p->dialog),
					       p->distribution_accel);
	p->distribution_accel = gtk_accel_group_new ();

	par1_key = gtk_label_parse_uline (GTK_LABEL (p->par1_label),
					  _(ds->label1));
	if (par1_key != GDK_VoidSymbol)
		gtk_widget_add_accelerator (p->par1_entry, "grab_focus",
					    p->distribution_accel, par1_key,
					    GDK_MOD1_MASK, 0);
	if (ds->label2) {
		par2_key = gtk_label_parse_uline (GTK_LABEL (p->par2_label),
						  _(ds->label2));
		if (par2_key != GDK_VoidSymbol)
			gtk_widget_add_accelerator
				(p->par2_entry, "grab_focus",
				 p->distribution_accel, par2_key,
				 GDK_MOD1_MASK, 0);
	        gtk_widget_show (p->par2_entry);
	} else {
		gtk_label_set_text (GTK_LABEL (p->par2_label), "");
	        gtk_widget_hide (p->par2_entry);
	}
	gtk_window_add_accel_group (GTK_WINDOW (p->dialog),
				    p->distribution_accel);
}

/*
 * combo_get_distribution
 * @combo  combo widget with distribution list
 *
 * Find from combo the distribution the user selected
 */
static random_distribution_t
combo_get_distribution (GtkWidget *combo)
{
        char *text;
	random_distribution_t ret = UniformDistribution;
	int i;

        text = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(combo)->entry));

	for (i = 0; distribution_strs[i].name != NULL; i++)
		if (strcmp(text, _(distribution_strs[i].name)) == 0)
			ret = distribution_strs[i].dist;

	return ret;
}

/*
 * distribution_callback
 * @widget  Not used
 * @p       Callback data
 *
 * Configure the random distribution parameters widgets for the distribution
 * which was selected.
 */
static void
distribution_callback (GtkWidget *widget, random_tool_callback_t *p)
{
	random_distribution_t dist;

	dist = combo_get_distribution (p->distribution_combo);
	distribution_parbox_config (p, dist);
}

/*
 * dialog_random_realized
 * @widget  unused
 * @p       callback data
 *
 * Make initial geometry of distribution table permanent.
 *
 * The dialog is constructed with the distribution_table containing the widgets
 * which need the most space. At construction time, we do not know how large
 * the distribution_table needs to be, but we do know when the dialog is
 * realized. This callback for "realized" makes this size the user specified
 * size so that the table will not shrink when we later change label texts and
 * hide/show widgets.
 */
static void
dialog_random_realized (GtkWidget *widget, random_tool_callback_t *p)
{
	GtkWidget *t = p->distribution_table;
	GtkWidget *l = p->par1_label;

	gtk_widget_set_usize (t, t->allocation.width, t->allocation.height);
	gtk_widget_set_usize (l, l->allocation.width, l->allocation.height);
	distribution_callback (widget, p);
}

/*
 * dialog_random_tool
 * @wbcg:   WorkbookControlGUI
 * @sheet: Sheet
 *
 * Display the random number dialog and get user choices.
 */
static int
dialog_random_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GladeXML  *gui;
        GtkWidget *dialog, *distribution_combo, *distribution_table;
        GtkWidget *par1_label, *par1_entry;
        GtkWidget *par2_label, *par2_entry;
	GtkWidget *vars_entry, *count_entry, *output_range_entry;
	GList     *distribution_type_strs = NULL;
	const DistributionStrs *ds;

	int                     vars, count;
	random_tool_t           param;
	data_analysis_output_t  dao;

	static random_distribution_t distribution = DiscreteDistribution;
	random_tool_callback_t callback_data;

	char  *text;
	int   selection, x1, x2, y1, y2;
	int   i, dist_str_no;

	gui = gnumeric_glade_xml_new (wbcg, "analysis-tools.glade");
        if (gui == NULL)
                return 0;

	dao.type = NewSheetOutput;

	dialog = glade_xml_get_widget (gui, "Random");
	distribution_table = glade_xml_get_widget (gui, "distribution_table");
	distribution_combo = glade_xml_get_widget (gui, "distribution_combo");
	par1_entry = glade_xml_get_widget (gui, "par1_entry");
	par1_label = glade_xml_get_widget (gui, "par1_label");
	par2_label = glade_xml_get_widget (gui, "par2_label");
	par2_entry = glade_xml_get_widget (gui, "par2_entry");
	vars_entry = glade_xml_get_widget (gui, "vars_entry");
	count_entry = glade_xml_get_widget (gui, "count_entry");
	output_range_entry
		= glade_xml_get_widget (gui, "random_output_range_entry");

        if (!dialog || !distribution_combo || !distribution_table ||
	    !par1_label || !par1_entry || !par2_label || !par2_entry) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 0;
        }

	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (par1_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (par2_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (vars_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (count_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (output_range_entry));

	if (set_output_option_signals (gui, &dao, "random") != 0)
	        return 0;

	for (i = 0, dist_str_no = 0; distribution_strs[i].name != NULL; i++) {
		distribution_type_strs
			= g_list_append (distribution_type_strs,
					 (gpointer) _(distribution_strs[i].name));
		if (distribution_strs[i].dist == distribution)
			dist_str_no = i;
	}
	gtk_combo_set_popdown_strings (GTK_COMBO (distribution_combo),
				       distribution_type_strs);
	gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (distribution_combo)->entry),
			   _(distribution_strs[dist_str_no].name));

	ds = distribution_strs_find (DiscreteDistribution);
	(void) gtk_label_parse_uline (GTK_LABEL (par1_label), _(ds->label1));

	callback_data.dialog = dialog;
	callback_data.distribution_table = distribution_table;
	callback_data.distribution_combo = distribution_combo;
	callback_data.par1_entry = par1_entry;
	callback_data.par1_label = par1_label;
	callback_data.par2_label = par2_label;
	callback_data.par2_entry = par2_entry;
	callback_data.distribution_accel = NULL;

	gtk_signal_connect (GTK_OBJECT(GTK_COMBO(distribution_combo)->entry),
			    "changed", GTK_SIGNAL_FUNC (distribution_callback),
			    &callback_data);

	gtk_signal_connect (GTK_OBJECT (dialog), "realize",
			    GTK_SIGNAL_FUNC (dialog_random_realized),
			    &callback_data);

random_dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1)
		return 1;

	if (selection != 0) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return 1;
	}

	text = gtk_entry_get_text (GTK_ENTRY (vars_entry));
	vars = atoi(text);

	text = gtk_entry_get_text (GTK_ENTRY (count_entry));
	count = atoi(text);

	distribution = combo_get_distribution (distribution_combo);
	switch (distribution) {
	case NormalDistribution:
		text = gtk_entry_get_text (GTK_ENTRY (par1_entry));
		param.normal.mean = atof(text);
		text = gtk_entry_get_text (GTK_ENTRY (par2_entry));
		param.normal.stdev = atof(text);
		break;
	case BernoulliDistribution:
		text = gtk_entry_get_text (GTK_ENTRY (par1_entry));
		param.bernoulli.p = atof(text);
		break;
	case PoissonDistribution:
		text = gtk_entry_get_text (GTK_ENTRY (par1_entry));
		param.poisson.lambda = atof(text);
		break;
	case ExponentialDistribution:
		text = gtk_entry_get_text (GTK_ENTRY (par1_entry));
		param.exponential.b = atof(text);
		break;
	case BinomialDistribution:
		text = gtk_entry_get_text (GTK_ENTRY (par1_entry));
		param.binomial.p = atof(text);
		text = gtk_entry_get_text (GTK_ENTRY (par2_entry));
		param.binomial.trials = atoi(text);
		break;
	case NegativeBinomialDistribution:
		text = gtk_entry_get_text (GTK_ENTRY (par1_entry));
		param.negbinom.p = atof(text);
		text = gtk_entry_get_text (GTK_ENTRY (par2_entry));
		param.negbinom.f = atoi(text);
		break;
	case DiscreteDistribution:
		text = gtk_entry_get_text (GTK_ENTRY (par1_entry));
		if (!parse_range (text, &param.discrete.start_col,
			  &param.discrete.start_row,
			  &param.discrete.end_col,
			  &param.discrete.end_row)) {
		        error_in_entry (wbcg, par1_entry,
					_("You should introduce a valid cell "
					  "range in 'Value and probability input "
					  "Range:'"));
			goto random_dialog_loop;
		}
		break;
	case UniformDistribution:
	default:
		text = gtk_entry_get_text (GTK_ENTRY (par1_entry));
		param.uniform.lower_limit = atof(text);
		text = gtk_entry_get_text (GTK_ENTRY (par2_entry));
		param.uniform.upper_limit = atof(text);
		break;
	}

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry (wbcg, output_range_entry,
					_("You should introduce a valid cell "
					  "range in 'Output Range:'"));
			goto random_dialog_loop;
		} else {
		        dao.start_col = x1;
		        dao.start_row = y1;
			dao.cols = x2-x1+1;
			dao.rows = y2-y1+1;
			dao.sheet = sheet;
		}
	}

	if (random_tool (WORKBOOK_CONTROL (wbcg), sheet,
			 vars, count, distribution, &param, &dao))
	        goto random_dialog_loop;

	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);

	if (callback_data.distribution_accel)
		gtk_accel_group_unref (callback_data.distribution_accel);
	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));
	g_list_free (distribution_type_strs);

	return 0;
}

static int
dialog_regression_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        static GtkWidget *dialog, *box, *vbox;
	static GtkWidget *range1_entry, *range2_entry, *output_range_entry;
	static GtkWidget *alpha_entry;
	static GSList    *output_ops;
	int 		 i, xdim;
	static int       labels = 0;
	int		 err = 0;

	data_analysis_output_t  dao;
	gnum_float alpha;

	char  *text;
	int   selection, output;
	static Range range_inputy, *range_inputxs;

	if (!dialog) {
		intercept_flag = 1; /* TODO This would be better using the
				       value in the checkbox instead of this
				       global, but I don't know GTK very well*/

	        dialog = new_dialog(_("Regression"));

		box = gtk_vbox_new (FALSE, 0);
		vbox = new_frame(_("Input:"), box);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		range1_entry = hbox_pack_label_and_entry
		  (dialog, vbox, _("Input Y Range:"), "", 20);

		range2_entry = hbox_pack_label_and_entry
		  (dialog, vbox, _("Input X Range:"), "", 20);

		alpha_entry = hbox_pack_label_and_entry
			(dialog, vbox, _("Confidence Level:"), "0.95", 20);

		add_check_buttons(vbox, first_row_label_button);
		add_check_buttons(vbox, force_intercept_zero_button);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

		gtk_widget_show_all (GNOME_DIALOG (dialog)->vbox);
	}
        gtk_widget_grab_focus (range1_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1)
		return 1;

	if (selection != 0) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return 1;
	}

	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_inputy.start.col,
			  &range_inputy.start.row,
			  &range_inputy.end.col,
			  &range_inputy.end.row)) {
	        error_in_entry (wbcg, range1_entry,
				_("You should introduce a valid cell range "
				  "in 'Input X Range'"));
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_multiple_ranges (text, &range_inputxs, &xdim)) {
	        error_in_entry (wbcg, range2_entry,
				_("You should introduce a valid cell range "
				  "in 'Input X Range'"));
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	if (parse_output (wbcg, sheet, output, output_range_entry, &dao))
	        goto dialog_loop;

	labels = label_row_flag;
	if (labels) {
	        range_inputy.start.row++;
		for (i = 0; i < xdim; i++)
	        	range_inputxs[i].start.row++;
	}

	err = regression_tool (WORKBOOK_CONTROL (wbcg), sheet,
			       &range_inputy, range_inputxs, alpha, &dao,
			       intercept_flag, xdim);
	if (err){
		switch (err){
		case 1:
			gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
			      _("There are too few data points to conduct this "
				"regression.\nThere must be at least as many "
				"data points as free variables."));
			break;
		case 2:
			gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
			      _("Two or more of the independent variables "
				"are linearly dependent,\nand the regression "
				"cannot be calculated. Remove one of these\n"
				"variables and try the regression again."));
			break;
		case 3:
			gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
			      _("There must be an equal number of entries "
				"for each variable in the regression."));
			break;
		}
		g_free (range_inputxs);
		goto dialog_loop;
	}
	g_free (range_inputxs);
	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));

	return 0;
}


static int
dialog_average_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GladeXML  *gui;
	GtkWidget *range_entry;
	GtkWidget *dialog;
	GtkWidget *checkbutton;
	GtkWidget *checkbutton2;
	GtkWidget *output_range_entry;
	GtkWidget *interval_entry;

	data_analysis_output_t dao;

	gboolean labels = FALSE;
	gboolean standard_errors_flag = FALSE;
	char     *text;
	int      interval;
	int      selection, x1, x2, y1, y2;
	Range    range;

	gui = gnumeric_glade_xml_new (wbcg, "analysis-tools.glade");
        if (gui == NULL)
                return 0;

	dao.type = NewSheetOutput;

	dialog = glade_xml_get_widget (gui, "MovingAverage");
        range_entry = glade_xml_get_widget (gui, "ma_entry1");
	checkbutton = glade_xml_get_widget (gui, "ma_checkbutton");
	checkbutton2 = glade_xml_get_widget (gui, "ma_checkbutton2");
	interval_entry = glade_xml_get_widget (gui, "ma_entry2");
	output_range_entry
		= glade_xml_get_widget (gui, "ma_output_range_entry");

        if (!dialog || !range_entry || !output_range_entry || !checkbutton ||
	    !interval_entry || !checkbutton2) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 0;
        }

	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (interval_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (output_range_entry));

	if (set_output_option_signals (gui, &dao, "ma"))
	        return 0;

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);
	gtk_signal_connect (GTK_OBJECT (checkbutton2), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled),
			    &standard_errors_flag);
	gtk_entry_set_text (GTK_ENTRY (interval_entry), "3");

        gtk_widget_grab_focus (range_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return 1;
	}

	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return 1;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry (wbcg, range_entry,
				_("You should introduce a valid cell range "
				  "in 'Range:'"));
		goto dialog_loop;
	}

	dao.labels_flag = labels;

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry (wbcg, output_range_entry,
					_("You should introduce a valid cell "
					  "range in 'Output Range:'"));
			goto dialog_loop;
		} else {
		        dao.start_col = x1;
		        dao.start_row = y1;
			dao.cols = x2-x1+1;
			dao.rows = y2-y1+1;
			dao.sheet = sheet;
		}
	}

	text = gtk_entry_get_text (GTK_ENTRY (interval_entry));
	interval = atoi (text);

	if (average_tool (WORKBOOK_CONTROL (wbcg), sheet,
			  &range, interval, standard_errors_flag, &dao))
	        goto dialog_loop;

	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));

	return 0;
}

static int
dialog_fourier_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GladeXML  *gui;
	GtkWidget *range_entry;
	GtkWidget *dialog;
	GtkWidget *checkbutton;
	GtkWidget *checkbutton2;
	GtkWidget *output_range_entry;

	data_analysis_output_t dao;

	gboolean labels = FALSE;
	gboolean standard_errors_flag = FALSE;
	char     *text;
	int      selection, x1, x2, y1, y2;
	Range    range;

	gui = gnumeric_glade_xml_new (wbcg, "analysis-tools.glade");
        if (gui == NULL)
                return 0;

	dao.type = NewSheetOutput;

	dialog = glade_xml_get_widget (gui, "FourierAnalysis");
        range_entry = glade_xml_get_widget (gui, "fa_entry1");
	checkbutton = glade_xml_get_widget (gui, "fa_checkbutton");
	checkbutton2 = glade_xml_get_widget (gui, "fa_checkbutton2");
	output_range_entry
		= glade_xml_get_widget (gui, "fa_output_range_entry");

        if (!dialog || !range_entry || !output_range_entry || !checkbutton ||
	    !checkbutton2) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 0;
        }

	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (output_range_entry));

	if (set_output_option_signals (gui, &dao, "fa"))
	        return 0;

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);
	gtk_signal_connect (GTK_OBJECT (checkbutton2), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled),
			    &standard_errors_flag);

        gtk_widget_grab_focus (range_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return 1;
	}

	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return 1;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry (wbcg, range_entry,
				_("You should introduce a valid cell range "
				  "in 'Range:'"));
		goto dialog_loop;
	}

	dao.labels_flag = labels;

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry (wbcg, output_range_entry,
					_("You should introduce a valid cell "
					  "range in 'Output Range:'"));
			goto dialog_loop;
		} else {
		        dao.start_col = x1;
		        dao.start_row = y1;
			dao.cols = x2-x1+1;
			dao.rows = y2-y1+1;
			dao.sheet = sheet;
		}
	}


	error_in_entry (wbcg, output_range_entry,
			_("Fourier analysis is not implemented yet.  Sorry."));

	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));

	return 0;
}

static int
dialog_histogram_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GladeXML  *gui;
	GtkWidget *range1_entry, *range2_entry;
	GtkWidget *dialog;
	GtkWidget *checkbutton;
	GtkWidget *sorted_cb, *percentage_cb, *chart_cb;
	GtkWidget *output_range_entry;

	data_analysis_output_t dao;

	gboolean labels = FALSE;
	gboolean sorted = FALSE;
	gboolean percentage = FALSE;
	gboolean chart = FALSE;

	char     *text;
	int      selection, x1, x2, y1, y2, err;
	Range    range_input1, range_input2;

	gui = gnumeric_glade_xml_new (wbcg, "analysis-tools.glade");
        if (gui == NULL)
                return 0;

	dao.type = NewSheetOutput;

	dialog = glade_xml_get_widget (gui, "Histogram");
        range1_entry = glade_xml_get_widget (gui, "hist_entry1");
        range2_entry = glade_xml_get_widget (gui, "hist_entry2");
	checkbutton = glade_xml_get_widget (gui, "hist_checkbutton");
	sorted_cb = glade_xml_get_widget (gui, "hist_checkbutton2");
	percentage_cb = glade_xml_get_widget (gui, "hist_checkbutton3");
	chart_cb = glade_xml_get_widget (gui, "hist_checkbutton4");
	output_range_entry
		= glade_xml_get_widget (gui, "hist_output_range_entry");

        if (!dialog || !range1_entry || !range2_entry ||
	    !output_range_entry || !checkbutton || !sorted_cb ||
	    !percentage_cb || !chart_cb) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 0;
        }

	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range1_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range2_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (output_range_entry));

	if (set_output_option_signals (gui, &dao, "hist"))
	        return 0;

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);

	gtk_signal_connect (GTK_OBJECT (sorted_cb), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &sorted);
	gtk_signal_connect (GTK_OBJECT (percentage_cb), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled),
			    &percentage);
	gtk_signal_connect (GTK_OBJECT (chart_cb), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &chart);


        gtk_widget_grab_focus (range1_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return 1;
	}

	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return 1;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start.col,
			  &range_input1.start.row,
			  &range_input1.end.col,
			  &range_input1.end.row)) {
	        error_in_entry (wbcg, range1_entry,
				_("You should introduce a valid cell range "
				  "in 'Input Range:'"));
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start.col,
			  &range_input2.start.row,
			  &range_input2.end.col,
			  &range_input2.end.row)) {
	        error_in_entry (wbcg, range2_entry,
				_("You should introduce a valid cell range "
				  "in 'Bin Range:'"));
		goto dialog_loop;
	}

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry (wbcg, output_range_entry,
					_("You should introduce a valid cell "
					  "range in 'Output Range:'"));
			goto dialog_loop;
		} else {
		        dao.start_col = x1;
		        dao.start_row = y1;
			dao.cols = x2-x1+1;
			dao.rows = y2-y1+1;
			dao.sheet = sheet;
		}
	}

	dao.labels_flag = labels;

	err = histogram_tool (WORKBOOK_CONTROL (wbcg), sheet,
			      &range_input1, &range_input2, labels,
			      sorted, percentage, chart, &dao);

	if (err == 1) {
	        error_in_entry (wbcg, range1_entry,
				_("Given input range contains non-numeric "
				  "data."));
	        goto dialog_loop;
	} else if (err == 2) {
	        error_in_entry (wbcg, range2_entry,
				_("Given bin range contains non-numeric data."));
	        goto dialog_loop;
	}

	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));

	return 0;
}

static int
dialog_ranking_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GladeXML  *gui;
	GtkWidget *range_entry;
	GtkWidget *dialog;
	GtkWidget *checkbutton;
	GtkWidget *output_range_entry;

	data_analysis_output_t dao;

	gboolean labels = FALSE;
	char     *text;
	int      group, selection, x1, x2, y1, y2;
	Range    range;

	gui = gnumeric_glade_xml_new (wbcg, "analysis-tools.glade");
        if (gui == NULL)
                return 0;

	dao.type = NewSheetOutput;
	group    = 0;

	dialog = glade_xml_get_widget (gui, "RankAndPercentile");
        range_entry = glade_xml_get_widget (gui, "rank_entry1");
	checkbutton = glade_xml_get_widget (gui, "rank_checkbutton");
	output_range_entry
		= glade_xml_get_widget (gui, "rank_output_range_entry");

        if (!dialog || !range_entry || !output_range_entry || !checkbutton) {
                printf ("Corrupt file `analysis-tools.glade'\n");
                return 0;
        }

	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (output_range_entry));

	if (set_group_option_signals (gui, &group,  "rank") ||
	    set_output_option_signals (gui, &dao, "rank"))
	        return 0;

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);

        gtk_widget_grab_focus (range_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return 1;
	}

	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return 1;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry (wbcg, range_entry,
				_("You should introduce a valid cell range "
				  "in 'Range:'"));
		goto dialog_loop;
	}

	dao.labels_flag = labels;

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry (wbcg, output_range_entry,
					_("You should introduce a valid cell "
					  "range in 'Output Range:'"));
			goto dialog_loop;
		} else {
		        dao.start_col = x1;
		        dao.start_row = y1;
			dao.cols = x2-x1+1;
			dao.rows = y2-y1+1;
			dao.sheet = sheet;
		}
	}

	if (ranking_tool (WORKBOOK_CONTROL (wbcg), sheet,
			  &range, !group, &dao))
	        goto dialog_loop;

	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));

	return 0;
}


static int
dialog_anova_single_factor_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GladeXML  *gui;
	GtkWidget *range_entry;
	GtkWidget *dialog;
	GtkWidget *checkbutton;
	GtkWidget *output_range_entry;
	GtkWidget *alpha_entry;

	data_analysis_output_t dao;

	gboolean labels = FALSE;
	char     *text;
	gnum_float  alpha;
	int      group, selection, x1, x2, y1, y2;
	Range    range;

	gui = gnumeric_glade_xml_new (wbcg, "analysis-tools.glade");
        if (gui == NULL)
                return 0;

	dao.type = NewSheetOutput;
	group    = 0;

	dialog = glade_xml_get_widget (gui, "Anova1");
        range_entry = glade_xml_get_widget (gui, "anova1_entry1");
	checkbutton = glade_xml_get_widget (gui, "anova1_checkbutton");
	alpha_entry = glade_xml_get_widget (gui, "anova1_entry2");
	output_range_entry
		= glade_xml_get_widget (gui, "anova1_output_range_entry");

        if (!dialog || !range_entry || !output_range_entry || !checkbutton ||
	    !alpha_entry) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 0;
        }

	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (alpha_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (output_range_entry));

	if (set_group_option_signals (gui, &group, "anova1") ||
	    set_output_option_signals (gui, &dao, "anova1"))
	        return 0;

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);
	gtk_entry_set_text (GTK_ENTRY (alpha_entry), "0.95");

        gtk_widget_grab_focus (range_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return 1;
	}

	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return 1;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry (wbcg, range_entry,
				_("You should introduce a valid cell range "
				  "in 'Range:'"));
		goto dialog_loop;
	}

	dao.labels_flag = labels;

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry (wbcg, output_range_entry,
					_("You should introduce a valid cell "
					  "range in 'Output Range:'"));
			goto dialog_loop;
		} else {
		        dao.start_col = x1;
		        dao.start_row = y1;
			dao.cols = x2-x1+1;
			dao.rows = y2-y1+1;
			dao.sheet = sheet;
		}
	}

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof (text);

	if (anova_single_factor_tool (WORKBOOK_CONTROL (wbcg), sheet,
				      &range, !group, alpha, &dao))
	        goto dialog_loop;

	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));

	return 0;
}


static int
dialog_anova_two_factor_without_r_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GladeXML  *gui;
	GtkWidget *range_entry;
	GtkWidget *dialog;
	GtkWidget *checkbutton;
	GtkWidget *output_range_entry;
	GtkWidget *alpha_entry;

	data_analysis_output_t dao;

	gboolean labels = FALSE;
	char     *text;
	gnum_float  alpha;
	int      selection, x1, x2, y1, y2;
	Range    range;

	gui = gnumeric_glade_xml_new (wbcg, "analysis-tools.glade");
        if (gui == NULL)
                return 0;

	dao.type = NewSheetOutput;

	dialog = glade_xml_get_widget (gui, "Anova2");
        range_entry = glade_xml_get_widget (gui, "anova2_entry1");
	checkbutton = glade_xml_get_widget (gui, "anova2_checkbutton");
	alpha_entry = glade_xml_get_widget (gui, "anova2_entry2");
	output_range_entry
		= glade_xml_get_widget (gui, "anova2_output_range_entry");

        if (!dialog || !range_entry || !output_range_entry || !checkbutton ||
	    !alpha_entry) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 0;
        }

	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (alpha_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (output_range_entry));

	if (set_output_option_signals (gui, &dao, "anova2"))
	        return 0;

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);
	gtk_entry_set_text (GTK_ENTRY (alpha_entry), "0.95");

        gtk_widget_grab_focus (range_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return 1;
	}

	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return 1;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry (wbcg, range_entry,
				_("You should introduce a valid cell range "
				  "in 'Range:'"));
		goto dialog_loop;
	}

	dao.labels_flag = labels;

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry (wbcg, output_range_entry,
					_("You should introduce a valid cell "
					  "range in 'Output Range:'"));
			goto dialog_loop;
		} else {
		        dao.start_col = x1;
		        dao.start_row = y1;
			dao.cols = x2-x1+1;
			dao.rows = y2-y1+1;
			dao.sheet = sheet;
		}
	}

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof (text);

	if (anova_two_factor_without_r_tool (WORKBOOK_CONTROL (wbcg), sheet,
					     &range, alpha, &dao))
	        goto dialog_loop;

	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));

	return 0;
}

static int
dialog_anova_two_factor_with_r_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GladeXML  *gui;
	GtkWidget *range_entry;
	GtkWidget *dialog;
	GtkWidget *rows_entry;
	GtkWidget *output_range_entry;
	GtkWidget *alpha_entry;

	data_analysis_output_t dao;

	char     *text;
	gnum_float  alpha;
	int      selection, x1, x2, y1, y2, rows, err;
	Range    range;

	gui = gnumeric_glade_xml_new (wbcg, "analysis-tools.glade");
        if (gui == NULL)
                return 0;

	dao.type = NewSheetOutput;

	dialog = glade_xml_get_widget (gui, "Anova3");
        range_entry = glade_xml_get_widget (gui, "anova3_entry1");
	rows_entry = glade_xml_get_widget (gui, "anova3_entry2");
	alpha_entry = glade_xml_get_widget (gui, "anova3_entry3");
	output_range_entry
		= glade_xml_get_widget (gui, "anova3_output_range_entry");

        if (!dialog || !range_entry || !output_range_entry || !rows_entry ||
	    !alpha_entry) {
                printf ("Corrupt file analysis-tools.glade\n");
                return 0;
        }

	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (range_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (rows_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (alpha_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (output_range_entry));

	if (set_output_option_signals (gui, &dao, "anova3"))
	        return 0;

	gtk_entry_set_text (GTK_ENTRY (alpha_entry), "0.95");

        gtk_widget_grab_focus (range_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return 1;
	}

	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return 1;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry (wbcg, range_entry,
				_("You should introduce a valid cell range "
				  "in 'Range:'"));
		goto dialog_loop;
	}

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry (wbcg, output_range_entry,
					_("You should introduce a valid cell "
					  "range in 'Output Range:'"));
			goto dialog_loop;
		} else {
		        dao.start_col = x1;
		        dao.start_row = y1;
			dao.cols = x2-x1+1;
			dao.rows = y2-y1+1;
			dao.sheet = sheet;
		}
	}

	text = gtk_entry_get_text (GTK_ENTRY (rows_entry));
	rows = atoi (text);

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof (text);

	err = anova_two_factor_with_r_tool (WORKBOOK_CONTROL (wbcg), sheet,
					    &range, rows, alpha, &dao);
	if (err == 1) {
	        error_in_entry (wbcg, rows_entry,
				_("Each sample should contain the same number "
				  "of rows (`Rows per sample:')"));
	        goto dialog_loop;
	} else if (err == 2) {
	        error_in_entry (wbcg, rows_entry,
				_("Given input range contains non-numeric "
				  "data."));
	        goto dialog_loop;
	} else if (err == 3) {
	        error_in_entry (wbcg, range_entry,
				_("The given input range should contain at "
				  "least two columns of data and the "
				  "labels."));
	        goto dialog_loop;
	}

	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));

	return 0;
}

static void
selection_made (GtkWidget *clist, gint row, gint column,
	       GdkEventButton *event, gpointer data)
{
	GtkWidget *dialog;

        selected_row = row;
	/* If the tool is double-clicked we pop up the tool and dismiss
           chooser. */
	if (event && event->type == GDK_2BUTTON_PRESS) {
		dialog = gtk_widget_get_toplevel (GTK_WIDGET (clist));
		gtk_signal_emit_by_name (GTK_OBJECT (dialog), "clicked", 0);
	}
}

void
dialog_data_analysis (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GladeXML  *gui;
	GtkWidget *dialog;
	GtkWidget *tool_list;

	int       i, selection;

 dialog_loop:
	gui = gnumeric_glade_xml_new (wbcg, "analysis-tools.glade");
        if (gui == NULL)
                return;

	dialog = glade_xml_get_widget (gui, "AnalysisTools");
        if (!dialog) {
                printf ("Corrupt file analysis-tools.glade\n");
                return;
        }

        tool_list = glade_xml_get_widget (gui, "clist1");
	gtk_signal_connect (GTK_OBJECT(tool_list), "select_row",
			    GTK_SIGNAL_FUNC(selection_made), NULL);

	for (i=0; tools[i].fun; i++) {
		char *tmp [2];
		tmp[0] = _(tools[i].name);
		tmp[1] = NULL;
	        gtk_clist_append (GTK_CLIST (tool_list), tmp);
	}
	gtk_clist_select_row (GTK_CLIST (tool_list), selected_row, 0);
	gnumeric_clist_moveto (GTK_CLIST (tool_list), selected_row);

	gtk_widget_grab_focus (GTK_WIDGET(tool_list));

	/* Run the dialog */
	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	gtk_object_unref (GTK_OBJECT (gui));

	if (selection == -1)
		return;
	else
		gtk_object_destroy (GTK_OBJECT (dialog));

	if (selection == 0) {
	        g_return_if_fail (tools[selected_row].fun != NULL);
		selection = tools[selected_row].fun (wbcg, sheet);
		if (selection == 1)
		        goto dialog_loop;
	}
}
