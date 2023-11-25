/*
 * dialog-analysis-tools.c:
 *
 * Authors:
 *  Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *  Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <tools/analysis-tools.h>
#include <tools/analysis-anova.h>
#include <tools/analysis-histogram.h>
#include <tools/analysis-exp-smoothing.h>

#include <workbook.h>
#include <workbook-control.h>
#include <wbc-gtk.h>
#include <workbook-view.h>
#include <gui-util.h>
#include <parse-util.h>
#include <gnm-format.h>
#include <dialogs/tool-dialogs.h>
#include <dialogs/dao-gui-utils.h>
#include <sheet.h>
#include <expr.h>
#include <number-match.h>
#include <ranges.h>
#include <selection.h>
#include <value.h>
#include <commands.h>
#include <dialogs/help.h>

#include <widgets/gnm-dao.h>
#include <widgets/gnm-expr-entry.h>

#include <string.h>

/**********************************************/
/*  Generic guru items */
/**********************************************/



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


static char const * const grouped_by_group[] = {
	"grouped_by_row",
	"grouped_by_col",
	"grouped_by_area",
	NULL
};

typedef struct {
	GnmGenericToolState base;
	GtkWidget *predetermined_button;
	GtkWidget *calculated_button;
	GtkEntry  *n_entry;
	GtkEntry  *max_entry;
	GtkEntry  *min_entry;
} HistogramToolState;

static char const * const bin_type_group[] = {
	"bintype_no_inf_lower",
	"bintype_no_inf_upper",
	"bintype_p_inf_lower",
	"bintype_p_inf_upper",
	"bintype_m_inf_lower",
	"bintype_m_inf_upper",
	"bintype_pm_inf_lower",
	"bintype_pm_inf_upper",
	NULL
};

static char const * const chart_group[] = {
	"nochart-button",
	"histogram-button",
	"barchart-button",
	"columnchart-button",
	NULL
};

static char const * const n_group[] = {
	"n-button",
	"nm1-button",
	"nm2-button",
	"nm3-button",
	NULL
};

/* Note: the items in this group need to match */
/*       moving_average_type_t except that     */
/*       moving_average_type_central_sma is a  */
/*       subtype of moving_average_type_sma.   */
static char const * const moving_average_group[] = {
	"sma-button",
	"cma-button",
	"wma-button",
	"spencer-ma-button",
	NULL
};

static char const * const exp_smoothing_group[] = {
	"ses-h-button",
	"ses-r-button",
	"des-button",
	"ates-button",
	"mtes-button",
	NULL
};



typedef struct {
	GnmGenericToolState base;
	GtkWidget *summary_stats_button;
	GtkWidget *mean_stats_button;
	GtkWidget *kth_largest_button;
	GtkWidget *kth_smallest_button;
	GtkWidget *ss_button;
	GtkWidget *c_entry;
	GtkWidget *l_entry;
	GtkWidget *s_entry;
} DescriptiveStatState;

typedef struct {
	GnmGenericToolState base;
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
	GtkWidget *options_grid;
	GtkWidget *mean_diff_entry;
	GtkWidget *alpha_entry;
	ttest_type invocation;
} TTestState;

typedef struct {
	GnmGenericToolState base;
	GtkWidget *options_grid;
	GtkWidget *method_label;
	GtkWidget *periodic_button;
	GtkWidget *random_button;
	GtkWidget *period_label;
	GtkWidget *random_label;
	GtkWidget *period_entry;
	GtkWidget *random_entry;
	GtkWidget *number_entry;
	GtkWidget *offset_label;
	GtkWidget *offset_entry;
	GtkWidget *major_label;
	GtkWidget *row_major_button;
	GtkWidget *col_major_button;
} SamplingState;

typedef struct {
	GnmGenericToolState base;
	GtkWidget *interval_entry;
	GtkWidget *show_std_errors;
	GtkWidget *n_button;
	GtkWidget *nm1_button;
	GtkWidget *nm2_button;
	GtkWidget *prior_button;
	GtkWidget *central_button;
	GtkWidget *offset_button;
	GtkWidget *offset_spin;
	GtkWidget *graph_button;
	GtkWidget *sma_button;
	GtkWidget *cma_button;
	GtkWidget *wma_button;
	GtkWidget *spencer_button;
} AverageToolState;

typedef struct {
	GnmGenericToolState base;
        GtkWidget *damping_fact_entry;
        GtkWidget *g_damping_fact_entry;
        GtkWidget *s_damping_fact_entry;
        GtkWidget *s_period_entry;
	GtkWidget *show_std_errors;
	GtkWidget *n_button;
	GtkWidget *nm1_button;
	GtkWidget *nm2_button;
	GtkWidget *nm3_button;
	GtkWidget *graph_button;
	GtkWidget *ses_h_button;
	GtkWidget *ses_r_button;
	GtkWidget *des_button;
	GtkWidget *ates_button;
	GtkWidget *mtes_button;
} ExpSmoothToolState;

typedef struct {
	GnmGenericToolState base;
	GtkWidget *confidence_entry;
	GtkWidget *simple_linear_regression_radio;
	GtkWidget *switch_variables_check;
	GtkWidget *residuals_check;
} RegressionToolState;

typedef struct {
	GnmGenericToolState base;
	GtkWidget *alpha_entry;
} AnovaSingleToolState;

typedef struct {
	GnmGenericToolState base;
	GtkWidget *alpha_entry;
	GtkWidget *replication_entry;
} AnovaTwoFactorToolState;

typedef struct {
	GnmGenericToolState base;
	GtkWidget *alpha_entry;
} FTestToolState;


/**********************************************/
/*  Generic functions for the analysis tools. */
/*  Functions in this section are being used  */
/*  by virtually all tools.                   */
/**********************************************/


/**
 * error_in_entry:
 * @state:
 * @entry:
 * @err_str:
 *
 * Show an error dialog and select corresponding entry
 */
void
error_in_entry (GnmGenericToolState *state, GtkWidget *entry, char const *err_str)
{
        go_gtk_notice_nonmodal_dialog ((GtkWindow *) state->dialog,
				       &(state->warning_dialog),
				       GTK_MESSAGE_ERROR,
				       "%s", err_str);

	if (GNM_EXPR_ENTRY_IS (entry))
		gnm_expr_entry_grab_focus (GNM_EXPR_ENTRY (entry), TRUE);
	else
		focus_on_entry (GTK_ENTRY (entry));
}

static void
cb_tool_destroy (GnmGenericToolState  *state)
{
	if (state->gui != NULL)
		g_object_unref (state->gui);
	wbcg_edit_finish (state->wbcg, WBC_EDIT_REJECT, NULL);
	if (state->state_destroy)
		state->state_destroy (state);
	g_free (state);
}

/**
 * cb_tool_cancel_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_tool_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
			GnmGenericToolState *state)
{
	gtk_widget_destroy (state->dialog);
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
dialog_tool_init_buttons (GnmGenericToolState *state,
			  GCallback ok_function,
			  GCallback close_function)
{
	state->ok_button = go_gtk_builder_get_widget (state->gui, "okbutton");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (ok_function), state);

	state->cancel_button = go_gtk_builder_get_widget (state->gui,
						     "cancelbutton");
	if (close_function == NULL)
		g_signal_connect (G_OBJECT (state->cancel_button),
				  "clicked",
				  G_CALLBACK (cb_tool_cancel_clicked), state);
	else
		g_signal_connect (G_OBJECT (state->cancel_button),
				  "clicked",
				  G_CALLBACK (close_function), state);

	state->apply_button = go_gtk_builder_get_widget (state->gui, "applybutton");
	if (state->apply_button != NULL )
		g_signal_connect (G_OBJECT (state->apply_button),
				  "clicked",
				  G_CALLBACK (ok_function), state);
	state->help_button = go_gtk_builder_get_widget (state->gui, "helpbutton");
	if (state->help_button != NULL )
		gnm_init_help_button (state->help_button,
					   state->help_link);
}


/**
 * dialog_tool_init: (skip)
 * @state:
 * @gui_name:
 * @dialog_name:
 * @ok_function:
 * @sensitivity_cb:
 *
 * Create the dialog (guru).
 *
 **/
gboolean
dialog_tool_init (GnmGenericToolState *state,
		  WBCGtk *wbcg,
		  Sheet *sheet,
		  char const *help_file,
		  char const *gui_name,
		  char const *dialog_name,
		  char const *error_str,
		  char const *key,
		  GCallback ok_function,
		  GCallback close_function,
		  GCallback sensitivity_cb,
		  GnmExprEntryFlags flags)
{
	GtkGrid  *grid;
	GtkWidget *widget;

	state->wbcg  = wbcg;
	state->wb    = wb_control_get_workbook (GNM_WBC (wbcg));
	state->sheet = sheet;
	state->sv    = wb_control_cur_sheet_view (GNM_WBC (wbcg));
	state->warning_dialog = NULL;
	state->help_link      = help_file;
	state->state_destroy = NULL;

	state->gui = gnm_gtk_builder_load (gui_name, NULL, GO_CMD_CONTEXT (wbcg));
        if (state->gui == NULL)
		goto dialog_tool_init_error;

	state->dialog = go_gtk_builder_get_widget (state->gui, dialog_name);
        if (state->dialog == NULL)
		goto dialog_tool_init_error;


	dialog_tool_init_buttons (state, ok_function, close_function);

	widget = go_gtk_builder_get_widget (state->gui, "var1-label");
	if (widget == NULL) {
		state->input_entry = NULL;
	} else {
		guint left_attach, top_attach, width, height;

		grid = GTK_GRID (gtk_widget_get_parent (widget));
		state->input_entry = gnm_expr_entry_new (state->wbcg, TRUE);
		g_object_set (G_OBJECT (state->input_entry), "hexpand", TRUE, NULL);
		gnm_expr_entry_disable_tips (state->input_entry);
		gnm_expr_entry_set_flags (state->input_entry,
					  flags | GNM_EE_FORCE_ABS_REF,
					  GNM_EE_MASK);

		gtk_container_child_get (GTK_CONTAINER (grid), widget,
					 "left-attach", &left_attach,
					 "top-attach", &top_attach,
		                         "width", &width,
		                         "height", &height,
					 NULL);

		gtk_grid_attach (grid, GTK_WIDGET (state->input_entry),
				  left_attach + width, top_attach,
				  1, height);
		g_signal_connect_after (G_OBJECT (state->input_entry),
					"changed",
					G_CALLBACK (sensitivity_cb), state);
		gnm_editable_enters (GTK_WINDOW (state->dialog),
					  GTK_WIDGET (state->input_entry));
		gtk_label_set_mnemonic_widget (GTK_LABEL (widget),
					       GTK_WIDGET (state->input_entry));
		go_atk_setup_label (widget, GTK_WIDGET (state->input_entry));
		gtk_widget_show (GTK_WIDGET (state->input_entry));
	}


/*                                                        */
/* If there is a var2-label, we need a second input field */
/*                                                        */
	widget = go_gtk_builder_get_widget (state->gui, "var2-label");
	if (widget == NULL) {
		state->input_entry_2 = NULL;
	} else {
		guint left_attach, top_attach, width, height;

		state->input_entry_2 = gnm_expr_entry_new (state->wbcg, TRUE);
		g_object_set (G_OBJECT (state->input_entry_2), "hexpand", TRUE, NULL);
		gnm_expr_entry_disable_tips (state->input_entry_2);
		gnm_expr_entry_set_flags (state->input_entry_2,
					  GNM_EE_SINGLE_RANGE | GNM_EE_FORCE_ABS_REF, GNM_EE_MASK);
		grid = GTK_GRID (gtk_widget_get_parent (widget));

		gtk_container_child_get (GTK_CONTAINER (grid), widget,
					 "left-attach", &left_attach,
					 "top-attach", &top_attach,
		                         "width", &width,
		                         "height", &height,
					 NULL);

		gtk_grid_attach (grid, GTK_WIDGET (state->input_entry_2),
				  left_attach + width, top_attach,
				  1, height);
		g_signal_connect_after (G_OBJECT (state->input_entry_2),
					"changed",
					G_CALLBACK (sensitivity_cb), state);
		gnm_editable_enters (GTK_WINDOW (state->dialog),
					  GTK_WIDGET (state->input_entry_2));
		gtk_label_set_mnemonic_widget (GTK_LABEL (widget),
					       GTK_WIDGET (state->input_entry_2));
		go_atk_setup_label (widget, GTK_WIDGET (state->input_entry_2));
		gtk_widget_show (GTK_WIDGET (state->input_entry_2));
	}

	state->warning = go_gtk_builder_get_widget (state->gui, "warnings");
	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_tool_destroy);

	dialog_tool_init_outputs (state, sensitivity_cb);

	gnm_keyed_dialog (wbcg, GTK_WINDOW (state->dialog), key);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog),
					   state->wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED |
					   GNM_DIALOG_DESTROY_SHEET_RENAMED);

	return FALSE;

 dialog_tool_init_error:
	go_gtk_notice_dialog (wbcg_toplevel (wbcg),
			      GTK_MESSAGE_ERROR,
			      "%s", error_str);
	g_free (state);
	return TRUE;
}

