/*
 * dialog-analysis-tools.c:
 *
 * Authors:
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 *  Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000 by Jukka-Pekka Iivonen <iivonen@iki.fi>
 */

#include <config.h>
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-help.h>
#include <glade/glade.h>
#include <string.h>
#include "gnumeric.h"
#include "workbook.h"
#include "workbook-control.h"
#include "workbook-edit.h"
#include "workbook-view.h"
#include "gui-util.h"
#include "utils-dialog.h"
#include "dialogs.h"
#include "parse-util.h"
#include "utils-dialog.h"
#include "tools.h"
#include "ranges.h"
#include "selection.h"
#include "value.h"
#include "widgets/gnumeric-expr-entry.h"

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


static int                     label_row_flag,  intercept_flag;

static stat_tool_t ds;
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
first_row_label_signal_fun ()
{
        label_row_flag = !label_row_flag;
}

static void
force_intercept_zero_signal_fun ()
{
	intercept_flag = !intercept_flag;
}

static check_button_t first_row_label_button[] = {
        { N_("Labels in First Row"), first_row_label_signal_fun, FALSE,
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


/**********************************************/
/*  Generic guru items */
/**********************************************/

static const char *output_group[] = {
	"newsheet-button",
	"newworkbook-button",
	"outputrange-button",
	0
};

static const char *grouped_by_group[] = {
	"grouped_by_row",
	"grouped_by_col",
	0
};


typedef enum {
	TOOL_CORRELATION = 1,       /* use GenericToolState */
	TOOL_COVARIANCE = 2,        /* use GenericToolState */
	TOOL_RANK_PERCENTILE = 3,   /* use GenericToolState */
	TOOL_FTEST = 4,   /* use GenericToolState */
	TOOL_GENERIC = 10,          /* all smaller types are generic */
	TOOL_DESC_STATS = 11,
	TOOL_TTEST = 12,
	TOOL_SAMPLING = 13
} ToolType;


#define CORRELATION_KEY       "analysistools-correlation-dialog"
#define COVARIANCE_KEY        "analysistools-covariance-dialog"
#define DESCRIPTIVE_STATS_KEY "analysistools-descriptive-stats-dialog"
#define RANK_PERCENTILE_KEY   "analysistools-rank-percentile-dialog"
#define TTEST_KEY             "analysistools-ttest-dialog"
#define FTEST_KEY             "analysistools-ftest-dialog"
#define SAMPLING_KEY          "analysistools-sampling-dialog"



typedef struct {
	ToolType  const type;
	GladeXML  *gui;
	GtkWidget *dialog;
	GnumericExprEntry *input_entry;
	GnumericExprEntry *input_entry_2;
	GnumericExprEntry *output_entry;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	GtkWidget *apply_button;
	GtkWidget *help_button;
	char *helpfile;
	GtkWidget *new_sheet;
	GtkWidget *new_workbook;
	GtkWidget *output_range;
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
} GenericToolState;

typedef struct {
	ToolType  const type;
	GladeXML  *gui;
	GtkWidget *dialog;
	GnumericExprEntry *input_entry;
	GnumericExprEntry *input_entry_2;
	GnumericExprEntry *output_entry;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	GtkWidget *apply_button;
	GtkWidget *help_button;
	char *helpfile;
	GtkWidget *new_sheet;
	GtkWidget *new_workbook;
	GtkWidget *output_range;
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;

	GtkWidget *summary_stats_button;
	GtkWidget *mean_stats_button;
	GtkWidget *kth_largest_button;
	GtkWidget *kth_smallest_button;
} DescriptiveStatState;

typedef enum {
	TTEST_PAIRED = 1,
	TTEST_UNPAIRED_EQUALVARIANCES = 2,
	TTEST_UNPAIRED_UNEQUALVARIANCES = 3,
	TTEST_ZTEST = 4
} ttest_type;

typedef struct {
	ToolType  const type;
	GladeXML  *gui;
	GtkWidget *dialog;
	GnumericExprEntry *input_entry;
	GnumericExprEntry *input_entry_2;
	GnumericExprEntry *output_entry;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	GtkWidget *apply_button;
	GtkWidget *help_button;
	char *helpfile;
	GtkWidget *new_sheet;
	GtkWidget *new_workbook;
	GtkWidget *output_range;
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;

	GtkWidget *paired_button;
	GtkWidget *unpaired_button;
	GtkWidget *known_button;
	GtkWidget *unknown_button;
	GtkWidget *equal_button;
	GtkWidget *unequal_button;
	GtkWidget *variablespaired_label;
	GtkWidget *varianceknown_label;
	GtkWidget *varianceequal_label;
	GtkWidget *var1_variance_label;
	GtkWidget *var2_variance_label;
	GtkWidget *var1_variance;
	GtkWidget *var2_variance;
	GtkWidget *options_table;

	ttest_type invocation;
} TTestState;

typedef struct {
	ToolType  const type;
	GladeXML  *gui;
	GtkWidget *dialog;
	GnumericExprEntry *input_entry;
	GnumericExprEntry *input_entry_2;
	GnumericExprEntry *output_entry;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	GtkWidget *apply_button;
	GtkWidget *help_button;
	char *helpfile;
	GtkWidget *new_sheet;
	GtkWidget *new_workbook;
	GtkWidget *output_range;
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;

	GtkWidget *periodic_button;
	GtkWidget *random_button;
	GtkWidget *method_label;
	GtkWidget *period_label;
	GtkWidget *random_label;
	GtkWidget *period_entry;
	GtkWidget *random_entry;
	GtkWidget *options_table;
} SamplingState;


typedef union {
	ToolType  const type;
	GenericToolState tt_generic;
	DescriptiveStatState tt_desc_stat;
	TTestState tt_ttest;
	SamplingState tt_sampling;
} ToolState;

/**********************************************/
/*  Generic functions for the analysis tools. */
/*  Functions in this section are being used  */
/*  by virtually all tools.                   */
/**********************************************/
/**
 * tool_help_cb:
 * @button:
 * @state:
 *
 * Provide help.
 **/
static void
tool_help_cb(GtkWidget *button, GenericToolState *state)
{
	if (state->helpfile != NULL) {
		GnomeHelpMenuEntry help_ref;
		help_ref.name = "gnumeric";
		help_ref.path = state->helpfile;
		gnome_help_display (NULL, &help_ref);		
	}
	return;
}

/**
 * tool_destroy:
 * @window:
 * @focus_widget:
 * @state:
 *
 * Destroy the dialog and associated data structures.
 *
 **/
static gboolean
tool_destroy (GtkObject *w, GenericToolState  *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		gtk_object_unref (GTK_OBJECT (state->gui));
		state->gui = NULL;
	}

	wbcg_edit_finish (state->wbcg, FALSE);

	state->dialog = NULL;

	g_free (state);

	return FALSE;
}

/**
 * cb_tool_cancel_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_tool_cancel_clicked(GtkWidget *button, GenericToolState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}

/**
 * tool_set_focus:
 * @window:
 * @focus_widget:
 * @state:
 *
 **/
static void
tool_set_focus (GtkWidget *window, GtkWidget *focus_widget,
			GenericToolState *state)
{
	if (IS_GNUMERIC_EXPR_ENTRY (focus_widget)) {
		wbcg_set_entry (state->wbcg,
				    GNUMERIC_EXPR_ENTRY (focus_widget));
		gnumeric_expr_entry_set_absolute (GNUMERIC_EXPR_ENTRY (focus_widget));
	} else
		wbcg_set_entry (state->wbcg, NULL);
}

/**
 * tool_set_focus_output_range:
 * @widget:
 * @focus_widget:
 * @state:
 *
 * Output range entry was focused. Switch to output range.
 * 
 **/
static void
tool_set_focus_output_range (GtkWidget *widget, GdkEventFocus *event,
			GenericToolState *state)
{
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(state->output_range),TRUE);
}

