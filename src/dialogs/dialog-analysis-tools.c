/*
 * dialog-analysis-tools.c: 
 *
 * Author:
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 *
 * (C) Copyright 2000 by Jukka-Pekka Iivonen <iivonen@iki.fi>
 */

#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include <string.h>
#include "gnumeric.h"
#include "workbook.h"
#include "gnumeric-util.h"
#include "utils-dialog.h"
#include "dialogs.h"
#include "utils.h"
#include "utils-dialog.h"
#include "tools.h"
#include "ranges.h"

static void dialog_correlation_tool         (Workbook *wb, Sheet *sheet);
static void dialog_covariance_tool          (Workbook *wb, Sheet *sheet);
static void dialog_descriptive_stat_tool    (Workbook *wb, Sheet *sheet);
static void dialog_ztest_tool               (Workbook *wb, Sheet *sheet);
static void dialog_ranking_tool      	    (Workbook *wb, Sheet *sheet);
static void dialog_sampling_tool     	    (Workbook *wb, Sheet *sheet);
static void dialog_ttest_paired_tool 	    (Workbook *wb, Sheet *sheet);
static void dialog_ttest_eq_tool     	    (Workbook *wb, Sheet *sheet);
static void dialog_ttest_neq_tool    	    (Workbook *wb, Sheet *sheet);
static void dialog_ftest_tool        	    (Workbook *wb, Sheet *sheet);
static void dialog_average_tool      	    (Workbook *wb, Sheet *sheet);
static void dialog_random_tool       	    (Workbook *wb, Sheet *sheet);
static void dialog_regression_tool   	    (Workbook *wb, Sheet *sheet);
static void dialog_anova_single_factor_tool (Workbook *wb, Sheet *sheet);
static void dialog_anova_two_factor_without_r_tool(Workbook *wb, Sheet *sheet);


static descriptive_stat_tool_t ds;
static int                     label_row_flag, standard_errors_flag,
			       intercept_flag; 
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
        GtkWidget *bernoulli_box, *binomial_box, *negbinom_box;
        GtkWidget *exponential_box;
        GtkWidget *combo;
} random_tool_callback_t;



static tool_list_t tools[] = {
        { { N_("Anova: Single Factor"), NULL },
	  dialog_anova_single_factor_tool },
        { { N_("Anova: Two-Factor Without Replication"), NULL },
	  dialog_anova_two_factor_without_r_tool },
        { { N_("Correlation"), NULL },
	  dialog_correlation_tool },
        { { N_("Covariance"), NULL },
	  dialog_covariance_tool },
        { { N_("Descriptive Statistics"), NULL },
	  dialog_descriptive_stat_tool },
        { { N_("F-Test: Two-Sample for Variances"), NULL }, 
	  dialog_ftest_tool },
        { { N_("Moving Average"), NULL },
	  dialog_average_tool },
        { { N_("Random Number Generation"), NULL },
	  dialog_random_tool },
        { { N_("Rank and Percentile"), NULL },
	  dialog_ranking_tool },
        { { N_("Regression"), NULL },
	  dialog_regression_tool },
        { { N_("Sampling"), NULL },
	  dialog_sampling_tool },
        { { N_("t-Test: Paired Two Sample for Means"), NULL }, 
	  dialog_ttest_paired_tool },
        { { N_("t-Test: Two-Sample Assuming Equal Variances"), NULL }, 
	  dialog_ttest_eq_tool },
        { { N_("t-Test: Two-Sample Assuming Unequal Variances"), NULL }, 
	  dialog_ttest_neq_tool },
        { { N_("z-Test: Two Sample for Means"), NULL },
	  dialog_ztest_tool },
	{ { NULL, NULL }, NULL }
};

/* Distribution strings for Random Number Generator */
static const char *distribution_strs[] = {
        N_("Discrete"),
        N_("Normal"),
     	N_("Poisson"),
	N_("Exponential"),
	N_("Binomial"),
	N_("Negative Binomial"),
        N_("Bernoulli"),
        N_("Uniform"),
        NULL
};

static void
new_sheet_toggled(GtkWidget *widget, data_analysis_output_type_t *type)
{
        if (GTK_TOGGLE_BUTTON (widget)->active) {
	        *type = NewSheetOutput;
	}
}

static void
new_workbook_toggled(GtkWidget *widget, data_analysis_output_type_t *type)
{
        if (GTK_TOGGLE_BUTTON (widget)->active) {
	        *type = NewWorkbookOutput;
	}
}

static void
range_output_toggled(GtkWidget *widget, data_analysis_output_type_t *type)
{
        if (GTK_TOGGLE_BUTTON (widget)->active) {
	        *type = RangeOutput;
	}
}

static void
columns_toggled(GtkWidget *widget, int *group)
{
        if (GTK_TOGGLE_BUTTON (widget)->active) {
	        *group = 0;
	}
}

static void
rows_toggled(GtkWidget *widget, int *group)
{
        if (GTK_TOGGLE_BUTTON (widget)->active) {
	        *group = 1;
	}
}

static int
set_output_option_signals (GladeXML *gui, data_analysis_output_t *dao, char *n)
{
	GtkWidget *radiobutton;
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

	return 0;
}

static int
set_group_option_signals (GladeXML *gui, int *group, char *n)
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
checkbutton_toggled(GtkWidget *widget, gboolean *flag)
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
force_intercept_zero_signal_fun ()
{
	intercept_flag = !intercept_flag;
}

