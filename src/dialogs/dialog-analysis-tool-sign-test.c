/*
 * dialog-analysis-tool-sign-test.c:
 *
 * Authors:
  *  Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2009-2010 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
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
#include <tools/analysis-sign-test.h>
#include <tools/analysis-signed-rank-test.h>
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

#define SIGN_TEST_KEY_ONE      "analysistools-sign-test-one-dialog"
#define SIGN_TEST_KEY_TWO      "analysistools-sign-test-two-dialog"

static char const * const grouped_by_group[] = {
	"grouped_by_row",
	"grouped_by_col",
	"grouped_by_area",
	NULL
};

typedef struct {
	GnmGenericToolState base;
	GtkWidget *alpha_entry;
	GtkWidget *median_entry;
} SignTestToolState;

/**
 * sign_test_tool_update_common_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity
 **/
static gboolean
sign_test_tool_update_common_sensitivity_cb (SignTestToolState *state)
{
	gnm_float alpha;
	gnm_float median;
	gboolean err;

	/* Checking Median*/
	err = entry_to_float
		(GTK_ENTRY (state->median_entry), &median, FALSE);
	if (err) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The predicted median should be a number."));
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


/**
 * sign_test_two_tool_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity
 **/
static void
sign_test_two_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
					  SignTestToolState *state)
{
        GnmValue *input_range;
 	gint w, h;

	/* Checking first input range*/
        input_range = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry),
		 state->base.sheet);
	if (input_range == NULL || !VALUE_IS_CELLRANGE (input_range)) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    (state->base.input_entry_2 == NULL)
				    ? _("The input range is invalid.")
				    : _("The first input range is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		value_release (input_range);
		return;
	} else {
		GnmRange r;
		range_init_rangeref (&r, &(input_range->v_range.cell));
		w = range_width (&r);
		h = range_height (&r);
		value_release (input_range);
	}

	/* Checking second input range*/
	if (state->base.input_entry_2 != NULL) {
		input_range = gnm_expr_entry_parse_as_value
			(GNM_EXPR_ENTRY (state->base.input_entry_2),
			 state->base.sheet);
		if (input_range == NULL || !VALUE_IS_CELLRANGE (input_range)) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The second input range is invalid."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			value_release (input_range);
			return;
		} else {
			GnmRange r;
			range_init_rangeref (&r, &(input_range->v_range.cell));
			value_release (input_range);
			if (w != range_width (&r) ||
			    h != range_height (&r)) {
				gtk_label_set_text
					(GTK_LABEL (state->base.warning),
					 _("The input ranges do not have the same shape."));
				gtk_widget_set_sensitive
					(state->base.ok_button, FALSE);
			return;

			}
		}
	}

	if (sign_test_tool_update_common_sensitivity_cb (state)) {
		gtk_label_set_text (GTK_LABEL (state->base.warning), "");
		gtk_widget_set_sensitive (state->base.ok_button, TRUE);
	}
}

static void
sign_test_two_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			      SignTestToolState *state)
{
	data_analysis_output_t  *dao;
	GtkWidget *w;
	analysis_tools_data_sign_test_two_t *data;
	analysis_tool_engine engine;

	data = g_new0 (analysis_tools_data_sign_test_two_t, 1);
	dao  = parse_output ((GnmGenericToolState *)state, NULL);

	data->base.range_1 = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);

	data->base.range_2 = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry_2), state->base.sheet);

	w = go_gtk_builder_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (w));

	entry_to_float
		(GTK_ENTRY (state->median_entry), &data->median, FALSE);

	data->base.alpha = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->alpha_entry));

	w =  go_gtk_builder_get_widget (state->base.gui, "signtest");
	engine =  gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w))
		? analysis_tool_sign_test_two_engine
		: analysis_tool_signed_rank_test_two_engine;

	if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg),
				state->base.sheet,
				dao, data, engine, TRUE))
		gtk_widget_destroy (state->base.dialog);

	return;
}

/**
 * dialog_sign_test_two_tool:
 *
 **/