/**
 * dialog_tool_init:
 * @state:
 * @gui_name:
 * @dialog_name:
 * @ok_function:
 * @sensitivity_cb:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_tool_init (GenericToolState *state, char *gui_name, char *dialog_name, 
                       GtkSignalFunc ok_function, GtkSignalFunc sensitivity_cb)
{
	GtkTable *table;
	GtkWidget *widget;

	state->gui = gnumeric_glade_xml_new (state->wbcg, gui_name);
        if (state->gui == NULL)
                return TRUE;

	state->dialog = glade_xml_get_widget (state->gui, dialog_name);
        if (state->dialog == NULL)
                return TRUE;

	state->ok_button     = glade_xml_get_widget(state->gui, "okbutton");
	gtk_signal_connect (GTK_OBJECT (state->ok_button), "clicked",
			    ok_function,
			    state);

	state->cancel_button  = glade_xml_get_widget(state->gui, "cancelbutton");
	gtk_signal_connect (GTK_OBJECT (state->cancel_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_tool_cancel_clicked),
			    state);

	state->apply_button     = glade_xml_get_widget(state->gui, "applybutton");
	if (state->apply_button != NULL ) 
		gtk_signal_connect (GTK_OBJECT (state->apply_button), "clicked",
				    ok_function, state);
	state->help_button     = glade_xml_get_widget(state->gui, "helpbutton");
	if (state->help_button != NULL ) 
		gtk_signal_connect (GTK_OBJECT (state->help_button), "clicked",
				    GTK_SIGNAL_FUNC (tool_help_cb), state);

	state->new_sheet  = glade_xml_get_widget(state->gui, "newsheet-button");
	state->new_workbook  = glade_xml_get_widget(state->gui, "newworkbook-button");
	state->output_range  = glade_xml_get_widget(state->gui, "outputrange-button");

	table = GTK_TABLE (glade_xml_get_widget (state->gui, "input-table"));
	state->input_entry = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags(state->input_entry,
                                      GNUM_EE_SINGLE_RANGE | GNUM_EE_SHEET_OPTIONAL, 
                                      GNUM_EE_MASK);
        gnumeric_expr_entry_set_scg(state->input_entry, wb_control_gui_cur_sheet (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->input_entry),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gtk_widget_show (GTK_WIDGET (state->input_entry));
	gtk_signal_connect_after (GTK_OBJECT (state->input_entry), "changed",
			    GTK_SIGNAL_FUNC (sensitivity_cb), state);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->input_entry));

/*                                                        */
/* If there is a var2-label, we need a second input field */
/*                                                        */
	widget = glade_xml_get_widget (state->gui, "var2-label");
	if (widget == NULL) {
		state->input_entry_2 = NULL;
	} else {
		state->input_entry_2 = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
		gnumeric_expr_entry_set_flags(state->input_entry_2,
                                      GNUM_EE_SINGLE_RANGE | GNUM_EE_SHEET_OPTIONAL, 
                                      GNUM_EE_MASK);
		gnumeric_expr_entry_set_scg(state->input_entry_2, 
					    wb_control_gui_cur_sheet (state->wbcg));
		gtk_table_attach (table, GTK_WIDGET (state->input_entry_2),
			  1, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
		gtk_widget_show (GTK_WIDGET (state->input_entry_2));		
		gnumeric_editable_enters (GTK_WINDOW (state->dialog),
					  GTK_EDITABLE (state->input_entry_2));
		gtk_signal_connect_after (GTK_OBJECT (state->input_entry_2), "changed",
					  GTK_SIGNAL_FUNC (sensitivity_cb), state);
	}


	table = GTK_TABLE (glade_xml_get_widget (state->gui, "output-table"));
	state->output_entry = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags(state->output_entry,
                                      GNUM_EE_SINGLE_RANGE | GNUM_EE_SHEET_OPTIONAL, 
                                      GNUM_EE_MASK);
        gnumeric_expr_entry_set_scg(state->output_entry, wb_control_gui_cur_sheet (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->output_entry),
			  1, 2, 2, 3,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gtk_widget_show (GTK_WIDGET (state->output_entry));


	gtk_signal_connect (GTK_OBJECT (state->dialog), "set-focus",
			    GTK_SIGNAL_FUNC (tool_set_focus), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (tool_destroy), state);
	gtk_signal_connect (GTK_OBJECT (state->output_entry), "focus-in-event",
			    GTK_SIGNAL_FUNC (tool_set_focus_output_range), state);
	gtk_signal_connect_after (GTK_OBJECT (state->output_entry), "changed",
			    GTK_SIGNAL_FUNC (sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->output_range), "toggled",
			    GTK_SIGNAL_FUNC (sensitivity_cb), state);
 
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->output_entry));

	return FALSE;
}

