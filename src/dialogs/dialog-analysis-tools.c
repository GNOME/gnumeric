/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "analysis-tools.h"
#include "analysis-histogram.h"

#include <workbook.h>
#include <workbook-control.h>
#include <wbc-gtk.h>
#include <workbook-view.h>
#include <gui-util.h>
#include <parse-util.h>
#include <gnm-format.h>
#include <tool-dialogs.h>
#include <dao-gui-utils.h>
#include <sheet.h>
#include <expr.h>
#include <number-match.h>
#include <ranges.h>
#include <selection.h>
#include <value.h>
#include <commands.h>
#include "help.h"

#include <widgets/gnm-dao.h>
#include <widgets/gnumeric-expr-entry.h>

#include <glade/glade.h>
#include <string.h>
#include <gtk/gtktable.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtklabel.h>

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
	GenericToolState base;
	GtkWidget *predetermined_button;
	GtkWidget *calculated_button;
	GtkWidget *bin_labels_button;
	GtkEntry  *n_entry;
	GtkEntry  *max_entry;
	GtkEntry  *min_entry;
} HistogramToolState;

typedef struct {
	GenericToolState base;
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
	GenericToolState base;
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
	GenericToolState base;
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
	GenericToolState base;
	GtkWidget *interval_entry;
} AverageToolState;

typedef struct {
	GenericToolState base;
        GtkWidget *damping_fact_entry;
} ExpSmoothToolState;

typedef struct {
	GenericToolState base;
	GtkWidget *confidence_entry;
} RegressionToolState;

typedef struct {
	GenericToolState base;
	GtkWidget *alpha_entry;
} AnovaSingleToolState;

typedef struct {
	GenericToolState base;
	GtkWidget *alpha_entry;
	GtkWidget *replication_entry;
} AnovaTwoFactorToolState;

typedef struct {
	GenericToolState base;
	GtkWidget *alpha_entry;
} FTestToolState;


/**********************************************/
/*  Generic functions for the analysis tools. */
/*  Functions in this section are being used  */
/*  by virtually all tools.                   */
/**********************************************/


/**
 * error_in_entry:
 *
 * @wbcg:
 * @entry:
 * @err_str:
 *
 * Show an error dialog and select corresponding entry
 */
void
error_in_entry (GenericToolState *state, GtkWidget *entry, char const *err_str)
{
        go_gtk_notice_nonmodal_dialog ((GtkWindow *) state->dialog,
				  &(state->warning_dialog),
				  GTK_MESSAGE_ERROR, err_str);

	if (IS_GNM_EXPR_ENTRY (entry))
		gnm_expr_entry_grab_focus (GNM_EXPR_ENTRY (entry), TRUE);
	else
		focus_on_entry (GTK_ENTRY (entry));
}

static void
cb_tool_destroy (GenericToolState  *state)
{
	if (state->gui != NULL)
		g_object_unref (G_OBJECT (state->gui));
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
			GenericToolState *state)
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
void
dialog_tool_init_buttons (GenericToolState *state,
			  GCallback ok_function,
			  GCallback close_function)
{
	state->ok_button = glade_xml_get_widget (state->gui, "okbutton");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (ok_function), state);

	state->cancel_button = glade_xml_get_widget (state->gui,
						     "cancelbutton");
	if (close_function == NULL)
		g_signal_connect (G_OBJECT (state->cancel_button),
				  "clicked",
				  G_CALLBACK (cb_tool_cancel_clicked), state);
	else
		g_signal_connect (G_OBJECT (state->cancel_button),
				  "clicked",
				  G_CALLBACK (close_function), state);

	state->apply_button = glade_xml_get_widget (state->gui, "applybutton");
	if (state->apply_button != NULL )
		g_signal_connect (G_OBJECT (state->apply_button),
				  "clicked",
				  G_CALLBACK (ok_function), state);
	state->help_button = glade_xml_get_widget (state->gui, "helpbutton");
	if (state->help_button != NULL )
		gnumeric_init_help_button (state->help_button,
					   state->help_link);
}

static gint
dialog_tool_cmp (GtkTableChild *tchild, GtkWidget *widget)
{
	return (tchild->widget != widget);
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
gboolean
dialog_tool_init (GenericToolState *state,
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
	GtkTable  *table;
	GtkWidget *widget;

	state->wbcg  = wbcg;
	state->wb    = wb_control_get_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->sv    = wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg));
	state->warning_dialog = NULL;
	state->help_link      = help_file;
	state->state_destroy = NULL;

	state->gui = gnm_glade_xml_new (GO_CMD_CONTEXT (state->wbcg),
		gui_name, NULL, NULL);
        if (state->gui == NULL)
		goto dialog_tool_init_error;

	state->dialog = glade_xml_get_widget (state->gui, dialog_name);
        if (state->dialog == NULL)
		goto dialog_tool_init_error;


	dialog_tool_init_buttons (state, ok_function, close_function);

	widget = glade_xml_get_widget (state->gui, "var1-label");
	if (widget == NULL) {
		state->input_entry = NULL;
	} else {
		table = GTK_TABLE (glade_xml_get_widget (state->gui,
							 "input-table"));
		state->input_entry = gnm_expr_entry_new (state->wbcg, TRUE);
		gnm_expr_entry_set_flags (state->input_entry, flags,
					  GNM_EE_MASK);
		gtk_table_attach (table, GTK_WIDGET (state->input_entry),
				  1, 2, 0, 1,
				  GTK_EXPAND | GTK_FILL, 0,
				  0, 0);
		g_signal_connect_after (G_OBJECT (state->input_entry),
					"changed",
					G_CALLBACK (sensitivity_cb), state);
		gnumeric_editable_enters (GTK_WINDOW (state->dialog),
					  GTK_WIDGET (state->input_entry));
		gtk_label_set_mnemonic_widget (GTK_LABEL (widget),
					       GTK_WIDGET (state->input_entry));
		go_atk_setup_label (widget, GTK_WIDGET (state->input_entry));
		gtk_widget_show (GTK_WIDGET (state->input_entry));
	}