static void
standard_errors_signal_fun ()
{
        standard_errors_flag = !standard_errors_flag;
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

static check_button_t label_button[] = {
        { N_("Labels"), first_row_label_signal_fun, FALSE,
	  "" },
        { NULL, NULL }
};

static check_button_t force_intercept_zero_button[] = {
	{ N_("Force Intercept to Be Zero"), force_intercept_zero_signal_fun, 
	  FALSE, ""},
	{ NULL, NULL }
};

static check_button_t standard_errors_button[] = {
        { N_("Standard Errors"), standard_errors_signal_fun, FALSE,
	  "" },
        { NULL, NULL }
};

static int selected_row;

/* Parses text specifying ranges into columns, sorting from left to right. 
For example, the text "A5:B30,J10:J15,C1:C5" would be returned in **ranges
as the equivalent of running parse_ranges on "A5:A30" "B5:B30" "J10:J15" and 
"C1:C5" in that order. */ 
static int
parse_multiple_ranges (char *text, Range **ranges, int *dim)
{
        char buf[256];
        char *p;
	int i, j, last, curdim;
	int start_col, start_row,
	    end_col, end_row;
	
	strcpy(buf, text);
	buf[255] = '\0';
	curdim = 0;
	last = 0;
	*ranges = NULL;
	for (i = 0; (i < 256) && buf[i] != '\0'; i++);
	buf[i + 1] = '\0'; /* Makes it easier to catch later, though it
			      it limits text entry to 254 useful bytes */
	for (i = last; (buf[i] != ',') && buf[i] != '\0'; i++);
	while (buf[last] != '\0'){
		Range *newranges;
		buf[i] = '\0';
		p = strchr(buf+last, ':');
		if (p == NULL)
	        	goto failure;
		*p = '\0';
		if (!parse_cell_name (buf+last, &start_col, &start_row))
	        	goto failure;
		if (!parse_cell_name (p+1, &end_col, &end_row))
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
		for (i = last; (buf[i] != ',') && buf[i] != '\0'; i++);
	}
	*dim = curdim;
	return 1;
failure:
	g_free (*ranges);
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
new_dialog(char *name)
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
	GnomeDialog *dialog;
	int       i;

	dialog = GNOME_DIALOG (gtk_widget_get_toplevel (box));
	for (i = 0; cbs[i].name; i++) {
	        GtkWidget *hbox, *entry;

		hbox = gtk_hbox_new (FALSE, 0);
	        button = gtk_check_button_new_with_label (cbs[i].name);
		gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
		if (cbs[i].entry_flag) {
		        entry = gnumeric_dialog_entry_new_with_max_length (dialog, 20);
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
	if (output == 2 && !parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry(wb, entry, 
			     _("You should introduce a valid cell range "
			       "in 'Into a Range:'"));
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
add_output_frame(GtkWidget *box, GSList **output_ops)
{
        GtkWidget *r, *hbox, *output_range_entry;

        box = new_frame("Output options:", box);
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

	return output_range_entry;
}

static GSList *
add_groupped_by(GtkWidget *box)
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


static void
dialog_correlation_tool (Workbook *wb, Sheet *sheet)
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

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/analysis-tools.glade", NULL);

        if (!gui) {
                printf ("Could not find analysis-tools.glade\n");
                return;
        }

	dao.type = NewSheetOutput;
	group    = 0;

	dialog = glade_xml_get_widget (gui, "Correlation");
        range_entry = glade_xml_get_widget (gui, "corr_entry1");
	checkbutton = glade_xml_get_widget (gui, "corr_checkbutton");
	output_range_entry = glade_xml_get_widget (gui, "corr_entry2");

        if (!dialog || !range_entry || !output_range_entry || !checkbutton) {
                printf ("Corrupt file analysis-tools.glade\n");
                return;
        }
	if (set_group_option_signals (gui, &group, "corr") ||
	    set_output_option_signals (gui, &dao, "corr"))
	        return;

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);

        gtk_widget_grab_focus (range_entry);

correlation_dialog_loop:

	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return;
	}
	
	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry(wb, range_entry, 
			       _("You should introduce a valid cell range "
			       "in 'Range:'"));
		goto correlation_dialog_loop;
	}

	dao.labels_flag = labels;

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry(wb, output_range_entry, 
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

	if (correlation_tool (wb, sheet, &range, !group, &dao))
	        goto correlation_dialog_loop;

	workbook_focus_sheet(sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));
}

static void
dialog_covariance_tool (Workbook *wb, Sheet *sheet)
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

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/analysis-tools.glade", NULL);

        if (!gui) {
                printf ("Could not find analysis-tools.glade\n");
                return;
        }

	dao.type = NewSheetOutput;
	group    = 0;

	dialog = glade_xml_get_widget (gui, "Covariance");
        range_entry = glade_xml_get_widget (gui, "cov_entry1");
	checkbutton = glade_xml_get_widget (gui, "cov_checkbutton");
	output_range_entry = glade_xml_get_widget (gui, "cov_entry2");

        if (!dialog || !range_entry || !output_range_entry || !checkbutton) {
                printf ("Corrupt file analysis-tools.glade\n");
                return;
        }
	if (set_group_option_signals (gui, &group, "cov") ||
	    set_output_option_signals (gui, &dao, "cov"))
	        return;

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);

        gtk_widget_grab_focus (range_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return;
	}
	
	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry(wb, range_entry, 
			       _("You should introduce a valid cell range "
			       "in 'Range:'"));
		goto dialog_loop;
	}

	dao.labels_flag = labels;

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry(wb, output_range_entry, 
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

	if (covariance_tool (wb, sheet, &range, !group, &dao))
	        goto dialog_loop;

	workbook_focus_sheet(sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));
}


static void
dialog_sampling_tool (Workbook *wb, Sheet *sheet)
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
		  (_("Input Range:"), "", 20, box);

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

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (range_entry);

