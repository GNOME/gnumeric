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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <gui-util.h>
#include <commands.h>
#include <workbook-control.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <sheet.h>
#include <dao-gui-utils.h>
#include <position.h>
#include <value.h>
#include <dao.h>
#include "scenarios.h"

#include <glade/glade.h>
#include <gtk/gtk.h>
#include <goffice/utils/go-glib-extras.h>
#include <string.h>

typedef struct {
	GenericToolState base;

	scenario_state_t *scenario_state;
        GtkWidget *name_entry;
} ScenariosState;

struct _scenario_state {
	GtkWidget  *show_button;
	GtkWidget  *delete_button;
	GtkWidget  *summary_button;

        GtkWidget  *scenarios_treeview;
	GSList     *new_report_sheets;

	scenario_t *old_values;
	scenario_t *current;
};


/********* Scenario Add UI **********************************************/

static gboolean
scenario_name_used (const GList *scenarios, const gchar *name)
{
	while (scenarios != NULL) {
		const scenario_t *s = scenarios->data;

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
check_name (const gchar *n)
{
	while (*n && g_unichar_isspace (g_utf8_get_char (n)))
		n = g_utf8_next_char (n);

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
	WorkbookControl         *wbc;
	gchar                   *name;
	gchar                   *comment;
	GnmValue                   *cell_range;
	GtkWidget               *entry, *comment_view;
	GtkTextBuffer           *buf;
	GtkTextIter             start, end;
	GnmRangeRef const      *rr = NULL;
	gboolean                res;
	scenario_t              *scenario;

	cell_range = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);

	if (cell_range != NULL)
		rr = value_get_rangeref (cell_range);

	if (rr == NULL) {
		go_gtk_notice_dialog (GTK_WINDOW (state->base.dialog),
				 GTK_MESSAGE_ERROR,
				 _("Invalid changing cells"));
		gnm_expr_entry_grab_focus (state->base.input_entry, TRUE);
		return;
	}

	if (rr->a.sheet != state->base.sheet) {
		go_gtk_notice_dialog (GTK_WINDOW (state->base.dialog),
				 GTK_MESSAGE_ERROR,
				 _("Changing cells should be on the current "
				   "sheet only."));
		gnm_expr_entry_grab_focus (state->base.input_entry, TRUE);
		goto out;
	}
	entry = glade_xml_get_widget (state->base.gui, "name_entry");

	name = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
	if (scenario_name_used (state->base.sheet->scenarios, name)) {
	        g_free (name);
		go_gtk_notice_dialog (GTK_WINDOW (state->base.dialog),
				 GTK_MESSAGE_ERROR,
				 _("Scenario name already used"));
		goto out;
	} else if (check_name (name)) {
	        g_free (name);
		go_gtk_notice_dialog (GTK_WINDOW (state->base.dialog),
				 GTK_MESSAGE_ERROR,
				 _("Invalid scenario name"));
		goto out;
	}

	comment_view = glade_xml_get_widget (state->base.gui, "comment_view");
	buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (comment_view));
	gtk_text_buffer_get_start_iter (buf, &start);
	gtk_text_buffer_get_end_iter (buf, &end);
	comment = g_strdup (gtk_text_buffer_get_text (buf, &start, &end,
						      FALSE));

	dao_init (&dao, NewSheetOutput);
	dao.sheet = state->base.sheet;

	wbc = WORKBOOK_CONTROL (state->base.wbcg);

	res = scenario_add_new (name, cell_range,
				(gchar *) gnm_expr_entry_get_text
				(GNM_EXPR_ENTRY (state->base.input_entry)),
				comment, state->base.sheet, &scenario);

	cmd_scenario_add (wbc, scenario, state->base.sheet);

	if (res)
		go_gtk_notice_dialog (GTK_WINDOW (state->base.dialog),
				 GTK_MESSAGE_WARNING,
				 _("Changing cells contain at least one "
				   "expression that is not just a value. "
				   "Note that showing the scenario will "
				   "overwrite the formula with its current "
				   "value."));

	g_free (name);
	g_free (comment);
	gtk_widget_destroy (state->base.dialog);
 out:
	value_release (cell_range);
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
	gtk_widget_set_sensitive (state->base.ok_button, TRUE);
}