/**
 * tool_load_selection:
 * @state:
 *
 * load the current selection in the output and input entries
 * show the dialog and focus the input_entry
 *
 **/
void
tool_load_selection (GnmGenericToolState *state, gboolean allow_multiple)
{
	GnmRange const *first = selection_first_range (state->sv, NULL, NULL);

	if (first != NULL) {
		if (allow_multiple) {
			char *text = selection_to_string (state->sv, TRUE);
			gnm_expr_entry_load_from_text  (state->input_entry,
							text);
			g_free (text);
		} else
			gnm_expr_entry_load_from_range (state->input_entry,
				state->sheet, first);
		if (state->gdao != NULL)
			gnm_dao_load_range (GNM_DAO (state->gdao), first);
	}

	gtk_widget_show (state->dialog);
	gnm_expr_entry_grab_focus (GNM_EXPR_ENTRY (state->input_entry),
				   TRUE);

}

/**
 * tool_setup_update: (skip)
 */
GtkWidget *
tool_setup_update (GnmGenericToolState* state, char const *name, GCallback cb,
		   gpointer closure)
{
	GtkWidget *w = go_gtk_builder_get_widget (state->gui, name);
	if (GTK_IS_SPIN_BUTTON (w)) {
		g_signal_connect_after (w, "value-changed", cb, closure);
		gnm_editable_enters (GTK_WINDOW (state->dialog), w);
	} else if (GTK_IS_ENTRY (w)) {
		g_signal_connect_after (w, "changed", cb, closure);
		gnm_editable_enters (GTK_WINDOW (state->dialog), w);
	} else if (GTK_IS_TOGGLE_BUTTON (w))
		g_signal_connect_after (w, "toggled", cb, closure);
	else
		g_warning ("tool_setup_update called with unknown type");
	return w;
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
 * are one or two standard input and one output item, permitting multiple
 * areas as first input.
 *
 * used by:
 * Correlation
 * Covariance
 * RankPercentile
 * FourierAnalysis
 *
 **/
static void
tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
			    GnmGenericToolState *state)
{
        GSList *input_range;

	/* Checking Input Range */
        input_range = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->input_entry), state->sheet);
	if (input_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->warning),
				    _("The input range is invalid."));
		gtk_widget_set_sensitive (state->ok_button, FALSE);
		return;
	} else
		range_list_destroy (input_range);

	/* Checking Output Page */
	if (!gnm_dao_is_ready (GNM_DAO (state->gdao))) {
		gtk_label_set_text (GTK_LABEL (state->warning),
				    _("The output specification "
				      "is invalid."));
		gtk_widget_set_sensitive (state->ok_button, FALSE);
		return;
	}

	gtk_label_set_text (GTK_LABEL (state->warning), "");
	gtk_widget_set_sensitive (state->ok_button, TRUE);

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
corr_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			 GnmGenericToolState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_generic_t  *data;

        char   *text;
	GtkWidget *w;

	if (state->warning_dialog != NULL)
		gtk_widget_destroy (state->warning_dialog);

	data = g_new0 (analysis_tools_data_generic_t, 1);
	dao  = parse_output (state, NULL);

	data->input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->input_entry), state->sheet);
	data->group_by = gnm_gui_group_value (state->gui, grouped_by_group);

	w = go_gtk_builder_get_widget (state->gui, "labels_button");
        data->labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	if (cmd_analysis_tool (GNM_WBC (state->wbcg), state->sheet,
			       dao, data, analysis_tool_correlation_engine, FALSE)) {

		switch (data->err - 1) {
		case GROUPED_BY_ROW:
			error_in_entry ((GnmGenericToolState *) state,
					GTK_WIDGET (state->input_entry),
					_("The selected input rows must have equal size!"));
			break;
		case GROUPED_BY_COL:
			error_in_entry ((GnmGenericToolState *) state,
					GTK_WIDGET (state->input_entry),
					_("The selected input columns must have equal size!"));
			break;
		case GROUPED_BY_AREA:
			error_in_entry ((GnmGenericToolState *) state,
					GTK_WIDGET (state->input_entry),
					_("The selected input areas must have equal size!"));
			break;
		default:
			text = g_strdup_printf (
				_("An unexpected error has occurred: %d."), data->err);
			error_in_entry ((GnmGenericToolState *) state,
					GTK_WIDGET (state->input_entry), text);
			g_free (text);
			break;
		}
		range_list_destroy (data->input);
		g_free (dao);
		g_free (data);
	} else
		gtk_widget_destroy (state->dialog);
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
int
dialog_correlation_tool (WBCGtk *wbcg, Sheet *sheet)
{
        GnmGenericToolState *state;
	char const * plugins[] = { "Gnumeric_fnstat",
				   NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, CORRELATION_KEY))
		return 0;

	state = g_new0 (GnmGenericToolState, 1);

	if (dialog_tool_init (state, wbcg, sheet,
			      GNUMERIC_HELP_LINK_CORRELATION,
			      "res:ui/correlation.ui", "Correlation",
			      _("Could not create the Correlation Tool dialog."),
			      CORRELATION_KEY,
			      G_CALLBACK (corr_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (tool_update_sensitivity_cb),
			      0))
		return 0;

	gnm_dao_set_put (GNM_DAO (state->gdao), TRUE, TRUE);
	tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

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
cov_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			GnmGenericToolState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_generic_t  *data;

        char   *text;
	GtkWidget *w;

	if (state->warning_dialog != NULL)
		gtk_widget_destroy (state->warning_dialog);

	data = g_new0 (analysis_tools_data_generic_t, 1);
	dao  = parse_output (state, NULL);

	data->input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->input_entry), state->sheet);
	data->group_by = gnm_gui_group_value (state->gui, grouped_by_group);

	w = go_gtk_builder_get_widget (state->gui, "labels_button");
        data->labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	if (cmd_analysis_tool (GNM_WBC (state->wbcg), state->sheet,
			       dao, data, analysis_tool_covariance_engine, FALSE)) {

		switch (data->err - 1) {
		case GROUPED_BY_ROW:
			error_in_entry ((GnmGenericToolState *) state,
					GTK_WIDGET (state->input_entry),
					_("The selected input rows must have equal size!"));
			break;
		case GROUPED_BY_COL:
			error_in_entry ((GnmGenericToolState *) state,
					GTK_WIDGET (state->input_entry),
					_("The selected input columns must have equal size!"));
			break;
		case GROUPED_BY_AREA:
			error_in_entry ((GnmGenericToolState *) state,
					GTK_WIDGET (state->input_entry),
					_("The selected input areas must have equal size!"));
			break;
		default:
			text = g_strdup_printf (
				_("An unexpected error has occurred: %d."), data->err);
			error_in_entry ((GnmGenericToolState *) state,
					GTK_WIDGET (state->input_entry), text);
			g_free (text);
			break;
		}
		range_list_destroy (data->input);
		g_free (dao);
		g_free (data);
	} else
		gtk_widget_destroy (state->dialog);
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
int
dialog_covariance_tool (WBCGtk *wbcg, Sheet *sheet)
{
        GnmGenericToolState *state;
	char const * plugins[] = { "Gnumeric_fnstat",
				   NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, COVARIANCE_KEY))
		return 0;

	state = g_new0 (GnmGenericToolState, 1);

	if (dialog_tool_init (state, wbcg, sheet,
			      GNUMERIC_HELP_LINK_COVARIANCE,
			      "res:ui/covariance.ui", "Covariance",
			      _("Could not create the Covariance Tool dialog."),
			      COVARIANCE_KEY,
			      G_CALLBACK (cov_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (tool_update_sensitivity_cb),
			      0))
		return 0;

	gnm_dao_set_put (GNM_DAO (state->gdao), TRUE, TRUE);
	tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

        return 0;
}

/**********************************************/
/*  End of covariance tool code */
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
rank_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			 GnmGenericToolState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_ranking_t  *data;

	GtkWidget *w;

	data = g_new0 (analysis_tools_data_ranking_t, 1);
	dao  = parse_output (state, NULL);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->input_entry), state->sheet);
	data->base.group_by = gnm_gui_group_value (state->gui, grouped_by_group);

	w = go_gtk_builder_get_widget (state->gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	w = go_gtk_builder_get_widget (state->gui, "rank_button");
        data->av_ties = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));


	if (!cmd_analysis_tool (GNM_WBC (state->wbcg), state->sheet,
				dao, data, analysis_tool_ranking_engine, TRUE))
		gtk_widget_destroy (state->dialog);
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
int
dialog_ranking_tool (WBCGtk *wbcg, Sheet *sheet)
{
        GnmGenericToolState *state;
	char const * plugins[] = { "Gnumeric_fnstat",
				   "Gnumeric_fnlookup",
				   NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, RANK_PERCENTILE_KEY))
		return 0;

	state = g_new0 (GnmGenericToolState, 1);

	if (dialog_tool_init (state, wbcg, sheet,
			      GNUMERIC_HELP_LINK_RANKING,
			      "res:ui/rank.ui", "RankPercentile",
			      _("Could not create the Rank and Percentile "
				"Tools dialog."),
			      RANK_PERCENTILE_KEY,
			      G_CALLBACK (rank_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (tool_update_sensitivity_cb),
			      0))
		return 0;

	gnm_dao_set_put (GNM_DAO (state->gdao), TRUE, TRUE);
	tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

        return 0;
}