/**********************************************/
/*  Generic functions for the analysis tools  */
/*  Functions in this section are being used  */
/*  some tools                                */
/**********************************************/

/**
 * tool_update_sensitivity:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity if the only items of interest
 * are the standard input (one range) and output items.
 **/
static void
tool_update_sensitivity_cb (GtkWidget *dummy, GenericToolState *state)
{
	gboolean ready  = FALSE;
	char const *output_text;
	char const *input_text;
	char const *input_text_2;
	int i;
        Value *output_range;
        Value *input_range;
        Value *input_range_2;

	output_text = gtk_entry_get_text (GTK_ENTRY (state->output_entry));
	input_text = gtk_entry_get_text (GTK_ENTRY (state->input_entry));
        output_range = range_parse(state->sheet,output_text,TRUE);
        input_range = range_parse(state->sheet,input_text,TRUE);
	if (state->input_entry_2 != NULL) {
		input_text_2 = gtk_entry_get_text (GTK_ENTRY (state->input_entry_2));
		input_range_2 = range_parse(state->sheet,input_text_2,TRUE);
	} else {
		input_range_2 = NULL;
	}
		
	i = gnumeric_glade_group_value (state->gui, output_group);

	ready = ((input_range != NULL) && 
		 ((state->input_entry_2 == NULL) || (input_range_2 != NULL)) &&
                 ((i != 2) || (output_range != NULL)));

        if (input_range != NULL) value_release(input_range);
        if (input_range_2 != NULL) value_release(input_range_2);
        if (output_range != NULL) value_release(output_range);

	if(state->apply_button != NULL) 
		gtk_widget_set_sensitive (state->apply_button, ready);
	gtk_widget_set_sensitive (state->ok_button, ready);
}


/**********************************************/
/*  Begin of correlation tool code */
/**********************************************/


/**
 * corr_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the correlation_tool.
 * Note that we assume that the ok_button is only active if the entry fields 
 * contain sensible data.
 **/
static void
corr_tool_ok_clicked_cb(GtkWidget *button, GenericToolState *state)
{
	data_analysis_output_t  dao;
	Range range;
        char   *text;
	GtkWidget *w;

	text = gtk_entry_get_text (GTK_ENTRY (state->input_entry));
	parse_range (text, &range.start.col,
			             &range.start.row,
			             &range.end.col,
				     &range.end.row);

        parse_output (state->wbcg, state->sheet, 
                      gnumeric_glade_group_value (state->gui, output_group),
	              GTK_WIDGET(state->output_entry), 
                      &dao);


	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	switch (correlation_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
				  &range, 
				  gnumeric_glade_group_value (state->gui, grouped_by_group), 
				  &dao)) {
	case 0: gtk_widget_destroy (state->dialog);
		break;
	case 1: error_in_entry (state->wbcg, GTK_WIDGET(state->input_entry),
				_("Please do not include any non-numeric (or empty)"
				  " data."));
	break;
	default: break;
	}
	return;
}



/**
 * dialog_correlation_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
static int
dialog_correlation_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GenericToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, CORRELATION_KEY))
		return 0;

	state = g_new (GenericToolState, 1);
	(*(ToolType *)state) = TOOL_CORRELATION;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->helpfile = "correlation-tool.html";

	if (dialog_tool_init (state, "correlation.glade", "Correlation", 
                       GTK_SIGNAL_FUNC (corr_tool_ok_clicked_cb), 
                       GTK_SIGNAL_FUNC (tool_update_sensitivity_cb))) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Correlation Tool dialog."));
		g_free (state);
		return 0;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       CORRELATION_KEY);

	tool_update_sensitivity_cb (NULL,state);	
	gtk_widget_show (state->dialog);

        return 0;
}

/**********************************************/
/*  End of correlation tool code */
/**********************************************/

/**********************************************/
/*  Begin of covariance tool code */
/**********************************************/


/**
 * cov_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the covariance_tool.
 * Note that we assume that the ok_button is only active if the entry fields 
 * contain sensible data.
 **/
static void
cov_tool_ok_clicked_cb(GtkWidget *button, GenericToolState *state)
{
	data_analysis_output_t  dao;
	Range range;
        char   *text;
	GtkWidget *w;

	text = gtk_entry_get_text (GTK_ENTRY (state->input_entry));
	parse_range (text, &range.start.col,
			             &range.start.row,
			             &range.end.col,
				     &range.end.row);

        parse_output (state->wbcg, state->sheet, 
                      gnumeric_glade_group_value (state->gui, output_group),
	              GTK_WIDGET(state->output_entry), 
                      &dao);


	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	switch (covariance_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
				  &range, 
				  gnumeric_glade_group_value (state->gui, grouped_by_group), 
				  &dao)) {
	case 0: gtk_widget_destroy (state->dialog);
		break;
	case 1: error_in_entry (state->wbcg, GTK_WIDGET(state->input_entry),
				_("Please do not include any non-numeric (or empty)"
				  " data."));
	break;
	default: break;
	}
	return;
}



