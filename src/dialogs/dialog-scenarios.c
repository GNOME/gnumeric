/*
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <gui-util.h>
#include <commands.h>
#include <workbook-control.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <sheet.h>
#include <dialogs/dao-gui-utils.h>
#include <position.h>
#include <ranges.h>
#include <value.h>
#include <cell.h>
#include <tools/dao.h>
#include <tools/scenarios.h>
#include <dialogs/tool-dialogs.h>

#include <goffice/goffice.h>
#include <string.h>

#define DARK_GRAY  GO_COLOR_GREY(51)    /* "gray20" */
#define LIGHT_GRAY GO_COLOR_GREY(199)   /* "gray78" */

typedef struct {
	GnmGenericToolState base;

	GtkWidget  *show_button;
	GtkWidget  *delete_button;
	GtkWidget  *summary_button;
        GtkWidget  *name_entry;

        GtkWidget  *scenarios_treeview;
	GSList     *new_report_sheets;

	GOUndo *undo;
	GnmScenario *current;
} ScenariosState;

typedef GnmValue * (*ScenarioValueCB) (int col, int row, GnmValue *v, gpointer data);

static void
scenario_for_each_value (GnmScenario *s, ScenarioValueCB fn, gpointer data)
{
}

/* Ok button pressed. */
static void
scenario_manager_ok (Sheet *sheet)
{
	GList *l, *scenarios = g_list_copy (sheet->scenarios);

	/* Update scenarios (free the deleted ones). */
	for (l = scenarios; l; l = l->next) {
		GnmScenario *sc = l->data;
		if (g_object_get_data (G_OBJECT (sc), "marked_deleted")) {
			gnm_sheet_scenario_remove (sc->sheet, sc);
		}
	}
	g_list_free (scenarios);

	sheet_redraw_all (sheet, TRUE);
}

/* Cancel button pressed. */
static void
scenario_recover_all (Sheet *sheet)
{
	GList *l;

	for (l = sheet->scenarios; l; l = l->next) {
		GnmScenario *sc = l->data;
		g_object_set_data (G_OBJECT (sc),
				   "marked_deleted", GUINT_TO_POINTER (FALSE));
	}
}

/* Scenario: Create summary report ***************************************/

static void
rm_fun_cb (gpointer key, gpointer value, gpointer user_data)
{
	g_free (value);
}

typedef struct {
	data_analysis_output_t dao;

	Sheet      *sheet;
	GHashTable *names;  /* A hash table for cell names->row. */
	int        col;
	int        row;
	GSList     *results;
} summary_cb_t;

static GnmValue *
summary_cb (int col, int row, GnmValue *v, summary_cb_t *p)
{
	char  *tmp = dao_find_name (p->sheet, col, row);
	int   *index;

	/* Check if some of the previous scenarios already included that
	 * cell. If so, it's row will be put into *index. */
	index = g_hash_table_lookup (p->names, tmp);
	if (index != NULL) {
		dao_set_cell_value (&p->dao, 2 + p->col, 3 + *index,
				    value_dup (v));

		/* Set the colors. */
		dao_set_colors (&p->dao, 2 + p->col, 3 + *index,
				2 + p->col, 3 + *index,
				gnm_color_new_go (GO_COLOR_BLACK),
				gnm_color_new_go (LIGHT_GRAY));

	} else {
		/* New cell. */
		GnmCell *cell;
		int  *r;

		/* Changing cell name. */
		dao_set_cell (&p->dao, 0, 3 + p->row, tmp);

		/* GnmValue of the cell in this scenario. */
		dao_set_cell_value (&p->dao, 2 + p->col, 3 + p->row,
				    value_dup (v));

		/* Current value of the cell. */
		cell = sheet_cell_fetch (p->sheet, col, row);
		dao_set_cell_value (&p->dao, 1, 3 + p->row,
				    value_dup (cell->value));

		/* Set the colors. */
		dao_set_colors (&p->dao, 2 + p->col, 3 + p->row,
				2 + p->col, 3 + p->row,
				gnm_color_new_go (GO_COLOR_BLACK),
				gnm_color_new_go (LIGHT_GRAY));

		/* Insert row number into the hash table. */
		r  = g_new (int, 1);
		*r = p->row;
		g_hash_table_insert (p->names, tmp, r);

		/* Increment the nbr of rows. */
		p->row++;
	}

	return v;
}

static void
scenario_summary_res_cells (WorkbookControl *wbc, GSList *results,
			    summary_cb_t *cb)
{
}