/**********************************************/
/*  End of rank and percentile tool code */
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
fourier_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			    GnmGenericToolState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_fourier_t  *data;

	GtkWidget               *w;

	data = g_new0 (analysis_tools_data_fourier_t, 1);
	dao  = parse_output (state, NULL);

	data->base.wbc = GNM_WBC (state->wbcg);
	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->input_entry), state->sheet);
	data->base.group_by = gnm_gui_group_value (state->gui, grouped_by_group);

	w = go_gtk_builder_get_widget (state->gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	w = go_gtk_builder_get_widget (state->gui, "inverse_button");
	data->inverse = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)) != 0;

	if (!cmd_analysis_tool (GNM_WBC (state->wbcg), state->sheet,
				dao, data, analysis_tool_fourier_engine, TRUE))
		gtk_widget_destroy (state->dialog);

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
int
dialog_fourier_tool (WBCGtk *wbcg, Sheet *sheet)
{
        GnmGenericToolState *state;
	char const * plugins[] = { "Gnumeric_fnTimeSeriesAnalysis",
				   "Gnumeric_fncomplex",
				   NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, FOURIER_KEY))
		return 0;

	state = g_new0 (GnmGenericToolState, 1);

	if (dialog_tool_init (state, wbcg, sheet,
			      GNUMERIC_HELP_LINK_FOURIER_ANALYSIS,
			      "res:ui/fourier-analysis.ui", "FourierAnalysis",
			      _("Could not create the Fourier Analysis Tool "
				"dialog."),
			      FOURIER_KEY,
			      G_CALLBACK (fourier_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (tool_update_sensitivity_cb),
			      0))
		return 0;

	gnm_dao_set_put (GNM_DAO (state->gdao), TRUE, TRUE);
	tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

        return 0;
}

/**********************************************/
/*  End of Fourier analysis tool code */
/**********************************************/

/**********************************************/
/*  Begin of descriptive statistics tool code */
/**********************************************/

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
cb_desc_stat_tool_ok_clicked (G_GNUC_UNUSED GtkWidget *button,
			      DescriptiveStatState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_descriptive_t  *data;

	GtkWidget *w;

	data = g_new0 (analysis_tools_data_descriptive_t, 1);
	dao  = parse_output ((GnmGenericToolState *)state, NULL);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->base.group_by = gnm_gui_group_value (state->base.gui, grouped_by_group);

	data->summary_statistics = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->summary_stats_button));
	data->confidence_level = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->mean_stats_button));
	data->kth_largest = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->kth_largest_button));
	data->kth_smallest = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->kth_smallest_button));
	data->use_ssmedian = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->ss_button));

	if (data->confidence_level == 1)
		data->c_level = gtk_spin_button_get_value
			(GTK_SPIN_BUTTON (state->c_entry));

	if (data->kth_largest == 1)
		entry_to_int (GTK_ENTRY (state->l_entry), &data->k_largest, TRUE);
	if (data->kth_smallest == 1)
		entry_to_int (GTK_ENTRY (state->s_entry), &data->k_smallest, TRUE);

	w = go_gtk_builder_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg), state->base.sheet,
				dao, data, analysis_tool_descriptive_engine, TRUE))
		gtk_widget_destroy (state->base.dialog);
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
desc_stat_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				      DescriptiveStatState *state)
{
	gboolean stats_button, ci_button, largest_button, smallest_button;
        GSList *input_range;

	/* Part 1: set the buttons on the statistics page. */

	stats_button = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->summary_stats_button));
	gtk_widget_set_sensitive (state->ss_button, stats_button);

	ci_button = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->mean_stats_button));
	gtk_widget_set_sensitive (state->c_entry, ci_button);

	largest_button = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->kth_largest_button));
	gtk_widget_set_sensitive (state->l_entry, largest_button);

	smallest_button = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->kth_smallest_button));
	gtk_widget_set_sensitive (state->s_entry, smallest_button);

	/* Part 2: set the okay button */

	/* Checking Input Page */
        input_range = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry),
		state->base.sheet);
	if (input_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The input range is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	} else
		range_list_destroy (input_range);

	/* Checking Statistics Page */
	if (!(stats_button || ci_button || largest_button || smallest_button)) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("No statistics are selected."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	if (ci_button) {
		gdouble c_level = gtk_spin_button_get_value
			(GTK_SPIN_BUTTON (state->c_entry));
		if (!(c_level > 0 && c_level <1)) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The confidence level should be "
					      "between 0 and 1."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			return;
		}
	}

	if (largest_button) {
		int k;
		if ((0 != entry_to_int (GTK_ENTRY (state->l_entry), &k, FALSE))
		    || !(k >0)) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("K must be a positive integer."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
		}
	}

	if (smallest_button) {
		int k;
		if ((0 != entry_to_int (GTK_ENTRY (state->s_entry), &k, FALSE))
		    || !(k >0)) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("K must be a positive integer."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
		}
	}

	/* Checking Output Page */
	if (!gnm_dao_is_ready (GNM_DAO (state->base.gdao))) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The output specification "
				      "is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	gtk_label_set_text (GTK_LABEL (state->base.warning), "");
	gtk_widget_set_sensitive (state->base.ok_button, TRUE);

	return;
}


/**
 * dialog_descriptive_stat_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
int
dialog_descriptive_stat_tool (WBCGtk *wbcg, Sheet *sheet)
{
        DescriptiveStatState *state;
	char const * plugins[] = {"Gnumeric_fnstat",
				  "Gnumeric_fnmath",
				  NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, DESCRIPTIVE_STATS_KEY))
		return 0;

	state = g_new0 (DescriptiveStatState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_DESCRIPTIVE_STATS,
			      "res:ui/descriptive-stats.ui", "DescStats",
			      _("Could not create the Descriptive Statistics "
				"Tool dialog."),
			      DESCRIPTIVE_STATS_KEY,
			      G_CALLBACK (cb_desc_stat_tool_ok_clicked), NULL,
			      G_CALLBACK (desc_stat_tool_update_sensitivity_cb),
			      0))
	{
		g_free(state);
		return 0;
	}

	state->summary_stats_button  = go_gtk_builder_get_widget
		(state->base.gui, "summary_stats_button");
	state->ss_button  = go_gtk_builder_get_widget
		(state->base.gui, "ss_button");
	state->mean_stats_button  = go_gtk_builder_get_widget
		(state->base.gui, "mean_stats_button");
	state->kth_largest_button  = go_gtk_builder_get_widget
		(state->base.gui, "kth_largest_button");
	state->kth_smallest_button  = go_gtk_builder_get_widget
		(state->base.gui, "kth_smallest_button");
	state->c_entry  = go_gtk_builder_get_widget (state->base.gui, "c_entry");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->c_entry), 0.95);
	state->l_entry  = go_gtk_builder_get_widget (state->base.gui, "l_entry");
	int_to_entry (GTK_ENTRY (state->l_entry), 1);
	state->s_entry  = go_gtk_builder_get_widget (state->base.gui, "s_entry");
	int_to_entry (GTK_ENTRY (state->s_entry), 1);


	g_signal_connect_after (G_OBJECT (state->summary_stats_button),
		"toggled",
		G_CALLBACK (desc_stat_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->mean_stats_button),
		"toggled",
		G_CALLBACK (desc_stat_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->kth_largest_button),
		"toggled",
		G_CALLBACK (desc_stat_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->kth_smallest_button),
		"toggled",
		G_CALLBACK (desc_stat_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->c_entry),
		"changed",
		G_CALLBACK (desc_stat_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->l_entry),
		"changed",
		G_CALLBACK (desc_stat_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->s_entry),
		"changed",
		G_CALLBACK (desc_stat_tool_update_sensitivity_cb), state);
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->c_entry));
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->l_entry));
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->s_entry));

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	desc_stat_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

        return 0;
}


/**********************************************/
/*  End of descriptive statistics tool code */
/**********************************************/