sampling_dialog_loop:

	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
	if (selection == -1)
		return;
	
	if (selection != 0) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry(wb, range_entry, 
			       _("You should introduce a valid cell range "
			       "in 'Range:'"));
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
dialog_descriptive_stat_tool (Workbook *wb, Sheet *sheet)
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
	        dialog = new_dialog(_("Descriptive Statistics"));

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame(_("Input:"), box);

		range_entry = hbox_pack_label_and_entry
		  (_("Input Range:"), "", 20, box);

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

	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
	if (selection == -1)
		return;
	
	if (selection != 0) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	i = gtk_radio_group_get_selected (group_ops);
	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry(wb, range_entry, 
			       _("You should introduce a valid cell range "
			       "in 'Range:'"));
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
	        range.start.row++;

	if (descriptive_stat_tool(wb, sheet, &range, !i, &ds, &dao))
	        goto stat_dialog_loop;

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}

static void
dialog_ztest_tool (Workbook *wb, Sheet *sheet)
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
	        dialog = new_dialog(_("z-Test: Two Sample for Means"));

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame(_("Input:"), box);

		range1_entry = hbox_pack_label_and_entry
		  (_("Variable 1 Range:"), "", 20, box);

		range2_entry = hbox_pack_label_and_entry
		  (_("Variable 2 Range:"), "", 20, box);

		mean_diff_entry = hbox_pack_label_and_entry
		  (_("Hypothesized Mean Difference:"), "0", 20, box);

		known_var1_entry = hbox_pack_label_and_entry
		  (_("Variable 1 Variance (known):"), "", 20, box);

		known_var2_entry = hbox_pack_label_and_entry
		  (_("Variable 2 Variance (known):"), "", 20, box);

		alpha_entry = hbox_pack_label_and_entry(_("Alpha:"), "0.95",
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

	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
	if (selection == -1)
		return;
	
	if (selection != 0) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start.col,
			  &range_input1.start.row,
			  &range_input1.end.col,
			  &range_input1.end.row)) {
	        error_in_entry(wb, range1_entry, 
			       _("You should introduce a valid cell range "
			       "in 'Variable 1:'"));
		goto ztest_dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start.col,
			  &range_input2.start.row,
			  &range_input2.end.col,
			  &range_input2.end.row)) {
	        error_in_entry(wb, range2_entry, 
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
dialog_ttest_paired_tool (Workbook *wb, Sheet *sheet)
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
	float_t  alpha, mean_diff;
	char     *text;
	int      selection, x1, x2, y1, y2;
	Range    range_input1, range_input2;

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/analysis-tools.glade", NULL);

        if (!gui) {
                printf ("Could not find analysis-tools.glade\n");
                return;
        }

	dao.type = NewSheetOutput;

	dialog = glade_xml_get_widget (gui, "TTest1");
        range1_entry = glade_xml_get_widget (gui, "ttest1_entry1");
        range2_entry = glade_xml_get_widget (gui, "ttest1_entry2");
	mean_diff_entry = glade_xml_get_widget (gui, "ttest1_entry3");
	alpha_entry = glade_xml_get_widget (gui, "ttest1_entry4");
	checkbutton = glade_xml_get_widget (gui, "ttest1_checkbutton");
	output_range_entry = glade_xml_get_widget (gui, "ttest1_entry5");

        if (!dialog || !range1_entry || !range2_entry || !alpha_entry ||
	    !mean_diff_entry || !output_range_entry || !checkbutton) {
                printf ("Corrupt file analysis-tools.glade\n");
                return;
        }
	if (set_output_option_signals (gui, &dao, "ttest1"))
	        return;

	gtk_entry_set_text (GTK_ENTRY (mean_diff_entry), "0");
	gtk_entry_set_text (GTK_ENTRY (alpha_entry), "0.95");

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);


        gtk_widget_grab_focus (range1_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return;
	}
	
	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start.col,
			  &range_input1.start.row,
			  &range_input1.end.col,
			  &range_input1.end.row)) {
	        error_in_entry(wb, range1_entry, 
			       _("You should introduce a valid cell range "
			       "in 'Variable 1:'"));
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start.col,
			  &range_input2.start.row,
			  &range_input2.end.col,
			  &range_input2.end.row)) {
	        error_in_entry(wb, range2_entry, 
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
		        error_in_entry(wb, output_range_entry, 
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

	if (ttest_paired_tool (wb, sheet, &range_input1, &range_input2,
			       mean_diff, alpha, &dao))
	        goto dialog_loop;

	workbook_focus_sheet(sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));
}

static void
dialog_ttest_eq_tool (Workbook *wb, Sheet *sheet)
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
	float_t  alpha, mean_diff;
	char     *text;
	int      selection, x1, x2, y1, y2;
	Range    range_input1, range_input2;

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/analysis-tools.glade", NULL);

        if (!gui) {
                printf ("Could not find analysis-tools.glade\n");
                return;
        }

	dao.type = NewSheetOutput;

	dialog = glade_xml_get_widget (gui, "TTest2");
        range1_entry = glade_xml_get_widget (gui, "ttest2_entry1");
        range2_entry = glade_xml_get_widget (gui, "ttest2_entry2");
	mean_diff_entry = glade_xml_get_widget (gui, "ttest2_entry3");
	alpha_entry = glade_xml_get_widget (gui, "ttest2_entry4");
	checkbutton = glade_xml_get_widget (gui, "ttest2_checkbutton");
	output_range_entry = glade_xml_get_widget (gui, "ttest2_entry5");

        if (!dialog || !range1_entry || !range2_entry || !alpha_entry ||
	    !mean_diff_entry || !output_range_entry || !checkbutton) {
                printf ("Corrupt file analysis-tools.glade\n");
                return;
        }
	if (set_output_option_signals (gui, &dao, "ttest2"))
	        return;

	gtk_entry_set_text (GTK_ENTRY (mean_diff_entry), "0");
	gtk_entry_set_text (GTK_ENTRY (alpha_entry), "0.95");

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);


        gtk_widget_grab_focus (range1_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return;
	}
	
	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start.col,
			  &range_input1.start.row,
			  &range_input1.end.col,
			  &range_input1.end.row)) {
	        error_in_entry(wb, range1_entry, 
			       _("You should introduce a valid cell range "
			       "in 'Variable 1:'"));
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start.col,
			  &range_input2.start.row,
			  &range_input2.end.col,
			  &range_input2.end.row)) {
	        error_in_entry(wb, range2_entry, 
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
		        error_in_entry(wb, output_range_entry, 
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

	if (ttest_eq_var_tool (wb, sheet, &range_input1, &range_input2,
			       mean_diff, alpha, &dao))
	        goto dialog_loop;

	workbook_focus_sheet(sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));
}

static void
dialog_ttest_neq_tool (Workbook *wb, Sheet *sheet)
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
	float_t  alpha, mean_diff;
	char     *text;
	int      selection, x1, x2, y1, y2;
	Range    range_input1, range_input2;

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/analysis-tools.glade", NULL);

        if (!gui) {
                printf ("Could not find analysis-tools.glade\n");
                return;
        }

	dao.type = NewSheetOutput;

	dialog = glade_xml_get_widget (gui, "TTest3");
        range1_entry = glade_xml_get_widget (gui, "ttest3_entry1");
        range2_entry = glade_xml_get_widget (gui, "ttest3_entry2");
	mean_diff_entry = glade_xml_get_widget (gui, "ttest3_entry3");
	alpha_entry = glade_xml_get_widget (gui, "ttest3_entry4");
	checkbutton = glade_xml_get_widget (gui, "ttest3_checkbutton");
	output_range_entry = glade_xml_get_widget (gui, "ttest3_entry5");

        if (!dialog || !range1_entry || !range2_entry || !alpha_entry ||
	    !mean_diff_entry || !output_range_entry || !checkbutton) {
                printf ("Corrupt file analysis-tools.glade\n");
                return;
        }
	if (set_output_option_signals (gui, &dao, "ttest3"))
	        return;

	gtk_entry_set_text (GTK_ENTRY (mean_diff_entry), "0");
	gtk_entry_set_text (GTK_ENTRY (alpha_entry), "0.95");

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);


        gtk_widget_grab_focus (range1_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return;
	}
	
	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start.col,
			  &range_input1.start.row,
			  &range_input1.end.col,
			  &range_input1.end.row)) {
	        error_in_entry(wb, range1_entry, 
			       _("You should introduce a valid cell range "
			       "in 'Variable 1:'"));
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start.col,
			  &range_input2.start.row,
			  &range_input2.end.col,
			  &range_input2.end.row)) {
	        error_in_entry(wb, range2_entry, 
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
		        error_in_entry(wb, output_range_entry, 
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

	if (ttest_neq_var_tool (wb, sheet, &range_input1, &range_input2,
				mean_diff, alpha, &dao))
	        goto dialog_loop;

	workbook_focus_sheet(sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));
}

static void
dialog_ftest_tool (Workbook *wb, Sheet *sheet)
{
        GladeXML  *gui;
	GtkWidget *range1_entry, *range2_entry;
	GtkWidget *alpha_entry;
	GtkWidget *dialog;
	GtkWidget *checkbutton;
	GtkWidget *output_range_entry;

	data_analysis_output_t dao;

	gboolean labels = FALSE;
	float_t  alpha;
	char     *text;
	int      selection, x1, x2, y1, y2;
	Range    range_input1, range_input2;

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/analysis-tools.glade", NULL);

        if (!gui) {
                printf ("Could not find analysis-tools.glade\n");
                return;
        }

	dao.type = NewSheetOutput;

	dialog = glade_xml_get_widget (gui, "FTest");
        range1_entry = glade_xml_get_widget (gui, "ftest_entry1");
        range2_entry = glade_xml_get_widget (gui, "ftest_entry2");
	alpha_entry = glade_xml_get_widget (gui, "ftest_entry3");
	checkbutton = glade_xml_get_widget (gui, "ftest_checkbutton");
	output_range_entry = glade_xml_get_widget (gui, "ftest_entry4");

        if (!dialog || !range1_entry || !range2_entry || 
	    !output_range_entry || !checkbutton) {
                printf ("Corrupt file analysis-tools.glade\n");
                return;
        }
	if (set_output_option_signals (gui, &dao, "ftest"))
	        return;

	gtk_entry_set_text (GTK_ENTRY (alpha_entry), "0.95");

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);


        gtk_widget_grab_focus (range1_entry);

ftest_dialog_loop:

	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return;
	}
	
	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_input1.start.col,
			  &range_input1.start.row,
			  &range_input1.end.col,
			  &range_input1.end.row)) {
	        error_in_entry(wb, range1_entry, 
			       _("You should introduce a valid cell range "
			       "in 'Variable 1:'"));
		goto ftest_dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_range (text, &range_input2.start.col,
			  &range_input2.start.row,
			  &range_input2.end.col,
			  &range_input2.end.row)) {
	        error_in_entry(wb, range2_entry, 
			       _("You should introduce a valid cell range "
			       "in 'Variable 2:'"));
		goto ftest_dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry(wb, output_range_entry, 
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

	if (ftest_tool (wb, sheet, &range_input1, &range_input2, alpha, &dao))
	        goto ftest_dialog_loop;

	workbook_focus_sheet(sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));
}