/**
 * dialog_covariance_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
static int
dialog_covariance_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GenericToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, COVARIANCE_KEY))
		return 0;

	state = g_new (GenericToolState, 1);
	(*(ToolType *)state) = TOOL_COVARIANCE;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->helpfile = "covariance-tool.html";

	if (dialog_tool_init (state, "covariance.glade", "Covariance", 
                       GTK_SIGNAL_FUNC (cov_tool_ok_clicked_cb), 
                       GTK_SIGNAL_FUNC (tool_update_sensitivity_cb))) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Covariance Tool dialog."));
		g_free (state);
		return 0;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       COVARIANCE_KEY);

	tool_update_sensitivity_cb (NULL,state);	
	gtk_widget_show (state->dialog);

        return 0;
}

/**********************************************/
/*  End of covariance tool code */
/**********************************************/

/**********************************************/
/*  Begin of descriptive statistics tool code */
/**********************************************/

static const char *stats_group[] = {
	"summary_stats_button",
	"mean_stats_button",
	"kth_largest_button",
	"kth_smallest_button",
	0
};

/**
 * cb_desc_stat_tool_ok_clicked:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the descriptive_stat_tool.
 * Note that we assume that the ok_button is only active if the entry fields 
 * contain sensible data.
 **/
static void
cb_desc_stat_tool_ok_clicked(GtkWidget *button, DescriptiveStatState *state)
{
	data_analysis_output_t  dao;
	descriptive_stat_tool_t dst;
	Range range;
        char   *text;
	GtkWidget *w;

	text = gtk_entry_get_text (GTK_ENTRY (state->input_entry));
	parse_range (text, &range.start.col,
			             &range.start.row,
			             &range.end.col,
				     &range.end.row);

	w = glade_xml_get_widget (state->gui, "summary_stats_button");
	dst.summary_statistics = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	w = glade_xml_get_widget (state->gui, "mean_stats_button");
	dst.confidence_level = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	w = glade_xml_get_widget (state->gui, "kth_largest_button");
	dst.kth_largest = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	w = glade_xml_get_widget (state->gui, "kth_smallest_button");
	dst.kth_smallest = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));


	w = glade_xml_get_widget (state->gui, "c_entry");
	text = gtk_entry_get_text (GTK_ENTRY (w));
	dst.c_level = atof(text);
	w = glade_xml_get_widget (state->gui, "l_entry");
	text = gtk_entry_get_text (GTK_ENTRY (w));
	dst.k_largest = atoi(text);
	w = glade_xml_get_widget (state->gui, "s_entry");
	text = gtk_entry_get_text (GTK_ENTRY (w));
	dst.k_smallest = atoi(text);

	
        parse_output (state->wbcg, state->sheet, 
                      gnumeric_glade_group_value (state->gui, output_group),
	              GTK_WIDGET(state->output_entry), 
                      &dao);


	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	if (descriptive_stat_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
				   &range, 
				   gnumeric_glade_group_value (state->gui, grouped_by_group), 
				   &dst, &dao))
	        return;

	gtk_widget_destroy (state->dialog);

	return;
}

/**
 * desc_stat_tool_update_sensitivity_cb:
 * @state:
 *
 * Update the dialog widgets sensitivity.
 * We cannot use tool_update_sensitivity_cb
 * since we are also considering whether in fact 
 * a statistic is selected.
 **/
static void
desc_stat_tool_update_sensitivity_cb (GtkWidget *dummy, DescriptiveStatState *state)
{
	gboolean ready  = FALSE;
	char const *output_text;
	char const *input_text;
	int i, j;
        Value *output_range;
        Value *input_range;

	output_text = gtk_entry_get_text (GTK_ENTRY (state->output_entry));
	input_text = gtk_entry_get_text (GTK_ENTRY (state->input_entry));
        output_range = range_parse(state->sheet,output_text,TRUE);
        input_range = range_parse(state->sheet,input_text,TRUE);
	i = gnumeric_glade_group_value (state->gui, output_group);
	j = gnumeric_glade_group_value (state->gui, stats_group);

	ready = ((input_range != NULL) && 
                 (j > -1) && 
                 ((i != 2) || (output_range != NULL)));

        if (input_range != NULL) value_release(input_range);
        if (output_range != NULL) value_release(output_range);

	gtk_widget_set_sensitive (state->ok_button, ready);
}


/**
 * dialog_desc_stat_tool_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_desc_stat_tool_init (DescriptiveStatState *state)
{
	if (dialog_tool_init ((GenericToolState *)state, "descriptive-stats.glade", "DescStats", 
                       GTK_SIGNAL_FUNC (cb_desc_stat_tool_ok_clicked), 
                       GTK_SIGNAL_FUNC (desc_stat_tool_update_sensitivity_cb))) {
		return TRUE;
	}

	state->summary_stats_button  = glade_xml_get_widget(state->gui, "summary_stats_button");
	state->mean_stats_button  = glade_xml_get_widget(state->gui, "mean_stats_button");
	state->kth_largest_button  = glade_xml_get_widget(state->gui, "kth_largest_button");
	state->kth_smallest_button  = glade_xml_get_widget(state->gui, "kth_smallest_button");

	gtk_signal_connect_after (GTK_OBJECT (state->summary_stats_button), "toggled",
			    GTK_SIGNAL_FUNC (desc_stat_tool_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->mean_stats_button), "toggled",
			    GTK_SIGNAL_FUNC (desc_stat_tool_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->kth_largest_button), "toggled",
			    GTK_SIGNAL_FUNC (desc_stat_tool_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->kth_smallest_button), "toggled",
			    GTK_SIGNAL_FUNC (desc_stat_tool_update_sensitivity_cb), state);
 
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       DESCRIPTIVE_STATS_KEY);

	desc_stat_tool_update_sensitivity_cb (NULL,state);	

	return FALSE;
}

/**
 * dialog_descriptive_stat_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
static int
dialog_descriptive_stat_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        DescriptiveStatState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, DESCRIPTIVE_STATS_KEY))
		return 0;

	state = g_new (DescriptiveStatState, 1);
	(*(ToolType *)state) = TOOL_DESC_STATS;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->helpfile = "descriptive-statistics-tool.html";

	if (dialog_desc_stat_tool_init (state)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Descriptive Statistics Tool dialog."));
		g_free (state);
		return 0;
	}

	gtk_widget_show (state->dialog);

        return 0;
}


/**********************************************/
/*  End of descriptive statistics tool code */
/**********************************************/

