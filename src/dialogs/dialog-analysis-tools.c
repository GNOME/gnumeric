/*
 * dialog-analysis-tools.c:
 *
 * Authors:
 *  Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *  Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <workbook.h>
#include <workbook-control.h>
#include <workbook-edit.h>
#include <workbook-view.h>
#include <gui-util.h>
#include <utils-dialog.h>
#include <parse-util.h>
#include <utils-dialog.h>
#include <format.h>
#include <tools.h>
#include <sheet.h>
#include <expr.h>
#include <number-match.h>
#include <ranges.h>
#include <selection.h>
#include <value.h>
#include <widgets/gnumeric-expr-entry.h>

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>
#include <string.h>

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
	"grouped_by_area",
	0
};


typedef enum {
	TOOL_CORRELATION = 1,       /* use GenericToolState */
	TOOL_COVARIANCE = 2,        /* use GenericToolState */
	TOOL_RANK_PERCENTILE = 3,   /* use GenericToolState */
	TOOL_HISTOGRAM = 5,   /* use GenericToolState */
	TOOL_FOURIER = 6,   /* use GenericToolState */
	TOOL_GENERIC = 10,          /* all smaller types are generic */
	TOOL_DESC_STATS = 11,
	TOOL_TTEST = 12,
	TOOL_SAMPLING = 13,
	TOOL_AVERAGE = 14,
	TOOL_REGRESSION = 15,
	TOOL_ANOVA_SINGLE = 16,
	TOOL_ANOVA_TWO_FACTOR = 17,
	TOOL_FTEST = 18,
	TOOL_RANDOM = 19,
	TOOL_EXP_SMOOTHING = 20
} ToolType;


#define CORRELATION_KEY       "analysistools-correlation-dialog"
#define COVARIANCE_KEY        "analysistools-covariance-dialog"
#define DESCRIPTIVE_STATS_KEY "analysistools-descriptive-stats-dialog"
#define RANK_PERCENTILE_KEY   "analysistools-rank-percentile-dialog"
#define TTEST_KEY             "analysistools-ttest-dialog"
#define FTEST_KEY             "analysistools-ftest-dialog"
#define SAMPLING_KEY          "analysistools-sampling-dialog"
#define HISTOGRAM_KEY         "analysistools-histogram-dialog"
#define FOURIER_KEY           "analysistools-fourier-dialog"
#define AVERAGE_KEY           "analysistools-moving-average-dialog"
#define EXP_SMOOTHING_KEY     "analysistools-exp-smoothing-dialog"
#define REGRESSION_KEY        "analysistools-regression-dialog"
#define ANOVA_TWO_FACTOR_KEY  "analysistools-anova-two-factor-dialog"
#define ANOVA_SINGLE_KEY      "analysistools-anova-single-factor-dialog"
#define RANDOM_KEY            "analysistools-random-dialog"



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
	char *help_link;
	char *input_var1_str;
	char *input_var2_str;
	GtkWidget *new_sheet;
	GtkWidget *new_workbook;
	GtkWidget *output_range;
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
	GtkAccelGroup *accel;
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
	char *help_link;
	char *input_var1_str;
	char *input_var2_str;
	GtkWidget *new_sheet;
	GtkWidget *new_workbook;
	GtkWidget *output_range;
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
	GtkAccelGroup *accel;

	GtkWidget *predetermined_button;
	GtkWidget *calculated_button;
	GtkWidget *bin_labels_button;
	GtkEntry  *n_entry;
	GtkEntry  *max_entry;
	GtkEntry  *min_entry;	
} HistogramToolState;

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
	char *help_link;
	char *input_var1_str;
	char *input_var2_str;
	GtkWidget *new_sheet;
	GtkWidget *new_workbook;
	GtkWidget *output_range;
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
	GtkAccelGroup *accel;

	GtkWidget *summary_stats_button;
	GtkWidget *mean_stats_button;
	GtkWidget *kth_largest_button;
	GtkWidget *kth_smallest_button;
	GtkWidget *c_entry;
	GtkWidget *l_entry;
	GtkWidget *s_entry;
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
	char *help_link;
	char *input_var1_str;
	char *input_var2_str;
	GtkWidget *new_sheet;
	GtkWidget *new_workbook;
	GtkWidget *output_range;
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
	GtkAccelGroup *accel;

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
	GtkWidget *mean_diff_entry;
	GtkWidget *alpha_entry;
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
	char *help_link;
	char *input_var1_str;
	char *input_var2_str;
	GtkWidget *new_sheet;
	GtkWidget *new_workbook;
	GtkWidget *output_range;
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
	GtkAccelGroup *accel;

	GtkWidget *periodic_button;
	GtkWidget *random_button;
	GtkWidget *method_label;
	GtkWidget *period_label;
	GtkWidget *random_label;
	GtkWidget *period_entry;
	GtkWidget *random_entry;
	GtkWidget *options_table;
	GtkWidget *number_entry;
} SamplingState;

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
	char *help_link;
	char *input_var1_str;
	char *input_var2_str;
	GtkWidget *new_sheet;
	GtkWidget *new_workbook;
	GtkWidget *output_range;
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
	GtkAccelGroup *accel;

	GtkWidget *interval_entry;
} AverageToolState;

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
        char *help_link;
        char *input_var1_str;
        char *input_var2_str;
        GtkWidget *new_sheet;
        GtkWidget *new_workbook;
        GtkWidget *output_range;
        Sheet  *sheet;
        Workbook  *wb;
        WorkbookControlGUI  *wbcg;
        GtkAccelGroup *accel;

        GtkWidget *damping_fact_entry;
} ExpSmoothToolState;

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
	char *help_link;
	char *input_var1_str;
	char *input_var2_str;
	GtkWidget *new_sheet;
	GtkWidget *new_workbook;
	GtkWidget *output_range;
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
	GtkAccelGroup *accel;

	GtkWidget *confidence_entry;
} RegressionToolState;

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
	char *help_link;
	char *input_var1_str;
	char *input_var2_str;
	GtkWidget *new_sheet;
	GtkWidget *new_workbook;
	GtkWidget *output_range;
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
	GtkAccelGroup *accel;

	GtkWidget *alpha_entry;
} AnovaSingleToolState;
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
	char *help_link;
	char *input_var1_str;
	char *input_var2_str;
	GtkWidget *new_sheet;
	GtkWidget *new_workbook;
	GtkWidget *output_range;
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
	GtkAccelGroup *accel;

	GtkWidget *alpha_entry;
	GtkWidget *replication_entry;
} AnovaTwoFactorToolState;

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
	char *help_link;
	char *input_var1_str;
	char *input_var2_str;
	GtkWidget *new_sheet;
	GtkWidget *new_workbook;
	GtkWidget *output_range;
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
	GtkAccelGroup *accel;

	GtkWidget *alpha_entry;
} FTestToolState;

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
	char *help_link;
	char *input_var1_str;
	char *input_var2_str;
	GtkWidget *new_sheet;
	GtkWidget *new_workbook;
	GtkWidget *output_range;
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
	GtkAccelGroup *accel;

	GtkWidget *distribution_table;
        GtkWidget *distribution_combo;
	GtkWidget *par1_label;
	GtkWidget *par1_entry;
	GtkWidget *par1_expr_entry;
	GtkWidget *par2_label;
	GtkWidget *par2_entry;
	GtkWidget *vars_entry;
	GtkWidget *count_entry;
	GtkAccelGroup *distribution_accel;
	random_distribution_t distribution;
} RandomToolState;

typedef union {
	ToolType  const type;
	GenericToolState tt_generic;
	HistogramToolState tt_histogram;
	DescriptiveStatState tt_desc_stat;
	TTestState tt_ttest;
	SamplingState tt_sampling;
	AverageToolState tt_average;
	RegressionToolState tt_regression;
	AnovaSingleToolState tt_anova_single;
	AnovaTwoFactorToolState tt_anova_two_factor;
	RandomToolState tt_random;
} ToolState;

/**********************************************/
/*  Generic functions for the analysis tools. */
/*  Functions in this section are being used  */
/*  by virtually all tools.                   */
/**********************************************/

/**
 * entry_to_float:
 * @entry:
 * @the_float:
 * update:
 *
 * retrieve a float from an entry field parsing all reasonable formats
 * 
  **/
static int
entry_to_float (GtkEntry *entry, gnum_float *the_float, gboolean update)
{
	char const  *text      = NULL;
	Value       *value     = NULL;
	StyleFormat *format    = NULL;

	text = gtk_entry_get_text (entry);
	value = format_match_number (text, NULL, &format);

	if ((value == NULL) || !VALUE_IS_NUMBER (value)) { 
		*the_float = 0.0;
		return 1;
	}
	*the_float = value_get_as_float (value);
	if (update) {
		char *tmp = format_value (format, value, NULL, 16);
		gtk_entry_set_text (entry, tmp);
		g_free (tmp);	
	}
	
	value_release (value);
	return 0;
}

/**
 * entry_to_int:
 * @entry:
 * @the_int:
 * update:
 *
 * retrieve an int from an entry field parsing all reasonable formats
 * 
  **/
static int
entry_to_int (GtkEntry *entry, gint *the_int, gboolean update)
{
	char const *text      = NULL;
	Value       *value     = NULL;
	StyleFormat *format    = NULL;

	text = gtk_entry_get_text (entry);
	value = format_match_number (text, NULL, &format);

	if ((value == NULL) || !(value->type == VALUE_INTEGER)) { 
		*the_int = 0;
		return 1;
	}
	*the_int = value_get_as_int (value);
	if (update) {
		char *tmp = format_value (format, value, NULL, 16);
		gtk_entry_set_text (entry, tmp);
		g_free (tmp);	
	}
	
	value_release (value);
	return 0;
}

/**
 * float_to_entry:
 * @entry:
 * @the_float:
 *
 * 
  **/
static void
float_to_entry (GtkEntry *entry, gnum_float the_float) {
	char        *text      = NULL;
	Value       *val = NULL;
       
	val = value_new_float(the_float);
	text = format_value (NULL, val, NULL, 16);
	if (text) {
		gtk_entry_set_text (entry, text);
		g_free (text);	
	}
	if (val)
		value_release(val);
	return;
}

/**
 * int_to_entry:
 * @entry:
 * @the_float:
 *
 * 
  **/
static void
int_to_entry (GtkEntry *entry, gint the_int) {
	char        *text      = NULL;
	Value       *val = NULL;
       
	val = value_new_int(the_int);
	text = format_value (NULL, val, NULL, 16);
	if (text) {
		gtk_entry_set_text (entry, text);
		g_free (text);	
	}
	if (val)
		value_release(val);
	return;
}


/**
 * gnumeric_expr_entry_parse_to_value:
 *
 * @ee: GnumericExprEntry
 * @sheet: the sheet where the cell range is evaluated. This really only needed if
 *         the range given does not include a sheet specification.
 *
 * Returns a (Value *) of type VALUE_CELLRANGE if the @range was
 * succesfully parsed or NULL on failure.
 */
static Value *
gnumeric_expr_entry_parse_to_value (GnumericExprEntry *ee, Sheet *sheet)
{
	char const *str = gtk_entry_get_text (GTK_ENTRY (ee));;
	return global_range_parse (sheet, str);
}

/**
 * gnumeric_expr_entry_parse_to_list:
 *
 * @ee: GnumericExprEntry
 * @sheet: the sheet where the cell range is evaluated. This really only needed if
 *         the range given does not include a sheet specification.
 *
 * Returns a (GSList *) 
 * or NULL on failure.
 */
static GSList *
gnumeric_expr_entry_parse_to_list (GnumericExprEntry *ee, Sheet *sheet)
{
	char const *str = gtk_entry_get_text (GTK_ENTRY (ee));;
	return global_range_list_parse (sheet, str);
}

/**
 * error_in_entry:
 *
 * @wbcg:
 * @entry:
 * @err_str: 
 *
 * Show an error dialog and select corresponding entry 
 */
static void
error_in_entry (WorkbookControlGUI *wbcg, GtkWidget *entry, const char *err_str)
{
        gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR, err_str);

	gtk_widget_grab_focus (entry);
	gtk_entry_set_position (GTK_ENTRY (entry), 0);
	gtk_entry_select_region (GTK_ENTRY (entry), 0,
				 GTK_ENTRY (entry)->text_length);
}

/**
 * parse_output:
 *
 * @state:
 * @dao:
 *
 * fill dao with information fromm dialog
 */
