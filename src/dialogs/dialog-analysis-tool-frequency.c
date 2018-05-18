/*
 * dialog-analysis-tool-frequency.c:
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
#include <tools/analysis-frequency.h>
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

#define FREQUENCY_KEY      "analysistools-frequency-dialog"

static char const * const grouped_by_group[] = {
	"grouped_by_row",
	"grouped_by_col",
	NULL
};

static char const * const chart_group[] = {
	"nochart-button",
	"barchart-button",
	"columnchart-button",
	NULL
};

typedef struct {
	GnmGenericToolState base;
	GtkWidget *predetermined_button;
	GtkWidget *calculated_button;
	GtkEntry  *n_entry;
} FrequencyToolState;


/**
 * frequency_tool_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity
 **/
static void
frequency_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				      FrequencyToolState *state)
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
					    _("The categories range is not valid."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			return;
		}
		value_release (input_range_2);
	} else if (entry_to_int(state->n_entry, &the_n,FALSE) != 0 || the_n <= 0) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The number of categories is invalid."));
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
 * frequency_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the frequency_tool.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
frequency_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			      FrequencyToolState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_frequency_t  *data;

	GtkWidget *w;

	data = g_new0 (analysis_tools_data_frequency_t, 1);
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
		data->bin = NULL;
	}

	data->chart = gnm_gui_group_value (state->base.gui, chart_group);

	w = go_gtk_builder_get_widget (state->base.gui, "labels_button");
	data->base.labels = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	w = go_gtk_builder_get_widget (state->base.gui, "percentage-button");
	data->percentage = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
	w = go_gtk_builder_get_widget (state->base.gui, "exact-button");
	data->exact = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));

	if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg),
				state->base.sheet,
				dao, data, analysis_tool_frequency_engine,
				TRUE))
		gtk_widget_destroy (state->base.dialog);

	return;
}

/**
 * frequency_tool_set_predetermined:
 * @widget:
 * @focus_widget:
 * @state:
 *
 * Output range entry was focused. Switch to output range.
 *
 **/
static gboolean
frequency_tool_set_predetermined (G_GNUC_UNUSED GtkWidget *widget,
				  G_GNUC_UNUSED GdkEventFocus *event,
				  FrequencyToolState *state)
{
	    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->predetermined_button), TRUE);
	    return FALSE;
}

/**
 * frequency_tool_set_calculated:
 * @widget:
 * @event:
 * @state:
 *
 **/
static gboolean
frequency_tool_set_calculated (G_GNUC_UNUSED GtkWidget *widget,
			       G_GNUC_UNUSED GdkEventFocus *event,
			       FrequencyToolState *state)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->calculated_button), TRUE);
	return FALSE;
}

/**
 * dialog_frequency_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
int
dialog_frequency_tool (WBCGtk *wbcg, Sheet *sheet)
{
        FrequencyToolState *state;
	char const * plugins[] = { "Gnumeric_fnlookup",
				   "Gnumeric_fninfo",
				   "Gnumeric_fnstring",
				   "Gnumeric_fnlogical",
				   NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, FREQUENCY_KEY))
		return 0;

	state = g_new0 (FrequencyToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_FREQUENCY,
			      "res:ui/frequency.ui", "Frequency",
			      _("Could not create the Frequency Tool dialog."),
			      FREQUENCY_KEY,
			      G_CALLBACK (frequency_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (frequency_tool_update_sensitivity_cb),
			      0))
	{
		g_free(state);
		return 0;
	}

	state->predetermined_button = tool_setup_update
		(&state->base, "pre_determined_button",
		 G_CALLBACK (frequency_tool_update_sensitivity_cb),
		 state);

	state->calculated_button  = tool_setup_update
		(&state->base, "calculated_button",
		 G_CALLBACK (frequency_tool_update_sensitivity_cb),
		 state);

	state->n_entry =
		GTK_ENTRY(tool_setup_update
			  (&state->base, "n_entry",
			   G_CALLBACK (frequency_tool_update_sensitivity_cb),
			   state));
	g_signal_connect (G_OBJECT (state->n_entry),
		"key-press-event",
		G_CALLBACK (frequency_tool_set_calculated), state);
	g_signal_connect (G_OBJECT
			  (gnm_expr_entry_get_entry (
				  GNM_EXPR_ENTRY (state->base.input_entry_2))),
		"focus-in-event",
		G_CALLBACK (frequency_tool_set_predetermined), state);

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	frequency_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

	gtk_widget_set_sensitive (GTK_WIDGET (state->n_entry), FALSE);
	gtk_widget_set_sensitive (state->calculated_button, FALSE);

        return 0;
}