static void
distribution_callback (GtkWidget *widget, random_tool_callback_t *p)
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
	case PoissonDistribution:
	        gtk_widget_hide (p->poisson_box);
		break;
	case ExponentialDistribution:
	        gtk_widget_hide (p->exponential_box);
		break;
	case BinomialDistribution:
	        gtk_widget_hide (p->binomial_box);
		break;
	case NegativeBinomialDistribution:
	        gtk_widget_hide (p->negbinom_box);
		break;
	default:
	        break;
	}

        text = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(p->combo)->entry));

	if (strcmp(text, _("Uniform")) == 0) {
	        distribution = UniformDistribution;
		gtk_widget_show (p->uniform_box);
	} else if (strcmp(text, _("Bernoulli")) == 0) {
	        distribution = BernoulliDistribution;
		gtk_widget_show (p->bernoulli_box);
	} else if (strcmp(text, _("Normal")) == 0) {
	        distribution = NormalDistribution;
		gtk_widget_show (p->normal_box);
	} else if (strcmp(text, _("Discrete")) == 0) {
	        distribution = DiscreteDistribution;
		gtk_widget_show (p->discrete_box);
	} else if (strcmp(text, _("Exponential")) == 0) {
	        distribution = ExponentialDistribution;
		gtk_widget_show (p->exponential_box);
	} else if (strcmp(text, _("Binomial")) == 0) {
	        distribution = BinomialDistribution;
		gtk_widget_show (p->binomial_box);
	} else if (strcmp(text, _("Negative Binomial")) == 0) {
	        distribution = NegativeBinomialDistribution;
		gtk_widget_show (p->negbinom_box);
	} else if (strcmp(text, _("Poisson")) == 0) {
	        distribution = PoissonDistribution;
		gtk_widget_show (p->poisson_box);
	}
}