/**********************************************/
/*  Begin of ttest tool code */
/**********************************************/

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
ttest_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			  TTestState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_ttests_t  *data;

	GtkWidget *w;
	int    err = 0;

	data = g_new0 (analysis_tools_data_ttests_t, 1);
	dao  = parse_output ((GnmGenericToolState *)state, NULL);

	data->base.wbc = GNM_WBC (state->base.wbcg);

	if (state->base.warning_dialog != NULL)
		gtk_widget_destroy (state->base.warning_dialog);

	data->base.range_1 = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);

	data->base.range_2 = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry_2), state->base.sheet);

	w = go_gtk_builder_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

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

	err = entry_to_float (GTK_ENTRY (state->mean_diff_entry), &data->mean_diff, TRUE);
	err = entry_to_float (GTK_ENTRY (state->alpha_entry), &data->base.alpha, TRUE);

	switch (state->invocation) {
	case TTEST_PAIRED:
		if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg),
					state->base.sheet,
					dao, data, analysis_tool_ttest_paired_engine,
					TRUE))
			gtk_widget_destroy (state->base.dialog);
		break;
	case TTEST_UNPAIRED_EQUALVARIANCES:
		if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg), state->base.sheet,
					dao, data, analysis_tool_ttest_eqvar_engine, TRUE))
			gtk_widget_destroy (state->base.dialog);
		break;
	case TTEST_UNPAIRED_UNEQUALVARIANCES:
		if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg), state->base.sheet,
					dao, data, analysis_tool_ttest_neqvar_engine, TRUE))
			gtk_widget_destroy (state->base.dialog);
		break;
	case TTEST_ZTEST:
		err = entry_to_float (GTK_ENTRY (state->var1_variance), &data->var1, TRUE);
		if (err != 0 || data->var1 <= 0) {
			error_in_entry ((GnmGenericToolState *) state, GTK_WIDGET (state->var1_variance),
					_("Please enter a valid\n"
					  "population variance for variable 1."));
			g_free (data);
			g_free (dao);
			return;
		}
		err = entry_to_float (GTK_ENTRY (state->var2_variance), &data->var2, TRUE);
		if (err != 0 || data->var2 <= 0) {
			error_in_entry ((GnmGenericToolState *) state, GTK_WIDGET (state->var2_variance),
					_("Please enter a valid\n"
					  "population variance for variable 2."));
			g_free (data);
			g_free (dao);
			return;
		}

		if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg), state->base.sheet,
					dao, data, analysis_tool_ztest_engine, TRUE))
			gtk_widget_destroy (state->base.dialog);
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
ttest_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
			     TTestState *state)
{
	gboolean ready  = FALSE;
	gboolean input_1_ready  = FALSE;
	gboolean input_2_ready  = FALSE;
	gboolean output_ready  = FALSE;
	gboolean mean_diff_ready = FALSE;
	gboolean alpha_ready = FALSE;
	int err;
	gnm_float mean_diff, alpha;
        GnmValue *input_range;
        GnmValue *input_range_2;

        input_range = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	input_range_2 = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry_2), state->base.sheet);

	err = entry_to_float (GTK_ENTRY (state->mean_diff_entry), &mean_diff, FALSE);
	mean_diff_ready = (err == 0);
	err = entry_to_float (GTK_ENTRY (state->alpha_entry), &alpha, FALSE);
	alpha_ready = (err == 0 && alpha > 0 && alpha < 1);
	input_1_ready = (input_range != NULL);
	input_2_ready = ((state->base.input_entry_2 == NULL) || (input_range_2 != NULL));
	output_ready =  gnm_dao_is_ready (GNM_DAO (state->base.gdao));

        value_release (input_range);
	value_release (input_range_2);

	ready = input_1_ready && input_2_ready && output_ready && alpha_ready && mean_diff_ready;
	gtk_widget_set_sensitive (state->base.ok_button, ready);

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
dialog_ttest_realized (G_GNUC_UNUSED GtkWidget *widget,
		       TTestState *state)
{
	GtkAllocation alloc;

	gtk_widget_get_allocation (state->options_grid, &alloc);
	gtk_widget_set_size_request (state->options_grid,
				     alloc.width, alloc.height);

	gtk_widget_get_allocation (state->paired_button, &alloc);
	gtk_widget_set_size_request (state->paired_button,
				     alloc.width, alloc.height);

	gtk_widget_get_allocation (state->unpaired_button, &alloc);
	gtk_widget_set_size_request (state->unpaired_button,
				     alloc.width, alloc.height);

	gtk_widget_get_allocation (state->variablespaired_label, &alloc);
	gtk_widget_set_size_request (state->variablespaired_label,
				     alloc.width, alloc.height);

	ttest_paired_toggled_cb (state->paired_button, state);
	dialog_ttest_adjust_to_invocation (state);
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
int
dialog_ttest_tool (WBCGtk *wbcg, Sheet *sheet, ttest_type test)
{
        TTestState *state;
	GtkDialog *dialog;
	char const * plugins[] = {"Gnumeric_fnstat",
				  "Gnumeric_fnmath",
				  "Gnumeric_fninfo",
				  "Gnumeric_fnlogical",
				  NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	dialog = gnm_dialog_raise_if_exists (wbcg, TTEST_KEY);
	if (dialog) {
		state = g_object_get_data (G_OBJECT (dialog), "state");
                state->invocation = test;
		dialog_ttest_adjust_to_invocation (state);
		return 0;
	}

	state = g_new0 (TTestState, 1);
	state->invocation = test;

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_MEAN_TESTS,
			      "res:ui/mean-tests.ui", "MeanTests",
			      _("Could not create the Mean Tests Tool dialog."),
			      TTEST_KEY,
			      G_CALLBACK (ttest_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (ttest_update_sensitivity_cb),
			      GNM_EE_SINGLE_RANGE))
	{
		g_free(state);
		return 0;
	}

	state->paired_button  = go_gtk_builder_get_widget (state->base.gui, "paired-button");
	state->unpaired_button  = go_gtk_builder_get_widget (state->base.gui, "unpaired-button");
	state->variablespaired_label = go_gtk_builder_get_widget (state->base.gui, "variablespaired-label");
	state->known_button  = go_gtk_builder_get_widget (state->base.gui, "known-button");
	state->unknown_button  = go_gtk_builder_get_widget (state->base.gui, "unknown-button");
	state->varianceknown_label = go_gtk_builder_get_widget (state->base.gui, "varianceknown-label");
	state->equal_button  = go_gtk_builder_get_widget (state->base.gui, "equal-button");
	state->unequal_button  = go_gtk_builder_get_widget (state->base.gui, "unequal-button");
	state->varianceequal_label = go_gtk_builder_get_widget (state->base.gui, "varianceequal-label");
	state->options_grid = go_gtk_builder_get_widget (state->base.gui, "options-grid");
	state->var1_variance_label = go_gtk_builder_get_widget (state->base.gui, "var1_variance-label");
	state->var1_variance = go_gtk_builder_get_widget (state->base.gui, "var1-variance");
	state->var2_variance_label = go_gtk_builder_get_widget (state->base.gui, "var2_variance-label");
	state->var2_variance = go_gtk_builder_get_widget (state->base.gui, "var2-variance");
	state->mean_diff_entry = go_gtk_builder_get_widget (state->base.gui, "meandiff");
	float_to_entry (GTK_ENTRY (state->mean_diff_entry), 0);
	state->alpha_entry = go_gtk_builder_get_widget (state->base.gui, "one_alpha");
	float_to_entry (GTK_ENTRY (state->alpha_entry), 0.05);

	g_signal_connect_after (G_OBJECT (state->paired_button),
		"toggled",
		G_CALLBACK (ttest_update_sensitivity_cb), state);
	g_signal_connect (G_OBJECT (state->paired_button),
		"toggled",
		G_CALLBACK (ttest_paired_toggled_cb), state);
	g_signal_connect_after (G_OBJECT (state->known_button),
		"toggled",
		G_CALLBACK (ttest_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->mean_diff_entry),
		"changed",
		G_CALLBACK (ttest_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->alpha_entry),
		"changed",
		G_CALLBACK (ttest_update_sensitivity_cb), state);
	g_signal_connect (G_OBJECT (state->known_button),
		"toggled",
		G_CALLBACK (ttest_known_toggled_cb), state);
	g_signal_connect (G_OBJECT (state->base.dialog),
		"realize",
		G_CALLBACK (dialog_ttest_realized), state);
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->var1_variance));
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->var2_variance));
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->mean_diff_entry));
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->alpha_entry));

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	ttest_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, FALSE);

        return 0;
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
ftest_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			  FTestToolState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_generic_b_t  *data;

	GtkWidget *w;

	data = g_new0 (analysis_tools_data_generic_b_t, 1);
	dao  = parse_output ((GnmGenericToolState *)state, NULL);

	data->wbc = GNM_WBC (state->base.wbcg);

	if (state->base.warning_dialog != NULL)
		gtk_widget_destroy (state->base.warning_dialog);

	data->range_1 = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);

	data->range_2 =  gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry_2), state->base.sheet);

	w = go_gtk_builder_get_widget (state->base.gui, "labels_button");
        data->labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	entry_to_float (GTK_ENTRY (state->alpha_entry), &data->alpha, TRUE);

	if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg), state->base.sheet,
				dao, data, analysis_tool_ftest_engine, TRUE))
		gtk_widget_destroy (state->base.dialog);

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
ftest_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
			     FTestToolState *state)
{
	gboolean ready  = FALSE;
	gboolean input_1_ready  = FALSE;
	gboolean input_2_ready  = FALSE;
	gboolean output_ready  = FALSE;
	gboolean alpha_ready = FALSE;
	int err;
	gnm_float  alpha;
        GnmValue *input_range;
        GnmValue *input_range_2;

        input_range = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	input_range_2 = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry_2), state->base.sheet);

	err = entry_to_float (GTK_ENTRY (state->alpha_entry), &alpha, FALSE);
	alpha_ready = (err == 0 && alpha > 0 && alpha < 1);
	input_1_ready = (input_range != NULL);
	input_2_ready = ((state->base.input_entry_2 == NULL) || (input_range_2 != NULL));
	output_ready = gnm_dao_is_ready (GNM_DAO (state->base.gdao));

        value_release (input_range);
        value_release (input_range_2);

	ready = input_1_ready && input_2_ready && output_ready && alpha_ready;
	gtk_widget_set_sensitive (state->base.ok_button, ready);

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
int
dialog_ftest_tool (WBCGtk *wbcg, Sheet *sheet)
{
        FTestToolState *state;
	char const * plugins[] = { "Gnumeric_fnstat",
				   NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, FTEST_KEY))
		return 0;

	state = g_new0 (FTestToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_F_TEST_TWO_SAMPLE,
			      "res:ui/variance-tests.ui", "VarianceTests",
			      _("Could not create the FTest Tool dialog."),
			      FTEST_KEY,
			      G_CALLBACK (ftest_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (ftest_update_sensitivity_cb),
			      GNM_EE_SINGLE_RANGE))
	{
		g_free(state);
		return 0;
	}

	state->alpha_entry = go_gtk_builder_get_widget (state->base.gui, "one_alpha");
	float_to_entry (GTK_ENTRY (state->alpha_entry), 0.05);
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->alpha_entry));
	g_signal_connect_after (G_OBJECT (state->alpha_entry),
		"changed",
		G_CALLBACK (ftest_update_sensitivity_cb), state);

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	ftest_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, FALSE);

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
sampling_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				     SamplingState *state)
{
	int periodic, size, number, err;
        GSList *input_range;

        input_range = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);

	if (input_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The input range is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	} else
		range_list_destroy (input_range);

	err = entry_to_int (GTK_ENTRY (state->number_entry), &number, FALSE);

	if (err != 0 || number < 1) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The requested number of samples is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

        periodic = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->periodic_button));

	if (periodic) {
		err = entry_to_int
			(GTK_ENTRY (state->period_entry), &size, FALSE);
		if (err != 0 || size < 1) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The requested period is invalid."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			return;
		}
		err = entry_to_int
			(GTK_ENTRY (state->offset_entry), &number, FALSE);
		if (err != 0 || number < 0) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The requested offset is invalid."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			return;
		}
	} else {
		err = entry_to_int
			(GTK_ENTRY (state->random_entry), &size, FALSE);
		if (err != 0 || size < 1) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The requested sample size is invalid."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			return;
		}
	}

       if (!gnm_dao_is_ready (GNM_DAO (state->base.gdao))) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The output specification "
				      "is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

       gtk_label_set_text (GTK_LABEL (state->base.warning), "");
       gtk_widget_set_sensitive (state->base.ok_button, TRUE);
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
sampling_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			     SamplingState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_sampling_t  *data;

	GtkWidget *w;

	data = g_new0 (analysis_tools_data_sampling_t, 1);
	dao  = parse_output ((GnmGenericToolState *)state, NULL);

	data->base.wbc = GNM_WBC (state->base.wbcg);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->base.group_by = gnm_gui_group_value (state->base.gui, grouped_by_group);

	w = go_gtk_builder_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

        data->periodic = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->periodic_button));

	if (data->periodic) {
		entry_to_int (GTK_ENTRY (state->period_entry), &data->period, TRUE);
		entry_to_int (GTK_ENTRY (state->offset_entry), &data->offset, TRUE);
		data->row_major = gtk_toggle_button_get_active
			(GTK_TOGGLE_BUTTON (state->row_major_button));
	} else
		entry_to_int (GTK_ENTRY (state->random_entry), &data->size, TRUE);

	entry_to_int (GTK_ENTRY (state->number_entry), &data->number, TRUE);

	if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg), state->base.sheet,
				dao, data, analysis_tool_sampling_engine, TRUE))
		gtk_widget_destroy (state->base.dialog);
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
		gtk_widget_show (state->offset_label);
		gtk_widget_show (state->offset_entry);
		gtk_widget_show (state->major_label);
		gtk_widget_show (state->row_major_button);
		gtk_widget_show (state->col_major_button);
	} else {
		gtk_widget_hide (state->period_label);
		gtk_widget_hide (state->period_entry);
		gtk_widget_hide (state->period_entry);
		gtk_widget_hide (state->offset_label);
		gtk_widget_hide (state->offset_entry);
		gtk_widget_hide (state->major_label);
		gtk_widget_hide (state->row_major_button);
		gtk_widget_hide (state->col_major_button);
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
dialog_sampling_realized (G_GNUC_UNUSED GtkWidget *widget,
			  SamplingState *state)
{
	GtkAllocation alloc;

	gtk_widget_get_allocation (state->options_grid, &alloc);
	gtk_widget_set_size_request (state->options_grid,
				     alloc.width, alloc.height);

	gtk_widget_get_allocation (state->random_button, &alloc);
	gtk_widget_set_size_request (state->random_button,
				     alloc.width, alloc.height);

	gtk_widget_get_allocation (state->periodic_button, &alloc);
	gtk_widget_set_size_request (state->periodic_button,
				     alloc.width, alloc.height);

	gtk_widget_get_allocation (state->method_label, &alloc);
	gtk_widget_set_size_request (state->method_label,
				     alloc.width, alloc.height);

	sampling_method_toggled_cb (state->periodic_button, state);
}

/**
 * dialog_sampling_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
int
dialog_sampling_tool (WBCGtk *wbcg, Sheet *sheet)
{
        SamplingState *state;
	char const * plugins[] = { "Gnumeric_fnlookup",
				   "Gnumeric_fnrandom",
				   NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, SAMPLING_KEY)) {
		return 0;
	}

	state = g_new0 (SamplingState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_SAMPLING,
			      "res:ui/sampling.ui", "Sampling",
			      _("Could not create the Sampling Tool dialog."),
			      SAMPLING_KEY,
			      G_CALLBACK (sampling_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (sampling_tool_update_sensitivity_cb),
			      0))
	{
		g_free(state);
		return 0;
	}

	state->periodic_button  = go_gtk_builder_get_widget (state->base.gui, "periodic-button");
	state->random_button  = go_gtk_builder_get_widget (state->base.gui, "random-button");
	state->method_label = go_gtk_builder_get_widget (state->base.gui, "method-label");
	state->options_grid = go_gtk_builder_get_widget (state->base.gui, "options-grid");
	state->period_label = go_gtk_builder_get_widget (state->base.gui, "period-label");
	state->random_label = go_gtk_builder_get_widget (state->base.gui, "random-label");
	state->period_entry = go_gtk_builder_get_widget (state->base.gui, "period-entry");
	state->random_entry = go_gtk_builder_get_widget (state->base.gui, "random-entry");
	state->number_entry = go_gtk_builder_get_widget (state->base.gui, "number-entry");
	state->offset_label = go_gtk_builder_get_widget (state->base.gui, "offset-label");
	state->offset_entry = go_gtk_builder_get_widget (state->base.gui, "offset-entry");
	state->major_label = go_gtk_builder_get_widget (state->base.gui, "pdir-label");
	state->row_major_button = go_gtk_builder_get_widget (state->base.gui, "row-major-button");
	state->col_major_button = go_gtk_builder_get_widget (state->base.gui, "col-major-button");

	int_to_entry (GTK_ENTRY (state->number_entry), 1);
	int_to_entry (GTK_ENTRY (state->offset_entry), 0);

	g_signal_connect_after (G_OBJECT (state->periodic_button),
		"toggled",
		G_CALLBACK (sampling_tool_update_sensitivity_cb), state);
	g_signal_connect (G_OBJECT (state->periodic_button),
		"toggled",
		G_CALLBACK (sampling_method_toggled_cb), state);
	g_signal_connect (G_OBJECT (state->base.dialog),
		"realize",
		G_CALLBACK (dialog_sampling_realized), state);
	g_signal_connect_after (G_OBJECT (state->period_entry),
		"changed",
		G_CALLBACK (sampling_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->random_entry),
		"changed",
		G_CALLBACK (sampling_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->number_entry),
		"changed",
		G_CALLBACK (sampling_tool_update_sensitivity_cb), state);
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->period_entry));
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->random_entry));
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->number_entry));

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	sampling_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

        return 0;
}
/**********************************************/
/*  End of sampling tool code */
/**********************************************/

/**********************************************/
/*  Begin of Regression tool code */
/**********************************************/

static gint
regression_tool_calc_height (GnmValue *val)
{
		GnmRange r;

		if (NULL == range_init_value (&r, val))
			return 0;
		return range_height (&r);
}

static gint
regression_tool_calc_width (GnmValue *val)
{
		GnmRange r;

		if (NULL == range_init_value (&r, val))
			return 0;
		return range_width (&r);
}


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
regression_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			       RegressionToolState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_regression_t  *data;

	GtkWidget *w;
	gnm_float confidence;
	gint y_h;

	if (state->base.warning_dialog != NULL)
		gtk_widget_destroy (state->base.warning_dialog);

	data = g_new0 (analysis_tools_data_regression_t, 1);
	dao  = parse_output ((GnmGenericToolState *)state, NULL);

	data->base.wbc = GNM_WBC (state->base.wbcg);

	data->base.range_1 = gnm_expr_entry_parse_as_value (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->base.range_2 = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry_2), state->base.sheet);

	y_h = regression_tool_calc_height(data->base.range_2);

	data->group_by = (y_h == 1) ? GROUPED_BY_ROW : GROUPED_BY_COL;

	w = go_gtk_builder_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	entry_to_float (GTK_ENTRY (state->confidence_entry), &confidence, TRUE);
	data->base.alpha = 1 - confidence;

	w = go_gtk_builder_get_widget (state->base.gui, "intercept-button");
	data->intercept = !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	data->residual = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->residuals_check));

	data->multiple_regression
		= !gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->simple_linear_regression_radio));

	data->multiple_y = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->switch_variables_check));

	if (cmd_analysis_tool (GNM_WBC (state->base.wbcg), state->base.sheet,
			       dao, data, analysis_tool_regression_engine, FALSE)) {
		char *text;

		text = g_strdup_printf (
			_("An unexpected error has occurred: %d."), data->base.err);
		error_in_entry ((GnmGenericToolState *) state,
				GTK_WIDGET (state->base.input_entry), text);
		g_free (text);

		value_release (data->base.range_1);
		value_release (data->base.range_2);
		g_free (dao);
		g_free (data);
	} else
		gtk_widget_destroy (state->base.dialog);

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
regression_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				       RegressionToolState *state)
{
	int err;
	gnm_float confidence;
        GnmValue *input_range;
        GnmValue *input_range_2;
	gint y_h, y_w;
	gint x_h, x_w;
	gboolean switch_v;

	switch_v = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->switch_variables_check));

	/* Checking Input Range */
        input_range_2 = gnm_expr_entry_parse_as_value (
		GNM_EXPR_ENTRY (state->base.input_entry_2), state->base.sheet);
	if (input_range_2 == NULL) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    switch_v ?
				    _("The x variable range is invalid.") :
				    _("The y variable range is invalid.") );
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	y_h = regression_tool_calc_height(input_range_2);
	y_w = regression_tool_calc_width (input_range_2);
	value_release (input_range_2);

	if (y_h == 0 || y_w == 0) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    switch_v ?
				    _("The x variable range is invalid.") :
				    _("The y variable range is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}
	if (y_h != 1 && y_w != 1) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    switch_v ?
				    _("The x variable range must be a vector (n by 1 or 1 by n).") :
				    _("The y variable range must be a vector (n by 1 or 1 by n)."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}
	if (y_h <= 2 && y_w <= 2) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    switch_v ?
				    _("The x variable range is too small") :
				    _("The y variable range is too small"));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	input_range = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	if (input_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    switch_v ?
				    _("The y variables range is invalid.") :
				    _("The x variables range is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	x_h = regression_tool_calc_height(input_range);
	x_w = regression_tool_calc_width (input_range);
	value_release (input_range);

	if (x_h == 0 || x_w == 0) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    switch_v ?
				    _("The y variables range is invalid.") :
				    _("The x variables range is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	if ((y_h == 1 && y_w != x_w) || (y_w == 1 && y_h != x_h)) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    switch_v ?
				    _("The sizes of the y variable and x variables ranges do not match.") :
				    _("The sizes of the x variable and y variables ranges do not match."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	err = entry_to_float (GTK_ENTRY (state->confidence_entry), &confidence, FALSE);

	if (err != 0 || (1 < confidence || confidence < 0)) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The confidence level is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

        if (!gnm_dao_is_ready (GNM_DAO (state->base.gdao))) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The output specification "
				      "is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	gtk_label_set_text (GTK_LABEL (state->base.warning), "");
	gtk_widget_set_sensitive (state->base.ok_button, TRUE);
}

static void
regression_tool_regression_radio_toggled_cb (G_GNUC_UNUSED
					     GtkToggleButton *togglebutton,
					     RegressionToolState *state)
{
	gboolean simple = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->simple_linear_regression_radio));
	if (!simple)
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (state->switch_variables_check),
			 FALSE);

	gtk_toggle_button_set_active
		(GTK_TOGGLE_BUTTON (state->residuals_check), !simple);
	gtk_widget_set_sensitive (state->residuals_check, !simple);

}

