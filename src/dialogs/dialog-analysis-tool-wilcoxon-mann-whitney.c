/*
 * dialog-analysis-tool-wilcoxon-mann-whitney.c:
 *
 * Authors:
  *  Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2010 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
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
#include <tools/analysis-wilcoxon-mann-whitney.h>
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

#define WILCOXON_MANN_WHITNEY_KEY "analysistools-principal-components-dialog"

#if 0
static char const * const grouped_by_group[] = {
	"grouped_by_row",
	"grouped_by_col",
	"grouped_by_area",
	NULL
};
#endif

static void
wilcoxon_mann_whitney_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
						 GnmGenericToolState *state)
{
        GnmValue *input_range;
        GnmValue *input_range_2;
	gboolean input_1_ready  = FALSE;
	gboolean input_2_ready  = FALSE;

	/* Checking Input Ranges */
        input_range = gnm_expr_entry_parse_as_value (
		GNM_EXPR_ENTRY (state->input_entry), state->sheet);
	input_range_2 = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->input_entry_2), state->sheet);

	input_1_ready = (input_range != NULL);
	input_2_ready = ((state->input_entry_2 == NULL) || (input_range_2 != NULL));
        value_release (input_range);
        value_release (input_range_2);

	if (!input_1_ready) {
		gtk_label_set_text (GTK_LABEL (state->warning),
				    _("The input range for variable 1 is invalid."));
		gtk_widget_set_sensitive (state->ok_button, FALSE);
		return;
	} else if (!input_2_ready) {
		gtk_label_set_text (GTK_LABEL (state->warning),
				    _("The input range for variable 2 is invalid."));
		gtk_widget_set_sensitive (state->ok_button, FALSE);
		return;
	}

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

/**
 * wilcoxon_mann_whitney_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 **/
static void
wilcoxon_mann_whitney_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			GnmGenericToolState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_generic_b_t  *data;

 	GtkWidget *w;

	if (state->warning_dialog != NULL)
		gtk_widget_destroy (state->warning_dialog);

	data = g_new0 (analysis_tools_data_generic_b_t, 1);
	dao  = parse_output (state, NULL);

	data->wbc = GNM_WBC (state->wbcg);

	data->range_1 = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->input_entry), state->sheet);

	data->range_2 =  gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->input_entry_2), state->sheet);

	w = go_gtk_builder_get_widget (state->gui, "labels_button");
        data->labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	if (cmd_analysis_tool (GNM_WBC (state->wbcg), state->sheet,
			       dao, data,
			       analysis_tool_wilcoxon_mann_whitney_engine, TRUE)) {
		char   *text;
		text = g_strdup_printf (
			_("An unexpected error has occurred."));
		error_in_entry (state, GTK_WIDGET (state->input_entry), text);
		g_free (text);
	} else
		gtk_widget_destroy (state->dialog);
	return;
}



/**
 * dialog_wilcoxon_mann_whitney_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
int
dialog_wilcoxon_m_w_tool (WBCGtk *wbcg, Sheet *sheet)
{
        GnmGenericToolState *state;
	char const * plugins[] = { "Gnumeric_fnstat",
				   "Gnumeric_fnmath",
				   "Gnumeric_fnlookup",
				   NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, WILCOXON_MANN_WHITNEY_KEY))
		return 0;

	state = g_new0 (GnmGenericToolState, 1);

	if (dialog_tool_init (state, wbcg, sheet,
			      GNUMERIC_HELP_LINK_WILCOXON_MANN_WHITNEY,
			      "res:ui/wilcoxon-mann-whitney.ui", "WilcoxonMannWhitney",
			      _("Could not create the Wilcoxon-Mann-Whitney Analysis Tool dialog."),
			      WILCOXON_MANN_WHITNEY_KEY,
			      G_CALLBACK (wilcoxon_mann_whitney_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (wilcoxon_mann_whitney_tool_update_sensitivity_cb),
			      GNM_EE_SINGLE_RANGE))
		return 0;

	gnm_dao_set_put (GNM_DAO (state->gdao), TRUE, TRUE);
	wilcoxon_mann_whitney_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

        return 0;
}