static void
dialog_random_tool (Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box, *param_box, *distribution_combo;
	static GtkWidget *vars_entry, *count_entry, *output_range_entry;
	static GtkWidget *discrete_range_entry;
	static GtkWidget *uniform_upper_entry, *uniform_lower_entry;
	static GtkWidget *normal_mean_entry, *normal_stdev_entry;
	static GtkWidget *poisson_lambda_entry;
	static GtkWidget *binomial_p_entry, *binomial_trials_entry;
	static GtkWidget *negbinom_p_entry, *negbinom_f_entry;
	static GtkWidget *exponential_b_entry;
	static GtkWidget *bernoulli_p_entry;

	static GSList    *output_ops;
	static GList     *distribution_type_strs;

	int                     vars, count;
	random_tool_t           param;
	data_analysis_output_t  dao;

	static random_tool_callback_t callback_data;

	char  *text;
	int   selection;
	int   output;

	if (!dialog) {
	        dialog = new_dialog(_("Random Number Generation"));

	        distribution_type_strs =
		  add_strings_to_glist (distribution_strs);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		vars_entry = hbox_pack_label_and_entry
		  (_("Number of Variables:"), "", 20, box);
	
		count_entry = hbox_pack_label_and_entry
		  (_("Number of Random Numbers:"), "", 20, box);

		distribution_combo = gtk_combo_new ();
		gtk_combo_set_popdown_strings (GTK_COMBO (distribution_combo),
					       distribution_type_strs);
		gtk_editable_set_editable
		  (GTK_EDITABLE (GTK_COMBO(distribution_combo)->entry), FALSE);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox),
					     distribution_combo);

		param_box = new_frame(_("Parameters:"), box);

		callback_data.dialog = dialog;
		callback_data.frame = param_box;
		callback_data.combo = distribution_combo;

		gtk_signal_connect
		  (GTK_OBJECT(GTK_COMBO(distribution_combo)->entry),
		   "changed", GTK_SIGNAL_FUNC (distribution_callback),
		   &callback_data);

		callback_data.discrete_box = gtk_vbox_new (FALSE, 0);
		discrete_range_entry = hbox_pack_label_and_entry
		  (_("Value and Probability Input Range:"), "", 20,
		   callback_data.discrete_box);

		callback_data.uniform_box = gtk_vbox_new (FALSE, 0);
		uniform_lower_entry = 
		  hbox_pack_label_and_entry(_("Between:"), "0", 20,
					    callback_data.uniform_box);
		uniform_upper_entry = 
		  hbox_pack_label_and_entry(_("And:"), "1", 20, 
					    callback_data.uniform_box);

		callback_data.normal_box = gtk_vbox_new (FALSE, 0);
		normal_mean_entry = hbox_pack_label_and_entry
		  (_("Mean = "), "0", 20, callback_data.normal_box);
		normal_stdev_entry = hbox_pack_label_and_entry
		  (_("Standard Deviation = "), "1", 20, callback_data.normal_box);

		callback_data.poisson_box = gtk_vbox_new (FALSE, 0);
		poisson_lambda_entry = hbox_pack_label_and_entry
		  (_("Lambda"), "0", 20, callback_data.poisson_box);

		callback_data.exponential_box = gtk_vbox_new (FALSE, 0);
		exponential_b_entry = hbox_pack_label_and_entry
		  (_("b Value"), "0", 20, callback_data.exponential_box);

		callback_data.binomial_box = gtk_vbox_new (FALSE, 0);
		binomial_p_entry = hbox_pack_label_and_entry
		  (_("p Value"), "0", 20, callback_data.binomial_box);
		binomial_trials_entry = hbox_pack_label_and_entry
		  (_("Number of Trials"), "0", 20, callback_data.binomial_box);

		callback_data.negbinom_box = gtk_vbox_new (FALSE, 0);
		negbinom_p_entry = hbox_pack_label_and_entry
		  (_("p Value"), "0", 20, callback_data.negbinom_box);
		negbinom_f_entry = hbox_pack_label_and_entry
		  (_("Number of Failures"), "0", 20,
		   callback_data.negbinom_box);

		callback_data.bernoulli_box = gtk_vbox_new (FALSE, 0);
		bernoulli_p_entry = hbox_pack_label_and_entry
		  (_("p Value"), "0", 20, callback_data.bernoulli_box);

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

		gtk_container_add(GTK_CONTAINER(param_box),
				  callback_data.discrete_box);

		gtk_container_add(GTK_CONTAINER(param_box),
				  callback_data.bernoulli_box);

		gtk_container_add(GTK_CONTAINER(param_box),
				  callback_data.poisson_box);

		gtk_container_add(GTK_CONTAINER(param_box),
				  callback_data.exponential_box);

		gtk_container_add(GTK_CONTAINER(param_box),
				  callback_data.binomial_box);

		gtk_container_add(GTK_CONTAINER(param_box),
				  callback_data.negbinom_box);

		gtk_container_add(GTK_CONTAINER(param_box),
				  callback_data.uniform_box);
		gtk_container_add(GTK_CONTAINER(param_box),
				  callback_data.normal_box);

		gtk_widget_show_all (dialog);
		gtk_widget_hide (callback_data.exponential_box);
		gtk_widget_hide (callback_data.negbinom_box);
		gtk_widget_hide (callback_data.binomial_box);
		gtk_widget_hide (callback_data.poisson_box);
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
			gtk_widget_hide (callback_data.poisson_box);
			gtk_widget_hide (callback_data.binomial_box);
			gtk_widget_hide (callback_data.negbinom_box);
			gtk_widget_hide (callback_data.exponential_box);
			break;
		case NormalDistribution:
		        gtk_widget_hide (callback_data.discrete_box);
			gtk_widget_hide (callback_data.uniform_box);
			gtk_widget_hide (callback_data.bernoulli_box);
			gtk_widget_hide (callback_data.poisson_box);
			gtk_widget_hide (callback_data.binomial_box);
			gtk_widget_hide (callback_data.negbinom_box);
			gtk_widget_hide (callback_data.exponential_box);
			break;
		case BernoulliDistribution:
		        gtk_widget_hide (callback_data.discrete_box);
			gtk_widget_hide (callback_data.uniform_box);
			gtk_widget_hide (callback_data.normal_box);
			gtk_widget_hide (callback_data.poisson_box);
			gtk_widget_hide (callback_data.binomial_box);
			gtk_widget_hide (callback_data.negbinom_box);
			gtk_widget_hide (callback_data.exponential_box);
			break;
		case UniformDistribution:
		        gtk_widget_hide (callback_data.discrete_box);
			gtk_widget_hide (callback_data.normal_box);
			gtk_widget_hide (callback_data.bernoulli_box);
			gtk_widget_hide (callback_data.poisson_box);
			gtk_widget_hide (callback_data.binomial_box);
			gtk_widget_hide (callback_data.negbinom_box);
			gtk_widget_hide (callback_data.exponential_box);
			break;
		case PoissonDistribution:
		        gtk_widget_hide (callback_data.discrete_box);
			gtk_widget_hide (callback_data.normal_box);
			gtk_widget_hide (callback_data.bernoulli_box);
			gtk_widget_hide (callback_data.uniform_box);
			gtk_widget_hide (callback_data.binomial_box);
			gtk_widget_hide (callback_data.negbinom_box);
			gtk_widget_hide (callback_data.exponential_box);
			break;
		case BinomialDistribution:
		        gtk_widget_hide (callback_data.discrete_box);
			gtk_widget_hide (callback_data.normal_box);
			gtk_widget_hide (callback_data.bernoulli_box);
			gtk_widget_hide (callback_data.uniform_box);
			gtk_widget_hide (callback_data.poisson_box);
			gtk_widget_hide (callback_data.negbinom_box);
			gtk_widget_hide (callback_data.exponential_box);
			break;
		case NegativeBinomialDistribution:
		        gtk_widget_hide (callback_data.discrete_box);
			gtk_widget_hide (callback_data.normal_box);
			gtk_widget_hide (callback_data.bernoulli_box);
			gtk_widget_hide (callback_data.uniform_box);
			gtk_widget_hide (callback_data.poisson_box);
			gtk_widget_hide (callback_data.binomial_box);
			gtk_widget_hide (callback_data.exponential_box);
			break;
		case ExponentialDistribution:
		        gtk_widget_hide (callback_data.discrete_box);
			gtk_widget_hide (callback_data.normal_box);
			gtk_widget_hide (callback_data.bernoulli_box);
			gtk_widget_hide (callback_data.uniform_box);
			gtk_widget_hide (callback_data.poisson_box);
			gtk_widget_hide (callback_data.binomial_box);
			gtk_widget_hide (callback_data.negbinom_box);
			break;
		default:
		        break;
		}
	}

        gtk_widget_grab_focus (vars_entry);