static void
scenario_summary (WorkbookControl *wbc,
		  Sheet           *sheet,
		  GSList          *results,
		  Sheet           **new_sheet)
{
	summary_cb_t cb;
	GList        *cur;
	GList        *scenarios = sheet->scenarios;

	/* Initialize: Currently only new sheet output supported. */
	dao_init_new_sheet (&cb.dao);
	dao_prepare_output (wbc, &cb.dao, _("Scenario Summary"));

	/* Titles. */
	dao_set_cell (&cb.dao, 1, 1, _("Current Values"));
	dao_set_cell (&cb.dao, 0, 2, _("Changing Cells:"));

	/* Go through all scenarios. */
	cb.row     = 0;
	cb.names   = g_hash_table_new (g_str_hash, g_str_equal);
	cb.sheet   = sheet;
	cb.results = results;
	for (cb.col = 0, cur = scenarios; cur != NULL; cb.col++,
		     cur = cur->next) {
		GnmScenario *s = cur->data;

		/* Scenario name. */
		dao_set_cell (&cb.dao, 2 + cb.col, 1, s->name);

		scenario_for_each_value (s, (ScenarioValueCB) summary_cb, &cb);
	}

	/* Set the alignment of names of the changing cells to be right. */
	dao_set_align (&cb.dao, 0, 3, 0, 2 + cb.row, GNM_HALIGN_RIGHT,
		       GNM_VALIGN_BOTTOM);

	/* Result cells. */
	if (results != NULL)
		scenario_summary_res_cells (wbc, results, &cb);

	/* Destroy the hash table. */
	g_hash_table_foreach (cb.names, (GHFunc) rm_fun_cb, NULL);
	g_hash_table_destroy (cb.names);

	/* Clean up the report output. */
	dao_set_bold (&cb.dao, 0, 0, 0, 2 + cb.row);
	dao_autofit_columns (&cb.dao);
	dao_set_cell (&cb.dao, 0, 0, _("Scenario Summary"));

	dao_set_colors (&cb.dao, 0, 0, cb.col + 1, 1,
			gnm_color_new_go (GO_COLOR_WHITE),
			gnm_color_new_go (DARK_GRAY));
	dao_set_colors (&cb.dao, 0, 2, 0, 2 + cb.row,
			gnm_color_new_go (GO_COLOR_BLACK),
			gnm_color_new_go (LIGHT_GRAY));

	dao_set_align (&cb.dao, 1, 1, cb.col + 1, 1, GNM_HALIGN_RIGHT,
		       GNM_VALIGN_BOTTOM);

	*new_sheet = cb.dao.sheet;
}

/********* Scenario Add UI **********************************************/

static gboolean
scenario_name_used (const GList *scenarios, const gchar *name)
{
	while (scenarios != NULL) {
		const GnmScenario *s = scenarios->data;

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
 * scenario_add_ok_clicked_cb:
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
	GnmScenario             *sc;
	GnmSheetRange           sr;

	cell_range = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);

	if (!cell_range || !gnm_sheet_range_from_value (&sr, cell_range)) {
		go_gtk_notice_dialog (GTK_WINDOW (state->base.dialog),
				 GTK_MESSAGE_ERROR,
				 _("Invalid changing cells"));
		gnm_expr_entry_grab_focus (state->base.input_entry, TRUE);
		return;
	}

	if (sr.sheet && sr.sheet != state->base.sheet) {
		go_gtk_notice_dialog (GTK_WINDOW (state->base.dialog),
				 GTK_MESSAGE_ERROR,
				 _("Changing cells should be on the current "
				   "sheet only."));
		gnm_expr_entry_grab_focus (state->base.input_entry, TRUE);
		goto out;
	}
	entry = go_gtk_builder_get_widget (state->base.gui, "name_entry");

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

	comment_view = go_gtk_builder_get_widget (state->base.gui, "comment_view");
	buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (comment_view));
	gtk_text_buffer_get_start_iter (buf, &start);
	gtk_text_buffer_get_end_iter (buf, &end);
	comment = g_strdup (gtk_text_buffer_get_text (buf, &start, &end,
						      FALSE));

	dao_init_new_sheet (&dao);
	dao.sheet = state->base.sheet;

	wbc = GNM_WBC (state->base.wbcg);

	sc = gnm_sheet_scenario_new (state->base.sheet, name);
	if (comment && comment[0])
		gnm_scenario_set_comment (sc, comment);
	gnm_scenario_add_area (sc, &sr);

	cmd_scenario_add (wbc, sc, state->base.sheet);

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

	wbc = GNM_WBC (wbcg);

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, "ScenarioAdd"))
		return;

	state = g_new (ScenariosState, 1);

	if (dialog_tool_init (&state->base, wbcg, wb_control_cur_sheet (wbc),
			      GNUMERIC_HELP_LINK_SCENARIOS_ADD,
			      "res:ui/scenario-add.ui", "ScenarioAdd",
			      error_str,
			      "ScenarioAdd",
			      G_CALLBACK (scenario_add_ok_clicked_cb), NULL,
			      G_CALLBACK (scenario_add_update_sensitivity_cb),
			      GNM_EE_SHEET_OPTIONAL))
	{
		g_free (state);
		return;
	}

	state->name_entry = go_gtk_builder_get_widget (state->base.gui, "name_entry");
	if (state->name_entry == NULL)
	        return;

	comment_view = go_gtk_builder_get_widget (state->base.gui, "comment_view");
	if (comment_view == NULL)
	        return;
	buf = g_string_new (NULL);
	g_string_append_printf (buf, _("Created on "));
	dao_append_date (buf);
	gtk_text_buffer_set_text (gtk_text_view_get_buffer
				  (GTK_TEXT_VIEW (comment_view)), buf->str,
				  strlen (buf->str));
	g_string_free (buf, TRUE);

	state->base.gdao = NULL;

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->base.dialog),
					   wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->name_entry));
	scenario_add_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);
}