/**********************************************/
/*  Begin of rank and percentile tool code */
/**********************************************/


/**
 * rank_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the ranking_tool.
 * Note that we assume that the ok_button is only active if the entry fields 
 * contain sensible data.
 **/
static void
rank_tool_ok_clicked_cb(GtkWidget *button, GenericToolState *state)
{
	data_analysis_output_t  dao;
	Range range;
        char   *text;
	GtkWidget *w;

	text = gtk_entry_get_text (GTK_ENTRY (state->input_entry));
	parse_range (text, &range.start.col,
			             &range.start.row,
			             &range.end.col,
				     &range.end.row);

        parse_output (state->wbcg, state->sheet, 
                      gnumeric_glade_group_value (state->gui, output_group),
	              GTK_WIDGET(state->output_entry), 
                      &dao);


	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	switch (ranking_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
				  &range, 
				  gnumeric_glade_group_value (state->gui, grouped_by_group), 
				  &dao)) {
	case 0: gtk_widget_destroy (state->dialog);
		break;
	default: break;
	}
	return;
}



/**
 * dialog_ranking_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
static int
dialog_ranking_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GenericToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, RANK_PERCENTILE_KEY))
		return 0;

	state = g_new (GenericToolState, 1);
	(*(ToolType *)state) = TOOL_RANK_PERCENTILE;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->helpfile = "rank-and-percentile-tool.html";

	if (dialog_tool_init (state, "rank.glade", "RankPercentile", 
                       GTK_SIGNAL_FUNC (rank_tool_ok_clicked_cb), 
                       GTK_SIGNAL_FUNC (tool_update_sensitivity_cb))) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Rank and  Percentile Tools dialog."));
		g_free (state);
		return 0;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       RANK_PERCENTILE_KEY);

	tool_update_sensitivity_cb (NULL,state);	
	gtk_widget_show (state->dialog);

        return 0;
}

/**********************************************/
/*  End of rank and percentile tool code */
/**********************************************/

/**********************************************/
/*  Begin of ttest tool code */
/**********************************************/

static TTestState *ttest_tool_state;

/**
 * ttest_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the appropriate tool.
 * Note that we assume that the ok_button is only active if the entry fields 
 * contain sensible data.
 **/
static void
ttest_tool_ok_clicked_cb(GtkWidget *button, TTestState *state)
{
	data_analysis_output_t  dao;
	Range range_1;
	Range range_2;
        char   *text;
	GtkWidget *w;
	GtkWidget *mean_diff_entry, *alpha_entry;
	int    err;
	gnum_float alpha, mean_diff, var1, var2;

	text = gtk_entry_get_text (GTK_ENTRY (state->input_entry));
	parse_range (text, &range_1.start.col,
			             &range_1.start.row,
			             &range_1.end.col,
				     &range_1.end.row);
	text = gtk_entry_get_text (GTK_ENTRY (state->input_entry_2));
	parse_range (text, &range_2.start.col,
			             &range_2.start.row,
			             &range_2.end.col,
				     &range_2.end.row);

        parse_output (state->wbcg, state->sheet, 
                      gnumeric_glade_group_value (state->gui, output_group),
	              GTK_WIDGET(state->output_entry), 
                      &dao);

	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	if( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->paired_button)) == 1) {
		state->invocation = TTEST_PAIRED;
	} else {
		if( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->known_button)) == 1) {
			state->invocation = TTEST_ZTEST;
		} else {
			if( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->equal_button)) == 1) {
				state->invocation = TTEST_UNPAIRED_EQUALVARIANCES;
			} else {
				state->invocation = TTEST_UNPAIRED_UNEQUALVARIANCES;
			}
		}
	}

	mean_diff_entry = glade_xml_get_widget (state->gui, "meandiff");
	text = gtk_entry_get_text (GTK_ENTRY (mean_diff_entry));
	mean_diff = atof(text);

	alpha_entry = glade_xml_get_widget (state->gui, "one_alpha");
	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	switch (state->invocation) {
	case TTEST_PAIRED: 
		err = ttest_paired_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
			       &range_1, &range_2,
					 mean_diff, alpha, &dao);
		break;
	case TTEST_UNPAIRED_EQUALVARIANCES:
		err = ttest_eq_var_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
					 &range_1, &range_2,
					 mean_diff, alpha, &dao);
		break;
	case TTEST_UNPAIRED_UNEQUALVARIANCES:
		err = ttest_neq_var_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
					  &range_1, &range_2,
					  mean_diff, alpha, &dao);
		break;
	case TTEST_ZTEST:
		text = gtk_entry_get_text (GTK_ENTRY (state->var1_variance));
		var1 = atof(text);
		text = gtk_entry_get_text (GTK_ENTRY (state->var2_variance));
		var2 = atof(text);
		err = ztest_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
				  &range_1, &range_2, mean_diff,
				  var1, var2, alpha, &dao);
		break;
	default: 
		err = 99;
		break;
	}

	switch (err) {
	case 0: 
		gtk_widget_destroy (state->dialog);
		break;
	default: 
		text = g_strdup_printf(_("An unexpected error has occurred: %d."), err);
		error_in_entry (state->wbcg, GTK_WIDGET(state->input_entry), text);
		g_free(text);
		break;
	}
	return;
}