random_dialog_loop:

	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
	if (selection == -1)
		return;
	
	if (selection != 0) {
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
	if (strcmp(text, _("Uniform")) == 0) {
	        distribution = UniformDistribution;
		text = gtk_entry_get_text (GTK_ENTRY (uniform_lower_entry));
		param.uniform.lower_limit = atof(text);
		text = gtk_entry_get_text (GTK_ENTRY (uniform_upper_entry));
		param.uniform.upper_limit = atof(text);
	} else if (strcmp(text, _("Normal")) == 0) {
	        distribution = NormalDistribution;
		text = gtk_entry_get_text (GTK_ENTRY (normal_mean_entry));
		param.normal.mean = atof(text);
		text = gtk_entry_get_text (GTK_ENTRY (normal_stdev_entry));
		param.normal.stdev = atof(text);
	} else if (strcmp(text, _("Bernoulli")) == 0) {
	        distribution = BernoulliDistribution;
		text = gtk_entry_get_text (GTK_ENTRY (bernoulli_p_entry));
		param.bernoulli.p = atof(text);
	} else if (strcmp(text, _("Poisson")) == 0) {
	        distribution = PoissonDistribution;
		text = gtk_entry_get_text (GTK_ENTRY (poisson_lambda_entry));
		param.poisson.lambda = atof(text);
	} else if (strcmp(text, _("Exponential")) == 0) {
	        distribution = ExponentialDistribution;
		text = gtk_entry_get_text (GTK_ENTRY (exponential_b_entry));
		param.exponential.b = atof(text);
	} else if (strcmp(text, _("Binomial")) == 0) {
	        distribution = BinomialDistribution;
		text = gtk_entry_get_text (GTK_ENTRY (binomial_p_entry));
		param.binomial.p = atof(text);
		text = gtk_entry_get_text (GTK_ENTRY (binomial_trials_entry));
		param.binomial.trials = atoi(text);
	} else if (strcmp(text, _("Negative Binomial")) == 0) {
	        distribution = NegativeBinomialDistribution;
		text = gtk_entry_get_text (GTK_ENTRY (negbinom_p_entry));
		param.negbinom.p = atof(text);
		text = gtk_entry_get_text (GTK_ENTRY (negbinom_f_entry));
		param.negbinom.f = atoi(text);
	} else if (strcmp(text, _("Discrete")) == 0) {
	        distribution = DiscreteDistribution;
		text = gtk_entry_get_text (GTK_ENTRY (discrete_range_entry));
		if (!parse_range (text, &param.discrete.start_col,
			  &param.discrete.start_row,
			  &param.discrete.end_col,
			  &param.discrete.end_row)) {
		        error_in_entry(wb, discrete_range_entry, 
				       _("You should introduce a valid cell "
				       "range in 'Value and Probability Input "
				       "Range:'"));
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
dialog_regression_tool (Workbook *wb, Sheet *sheet)
{
        static GtkWidget *dialog, *box, *vbox;
	static GtkWidget *range1_entry, *range2_entry, *output_range_entry;
	static GtkWidget *alpha_entry;
	static GSList    *output_ops;
	int 		 i, xdim;
	static int       labels = 0;
	int		 err = 0;

	data_analysis_output_t  dao;
	float_t alpha;

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
		  (_("Input Y Range:"), "", 20, vbox);

		range2_entry = hbox_pack_label_and_entry
		  (_("Input X Range:"), "", 20, vbox);

		alpha_entry = hbox_pack_label_and_entry(_("Confidence Level:"),
							"0.95", 20, vbox);

		add_check_buttons(vbox, first_row_label_button);
		add_check_buttons(vbox, force_intercept_zero_button); 

		box = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		output_range_entry = add_output_frame(box, &output_ops);

		gtk_widget_show_all (dialog);
	} else
		gtk_widget_show_all (dialog);

        gtk_widget_grab_focus (range1_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
	if (selection == -1)
		return;
	
	if (selection != 0) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range1_entry));
	if (!parse_range (text, &range_inputy.start.col,
			  &range_inputy.start.row,
			  &range_inputy.end.col,
			  &range_inputy.end.row)) {
	        error_in_entry(wb, range1_entry, 
			       _("You should introduce a valid cell range "
			       "in 'Variable 1:'"));
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range2_entry));
	if (!parse_multiple_ranges (text, &range_inputxs, &xdim)) {
	        error_in_entry(wb, range2_entry, 
			       _("You should introduce a valid cell range "
			       "in 'Variable 2:'"));
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	if (parse_output(output, sheet, output_range_entry, wb, &dao))
	        goto dialog_loop;

	labels = label_row_flag;
	if (labels) {
	        range_inputy.start.row++;
		for (i = 0; i < xdim; i++) 
	        	range_inputxs[i].start.row++;
	}
	
	err = regression_tool (wb, sheet, &range_inputy,
			     range_inputxs, alpha, &dao, intercept_flag, xdim);
	if (err){
		switch (err){
		case 1: 
			gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
			      _("There are too few data points to conduct this "
				"regression.\nThere must be at least as many "
				"data points as free variables."));
			break;
		case 2: 
			gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR, 
			      _("Two or more of the independent variables "
				"are linearly dependent,\nand the regression "
				"cannot be calculated. Remove one of these\n"
				"variables and try the regression again."));
			break;
		case 3:
			gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
			      _("There must be an equal number of entries "
				"for each variable in the regression."));
			break;
		}
		g_free (range_inputxs);
		goto dialog_loop; 
	}
	g_free (range_inputxs);
	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}

