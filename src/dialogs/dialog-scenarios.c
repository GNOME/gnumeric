/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * dialog-scenarios.c:  Create, edit, and view scenarios.
 *
 * Author:
 *        Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 2003 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
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
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <commands.h>
#include <workbook-control.h>
#include <workbook.h>
#include <workbook-edit.h>
#include <sheet.h>
#include <dao-gui-utils.h>
#include "scenarios.h"
#include <glade/glade.h>

typedef GenericToolState ScenariosState;


/********* Scenario Add UI **********************************************/

static gboolean
scenario_name_used (GList *scenarios, gchar *name)
{
	scenario_t *s;

	while (scenarios != NULL) {
		s = (scenario_t *) scenarios->data;

		if (strcmp (s->name, name) == 0)
			return TRUE;
		scenarios = scenarios->next;
	}
	return FALSE;
}

/*
 * A scenario name should have at least one printable character.
 */
static gboolean
check_name (gchar *n)
{
	while (isspace (*n))
		n++;
	if (*n)
		return FALSE;
	else
		return TRUE;
}

/**
 * scenarios_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Run the scenario manager tool.
 **/
static void
scenario_add_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			    ScenariosState *state)
{
	data_analysis_output_t  dao;
	gchar                   *name;
	gchar                   *comment;
	Value                   *cell_range;
	GtkWidget               *entry, *comment_view;
	GtkTextBuffer           *buf;
	GtkTextIter             start, end;
	char const              *tmp;

	cell_range = gnm_expr_entry_parse_as_value
		(GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

	if (cell_range == NULL) {
		gnumeric_notice (state->wbcg, GTK_MESSAGE_ERROR, 
				 _("Invalid changing cells"));
		return;
	}
	entry = glade_xml_get_widget (state->gui, "name_entry");

	name = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
	if (scenario_name_used (state->sheet->scenarios, name)) {
	        g_free (name);
		gnumeric_notice (state->wbcg, GTK_MESSAGE_ERROR, 
				 _("Scenario name already used"));
		return;
	} else if (check_name (name)) {
	        g_free (name);
		gnumeric_notice (state->wbcg, GTK_MESSAGE_ERROR, 
				 _("Invalid scenario name"));
		return;
	}

	comment_view = glade_xml_get_widget (state->gui, "comment_view");
	buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (comment_view));
	gtk_text_buffer_get_start_iter (buf, &start);
	gtk_text_buffer_get_end_iter (buf, &end);
	comment = g_strdup (gtk_text_buffer_get_text (buf, &start, &end,
						      FALSE));

	dao_init (&dao, NewSheetOutput);
	dao.sheet = state->sheet;

	tmp = gnm_expr_entry_get_text
		(GNUMERIC_EXPR_ENTRY (state->input_entry));

	scenario_add_new (WORKBOOK_CONTROL (state->wbcg), name,
			  cell_range, tmp, comment, &dao);

	value_release (cell_range);
	gtk_widget_destroy (state->dialog);
	return;
}

/**
 * scenario_add_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity
 **/
static void
scenario_add_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				    ScenariosState *state)
{
	gtk_widget_set_sensitive (state->ok_button, TRUE);
}

void
dialog_scenario_add (WorkbookControlGUI *wbcg)
{
        GenericToolState *state;
	WorkbookControl *wbc;
	char const *error_str = _("Could not create the Scenario Add dialog.");

	if (wbcg == NULL)
		return;

	wbc = WORKBOOK_CONTROL (wbcg);

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, "ScenarioAdd"))
		return;

	state = g_new (GenericToolState, 1);

	if (dialog_tool_init (state, wbcg, wb_control_cur_sheet (wbc),
			      "scenario-add.html",
			      "scenario-add.glade", "ScenarioAdd",
			      _("_Changing cells:"), NULL, error_str,
			      "ScenarioAdd",
			      G_CALLBACK (scenario_add_ok_clicked_cb), NULL,
			      G_CALLBACK (scenario_add_update_sensitivity_cb),
			      0))
		return;

	state->name_entry = glade_xml_get_widget (state->gui, "name_entry");
	if (state->name_entry == NULL)
	        return;
	
	state->output_entry = NULL;
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->name_entry));
	scenario_add_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);
}


/********* Scenario Manager UI ******************************************/

static void
update_comment (ScenariosState *state, gchar *cells, gchar *comment)
{
	GtkWidget     *w;
	GtkTextBuffer *buf;

	/* Update changing cells box */
	w = glade_xml_get_widget (state->gui, "changing_cells_entry");
	gtk_entry_set_text (GTK_ENTRY (w), cells);

	/* Update comment text view */
	w = glade_xml_get_widget (state->gui, "comment_view");
	buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (w));
 
	gtk_text_buffer_set_text (buf, comment, strlen (comment));
}