/**
 * ttest_known_toggled_cb:
 * @button:
 * @state:
 *
 * The paired/unpaired variables status has changed.
 *
 **/
static void
ttest_known_toggled_cb(GtkWidget *button, TTestState *state)
{
	if( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)) == 1) {
		gtk_widget_hide(state->equal_button);
		gtk_widget_hide(state->unequal_button);
		gtk_widget_hide(state->varianceequal_label);
		gtk_widget_show(state->var2_variance_label);
		gtk_widget_show(state->var2_variance);
		gtk_widget_show(state->var1_variance_label);
		gtk_widget_show(state->var1_variance);
	} else {
		gtk_widget_hide(state->var2_variance_label);
		gtk_widget_hide(state->var2_variance);
		gtk_widget_hide(state->var1_variance_label);
		gtk_widget_hide(state->var1_variance);
		gtk_widget_show(state->equal_button);
		gtk_widget_show(state->unequal_button);
		gtk_widget_show(state->varianceequal_label);
	} 
}
/**
 * ttest_paired_toggled_cb:
 * @button:
 * @state:
 *
 * The paired/unpaired variables status has changed.
 *
 **/
static void
ttest_paired_toggled_cb(GtkWidget *button, TTestState *state)
{
	if( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)) == 1) {
		gtk_widget_hide(state->var2_variance_label);
		gtk_widget_hide(state->var2_variance);
		gtk_widget_hide(state->var1_variance_label);
		gtk_widget_hide(state->var1_variance);
		gtk_widget_hide(state->equal_button);
		gtk_widget_hide(state->unequal_button);
		gtk_widget_hide(state->varianceequal_label);
		gtk_widget_hide(state->known_button);
		gtk_widget_hide(state->unknown_button);
		gtk_widget_hide(state->varianceknown_label);
	} else {
		gtk_widget_show(state->known_button);
		gtk_widget_show(state->unknown_button);
		gtk_widget_show(state->varianceknown_label);
		ttest_known_toggled_cb(GTK_WIDGET(state->known_button),state);
	} 
}

/**
 * dialog_ttest_adjust_to_invocation:
 * @state:
 *
 * Set the options to match the invocation.
 *
 **/
static void
dialog_ttest_adjust_to_invocation (TTestState *state)
{
	switch (state->invocation) {
	case TTEST_PAIRED: 
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(state->paired_button),TRUE);
		break;
	case TTEST_UNPAIRED_EQUALVARIANCES:
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(state->equal_button),TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(state->unknown_button),TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(state->unpaired_button),TRUE);
		break;
	case TTEST_UNPAIRED_UNEQUALVARIANCES:
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(state->unequal_button),TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(state->unknown_button),TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(state->unpaired_button),TRUE);
		break;
	case TTEST_ZTEST:
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(state->known_button),TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(state->unpaired_button),TRUE);
		break;
	default: 
		break;
	}
}


/**
 * dialog_ttest_realized:
 * @widget
 * @state:
 *
 * Fix the size of the options table.
 *
 **/
static void
dialog_ttest_realized (GtkWidget *widget, TTestState *state)
{
	gtk_widget_set_usize (state->options_table, 
			      state->options_table->allocation.width, 
			      state->options_table->allocation.height);
	gtk_widget_set_usize (state->paired_button, 
			      state->paired_button->allocation.width, 
			      state->paired_button->allocation.height);
	gtk_widget_set_usize (state->unpaired_button, 
			      state->unpaired_button->allocation.width, 
			      state->unpaired_button->allocation.height);
	gtk_widget_set_usize (state->variablespaired_label, 
			      state->variablespaired_label->allocation.width, 
			      state->variablespaired_label->allocation.height);
	ttest_paired_toggled_cb (state->paired_button,state);
	dialog_ttest_adjust_to_invocation (state);
}

/**
 * dialog_ttest_tool_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_ttest_tool_init (TTestState *state)
{
	if (dialog_tool_init ((GenericToolState *)state, "mean-tests.glade", "MeanTests", 
                       GTK_SIGNAL_FUNC (ttest_tool_ok_clicked_cb), 
                       GTK_SIGNAL_FUNC (tool_update_sensitivity_cb))) {
		return TRUE;
	}

	state->paired_button  = glade_xml_get_widget(state->gui, "paired-button");
	state->unpaired_button  = glade_xml_get_widget(state->gui, "unpaired-button");
	state->variablespaired_label = glade_xml_get_widget(state->gui, "variablespaired-label");
	state->known_button  = glade_xml_get_widget(state->gui, "known-button");
	state->unknown_button  = glade_xml_get_widget(state->gui, "unknown-button");
	state->varianceknown_label = glade_xml_get_widget(state->gui, "varianceknown-label");
	state->equal_button  = glade_xml_get_widget(state->gui, "equal-button");
	state->unequal_button  = glade_xml_get_widget(state->gui, "unequal-button");
	state->varianceequal_label = glade_xml_get_widget(state->gui, "varianceequal-label");
	state->options_table = glade_xml_get_widget(state->gui, "options-table");
	state->var1_variance_label = glade_xml_get_widget(state->gui, "var1_variance-label");
	state->var1_variance = glade_xml_get_widget(state->gui, "var1-variance");
	state->var2_variance_label = glade_xml_get_widget(state->gui, "var2_variance-label");
	state->var2_variance = glade_xml_get_widget(state->gui, "var2-variance");

	gtk_signal_connect_after (GTK_OBJECT (state->paired_button), "toggled",
			    GTK_SIGNAL_FUNC (tool_update_sensitivity_cb), state);
	gtk_signal_connect (GTK_OBJECT (state->paired_button), "toggled",
			    GTK_SIGNAL_FUNC (ttest_paired_toggled_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->known_button), "toggled",
			    GTK_SIGNAL_FUNC (tool_update_sensitivity_cb), state);
	gtk_signal_connect (GTK_OBJECT (state->known_button), "toggled",
			    GTK_SIGNAL_FUNC (ttest_known_toggled_cb), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "realize",
			    GTK_SIGNAL_FUNC (dialog_ttest_realized), state);
 
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       TTEST_KEY);

	tool_update_sensitivity_cb (NULL,(GenericToolState *)state);	

	return FALSE;
}


/**
 * dialog_ttest_tool:
 * @wbcg:
 * @sheet:
 * @test:
 *
 * Show the dialog (guru).
 *
 **/