static void
dialog_average_tool (Workbook *wb, Sheet *sheet)
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
	        dialog = new_dialog(_("Moving Average"));

		box = gtk_vbox_new (FALSE, 0);
		vbox = new_frame(_("Input:"), box);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		range_entry = hbox_pack_label_and_entry
		  (_("Input Range:"), "", 20, vbox);

		interval_entry = hbox_pack_label_and_entry(_("Interval:"), "3",
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

	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
	if (selection == -1)
		return;
	
	if (selection != 0) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry(wb, range_entry, 
			       _("You should introduce a valid cell range "
			       "in 'Range Input:'"));
		goto dialog_loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (interval_entry));
	interval = atoi(text);

	if (parse_output(output, sheet, output_range_entry, wb, &dao))
	        goto dialog_loop;

	labels = label_row_flag;
	if (labels) {
	        range.start.row++;
	        range.start.row++;
	}

	if (average_tool (wb, sheet, &range, interval,
			  standard_errors_flag, &dao))
	        goto dialog_loop;

	workbook_focus_sheet(sheet);
 	gnome_dialog_close (GNOME_DIALOG (dialog));
}


static void
dialog_ranking_tool (Workbook *wb, Sheet *sheet)
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

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/analysis-tools.glade", NULL);

        if (!gui) {
                printf ("Could not find analysis-tools.glade\n");
                return;
        }

	dao.type = NewSheetOutput;
	group    = 0;

	dialog = glade_xml_get_widget (gui, "RankAndPercentile");
        range_entry = glade_xml_get_widget (gui, "rank_entry1");
	checkbutton = glade_xml_get_widget (gui, "rank_checkbutton");
	output_range_entry = glade_xml_get_widget (gui, "rank_entry2");

        if (!dialog || !range_entry || !output_range_entry || !checkbutton) {
                printf ("Corrupt file `analysis-tools.glade'\n");
                return;
        }
	if (set_group_option_signals (gui, &group,  "rank") ||
	    set_output_option_signals (gui, &dao, "rank"))
	        return;

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);

        gtk_widget_grab_focus (range_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return;
	}
	
	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry(wb, range_entry, 
			       _("You should introduce a valid cell range "
			       "in 'Range:'"));
		goto dialog_loop;
	}

	dao.labels_flag = labels;

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry(wb, output_range_entry, 
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

	if (ranking_tool (wb, sheet, &range, !group, &dao))
	        goto dialog_loop;

	workbook_focus_sheet(sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));
}