void
dialog_scenario_add (WBCGtk *wbcg)
{
        ScenariosState *state;
	WorkbookControl  *wbc;
	GtkWidget        *comment_view;
	char const *error_str = _("Could not create the Scenario Add dialog.");
	GString          *buf;

	if (wbcg == NULL)
		return;

	wbc = WORKBOOK_CONTROL (wbcg);

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, "ScenarioAdd"))
		return;

	state = g_new (ScenariosState, 1);

	if (dialog_tool_init (&state->base, wbcg, wb_control_cur_sheet (wbc),
			      GNUMERIC_HELP_LINK_SCENARIOS_ADD,
			      "scenario-add.glade", "ScenarioAdd",
			      error_str,
			      "ScenarioAdd",
			      G_CALLBACK (scenario_add_ok_clicked_cb), NULL,
			      G_CALLBACK (scenario_add_update_sensitivity_cb),
			      GNM_EE_SHEET_OPTIONAL))
		return;

	state->name_entry = glade_xml_get_widget (state->base.gui, "name_entry");
	if (state->name_entry == NULL)
	        return;

	comment_view = glade_xml_get_widget (state->base.gui, "comment_view");
	if (comment_view == NULL)
	        return;
	buf = g_string_new (NULL);
	g_string_append_printf (buf, _("Created on "));
	dao_append_date (buf);
	gtk_text_buffer_set_text (gtk_text_view_get_buffer
				  (GTK_TEXT_VIEW (comment_view)), buf->str,
				  strlen (buf->str));
	g_string_free (buf, FALSE);

	state->base.gdao = NULL;

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->base.dialog),
					   wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	gnumeric_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->name_entry));
	scenario_add_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);
}


/********* Scenario Manager UI ******************************************/

static void
update_comment (ScenariosState *state, const gchar *cells,
		const gchar *comment)
{
	GtkWidget     *w;
	GtkTextBuffer *buf;

	/* Update changing cells box */
	w = glade_xml_get_widget (state->base.gui, "changing_cells_entry");
	gtk_entry_set_text (GTK_ENTRY (w), cells);

	/* Update comment text view */
	w = glade_xml_get_widget (state->base.gui, "comment_view");
	buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (w));

	gtk_text_buffer_set_text (buf, comment, strlen (comment));
}