static int
parse_output (GenericToolState *state, data_analysis_output_t *dao)
{
        Value *output_range;
	GtkWidget *autofitbutton;

	dao->start_col = 0;
	dao->start_row = 0;
	dao->cols = 0;
	dao->rows = 0;
	dao->sheet = NULL;

	switch (gnumeric_glade_group_value (state->gui, output_group)) {
	case 0:
	default:
	        dao->type = NewSheetOutput;
		break;
	case 1:
	        dao->type = NewWorkbookOutput;
		break;
	case 2:
		output_range = gnumeric_expr_entry_parse_to_value 
			(GNUMERIC_EXPR_ENTRY (state->output_entry), state->sheet);
		g_return_val_if_fail (output_range != NULL, 1);
		g_return_val_if_fail (output_range->type == VALUE_CELLRANGE, 1);

	        dao->type = RangeOutput;
		dao->start_col = output_range->v_range.cell.a.col;
		dao->start_row = output_range->v_range.cell.a.row;
		dao->cols = output_range->v_range.cell.b.col
			- output_range->v_range.cell.a.col + 1;
		dao->rows = output_range->v_range.cell.b.row
			- output_range->v_range.cell.a.row + 1;
		dao->sheet = output_range->v_range.cell.a.sheet;

		value_release (output_range);
		break;
	}

	autofitbutton = glade_xml_get_widget (state->gui, "autofit_button");
	
	if (autofitbutton != NULL) {
		dao->autofit_flag = gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (autofitbutton));
	} else {
		dao->autofit_flag = TRUE;
	}

	return 0;
}

/**
 * focus_on_entry:
 * @widget:
 * @entry:
 *
 * callback to focus on an entry when the output
 * range button is pressed.
 *
 **/
static void
focus_on_entry (GtkWidget *widget, GtkWidget *entry)
{
        if (GTK_TOGGLE_BUTTON (widget)->active)
		gtk_widget_grab_focus (entry);
}


static void
tool_help_cb (GtkWidget *button, GenericToolState *state)
{
	gnumeric_help_display (state->help_link);
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

	if (state->accel != NULL) {
		gtk_accel_group_unref (state->accel);
		state->accel = NULL;
	}

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
cb_tool_cancel_clicked (GtkWidget *button, GenericToolState *state)
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
	    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->output_range), TRUE);
}

/**
 * dialog_tool_init_outputs:
 * @state:
 * @sensitivity_cb:
 *
 * Setup the output range.
 *
 **/
static void
dialog_tool_init_outputs (GenericToolState *state, GtkSignalFunc sensitivity_cb)
{
	GtkTable *table;

	state->new_sheet  = glade_xml_get_widget (state->gui, "newsheet-button");
	state->new_workbook  = glade_xml_get_widget (state->gui, "newworkbook-button");
	state->output_range  = glade_xml_get_widget (state->gui, "outputrange-button");

	table = GTK_TABLE (glade_xml_get_widget (state->gui, "output-table"));
	state->output_entry = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags (state->output_entry,
                                      GNUM_EE_SINGLE_RANGE,
                                      GNUM_EE_MASK);
        gnumeric_expr_entry_set_scg (state->output_entry, 
				     wb_control_gui_cur_sheet (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->output_entry),
			  1, 2, 2, 3,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gtk_signal_connect (GTK_OBJECT (state->output_range),   "toggled",
			    GTK_SIGNAL_FUNC (focus_on_entry),
			    state->output_entry);
	gtk_signal_connect (GTK_OBJECT (state->output_entry), "focus-in-event",
			    GTK_SIGNAL_FUNC (tool_set_focus_output_range), state);
	gtk_signal_connect_after (GTK_OBJECT (state->output_entry), "changed",
				  GTK_SIGNAL_FUNC (sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->output_range), "toggled",
				  GTK_SIGNAL_FUNC (sensitivity_cb), state);

 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->output_entry));
	gtk_widget_show (GTK_WIDGET (state->output_entry));

	return;
}

/**
 * dialog_tool_init_buttons:
 * @state:
 * @ok_function:
 *
 * Setup the buttons
 *
 **/
static void
dialog_tool_init_buttons (GenericToolState *state, GtkSignalFunc ok_function)
{
	state->ok_button = glade_xml_get_widget (state->gui, "okbutton");
	gtk_signal_connect (GTK_OBJECT (state->ok_button), "clicked",
			    ok_function,
			    state);

	state->cancel_button = glade_xml_get_widget (state->gui, "cancelbutton");
	gtk_signal_connect (GTK_OBJECT (state->cancel_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_tool_cancel_clicked),
			    state);
	state->apply_button = glade_xml_get_widget (state->gui, "applybutton");
	if (state->apply_button != NULL )
		gtk_signal_connect (GTK_OBJECT (state->apply_button), "clicked",
				    ok_function, state);
	state->help_button = glade_xml_get_widget (state->gui, "helpbutton");
	if (state->help_button != NULL )
		gtk_signal_connect (GTK_OBJECT (state->help_button), "clicked",
				    GTK_SIGNAL_FUNC (tool_help_cb), state);
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
		  GtkSignalFunc ok_function, GtkSignalFunc sensitivity_cb,
		  GnumericExprEntryFlags flags)
{
	GtkTable *table;
	GtkWidget *widget;
	gint key;

	state->gui = gnumeric_glade_xml_new (state->wbcg, gui_name);
        if (state->gui == NULL)
                return TRUE;

	state->dialog = glade_xml_get_widget (state->gui, dialog_name);
        if (state->dialog == NULL)
                return TRUE;

	state->accel = gtk_accel_group_new ();

	dialog_tool_init_buttons (state, ok_function);

	table = GTK_TABLE (glade_xml_get_widget (state->gui, "input-table"));
	state->input_entry = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags (state->input_entry, flags, GNUM_EE_MASK);
        gnumeric_expr_entry_set_scg (state->input_entry, 
				     wb_control_gui_cur_sheet (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->input_entry),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gtk_signal_connect_after (GTK_OBJECT (state->input_entry), "changed",
				  GTK_SIGNAL_FUNC (sensitivity_cb), state);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->input_entry));
	if (state->input_var1_str == NULL) {
		state->input_var1_str = _("_Input Range:");
	}
	widget = glade_xml_get_widget (state->gui, "var1-label");
	key = gtk_label_parse_uline (GTK_LABEL (widget), state->input_var1_str);
	if (key != GDK_VoidSymbol)
		gtk_widget_add_accelerator (GTK_WIDGET (state->input_entry),
					    "grab_focus",
					    state->accel, key,
					    GDK_MOD1_MASK, 0);
	gtk_widget_show (GTK_WIDGET (state->input_entry));


/*                                                        */
/* If there is a var2-label, we need a second input field */
/*                                                        */
	widget = glade_xml_get_widget (state->gui, "var2-label");
	if (widget == NULL) {
		state->input_entry_2 = NULL;
	} else {
		state->input_entry_2 = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new 
							    (state->wbcg));
		gnumeric_expr_entry_set_flags (state->input_entry_2, flags, GNUM_EE_MASK);
		gnumeric_expr_entry_set_scg (state->input_entry_2,
					     wb_control_gui_cur_sheet (state->wbcg));
		gtk_table_attach (table, GTK_WIDGET (state->input_entry_2),
			  1, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
		gnumeric_editable_enters (GTK_WINDOW (state->dialog),
					  GTK_EDITABLE (state->input_entry_2));
		gtk_signal_connect_after (GTK_OBJECT (state->input_entry_2), "changed",
					  GTK_SIGNAL_FUNC (sensitivity_cb), state);
		if (state->input_var2_str != NULL) {
			key = gtk_label_parse_uline (GTK_LABEL (widget), state->input_var2_str);
			if (key != GDK_VoidSymbol)
				gtk_widget_add_accelerator (GTK_WIDGET (state->input_entry_2),
							    "grab_focus",
							    state->accel, key,
							    GDK_MOD1_MASK, 0);
		}
		gtk_widget_show (GTK_WIDGET (state->input_entry_2));
	}

	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "set-focus",
			    GTK_SIGNAL_FUNC (tool_set_focus), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (tool_destroy), state);

	dialog_tool_init_outputs (state, sensitivity_cb);

	gtk_window_add_accel_group (GTK_WINDOW (state->dialog),
				    state->accel);

	return FALSE;
}

/**********************************************/
/*  Generic functions for the analysis tools  */
/*  Functions in this section are being used  */
/*  some tools                                */
/**********************************************/

/**
 * tool_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity if the only items of interest
 * are the standard input (one or two ranges) and output items.
 *
 *
 **/
static void
tool_update_sensitivity_cb (GtkWidget *dummy, GenericToolState *state)
{
	gboolean ready  = FALSE;
	gboolean input_1_ready  = FALSE;
	gboolean input_2_ready  = FALSE;
	gboolean output_ready  = FALSE;

	int i;
        Value *output_range;
        Value *input_range;
        Value *input_range_2;

	output_range = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->output_entry), state->sheet);
        input_range = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);
	if (state->input_entry_2 != NULL) {
		input_range_2 = gnumeric_expr_entry_parse_to_value 
			(GNUMERIC_EXPR_ENTRY (state->input_entry_2), state->sheet); 
	} else {
		input_range_2 = NULL;
	}
		
	i = gnumeric_glade_group_value (state->gui, output_group);

	input_1_ready = (input_range != NULL);
	input_2_ready = ((state->input_entry_2 == NULL) || (input_range_2 != NULL));
	output_ready =  ((i != 2) || (output_range != NULL));

        if (input_range != NULL) value_release (input_range);
        if (input_range_2 != NULL) value_release (input_range_2);
        if (output_range != NULL) value_release (output_range);

	ready = input_1_ready && input_2_ready && output_ready;
	if (state->apply_button != NULL)
		gtk_widget_set_sensitive (state->apply_button, ready);
	gtk_widget_set_sensitive (state->ok_button, ready);

	return;
}

/**
 * tool_update_sensitivity_multiple_areas_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity if the only items of interest
 * are one or two standard input and and one output item, permitting multiple 
 * areas as first input.
 **/