/*                                                        */
/* If there is a var2-label, we need a second input field */
/*                                                        */
	widget = glade_xml_get_widget (state->gui, "var2-label");
	if (widget == NULL) {
		state->input_entry_2 = NULL;
	} else {
		GList *this_label_widget;
		GtkTableChild *tchild;

		state->input_entry_2 = gnm_expr_entry_new (state->wbcg, TRUE);
		gnm_expr_entry_set_flags (state->input_entry_2,
					  GNM_EE_SINGLE_RANGE, GNM_EE_MASK);
		table = GTK_TABLE (gtk_widget_get_parent (widget));

		this_label_widget = g_list_find_custom
		  (table->children, widget, (GCompareFunc) dialog_tool_cmp);
		tchild = (GtkTableChild *)(this_label_widget->data);

		gtk_table_attach (table, GTK_WIDGET (state->input_entry_2),
			  1, 2, tchild->top_attach, tchild->bottom_attach,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
		g_signal_connect_after (G_OBJECT (state->input_entry_2),
					"changed",
					G_CALLBACK (sensitivity_cb), state);
		gnumeric_editable_enters (GTK_WINDOW (state->dialog),
					  GTK_WIDGET (state->input_entry_2));
		gtk_label_set_mnemonic_widget (GTK_LABEL (widget),
					       GTK_WIDGET (state->input_entry_2));
		go_atk_setup_label (widget, GTK_WIDGET (state->input_entry_2));
		gtk_widget_show (GTK_WIDGET (state->input_entry_2));
	}

	state->warning = glade_xml_get_widget (state->gui, "warnings");
	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_tool_destroy);

	dialog_tool_init_outputs (state, sensitivity_cb);

	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (state->dialog), key);

	return FALSE;

 dialog_tool_init_error:
	go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR, error_str);
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
tool_load_selection (GenericToolState *state, gboolean allow_multiple)
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
				   FALSE);
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
			    GenericToolState *state)
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
			 GenericToolState *state)
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
	data->group_by = gnumeric_glade_group_value (state->gui, grouped_by_group);

	w = glade_xml_get_widget (state->gui, "labels_button");
        data->labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	if (cmd_analysis_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
			       dao, data, analysis_tool_correlation_engine)) {

		switch (data->err - 1) {
		case GROUPED_BY_ROW:
			error_in_entry ((GenericToolState *) state,
					GTK_WIDGET (state->input_entry),
					_("The selected input rows must have equal size!"));
			break;
		case GROUPED_BY_COL:
			error_in_entry ((GenericToolState *) state,
					GTK_WIDGET (state->input_entry),
					_("The selected input columns must have equal size!"));
			break;
		case GROUPED_BY_AREA:
			error_in_entry ((GenericToolState *) state,
					GTK_WIDGET (state->input_entry),
					_("The selected input areas must have equal size!"));
			break;
		default:
			text = g_strdup_printf (
				_("An unexpected error has occurred: %d."), data->err);
			error_in_entry ((GenericToolState *) state,
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
        GenericToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, CORRELATION_KEY))
		return 0;

	state = g_new0 (GenericToolState, 1);

	if (dialog_tool_init (state, wbcg, sheet,
			      GNUMERIC_HELP_LINK_CORRELATION,
			      "correlation.glade", "Correlation",
			      _("Could not create the Correlation Tool dialog."),
			      CORRELATION_KEY,
			      G_CALLBACK (corr_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (tool_update_sensitivity_cb),
			      0))
		return 0;

	gnm_dao_set_put (GNM_DAO (state->gdao), TRUE, TRUE);
	tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);

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
			GenericToolState *state)
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
	data->group_by = gnumeric_glade_group_value (state->gui, grouped_by_group);

	w = glade_xml_get_widget (state->gui, "labels_button");
        data->labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	if (cmd_analysis_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
			       dao, data, analysis_tool_covariance_engine)) {

		switch (data->err - 1) {
		case GROUPED_BY_ROW:
			error_in_entry ((GenericToolState *) state,
					GTK_WIDGET (state->input_entry),
					_("The selected input rows must have equal size!"));
			break;
		case GROUPED_BY_COL:
			error_in_entry ((GenericToolState *) state,
					GTK_WIDGET (state->input_entry),
					_("The selected input columns must have equal size!"));
			break;
		case GROUPED_BY_AREA:
			error_in_entry ((GenericToolState *) state,
					GTK_WIDGET (state->input_entry),
					_("The selected input areas must have equal size!"));
			break;
		default:
			text = g_strdup_printf (
				_("An unexpected error has occurred: %d."), data->err);
			error_in_entry ((GenericToolState *) state,
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
        GenericToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, COVARIANCE_KEY))
		return 0;

	state = g_new0 (GenericToolState, 1);

	if (dialog_tool_init (state, wbcg, sheet,
			      GNUMERIC_HELP_LINK_COVARIANCE,
			      "covariance.glade", "Covariance",
			      _("Could not create the Covariance Tool dialog."),
			      COVARIANCE_KEY,
			      G_CALLBACK (cov_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (tool_update_sensitivity_cb),
			      0))
		return 0;

	gnm_dao_set_put (GNM_DAO (state->gdao), TRUE, TRUE);
	tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);

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
			 GenericToolState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_ranking_t  *data;

	GtkWidget *w;

	data = g_new0 (analysis_tools_data_ranking_t, 1);
	dao  = parse_output (state, NULL);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->input_entry), state->sheet);
	data->base.group_by = gnumeric_glade_group_value (state->gui, grouped_by_group);

	w = glade_xml_get_widget (state->gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	w = glade_xml_get_widget (state->gui, "rank_button");
        data->av_ties = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));


	if (!cmd_analysis_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
			       dao, data, analysis_tool_ranking_engine))
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
        GenericToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, RANK_PERCENTILE_KEY))
		return 0;

	state = g_new0 (GenericToolState, 1);

	if (dialog_tool_init (state, wbcg, sheet,
			      GNUMERIC_HELP_LINK_RANKING,
			      "rank.glade", "RankPercentile",
			      _("Could not create the Rank and Percentile "
				"Tools dialog."),
			      RANK_PERCENTILE_KEY,
			      G_CALLBACK (rank_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (tool_update_sensitivity_cb),
			      0))
		return 0;

	gnm_dao_set_put (GNM_DAO (state->gdao), FALSE, FALSE);
	tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);

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
			    GenericToolState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_fourier_t  *data;

	GtkWidget               *w;

	data = g_new0 (analysis_tools_data_fourier_t, 1);
	dao  = parse_output (state, NULL);

	data->base.wbc = WORKBOOK_CONTROL (state->wbcg);
	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->input_entry), state->sheet);
	data->base.group_by = gnumeric_glade_group_value (state->gui, grouped_by_group);

	w = glade_xml_get_widget (state->gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	w = glade_xml_get_widget (state->gui, "inverse_button");
	data->inverse = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)) != 0;

	if (!cmd_analysis_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
			       dao, data, analysis_tool_fourier_engine))
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
        GenericToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, FOURIER_KEY))
		return 0;

	state = g_new0 (GenericToolState, 1);

	if (dialog_tool_init (state, wbcg, sheet,
			      GNUMERIC_HELP_LINK_FOURIER_ANALYSIS,
			      "fourier-analysis.glade", "FourierAnalysis",
			      _("Could not create the Fourier Analysis Tool "
				"dialog."),
			      FOURIER_KEY,
			      G_CALLBACK (fourier_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (tool_update_sensitivity_cb),
			      0))
		return 0;

	gnm_dao_set_put (GNM_DAO (state->gdao), FALSE, FALSE);
	tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);

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
	gint err;

	data = g_new0 (analysis_tools_data_descriptive_t, 1);
	dao  = parse_output ((GenericToolState *)state, NULL);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->base.group_by = gnumeric_glade_group_value (state->base.gui, grouped_by_group);

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
		err = entry_to_int (GTK_ENTRY (state->l_entry), &data->k_largest, TRUE);
	if (data->kth_smallest == 1)
		err = entry_to_int (GTK_ENTRY (state->s_entry), &data->k_smallest, TRUE);

	w = glade_xml_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	if (!cmd_analysis_tool (WORKBOOK_CONTROL (state->base.wbcg), state->base.sheet,
			       dao, data, analysis_tool_descriptive_engine))
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

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, DESCRIPTIVE_STATS_KEY))
		return 0;

	state = g_new0 (DescriptiveStatState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_DESCRIPTIVE_STATS,
			      "descriptive-stats.glade", "DescStats",
			      _("Could not create the Descriptive Statistics "
				"Tool dialog."),
			      DESCRIPTIVE_STATS_KEY,
			      G_CALLBACK (cb_desc_stat_tool_ok_clicked), NULL,
			      G_CALLBACK (desc_stat_tool_update_sensitivity_cb),
			      0))
		return 0;

	state->summary_stats_button  = glade_xml_get_widget
		(state->base.gui, "summary_stats_button");
	state->ss_button  = glade_xml_get_widget
		(state->base.gui, "ss_button");
	state->mean_stats_button  = glade_xml_get_widget
		(state->base.gui, "mean_stats_button");
	state->kth_largest_button  = glade_xml_get_widget
		(state->base.gui, "kth_largest_button");
	state->kth_smallest_button  = glade_xml_get_widget
		(state->base.gui, "kth_smallest_button");
	state->c_entry  = glade_xml_get_widget (state->base.gui, "c_entry");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->c_entry), 0.95);
	state->l_entry  = glade_xml_get_widget (state->base.gui, "l_entry");
	int_to_entry (GTK_ENTRY (state->l_entry), 1);
	state->s_entry  = glade_xml_get_widget (state->base.gui, "s_entry");
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
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->c_entry));
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->l_entry));
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->s_entry));

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	desc_stat_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);

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
	dao  = parse_output ((GenericToolState *)state, NULL);

	data->base.wbc = WORKBOOK_CONTROL (state->base.wbcg);

	if (state->base.warning_dialog != NULL)
		gtk_widget_destroy (state->base.warning_dialog);

	data->base.range_1 = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);

	data->base.range_2 = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry_2), state->base.sheet);

	w = glade_xml_get_widget (state->base.gui, "labels_button");
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
		if (!cmd_analysis_tool (WORKBOOK_CONTROL (state->base.wbcg), state->base.sheet,
					dao, data, analysis_tool_ttest_paired_engine))
			gtk_widget_destroy (state->base.dialog);
		break;
	case TTEST_UNPAIRED_EQUALVARIANCES:
		if (!cmd_analysis_tool (WORKBOOK_CONTROL (state->base.wbcg), state->base.sheet,
					dao, data, analysis_tool_ttest_eqvar_engine))
			gtk_widget_destroy (state->base.dialog);
		break;
	case TTEST_UNPAIRED_UNEQUALVARIANCES:
		if (!cmd_analysis_tool (WORKBOOK_CONTROL (state->base.wbcg), state->base.sheet,
					dao, data, analysis_tool_ttest_neqvar_engine))
			gtk_widget_destroy (state->base.dialog);
		break;
	case TTEST_ZTEST:
		err = entry_to_float (GTK_ENTRY (state->var1_variance), &data->var1, TRUE);
		if (err != 0 || data->var1 <= 0.0) {
			error_in_entry ((GenericToolState *) state, GTK_WIDGET (state->var1_variance),
					_("Please enter a valid\n"
					  "population variance for variable 1."));
			g_free (data);
			g_free (dao);
			return;
		}
		err = entry_to_float (GTK_ENTRY (state->var2_variance), &data->var2, TRUE);
		if (err != 0 || data->var2 <= 0.0) {
			error_in_entry ((GenericToolState *) state, GTK_WIDGET (state->var2_variance),
					_("Please enter a valid\n"
					  "population variance for variable 2."));
			g_free (data);
			g_free (dao);
			return;
		}

		if (!cmd_analysis_tool (WORKBOOK_CONTROL (state->base.wbcg), state->base.sheet,
					dao, data, analysis_tool_ztest_engine))
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
	alpha_ready = (err == 0 && alpha > 0.0 && alpha < 1.0);
	input_1_ready = (input_range != NULL);
	input_2_ready = ((state->base.input_entry_2 == NULL) || (input_range_2 != NULL));
	output_ready =  gnm_dao_is_ready (GNM_DAO (state->base.gdao));

        if (input_range != NULL) value_release (input_range);
        if (input_range_2 != NULL) value_release (input_range_2);

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
	gtk_widget_set_size_request (state->options_table,
				     state->options_table->allocation.width,
				     state->options_table->allocation.height);
	gtk_widget_set_size_request (state->paired_button,
				     state->paired_button->allocation.width,
				     state->paired_button->allocation.height);
	gtk_widget_set_size_request (state->unpaired_button,
				     state->unpaired_button->allocation.width,
				     state->unpaired_button->allocation.height);
	gtk_widget_set_size_request (state->variablespaired_label,
				     state->variablespaired_label->allocation.width,
				     state->variablespaired_label->allocation.height);
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

	if (wbcg == NULL) {
		return 1;
	}

	/* Only pop up one copy per workbook */
	dialog = gnumeric_dialog_raise_if_exists (wbcg, TTEST_KEY);
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
			      "mean-tests.glade", "MeanTests",
			      _("Could not create the Mean Tests Tool dialog."),
			      TTEST_KEY,
			      G_CALLBACK (ttest_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (ttest_update_sensitivity_cb),
			      GNM_EE_SINGLE_RANGE))
		return 0;

	state->paired_button  = glade_xml_get_widget (state->base.gui, "paired-button");
	state->unpaired_button  = glade_xml_get_widget (state->base.gui, "unpaired-button");
	state->variablespaired_label = glade_xml_get_widget (state->base.gui, "variablespaired-label");
	state->known_button  = glade_xml_get_widget (state->base.gui, "known-button");
	state->unknown_button  = glade_xml_get_widget (state->base.gui, "unknown-button");
	state->varianceknown_label = glade_xml_get_widget (state->base.gui, "varianceknown-label");
	state->equal_button  = glade_xml_get_widget (state->base.gui, "equal-button");
	state->unequal_button  = glade_xml_get_widget (state->base.gui, "unequal-button");
	state->varianceequal_label = glade_xml_get_widget (state->base.gui, "varianceequal-label");
	state->options_table = glade_xml_get_widget (state->base.gui, "options-table");
	state->var1_variance_label = glade_xml_get_widget (state->base.gui, "var1_variance-label");
	state->var1_variance = glade_xml_get_widget (state->base.gui, "var1-variance");
	state->var2_variance_label = glade_xml_get_widget (state->base.gui, "var2_variance-label");
	state->var2_variance = glade_xml_get_widget (state->base.gui, "var2-variance");
	state->mean_diff_entry = glade_xml_get_widget (state->base.gui, "meandiff");
	float_to_entry (GTK_ENTRY (state->mean_diff_entry), 0);
	state->alpha_entry = glade_xml_get_widget (state->base.gui, "one_alpha");
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
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->var1_variance));
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->var2_variance));
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->mean_diff_entry));
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->alpha_entry));

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	ttest_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, FALSE);

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
	analysis_tools_data_ftest_t  *data;

	GtkWidget *w;
	gint err;

	data = g_new0 (analysis_tools_data_ftest_t, 1);
	dao  = parse_output ((GenericToolState *)state, NULL);

	data->wbc = WORKBOOK_CONTROL (state->base.wbcg);

	if (state->base.warning_dialog != NULL)
		gtk_widget_destroy (state->base.warning_dialog);

	data->range_1 = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);

	data->range_2 =  gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry_2), state->base.sheet);

	w = glade_xml_get_widget (state->base.gui, "labels_button");
        data->labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = entry_to_float (GTK_ENTRY (state->alpha_entry), &data->alpha, TRUE);

	if (!cmd_analysis_tool (WORKBOOK_CONTROL (state->base.wbcg), state->base.sheet,
				dao, data, analysis_tool_ftest_engine))
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
	alpha_ready = (err == 0 && alpha > 0.0 && alpha < 1.0);
	input_1_ready = (input_range != NULL);
	input_2_ready = ((state->base.input_entry_2 == NULL) || (input_range_2 != NULL));
	output_ready = gnm_dao_is_ready (GNM_DAO (state->base.gdao));

        if (input_range != NULL) value_release (input_range);
        if (input_range_2 != NULL) value_release (input_range_2);

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

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, FTEST_KEY))
		return 0;

	state = g_new0 (FTestToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_F_TEST_TWO_SAMPLE,
			      "variance-tests.glade", "VarianceTests",
			      _("Could not create the FTest Tool dialog."),
			      FTEST_KEY,
			      G_CALLBACK (ftest_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (ftest_update_sensitivity_cb),
			      GNM_EE_SINGLE_RANGE))
		return 0;

	state->alpha_entry = glade_xml_get_widget (state->base.gui, "one_alpha");
	float_to_entry (GTK_ENTRY (state->alpha_entry), 0.05);
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->alpha_entry));
	g_signal_connect_after (G_OBJECT (state->alpha_entry),
		"changed",
		G_CALLBACK (ftest_update_sensitivity_cb), state);

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	ftest_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, FALSE);

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
	gboolean ready  = FALSE;
	int periodic, size, number, err_size, err_number;
        GSList *input_range;

        input_range = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
        periodic = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->periodic_button));

	err_size = entry_to_int
		(GTK_ENTRY ((periodic == 1) ? state->period_entry
			    : state->random_entry),
		 &size, FALSE);

	err_number = entry_to_int (GTK_ENTRY (state->number_entry), &number, FALSE);

	ready = ((input_range != NULL) &&
		 (err_size == 0 && size > 0) &&
		 (err_number == 0 && number > 0) &&
                 gnm_dao_is_ready (GNM_DAO (state->base.gdao)));

        if (input_range != NULL) range_list_destroy (input_range);

	gtk_widget_set_sensitive (state->base.apply_button, ready);
	gtk_widget_set_sensitive (state->base.ok_button, ready);
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
	gint err;

	data = g_new0 (analysis_tools_data_sampling_t, 1);
	dao  = parse_output ((GenericToolState *)state, NULL);

	data->base.wbc = WORKBOOK_CONTROL (state->base.wbcg);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->base.group_by = gnumeric_glade_group_value (state->base.gui, grouped_by_group);

	w = glade_xml_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

        data->periodic = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->periodic_button));

	if (data->periodic == 1) {
		err = entry_to_int (GTK_ENTRY (state->period_entry), &data->size, TRUE);
	} else {
		err = entry_to_int (GTK_ENTRY (state->random_entry), &data->size, TRUE);
	}
	err = entry_to_int (GTK_ENTRY (state->number_entry), &data->number, TRUE);

	if (!cmd_analysis_tool (WORKBOOK_CONTROL (state->base.wbcg), state->base.sheet,
			       dao, data, analysis_tool_sampling_engine))
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
dialog_sampling_realized (G_GNUC_UNUSED GtkWidget *widget,
			  SamplingState *state)
{
	gtk_widget_set_size_request (state->options_table,
				     state->options_table->allocation.width,
				     state->options_table->allocation.height);
	gtk_widget_set_size_request (state->random_button,
				     state->random_button->allocation.width,
				     state->random_button->allocation.height);
	gtk_widget_set_size_request (state->periodic_button,
				     state->periodic_button->allocation.width,
				     state->periodic_button->allocation.height);
	gtk_widget_set_size_request (state->method_label,
				     state->method_label->allocation.width,
				     state->method_label->allocation.height);
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

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, SAMPLING_KEY)) {
		return 0;
	}

	state = g_new0 (SamplingState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_SAMPLING,
			      "sampling.glade", "Sampling",
			      _("Could not create the Sampling Tool dialog."),
			      SAMPLING_KEY,
			      G_CALLBACK (sampling_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (sampling_tool_update_sensitivity_cb),
			      0))
		return 0;

	state->periodic_button  = glade_xml_get_widget (state->base.gui, "periodic-button");
	state->random_button  = glade_xml_get_widget (state->base.gui, "random-button");
	state->method_label = glade_xml_get_widget (state->base.gui, "method-label");
	state->options_table = glade_xml_get_widget (state->base.gui, "options-table");
	state->period_label = glade_xml_get_widget (state->base.gui, "period-label");
	state->random_label = glade_xml_get_widget (state->base.gui, "random-label");
	state->period_entry = glade_xml_get_widget (state->base.gui, "period-entry");
	state->random_entry = glade_xml_get_widget (state->base.gui, "random-entry");
	state->number_entry = glade_xml_get_widget (state->base.gui, "number-entry");
	int_to_entry (GTK_ENTRY (state->number_entry), 1);

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
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->period_entry));
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->random_entry));
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->number_entry));

	gnm_dao_set_put (GNM_DAO (state->base.gdao), FALSE, FALSE);
	sampling_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);

        return 0;
}
/**********************************************/
/*  End of sampling tool code */
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
regression_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			       RegressionToolState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_regression_t  *data;

	GtkWidget *w;
	gint err;
	gnm_float confidence;

	if (state->base.warning_dialog != NULL)
		gtk_widget_destroy (state->base.warning_dialog);

	data = g_new0 (analysis_tools_data_regression_t, 1);
	dao  = parse_output ((GenericToolState *)state, NULL);

	data->base.wbc = WORKBOOK_CONTROL (state->base.wbcg);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->y_input = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry_2), state->base.sheet);
	data->base.group_by = gnumeric_glade_group_value (state->base.gui, grouped_by_group);

	w = glade_xml_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = entry_to_float (GTK_ENTRY (state->confidence_entry), &confidence, TRUE);
	data->alpha = 1 - confidence;

	w = glade_xml_get_widget (state->base.gui, "intercept-button");
	data->intercept = 1 - gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	if (cmd_analysis_tool (WORKBOOK_CONTROL (state->base.wbcg), state->base.sheet,
			       dao, data, analysis_tool_regression_engine)) {
		char *text;

		switch ( data->base.err) {
		case analysis_tools_reported_err_input:
			gnm_expr_entry_grab_focus
				(GNM_EXPR_ENTRY (state->base.input_entry),
				 TRUE);
			break;
		case analysis_tools_reported_err:
			break;
		case  analysis_tools_REG_invalid_dimensions:
			error_in_entry ((GenericToolState *) state,
					GTK_WIDGET (state->base.input_entry),
			      _("There must be an equal number of entries "
				"for each variable in the regression."));
			break;
		default:
			text = g_strdup_printf (
				_("An unexpected error has occurred: %d."), data->base.err);
			error_in_entry ((GenericToolState *) state,
					GTK_WIDGET (state->base.input_entry), text);
			g_free (text);
			break;
		}
		if (data->base.input)
			range_list_destroy (data->base.input);
		if (data->y_input)
			value_release (data->y_input);
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
	gboolean ready  = FALSE;
	gboolean input_1_ready  = FALSE;
	gboolean input_2_ready  = FALSE;
	gboolean output_ready  = FALSE;
	int err;
	gnm_float confidence;
        GSList *input_range;
        GnmValue *input_range_2;

        input_range = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	input_range_2 = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry_2), state->base.sheet);

	err = entry_to_float (GTK_ENTRY (state->confidence_entry), &confidence, FALSE);

	input_1_ready = (input_range != NULL);
	input_2_ready = (input_range_2 != NULL);
	output_ready =  gnm_dao_is_ready (GNM_DAO (state->base.gdao));

	ready = input_1_ready &&
		input_2_ready &&
		(err == 0) && (1 > confidence ) && (confidence > 0) &&
		output_ready;

        if (input_range != NULL) range_list_destroy (input_range);
        if (input_range_2 != NULL) value_release (input_range_2);

	gtk_widget_set_sensitive (state->base.ok_button, ready);
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

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, REGRESSION_KEY))
		return 0;

	state = g_new0 (RegressionToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_REGRESSION,
			      "regression.glade", "Regression",
			      _("Could not create the Regression Tool dialog."),
			      REGRESSION_KEY,
			      G_CALLBACK (regression_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (regression_tool_update_sensitivity_cb),
			      0))
		return 0;

	state->confidence_entry = glade_xml_get_widget (state->base.gui, "confidence-entry");
	float_to_entry (GTK_ENTRY (state->confidence_entry), 0.95);
	g_signal_connect_after (G_OBJECT (state->confidence_entry),
		"changed",
		G_CALLBACK (regression_tool_update_sensitivity_cb), state);
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->confidence_entry));

	gnm_dao_set_put (GNM_DAO (state->base.gdao), FALSE, FALSE);
	regression_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);

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
	gint                    err;

	data = g_new0 (analysis_tools_data_exponential_smoothing_t, 1);
	dao  = parse_output ((GenericToolState *)state, NULL);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->base.group_by = gnumeric_glade_group_value (state->base.gui, grouped_by_group);

	w = glade_xml_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = entry_to_float (GTK_ENTRY (state->damping_fact_entry), &data->damp_fact, TRUE);

	w = glade_xml_get_widget (state->base.gui, "std_errors_button");
	data->std_error_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	if (!cmd_analysis_tool (WORKBOOK_CONTROL (state->base.wbcg), state->base.sheet,
			       dao, data, analysis_tool_exponential_smoothing_engine))
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
	gboolean ready  = FALSE;
	int err;
	gnm_float damp_fact;
        GSList *input_range;

        input_range = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	err = entry_to_float (GTK_ENTRY (state->damping_fact_entry), &damp_fact, FALSE);

	ready = ((input_range != NULL) &&
                 (err == 0 && damp_fact >= 0 && damp_fact <= 1) &&
                 gnm_dao_is_ready (GNM_DAO (state->base.gdao)));

        if (input_range != NULL) range_list_destroy (input_range);

	gtk_widget_set_sensitive (state->base.ok_button, ready);
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

	if (wbcg == NULL) {
		return 1;
	}

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, EXP_SMOOTHING_KEY))
		return 0;

	state = g_new0 (ExpSmoothToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_EXP_SMOOTHING,
			      "exp-smoothing.glade",
			      "ExpSmoothing",
			      _("Could not create the Exponential Smoothing "
				"Tool dialog."),
			      EXP_SMOOTHING_KEY,
			      G_CALLBACK (exp_smoothing_tool_ok_clicked_cb),
			      NULL,
			      G_CALLBACK (exp_smoothing_tool_update_sensitivity_cb),
			      0))
		return 0;

	state->damping_fact_entry = glade_xml_get_widget (state->base.gui,
							  "damping-fact-entry");
	float_to_entry (GTK_ENTRY (state->damping_fact_entry), 0.2);
	g_signal_connect_after (G_OBJECT (state->damping_fact_entry),
		"changed",
		G_CALLBACK (exp_smoothing_tool_update_sensitivity_cb), state);
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->damping_fact_entry));

	gnm_dao_set_put (GNM_DAO (state->base.gdao), FALSE, FALSE);
	exp_smoothing_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);

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
	gint                    err;

	data = g_new0 (analysis_tools_data_moving_average_t, 1);
	dao  = parse_output ((GenericToolState *)state, NULL);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->base.group_by = gnumeric_glade_group_value (state->base.gui, grouped_by_group);

	w = glade_xml_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	err = entry_to_int (GTK_ENTRY (state->interval_entry), &data->interval, TRUE);

	w = glade_xml_get_widget (state->base.gui, "std_errors_button");
	data->std_error_flag = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	if (!cmd_analysis_tool (WORKBOOK_CONTROL (state->base.wbcg), state->base.sheet,
			       dao, data, analysis_tool_moving_average_engine))
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
	gboolean ready  = FALSE;
	int interval, err;
        GSList *input_range;

        input_range = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	err = entry_to_int (GTK_ENTRY (state->interval_entry), &interval, FALSE);

	ready = ((input_range != NULL) &&
                 (err == 0 && interval > 0) &&
                 gnm_dao_is_ready (GNM_DAO (state->base.gdao)));

        if (input_range != NULL) range_list_destroy (input_range);

	gtk_widget_set_sensitive (state->base.ok_button, ready);
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

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, AVERAGE_KEY))
		return 0;

	state = g_new0 (AverageToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_MOVING_AVERAGES,
			      "moving-averages.glade",
			      "MovAverages",
			      _("Could not create the Moving Average Tool "
				"dialog."),
			      AVERAGE_KEY,
			      G_CALLBACK (average_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (average_tool_update_sensitivity_cb),
			      0))
		return 0;

	state->interval_entry = glade_xml_get_widget (state->base.gui, "interval-entry");
	int_to_entry (GTK_ENTRY (state->interval_entry), 3);
	g_signal_connect_after (G_OBJECT (state->interval_entry),
		"changed",
		G_CALLBACK (average_tool_update_sensitivity_cb), state);
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->interval_entry));

	gnm_dao_set_put (GNM_DAO (state->base.gdao), FALSE, FALSE);
	average_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);

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
	gboolean ready  = FALSE;
	gboolean input_ready  = FALSE;
	gboolean bin_ready  = FALSE;
	gboolean output_ready  = FALSE;

	int the_n;
	gboolean predetermined_bins;
        GSList *input_range;
        GnmValue *input_range_2 = NULL;

        input_range = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);

	predetermined_bins = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->predetermined_button));
	if (predetermined_bins)
		input_range_2 =  gnm_expr_entry_parse_as_value
			(GNM_EXPR_ENTRY (state->base.input_entry_2), state->base.sheet);

	input_ready = (input_range != NULL);
	bin_ready = (predetermined_bins && input_range_2 != NULL) ||
		(!predetermined_bins && entry_to_int(state->n_entry, &the_n,FALSE) == 0
			&& the_n > 0);
	output_ready = gnm_dao_is_ready (GNM_DAO (state->base.gdao));

        if (input_range != NULL) range_list_destroy (input_range);
        if (input_range_2 != NULL) value_release (input_range_2);

	ready = input_ready && bin_ready && output_ready;
	gtk_widget_set_sensitive (state->base.ok_button, ready);
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
	dao  = parse_output ((GenericToolState *)state, NULL);

	data->input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->group_by = gnumeric_glade_group_value (state->base.gui, grouped_by_group);

	if (gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->predetermined_button))) {
		w = glade_xml_get_widget (state->base.gui, "labels_2_button");
		data->bin_labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
		data->bin = g_slist_prepend (NULL, gnm_expr_entry_parse_as_value
					     (GNM_EXPR_ENTRY (state->base.input_entry_2),
					      state->base.sheet));
	} else {
		entry_to_int(state->n_entry, &data->n,TRUE);
		data->max_given = (0 == entry_to_float (state->max_entry,
							    &data->max , TRUE));
		data->min_given = (0 == entry_to_float (state->min_entry,
							    &data->min , TRUE));
		data->bin = NULL;
	}

	w = glade_xml_get_widget (state->base.gui, "labels_button");
	data->labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	w = glade_xml_get_widget (state->base.gui, "pareto-button");
	data->pareto = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	w = glade_xml_get_widget (state->base.gui, "percentage-button");
	data->percentage = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	w = glade_xml_get_widget (state->base.gui, "cum-button");
	data->cumulative = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	w = glade_xml_get_widget (state->base.gui, "chart-button");
	data->chart = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	if (!cmd_analysis_tool (WORKBOOK_CONTROL (state->base.wbcg), state->base.sheet,
			       dao, data, analysis_tool_histogram_engine))
		gtk_widget_destroy (state->base.dialog);