gboolean
find_scenario_strs (GList *scenarios, gchar *name, 
		    gchar **cells, gchar **comment)
{
	static gchar *buf1 = NULL, *buf2 = NULL;

	if (buf1)
		g_free (buf1);
	if (buf2)
		g_free (buf2);

	while (scenarios) {
		scenario_t *scenario = (scenario_t *) scenarios->data;

		if (strcmp (scenario->name, name) == 0) {
			buf1 = g_strdup (scenario->cell_sel_str);
			buf2 = g_strdup (scenario->comment);
			*cells   = buf1;
			*comment = buf2;
			return FALSE;
		}
		scenarios = scenarios->next;
	}
	return TRUE;
}

static void
set_selection_state (ScenariosState *state, gboolean f)
{
	/* Set the sensitivies to FALSE since no selections have been made */
	gtk_widget_set_sensitive (state->scenario_buttons->show_button, f);
	gtk_widget_set_sensitive (state->scenario_buttons->delete_button, f);
	gtk_widget_set_sensitive (state->scenario_buttons->summary_button, f);

	if (f) {
		GtkTreeSelection        *selection;
		GtkTreeIter             iter;
		GtkTreeModel            *model;
		gchar                   *name;
		gchar                   *comment;
		gchar                   *cells;

		selection = gtk_tree_view_get_selection
			(GTK_TREE_VIEW (state->scenarios_treeview));
		if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;
		model = gtk_tree_view_get_model 
			(GTK_TREE_VIEW (state->scenarios_treeview));
	
		gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
				    0, &name, -1);
	
		find_scenario_strs (state->sheet->scenarios, name,
				    &cells, &comment);
		update_comment (state, cells, comment);
	} else
		update_comment (state, "", "");
}

static void
update_scenarios_treeview (GtkWidget *view, GList *scenarios)
{
          GList        *current;
	  GtkTreeIter  iter;
	  GtkListStore *store;
	  GtkTreePath  *path;

	  store = gtk_list_store_new (1, G_TYPE_STRING);

	  while (scenarios != NULL) {
	          scenario_t *s = (scenario_t *) scenarios->data;

	          gtk_list_store_append (store, &iter);
	          gtk_list_store_set (store, &iter, 0, s->name, -1);
	          scenarios = scenarios->next;
	  }
	  path = gtk_tree_path_new_from_string ("0");
	  gtk_tree_model_get_iter (GTK_TREE_MODEL (store),
				   &iter,
				   path);
	  gtk_tree_path_free (path);

	  gtk_tree_view_set_model (GTK_TREE_VIEW (view),
				   GTK_TREE_MODEL (store));
	  gtk_tree_view_append_column
	          (GTK_TREE_VIEW (view),
		   gtk_tree_view_column_new_with_attributes 
		   (_("Name"),
		    gtk_cell_renderer_text_new (), "text", 0, NULL));
}

/**
 * scenarios_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity
 **/
static void
scenarios_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				 ScenariosState *state)
{
	gtk_widget_set_sensitive (state->ok_button, TRUE);
}


/**
 * scenarios_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Run the scenario manager tool.
 **/
static void
scenarios_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			 ScenariosState *state)
{
	data_analysis_output_t  dao;

	dao_init (&dao, NewSheetOutput);
	dao.sheet = state->sheet;

	scenarios_ok (WORKBOOK_CONTROL (state->wbcg), &dao);

	gtk_widget_destroy (state->dialog);
	return;
}

static void
scenarios_show_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			   ScenariosState *state)
{
	data_analysis_output_t  dao;
	WorkbookControl         *wbc;
	GtkTreeSelection        *selection;
	GtkTreeIter             iter;
	GtkTreeModel            *model;
	gchar                   *value;

	selection = gtk_tree_view_get_selection
	        (GTK_TREE_VIEW (state->scenarios_treeview));
	dao_init (&dao, NewSheetOutput);
	dao.sheet = state->sheet;
	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;
	model = gtk_tree_view_get_model 
	        (GTK_TREE_VIEW (state->scenarios_treeview));
	
	gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
			    0, &value,
			    -1);
	
	wbc = WORKBOOK_CONTROL (state->wbcg);
	scenario_show (wbc, value, &dao);
}

static void
scenarios_delete_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			     ScenariosState *state)
{
	data_analysis_output_t  dao;
	GtkTreeSelection        *selection;
	GtkTreeIter             iter;
	GtkTreeModel            *model;
	gchar                   *value;

	selection = gtk_tree_view_get_selection
	        (GTK_TREE_VIEW (state->scenarios_treeview));
	dao_init (&dao, NewSheetOutput);
	dao.sheet = state->sheet;
	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;
	model = gtk_tree_view_get_model 
	        (GTK_TREE_VIEW (state->scenarios_treeview));
	
	gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
			    0, &value,
			    -1);
	
	gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
	scenario_delete (WORKBOOK_CONTROL (state->wbcg), value, &dao);
	set_selection_state (state, FALSE);
}