static void
tool_update_sensitivity_multiple_areas_cb (GtkWidget *dummy, GenericToolState *state)
{
	gboolean ready  = FALSE;
	gboolean input_1_ready  = FALSE;
	gboolean input_2_ready  = FALSE;
	gboolean output_ready  = FALSE;

	int i;
        Value *output_range;
        GSList *input_range;
        Value *input_range_2;

        output_range = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->output_entry), state->sheet);
        input_range = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

	if (state->input_entry_2 != NULL) {
		input_range_2 =  gnumeric_expr_entry_parse_to_value 
			(GNUMERIC_EXPR_ENTRY (state->input_entry_2), state->sheet);
	} else {
		input_range_2 = NULL;
	}

	i = gnumeric_glade_group_value (state->gui, output_group);

	input_1_ready = (input_range != NULL);
	input_2_ready = ((state->input_entry_2 == NULL) || (input_range_2 != NULL));
	output_ready =  ((i != 2) || (output_range != NULL));

        if (input_range != NULL) range_list_destroy (input_range);
        if (input_range_2 != NULL) value_release (input_range_2);
        if (output_range != NULL) value_release (output_range);

	ready = input_1_ready && input_2_ready && output_ready;
	if (state->apply_button != NULL)
		gtk_widget_set_sensitive (state->apply_button, ready);
	gtk_widget_set_sensitive (state->ok_button, ready);

	return;
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
corr_tool_ok_clicked_cb (GtkWidget *button, GenericToolState *state)
{
	data_analysis_output_t  dao;
        char   *text;
	GtkWidget *w;
	GSList *input;
	gint err;

	input = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

        parse_output (state, &dao);

	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = correlation_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet, input,
				gnumeric_glade_group_value (state->gui, grouped_by_group),
				&dao);
	switch (err) {
	case 0: gtk_widget_destroy (state->dialog);
		break;
	case 1:
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry),
				_("The selected input rows must have equal size!"));
		break;
	case 2:
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry),
				_("The selected input columns must have equal size!"));
		break;
	case 3:
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry),
				_("The selected input areas must have equal size!"));
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: %d."), err);
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry), text);
		g_free (text);
		break;
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
	state->help_link = "correlation-tool.html";
	state->input_var1_str = NULL;
	state->input_var2_str = NULL;

	if (dialog_tool_init (state, "correlation.glade", "Correlation",
			      GTK_SIGNAL_FUNC (corr_tool_ok_clicked_cb),
			      GTK_SIGNAL_FUNC (tool_update_sensitivity_multiple_areas_cb),
			      0)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Correlation Tool dialog."));
		g_free (state);
		return 0;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       CORRELATION_KEY);

	tool_update_sensitivity_multiple_areas_cb (NULL, state);
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
cov_tool_ok_clicked_cb (GtkWidget *button, GenericToolState *state)
{
	data_analysis_output_t  dao;
        char   *text;
	GtkWidget *w;
	GSList *input;
	gint err;

	input = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

        parse_output (state, &dao);


	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	
	err = covariance_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet, input,
				 gnumeric_glade_group_value (state->gui, grouped_by_group),
			       &dao);
	switch (err) {
	case 0: gtk_widget_destroy (state->dialog);
		break;
	case 1:
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry),
				_("The selected input rows must have equal size!"));
		break;
	case 2:
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry),
				_("The selected input columns must have equal size!"));
		break;
	case 3:
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry),
				_("The selected input areas must have equal size!"));
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: %d."), err);
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry), text);
		g_free (text);
		break;
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
	state->help_link = "covariance-tool.html";
	state->input_var1_str = NULL;
	state->input_var2_str = NULL;

	if (dialog_tool_init (state, "covariance.glade", "Covariance",
			      GTK_SIGNAL_FUNC (cov_tool_ok_clicked_cb),
			      GTK_SIGNAL_FUNC (tool_update_sensitivity_multiple_areas_cb),
			      0)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Covariance Tool dialog."));
		g_free (state);
		return 0;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       COVARIANCE_KEY);

	tool_update_sensitivity_multiple_areas_cb (NULL, state);
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
cb_desc_stat_tool_ok_clicked (GtkWidget *button, DescriptiveStatState *state)
{
	data_analysis_output_t  dao;
	descriptive_stat_tool_t dst;
        char   *text;
	GtkWidget *w;	
	GSList *input;
	int err;

	input = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

	dst.summary_statistics = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->summary_stats_button));
	dst.confidence_level = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->mean_stats_button));
	dst.kth_largest = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->kth_largest_button));
	dst.kth_smallest = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->kth_smallest_button));

	if (dst.confidence_level == 1)
		err = entry_to_float (GTK_ENTRY (state->c_entry), &dst.c_level, TRUE);
	if (dst.kth_largest == 1)
		err = entry_to_int (GTK_ENTRY (state->l_entry), &dst.k_largest, TRUE);
	if (dst.kth_smallest == 1)
		err = entry_to_int (GTK_ENTRY (state->s_entry), &dst.k_smallest, TRUE);
      
        parse_output ((GenericToolState *)state, &dao);

	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = descriptive_stat_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet, input,
				   gnumeric_glade_group_value (state->gui, grouped_by_group),
				     &dst, &dao);
	switch (err) {
	case 0:
		gtk_widget_destroy (state->dialog);
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: %d."), err);
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry), text);
		g_free (text);
		break;
	}
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
	int i, j, an_int;
	gnum_float a_float;
        Value *output_range;
        GSList *input_range;

        output_range = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->output_entry), state->sheet);
        input_range = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);
	i = gnumeric_glade_group_value (state->gui, output_group);
	j = gnumeric_glade_group_value (state->gui, stats_group);

	ready = ((input_range != NULL) &&
                 (j > -1) &&
		 (gtk_toggle_button_get_active (
			 GTK_TOGGLE_BUTTON (state->mean_stats_button)) == 0 ||
			 (0 == entry_to_float (GTK_ENTRY (state->c_entry), &a_float, FALSE) && 
				 a_float > 0 && a_float < 1)) &&
		 (gtk_toggle_button_get_active (
			 GTK_TOGGLE_BUTTON (state->kth_largest_button)) == 0 ||
			 (0 == entry_to_int (GTK_ENTRY (state->l_entry), &an_int, FALSE) && 
				 an_int > 0)) &&
		 (gtk_toggle_button_get_active (
			 GTK_TOGGLE_BUTTON (state->kth_smallest_button)) == 0 ||
			 (0 == entry_to_int (GTK_ENTRY (state->s_entry), &an_int, FALSE) && 
				 an_int > 0)) &&
                 ((i != 2) || (output_range != NULL)));

        if (input_range != NULL) range_list_destroy (input_range);
        if (output_range != NULL) value_release (output_range);

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
			      GTK_SIGNAL_FUNC (desc_stat_tool_update_sensitivity_cb),
			      0)) {
		return TRUE;
	}

	state->summary_stats_button  = glade_xml_get_widget (state->gui, "summary_stats_button");
	state->mean_stats_button  = glade_xml_get_widget (state->gui, "mean_stats_button");
	state->kth_largest_button  = glade_xml_get_widget (state->gui, "kth_largest_button");
	state->kth_smallest_button  = glade_xml_get_widget (state->gui, "kth_smallest_button");
	state->c_entry  = glade_xml_get_widget (state->gui, "c_entry");
	float_to_entry (GTK_ENTRY (state->c_entry), 0.95);
	state->l_entry  = glade_xml_get_widget (state->gui, "l_entry");
	int_to_entry (GTK_ENTRY (state->l_entry), 1);
	state->s_entry  = glade_xml_get_widget (state->gui, "s_entry");
	int_to_entry (GTK_ENTRY (state->s_entry), 1);


	gtk_signal_connect_after (GTK_OBJECT (state->summary_stats_button), "toggled",
				  GTK_SIGNAL_FUNC (desc_stat_tool_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->mean_stats_button), "toggled",
				  GTK_SIGNAL_FUNC (desc_stat_tool_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->kth_largest_button), "toggled",
				  GTK_SIGNAL_FUNC (desc_stat_tool_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->kth_smallest_button), "toggled",
				  GTK_SIGNAL_FUNC (desc_stat_tool_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->c_entry), "changed",
				  GTK_SIGNAL_FUNC (desc_stat_tool_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->l_entry), "changed",
				  GTK_SIGNAL_FUNC (desc_stat_tool_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->s_entry), "changed",
				  GTK_SIGNAL_FUNC (desc_stat_tool_update_sensitivity_cb), state);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->c_entry));
  	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->l_entry));
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->s_entry));

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       DESCRIPTIVE_STATS_KEY);

	desc_stat_tool_update_sensitivity_cb (NULL, state);

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
	state->help_link = "descriptive-statistics-tool.html";
	state->input_var1_str = NULL;
	state->input_var2_str = NULL;

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
rank_tool_ok_clicked_cb (GtkWidget *button, GenericToolState *state)
{
	data_analysis_output_t  dao;
        char   *text;
	GtkWidget *w;
	GSList *input;
	gint err;
	gboolean av_ties_flag;

	input = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

        parse_output (state, &dao);

	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	w = glade_xml_get_widget (state->gui, "rank_button");
        av_ties_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = ranking_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet, input,
			    gnumeric_glade_group_value (state->gui, grouped_by_group),
			    av_ties_flag, &dao);
	switch (err) {
	case 0: gtk_widget_destroy (state->dialog);
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: %d."), err);
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry), text);
		g_free (text);
		break;
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
	state->help_link = "rank-and-percentile-tool.html";
	state->input_var1_str = NULL;
	state->input_var2_str = NULL;

	if (dialog_tool_init (state, "rank.glade", "RankPercentile",
			      GTK_SIGNAL_FUNC (rank_tool_ok_clicked_cb),
			      GTK_SIGNAL_FUNC (tool_update_sensitivity_multiple_areas_cb),
			      0)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Rank and  Percentile Tools dialog."));
		g_free (state);
		return 0;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       RANK_PERCENTILE_KEY);

	tool_update_sensitivity_multiple_areas_cb (NULL, state);
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
ttest_tool_ok_clicked_cb (GtkWidget *button, TTestState *state)
{
	data_analysis_output_t  dao;
	Value *range_1;
	Value *range_2;
        char   *text;
	GtkWidget *w;
	int    err = 0;
	gnum_float alpha, mean_diff, var1, var2;

	range_1 = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

	range_2 = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->input_entry_2), state->sheet);

        parse_output ((GenericToolState *)state, &dao);

	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->paired_button)) == 1) {
		state->invocation = TTEST_PAIRED;
	} else {
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->known_button)) == 1) {
			state->invocation = TTEST_ZTEST;
		} else {
			if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON 
							  (state->equal_button)) == 1) {
				state->invocation = TTEST_UNPAIRED_EQUALVARIANCES;
			} else {
				state->invocation = TTEST_UNPAIRED_UNEQUALVARIANCES;
			}
		}
	}

	err = entry_to_float (GTK_ENTRY (state->mean_diff_entry), &mean_diff, TRUE);
	err = entry_to_float (GTK_ENTRY (state->alpha_entry), &alpha, TRUE);

	switch (state->invocation) {
	case TTEST_PAIRED:
		err = ttest_paired_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
			       range_1, range_2,
					 mean_diff, alpha, &dao);
		break;
	case TTEST_UNPAIRED_EQUALVARIANCES:
		err = ttest_eq_var_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
					 range_1, range_2,
					 mean_diff, alpha, &dao);
		break;
	case TTEST_UNPAIRED_UNEQUALVARIANCES:
		err = ttest_neq_var_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
					  range_1, range_2,
					  mean_diff, alpha, &dao);
		break;
	case TTEST_ZTEST:
		err = entry_to_float (GTK_ENTRY (state->var1_variance), &var1, TRUE);
		if (err != 0 || var1 <= 0.0) {
			error_in_entry (state->wbcg, GTK_WIDGET (state->var1_variance),
					_("Please enter a valid\n"
					  "population variance for variable 1."));
			return;
		}
		err = entry_to_float (GTK_ENTRY (state->var2_variance), &var2, TRUE);
		if (err != 0 || var2 <= 0.0) {
			error_in_entry (state->wbcg, GTK_WIDGET (state->var2_variance),
					_("Please enter a valid\n"
					  "population variance for variable 2."));
			return;
		}
		
		err = ztest_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
				  range_1, range_2, mean_diff,
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
	case 1:
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry),
				_("The two input ranges must have the same size."));
		
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: %d."), err);
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry), text);
		g_free (text);
		break;
	}
	return;
}

/**
 * ttest_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity if the only items of interest
 * are the standard input (one or two ranges) and output items.
 **/
static void
ttest_update_sensitivity_cb (GtkWidget *dummy, TTestState *state)
{
	gboolean ready  = FALSE;
	gboolean input_1_ready  = FALSE;
	gboolean input_2_ready  = FALSE;
	gboolean output_ready  = FALSE;
	gboolean mean_diff_ready = FALSE;
	gboolean alpha_ready = FALSE;
	int i, err;
	gnum_float mean_diff, alpha;
        Value *output_range;
        Value *input_range;
        Value *input_range_2;

	output_range = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->output_entry), state->sheet);
        input_range = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);
	input_range_2 = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->input_entry_2), state->sheet); 
		
	i = gnumeric_glade_group_value (state->gui, output_group);
	err = entry_to_float (GTK_ENTRY (state->mean_diff_entry), &mean_diff, FALSE);
	mean_diff_ready = (err == 0);
	err = entry_to_float (GTK_ENTRY (state->alpha_entry), &alpha, FALSE);
	alpha_ready = (err == 0 && alpha > 0.0 && alpha < 1.0);
	input_1_ready = (input_range != NULL);
	input_2_ready = ((state->input_entry_2 == NULL) || (input_range_2 != NULL));
	output_ready =  ((i != 2) || (output_range != NULL));

        if (input_range != NULL) value_release (input_range);
        if (input_range_2 != NULL) value_release (input_range_2);
        if (output_range != NULL) value_release (output_range);

	ready = input_1_ready && input_2_ready && output_ready && alpha_ready && mean_diff_ready;
	gtk_widget_set_sensitive (state->ok_button, ready);

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
ttest_known_toggled_cb (GtkWidget *button, TTestState *state)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)) == 1) {
		gtk_widget_hide (state->equal_button);
		gtk_widget_hide (state->unequal_button);
		gtk_widget_hide (state->varianceequal_label);
		gtk_widget_show (state->var2_variance_label);
		gtk_widget_show (state->var2_variance);
		gtk_widget_show (state->var1_variance_label);
		gtk_widget_show (state->var1_variance);
	} else {
		gtk_widget_hide (state->var2_variance_label);
		gtk_widget_hide (state->var2_variance);
		gtk_widget_hide (state->var1_variance_label);
		gtk_widget_hide (state->var1_variance);
		gtk_widget_show (state->equal_button);
		gtk_widget_show (state->unequal_button);
		gtk_widget_show (state->varianceequal_label);
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
ttest_paired_toggled_cb (GtkWidget *button, TTestState *state)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)) == 1) {
		gtk_widget_hide (state->var2_variance_label);
		gtk_widget_hide (state->var2_variance);
		gtk_widget_hide (state->var1_variance_label);
		gtk_widget_hide (state->var1_variance);
		gtk_widget_hide (state->equal_button);
		gtk_widget_hide (state->unequal_button);
		gtk_widget_hide (state->varianceequal_label);
		gtk_widget_hide (state->known_button);
		gtk_widget_hide (state->unknown_button);
		gtk_widget_hide (state->varianceknown_label);
	} else {
		gtk_widget_show (state->known_button);
		gtk_widget_show (state->unknown_button);
		gtk_widget_show (state->varianceknown_label);
		ttest_known_toggled_cb (GTK_WIDGET (state->known_button), state);
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
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->paired_button), TRUE);
		break;
	case TTEST_UNPAIRED_EQUALVARIANCES:
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->equal_button), TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->unknown_button), TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->unpaired_button), TRUE);
		break;
	case TTEST_UNPAIRED_UNEQUALVARIANCES:
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->unequal_button), TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->unknown_button), TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->unpaired_button), TRUE);
		break;
	case TTEST_ZTEST:
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->known_button), TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->unpaired_button), TRUE);
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
	ttest_paired_toggled_cb (state->paired_button, state);
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
			      GTK_SIGNAL_FUNC (ttest_update_sensitivity_cb),
			      GNUM_EE_SINGLE_RANGE)) {
		return TRUE;
	}

	state->paired_button  = glade_xml_get_widget (state->gui, "paired-button");
	state->unpaired_button  = glade_xml_get_widget (state->gui, "unpaired-button");
	state->variablespaired_label = glade_xml_get_widget (state->gui, "variablespaired-label");
	state->known_button  = glade_xml_get_widget (state->gui, "known-button");
	state->unknown_button  = glade_xml_get_widget (state->gui, "unknown-button");
	state->varianceknown_label = glade_xml_get_widget (state->gui, "varianceknown-label");
	state->equal_button  = glade_xml_get_widget (state->gui, "equal-button");
	state->unequal_button  = glade_xml_get_widget (state->gui, "unequal-button");
	state->varianceequal_label = glade_xml_get_widget (state->gui, "varianceequal-label");
	state->options_table = glade_xml_get_widget (state->gui, "options-table");
	state->var1_variance_label = glade_xml_get_widget (state->gui, "var1_variance-label");
	state->var1_variance = glade_xml_get_widget (state->gui, "var1-variance");
	state->var2_variance_label = glade_xml_get_widget (state->gui, "var2_variance-label");
	state->var2_variance = glade_xml_get_widget (state->gui, "var2-variance");
	state->mean_diff_entry = glade_xml_get_widget (state->gui, "meandiff");
	float_to_entry (GTK_ENTRY (state->mean_diff_entry), 0);
	state->alpha_entry = glade_xml_get_widget (state->gui, "one_alpha");
	float_to_entry (GTK_ENTRY (state->alpha_entry), 0.05);

	gtk_signal_connect_after (GTK_OBJECT (state->paired_button), "toggled",
				  GTK_SIGNAL_FUNC (ttest_update_sensitivity_cb), state);
	gtk_signal_connect (GTK_OBJECT (state->paired_button), "toggled",
			    GTK_SIGNAL_FUNC (ttest_paired_toggled_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->known_button), "toggled",
				  GTK_SIGNAL_FUNC (ttest_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->mean_diff_entry), "changed",
				  GTK_SIGNAL_FUNC (ttest_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->alpha_entry), "changed",
				  GTK_SIGNAL_FUNC (ttest_update_sensitivity_cb), state);
	gtk_signal_connect (GTK_OBJECT (state->known_button), "toggled",
			    GTK_SIGNAL_FUNC (ttest_known_toggled_cb), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "realize",
			    GTK_SIGNAL_FUNC (dialog_ttest_realized), state);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->var1_variance));
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->var2_variance));
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->mean_diff_entry));
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->alpha_entry));


	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       TTEST_KEY);

	ttest_update_sensitivity_cb (NULL, state);

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
		dialog_ttest_adjust_to_invocation (ttest_tool_state);
		return 0;
	}

	state = g_new (TTestState, 1);
	(*(ToolType *)state) = TOOL_TTEST;
	ttest_tool_state = state;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->invocation = test;
	state->help_link = "t-test.html";
	state->input_var1_str = _("Var_iable 1 Range:");
	state->input_var2_str = _("_Variable 2 Range:");;

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
	return dialog_ttest_tool (wbcg, sheet, TTEST_PAIRED);
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
	return dialog_ttest_tool (wbcg, sheet, TTEST_UNPAIRED_EQUALVARIANCES);
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
	return dialog_ttest_tool (wbcg, sheet, TTEST_UNPAIRED_UNEQUALVARIANCES);
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
	return dialog_ttest_tool (wbcg, sheet, TTEST_ZTEST);
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
ftest_tool_ok_clicked_cb (GtkWidget *button, FTestToolState *state)
{
	data_analysis_output_t  dao;
	Value *range_1;
	Value *range_2;
        char   *text;
	GtkWidget *w;
	gnum_float alpha;
	gint err;

	range_1 = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

	range_2 =  gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->input_entry_2), state->sheet);

        parse_output ((GenericToolState *)state, &dao);

	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = entry_to_float (GTK_ENTRY (state->alpha_entry), &alpha, TRUE);

	err = ftest_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
			  range_1, range_2,  alpha, &dao);

	switch (err) {
	case 0: gtk_widget_destroy (state->dialog);
		break;
	case 1:
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry),
				_("Each variable should have at least 2 observations!"));
		break;
	case 2:
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry_2),
				_("Each variable should have at least 2 observations!"));
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: %d."), err);
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry), text);
		g_free (text);		
		break;
	}
	return;
}

