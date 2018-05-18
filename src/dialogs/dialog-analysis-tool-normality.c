/*
 * dialog-analysis-tool-normality.c:
 *
 * Authors:
  *  Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2009 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
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
#include <tools/analysis-normality.h>
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

#define NORMALITY_KEY      "analysistools-normality-dialog"

typedef struct {
	GnmGenericToolState base;
	GtkWidget *alpha_entry;
} NormalityTestsToolState;


static char const * const grouped_by_group[] = {
	"grouped_by_row",
	"grouped_by_col",
	"grouped_by_area",
	NULL
};

static char const * const test_group[] = {
	"andersondarling",
	"cramervonmises",
	"lilliefors",
	"shapirofrancia",
	NULL
};

/**
 * normality_tool_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity
 **/
static void
normality_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				      NormalityTestsToolState *state)
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
 * normality_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the normality_tool.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
normality_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			      NormalityTestsToolState *state)
{
	data_analysis_output_t  *dao;
	GtkWidget *w;
	analysis_tools_data_normality_t *data;

	data = g_new0 (analysis_tools_data_normality_t, 1);
	dao  = parse_output ((GnmGenericToolState *)state, NULL);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->base.group_by = gnm_gui_group_value (state->base.gui, grouped_by_group);

	w = go_gtk_builder_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	data->alpha = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->alpha_entry));

	data->type = gnm_gui_group_value (state->base.gui, test_group);

	w = go_gtk_builder_get_widget (state->base.gui, "normalprobabilityplot");
	data->graph = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg),
				state->base.sheet,
				dao, data, analysis_tool_normality_engine,
				TRUE))
		gtk_widget_destroy (state->base.dialog);

	return;
}

/**
 * dialog_normality_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
int
dialog_normality_tool (WBCGtk *wbcg, Sheet *sheet)
{
        NormalityTestsToolState *state;
	char const * plugins[] = { "Gnumeric_fnstat",
				   "Gnumeric_fnlogical",
				   NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, NORMALITY_KEY))
		return 0;

	state = g_new0 (NormalityTestsToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_NORMALITY,
			      "res:ui/normality-tests.ui", "Normality-Tests",
			      _("Could not create the Normality Test Tool dialog."),
			      NORMALITY_KEY,
			      G_CALLBACK (normality_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (normality_tool_update_sensitivity_cb),
			      0))
	{
		g_free(state);
		return 0;
	}


	state->alpha_entry = tool_setup_update
		(&state->base, "alpha-entry",
		 G_CALLBACK (normality_tool_update_sensitivity_cb),
		 state);

	float_to_entry (GTK_ENTRY (state->alpha_entry), 0.05);

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	normality_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

        return 0;
}