static void
dialog_anova_single_factor_tool (Workbook *wb, Sheet *sheet)
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
	float_t  alpha;
	int      group, selection, x1, x2, y1, y2;
	Range    range;

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/analysis-tools.glade", NULL);

        if (!gui) {
                printf ("Could not find analysis-tools.glade\n");
                return;
        }

	dao.type = NewSheetOutput;
	group    = 0;

	dialog = glade_xml_get_widget (gui, "Anova1");
        range_entry = glade_xml_get_widget (gui, "anova1_entry1");
	checkbutton = glade_xml_get_widget (gui, "anova1_checkbutton");
	alpha_entry = glade_xml_get_widget (gui, "anova1_entry2");
	output_range_entry = glade_xml_get_widget (gui, "anova1_entry3");

        if (!dialog || !range_entry || !output_range_entry || !checkbutton ||
	    !alpha_entry) {
                printf ("Corrupt file analysis-tools.glade\n");
                return;
        }
	if (set_group_option_signals (gui, &group, "anova1") ||
	    set_output_option_signals (gui, &dao, "anova1"))
	        return;

	gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (checkbutton_toggled), &labels);
	gtk_entry_set_text (GTK_ENTRY (alpha_entry), "0.95");

        gtk_widget_grab_focus (range_entry);

dialog_loop:

	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
	if (selection == -1) {
	        gtk_object_unref (GTK_OBJECT (gui));
		return;
	}
	
	if (selection != 0) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry(wb, range_entry, 
			       _("You should introduce a valid cell range "
			       "in 'Range:'"));
		goto dialog_loop;
	}

	dao.labels_flag = labels;

	if (dao.type == RangeOutput) {
	        text = gtk_entry_get_text (GTK_ENTRY (output_range_entry));
	        if (!parse_range (text, &x1, &y1, &x2, &y2)) {
		        error_in_entry(wb, output_range_entry, 
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

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	alpha = atof (text);

	if (anova_single_factor_tool (wb, sheet, &range, !group, alpha, &dao))
	        goto dialog_loop;

	workbook_focus_sheet(sheet);

	gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));
}


static void
dialog_anova_two_factor_without_r_tool (Workbook *wb, Sheet *sheet)
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
	        dialog = new_dialog(_("Anova: Two-Factor Without Replication"));

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		box = new_frame(_("Input:"), box);

		range_entry = hbox_pack_label_and_entry
		  (_("Input Range:"), "", 20, box);

		alpha_entry = hbox_pack_label_and_entry(_("Alpha:"), "0.95",
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

	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
	if (selection != 0) {
	        gnome_dialog_close (GNOME_DIALOG (dialog));
		return;
	}

	output = gtk_radio_group_get_selected (output_ops);

	text = gtk_entry_get_text (GTK_ENTRY (range_entry));
	if (!parse_range (text, &range.start.col,
			  &range.start.row,
			  &range.end.col,
			  &range.end.row)) {
	        error_in_entry(wb, range_entry, 
			       _("You should introduce a valid cell range "
			       "in 'Range:'"));
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
selection_made (GtkWidget *clist, gint row, gint column,
	       GdkEventButton *event, gpointer data)
{
        selected_row = row;
}


void
dialog_data_analysis (Workbook *wb, Sheet *sheet)
{
        GladeXML  *gui;
	GtkWidget *dialog;
	GtkWidget *tool_list;

	int       i, selection;

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/analysis-tools.glade", NULL);

        if (!gui) {
                printf ("Could not find analysis-tools.glade\n");
                return;
        }

	dialog = glade_xml_get_widget (gui, "AnalysisTools");
        if (!dialog) {
                printf ("Corrupt file analysis-tools.glade\n");
                return;
        }

        tool_list = glade_xml_get_widget (gui, "clist1");
	gtk_signal_connect (GTK_OBJECT(tool_list), "select_row",
			    GTK_SIGNAL_FUNC(selection_made), NULL);

	for (i=0; tools[i].fun; i++)
	        gtk_clist_append (GTK_CLIST (tool_list),
				  (char **) &tools[i].name);
	gtk_clist_select_row (GTK_CLIST (tool_list), selected_row, 0);

	gtk_widget_grab_focus (GTK_WIDGET(tool_list));

	/* Run the dialog */
	selection = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
	gtk_object_unref (GTK_OBJECT (gui));
	
	if (selection == -1)
		return;
	
	gnome_dialog_close (GNOME_DIALOG (dialog));

	if (selection == 0) {
	        g_return_if_fail (tools[selected_row].fun != NULL);
		tools[selected_row].fun(wb, sheet);
	}
}