/**
 * ftest_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity if the only items of interest
 * are the standard input (one or two ranges) and output items.
 **/
static void
ftest_update_sensitivity_cb (GtkWidget *dummy, FTestToolState *state)
{
	gboolean ready  = FALSE;
	gboolean input_1_ready  = FALSE;
	gboolean input_2_ready  = FALSE;
	gboolean output_ready  = FALSE;
	gboolean alpha_ready = FALSE;
	int i, err;
	gnum_float  alpha;
        Value *output_range;
        Value *input_range;
        Value *input_range_2;

	output_range = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->output_entry), state->sheet);
        input_range = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);
	input_range_2 = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->input_entry_2), state->sheet); 
		
	i = gnumeric_glade_group_value (state->gui, output_group);
	err = entry_to_float (GTK_ENTRY (state->alpha_entry), &alpha, FALSE);
	alpha_ready = (err == 0 && alpha > 0.0 && alpha < 1.0);
	input_1_ready = (input_range != NULL);
	input_2_ready = ((state->input_entry_2 == NULL) || (input_range_2 != NULL));
	output_ready =  ((i != 2) || (output_range != NULL));

        if (input_range != NULL) value_release (input_range);
        if (input_range_2 != NULL) value_release (input_range_2);
        if (output_range != NULL) value_release (output_range);

	ready = input_1_ready && input_2_ready && output_ready && alpha_ready;
	gtk_widget_set_sensitive (state->ok_button, ready);

	return;
}

/**
 * dialog_ftest_tool_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_ftest_tool_init (FTestToolState *state)
{
	if (dialog_tool_init ((GenericToolState *)state, "variance-tests.glade", "VarianceTests", 
			      GTK_SIGNAL_FUNC (ftest_tool_ok_clicked_cb), 
			      GTK_SIGNAL_FUNC (ftest_update_sensitivity_cb),
			      GNUM_EE_SINGLE_RANGE)) {
		return TRUE;
	}

	state->alpha_entry = glade_xml_get_widget (state->gui, "one_alpha");
 	float_to_entry (GTK_ENTRY (state->alpha_entry), 0.05);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->alpha_entry));
	gtk_signal_connect_after (GTK_OBJECT (state->alpha_entry), "changed",
				  GTK_SIGNAL_FUNC (ftest_update_sensitivity_cb), state);

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       FTEST_KEY);

	ftest_update_sensitivity_cb (NULL, state);

	return FALSE;
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
        FTestToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, FTEST_KEY))
		return 0;

	state = g_new (FTestToolState, 1);
	(*(ToolType *)state) = TOOL_FTEST;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->help_link = "ftest-two-sample-for-variances-tool.html";
	state->input_var1_str = _("Var_iable 1 Range");
	state->input_var2_str = _("_Variable 2 Range");

	if (dialog_ftest_tool_init (state)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the FTest Tool dialog."));
		g_free (state);
		return 0;
	}

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
	int i, periodic, size, number, err_size, err_number;
        Value *output_range;
        GSList *input_range;

        output_range = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->output_entry), state->sheet);
        input_range = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);
	i = gnumeric_glade_group_value (state->gui, output_group);
        periodic = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->periodic_button));

	if (periodic == 1) {
		err_size = entry_to_int (GTK_ENTRY (state->period_entry), &size, FALSE);
	} else {
		err_size = entry_to_int (GTK_ENTRY (state->random_entry), &size, FALSE);
	}
	err_number = entry_to_int (GTK_ENTRY (state->number_entry), &number, FALSE);

	ready = ((input_range != NULL) &&
		 (err_size == 0 && size > 0) && 
		 (err_number == 0 && number > 0) &&
                 ((i != 2) || (output_range != NULL)));

        if (input_range != NULL) range_list_destroy (input_range);
        if (output_range != NULL) value_release (output_range);

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
sampling_tool_ok_clicked_cb (GtkWidget *button, SamplingState *state)
{

	data_analysis_output_t  dao;
        char   *text;
	GtkWidget *w;
	GSList *input;
	gint size, number;
	gint periodic, err;

	input = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

        parse_output ((GenericToolState *)state, &dao);

	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

        periodic = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->periodic_button));

	if (periodic == 1) {
		err = entry_to_int (GTK_ENTRY (state->period_entry), &size, TRUE);
	} else {
		err = entry_to_int (GTK_ENTRY (state->random_entry), &size, TRUE);
	}
	err = entry_to_int (GTK_ENTRY (state->number_entry), &number, TRUE);

	err = sampling_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet, input,
			       gnumeric_glade_group_value (state->gui, grouped_by_group),
			     periodic, size, number, &dao);
	switch (err) {
	case 0:
		if (button == state->ok_button)
			gtk_widget_destroy (state->dialog);
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: %d."), err);
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry), text);
		g_free (text);		
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
sampling_method_toggled_cb (GtkWidget *button, SamplingState *state)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)) == 1) {
		gtk_widget_hide (state->random_label);
		gtk_widget_hide (state->random_entry);
		gtk_widget_show (state->period_label);
		gtk_widget_show (state->period_entry);
	} else {
		gtk_widget_hide (state->period_label);
		gtk_widget_hide (state->period_entry);
		gtk_widget_show (state->random_label);
		gtk_widget_show (state->random_entry);
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
	sampling_method_toggled_cb (state->periodic_button, state);
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
			      GTK_SIGNAL_FUNC (sampling_tool_update_sensitivity_cb),
			      0)) {
		return TRUE;
	}

	state->periodic_button  = glade_xml_get_widget (state->gui, "periodic-button");
	state->random_button  = glade_xml_get_widget (state->gui, "random-button");
	state->method_label = glade_xml_get_widget (state->gui, "method-label");
	state->options_table = glade_xml_get_widget (state->gui, "options-table");
	state->period_label = glade_xml_get_widget (state->gui, "period-label");
	state->random_label = glade_xml_get_widget (state->gui, "random-label");
	state->period_entry = glade_xml_get_widget (state->gui, "period-entry");
	state->random_entry = glade_xml_get_widget (state->gui, "random-entry");
	state->number_entry = glade_xml_get_widget (state->gui, "number-entry");
	int_to_entry (GTK_ENTRY (state->number_entry), 1);

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
	gtk_signal_connect_after (GTK_OBJECT (state->number_entry), "changed",
				  GTK_SIGNAL_FUNC (sampling_tool_update_sensitivity_cb), state);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->period_entry));
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->random_entry));
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->number_entry));

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SAMPLING_KEY);

	sampling_tool_update_sensitivity_cb (NULL, state);

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
	state->help_link = "sampling-tool.html";
	state->input_var1_str = NULL;
	state->input_var2_str = NULL;

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

/**********************************************/
/*  Begin of random tool code */
/**********************************************/

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
	gboolean par1_is_range;
} DistributionStrs;

/* Distribution strings for Random Number Generator */
static const DistributionStrs distribution_strs[] = {
        { DiscreteDistribution,
	  N_("Discrete"), N_("_Value And Probability Input Range:"), NULL, TRUE },
        { NormalDistribution,
	  N_("Normal"), N_("_Mean:"), N_("_Standard Deviation:"), FALSE },
     	{ PoissonDistribution,
	  N_("Poisson"), N_("_Lambda:"), NULL, FALSE },
	{ ExponentialDistribution,
	  N_("Exponential"), N_("_b Value:"), NULL, FALSE },
	{ BinomialDistribution,
	  N_("Binomial"), N_("_p Value:"), N_("N_umber of Trials:"), FALSE },
	{ NegativeBinomialDistribution,
	  N_("Negative Binomial"), N_("_p Value:"),
	  N_("N_umber of Failures"), FALSE },
        { BernoulliDistribution,
	  N_("Bernoulli"), N_("_p Value:"), NULL, FALSE },
        { UniformDistribution,
	  N_("Uniform"), N_("_Lower Bound:"),  N_("_Upper Bound:"), FALSE },
        { 0, NULL, NULL, NULL, FALSE }
};

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
 * combo_get_distribution
 * @combo  combo widget with distribution list
 *
 * Find from combo the distribution the user selected
 */
static random_distribution_t
combo_get_distribution (GtkWidget *combo)
{
        char const *text;
	int i;

        text = gtk_entry_get_text (GTK_ENTRY (GTK_COMBO (combo)->entry));

	for (i = 0; distribution_strs[i].name != NULL; i++)
		if (strcmp (text, _(distribution_strs[i].name)) == 0)
			return distribution_strs[i].dist;

	return UniformDistribution;
}

/**
 * random_tool_update_sensitivity:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity if the only items of interest
 * are the standard input (one range) and output items.
 **/