static int
dialog_ttest_tool (WorkbookControlGUI *wbcg, Sheet *sheet, ttest_type test)
{
        TTestState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, TTEST_KEY)) {
                ttest_tool_state->invocation = test;                
		dialog_ttest_adjust_to_invocation(ttest_tool_state);
		return 0;
	}

	state = g_new (TTestState, 1);
	(*(ToolType *)state) = TOOL_TTEST;
	ttest_tool_state = state;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->invocation = test;
	state->helpfile = "t-test.html";

	if (dialog_ttest_tool_init (state)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Mean Tests Tool dialog."));
		g_free (state);
		return 0;
	}
	gtk_widget_show (state->dialog);

        return 0;
}

/**
 * dialog_ttest_paired_tool:
 * @wbcg:
 * @sheet:
 *
 * Call dialog_ttest_tool with appropriate selector.
 *
 **/
static int
dialog_ttest_paired_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	return dialog_ttest_tool(wbcg, sheet, TTEST_PAIRED);
}

/**
 * dialog_ttest_eq_tool:
 * @wbcg:
 * @sheet:
 *
 * Call dialog_ttest_tool with appropriate selector.
 *
 **/
static int
dialog_ttest_eq_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	return dialog_ttest_tool(wbcg, sheet, TTEST_UNPAIRED_EQUALVARIANCES);
}

/**
 * dialog_ttest_neq_tool:
 * @wbcg:
 * @sheet:
 *
 * Call dialog_ttest_tool with appropriate selector.
 *
 **/
static int
dialog_ttest_neq_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	return dialog_ttest_tool(wbcg, sheet, TTEST_UNPAIRED_UNEQUALVARIANCES);
}

/**
 * dialog_ztest_tool:
 * @wbcg:
 * @sheet:
 *
 * Call dialog_ttest_tool with appropriate selector.
 *
 **/
static int
dialog_ztest_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	return dialog_ttest_tool(wbcg, sheet, TTEST_ZTEST);
}

/**********************************************/
/*  End of ttest tool code */
/**********************************************/


/**********************************************/
/*  Begin of ftest tool code */
/**********************************************/


/**
 * ftest_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the correlation_tool.
 * Note that we assume that the ok_button is only active if the entry fields 
 * contain sensible data.
 **/
static void
ftest_tool_ok_clicked_cb(GtkWidget *button, GenericToolState *state)
{
	data_analysis_output_t  dao;
	Range range_1;
	Range range_2;
        char   *text;
	GtkWidget *w, *alpha_entry;
	gnum_float alpha;

	text = gtk_entry_get_text (GTK_ENTRY (state->input_entry));
	parse_range (text, &range_1.start.col,
			             &range_1.start.row,
			             &range_1.end.col,
				     &range_1.end.row);
	text = gtk_entry_get_text (GTK_ENTRY (state->input_entry_2));
	parse_range (text, &range_2.start.col,
			             &range_2.start.row,
			             &range_2.end.col,
				     &range_2.end.row);

        parse_output (state->wbcg, state->sheet, 
                      gnumeric_glade_group_value (state->gui, output_group),
	              GTK_WIDGET(state->output_entry), 
                      &dao);


	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	alpha_entry = glade_xml_get_widget (state->gui, "one_alpha");
	text = gtk_entry_get_text (GTK_ENTRY (alpha_entry));
	alpha = atof(text);

	switch (ftest_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
			    &range_1, &range_2,  alpha, &dao)) {
	case 0: gtk_widget_destroy (state->dialog);
		break;
	default: break;
	}
	return;
}



/**
 * dialog_ftest_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
static int
dialog_ftest_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GenericToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, FTEST_KEY))
		return 0;

	state = g_new (GenericToolState, 1);
	(*(ToolType *)state) = TOOL_FTEST;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->helpfile = "ftest-two-sample-for-variances-tool.html";

	if (dialog_tool_init (state, "variance-tests.glade", "VarianceTests", 
                       GTK_SIGNAL_FUNC (ftest_tool_ok_clicked_cb), 
                       GTK_SIGNAL_FUNC (tool_update_sensitivity_cb))) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the FTest Tool dialog."));
		g_free (state);
		return 0;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       FTEST_KEY);

	tool_update_sensitivity_cb (NULL,state);	
	gtk_widget_show (state->dialog);

        return 0;
}

/**********************************************/
/*  End of ftest tool code */
/**********************************************/

/**********************************************/
/*  Begin of sampling tool code */
/**********************************************/

/**
 * sampling_tool_update_sensitivity:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity if the only items of interest
 * are the standard input (one range) and output items.
 **/
static void
sampling_tool_update_sensitivity_cb (GtkWidget *dummy, SamplingState *state)
{
	gboolean ready  = FALSE;
	char const *output_text;
	char const *input_text;
	int i, periodic, size;
        Value *output_range;
        Value *input_range;
	char *text;

	output_text = gtk_entry_get_text (GTK_ENTRY (state->output_entry));
	input_text = gtk_entry_get_text (GTK_ENTRY (state->input_entry));
        output_range = range_parse(state->sheet,output_text,TRUE);
        input_range = range_parse(state->sheet,input_text,TRUE);
		
	i = gnumeric_glade_group_value (state->gui, output_group);
        periodic = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->periodic_button));
        
	if (periodic == 1) {
		text = gtk_entry_get_text (GTK_ENTRY (state->period_entry));
	} else {
		text = gtk_entry_get_text (GTK_ENTRY (state->random_entry));
	}
	size = atoi(text);

	ready = ((input_range != NULL) && 
		 (size > 0) &&
                 ((i != 2) || (output_range != NULL)));

        if (input_range != NULL) value_release(input_range);
        if (output_range != NULL) value_release(output_range);

	gtk_widget_set_sensitive (state->apply_button, ready);
	gtk_widget_set_sensitive (state->ok_button, ready);
}