/********* Scenario Manager UI ******************************************/

static void
update_comment (ScenariosState *state, const gchar *cells,
		const gchar *comment)
{
	GtkWidget     *w;
	GtkTextBuffer *buf;

	/* Update changing cells box */
	w = go_gtk_builder_get_widget (state->base.gui, "changing_cells_entry");
	gtk_entry_set_text (GTK_ENTRY (w), cells);

	/* Update comment text view */
	w = go_gtk_builder_get_widget (state->base.gui, "comment_view");
	buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (w));

	gtk_text_buffer_set_text (buf, comment, strlen (comment));
}

static void
set_selection_state (ScenariosState *state, gboolean f)
{
	/* Set the sensitivies to FALSE since no selections have been made */
	gtk_widget_set_sensitive (state->show_button, f);
	gtk_widget_set_sensitive (state->delete_button, f);

	if (f) {
		GtkTreeSelection        *selection;
		GtkTreeIter             iter;
		GtkTreeModel            *model;
		gchar                   *name;
		gchar                   *cells_txt;
		GnmScenario             *sc;

		selection = gtk_tree_view_get_selection
			(GTK_TREE_VIEW
			 (state->scenarios_treeview));
		if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
			return;
		model = gtk_tree_view_get_model
			(GTK_TREE_VIEW
			 (state->scenarios_treeview));

		gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
				    0, &name, -1);

		sc = gnm_sheet_scenario_find (state->base.sheet, name);
		if (!sc)
			return;
		cells_txt = gnm_scenario_get_range_str (sc);
		update_comment (state, cells_txt, sc->comment);
		g_free (cells_txt);
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
	          GnmScenario *s = scenarios->data;

	          gtk_list_store_append (store, &iter);
	          gtk_list_store_set (store, &iter, 0, s->name, -1);
	          scenarios = scenarios->next;
	  }
	  path = gtk_tree_path_new_from_string ("0");
	  if (gtk_tree_model_get_iter (GTK_TREE_MODEL (store),
				       &iter, path)) {
		  ;		/* Do something */
	  }  else {
		  /* No scenarios */
	  }
	  gtk_tree_path_free (path);

	  gtk_tree_view_set_model (GTK_TREE_VIEW (view),
				   GTK_TREE_MODEL (store));
	  g_object_unref (store);
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
	g_slist_free (state->new_report_sheets);
	state->new_report_sheets = NULL;

	if (state->undo) {
		g_object_unref (state->undo);
		state->undo = NULL;
	}
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
	if (state->current) {
		WorkbookControl *wbc = GNM_WBC (state->base.wbcg);
		cmd_scenario_mngr (wbc, state->current, state->undo);
	}

	scenario_manager_ok (state->base.sheet);

	scenario_manager_free (state);
	gtk_widget_destroy (state->base.dialog);

	return;
}

static void
restore_old_values (ScenariosState *state)
{
	GOCmdContext *cc;

	if (state->undo == NULL)
		return;

	cc = GO_CMD_CONTEXT (state->base.wbcg);
	go_undo_undo_with_data (state->undo, cc);
	g_object_unref (state->undo);
	state->undo = NULL;
	state->current = NULL;
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

	wbc = GNM_WBC (state->base.wbcg);

	/* Remove report sheets created on this dialog session. */
	for (cur = state->new_report_sheets; cur != NULL;
	     cur = cur->next) {
		Sheet *sheet = cur->data;

		/* Check that if the focus is on a deleted sheet. */
		if (wb_control_cur_sheet (wbc) == sheet)
			wb_control_sheet_focus (wbc, state->base.sheet);

		/* Delete a report sheet. */
		workbook_sheet_delete (sheet);
	}

	/* Recover the deleted scenarios. */
	scenario_recover_all (state->base.sheet);

	scenario_manager_free (state);
	gtk_widget_destroy (state->base.dialog);
}