static void
random_tool_update_sensitivity_cb (GtkWidget *dummy, RandomToolState *state)
{
	gboolean ready  = FALSE;
	gint count, vars;
	gnum_float a_float, from_val, to_val, p_val;
        Value *output_range;
	Value *disc_prob_range;
	random_distribution_t the_dist;

        output_range = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->output_entry), state->sheet);
	the_dist = combo_get_distribution (state->distribution_combo);

	ready = ((entry_to_int (GTK_ENTRY (state->vars_entry), &vars, FALSE) == 0 && 
		  vars > 0) &&
		 (entry_to_int (GTK_ENTRY (state->count_entry), &count, FALSE) == 0 &&
		  count > 0) &&
                 ((gnumeric_glade_group_value (state->gui, output_group) != 2) || 
		  (output_range != NULL)));
        if (output_range != NULL) value_release (output_range);

	switch (the_dist) {
	case NormalDistribution:
		ready = ready && entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, 
						 FALSE) == 0 &&
			entry_to_float (GTK_ENTRY (state->par2_entry), &a_float, 
					FALSE) == 0;
		break;
	case BernoulliDistribution:
		ready = ready && 
			entry_to_float (GTK_ENTRY (state->par1_entry), &p_val, FALSE) == 0 &&
			p_val <= 1.0 && p_val > 0.0;
		break;
	case PoissonDistribution:
		ready = ready && 
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0.0;
		break;
	case ExponentialDistribution:
		ready = ready && 
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0.0;
		break;
	case BinomialDistribution:
		ready = ready && 
			entry_to_float (GTK_ENTRY (state->par1_entry), &p_val, FALSE) == 0 &&
			entry_to_int (GTK_ENTRY (state->par2_entry), &count, FALSE) == 0 && 
			p_val <= 1.0 && p_val > 0.0 &&
			count > 0;
		break;
	case NegativeBinomialDistribution:
		ready = ready && 
			entry_to_float (GTK_ENTRY (state->par1_entry), &p_val, FALSE) == 0 &&
			entry_to_int (GTK_ENTRY (state->par2_entry), &count, FALSE) == 0 && 
			p_val <= 1.0 && p_val > 0.0 &&
			count > 0;
		break;
	case DiscreteDistribution:
		disc_prob_range = gnumeric_expr_entry_parse_to_value 
			(GNUMERIC_EXPR_ENTRY (state->par1_expr_entry), state->sheet);
		ready = ready && disc_prob_range != NULL;
		if (disc_prob_range != NULL) value_release (disc_prob_range);
		break;
	case UniformDistribution:
	default:
		ready = ready && 
			entry_to_float (GTK_ENTRY (state->par1_entry), &from_val, FALSE) == 0 &&
			entry_to_float (GTK_ENTRY (state->par2_entry), &to_val, FALSE) == 0 && 
			from_val <= to_val;
		break;		
	}

	gtk_widget_set_sensitive (state->apply_button, ready);
	gtk_widget_set_sensitive (state->ok_button, ready);
}

/*
 * distribution_parbox_config
 * @p     Callback data
 * @dist  Distribution
 *
 * Configure parameter widgets given random distribution.
 *
 * Set labels and accelerators, and hide/show entry fields as needed.
 **/

static void
distribution_parbox_config (RandomToolState *state,
			    random_distribution_t dist)
{
	GtkWidget *par1_entry;
	guint par1_key = 0, par2_key = 0;
	const DistributionStrs *ds = distribution_strs_find (dist);

	if (ds->par1_is_range) {
		par1_entry = state->par1_expr_entry;
		gtk_widget_hide (state->par1_entry);
	} else {
		par1_entry = state->par1_entry;
		gtk_widget_hide (state->par1_expr_entry);
	}
	gtk_widget_show (par1_entry);

	if (state->distribution_accel != NULL) {
		gtk_window_remove_accel_group (GTK_WINDOW (state->dialog),
					       state->distribution_accel);
		gtk_accel_group_unref (state->distribution_accel);
	}
	state->distribution_accel = gtk_accel_group_new ();

	par1_key = gtk_label_parse_uline (GTK_LABEL (state->par1_label),
					  _(ds->label1));
	if (par1_key != GDK_VoidSymbol)
		gtk_widget_add_accelerator (par1_entry, "grab_focus",
					    state->distribution_accel, par1_key,
					    GDK_MOD1_MASK, 0);
	if (ds->label2 != NULL) {
		par2_key = gtk_label_parse_uline (GTK_LABEL (state->par2_label),
						  _(ds->label2));
		if (par2_key != GDK_VoidSymbol)
			gtk_widget_add_accelerator
				(state->par2_entry, "grab_focus",
				 state->distribution_accel, par2_key,
				 GDK_MOD1_MASK, 0);
	        gtk_widget_show (state->par2_entry);
	} else {
		gtk_label_set_text (GTK_LABEL (state->par2_label), "");
	        gtk_widget_hide (state->par2_entry);
	}
	gtk_window_add_accel_group (GTK_WINDOW (state->dialog),
				    state->distribution_accel);
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
distribution_callback (GtkWidget *widget, RandomToolState *state)
{
	random_distribution_t dist;

	dist = combo_get_distribution (state->distribution_combo);
	distribution_parbox_config (state, dist);
}


/**
 * dialog_random_realized:
 * @widget
 * @state:
 *
 * Make initial geometry of distribution table permanent.
 *
 * The dialog is constructed with the distribution_table containing the widgets
 * which need the most space. At construction time, we do not know how large
 * the distribution_table needs to be, but we do know when the dialog is
 * realized. This callback for "realized" makes this size the user specified
 * size so that the table will not shrink when we later change label texts and
 * hide/show widgets.
  *
 **/
static void
dialog_random_realized (GtkWidget *widget, RandomToolState *state)
{
	GtkWidget *t = state->distribution_table;
	GtkWidget *l = state->par1_label;

	gtk_widget_set_usize (t, t->allocation.width, t->allocation.height);
	gtk_widget_set_usize (l, l->allocation.width, l->allocation.height);
	distribution_callback (widget, state);
}


/**
 * random_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the appropriate tool.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
random_tool_ok_clicked_cb (GtkWidget *button, RandomToolState *state)
{

	data_analysis_output_t  dao;
        char   *text;
	gint vars, count, err;
	random_tool_t           param;

        parse_output ((GenericToolState *)state, &dao);

	err = entry_to_int (GTK_ENTRY (state->vars_entry), &vars, FALSE);
	err = entry_to_int (GTK_ENTRY (state->count_entry), &count, FALSE);

	state->distribution = combo_get_distribution (state->distribution_combo);
	switch (state->distribution) {
	case NormalDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry), &param.normal.mean, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par2_entry), &param.normal.stdev, TRUE);
		break;
	case BernoulliDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry), &param.bernoulli.p, TRUE);
		break;
	case PoissonDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry), &param.poisson.lambda, TRUE);
		break;
	case ExponentialDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry), &param.exponential.b, TRUE);
		break;
	case BinomialDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry), &param.binomial.p, TRUE);
		err = entry_to_int (GTK_ENTRY (state->par2_entry), &param.binomial.trials, TRUE);
		break;
	case NegativeBinomialDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry), &param.negbinom.p, TRUE);
		err = entry_to_int (GTK_ENTRY (state->par2_entry), &param.negbinom.f, TRUE);
		break;
	case DiscreteDistribution:
		param.discrete.range = gnumeric_expr_entry_parse_to_value (
			GNUMERIC_EXPR_ENTRY (state->par1_expr_entry), state->sheet);
		break;
	case UniformDistribution:
	default:
		err = entry_to_float (GTK_ENTRY (state->par1_entry), 
				     &param.uniform.lower_limit, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par2_entry), 
				     &param.uniform.upper_limit, TRUE);
		break;
	}

	err = random_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
			   vars, count, state->distribution, &param,&dao);
	switch (err) {
	case 0:
		if (button == state->ok_button) {
			if (state->distribution_accel) {
				gtk_accel_group_unref (state->distribution_accel);
				state->distribution_accel = NULL;
			}
			gtk_widget_destroy (state->dialog);
		}
		break;
	case 1: /* non-numeric probability (DiscreteDistribution) */
		error_in_entry (state->wbcg, GTK_WIDGET (state->par1_expr_entry),
				_("The probability input range contains a non-numeric value.\n"
				  "All probabilities must be non-negative numbers."));
		break;
        case 2: /* probabilities are all zero  (DiscreteDistribution) */
		error_in_entry (state->wbcg, GTK_WIDGET (state->par1_expr_entry),
				_("The probabilities may not all be 0!"));
		break;
        case 3: /* negative probability  (DiscreteDistribution) */
		error_in_entry (state->wbcg, GTK_WIDGET (state->par1_expr_entry),
				_("The probability input range contains a negative number.\n"
				"All probabilities must be non-negative!"));
		break;
        case 4: /* value is empty  (DiscreteDistribution) */
		error_in_entry (state->wbcg, GTK_WIDGET (state->par1_expr_entry), 
				_("None of the values in the value range may be empty!"));
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: %d."), err);
		gnumeric_notice (state->wbcg, GNOME_MESSAGE_BOX_ERROR, text);
		g_free (text);		
		break;
	}
	return;
}



/**
 * dialog_random_tool_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_random_tool_init (RandomToolState *state)
{
	int   i, dist_str_no;
	const DistributionStrs *ds;
	GList *distribution_type_strs = NULL;
	GtkTable *table;

	state->gui = gnumeric_glade_xml_new (state->wbcg, "random-generation.glade");
        if (state->gui == NULL)
                return TRUE;

	state->dialog = glade_xml_get_widget (state->gui, "Random");
        if (state->dialog == NULL)
                return TRUE;

	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "set-focus",
			    GTK_SIGNAL_FUNC (tool_set_focus), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (tool_destroy), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "realize",
			    GTK_SIGNAL_FUNC (dialog_random_realized), state);

	state->accel = NULL;
	state->distribution = DiscreteDistribution;

	dialog_tool_init_buttons ((GenericToolState *)state,
				  GTK_SIGNAL_FUNC (random_tool_ok_clicked_cb) );

	dialog_tool_init_outputs ((GenericToolState *)state,
				  GTK_SIGNAL_FUNC (random_tool_update_sensitivity_cb));

	state->distribution_table = glade_xml_get_widget (state->gui, "distribution_table");
	state->distribution_combo = glade_xml_get_widget (state->gui, "distribution_combo");
	state->par1_entry = glade_xml_get_widget (state->gui, "par1_entry");
	state->par1_label = glade_xml_get_widget (state->gui, "par1_label");
	state->par2_label = glade_xml_get_widget (state->gui, "par2_label");
	state->par2_entry = glade_xml_get_widget (state->gui, "par2_entry");
	state->vars_entry = glade_xml_get_widget (state->gui, "vars_entry");
	state->count_entry = glade_xml_get_widget (state->gui, "count_entry");
	int_to_entry (GTK_ENTRY (state->count_entry), 1);
	state->distribution_accel = NULL;

	for (i = 0, dist_str_no = 0; distribution_strs[i].name != NULL; i++) {
		distribution_type_strs
			= g_list_append (distribution_type_strs,
					 (gpointer) _(distribution_strs[i].name));
		if (distribution_strs[i].dist == state->distribution)
			dist_str_no = i;
	}
	gtk_combo_set_popdown_strings (GTK_COMBO (state->distribution_combo),
				       distribution_type_strs);
	g_list_free (distribution_type_strs);
	distribution_type_strs = NULL;

	gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (state->distribution_combo)->entry),
			   _(distribution_strs[dist_str_no].name));

	ds = distribution_strs_find (DiscreteDistribution);
	(void) gtk_label_parse_uline (GTK_LABEL (state->par1_label), _(ds->label1));

  	gtk_signal_connect (GTK_OBJECT (GTK_COMBO (state->distribution_combo)->entry),
			    "changed", GTK_SIGNAL_FUNC (distribution_callback),
			    state);
  	gtk_signal_connect (GTK_OBJECT (GTK_COMBO (state->distribution_combo)->entry),
			    "changed", GTK_SIGNAL_FUNC (random_tool_update_sensitivity_cb),
			    state);

	table = GTK_TABLE (glade_xml_get_widget (state->gui, "distribution_table"));
	state->par1_expr_entry = GTK_WIDGET (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags (GNUMERIC_EXPR_ENTRY (state->par1_expr_entry),
				       GNUM_EE_SINGLE_RANGE, GNUM_EE_MASK);
        gnumeric_expr_entry_set_scg (GNUMERIC_EXPR_ENTRY (state->par1_expr_entry),
				     wb_control_gui_cur_sheet (state->wbcg));
	gtk_table_attach (table, state->par1_expr_entry,
			  1, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->par1_expr_entry));


	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->par1_entry));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->par2_entry));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->vars_entry));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->count_entry));

	gtk_signal_connect_after (GTK_OBJECT (state->vars_entry), "changed",
				  GTK_SIGNAL_FUNC (random_tool_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->count_entry), "changed",
				  GTK_SIGNAL_FUNC (random_tool_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->par1_entry), "changed",
				  GTK_SIGNAL_FUNC (random_tool_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->par2_entry), "changed",
				  GTK_SIGNAL_FUNC (random_tool_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->par1_expr_entry), "changed",
				  GTK_SIGNAL_FUNC (random_tool_update_sensitivity_cb), state);

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       RANDOM_KEY);

	random_tool_update_sensitivity_cb (NULL, state);

	return FALSE;
}


/**
 * dialog_random_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
static int
dialog_random_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        RandomToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, RANDOM_KEY)) {
		return 0;
	}

	state = g_new (RandomToolState, 1);
	(*(ToolType *)state) = TOOL_RANDOM;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->help_link = "random-number-generation-tool.html";
	state->input_var1_str = NULL;
	state->input_var2_str = NULL;

	if (dialog_random_tool_init (state)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Random Tool dialog."));
		g_free (state);
		return 0;
	}
	gtk_widget_show (state->dialog);

        return 0;
}
/**********************************************/
/*  End of random tool code */
/**********************************************/