static void
regression_tool_regression_check_toggled_cb (G_GNUC_UNUSED
					     GtkToggleButton *togglebutton,
					     RegressionToolState *state)
{
	GtkWidget *w1, *w2;

	w1 = go_gtk_builder_get_widget (state->base.gui, "var1-label");
	w2 = go_gtk_builder_get_widget (state->base.gui, "var2-label");

	if (gtk_toggle_button_get_active
	    (GTK_TOGGLE_BUTTON (state->switch_variables_check))) {
 		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON
			 (state->simple_linear_regression_radio),
			 TRUE);
		gtk_label_set_markup_with_mnemonic  (GTK_LABEL (w1),
						     _("_Y variables:"));
		gtk_label_set_markup_with_mnemonic  (GTK_LABEL (w2),
						     _("_X variable:"));
	} else {
		gtk_label_set_markup_with_mnemonic  (GTK_LABEL (w1),
						     _("_X variables:"));
		gtk_label_set_markup_with_mnemonic  (GTK_LABEL (w2),
						     _("_Y variable:"));
	}
	regression_tool_update_sensitivity_cb (NULL, state);
}


/**
 * dialog_regression_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
int
dialog_regression_tool (WBCGtk *wbcg, Sheet *sheet)
{
        RegressionToolState *state;
	char const * plugins[] = { "Gnumeric_fnstat",
				   "Gnumeric_fnlookup",
				   "Gnumeric_fnmath",
				   "Gnumeric_fninfo",
				   "Gnumeric_fnstring",
				   NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, REGRESSION_KEY))
		return 0;

	state = g_new0 (RegressionToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_REGRESSION,
			      "res:ui/regression.ui", "Regression",
			      _("Could not create the Regression Tool dialog."),
			      REGRESSION_KEY,
			      G_CALLBACK (regression_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (regression_tool_update_sensitivity_cb),
			      GNM_EE_SINGLE_RANGE))
	{
		g_free(state);
		return 0;
	}

	state->confidence_entry = go_gtk_builder_get_widget (state->base.gui, "confidence-entry");
	float_to_entry (GTK_ENTRY (state->confidence_entry), 0.95);
	g_signal_connect_after (G_OBJECT (state->confidence_entry),
		"changed",
		G_CALLBACK (regression_tool_update_sensitivity_cb), state);
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->confidence_entry));

	state->simple_linear_regression_radio
		= go_gtk_builder_get_widget
		(state->base.gui, "simple-regression-button");
	state->switch_variables_check
		= go_gtk_builder_get_widget
		(state->base.gui, "multiple-independent-check");
	state->residuals_check
		= go_gtk_builder_get_widget
		(state->base.gui, "residuals-button");
	gtk_toggle_button_set_active
		(GTK_TOGGLE_BUTTON (state->simple_linear_regression_radio),
		 FALSE);
	gtk_toggle_button_set_active
		(GTK_TOGGLE_BUTTON (state->switch_variables_check),
		 FALSE);
	gtk_toggle_button_set_active
		(GTK_TOGGLE_BUTTON (state->residuals_check),
		 TRUE);
	g_signal_connect
		(G_OBJECT (state->simple_linear_regression_radio),
		 "toggled",
		 G_CALLBACK (regression_tool_regression_radio_toggled_cb),
		 state);
	g_signal_connect
		(G_OBJECT (state->switch_variables_check),
		 "toggled",
		 G_CALLBACK (regression_tool_regression_check_toggled_cb),
		 state);



	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	regression_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

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
exp_smoothing_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
				  ExpSmoothToolState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_exponential_smoothing_t  *data;

	GtkWidget               *w;

	data = g_new0 (analysis_tools_data_exponential_smoothing_t, 1);
	dao  = parse_output ((GnmGenericToolState *)state, NULL);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->base.group_by = gnm_gui_group_value (state->base.gui, grouped_by_group);

	w = go_gtk_builder_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	entry_to_float (GTK_ENTRY (state->damping_fact_entry),
			&data->damp_fact, TRUE);
	entry_to_float (GTK_ENTRY (state->g_damping_fact_entry),
			&data->g_damp_fact, TRUE);
	entry_to_float (GTK_ENTRY (state->s_damping_fact_entry),
			&data->s_damp_fact, TRUE);
	entry_to_int (GTK_ENTRY (state->s_period_entry),
		      &data->s_period, TRUE);

	data->std_error_flag = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->show_std_errors));
	data->show_graph = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->graph_button));
	data->df = gnm_gui_group_value (state->base.gui, n_group);

	data->es_type = gnm_gui_group_value (state->base.gui, exp_smoothing_group);

	if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg), state->base.sheet,
				dao, data, analysis_tool_exponential_smoothing_engine,
				TRUE))
		gtk_widget_destroy (state->base.dialog);

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
exp_smoothing_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
					  ExpSmoothToolState *state)
{
	int err, period;
	gnm_float damp_fact;
        GSList *input_range;

        input_range = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	if (input_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The input range is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	} else
		range_list_destroy (input_range);

	switch (gnm_gui_group_value (state->base.gui, exp_smoothing_group)) {
	case exp_smoothing_type_mtes:
	case exp_smoothing_type_ates:
		err = entry_to_float (GTK_ENTRY (state->s_damping_fact_entry),
				      &damp_fact, FALSE);
		if (err!= 0 || damp_fact < 0 || damp_fact > 1)  {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The given seasonal damping "
					      "factor is invalid."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			return;
		}
		err = entry_to_int (GTK_ENTRY (state->s_period_entry),
				      &period, FALSE);
		if (err!= 0 || period < 2)  {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The given seasonal period "
					      "is invalid."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			return;
		}
		/* no break */
	case exp_smoothing_type_des:
		err = entry_to_float (GTK_ENTRY (state->g_damping_fact_entry),
				      &damp_fact, FALSE);
		if (err!= 0 || damp_fact < 0 || damp_fact > 1)  {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The given growth "
					      "damping factor is invalid."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			return;
		}
		/* no break */
	case exp_smoothing_type_ses_r:
	case exp_smoothing_type_ses_h:
		err = entry_to_float (GTK_ENTRY (state->damping_fact_entry),
				      &damp_fact, FALSE);
		if (err!= 0 || damp_fact < 0 || damp_fact > 1)  {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The given damping factor is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
		}
		break;
	}

	if (!gnm_dao_is_ready (GNM_DAO (state->base.gdao))) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The output specification "
				      "is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	gtk_label_set_text (GTK_LABEL (state->base.warning), "");
	gtk_widget_set_sensitive (state->base.ok_button, TRUE);
}