static gboolean
find_scenario_strs (GList *scenarios, gchar *name,
		    gchar **cells, gchar **comment)
{
	static gchar *buf1 = NULL, *buf2 = NULL;

	while (scenarios) {
		const scenario_t *scenario = scenarios->data;

		if (strcmp (scenario->name, name) == 0) {
			g_free (buf1);
			g_free (buf2);

			*cells = buf1 = g_strdup (scenario->cell_sel_str);
			*comment = buf2 = g_strdup (scenario->comment);
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
	gtk_widget_set_sensitive (state->scenario_state->show_button, f);
	gtk_widget_set_sensitive (state->scenario_state->delete_button, f);

	if (f) {
		GtkTreeSelection        *selection;
		GtkTreeIter             iter;
		GtkTreeModel            *model;
		gchar                   *name;
		gchar                   *comment = NULL;
		gchar                   *cells = NULL;

		selection = gtk_tree_view_get_selection
			(GTK_TREE_VIEW
			 (state->scenario_state->scenarios_treeview));
		if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;
		model = gtk_tree_view_get_model
			(GTK_TREE_VIEW
			 (state->scenario_state->scenarios_treeview));

		gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
				    0, &name, -1);

		find_scenario_strs (state->base.sheet->scenarios, name,
				    &cells, &comment);
		update_comment (state, cells, comment);
	} else
		update_comment (state, "", "");
}

static void
update_scenarios_treeview (GtkWidget *view, GList *scenarios)
{
	  GtkTreeIter  iter;
	  GtkListStore *store;
	  GtkTreePath  *path;

	  store = gtk_list_store_new (1, G_TYPE_STRING);

	  while (scenarios != NULL) {
	          scenario_t *s = scenarios->data;

	          gtk_list_store_append (store, &iter);
	          gtk_list_store_set (store, &iter, 0, s->name, -1);
	          scenarios = scenarios->next;
	  }
	  path = gtk_tree_path_new_from_string ("0");
	  if (gtk_tree_model_get_iter (GTK_TREE_MODEL (store),
				       &iter, path)) {
		  ;		/* Do something */
	  }  else {
		  g_warning ("Did not get a valid iterator");
	  }
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
	gtk_widget_set_sensitive (state->base.ok_button, TRUE);
}


static void
scenario_manager_free (ScenariosState *state)
{
	g_slist_free (state->scenario_state->new_report_sheets);
	state->scenario_state->new_report_sheets = NULL;

	g_free (state->scenario_state);
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
	data_analysis_output_t dao;
	WorkbookControl        *wbc;
	scenario_cmd_t         *cmd = g_new (scenario_cmd_t, 1);

	if (state->scenario_state->current) {
		dao_init (&dao, NewSheetOutput);
		dao.sheet = state->base.sheet;
		wbc = WORKBOOK_CONTROL (state->base.wbcg);

		cmd->redo = state->scenario_state->current;
		cmd->undo = state->scenario_state->old_values;

		cmd_scenario_mngr (wbc, cmd, state->base.sheet);
	}

	scenario_manager_ok (state->base.sheet);

	scenario_manager_free (state);
	gtk_widget_destroy (state->base.dialog);

	return;
}

static void
restore_old_values (ScenariosState *state)
{
	data_analysis_output_t  dao;
	WorkbookControl *wbc;

	if (state->scenario_state->old_values == NULL)
		return;
	wbc = WORKBOOK_CONTROL (state->base.wbcg);
	dao_init (&dao, NewSheetOutput);
	dao.sheet = state->base.sheet;
	scenario_show (wbc, NULL,
		       (scenario_t *) state->scenario_state->old_values,
		       &dao);
	state->scenario_state->current    = NULL;
	state->scenario_state->old_values = NULL;
}

/**
 * scenarios_cancel_clicked_cb:
 * @button:
 * @state:
 *
 * Cancel clicked on the scenario manager tool.
 **/
static void
scenarios_cancel_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			     ScenariosState *state)
{
	GSList *cur;
	WorkbookControl *wbc;

	restore_old_values (state);

	wbc = WORKBOOK_CONTROL (state->base.wbcg);

	/* Remove report sheets created on this dialog session. */
	for (cur = state->scenario_state->new_report_sheets; cur != NULL;
	     cur = cur->next) {
		Sheet *sheet = (Sheet *) cur->data;

		/* Check that if the focus is on a deleted sheet. */
		if (wb_control_cur_sheet (wbc) == sheet)
			wb_control_sheet_focus (wbc, state->base.sheet);

		/* Delete a report sheet. */
		workbook_sheet_delete (sheet);
	}

	/* Recover the deleted scenarios. */
	scenario_recover_all (state->base.sheet->scenarios);

	scenario_manager_free (state);
	gtk_widget_destroy (state->base.dialog);
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
	const gchar             *value;

	selection = gtk_tree_view_get_selection
	        (GTK_TREE_VIEW (state->scenario_state->scenarios_treeview));
	dao_init (&dao, NewSheetOutput);
	dao.sheet = state->base.sheet;
	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;
	model = gtk_tree_view_get_model
	        (GTK_TREE_VIEW (state->scenario_state->scenarios_treeview));

	gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,  0, &value, -1);

	wbc = WORKBOOK_CONTROL (state->base.wbcg);
	state->scenario_state->current =
		scenario_by_name (state->base.sheet->scenarios, value, NULL),
	state->scenario_state->old_values =
		scenario_show (wbc,
			       state->scenario_state->current,
			       state->scenario_state->old_values,
			       &dao);
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
	gboolean                all_deleted;

	restore_old_values (state);

	selection = gtk_tree_view_get_selection
	        (GTK_TREE_VIEW (state->scenario_state->scenarios_treeview));
	dao_init (&dao, NewSheetOutput);
	dao.sheet = state->base.sheet;
	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;
	model = gtk_tree_view_get_model
	        (GTK_TREE_VIEW (state->scenario_state->scenarios_treeview));

	gtk_tree_model_get (GTK_TREE_MODEL (model), &iter, 0, &value, -1);

	gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
	all_deleted = scenario_mark_deleted (state->base.sheet->scenarios, value);
	set_selection_state (state, FALSE);

	if (all_deleted)
		gtk_widget_set_sensitive
			(state->scenario_state->summary_button, FALSE);
}