/**********************************************/
/*  Begin of Regression tool code */
/**********************************************/


/**
 * regression_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the regression_tool.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
regression_tool_ok_clicked_cb (GtkWidget *button, RegressionToolState *state)
{
	data_analysis_output_t  dao;
	GSList *x_input;
	Value  *y_input;
	GtkWidget *w;
	int intercept_flag, err;
	gnum_float confidence;

	x_input = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);
	y_input = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->input_entry_2), state->sheet);

        parse_output ((GenericToolState *)state, &dao);

	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = entry_to_float (GTK_ENTRY (state->confidence_entry), &confidence, TRUE);

	w = glade_xml_get_widget (state->gui, "intercept-button");
	intercept_flag = 1 - gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = regression_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
			       x_input, y_input, 
			       gnumeric_glade_group_value (state->gui, grouped_by_group),
			       1 - confidence, &dao, intercept_flag);

	switch (err) {
	case 0:
		gtk_widget_destroy (state->dialog);
		break;
	case 1:
	        gnumeric_notice (state->wbcg, GNOME_MESSAGE_BOX_ERROR,
			      _("There are too few data points to conduct this "
				"regression.\nThere must be at least as many "
				"data points as free variables."));
		break;
	case 2:
	        gnumeric_notice (state->wbcg, GNOME_MESSAGE_BOX_ERROR,
			      _("Two or more of the independent variables "
				"are linearly dependent,\nand the regression "
				"cannot be calculated. Remove one of these\n"
				"variables and try the regression again."));
                break;
	case 3:
		gnumeric_notice (state->wbcg, GNOME_MESSAGE_BOX_ERROR,
			      _("There must be an equal number of entries "
				"for each variable in the regression."));
                break;
	default:
		break;
	}
	return;
}

/**
 * regression_tool_update_sensitivity_cb:
 * @state:
 *
 * Update the dialog widgets sensitivity.
 * We cannot use tool_update_sensitivity_cb
 * since we are also considering whether in fact
 * an interval is given.
 **/
static void
regression_tool_update_sensitivity_cb (GtkWidget *dummy, RegressionToolState *state)
{
	gboolean ready  = FALSE;
	gboolean input_1_ready  = FALSE;
	gboolean input_2_ready  = FALSE;
	gboolean output_ready  = FALSE;
	int i, err;
	gnum_float confidence;
        Value *output_range;
        GSList *input_range;
        Value *input_range_2;

        output_range = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->output_entry), state->sheet);
        input_range = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);
	input_range_2 = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->input_entry_2), state->sheet);

	i = gnumeric_glade_group_value (state->gui, output_group);
	err = entry_to_float (GTK_ENTRY (state->confidence_entry), &confidence, FALSE);

	input_1_ready = (input_range != NULL);
	input_2_ready = (input_range_2 != NULL);
	output_ready =  ((i != 2) || (output_range != NULL));

	ready = input_1_ready &&
		input_2_ready &&
		(err == 0) && (1 > confidence ) && (confidence > 0) &&
		output_ready;

        if (input_range != NULL) range_list_destroy (input_range);
        if (input_range_2 != NULL) value_release (input_range_2);
        if (output_range != NULL) value_release (output_range);

	gtk_widget_set_sensitive (state->ok_button, ready);
}


/**
 * dialog_regression_tool_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_regression_tool_init (RegressionToolState *state)
{
	GtkTable *table;
	GtkWidget *widget;
	gint key;

	state->gui = gnumeric_glade_xml_new (state->wbcg, "regression.glade");
        if (state->gui == NULL)
                return TRUE;

	state->dialog = glade_xml_get_widget (state->gui, "Regression");
        if (state->dialog == NULL)
                return TRUE;

	state->accel = gtk_accel_group_new ();

	dialog_tool_init_buttons ((GenericToolState *)state, 
				  GTK_SIGNAL_FUNC (regression_tool_ok_clicked_cb));

	table = GTK_TABLE (glade_xml_get_widget (state->gui, "input-table"));
	state->input_entry = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags (state->input_entry, 0, GNUM_EE_MASK);
        gnumeric_expr_entry_set_scg (state->input_entry, wb_control_gui_cur_sheet (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->input_entry),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gtk_signal_connect_after (GTK_OBJECT (state->input_entry), "changed",
				  GTK_SIGNAL_FUNC (regression_tool_update_sensitivity_cb), 
				  state);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->input_entry));

	widget = glade_xml_get_widget (state->gui, "var1-label");
	state->input_var1_str = _("_X Variables:");
	key = gtk_label_parse_uline (GTK_LABEL (widget), state->input_var1_str);
	if (key != GDK_VoidSymbol)
		gtk_widget_add_accelerator (GTK_WIDGET (state->input_entry),
					    "grab_focus",
					    state->accel, key,
					    GDK_MOD1_MASK, 0);
	gtk_widget_show (GTK_WIDGET (state->input_entry));

	state->input_entry_2 = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags (state->input_entry_2, GNUM_EE_SINGLE_RANGE, 
				       GNUM_EE_MASK);
	gnumeric_expr_entry_set_scg (state->input_entry_2,
				     wb_control_gui_cur_sheet (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->input_entry_2),
			  1, 2, 2, 3,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->input_entry_2));
	gtk_signal_connect_after (GTK_OBJECT (state->input_entry_2), "changed",
				  GTK_SIGNAL_FUNC (regression_tool_update_sensitivity_cb), 
				  state);
	widget = glade_xml_get_widget (state->gui, "var2-label");
	state->input_var2_str = _("_Y Variable:");
	key = gtk_label_parse_uline (GTK_LABEL (widget), state->input_var2_str);
	if (key != GDK_VoidSymbol)
		gtk_widget_add_accelerator (GTK_WIDGET (state->input_entry_2),
					    "grab_focus",
					    state->accel, key,
					    GDK_MOD1_MASK, 0);
	gtk_widget_show (GTK_WIDGET (state->input_entry_2));

	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "set-focus",
			    GTK_SIGNAL_FUNC (tool_set_focus), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (tool_destroy), state);

	dialog_tool_init_outputs ((GenericToolState *)state, 
				  GTK_SIGNAL_FUNC (regression_tool_update_sensitivity_cb));

	gtk_window_add_accel_group (GTK_WINDOW (state->dialog),
				    state->accel);

	state->confidence_entry = glade_xml_get_widget (state->gui, "confidence-entry");
	float_to_entry (GTK_ENTRY (state->confidence_entry), 0.95);
	gtk_signal_connect_after (GTK_OBJECT (state->confidence_entry), "changed",
				  GTK_SIGNAL_FUNC (regression_tool_update_sensitivity_cb), state);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->confidence_entry));

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       REGRESSION_KEY);

	regression_tool_update_sensitivity_cb (NULL, state);

	return FALSE;
}

/**
 * dialog_regression_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
static int
dialog_regression_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        RegressionToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, REGRESSION_KEY))
		return 0;

	state = g_new (RegressionToolState, 1);
	(*(ToolType *)state) = TOOL_REGRESSION;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->help_link = "regression-tool.html";

	if (dialog_regression_tool_init (state)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Regression Tool dialog."));
		g_free (state);
		return 0;
	}

	gtk_widget_show (state->dialog);

        return 0;
}

/**********************************************/
/*  End of Regression tool code */
/**********************************************/

/**********************************************/
/*  Begin of Exponential smoothing tool code */
/**********************************************/


/**
 * exp_smoothing_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 **/
static void
exp_smoothing_tool_ok_clicked_cb (GtkWidget *button, ExpSmoothToolState *state)
{
	data_analysis_output_t  dao;
	GSList                  *input;
        char                    *text;
	GtkWidget               *w;
	int                     standard_errors_flag;
	gnum_float              damp_fact;
	gint                    err;

	input = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

        parse_output ((GenericToolState *)state, &dao);

	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = entry_to_float (GTK_ENTRY (state->damping_fact_entry), &damp_fact, TRUE);

	w = glade_xml_get_widget (state->gui, "std_errors_button");
	standard_errors_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = exp_smoothing_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet, input,
				  gnumeric_glade_group_value (state->gui, grouped_by_group),
				  damp_fact, standard_errors_flag,
				  &dao);

	switch (err) {
	case 0:
		gtk_widget_destroy (state->dialog);
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: %d."), err);
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry), text);
		g_free (text);
		break;
	}
	return;
}

/**
 * exp_smoothing_tool_update_sensitivity_cb:
 * @state:
 *
 * Update the dialog widgets sensitivity.
 * We cannot use tool_update_sensitivity_cb
 * since we are also considering whether in fact
 * a damping factor is given.
 **/
static void
exp_smoothing_tool_update_sensitivity_cb (GtkWidget *dummy,
					  ExpSmoothToolState *state)
{
	gboolean ready  = FALSE;
	int i, err;
	gnum_float damp_fact;
        Value *output_range;
        GSList *input_range;

        output_range = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->output_entry), state->sheet);
        input_range = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);
	i = gnumeric_glade_group_value (state->gui, output_group);
	err = entry_to_float (GTK_ENTRY (state->damping_fact_entry), &damp_fact, FALSE);

	ready = ((input_range != NULL) &&
                 (err == 0 && damp_fact >= 0 && damp_fact <= 1) &&
                 ((i != 2) || (output_range != NULL)));

        if (input_range != NULL) range_list_destroy (input_range);
        if (output_range != NULL) value_release (output_range);

	gtk_widget_set_sensitive (state->ok_button, ready);
}


/**
 * dialog_exp_smoothing_tool_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_exp_smoothing_tool_init (ExpSmoothToolState *state)
{
	if (dialog_tool_init ((GenericToolState *)state, "exp-smoothing.glade",
			      "ExpSmoothing",
			      GTK_SIGNAL_FUNC (exp_smoothing_tool_ok_clicked_cb),
			      GTK_SIGNAL_FUNC (exp_smoothing_tool_update_sensitivity_cb),
			      0)) {
		return TRUE;
	}

	state->damping_fact_entry = glade_xml_get_widget (state->gui,
							  "damping-fact-entry");
	float_to_entry (GTK_ENTRY (state->damping_fact_entry), 0.2);
	gtk_signal_connect_after (GTK_OBJECT (state->damping_fact_entry),"changed",
				  GTK_SIGNAL_FUNC
				  (exp_smoothing_tool_update_sensitivity_cb),
				  state);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->damping_fact_entry));

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       EXP_SMOOTHING_KEY);

	exp_smoothing_tool_update_sensitivity_cb (NULL, state);

	return FALSE;
}

/**
 * dialog_exp_smoothing_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
static int
dialog_exp_smoothing_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        ExpSmoothToolState *state;

	if (wbcg == NULL) {
		return 1;
	}

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, EXP_SMOOTHING_KEY))
		return 0;

	state = g_new (ExpSmoothToolState, 1);
	(*(ToolType *)state) = TOOL_EXP_SMOOTHING;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->help_link = "exp-smoothing-tool.html";
	state->input_var1_str = NULL;
	state->input_var2_str = NULL;

	if (dialog_exp_smoothing_tool_init (state)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Exponential Smoothing "
				   "Tool dialog."));
		g_free (state);
		return 0;
	}

	gtk_widget_show (state->dialog);

        return 0;
}

/**********************************************/
/*  End of Exponential Smoothing tool code */
/**********************************************/

/**********************************************/
/*  Begin of Moving Averages tool code */
/**********************************************/


/**
 * average_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the average_tool.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
average_tool_ok_clicked_cb (GtkWidget *button, AverageToolState *state)
{
	data_analysis_output_t  dao;
	GSList                  *input;
        char                    *text;
	GtkWidget               *w;
	int                     standard_errors_flag, 
                                interval;
	gint                    err;

	input = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

        parse_output ((GenericToolState *)state, &dao);

	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = entry_to_int (GTK_ENTRY (state->interval_entry), &interval, TRUE);

	w = glade_xml_get_widget (state->gui, "std_errors_button");
	standard_errors_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = average_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet, input,
			    gnumeric_glade_group_value (state->gui, grouped_by_group),
			    interval, standard_errors_flag, &dao);
	switch (err) {
	case 0:
		gtk_widget_destroy (state->dialog);
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: %d."), err);
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry), text);
		g_free (text);		
		break;
	}
	return;
}

/**
 * average_tool_update_sensitivity_cb:
 * @state:
 *
 * Update the dialog widgets sensitivity.
 * We cannot use tool_update_sensitivity_cb
 * since we are also considering whether in fact
 * an interval is given.
 **/
