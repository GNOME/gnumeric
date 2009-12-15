/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-analysis-tool-sign-test.c:
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "analysis-sign-test.h"
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
#include <gtk/gtk.h>

#define SIGN_TEST_KEY_ONE      "analysistools-sign-test-one-dialog"
#define SIGN_TEST_KEY_TWO      "analysistools-sign-test-two-dialog"

static char const * const grouped_by_group[] = {
	"grouped_by_row",
	"grouped_by_col",
	"grouped_by_area",
	NULL
};

typedef struct {
	GenericToolState base;
	GtkWidget *alpha_entry;
	GtkWidget *median_entry;
} SignTestToolState;

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
	gnm_float alpha;
	gnm_float median;
        GSList *input_range;
	gboolean err;

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

	/* Checking second input range*/
	if (state->base.input_entry_2 != NULL) {
		input_range = gnm_expr_entry_parse_as_list
			(GNM_EXPR_ENTRY (state->base.input_entry_2),
			 state->base.sheet);
		if (input_range == NULL) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The second input range is invalid."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			return;
		} else
			range_list_destroy (input_range);
	}

	/* Checking Median*/
	err = entry_to_float
		(GTK_ENTRY (state->median_entry), &median, FALSE);
	if (err) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The predicted median should be a number."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
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
	gboolean err;

	data = g_new0 (analysis_tools_data_sign_test_t, 1);
	dao  = parse_output ((GenericToolState *)state, NULL);

	data->base.input = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	data->base.group_by = gnumeric_glade_group_value (state->base.gui, grouped_by_group);
	
	w = glade_xml_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active 
		(GTK_TOGGLE_BUTTON (w));
	
	err = entry_to_float
		(GTK_ENTRY (state->median_entry), &data->median, FALSE);
	data->alpha = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->alpha_entry));

	if (!cmd_analysis_tool (WORKBOOK_CONTROL (state->base.wbcg), state->base.sheet,
				dao, data, analysis_tool_sign_test_engine))
		gtk_widget_destroy (state->base.dialog);

	return;
}

static void
sign_test_two_tool_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			      SignTestToolState *state)
{
	data_analysis_output_t  *dao;
	GtkWidget *w;
	analysis_tools_data_sign_test_two_t *data;
	gboolean err;

	data = g_new0 (analysis_tools_data_sign_test_two_t, 1);
	dao  = parse_output ((GenericToolState *)state, NULL);

	data->base.range_1 = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);

	data->base.range_2 = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry_2), state->base.sheet);

	w = glade_xml_get_widget (state->base.gui, "labels_button");
        data->base.labels = gtk_toggle_button_get_active 
		(GTK_TOGGLE_BUTTON (w));
	
	err = entry_to_float
		(GTK_ENTRY (state->median_entry), &data->median, FALSE);

	data->base.alpha = gtk_spin_button_get_value
		(GTK_SPIN_BUTTON (state->alpha_entry));

	if (!cmd_analysis_tool (WORKBOOK_CONTROL (state->base.wbcg), state->base.sheet,
				dao, data, analysis_tool_sign_test_two_engine))
		gtk_widget_destroy (state->base.dialog);

	return;
}

/**
 * dialog_sign_test_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
int
dialog_sign_test_tool (WBCGtk *wbcg, Sheet *sheet, signtest_type n_median)
{
	char const *key, *glade;
	char const * plugins[] = { "Gnumeric_fnstat",
				   "Gnumeric_fnlogical",
				   "Gnumeric_fnmath",
				   "Gnumeric_fninfo",
				   NULL};
        SignTestToolState *state;
	GnmExprEntryFlags flags = 0;
	GCallback cb;

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;


	switch (n_median) {
	case SIGNTEST_2:
		key = SIGN_TEST_KEY_TWO;		
		glade = "sign-test-two.glade";
		flags = GNM_EE_SINGLE_RANGE;
		cb = G_CALLBACK (sign_test_two_tool_ok_clicked_cb);
		break;
	case SIGNTEST_1:
	default:
		key = SIGN_TEST_KEY_ONE;
		glade = "sign-test.glade";
		cb = G_CALLBACK (sign_test_tool_ok_clicked_cb);
		break;
	}
	
	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, key))
		return 0;

	state = g_new0 (SignTestToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_SIGN_TEST,
			      glade, "Sign-Test",
			      _("Could not create the Sign Test Tool dialog."),
			      key, cb, NULL,
			      G_CALLBACK (sign_test_tool_update_sensitivity_cb),
			      flags))
		return 0;

	
	state->alpha_entry = glade_xml_get_widget (state->base.gui,
						   "alpha-entry");
	float_to_entry (GTK_ENTRY (state->alpha_entry), 0.05);
	g_signal_connect_after (G_OBJECT (state->alpha_entry),
		"changed",
		G_CALLBACK (sign_test_tool_update_sensitivity_cb), state);
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->alpha_entry));

	state->median_entry = glade_xml_get_widget (state->base.gui,
						    "median-entry");
	int_to_entry (GTK_ENTRY (state->median_entry), 0);
	g_signal_connect_after (G_OBJECT (state->median_entry),
		"changed",
		G_CALLBACK (sign_test_tool_update_sensitivity_cb), state);
	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->median_entry));

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	sign_test_tool_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);

	return 0;
}