static void
scenarios_summary_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			      ScenariosState *state)
{
	Sheet  *new_sheet;
	GSList *results;

	restore_old_values (state);

	results = gnm_expr_entry_parse_as_list (
		GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);

	if (results == NULL) {
		go_gtk_notice_dialog (GTK_WINDOW (state->base.dialog),
				 GTK_MESSAGE_ERROR,
				 _("Results entry did not contain valid "
				   "cell names."));
		return;
	}

	scenario_summary (WORKBOOK_CONTROL (state->base.wbcg), state->base.sheet,
			  results, &new_sheet);

	state->scenario_state->new_report_sheets =
		g_slist_prepend (state->scenario_state->new_report_sheets,
				 new_sheet);
	if (results)
		go_slist_free_custom (results, (GFreeFunc)value_release);
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
	/* Show button */
	state->scenario_state->show_button =
	        glade_xml_get_widget (state->base.gui, "show_button");
	if (state->scenario_state->show_button == NULL)
	        return TRUE;
	g_signal_connect (G_OBJECT (state->scenario_state->show_button),
			  "clicked",
			  G_CALLBACK (scenarios_show_clicked_cb), state);

	/* Delete button */
	state->scenario_state->delete_button =
	        glade_xml_get_widget (state->base.gui, "delete_button");
	if (state->scenario_state->delete_button == NULL)
	        return TRUE;
	g_signal_connect (G_OBJECT (state->scenario_state->delete_button),
			  "clicked",
			  G_CALLBACK (scenarios_delete_clicked_cb), state);

	/* Summary button */
	state->scenario_state->summary_button =
	        glade_xml_get_widget (state->base.gui, "summary_button");
	if (state->scenario_state->summary_button == NULL)
	        return TRUE;
	g_signal_connect (G_OBJECT (state->scenario_state->summary_button),
			  "clicked",
			  G_CALLBACK (scenarios_summary_clicked_cb), 
			  state);

	set_selection_state (state, FALSE);

	return FALSE;
}

void
dialog_scenarios (WBCGtk *wbcg)
{
        ScenariosState   *state;
	WorkbookControl  *wbc;
	Sheet            *sheet;
	GtkWidget        *w;
	GtkTreeSelection *select;
	char const *error_str = _("Could not create the Scenarios dialog.");

	g_return_if_fail (wbcg != NULL);

	wbc   = WORKBOOK_CONTROL (wbcg);
	sheet = wb_control_cur_sheet (wbc);

	state = g_new (ScenariosState, 1);
	state->scenario_state = g_new (scenario_state_t, 1);
	state->scenario_state->new_report_sheets = NULL;
	state->scenario_state->current    = NULL;
	state->scenario_state->old_values = NULL;
	state->base.wb = wb_control_get_workbook (wbc);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_SCENARIOS_VIEW,
			      "scenario-manager.glade", "Scenarios",
			      error_str, "Scenarios",
			      G_CALLBACK (scenarios_ok_clicked_cb),
			      G_CALLBACK (scenarios_cancel_clicked_cb),
			      G_CALLBACK (scenarios_update_sensitivity_cb),
			      0))
		goto error_out;

	if (init_scenario_buttons (state))
		goto error_out;

	state->scenario_state->scenarios_treeview = glade_xml_get_widget
	        (state->base.gui, "scenarios_treeview");
	if (state->scenario_state->scenarios_treeview == NULL)
	        goto error_out;

	w = glade_xml_get_widget (state->base.gui, "changing_cells_entry");
	if (w == NULL)
	        goto error_out;
	gtk_widget_set_sensitive (w, FALSE);
	w = glade_xml_get_widget (state->base.gui, "comment_view");

	if (w == NULL)
	        goto error_out;
	gtk_widget_set_sensitive (w, FALSE);

	if (state->base.sheet->scenarios == NULL)
		gtk_widget_set_sensitive
			(state->scenario_state->summary_button, FALSE);

	update_scenarios_treeview (state->scenario_state->scenarios_treeview,
				   sheet->scenarios);
	select = gtk_tree_view_get_selection
		(GTK_TREE_VIEW (state->scenario_state->scenarios_treeview));
	g_signal_connect (select, "changed",
			  G_CALLBACK (cb_selection_changed), state);

	scenarios_update_sensitivity_cb (NULL, state);
	gtk_widget_show (state->base.dialog);

        return;

 error_out:
	go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR, error_str);
	g_free (state->scenario_state);
	g_free (state);

	return;
}