static void
exp_smoothing_tool_check_error_cb (G_GNUC_UNUSED GtkToggleButton *togglebutton, gpointer user_data)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (user_data), TRUE);
}


static void
exp_smoothing_ses_h_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
	ExpSmoothToolState *state = (ExpSmoothToolState *)user_data;
	gboolean std_error;

	if (!gtk_toggle_button_get_active (togglebutton))
		return;

	gtk_widget_set_sensitive (state->g_damping_fact_entry, FALSE);
	gtk_widget_set_sensitive (state->s_damping_fact_entry, FALSE);
	gtk_widget_set_sensitive (state->s_period_entry, FALSE);

	std_error = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->show_std_errors));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->n_button), TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->show_std_errors), std_error);
}

static void
exp_smoothing_ses_r_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
	ExpSmoothToolState *state = (ExpSmoothToolState *)user_data;
	gboolean std_error;

	if (!gtk_toggle_button_get_active (togglebutton))
		return;

	gtk_widget_set_sensitive (state->g_damping_fact_entry, FALSE);
	gtk_widget_set_sensitive (state->s_damping_fact_entry, FALSE);
	gtk_widget_set_sensitive (state->s_period_entry, FALSE);

	std_error = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->show_std_errors));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->nm1_button), TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->show_std_errors), std_error);
}

static void
exp_smoothing_des_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
	ExpSmoothToolState *state = (ExpSmoothToolState *)user_data;
	gboolean std_error;

	if (!gtk_toggle_button_get_active (togglebutton))
		return;

	gtk_widget_set_sensitive (state->g_damping_fact_entry, TRUE);
	gtk_widget_set_sensitive (state->s_damping_fact_entry, FALSE);
	gtk_widget_set_sensitive (state->s_period_entry, FALSE);

	std_error = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->show_std_errors));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->nm2_button), TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->show_std_errors), std_error);
}

static void
exp_smoothing_tes_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
	ExpSmoothToolState *state = (ExpSmoothToolState *)user_data;
	gboolean std_error;

	if (!gtk_toggle_button_get_active (togglebutton))
		return;

	gtk_widget_set_sensitive (state->g_damping_fact_entry, TRUE);
	gtk_widget_set_sensitive (state->s_damping_fact_entry, TRUE);
	gtk_widget_set_sensitive (state->s_period_entry, TRUE);

	std_error = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->show_std_errors));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->nm3_button), TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->show_std_errors), std_error);
}

