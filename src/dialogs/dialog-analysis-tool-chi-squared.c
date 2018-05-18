/*
 * dialog-analysis-tool-chi-squared.c:
 *
 * Authors:
  *  Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2008 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
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
#include <tools/analysis-chi-squared.h>
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

#define CHI_SQUARED_I_KEY      "analysistools-chi-square-independence-dialog"

typedef struct {
	GnmGenericToolState base;
	GtkWidget *alpha_entry;
	GtkWidget *label;
} ChiSquaredIToolState;

/**
 * chi_squared_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the fourier_tool.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
chi_squared_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
				 ChiSquaredIToolState *state)
{
	data_analysis_output_t  *dao;
	GtkWidget *w;
	analysis_tools_data_chi_squared_t *data;

	data = g_new0 (analysis_tools_data_chi_squared_t, 1);
	dao  = parse_output ((GnmGenericToolState *)state, NULL);

	data->input = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry),
		 state->base.sheet);

	data->wbc = GNM_WBC (state->base.wbcg);

        data->labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->label));

	data->alpha = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->alpha_entry));

	w = go_gtk_builder_get_widget (state->base.gui, "test-of-independence");
	data->independence = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	data->n_c = (data->input->v_range.cell.b.col - data->input->v_range.cell.a.col + 1);
	data->n_r = (data->input->v_range.cell.b.row - data->input->v_range.cell.a.row + 1);

	if (data->labels)
		data->n_c--, data->n_r--;


	if (!cmd_analysis_tool (data->wbc, state->base.sheet,
				dao, data, analysis_tool_chi_squared_engine,
				TRUE))
		gtk_widget_destroy (state->base.dialog);

	return;
}

static int
calc_min_size (GnmValue *input_range)
{
	int row = (input_range->v_range.cell.b.row - input_range->v_range.cell.a.row + 1);
	int col = (input_range->v_range.cell.b.col - input_range->v_range.cell.a.col + 1);

	return ((row < col) ? row : col);
}

/**
 * chi_squared_tool_update_sensitivity_cb:
 * @state:
 *
 * Update the dialog widgets sensitivity.
 * We cannot use tool_update_sensitivity_cb
 * since we are also considering whether in fact
 * an alpha is given.
 **/
static void
chi_squared_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
					ChiSquaredIToolState  *state)
{
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
	} else {
		int min_size = calc_min_size (input_range);
		gboolean label = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->label));
		value_release (input_range);

		if (min_size < (label ? 3 : 2)) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The input range is too small."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			return;
		}
	}

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
 * dialog_chi_squared_tool:
 * @wbcg:
 * @sheet:
 * @independence
 *
 * Show the dialog (guru).
 *
 **/
int
dialog_chi_square_tool (WBCGtk *wbcg, Sheet *sheet, gboolean independence)
{
        ChiSquaredIToolState *state;
	char const *type;
	char const * plugins[] = { "Gnumeric_fnstat",
				   "Gnumeric_fnlookup",
				   "Gnumeric_fnmath",
				   NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, CHI_SQUARED_I_KEY))
		return 0;

	state = g_new0 (ChiSquaredIToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_CHI_SQUARED,
			      "res:ui/chi-squared.ui", "Chi-Squared Tests",
			      _("Could not create the Chi Squared Tests "
				"tool dialog."),
			      CHI_SQUARED_I_KEY,
			      G_CALLBACK (chi_squared_tool_ok_clicked_cb),
			      NULL,
			      G_CALLBACK (chi_squared_tool_update_sensitivity_cb),
			      GNM_EE_SINGLE_RANGE))
	{
		g_free(state);
		return 0;
	}

	if (independence)
		type ="test-of-independence";
	else
		type ="test-of-homogeneity";
	gtk_toggle_button_set_active
		(GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->base.gui,
							  type)),
		 TRUE);

	state->label = tool_setup_update
		(&state->base, "labels_button",
		 G_CALLBACK (chi_squared_tool_update_sensitivity_cb),
		 state);

	state->alpha_entry = tool_setup_update
		(&state->base, "alpha-entry",
		 G_CALLBACK (chi_squared_tool_update_sensitivity_cb),
		 state);
	float_to_entry (GTK_ENTRY (state->alpha_entry), 0.05);

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	chi_squared_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

        return 0;
}
