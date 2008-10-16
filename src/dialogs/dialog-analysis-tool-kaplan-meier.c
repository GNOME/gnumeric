/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-analysis-tool-kaplan-meier.c:
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "analysis-kaplan-meier.h"
#include "analysis-tools.h"

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

#define KAPLAN_MEIER_KEY      "analysistools-kaplan-meier-dialog"

typedef struct {
	GenericToolState base;
	GtkWidget *censorship_button;
	GtkWidget *censorship_button_zero;
	GtkWidget *censorship_button_one;
	GtkWidget *graph_button;
	GtkWidget *tick_button;
	GtkWidget *std_error_button;
} KaplanMeierToolState;

static char const * const censor_mark_group[] = {
	"censor-button-0",
	"censor-button-1",
	NULL
};

/**
 * kaplan_meier_tool_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity
 **/
static void
kaplan_meier_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				      KaplanMeierToolState *state)
{
	gboolean censorship;
        GnmValue *input_range;
        GnmValue *input_range_2 = NULL;
	int height, width;
	

	input_range = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	if (input_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The time column is not valid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	height = input_range->v_range.cell.b.row - input_range->v_range.cell.a.row;
	width  = input_range->v_range.cell.b.col - input_range->v_range.cell.a.col;
	
	value_release (input_range);

	if (width != 0) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The time column should be part of a single column."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	censorship = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->censorship_button));
	if (censorship) {
		input_range_2 =  gnm_expr_entry_parse_as_value
			(GNM_EXPR_ENTRY (state->base.input_entry_2), state->base.sheet);
		if (input_range_2 == NULL) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The censorship column is not valid."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			return;
		}

		if (input_range_2->v_range.cell.b.col != input_range_2->v_range.cell.a.col) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The censorship column should be part of a single column."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			value_release (input_range_2);
			return;
		} 
		if (input_range_2->v_range.cell.b.row - input_range_2->v_range.cell.a.row != height) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The censorship and time columns should have the same height."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			value_release (input_range_2);
			return;
		}

		value_release (input_range_2);
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
 * kaplan_meier_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the kaplan_meier_tool.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
kaplan_meier_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			      KaplanMeierToolState *state)
{
	data_analysis_output_t  *dao;
	analysis_tools_data_kaplan_meier_t  *data;

	data = g_new0 (analysis_tools_data_kaplan_meier_t, 1);
	dao  = parse_output ((GenericToolState *)state, NULL);


	data->base.wbc = WORKBOOK_CONTROL (state->base.wbcg);

	if (state->base.warning_dialog != NULL)
		gtk_widget_destroy (state->base.warning_dialog);

	data->base.range_1 = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	
	data->censored = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->censorship_button));

	if (data->censored)
		data->base.range_2 =  gnm_expr_entry_parse_as_value
			(GNM_EXPR_ENTRY (state->base.input_entry_2), state->base.sheet);
	else
		data->base.range_2 = NULL;

	data->censor_mark = gnumeric_glade_group_value (state->base.gui, 
							censor_mark_group);
	data->chart = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->graph_button));
	data->ticks = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->tick_button));
	data->std_err = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->std_error_button));

	if (!cmd_analysis_tool (WORKBOOK_CONTROL (state->base.wbcg), state->base.sheet,
				dao, data, analysis_tool_kaplan_meier_engine))
		gtk_widget_destroy (state->base.dialog);
	
	return;
}

/**
 * kaplan_meier_tool_set_graph:
 * @widget:
 * @focus_widget:
 * @state:
 *
 *
 **/
static gboolean
kaplan_meier_tool_set_graph_cb (G_GNUC_UNUSED GtkWidget *dummy,
				KaplanMeierToolState *state)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->graph_button), TRUE);
	kaplan_meier_tool_update_sensitivity_cb (NULL, state);
	return FALSE;
}

/**
 * kaplan_meier_tool_set_censorship:
 * @widget:
 * @event:
 * @state:
 *
 **/
static gboolean
kaplan_meier_tool_set_censorship_cb (G_GNUC_UNUSED GtkWidget *widget,
				     G_GNUC_UNUSED GdkEventFocus *event,
				     KaplanMeierToolState *state)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->censorship_button), TRUE);
	return FALSE;
}
static gboolean
kaplan_meier_tool_set_censor_cb (G_GNUC_UNUSED GtkWidget *dummy,
				KaplanMeierToolState *state)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->censorship_button), TRUE);
	
	return FALSE;
}

/**
 * dialog_kaplan_meier_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
int
dialog_kaplan_meier_tool (WBCGtk *wbcg, Sheet *sheet)
{
        KaplanMeierToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, KAPLAN_MEIER_KEY))
		return 0;

	state = g_new0 (KaplanMeierToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_KAPLAN_MEIER,
			      "kaplan-meier.glade", "KaplanMeier",
			      _("Could not create the Kaplan Meier Tool dialog."),
			      KAPLAN_MEIER_KEY,
			      G_CALLBACK (kaplan_meier_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (kaplan_meier_tool_update_sensitivity_cb),
			      0))
		return 0;

	

	state->censorship_button = GTK_WIDGET (glade_xml_get_widget
						  (state->base.gui,
						   "censor-button"));
	state->censorship_button_zero = GTK_WIDGET (glade_xml_get_widget
						    (state->base.gui,
						     "censor-button-0"));
	state->censorship_button_one = GTK_WIDGET (glade_xml_get_widget
						   (state->base.gui,
						    "censor-button-1"));
	state->graph_button = GTK_WIDGET (glade_xml_get_widget
						  (state->base.gui,
						   "graph-button"));
	state->tick_button = GTK_WIDGET (glade_xml_get_widget
						  (state->base.gui,
						   "tick-button"));
	state->std_error_button = GTK_WIDGET (glade_xml_get_widget
						  (state->base.gui,
						   "std-error-button"));
	g_signal_connect_after (G_OBJECT (state->censorship_button),
		"toggled",
		G_CALLBACK (kaplan_meier_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->graph_button),
		"toggled",
		G_CALLBACK (kaplan_meier_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->std_error_button),
		"toggled",
		G_CALLBACK (kaplan_meier_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->tick_button),
		"toggled",
		G_CALLBACK (kaplan_meier_tool_set_graph_cb), state);
	g_signal_connect_after (G_OBJECT (state->censorship_button_zero),
		"toggled",
		G_CALLBACK (kaplan_meier_tool_set_censor_cb), state);
	g_signal_connect_after (G_OBJECT (state->censorship_button_one),
		"toggled",
		G_CALLBACK (kaplan_meier_tool_set_censor_cb), state);
	g_signal_connect (G_OBJECT
			  (gnm_expr_entry_get_entry (
				  GNM_EXPR_ENTRY (state->base.input_entry_2))),
		"focus-in-event",
		G_CALLBACK (kaplan_meier_tool_set_censorship_cb), state);

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	kaplan_meier_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);

        return 0;
}