/**
 * dialog_exp_smoothing_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
int
dialog_exp_smoothing_tool (WBCGtk *wbcg, Sheet *sheet)
{
        ExpSmoothToolState *state;
	char const * plugins[] = { "Gnumeric_fnstat",
				   "Gnumeric_fnlookup",
				   "Gnumeric_fnmath",
				   "Gnumeric_fnlogical",
				   NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, EXP_SMOOTHING_KEY))
		return 0;

	state = g_new0 (ExpSmoothToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_EXP_SMOOTHING,
			      "res:ui/exp-smoothing.ui",
			      "ExpSmoothing",
			      _("Could not create the Exponential Smoothing "
				"Tool dialog."),
			      EXP_SMOOTHING_KEY,
			      G_CALLBACK (exp_smoothing_tool_ok_clicked_cb),
			      NULL,
			      G_CALLBACK (exp_smoothing_tool_update_sensitivity_cb),
			      0))
	{
		g_free(state);
		return 0;
	}

	state->damping_fact_entry = go_gtk_builder_get_widget (state->base.gui,
							  "damping-fact-spin");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->damping_fact_entry), 0.2);
	float_to_entry (GTK_ENTRY (state->damping_fact_entry), 0.2);
	state->g_damping_fact_entry = go_gtk_builder_get_widget (state->base.gui,
							  "g-damping-fact-spin");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->g_damping_fact_entry), 0.25);
	state->s_damping_fact_entry = go_gtk_builder_get_widget (state->base.gui,
							  "s-damping-fact-spin");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->s_damping_fact_entry), 0.3);
	state->s_period_entry = go_gtk_builder_get_widget (state->base.gui,
							  "s-period-spin");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->s_period_entry), 12.);


	state->n_button = go_gtk_builder_get_widget (state->base.gui, "n-button");
	state->nm1_button = go_gtk_builder_get_widget (state->base.gui, "nm1-button");
	state->nm2_button = go_gtk_builder_get_widget (state->base.gui, "nm2-button");
	state->nm3_button = go_gtk_builder_get_widget (state->base.gui, "nm3-button");

	state->show_std_errors = go_gtk_builder_get_widget (state->base.gui, "std-errors-button");
	state->graph_button = go_gtk_builder_get_widget (state->base.gui, "graph-check");

	state->ses_h_button = go_gtk_builder_get_widget (state->base.gui, "ses-h-button");
	state->ses_r_button = go_gtk_builder_get_widget (state->base.gui, "ses-r-button");
	state->des_button = go_gtk_builder_get_widget (state->base.gui, "des-button");
	state->ates_button = go_gtk_builder_get_widget (state->base.gui, "ates-button");
	state->mtes_button = go_gtk_builder_get_widget (state->base.gui, "mtes-button");

	g_signal_connect_after (G_OBJECT (state->n_button),
		"toggled",
		G_CALLBACK (exp_smoothing_tool_check_error_cb), state->show_std_errors);
	g_signal_connect_after (G_OBJECT (state->nm1_button),
		"toggled",
		G_CALLBACK (exp_smoothing_tool_check_error_cb), state->show_std_errors);
	g_signal_connect_after (G_OBJECT (state->nm2_button),
		"toggled",
		G_CALLBACK (exp_smoothing_tool_check_error_cb), state->show_std_errors);
	g_signal_connect_after (G_OBJECT (state->nm3_button),
		"toggled",
		G_CALLBACK (exp_smoothing_tool_check_error_cb), state->show_std_errors);
	g_signal_connect_after (G_OBJECT (state->damping_fact_entry),
		"changed",
		G_CALLBACK (exp_smoothing_tool_update_sensitivity_cb), state);

	g_signal_connect_after (G_OBJECT (state->ses_h_button),
				"toggled",
				G_CALLBACK (exp_smoothing_ses_h_cb), state);
	g_signal_connect_after (G_OBJECT (state->ses_r_button),
				"toggled",
				G_CALLBACK (exp_smoothing_ses_r_cb), state);
	g_signal_connect_after (G_OBJECT (state->des_button),
				"toggled",
				G_CALLBACK (exp_smoothing_des_cb), state);
	g_signal_connect_after (G_OBJECT (state->ates_button),
				"toggled",
				G_CALLBACK (exp_smoothing_tes_cb), state);
	g_signal_connect_after (G_OBJECT (state->mtes_button),
				"toggled",
				G_CALLBACK (exp_smoothing_tes_cb), state);

	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->damping_fact_entry));
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->g_damping_fact_entry));
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->s_damping_fact_entry));

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->ses_h_button), TRUE);
	exp_smoothing_ses_h_cb (GTK_TOGGLE_BUTTON (state->ses_h_button), state);
	exp_smoothing_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

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
average_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			    AverageToolState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_moving_average_t  *data;

	GtkWidget               *w;

	data = g_new0 (analysis_tools_data_moving_average_t, 1);
	dao  = parse_output ((GnmGenericToolState *)state, NULL);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->base.group_by = gnm_gui_group_value (state->base.gui, grouped_by_group);

	w = go_gtk_builder_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	entry_to_int (GTK_ENTRY (state->interval_entry), &data->interval, TRUE);
	entry_to_int (GTK_ENTRY (state->offset_spin), &data->offset, TRUE);

	data->std_error_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->show_std_errors));
	data->show_graph = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->graph_button));

	data->df = gnm_gui_group_value (state->base.gui, n_group);

	data->ma_type = gnm_gui_group_value (state->base.gui, moving_average_group);

	switch (data->ma_type) {
	case moving_average_type_sma:
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->central_button))
		    && (data->interval % 2 == 0))
			data->ma_type = moving_average_type_central_sma;
		break;
	case moving_average_type_cma:
		data->interval = 0;
		data->offset = 0;
		break;
	case moving_average_type_spencer_ma:
		data->interval = 15;
		data->offset = 7;
		break;
	case moving_average_type_wma:
		data->offset = 0;
		break;
	default:
		break;
	}

	if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg), state->base.sheet,
				dao, data, analysis_tool_moving_average_engine, TRUE))
		gtk_widget_destroy (state->base.dialog);

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
average_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				    AverageToolState *state)
{
	int interval, err, offset;
        GSList *input_range;
	moving_average_type_t type;


        input_range = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	if (input_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The input range is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	} else
		range_list_destroy (input_range);

	type = gnm_gui_group_value (state->base.gui, moving_average_group);

	if ((type == moving_average_type_sma) || (type == moving_average_type_wma)) {
		err = entry_to_int (GTK_ENTRY (state->interval_entry),
				    &interval, FALSE);
		if (err!= 0 || interval <= 0)  {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The given interval is invalid."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			return;
		}
	}

	if (type == moving_average_type_sma) {
		err = entry_to_int (GTK_ENTRY (state->offset_spin), &offset, FALSE);
		if (err!= 0 || offset < 0 || offset > interval)  {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The given offset is invalid."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			return;
		}
	}

	if (!gnm_dao_is_ready (GNM_DAO (state->base.gdao))) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The output specification "
				      "is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	gtk_label_set_text (GTK_LABEL (state->base.warning), "");
	gtk_widget_set_sensitive (state->base.ok_button, TRUE);
}

static void
average_tool_check_error_cb (G_GNUC_UNUSED GtkToggleButton *togglebutton, gpointer user_data)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (user_data), TRUE);
}

static void
average_tool_central_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
	AverageToolState *state = (AverageToolState *)user_data;
	int interval;
	int err;

	if (gtk_toggle_button_get_active (togglebutton)) {
		err = entry_to_int (GTK_ENTRY (state->interval_entry), &interval, TRUE);
		if (err == 0)
			gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->offset_spin), (interval/2));
	}
}

static void
average_tool_prior_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
	AverageToolState *state = (AverageToolState *)user_data;

	if (gtk_toggle_button_get_active (togglebutton))
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->offset_spin), 0.0);
}

static void
average_tool_interval_cb (G_GNUC_UNUSED GtkWidget *dummy, AverageToolState *state)
{
	int interval;
	int err;

	err = entry_to_int (GTK_ENTRY (state->interval_entry), &interval, TRUE);

	if (err == 0)
		gtk_spin_button_set_range (GTK_SPIN_BUTTON (state->offset_spin),
					   0, interval - 1);
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->central_button)))
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->offset_spin), interval/2);
}

static void
average_tool_offset_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
	AverageToolState *state = (AverageToolState *)user_data;

	gtk_widget_set_sensitive (state->offset_spin, gtk_toggle_button_get_active (togglebutton));
}


static void
average_tool_sma_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
	AverageToolState *state = (AverageToolState *)user_data;

	if (!gtk_toggle_button_get_active (togglebutton))
		return;

	gtk_widget_set_sensitive (state->prior_button, TRUE);
	gtk_widget_set_sensitive (state->central_button, TRUE);
	gtk_widget_set_sensitive (state->offset_button, TRUE);
	gtk_widget_set_sensitive (state->interval_entry, TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->prior_button), TRUE);
	average_tool_update_sensitivity_cb (NULL, state);
}

static void
average_tool_cma_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
	AverageToolState *state = (AverageToolState *)user_data;

	if (!gtk_toggle_button_get_active (togglebutton))
		return;

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->prior_button), TRUE);
	gtk_widget_set_sensitive (state->prior_button, FALSE);
	gtk_widget_set_sensitive (state->central_button, FALSE);
	gtk_widget_set_sensitive (state->offset_button, FALSE);
	gtk_widget_set_sensitive (state->interval_entry, FALSE);
	average_tool_update_sensitivity_cb (NULL, state);
}

static void
average_tool_wma_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
	AverageToolState *state = (AverageToolState *)user_data;

	if (!gtk_toggle_button_get_active (togglebutton))
		return;
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->prior_button), TRUE);
	gtk_widget_set_sensitive (state->prior_button, FALSE);
	gtk_widget_set_sensitive (state->central_button, FALSE);
	gtk_widget_set_sensitive (state->offset_button, FALSE);
	gtk_widget_set_sensitive (state->interval_entry, TRUE);
	average_tool_update_sensitivity_cb (NULL, state);
}

static void
average_tool_spencer_cb (GtkToggleButton *togglebutton, gpointer user_data)
{
	AverageToolState *state = (AverageToolState *)user_data;

	if (!gtk_toggle_button_get_active (togglebutton))
		return;
	int_to_entry (GTK_ENTRY (state->interval_entry), 15);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->central_button), TRUE);
	gtk_widget_set_sensitive (state->prior_button, FALSE);
	gtk_widget_set_sensitive (state->central_button, FALSE);
	gtk_widget_set_sensitive (state->offset_button, FALSE);
	gtk_widget_set_sensitive (state->interval_entry, FALSE);
	average_tool_update_sensitivity_cb (NULL, state);
}

/**
 * dialog_average_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
int
dialog_average_tool (WBCGtk *wbcg, Sheet *sheet)
{
        AverageToolState *state;
	char const * plugins[] = { "Gnumeric_fnstat",
				   "Gnumeric_fnlookup",
				   "Gnumeric_fnmath",
				   NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, AVERAGE_KEY))
		return 0;

	state = g_new0 (AverageToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_MOVING_AVERAGES,
			      "res:ui/moving-averages.ui",
			      "MovAverages",
			      _("Could not create the Moving Average Tool "
				"dialog."),
			      AVERAGE_KEY,
			      G_CALLBACK (average_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (average_tool_update_sensitivity_cb),
			      0))
	{
		g_free(state);
		return 0;
	}

	state->interval_entry = go_gtk_builder_get_widget (state->base.gui, "interval-entry");
	int_to_entry (GTK_ENTRY (state->interval_entry), 3);
	state->n_button = go_gtk_builder_get_widget (state->base.gui, "n-button");
	state->nm1_button = go_gtk_builder_get_widget (state->base.gui, "nm1-button");
	state->nm2_button = go_gtk_builder_get_widget (state->base.gui, "nm2-button");
	state->prior_button = go_gtk_builder_get_widget (state->base.gui, "prior-button");
	state->central_button = go_gtk_builder_get_widget (state->base.gui, "central-button");
	state->offset_button = go_gtk_builder_get_widget (state->base.gui, "offset-button");
	state->offset_spin = go_gtk_builder_get_widget (state->base.gui, "offset-spinbutton");
	state->show_std_errors = go_gtk_builder_get_widget (state->base.gui, "std-errors-button");
	state->graph_button = go_gtk_builder_get_widget (state->base.gui, "graph-check");
	state->sma_button = go_gtk_builder_get_widget (state->base.gui, "sma-button");
	state->cma_button = go_gtk_builder_get_widget (state->base.gui, "cma-button");
	state->wma_button = go_gtk_builder_get_widget (state->base.gui, "wma-button");
	state->spencer_button = go_gtk_builder_get_widget (state->base.gui, "spencer-ma-button");


	g_signal_connect_after (G_OBJECT (state->n_button),
		"toggled",
		G_CALLBACK (average_tool_check_error_cb), state->show_std_errors);
	g_signal_connect_after (G_OBJECT (state->nm1_button),
		"toggled",
		G_CALLBACK (average_tool_check_error_cb), state->show_std_errors);
	g_signal_connect_after (G_OBJECT (state->nm2_button),
		"toggled",
		G_CALLBACK (average_tool_check_error_cb), state->show_std_errors);

	g_signal_connect_after (G_OBJECT (state->prior_button),
		"toggled",
		G_CALLBACK (average_tool_prior_cb), state);
	g_signal_connect_after (G_OBJECT (state->central_button),
		"toggled",
		G_CALLBACK (average_tool_central_cb), state);
	g_signal_connect_after (G_OBJECT (state->offset_button),
		"toggled",
		G_CALLBACK (average_tool_offset_cb), state);

	g_signal_connect_after (G_OBJECT (state->sma_button),
		"toggled",
		G_CALLBACK (average_tool_sma_cb), state);
	g_signal_connect_after (G_OBJECT (state->cma_button),
		"toggled",
		G_CALLBACK (average_tool_cma_cb), state);
	g_signal_connect_after (G_OBJECT (state->wma_button),
		"toggled",
		G_CALLBACK (average_tool_wma_cb), state);
	g_signal_connect_after (G_OBJECT (state->spencer_button),
		"toggled",
		G_CALLBACK (average_tool_spencer_cb), state);


	g_signal_connect_after (G_OBJECT (state->interval_entry),
		"changed",
		G_CALLBACK (average_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->interval_entry),
		"changed",
		G_CALLBACK (average_tool_interval_cb), state);

	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->interval_entry));

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	average_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

        return 0;
}

/**********************************************/
/*  End of Moving Averages tool code */
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
histogram_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				      HistogramToolState *state)
{
	int the_n;
	gboolean predetermined_bins;
        GSList *input_range;
        GnmValue *input_range_2 = NULL;

	input_range = gnm_expr_entry_parse_as_list
		(GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	if (input_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The input range is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	range_list_destroy (input_range);

	predetermined_bins = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->predetermined_button));
	if (predetermined_bins) {
		input_range_2 =  gnm_expr_entry_parse_as_value
			(GNM_EXPR_ENTRY (state->base.input_entry_2), state->base.sheet);
		if (input_range_2 == NULL) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The cutoff range is not valid."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			return;
		}
		value_release (input_range_2);
	} else if (entry_to_int(state->n_entry, &the_n,FALSE) != 0 || the_n <= 0) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The number of to be calculated cutoffs is invalid."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			return;
	}

        if (!gnm_dao_is_ready (GNM_DAO (state->base.gdao))) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The output specification "
				      "is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	gtk_label_set_text (GTK_LABEL (state->base.warning), "");
	gtk_widget_set_sensitive (state->base.ok_button, TRUE);

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
histogram_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			      HistogramToolState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_histogram_t  *data;

	GtkWidget *w;

	data = g_new0 (analysis_tools_data_histogram_t, 1);
	dao  = parse_output ((GnmGenericToolState *)state, NULL);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->base.group_by = gnm_gui_group_value (state->base.gui, grouped_by_group);

	data->predetermined = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->predetermined_button));
	if (data->predetermined) {
		w = go_gtk_builder_get_widget (state->base.gui, "labels_2_button");
		data->bin = gnm_expr_entry_parse_as_value
			(GNM_EXPR_ENTRY (state->base.input_entry_2),
			 state->base.sheet);
	} else {
		entry_to_int(state->n_entry, &data->n,TRUE);
		data->max_given = (0 == entry_to_float (state->max_entry,
							    &data->max , TRUE));
		data->min_given = (0 == entry_to_float (state->min_entry,
							    &data->min , TRUE));
		data->bin = NULL;
	}

	data->bin_type = gnm_gui_group_value (state->base.gui, bin_type_group);
	data->chart = gnm_gui_group_value (state->base.gui, chart_group);

	w = go_gtk_builder_get_widget (state->base.gui, "labels_button");
	data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	w = go_gtk_builder_get_widget (state->base.gui, "percentage-button");
	data->percentage = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	w = go_gtk_builder_get_widget (state->base.gui, "cum-button");
	data->cumulative = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	w = go_gtk_builder_get_widget (state->base.gui, "only-num-button");
	data->only_numbers = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg), state->base.sheet,
				dao, data, analysis_tool_histogram_engine, TRUE))
		gtk_widget_destroy (state->base.dialog);

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
static gboolean
histogram_tool_set_predetermined (G_GNUC_UNUSED GtkWidget *widget,
				  G_GNUC_UNUSED GdkEventFocus *event,
				  HistogramToolState *state)
{
	    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->predetermined_button), TRUE);
	    return FALSE;
}