/**
 * sampling_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the appropriate tool.
 * Note that we assume that the ok_button is only active if the entry fields 
 * contain sensible data.
 **/
static void
sampling_tool_ok_clicked_cb(GtkWidget *button, SamplingState *state)
{
	
	data_analysis_output_t  dao;
	Range range;
        char   *text;
	gint size;
	gint periodic;

	text = gtk_entry_get_text (GTK_ENTRY (state->input_entry));
	parse_range (text, &range.start.col,
			             &range.start.row,
			             &range.end.col,
				     &range.end.row);

        parse_output (state->wbcg, state->sheet, 
                      gnumeric_glade_group_value (state->gui, output_group),
	              GTK_WIDGET(state->output_entry), 
                      &dao);


        periodic = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->periodic_button));
        
	if (periodic == 1) {
		text = gtk_entry_get_text (GTK_ENTRY (state->period_entry));
	} else {
		text = gtk_entry_get_text (GTK_ENTRY (state->random_entry));
	}
	size = atoi(text);

	switch (sampling_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
			    &range, periodic,  size, &dao)) {
	case 0: 
		if (button == state->ok_button) 
			gtk_widget_destroy (state->dialog);
		break;
	default: 
		break;
	}
	return;
}

/**
 * sampling_method_toggled_cb:
 * @button:
 * @state:
 *
 * The method status has changed.
 *
 **/
static void
sampling_method_toggled_cb(GtkWidget *button, SamplingState *state)
{
	if( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)) == 1) {
		gtk_widget_hide(state->random_label);
		gtk_widget_hide(state->random_entry);
		gtk_widget_show(state->period_label);
		gtk_widget_show(state->period_entry);
	} else {
		gtk_widget_hide(state->period_label);
		gtk_widget_hide(state->period_entry);
		gtk_widget_show(state->random_label);
		gtk_widget_show(state->random_entry);
	} 
}


/**
 * dialog_sampling_realized:
 * @widget
 * @state:
 *
 * Fix the size of the options table.
 *
 **/
static void
dialog_sampling_realized (GtkWidget *widget, SamplingState *state)
{
	gtk_widget_set_usize (state->options_table, 
			      state->options_table->allocation.width, 
			      state->options_table->allocation.height);
	gtk_widget_set_usize (state->random_button, 
			      state->random_button->allocation.width, 
			      state->random_button->allocation.height);
	gtk_widget_set_usize (state->periodic_button, 
			      state->periodic_button->allocation.width, 
			      state->periodic_button->allocation.height);
	gtk_widget_set_usize (state->method_label, 
			      state->method_label->allocation.width, 
			      state->method_label->allocation.height);
	sampling_method_toggled_cb (state->periodic_button,state);
}

/**
 * dialog_sampling_tool_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_sampling_tool_init (SamplingState *state)
{
	if (dialog_tool_init ((GenericToolState *)state, "sampling.glade", "Sampling", 
                       GTK_SIGNAL_FUNC (sampling_tool_ok_clicked_cb), 
                       GTK_SIGNAL_FUNC (sampling_tool_update_sensitivity_cb))) {
		return TRUE;
	}

	state->periodic_button  = glade_xml_get_widget(state->gui, "periodic-button");
	state->random_button  = glade_xml_get_widget(state->gui, "random-button");
	state->method_label = glade_xml_get_widget(state->gui, "method-label");
	state->options_table = glade_xml_get_widget(state->gui, "options-table");
	state->period_label = glade_xml_get_widget(state->gui, "period-label");
	state->random_label = glade_xml_get_widget(state->gui, "random-label");
	state->period_entry = glade_xml_get_widget(state->gui, "period-entry");
	state->random_entry = glade_xml_get_widget(state->gui, "random-entry");

	gtk_signal_connect_after (GTK_OBJECT (state->periodic_button), "toggled",
			    GTK_SIGNAL_FUNC (sampling_tool_update_sensitivity_cb), state);
	gtk_signal_connect (GTK_OBJECT (state->periodic_button), "toggled",
			    GTK_SIGNAL_FUNC (sampling_method_toggled_cb), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "realize",
			    GTK_SIGNAL_FUNC (dialog_sampling_realized), state);
	gtk_signal_connect_after (GTK_OBJECT (state->period_entry), "changed",
			    GTK_SIGNAL_FUNC (sampling_tool_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->random_entry), "changed",
			    GTK_SIGNAL_FUNC (sampling_tool_update_sensitivity_cb), state);
 
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SAMPLING_KEY);

	tool_update_sensitivity_cb (NULL,(GenericToolState *)state);	

	return FALSE;
}


/**
 * dialog_sampling_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
static int
dialog_sampling_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        SamplingState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, SAMPLING_KEY)) {
		return 0;
	}

	state = g_new (SamplingState, 1);
	(*(ToolType *)state) = TOOL_SAMPLING;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->helpfile = "sampling-tool.html";

	if (dialog_sampling_tool_init (state)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Sampling Tool dialog."));
		g_free (state);
		return 0;
	}
	gtk_widget_show (state->dialog);

        return 0;
}
/**********************************************/
/*  End of sampling tool code */
/**********************************************/

/*************************************************************************
 *
 * Modal dialogs for the tools, without using expression entry widgets.
 * 
 * These dialogs are supposed to be changed to use the expresion entry 
 * widgets as above.
 */


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

/*************************************************************************
 *
 * Modal dialog for tool selection
 * 
 */

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