static void
scenarios_show_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			   ScenariosState *state)
{
	GtkTreeSelection        *selection;
	GtkTreeIter             iter;
	GtkTreeModel            *model;
	const gchar             *value;

	selection = gtk_tree_view_get_selection
	        (GTK_TREE_VIEW (state->scenarios_treeview));
	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;
	model = gtk_tree_view_get_model
	        (GTK_TREE_VIEW (state->scenarios_treeview));

	gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,  0, &value, -1);

	restore_old_values (state);

	state->current = gnm_sheet_scenario_find (state->base.sheet, value);
	state->undo = gnm_scenario_apply (state->current);
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
	GnmScenario             *sc;
	GList                   *l;

	restore_old_values (state);

	selection = gtk_tree_view_get_selection
	        (GTK_TREE_VIEW (state->scenarios_treeview));
	dao_init_new_sheet (&dao);
	dao.sheet = state->base.sheet;
	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;
	model = gtk_tree_view_get_model
	        (GTK_TREE_VIEW (state->scenarios_treeview));

	gtk_tree_model_get (GTK_TREE_MODEL (model), &iter, 0, &value, -1);

	gtk_list_store_remove (GTK_LIST_STORE (model), &iter);

	sc = gnm_sheet_scenario_find (state->base.sheet, value);
	if (sc)
		g_object_set_data (G_OBJECT (sc),
				   "marked_deleted", GUINT_TO_POINTER (TRUE));

	set_selection_state (state, FALSE);

	all_deleted = TRUE;
	for (l = state->base.sheet->scenarios; l && all_deleted; l = l->next) {
		GnmScenario *sc = l->data;
		if (!g_object_get_data (G_OBJECT (sc), "marked_deleted"))
			all_deleted = FALSE;
	}

	gtk_widget_set_sensitive
		(state->summary_button, !all_deleted);
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

	scenario_summary (GNM_WBC (state->base.wbcg), state->base.sheet,
			  results, &new_sheet);

	state->new_report_sheets =
		g_slist_prepend (state->new_report_sheets,
				 new_sheet);
	if (results)
		g_slist_free_full (results, (GDestroyNotify)value_release);
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
	state->show_button =
	        go_gtk_builder_get_widget (state->base.gui, "show_button");
	if (state->show_button == NULL)
	        return TRUE;
	g_signal_connect (G_OBJECT (state->show_button),
			  "clicked",
			  G_CALLBACK (scenarios_show_clicked_cb), state);

	/* Delete button */
	state->delete_button =
	        go_gtk_builder_get_widget (state->base.gui, "delete_button");
	if (state->delete_button == NULL)
	        return TRUE;
	g_signal_connect (G_OBJECT (state->delete_button),
			  "clicked",
			  G_CALLBACK (scenarios_delete_clicked_cb), state);

	/* Summary button */
	state->summary_button =
	        go_gtk_builder_get_widget (state->base.gui, "summary_button");
	if (state->summary_button == NULL)
	        return TRUE;
	g_signal_connect (G_OBJECT (state->summary_button),
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

	wbc   = GNM_WBC (wbcg);
	sheet = wb_control_cur_sheet (wbc);

	state = g_new (ScenariosState, 1);
	state->new_report_sheets = NULL;
	state->current    = NULL;
	state->undo = NULL;
	state->base.wb = wb_control_get_workbook (wbc);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_SCENARIOS_VIEW,
			      "res:ui/scenario-manager.ui", "Scenarios",
			      error_str, "Scenarios",
			      G_CALLBACK (scenarios_ok_clicked_cb),
			      G_CALLBACK (scenarios_cancel_clicked_cb),
			      G_CALLBACK (scenarios_update_sensitivity_cb),
			      0))
		goto error_out;

	if (init_scenario_buttons (state))
		goto error_out;

	state->scenarios_treeview = go_gtk_builder_get_widget
	        (state->base.gui, "scenarios_treeview");
	if (state->scenarios_treeview == NULL)
	        goto error_out;

	w = go_gtk_builder_get_widget (state->base.gui, "changing_cells_entry");
	if (w == NULL)
	        goto error_out;
	gtk_widget_set_sensitive (w, FALSE);
	w = go_gtk_builder_get_widget (state->base.gui, "comment_view");

	if (w == NULL)
	        goto error_out;
	gtk_widget_set_sensitive (w, FALSE);

	if (state->base.sheet->scenarios == NULL)
		gtk_widget_set_sensitive
			(state->summary_button, FALSE);

	update_scenarios_treeview (state->scenarios_treeview,
				   sheet->scenarios);
	select = gtk_tree_view_get_selection
		(GTK_TREE_VIEW (state->scenarios_treeview));
	g_signal_connect (select, "changed",
			  G_CALLBACK (cb_selection_changed), state);

	scenarios_update_sensitivity_cb (NULL, state);
	gtk_widget_show (state->base.dialog);

        return;

 error_out:
	go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
			      "%s", error_str);
	g_free (state);

	return;
}