/**
 * histogram_tool_set_calculated:
 * @widget:
 * @event:
 * @state:
 *
 **/
static gboolean
histogram_tool_set_calculated (G_GNUC_UNUSED GtkWidget *widget,
			       G_GNUC_UNUSED GdkEventFocus *event,
			       HistogramToolState *state)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->calculated_button), TRUE);
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
int
dialog_histogram_tool (WBCGtk *wbcg, Sheet *sheet)
{
        HistogramToolState *state;
	char const * plugins[] = {"Gnumeric_fnlogical",
				  "Gnumeric_fnstat",
				  "Gnumeric_fnlookup",
				  NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, HISTOGRAM_KEY))
		return 0;

	state = g_new0 (HistogramToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_HISTOGRAM,
			      "res:ui/histogram.ui", "Histogram",
			      _("Could not create the Histogram Tool dialog."),
			      HISTOGRAM_KEY,
			      G_CALLBACK (histogram_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (histogram_tool_update_sensitivity_cb),
			      0))
	{
		g_free(state);
		return 0;
	}

	state->predetermined_button = GTK_WIDGET (go_gtk_builder_get_widget
						  (state->base.gui,
						   "pre_determined_button"));
	state->calculated_button = GTK_WIDGET (go_gtk_builder_get_widget
					       (state->base.gui,
						"calculated_button"));
	state->n_entry = GTK_ENTRY(go_gtk_builder_get_widget (state->base.gui,
							  "n_entry"));
	state->max_entry = GTK_ENTRY(go_gtk_builder_get_widget (state->base.gui,
							    "max_entry"));
	state->min_entry = GTK_ENTRY(go_gtk_builder_get_widget (state->base.gui,
							    "min_entry"));

	g_signal_connect_after (G_OBJECT (state->predetermined_button),
		"toggled",
		G_CALLBACK (histogram_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->calculated_button),
		"toggled",
		G_CALLBACK (histogram_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->n_entry),
		"changed",
		G_CALLBACK (histogram_tool_update_sensitivity_cb), state);
	g_signal_connect (G_OBJECT (state->n_entry),
		"key-press-event",
		G_CALLBACK (histogram_tool_set_calculated), state);
	g_signal_connect (G_OBJECT (state->min_entry),
		"key-press-event",
		G_CALLBACK (histogram_tool_set_calculated), state);
	g_signal_connect (G_OBJECT (state->max_entry),
		"key-press-event",
		G_CALLBACK (histogram_tool_set_calculated), state);
	g_signal_connect (G_OBJECT
			  (gnm_expr_entry_get_entry (
				  GNM_EXPR_ENTRY (state->base.input_entry_2))),
		"focus-in-event",
		G_CALLBACK (histogram_tool_set_predetermined), state);

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	histogram_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->calculated_button), TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->base.gui,"histogram-button")), TRUE);
	gtk_entry_set_text (GTK_ENTRY (state->n_entry), "12");

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
anova_single_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
				 AnovaSingleToolState *state)
{
	data_analysis_output_t  *dao;
	GtkWidget *w;
	analysis_tools_data_anova_single_t *data;

	data = g_new0 (analysis_tools_data_anova_single_t, 1);
	dao  = parse_output ((GnmGenericToolState *)state, NULL);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->base.group_by = gnm_gui_group_value (state->base.gui, grouped_by_group);

	w = go_gtk_builder_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	data->alpha = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->alpha_entry));

	if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg), state->base.sheet,
				dao, data, analysis_tool_anova_single_engine, TRUE))
		gtk_widget_destroy (state->base.dialog);

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
anova_single_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
					 AnovaSingleToolState *state)
{
	gnm_float alpha;
        GSList *input_range;

        input_range = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry),
		state->base.sheet);
	if (input_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The input range is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	} else
		range_list_destroy (input_range);

	/* Checking Alpha*/
	alpha = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->alpha_entry));
	if (!(alpha > 0 && alpha < 1)) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The alpha value should "
				      "be a number between 0 and 1."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	/* Checking Output Page */
	if (!gnm_dao_is_ready (GNM_DAO (state->base.gdao))) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The output specification "
				      "is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	gtk_label_set_text (GTK_LABEL (state->base.warning), "");
	gtk_widget_set_sensitive (state->base.ok_button, TRUE);

}


/**
 * dialog_anova_single_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
int
dialog_anova_single_factor_tool (WBCGtk *wbcg, Sheet *sheet)
{
        AnovaSingleToolState *state;
	char const * plugins[] = { "Gnumeric_fnstat",
				   NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, ANOVA_SINGLE_KEY))
		return 0;

	state = g_new0 (AnovaSingleToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_ANOVA_SINGLE_FACTOR,
			      "res:ui/anova-one.ui", "ANOVA",
			      _("Could not create the ANOVA (single factor) "
				"tool dialog."),
			      ANOVA_SINGLE_KEY,
			      G_CALLBACK (anova_single_tool_ok_clicked_cb),
			      NULL,
			      G_CALLBACK (anova_single_tool_update_sensitivity_cb),
			      0))
	{
		g_free(state);
		return 0;
	}

	state->alpha_entry = go_gtk_builder_get_widget (state->base.gui,
						   "alpha-entry");
	float_to_entry (GTK_ENTRY (state->alpha_entry), 0.05);
	g_signal_connect_after (G_OBJECT (state->alpha_entry),
		"changed",
		G_CALLBACK (anova_single_tool_update_sensitivity_cb), state);
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->alpha_entry));

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	anova_single_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

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
anova_two_factor_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
				     AnovaTwoFactorToolState *state)
{
	data_analysis_output_t  *dao;
	GtkWidget *w;
	analysis_tools_data_anova_two_factor_t *data;
	char *text;

	if (state->base.warning_dialog != NULL)
		gtk_widget_destroy (state->base.warning_dialog);

	data = g_new0 (analysis_tools_data_anova_two_factor_t, 1);
	dao  = parse_output ((GnmGenericToolState *)state, NULL);

	data->input = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry),
		 state->base.sheet);
	data->err = analysis_tools_noerr;
	data->wbc = GNM_WBC (state->base.wbcg);

	w = go_gtk_builder_get_widget (state->base.gui, "labels_button");
        data->labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	data->alpha = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->alpha_entry));
	entry_to_int (GTK_ENTRY (state->replication_entry),
			    &data->replication, TRUE);

	if (cmd_analysis_tool (GNM_WBC (state->base.wbcg),
			       state->base.sheet,
			       dao, data, analysis_tool_anova_two_factor_engine, FALSE)) {
		switch (data->err) {
		case analysis_tools_missing_data:
			error_in_entry ((GnmGenericToolState *) state,
					GTK_WIDGET (state->base.input_entry),
					data->labels ? _("The given input range should contain at "
					  "least two columns and two rows of data and the "
					  "labels.") :
					_("The given input range should contain at "
					  "least two columns and two rows of data."));
			break;
		case analysis_tools_too_few_cols:
			error_in_entry ((GnmGenericToolState *) state,
					GTK_WIDGET (state->base.input_entry),
					data->labels ? _("The given input range should contain at "
					  "least two columns of data and the "
					  "labels.") :
					_("The given input range should contain at "
					  "least two columns of data."));
			break;
		case analysis_tools_too_few_rows:
			error_in_entry ((GnmGenericToolState *) state,
					GTK_WIDGET (state->base.input_entry),
					data->labels ? _("The given input range should contain at "
					  "least two rows of data and the "
					  "labels.") :
					_("The given input range should "
					  "contain at least two rows of "
					  "data."));
			break;
		case analysis_tools_replication_invalid:
			error_in_entry ((GnmGenericToolState *) state,
					GTK_WIDGET (state->base.input_entry),
					_("The number of data rows must be a "
					  "multiple of the replication "
					  "number."));
			break;
		default:
			text = g_strdup_printf (
				_("An unexpected error has occurred: %d."),
				data->err);
			error_in_entry ((GnmGenericToolState *) state,
					GTK_WIDGET (state->base.input_entry),
					text);
			g_free (text);
			break;
		}
		value_release (data->input);
		g_free (dao);
		g_free (data);
	} else
		gtk_widget_destroy (state->base.dialog);

	return;
}

/**
 * anova_two_factor_tool_update_sensitivity_cb:
 * @state:
 *
 * Update the dialog widgets sensitivity.
 * We cannot use tool_update_sensitivity_cb
 * since we are also considering whether in fact
 * an alpha and a replication is given.
 **/
static void
anova_two_factor_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
					     AnovaTwoFactorToolState *state)
{
	int replication, err_replication;
	gnm_float alpha;
        GnmValue *input_range;

	/* Checking Input Range */
        input_range = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry),
		 state->base.sheet);
	if (input_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The input range is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	} else
		value_release (input_range);

	/* Checking Alpha*/
	alpha = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->alpha_entry));
	if (!(alpha > 0 && alpha < 1)) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The alpha value should "
				      "be a number between 0 and 1."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}


	/* Checking Replication*/
	err_replication = entry_to_int (GTK_ENTRY (state->replication_entry),
					&replication, FALSE);
	if (!(err_replication == 0 && replication > 0)) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The number of rows per sample "
				      "should be a positive integer."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	/* Checking Output Page */
	if (!gnm_dao_is_ready (GNM_DAO (state->base.gdao))) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The output specification "
				      "is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	gtk_label_set_text (GTK_LABEL (state->base.warning), "");
	gtk_widget_set_sensitive (state->base.ok_button, TRUE);

	return;
}

/**
 * dialog_anova_two_factor_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
int
dialog_anova_two_factor_tool (WBCGtk *wbcg, Sheet *sheet)
{
        AnovaTwoFactorToolState *state;
	char const * plugins[] = { "Gnumeric_fnstat",
				   "Gnumeric_fnlookup",
				   "Gnumeric_fnmath",
				   "Gnumeric_fninfo",
				   "Gnumeric_fnlogical",
				   NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, ANOVA_TWO_FACTOR_KEY))
		return 0;

	state = g_new0 (AnovaTwoFactorToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_ANOVA_TWO_FACTOR,
			      "res:ui/anova-two.ui", "ANOVA",
			      _("Could not create the ANOVA (two factor) "
				"tool dialog."),
			      ANOVA_TWO_FACTOR_KEY,
			      G_CALLBACK (anova_two_factor_tool_ok_clicked_cb),
			      NULL,
			      G_CALLBACK (anova_two_factor_tool_update_sensitivity_cb),
			      GNM_EE_SINGLE_RANGE))
	{
		g_free(state);
		return 0;
	}

	state->alpha_entry = go_gtk_builder_get_widget (state->base.gui,
						   "alpha-entry");
	float_to_entry (GTK_ENTRY(state->alpha_entry), 0.05);
	state->replication_entry = go_gtk_builder_get_widget (state->base.gui,
							 "replication-entry");
	int_to_entry (GTK_ENTRY(state->replication_entry), 1);

	g_signal_connect_after (G_OBJECT (state->alpha_entry),
		"changed",
		G_CALLBACK (anova_two_factor_tool_update_sensitivity_cb),
				state);
	g_signal_connect_after (G_OBJECT (state->replication_entry),
		"changed",
		G_CALLBACK (anova_two_factor_tool_update_sensitivity_cb),
				state);
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->alpha_entry));
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->replication_entry));

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	anova_two_factor_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, FALSE);

        return 0;
}

/**********************************************/
/*  End of ANOVA (Two Factor) tool code */
/**********************************************/