static void
average_tool_update_sensitivity_cb (GtkWidget *dummy, AverageToolState *state)
{
	gboolean ready  = FALSE;
	int i, interval, err;
        Value *output_range;
        GSList *input_range;

        output_range = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->output_entry), state->sheet);
        input_range = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);
	i = gnumeric_glade_group_value (state->gui, output_group);
	err = entry_to_int (GTK_ENTRY (state->interval_entry), &interval, FALSE);

	ready = ((input_range != NULL) &&
                 (err == 0 && interval > 0) &&
                 ((i != 2) || (output_range != NULL)));

        if (input_range != NULL) range_list_destroy (input_range);
        if (output_range != NULL) value_release (output_range);

	gtk_widget_set_sensitive (state->ok_button, ready);
}


/**
 * dialog_average_tool_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_average_tool_init (AverageToolState *state)
{
	if (dialog_tool_init ((GenericToolState *)state, "moving-averages.glade", 
			      "MovAverages",
			      GTK_SIGNAL_FUNC (average_tool_ok_clicked_cb),
			      GTK_SIGNAL_FUNC (average_tool_update_sensitivity_cb),
			      0)) {
		return TRUE;
	}

	state->interval_entry = glade_xml_get_widget (state->gui, "interval-entry");
	int_to_entry (GTK_ENTRY (state->interval_entry), 3);
	gtk_signal_connect_after (GTK_OBJECT (state->interval_entry), "changed",
				  GTK_SIGNAL_FUNC 
				  (average_tool_update_sensitivity_cb), state);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->interval_entry));

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       AVERAGE_KEY);

	average_tool_update_sensitivity_cb (NULL, state);

	return FALSE;
}

/**
 * dialog_average_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
static int
dialog_average_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        AverageToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, AVERAGE_KEY))
		return 0;

	state = g_new (AverageToolState, 1);
	(*(ToolType *)state) = TOOL_AVERAGE;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->help_link = "moving-average-tool.html";
	state->input_var1_str = NULL;
	state->input_var2_str = NULL;

	if (dialog_average_tool_init (state)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Moving Average Tool dialog."));
		g_free (state);
		return 0;
	}

	gtk_widget_show (state->dialog);

        return 0;
}

/**********************************************/
/*  End of Moving Averages tool code */
/**********************************************/

/**********************************************/
/*  Begin of Fourier analysis tool code */
/**********************************************/

/**
 * fourier_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the fourier_tool.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
fourier_tool_ok_clicked_cb (GtkWidget *button, GenericToolState *state)
{
	data_analysis_output_t  dao;
	GSList                  *input;
	GtkWidget               *w;
	gint                    inverse;
	gint                    err;
	char                    *text;

	input = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

        parse_output (state, &dao);

	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	w = glade_xml_get_widget (state->gui, "inverse_button");
	inverse = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = fourier_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet, input,
			    gnumeric_glade_group_value (state->gui, grouped_by_group),
			    inverse, &dao);

	switch (err) {
	case 0:
		gtk_widget_destroy (state->dialog);
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: %d."), err);
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry), text);
		g_free (text);
		break;
	}

	return;
}



/**
 * dialog_fourier_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
static int
dialog_fourier_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GenericToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, FOURIER_KEY))
		return 0;

	state = g_new (GenericToolState, 1);
	(*(ToolType *)state) = TOOL_FOURIER;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->help_link = "fourier-analysis-tool.html";
	state->input_var1_str = NULL;
	state->input_var2_str = NULL;

	if (dialog_tool_init (state, "fourier-analysis.glade", "FourierAnalysis",
			      GTK_SIGNAL_FUNC (fourier_tool_ok_clicked_cb),
			      GTK_SIGNAL_FUNC (tool_update_sensitivity_cb),
			      0)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Fourier Analyis Tool dialog."));
		g_free (state);
		return 0;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       FOURIER_KEY);

	tool_update_sensitivity_cb (NULL, state);
	gtk_widget_show (state->dialog);

        return 0;
}

/**********************************************/
/*  End of Fourier analysis tool code */
/**********************************************/

/**********************************************/
/*  Begin of histogram tool code */
/**********************************************/

/**
 * histogram_tool_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity 
 **/
static void
histogram_tool_update_sensitivity_cb (GtkWidget *dummy, HistogramToolState *state)
{
	gboolean ready  = FALSE;
	gboolean input_ready  = FALSE;
	gboolean bin_ready  = FALSE;
	gboolean output_ready  = FALSE;

	int i;
	int the_n;
	gboolean predetermined_bins;
        Value *output_range = NULL;
        GSList *input_range;
        Value *input_range_2 = NULL;

        input_range = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

	i = gnumeric_glade_group_value (state->gui, output_group);
	if (i == 2)
		output_range = gnumeric_expr_entry_parse_to_value 
			(GNUMERIC_EXPR_ENTRY (state->output_entry), state->sheet);

	predetermined_bins = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->predetermined_button));
	if (predetermined_bins) 
		input_range_2 =  gnumeric_expr_entry_parse_to_value 
			(GNUMERIC_EXPR_ENTRY (state->input_entry_2), state->sheet);

	input_ready = (input_range != NULL);
	bin_ready = (predetermined_bins && input_range_2 != NULL) || 
		(!predetermined_bins && entry_to_int(state->n_entry, &the_n,FALSE) == 0 
			&& the_n > 0);
	output_ready =  ((i != 2) || (output_range != NULL));

        if (input_range != NULL) range_list_destroy (input_range);
        if (input_range_2 != NULL) value_release (input_range_2);
        if (output_range != NULL) value_release (output_range);

	ready = input_ready && bin_ready && output_ready;
	if (state->apply_button != NULL)
		gtk_widget_set_sensitive (state->apply_button, ready);
	gtk_widget_set_sensitive (state->ok_button, ready);
	return;
}

/**
 * histogram_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the histogram_tool.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
histogram_tool_ok_clicked_cb (GtkWidget *button, HistogramToolState *state)
{
	data_analysis_output_t  dao;
	GSList *input;
	Value  *bin;
        char   *text;
	GtkWidget *w;
	int pareto, cum, chart, err, bin_labels = 0, percent;
	histogram_calc_bin_info_t bin_info = {FALSE, FALSE, 0, 0, 0};
	histogram_calc_bin_info_t *bin_info_ptr = &bin_info;

	input = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

	if (gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->predetermined_button))) {
		w = glade_xml_get_widget (state->gui, "labels_2_button");
		bin_labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
		bin = gnumeric_expr_entry_parse_to_value 
			(GNUMERIC_EXPR_ENTRY (state->input_entry_2), state->sheet);
		bin_info_ptr = NULL;
	} else {
		entry_to_int(state->n_entry, &bin_info.n,TRUE);
		bin_info.max_given = (0 == entry_to_float (state->max_entry, 
							    &bin_info.max , TRUE));
		bin_info.min_given = (0 == entry_to_float (state->min_entry, 
							    &bin_info.min , TRUE));
		bin = NULL;
	}

        parse_output ((GenericToolState *) state, &dao);

	w = glade_xml_get_widget (state->gui, "labels_button");
	dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	w = glade_xml_get_widget (state->gui, "pareto-button");
	pareto = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	w = glade_xml_get_widget (state->gui, "percentage-button");
	percent  = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	w = glade_xml_get_widget (state->gui, "cum-button");
	cum = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	w = glade_xml_get_widget (state->gui, "chart-button");
	chart = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = histogram_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
			      input, bin,
			      gnumeric_glade_group_value (state->gui, grouped_by_group),
			      bin_labels, pareto, percent, cum, chart, bin_info_ptr, &dao);
	switch (err) {
	case 0:
		gtk_widget_destroy (state->dialog);
		break;
        case 2:
	        error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry_2),
				_("Each row of the bin range should contain one numeric value\n"
				  "(ignoring the label if applicable)."));
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: %d."), err);
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry), text);
		g_free (text);		
		break;
	}
	return;
}

/**
 * histogram_tool_set_predetermined:
 * @widget:
 * @focus_widget:
 * @state:
 *
 * Output range entry was focused. Switch to output range.
 *
 **/
static void
histogram_tool_set_predetermined (GtkWidget *widget, GdkEventFocus *event,
			HistogramToolState *state)
{
	    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->predetermined_button), TRUE);
}

/**
 * histogram_tool_set_predetermined_on_toggle:
 * @widget:
 * @focus_widget:
 * @state:
 *
 * Output range entry was focused. Switch to output range.
 *
 **/
static void
histogram_tool_set_predetermined_on_toggle (GtkWidget *widget,
			HistogramToolState *state)
{
	    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->predetermined_button), TRUE);
}


/**
 * histogram_tool_set_calculated:
 * @widget:
 * @focus_widget:
 * @state:
 *
 * Output range entry was focused. Switch to output range.
 *
 **/
static void
histogram_tool_set_calculated (GtkWidget *widget, GdkEventFocus *event,
			HistogramToolState *state)
{
	    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->calculated_button), TRUE);
}


/**
 * dialog_histogram_tool_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_histogram_tool_init (HistogramToolState *state)
{
	GtkTable *table;
	GtkWidget *widget;
	gint key;

	state->gui = gnumeric_glade_xml_new (state->wbcg, "histogram.glade");
        if (state->gui == NULL)
                return TRUE;

	state->dialog = glade_xml_get_widget (state->gui, "Histogram");
        if (state->dialog == NULL)
                return TRUE;

	state->accel = gtk_accel_group_new ();

	dialog_tool_init_buttons ((GenericToolState *) state, 
				  GTK_SIGNAL_FUNC (histogram_tool_ok_clicked_cb));

	table = GTK_TABLE (glade_xml_get_widget (state->gui, "input-table"));
	state->input_entry = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags (state->input_entry, 0, GNUM_EE_MASK);
        gnumeric_expr_entry_set_scg (state->input_entry, wb_control_gui_cur_sheet (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->input_entry),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gtk_signal_connect_after (GTK_OBJECT (state->input_entry), "changed",
				  GTK_SIGNAL_FUNC (histogram_tool_update_sensitivity_cb), 
				  state);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->input_entry));
	if (state->input_var1_str == NULL) {
		state->input_var1_str = _("_Input Range:");
	}
	widget = glade_xml_get_widget (state->gui, "var1-label");
	key = gtk_label_parse_uline (GTK_LABEL (widget), state->input_var1_str);
	if (key != GDK_VoidSymbol)
		gtk_widget_add_accelerator (GTK_WIDGET (state->input_entry),
					    "grab_focus",
					    state->accel, key,
					    GDK_MOD1_MASK, 0);
	gtk_widget_show (GTK_WIDGET (state->input_entry));

	table = GTK_TABLE (glade_xml_get_widget (state->gui, "bin_table"));
	state->input_entry_2 = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags (state->input_entry_2, GNUM_EE_SINGLE_RANGE, GNUM_EE_MASK);
	gnumeric_expr_entry_set_scg (state->input_entry_2,
				     wb_control_gui_cur_sheet (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->input_entry_2),
			  1, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->input_entry_2));
	gtk_signal_connect_after (GTK_OBJECT (state->input_entry_2), "changed",
				  GTK_SIGNAL_FUNC (histogram_tool_update_sensitivity_cb), 
				  state);
	widget = glade_xml_get_widget (state->gui, "var2-label");
	key = gtk_label_parse_uline (GTK_LABEL (widget), state->input_var2_str);
	if (key != GDK_VoidSymbol)
		gtk_widget_add_accelerator (GTK_WIDGET (state->input_entry_2),
					    "grab_focus",
					    state->accel, key,
					    GDK_MOD1_MASK, 0);
	gtk_widget_show (GTK_WIDGET (state->input_entry_2));

	state->predetermined_button = GTK_WIDGET(glade_xml_get_widget (state->gui, 
								       "pre_determined_button"));
	state->calculated_button = GTK_WIDGET(glade_xml_get_widget (state->gui, 
								    "calculated_button"));
	state->bin_labels_button = GTK_WIDGET(glade_xml_get_widget (state->gui, 
								    "labels_2_button"));
	state->n_entry = GTK_ENTRY(glade_xml_get_widget (state->gui, 
							  "n_entry"));
	state->max_entry = GTK_ENTRY(glade_xml_get_widget (state->gui, 
							    "max_entry"));
	state->min_entry = GTK_ENTRY(glade_xml_get_widget (state->gui, 
							    "min_entry"));


	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "set-focus",
			    GTK_SIGNAL_FUNC (tool_set_focus), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (tool_destroy), state);
	gtk_signal_connect_after (GTK_OBJECT (state->predetermined_button), "toggled",
				  GTK_SIGNAL_FUNC (histogram_tool_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->calculated_button), "toggled",
				  GTK_SIGNAL_FUNC (histogram_tool_update_sensitivity_cb), state);
	gtk_signal_connect_after (GTK_OBJECT (state->n_entry), "changed",
				  GTK_SIGNAL_FUNC (histogram_tool_update_sensitivity_cb), state);

	gtk_signal_connect (GTK_OBJECT (state->n_entry), "focus-in-event",
			    GTK_SIGNAL_FUNC (histogram_tool_set_calculated), state);
	gtk_signal_connect (GTK_OBJECT (state->min_entry), "focus-in-event",
			    GTK_SIGNAL_FUNC (histogram_tool_set_calculated), state);
	gtk_signal_connect (GTK_OBJECT (state->max_entry), "focus-in-event",
			    GTK_SIGNAL_FUNC (histogram_tool_set_calculated), state);
	gtk_signal_connect (GTK_OBJECT (state->input_entry_2), "focus-in-event",
			    GTK_SIGNAL_FUNC (histogram_tool_set_predetermined), state);
	gtk_signal_connect (GTK_OBJECT (state->bin_labels_button), "toggled",
			    GTK_SIGNAL_FUNC (histogram_tool_set_predetermined_on_toggle), state);

	dialog_tool_init_outputs ((GenericToolState *) state, GTK_SIGNAL_FUNC 
				  (histogram_tool_update_sensitivity_cb));

	gtk_window_add_accel_group (GTK_WINDOW (state->dialog),
				    state->accel);

	return FALSE;
}

/**
 * dialog_histogram_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
static int
dialog_histogram_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        HistogramToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, HISTOGRAM_KEY))
		return 0;

	state = g_new (HistogramToolState, 1);
	(*(ToolType *)state) = TOOL_HISTOGRAM;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->help_link = "histogram-tool.html";
	state->input_var1_str = _("_Input Range:");
	state->input_var2_str = _("Bin _Range:");

	if (dialog_histogram_tool_init (state)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Histogram Tool dialog."));
		g_free (state);
		return 0;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       HISTOGRAM_KEY);

	histogram_tool_update_sensitivity_cb (NULL, state);
	gtk_widget_show (state->dialog);

        return 0;
}

/**********************************************/
/*  End of histogram tool code */
/**********************************************/

