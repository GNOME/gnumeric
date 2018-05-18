/*
 * dialog-advanced-filter.c:
 *
 * Authors:
 *        Jukka-Pekka Iivonen <iivonen@iki.fi>
 *        Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 * (C) Copyright 2002 by Andreas J. Guelzow <aguelzow@taliesin.ca>
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
 **/
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <sheet.h>
#include <cell.h>
#include <ranges.h>
#include <gui-util.h>
#include <dialogs/tool-dialogs.h>
#include <dialogs/dao-gui-utils.h>
#include <value.h>
#include <wbc-gtk.h>

#include <widgets/gnm-expr-entry.h>
#include <widgets/gnm-dao.h>
#include <tools/filter.h>
#include <tools/analysis-tools.h>
#include <commands.h>


#define ADVANCED_FILTER_KEY         "advanced-filter-dialog"

typedef GnmGenericToolState AdvancedFilterState;

/**
 * advanced_filter_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity
 **/
static void
advanced_filter_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				       AdvancedFilterState *state)
{
        GnmValue *input_range    = NULL;
        GnmValue *criteria_range = NULL;

        input_range = gnm_expr_entry_parse_as_value (
		GNM_EXPR_ENTRY (state->input_entry), state->sheet);
	if (input_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->warning),
				    _("The list range is invalid."));
		gtk_widget_set_sensitive (state->ok_button, FALSE);
		return;
	} else
		value_release (input_range);

	criteria_range =  gnm_expr_entry_parse_as_value
		(state->input_entry_2, state->sheet);
	if (criteria_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->warning),
				    _("The criteria range is invalid."));
		gtk_widget_set_sensitive (state->ok_button, FALSE);
		return;
	} else
		value_release (criteria_range);

	if (!gnm_dao_is_ready (GNM_DAO (state->gdao))) {
		gtk_label_set_text (GTK_LABEL (state->warning),
				    _("The output range is invalid."));
		gtk_widget_set_sensitive (state->ok_button, FALSE);
		return;
	}

	gtk_label_set_text (GTK_LABEL (state->warning), "");
	gtk_widget_set_sensitive (state->ok_button, TRUE);
	return;
}

/**
 * advanced_filter_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the advanced_filter.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
advanced_filter_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			       AdvancedFilterState *state)
{
	data_analysis_output_t  *dao;
	GnmValue                   *input;
	GnmValue                   *criteria;
	char                    *text;
	GtkWidget               *w;
	int                     err = 0;
	gboolean                unique;

	input = gnm_expr_entry_parse_as_value (
		GNM_EXPR_ENTRY (state->input_entry), state->sheet);

	criteria = gnm_expr_entry_parse_as_value
		(state->input_entry_2, state->sheet);

        dao  = parse_output ((GnmGenericToolState *) state, NULL);

	w = go_gtk_builder_get_widget (state->gui, "unique-button");
	unique = (1 == gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)));

	if (dao->type == InPlaceOutput)
		err = advanced_filter (GNM_WBC (state->wbcg),
				       dao, input, criteria, unique);
	else {
		analysis_tools_data_advanced_filter_t  *
			data = g_new0 (analysis_tools_data_advanced_filter_t, 1);
		data->base.wbc = GNM_WBC (state->wbcg);
		data->base.range_1 = input;
		data->base.range_2 = criteria;
		data->unique_only_flag = unique;

		if (cmd_analysis_tool (GNM_WBC (state->wbcg), state->sheet,
				       dao, data, analysis_tool_advanced_filter_engine, FALSE)) {
			err = data->base.err;
			g_free (data);
		} else
			err = analysis_tools_noerr;

	}

	if (dao->type == InPlaceOutput || err != analysis_tools_noerr) {
		value_release (input);
		value_release (criteria);
		g_free (dao);
	}

	switch (err) {
	case analysis_tools_noerr:
		gtk_widget_destroy (state->dialog);
		break;
	case analysis_tools_invalid_field:
		error_in_entry ((GnmGenericToolState *) state,
				GTK_WIDGET (state->input_entry_2),
				_("The given criteria are invalid."));
		break;
	case analysis_tools_no_records_found:
		go_gtk_notice_nonmodal_dialog ((GtkWindow *) state->dialog,
					  &(state->warning_dialog),
					  GTK_MESSAGE_INFO,
					  _("No matching records were found."));
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: "
					  "%d."), err);
		error_in_entry ((GnmGenericToolState *) state,
				GTK_WIDGET (state->input_entry), text);
		g_free (text);
		break;
	}
	return;
}

/**
 * dialog_advanced_filter:
 * @wbcg:
 *
 * Show the dialog (guru).
 **/
void
dialog_advanced_filter (WBCGtk *wbcg)
{
        AdvancedFilterState *state;
	WorkbookControl *wbc;

	g_return_if_fail (wbcg != NULL);

	wbc = GNM_WBC (wbcg);

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, ADVANCED_FILTER_KEY))
		return;

	state = g_new (AdvancedFilterState, 1);

	if (dialog_tool_init (state, wbcg, wb_control_cur_sheet (wbc),
			      GNUMERIC_HELP_LINK_ADVANCED_FILTER,
			      "res:ui/advanced-filter.ui", "Filter",
			      _("Could not create the Advanced Filter dialog."),
			      ADVANCED_FILTER_KEY,
			      G_CALLBACK (advanced_filter_ok_clicked_cb), NULL,
			      G_CALLBACK (advanced_filter_update_sensitivity_cb),
			      0))
		return;

	gnm_dao_set_inplace (GNM_DAO (state->gdao), _("Filter _in-place"));
	gnm_dao_set_put (GNM_DAO (state->gdao), FALSE, FALSE);
	advanced_filter_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

        return;
}