static void
scenarios_summary_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			      ScenariosState *state)
{
	data_analysis_output_t  dao;

	dao_init (&dao, NewSheetOutput);
	dao.sheet = state->sheet;

	scenario_summary (WORKBOOK_CONTROL (state->wbcg), &dao);
}

static void
cb_selection_changed (G_GNUC_UNUSED GtkTreeSelection *selection,
		      ScenariosState *state)
{
	set_selection_state (state, TRUE);
}

static gboolean
init_scenario_buttons (ScenariosState *state)
{
	GtkWidget *w;

	state->scenario_buttons = g_new (scenario_buttons_t, 1);

	/* Show button */
	state->scenario_buttons->show_button =
	        glade_xml_get_widget (state->gui, "show_button");
	if (state->scenario_buttons->show_button == NULL)
	        return TRUE;
	g_signal_connect (G_OBJECT (state->scenario_buttons->show_button),
			  "clicked",
			  G_CALLBACK (scenarios_show_clicked_cb), state);

	/* Delete button */
	state->scenario_buttons->delete_button =
	        glade_xml_get_widget (state->gui, "delete_button");
	if (state->scenario_buttons->delete_button == NULL)
	        return TRUE;
	g_signal_connect (G_OBJECT (state->scenario_buttons->delete_button),
			  "clicked",
			  G_CALLBACK (scenarios_delete_clicked_cb), state);

	/* Summary button */
	state->scenario_buttons->summary_button =
	        glade_xml_get_widget (state->gui, "summary_button");
	if (state->scenario_buttons->summary_button == NULL)
	        return TRUE;
	g_signal_connect (G_OBJECT (state->scenario_buttons->summary_button),
			  "clicked",
			  G_CALLBACK (scenarios_summary_clicked_cb), state);

	set_selection_state (state, FALSE);

	return FALSE;
}

void
dialog_scenarios (WorkbookControlGUI *wbcg)
{
        ScenariosState   *state;
	WorkbookControl  *wbc;
	Sheet            *sheet;
	GtkWidget        *w;
	GtkTreeSelection *select;
	char const *error_str = _("Could not create the Scenarios dialog.");

	g_return_if_fail (wbcg != NULL);

	wbc = WORKBOOK_CONTROL (wbcg);
	sheet = wb_control_cur_sheet (wbc);

	state = g_new (ScenariosState, 1);

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, "Scenarios")) {
	        update_scenarios_treeview (state->scenarios_treeview,
					   sheet->scenarios);
		return;
	}

	if (dialog_tool_init (state, wbcg, sheet,
			      "scenarios.html",
			      "scenario-manager.glade", "Scenarios",
			      NULL, NULL, error_str, "Scenarios",
			      G_CALLBACK (scenarios_ok_clicked_cb), NULL,
			      G_CALLBACK (scenarios_update_sensitivity_cb),
			      0))
		goto error_out;

	if (init_scenario_buttons (state))
		goto error_out;

	state->scenarios_treeview = glade_xml_get_widget
	        (state->gui, "scenarios_treeview");
	if (state->scenarios_treeview == NULL)
	        goto error_out;

	/* Set sensitivity of comment and changing cells widgets. */
	w = glade_xml_get_widget (state->gui, "changing_cells_label");
	if (w == NULL)
	        goto error_out;
	gtk_widget_set_sensitive (w, FALSE);
	w = glade_xml_get_widget (state->gui, "changing_cells_entry");
	if (w == NULL)
	        goto error_out;
	gtk_widget_set_sensitive (w, FALSE);
	w = glade_xml_get_widget (state->gui, "comment_view");
	if (w == NULL)
	        goto error_out;
	gtk_widget_set_sensitive (w, FALSE);
	w = glade_xml_get_widget (state->gui, "comment_label");
	if (w == NULL)
	        goto error_out;
	gtk_widget_set_sensitive (w, FALSE);

	update_scenarios_treeview (state->scenarios_treeview,
				   sheet->scenarios);
	select = gtk_tree_view_get_selection
		(GTK_TREE_VIEW (state->scenarios_treeview));
	g_signal_connect (select, "changed",
			  G_CALLBACK (cb_selection_changed), state);

	scenarios_update_sensitivity_cb (NULL, state);
	gtk_widget_show (state->dialog);

        return;

 error_out:
	gnumeric_notice (wbcg, GTK_MESSAGE_ERROR, error_str);
	g_free (state);

	return;
}