/**********************************************/
/*  Begin of ANOVA (single factor) tool code */
/**********************************************/


/**
 * anova_single_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the fourier_tool.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
anova_single_tool_ok_clicked_cb (GtkWidget *button, AnovaSingleToolState *state)
{
	data_analysis_output_t  dao;
        char   *text;
	GtkWidget *w;
	gnum_float alpha;
	GSList *input;
	gint err;

	input = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

        parse_output ((GenericToolState *)state, &dao);

	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = entry_to_float (GTK_ENTRY (state->alpha_entry), &alpha, FALSE);
	
	err = anova_single_factor_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
					input,
					gnumeric_glade_group_value (state->gui,
								      grouped_by_group),
					alpha, &dao);
	switch (err) {
	case 0:
		gtk_widget_destroy (state->dialog);
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: %d."), err);
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry), text);
		g_free (text);		
		break;
	}
	return;
}

/**
 * anova_single_tool_update_sensitivity_cb:
 * @state:
 *
 * Update the dialog widgets sensitivity.
 * We cannot use tool_update_sensitivity_cb
 * since we are also considering whether in fact
 * an alpha is given.
 **/
static void
anova_single_tool_update_sensitivity_cb (GtkWidget *dummy, AnovaSingleToolState *state)
{
	gboolean input_1_ready  = FALSE;
	gboolean output_ready  = FALSE;
	gboolean ready  = FALSE;
	int i, err;
	gnum_float alpha;
        Value *output_range;
        GSList *input_range;

        output_range = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->output_entry), state->sheet);
        input_range = gnumeric_expr_entry_parse_to_list (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

	i = gnumeric_glade_group_value (state->gui, output_group);
	err = entry_to_float (GTK_ENTRY (state->alpha_entry), &alpha, FALSE);

	input_1_ready = (input_range != NULL);
	output_ready =  ((i != 2) || (output_range != NULL));

	ready = (input_1_ready &&
                 (err == 0) && (alpha > 0) && (alpha < 1) &&
                 (output_ready));

        if (input_range != NULL) range_list_destroy (input_range);
        if (output_range != NULL) value_release (output_range);

	gtk_widget_set_sensitive (state->ok_button, ready);
}


/**
 * dialog_anova_single_tool_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_anova_single_tool_init (AnovaSingleToolState *state)
{
	if (dialog_tool_init ((GenericToolState *)state, "anova-one.glade", "ANOVA",
			      GTK_SIGNAL_FUNC (anova_single_tool_ok_clicked_cb),
			      GTK_SIGNAL_FUNC (anova_single_tool_update_sensitivity_cb), 
			      0)) {
		return TRUE;
	}

	state->alpha_entry = glade_xml_get_widget (state->gui, "alpha-entry");
	float_to_entry (GTK_ENTRY (state->alpha_entry), 0.05);
	gtk_signal_connect_after (GTK_OBJECT (state->alpha_entry), "changed",
				  GTK_SIGNAL_FUNC (anova_single_tool_update_sensitivity_cb), 
				  state);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->alpha_entry));

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       ANOVA_SINGLE_KEY);

	anova_single_tool_update_sensitivity_cb (NULL, state);

	return FALSE;
}

/**
 * dialog_anova_single_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
static int
dialog_anova_single_factor_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        AnovaSingleToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, ANOVA_SINGLE_KEY))
		return 0;

	state = g_new (AnovaSingleToolState, 1);
	(*(ToolType *)state) = TOOL_ANOVA_SINGLE;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->help_link = "anova.html#ANOVA-SINGLE-FACTOR-TOOL";
	state->input_var1_str = NULL;
	state->input_var2_str = NULL;

	if (dialog_anova_single_tool_init (state)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the ANOVA (single factor) tool dialog."));
		g_free (state);
		return 0;
	}

	gtk_widget_show (state->dialog);

        return 0;
}

/**********************************************/
/*  End of ANOVA (Single Factor) tool code */
/**********************************************/

/**********************************************/
/*  Begin of ANOVA (two factor) tool code */
/**********************************************/


/**
 * anova_two_factor_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the fourier_tool.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
anova_two_factor_tool_ok_clicked_cb (GtkWidget *button, AnovaTwoFactorToolState *state)
{
	data_analysis_output_t  dao;
	Value *input;
        char   *text;
	GtkWidget *w;
	gint replication;
	gnum_float alpha;
	gint err;

	input = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

        parse_output ((GenericToolState *)state, &dao);

	w = glade_xml_get_widget (state->gui, "labels_button");
        dao.labels_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = entry_to_float (GTK_ENTRY (state->alpha_entry), &alpha, TRUE);
	err = entry_to_int (GTK_ENTRY (state->replication_entry), &replication, TRUE);

	if (replication == 1 ) {
		err = anova_two_factor_without_r_tool (WORKBOOK_CONTROL (state->wbcg),
						       state->sheet, input, alpha, &dao);
	} else {
		err = anova_two_factor_with_r_tool (WORKBOOK_CONTROL (state->wbcg),
						    state->sheet, input, replication,
						    alpha, &dao);
	}
	switch (err) {
	case 0:
		gtk_widget_destroy (state->dialog);
		break;
	case 1:
		error_in_entry (state->wbcg, state->replication_entry,
				_("Each sample must contain the same number "
				  "of rows."));
		break;
	case 2:
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry),
				_("Given input range contains non-numeric "
				  "data."));
		break;
	case 3:
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry),
				_("The given input range should contain at "
				  "least two columns of data and the "
				  "labels."));
		break;
	case 4: 
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry), 
				_("One of the factor combinations does not contain\n"
				  "any observations!"));
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: %d."), err);
		error_in_entry (state->wbcg, GTK_WIDGET (state->input_entry), text);
		g_free (text);		
		break;
	}
	return;
}

/**
 * anova_two_factor_tool_update_sensitivity_cb:
 * @state:
 *
 * Update the dialog widgets sensitivity.
 * We cannot use tool_update_sensitivity_cb
 * since we are also considering whether in fact
 * an alpha and a replaication is given.
 **/
static void
anova_two_factor_tool_update_sensitivity_cb (GtkWidget *dummy, AnovaTwoFactorToolState *state)
{
	gboolean ready  = FALSE;
	int i, replication, err_alpha, err_replication;
	gnum_float alpha;
        Value *output_range;
        Value *input_range;

        output_range = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->output_entry), state->sheet);
        input_range = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);
	i = gnumeric_glade_group_value (state->gui, output_group);
	err_alpha = entry_to_float (GTK_ENTRY (state->alpha_entry), &alpha, FALSE);
	err_replication = entry_to_int (GTK_ENTRY (state->replication_entry), &replication, FALSE);

	ready = ((input_range != NULL) &&
                 (err_alpha == 0 && alpha > 0 && alpha < 1) &&
		 (err_replication == 0 && replication > 0) &&
                 ((i != 2) || (output_range != NULL)));

        if (input_range != NULL) value_release (input_range);
        if (output_range != NULL) value_release (output_range);

	gtk_widget_set_sensitive (state->ok_button, ready);
}


/**
 * dialog_anova_two_factor_tool_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_anova_two_factor_tool_init (AnovaTwoFactorToolState *state)
{
	if (dialog_tool_init ((GenericToolState *)state, "anova-two.glade", "ANOVA",
			      GTK_SIGNAL_FUNC (anova_two_factor_tool_ok_clicked_cb),
			      GTK_SIGNAL_FUNC (anova_two_factor_tool_update_sensitivity_cb),
			      GNUM_EE_SINGLE_RANGE)) {
		return TRUE;
	}

	state->alpha_entry = glade_xml_get_widget (state->gui, "alpha-entry");
	float_to_entry (GTK_ENTRY(state->alpha_entry), 0.05);
	state->replication_entry = glade_xml_get_widget (state->gui, "replication-entry");
	int_to_entry (GTK_ENTRY(state->replication_entry), 1);

	gtk_signal_connect_after (GTK_OBJECT (state->alpha_entry), "changed",
				  GTK_SIGNAL_FUNC (anova_two_factor_tool_update_sensitivity_cb), 
				  state);
	gtk_signal_connect_after (GTK_OBJECT (state->replication_entry), "changed",
				  GTK_SIGNAL_FUNC (anova_two_factor_tool_update_sensitivity_cb), 
				  state);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->alpha_entry));
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->replication_entry));

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       ANOVA_TWO_FACTOR_KEY);

	anova_two_factor_tool_update_sensitivity_cb (NULL, state);

	return FALSE;
}

/**
 * dialog_anova_two_factor_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
static int
dialog_anova_two_factor_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        AnovaTwoFactorToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, ANOVA_TWO_FACTOR_KEY))
		return 0;

	state = g_new (AnovaTwoFactorToolState, 1);
	(*(ToolType *)state) = TOOL_ANOVA_TWO_FACTOR;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->help_link = "anova.html#ANOVA-TWO-FACTOR-TOOL";
	state->input_var1_str = NULL;
	state->input_var2_str = NULL;

	if (dialog_anova_two_factor_tool_init (state)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the ANOVA (two factor) tool dialog."));
		g_free (state);
		return 0;
	}

	gtk_widget_show (state->dialog);

        return 0;
}

/**********************************************/
/*  End of ANOVA (Two Factor) tool code */
/**********************************************/


/*************************************************************************
 *
 * Modal dialog for tool selection
 *
 */

typedef int (*tool_fun_ptr_t)(WorkbookControlGUI *wbcg, Sheet *sheet);

typedef struct {
        char const *    name;
        tool_fun_ptr_t  fun;
} tool_list_t;

static tool_list_t tools[] = {
        { N_("Anova: Single Factor"),
	  dialog_anova_single_factor_tool },
        { N_("Anova: Two-Factor With Or Without Replication"),
	  dialog_anova_two_factor_tool },
        { N_("Correlation"),
	  dialog_correlation_tool },
        { N_("Covariance"),
	  dialog_covariance_tool },
        { N_("Descriptive Statistics"),
	  dialog_descriptive_stat_tool },
        { N_("Exponential Smoothing"),
	  dialog_exp_smoothing_tool },
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

static int selected_row;

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

static void
dialog_help_cb (GtkWidget *button, gchar *link)
{
	gnumeric_help_display (link);
}

void
dialog_data_analysis (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GladeXML  *gui;
	GtkWidget *dialog;
	GtkWidget *tool_list;
	GtkWidget *helpbutton;
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

        helpbutton = glade_xml_get_widget (gui, "helpbutton");
	gtk_signal_connect (GTK_OBJECT (helpbutton), "clicked",
			    GTK_SIGNAL_FUNC (dialog_help_cb),
			    "analysis-tools.html");

	tool_list = glade_xml_get_widget (gui, "clist1");
	gtk_signal_connect (GTK_OBJECT (tool_list), "select_row",
			    GTK_SIGNAL_FUNC (selection_made), NULL);

	for (i=0; tools[i].fun; i++) {
		char *tmp[2];
		tmp[0] = _(tools[i].name);
		tmp[1] = NULL;
	        gtk_clist_append (GTK_CLIST (tool_list), tmp);
	}
	gtk_clist_select_row (GTK_CLIST (tool_list), selected_row, 0);
	gnumeric_clist_moveto (GTK_CLIST (tool_list), selected_row);

	gtk_widget_grab_focus (GTK_WIDGET (tool_list));

	/* Run the dialog */
 loop:
	selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));

	if (selection == 2)
	        goto loop;

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
