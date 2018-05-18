/*
 * dialog-analysis-tool-one-mean.c:
 *
 * Authors:
  *  Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2012 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
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
#include <tools/analysis-one-mean-test.h>
#include <tools/analysis-tools.h>

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

#define ONE_MEAN_TEST_KEY      "analysistools-one-mean-test-dialog"

static char const * const grouped_by_group[] = {
	"grouped_by_row",
	"grouped_by_col",
	"grouped_by_area",
	NULL
};

typedef struct {
	GnmGenericToolState base;
	GtkWidget *alpha_entry;
	GtkWidget *mean_entry;
} OneeMeanTestToolState;

/**
 * one_mean_test_tool_update_common_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity
 **/
static gboolean
one_mean_test_tool_update_common_sensitivity_cb (OneeMeanTestToolState *state)
{
	gnm_float alpha;
	gnm_float mean;
	gboolean err;

	/* Checking Mean*/
	err = entry_to_float
		(GTK_ENTRY (state->mean_entry), &mean, FALSE);
	if (err) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The predicted mean should be a number."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return FALSE;
	}

	/* Checking Alpha*/
	alpha = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->alpha_entry));
	if (!(alpha > 0 && alpha < 1)) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The alpha value should "
				      "be a number between 0 and 1."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return FALSE;
	}

	/* Checking Output Page */
	if (!gnm_dao_is_ready (GNM_DAO (state->base.gdao))) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The output specification "
				      "is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return FALSE;
	}

	return TRUE;
}




/************************************************************************************/

/**
 * one_mean_test_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the one_mean_test_tool.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
one_mean_test_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			      OneeMeanTestToolState *state)
{
	data_analysis_output_t  *dao;
	GtkWidget *w;
	analysis_tools_data_one_mean_test_t *data;

	data = g_new0 (analysis_tools_data_one_mean_test_t, 1);
	dao  = parse_output ((GnmGenericToolState *)state, NULL);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->base.group_by = gnm_gui_group_value (state->base.gui, grouped_by_group);

	w = go_gtk_builder_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (w));

	entry_to_float
		(GTK_ENTRY (state->mean_entry), &data->mean, FALSE);
	data->alpha = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->alpha_entry));

	if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg),
				state->base.sheet,
				dao, data, analysis_tool_one_mean_test_engine,
				TRUE))
		gtk_widget_destroy (state->base.dialog);

	return;
}

/**
 * one_mean_test_tool_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity
 **/
static void
one_mean_test_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				      OneeMeanTestToolState *state)
{
        GSList *input_range;

	/* Checking first input range*/
        input_range = gnm_expr_entry_parse_as_list
		(GNM_EXPR_ENTRY (state->base.input_entry),
		 state->base.sheet);
	if (input_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    (state->base.input_entry_2 == NULL)
				    ? _("The input range is invalid.")
				    : _("The first input range is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	} else
		range_list_destroy (input_range);

	if (one_mean_test_tool_update_common_sensitivity_cb (state)) {
		gtk_label_set_text (GTK_LABEL (state->base.warning), "");
		gtk_widget_set_sensitive (state->base.ok_button, TRUE);
	}

}

/**
 * dialog_one_mean_test_tool:
 *
 **/
int
dialog_one_mean_test_tool (WBCGtk *wbcg, Sheet *sheet)
{
	char const * plugins[] = { "Gnumeric_fnstat",
				   "Gnumeric_fnlogical",
				   "Gnumeric_fnmath",
				   NULL};
        OneeMeanTestToolState *state;

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, ONE_MEAN_TEST_KEY))
		return 0;

	state = g_new0 (OneeMeanTestToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_ONE_MEAN,
			      "res:ui/one-mean-test.ui", "One-Mean-Test",
			      _("Could not create the Student-t Test Tool dialog."),
			      ONE_MEAN_TEST_KEY,
			      G_CALLBACK (one_mean_test_tool_ok_clicked_cb),
			      NULL,
			      G_CALLBACK (one_mean_test_tool_update_sensitivity_cb),
			      0))
	{
		g_free(state);
		return 0;
	}


	state->alpha_entry = tool_setup_update
		(&state->base, "alpha-entry",
		 G_CALLBACK (one_mean_test_tool_update_sensitivity_cb),
		 state);
	state->mean_entry = tool_setup_update
		(&state->base, "mean-entry",
		 G_CALLBACK (one_mean_test_tool_update_sensitivity_cb),
		 state);

	int_to_entry (GTK_ENTRY (state->mean_entry), 0);
	float_to_entry (GTK_ENTRY (state->alpha_entry), 0.05);

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	one_mean_test_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

	return 0;

}