/*				_("Each row of the bin range should contain one numeric value\n" */
/*				  "(ignoring the label if applicable).")); */
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
 * histogram_tool_set_predetermined_on_toggle:
 * @widget:
 * @state:
 *
 **/
static void
histogram_tool_set_predetermined_on_toggle (G_GNUC_UNUSED GtkWidget *widget,
					    HistogramToolState *state)
{
	    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->predetermined_button), TRUE);
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

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, HISTOGRAM_KEY))
		return 0;

	state = g_new0 (HistogramToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_HISTOGRAM,
			      "histogram.glade", "Histogram",
			      _("Could not create the Histogram Tool dialog."),
			      HISTOGRAM_KEY,
			      G_CALLBACK (histogram_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (histogram_tool_update_sensitivity_cb),
			      0))
		return 0;

	state->predetermined_button = GTK_WIDGET (glade_xml_get_widget
						  (state->base.gui,
						   "pre_determined_button"));
	state->calculated_button = GTK_WIDGET (glade_xml_get_widget
					       (state->base.gui,
						"calculated_button"));
	state->bin_labels_button = GTK_WIDGET (glade_xml_get_widget
					       (state->base.gui,
						"labels_2_button"));
	state->n_entry = GTK_ENTRY(glade_xml_get_widget (state->base.gui,
							  "n_entry"));
	state->max_entry = GTK_ENTRY(glade_xml_get_widget (state->base.gui,
							    "max_entry"));
	state->min_entry = GTK_ENTRY(glade_xml_get_widget (state->base.gui,
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
		"focus-in-event",
		G_CALLBACK (histogram_tool_set_calculated), state);
	g_signal_connect (G_OBJECT (state->min_entry),
		"focus-in-event",
		G_CALLBACK (histogram_tool_set_calculated), state);
	g_signal_connect (G_OBJECT (state->max_entry),
		"focus-in-event",
		G_CALLBACK (histogram_tool_set_calculated), state);
	g_signal_connect (G_OBJECT
			  (gnm_expr_entry_get_entry (
				  GNM_EXPR_ENTRY (state->base.input_entry_2))),
		"focus-in-event",
		G_CALLBACK (histogram_tool_set_predetermined), state);
	g_signal_connect (G_OBJECT (state->bin_labels_button),
		"toggled",
		G_CALLBACK (histogram_tool_set_predetermined_on_toggle), state);

	gnm_dao_set_put (GNM_DAO (state->base.gdao), FALSE, FALSE);
	histogram_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);

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
	dao  = parse_output ((GenericToolState *)state, NULL);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->base.group_by = gnumeric_glade_group_value (state->base.gui, grouped_by_group);

	w = glade_xml_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	data->alpha = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->alpha_entry));

	if (!cmd_analysis_tool (WORKBOOK_CONTROL (state->base.wbcg), state->base.sheet,
				dao, data, analysis_tool_anova_single_engine))
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

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, ANOVA_SINGLE_KEY))
		return 0;

	state = g_new0 (AnovaSingleToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_ANOVA_SINGLE_FACTOR,
			      "anova-one.glade", "ANOVA",
			      _("Could not create the ANOVA (single factor) "
				"tool dialog."),
			      ANOVA_SINGLE_KEY,
			      G_CALLBACK (anova_single_tool_ok_clicked_cb),
			      NULL,
			      G_CALLBACK (anova_single_tool_update_sensitivity_cb),
			      0))
		return 0;

	state->alpha_entry = glade_xml_get_widget (state->base.gui,
						   "alpha-entry");
	float_to_entry (GTK_ENTRY (state->alpha_entry), 0.05);
	g_signal_connect_after (G_OBJECT (state->alpha_entry),
		"changed",
		G_CALLBACK (anova_single_tool_update_sensitivity_cb), state);
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->alpha_entry));

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	anova_single_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);

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
	gint err;
	analysis_tools_data_anova_two_factor_t *data;
	char *text;

	if (state->base.warning_dialog != NULL)
		gtk_widget_destroy (state->base.warning_dialog);

	data = g_new0 (analysis_tools_data_anova_two_factor_t, 1);
	dao  = parse_output ((GenericToolState *)state, NULL);

	data->input = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry),
		 state->base.sheet);
	data->err = analysis_tools_noerr;
	data->wbc = WORKBOOK_CONTROL (state->base.wbcg);

	w = glade_xml_get_widget (state->base.gui, "labels_button");
        data->labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	data->alpha = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->alpha_entry));
	err = entry_to_int (GTK_ENTRY (state->replication_entry),
			    &data->replication, TRUE);

	if (cmd_analysis_tool (WORKBOOK_CONTROL (state->base.wbcg),
			       state->base.sheet,
			       dao, data, analysis_tool_anova_two_factor_engine)) {
		switch (data->err) {
		case analysis_tools_missing_data:
			error_in_entry ((GenericToolState *) state,
					GTK_WIDGET (state->base.input_entry),
					data->labels ? _("The given input range should contain at "
					  "least two columns and two rows of data and the "
					  "labels.") :
					_("The given input range should contain at "
					  "least two columns and two rows of data."));
			break;
		case analysis_tools_too_few_cols:
			error_in_entry ((GenericToolState *) state,
					GTK_WIDGET (state->base.input_entry),
					data->labels ? _("The given input range should contain at "
					  "least two columns of data and the "
					  "labels.") :
					_("The given input range should contain at "
					  "least two columns of data."));
			break;
		case analysis_tools_too_few_rows:
			error_in_entry ((GenericToolState *) state,
					GTK_WIDGET (state->base.input_entry),
					data->labels ? _("The given input range should contain at "
					  "least two rows of data and the "
					  "labels.") :
					_("The given input range should "
					  "contain at least two rows of "
					  "data."));
			break;
		case analysis_tools_replication_invalid:
			error_in_entry ((GenericToolState *) state,
					GTK_WIDGET (state->base.input_entry),
					_("The number of data rows must be a "
					  "multiple of the replication "
					  "number."));
			break;
		default:
			text = g_strdup_printf (
				_("An unexpected error has occurred: %d."),
				data->err);
			error_in_entry ((GenericToolState *) state,
					GTK_WIDGET (state->base.input_entry),
					text);
			g_free (text);
			break;
		}
		if (data->input)
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

	if (wbcg == NULL)
		return 1;

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, ANOVA_TWO_FACTOR_KEY))
		return 0;

	state = g_new0 (AnovaTwoFactorToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_ANOVA_TWO_FACTOR,
			      "anova-two.glade", "ANOVA",
			      _("Could not create the ANOVA (two factor) "
				"tool dialog."),
			      ANOVA_TWO_FACTOR_KEY,
			      G_CALLBACK (anova_two_factor_tool_ok_clicked_cb),
			      NULL,
			      G_CALLBACK (anova_two_factor_tool_update_sensitivity_cb),
			      GNM_EE_SINGLE_RANGE))
		return 0;

	state->alpha_entry = glade_xml_get_widget (state->base.gui,
						   "alpha-entry");
	float_to_entry (GTK_ENTRY(state->alpha_entry), 0.05);
	state->replication_entry = glade_xml_get_widget (state->base.gui,
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
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->alpha_entry));
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->replication_entry));

	gnm_dao_set_put (GNM_DAO (state->base.gdao), FALSE, FALSE);
	anova_two_factor_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, FALSE);

        return 0;
}

/**********************************************/
/*  End of ANOVA (Two Factor) tool code */
/**********************************************/