int
dialog_sign_test_two_tool (WBCGtk *wbcg, Sheet *sheet, signtest_type type)
{
	char const * plugins[] = { "Gnumeric_fnstat",
				   "Gnumeric_fnlogical",
				   "Gnumeric_fnmath",
				   "Gnumeric_fninfo",
				   NULL};
        SignTestToolState *state;
	GtkWidget *w;

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, SIGN_TEST_KEY_TWO))
		return 0;

	state = g_new0 (SignTestToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_SIGN_TEST_TWO,
			      "res:ui/sign-test-two.ui", "Sign-Test",
			      _("Could not create the Sign Test Tool dialog."),
			      SIGN_TEST_KEY_TWO,
			      G_CALLBACK (sign_test_two_tool_ok_clicked_cb),
			      NULL,
			      G_CALLBACK (sign_test_two_tool_update_sensitivity_cb),
			      GNM_EE_SINGLE_RANGE))
	{
		g_free(state);
		return 0;
	}


	state->alpha_entry = tool_setup_update
		(&state->base, "alpha-entry",
		 G_CALLBACK (sign_test_two_tool_update_sensitivity_cb),
		 state);
	float_to_entry (GTK_ENTRY (state->alpha_entry), 0.05);

	state->median_entry = tool_setup_update
		(&state->base, "median-entry",
		 G_CALLBACK (sign_test_two_tool_update_sensitivity_cb),
		 state);
	int_to_entry (GTK_ENTRY (state->median_entry), 0);

	w =  go_gtk_builder_get_widget (state->base.gui,
				   (type == SIGNTEST) ? "signtest"
				   : "signedranktest");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), TRUE);

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	sign_test_two_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

	return 0;

}

/************************************************************************************/

/**
 * sign_test_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the sign_test_tool.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
sign_test_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			      SignTestToolState *state)
{
	data_analysis_output_t  *dao;
	GtkWidget *w;
	analysis_tools_data_sign_test_t *data;
	analysis_tool_engine engine;

	data = g_new0 (analysis_tools_data_sign_test_t, 1);
	dao  = parse_output ((GnmGenericToolState *)state, NULL);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->base.group_by = gnm_gui_group_value (state->base.gui, grouped_by_group);

	w = go_gtk_builder_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (w));

	entry_to_float
		(GTK_ENTRY (state->median_entry), &data->median, FALSE);
	data->alpha = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->alpha_entry));

	w =  go_gtk_builder_get_widget (state->base.gui, "signtest");
	engine =  gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w))
		? analysis_tool_sign_test_engine
		: analysis_tool_signed_rank_test_engine;

	if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg),
				state->base.sheet,
				dao, data, engine, TRUE))
		gtk_widget_destroy (state->base.dialog);

	return;
}

/**
 * sign_test_tool_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity
 **/
static void
sign_test_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				      SignTestToolState *state)
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

	if (sign_test_tool_update_common_sensitivity_cb (state)) {
		gtk_label_set_text (GTK_LABEL (state->base.warning), "");
		gtk_widget_set_sensitive (state->base.ok_button, TRUE);
	}

}

/**
 * dialog_sign_test_tool:
 *
 **/
int
dialog_sign_test_tool (WBCGtk *wbcg, Sheet *sheet, signtest_type type)
{
	char const * plugins[] = { "Gnumeric_fnstat",
				   "Gnumeric_fnlogical",
				   "Gnumeric_fnmath",
				   "Gnumeric_fninfo",
				   NULL};
        SignTestToolState *state;
	GtkWidget *w;

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, SIGN_TEST_KEY_ONE))
		return 0;

	state = g_new0 (SignTestToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_SIGN_TEST,
			      "res:ui/sign-test.ui", "Sign-Test",
			      _("Could not create the Sign Test Tool dialog."),
			      SIGN_TEST_KEY_ONE,
			      G_CALLBACK (sign_test_tool_ok_clicked_cb),
			      NULL,
			      G_CALLBACK (sign_test_tool_update_sensitivity_cb),
			      0))
	{
		g_free(state);
		return 0;
	}


	state->alpha_entry = tool_setup_update
		(&state->base, "alpha-entry",
		 G_CALLBACK (sign_test_two_tool_update_sensitivity_cb),
		 state);
	state->median_entry = tool_setup_update
		(&state->base, "median-entry",
		 G_CALLBACK (sign_test_two_tool_update_sensitivity_cb),
		 state);

	int_to_entry (GTK_ENTRY (state->median_entry), 0);
	float_to_entry (GTK_ENTRY (state->alpha_entry), 0.05);

	w =  go_gtk_builder_get_widget (state->base.gui,
				   (type == SIGNTEST) ? "signtest"
				   : "signedranktest");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), TRUE);

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	sign_test_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

	return 0;

}
