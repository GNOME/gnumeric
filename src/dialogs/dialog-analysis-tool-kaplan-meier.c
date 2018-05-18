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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <tools/analysis-kaplan-meier.h>
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

#define KAPLAN_MEIER_KEY      "analysistools-kaplan-meier-dialog"

typedef struct {
	GnmGenericToolState base;
	GtkWidget *censorship_button;
	GtkWidget *censor_spin_from;
	GtkWidget *censor_spin_to;
	GtkWidget *graph_button;
	GtkWidget *logrank_button;
	GtkWidget *tick_button;
	GtkWidget *add_group_button;
	GtkWidget *remove_group_button;
	GtkWidget *std_error_button;
	GtkWidget *groups_check;
	GtkWidget *groups_grid;
	GnmExprEntry *groups_input;
	GtkTreeView *groups_treeview;
	GtkListStore *groups_list;
} KaplanMeierToolState;

enum
{
     GROUP_NAME,
     GROUP_FROM,
     GROUP_TO,
     GROUP_ADJUSTMENT_FROM,
     GROUP_ADJUSTMENT_TO,
     GROUP_COLUMNS
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
	gboolean groups;
        GnmValue *input_range;
        GnmValue *input_range_2 = NULL;
	int height, width;

	censorship = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->censorship_button));
	groups = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->groups_check));

	gtk_widget_set_sensitive (state->tick_button, censorship);

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

	if (groups) {
		input_range_2 =  gnm_expr_entry_parse_as_value
			(GNM_EXPR_ENTRY (state->groups_input), state->base.sheet);

		if (input_range_2 == NULL) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The groups column is not valid."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			return;
		}
		if (input_range_2->v_range.cell.b.col != input_range_2->v_range.cell.a.col) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The groups column should be part of a single column."));
			gtk_widget_set_sensitive (state->base.ok_button, FALSE);
			value_release (input_range_2);
			return;
		}
		if (input_range_2->v_range.cell.b.row - input_range_2->v_range.cell.a.row != height) {
			gtk_label_set_text (GTK_LABEL (state->base.warning),
					    _("The groups and time columns should have the same height."));
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

static gboolean
kaplan_meier_tool_get_groups_cb (GtkTreeModel *model,
				 G_GNUC_UNUSED GtkTreePath *path,
				 GtkTreeIter *iter,
				 gpointer data)
{
	GSList **list = data;
	analysis_tools_kaplan_meier_group_t *group_item =
		g_new0 (analysis_tools_kaplan_meier_group_t, 1);

	gtk_tree_model_get (model, iter,
			    GROUP_NAME, &(group_item->name),
			    GROUP_FROM, &(group_item->group_from),
			    GROUP_TO, &(group_item->group_to),
			    -1);
	*list = g_slist_prepend (*list, group_item);

	return FALSE;
}

static GSList *
kaplan_meier_tool_get_groups (KaplanMeierToolState *state)
{
	GSList *list = NULL;

	if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->groups_check)))
		return NULL;

	gtk_tree_model_foreach (GTK_TREE_MODEL (state->groups_list),
				kaplan_meier_tool_get_groups_cb,
				&list);
	return g_slist_reverse (list);
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
	dao  = parse_output ((GnmGenericToolState *)state, NULL);


	data->base.wbc = GNM_WBC (state->base.wbcg);

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

	data->censor_mark = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (state->censor_spin_from));
	data->censor_mark_to = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (state->censor_spin_to));

	data->group_list = kaplan_meier_tool_get_groups (state);
	if (data->group_list == NULL) {
		data->range_3 = NULL;
		data->logrank_test = FALSE;
	} else {
		data->range_3 = gnm_expr_entry_parse_as_value
			(GNM_EXPR_ENTRY (state->groups_input), state->base.sheet);
		data->logrank_test = gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (state->logrank_button));
	}

	data->median = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget
				   (state->base.gui,
				    "median-button")));
	data->chart = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->graph_button));
	data->ticks = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->tick_button));
	data->std_err = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->std_error_button));

	if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg),
				state->base.sheet,
				dao, data, analysis_tool_kaplan_meier_engine,
				TRUE))
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
kaplan_meier_tool_set_groups_cb (G_GNUC_UNUSED GtkWidget *widget,
				 G_GNUC_UNUSED GdkEventFocus *event,
				 KaplanMeierToolState *state)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->groups_check), TRUE);
	return FALSE;
}


static gboolean
kaplan_meier_tool_set_censor_from_cb (G_GNUC_UNUSED GtkWidget *dummy,
				KaplanMeierToolState *state)
{
	gtk_spin_button_set_range (GTK_SPIN_BUTTON (state->censor_spin_to),
				   gtk_spin_button_get_value (GTK_SPIN_BUTTON (state->censor_spin_from)),G_MAXSHORT);
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

static void
cb_group_name_edited (GtkCellRendererText *cell,
		      gchar               *path_string,
		      gchar               *new_text,
		      KaplanMeierToolState *state)
{
	GtkTreeIter iter;
	GtkTreePath *path;

	if (cell != NULL) {
		path = gtk_tree_path_new_from_string (path_string);
		if (gtk_tree_model_get_iter (GTK_TREE_MODEL (state->groups_list),
					     &iter, path))
			gtk_list_store_set (state->groups_list, &iter,
					    GROUP_NAME, new_text, -1);
		else
			g_warning ("Did not get a valid iterator");
		gtk_tree_path_free (path);
	}
}

static void
cb_change_to (GtkCellRendererText *cell,
	      gchar               *path_string,
	      gchar               *new_text,
	      KaplanMeierToolState *state)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	guint val = (guint) (atoi (new_text));

	if (cell != NULL) {
		path = gtk_tree_path_new_from_string (path_string);
		if (gtk_tree_model_get_iter (GTK_TREE_MODEL (state->groups_list),
					     &iter, path))
			gtk_list_store_set (state->groups_list, &iter,
					    GROUP_TO, val, -1);
		else
			g_warning ("Did not get a valid iterator");
		gtk_tree_path_free (path);
	}
}

static void
cb_change_from (GtkCellRendererText *cell,
	      gchar               *path_string,
	      gchar               *new_text,
	      KaplanMeierToolState *state)
{
	if (cell != NULL) {
		GtkTreeIter iter;
		GtkTreePath *path;
		guint val = (guint) (atoi (new_text));
		guint old_to;
		GObject *adjustment_to;


		path = gtk_tree_path_new_from_string (path_string);
		if (gtk_tree_model_get_iter (GTK_TREE_MODEL (state->groups_list),
					     &iter, path))
			gtk_list_store_set (state->groups_list, &iter,
					    GROUP_FROM, val,
					    -1);
		else
			g_warning ("Did not get a valid iterator");
		gtk_tree_path_free (path);

		gtk_tree_model_get (GTK_TREE_MODEL (state->groups_list), &iter,
				    GROUP_TO, &old_to,
				    GROUP_ADJUSTMENT_TO, &adjustment_to,
				    -1);

		if (old_to < val)
			gtk_list_store_set (state->groups_list, &iter,
					    GROUP_TO, val,
					    -1);
		g_object_set (adjustment_to, "lower", (gdouble) val, NULL);

	}
}

static void
cb_selection_changed (GtkTreeSelection *selection,
		      KaplanMeierToolState *state)
{
	gtk_widget_set_sensitive (state->remove_group_button,
				  gtk_tree_selection_get_selected (selection, NULL, NULL));
}

static void
kaplan_meier_tool_update_groups_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				      KaplanMeierToolState *state)
{
	gboolean groups = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->groups_check));
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->groups_treeview);

	gtk_widget_set_sensitive (state->add_group_button, groups);
	gtk_widget_set_sensitive (GTK_WIDGET (state->groups_treeview), groups);

	if (groups) {
		cb_selection_changed (selection, state);
		gtk_widget_set_sensitive (state->logrank_button, TRUE);
	} else {
		gtk_tree_selection_unselect_all (selection);
		gtk_widget_set_sensitive (state->remove_group_button, FALSE);
		gtk_widget_set_sensitive (state->logrank_button, FALSE);
	}
}

static void
dialog_kaplan_meier_tool_treeview_add_item  (KaplanMeierToolState *state, guint i)
{
		GtkTreeIter iter;
		char * name = g_strdup_printf (_("Group %d"), i);
		GObject *adjustment_to =
			G_OBJECT (gtk_adjustment_new (0, 0, G_MAXUSHORT, 1, 1, 1));
		GObject *adjustment_from =
			G_OBJECT (gtk_adjustment_new (0, 0, G_MAXUSHORT, 1, 1, 1));
		gtk_list_store_append (state->groups_list, &iter);
		gtk_list_store_set (state->groups_list, &iter,
				    GROUP_NAME, name,
				    GROUP_FROM, (guint) i,
				    GROUP_TO, (guint) i,
				    GROUP_ADJUSTMENT_FROM, adjustment_from,
				    GROUP_ADJUSTMENT_TO, adjustment_to,
				    -1);
		g_free (name);
}

static void
dialog_kaplan_meier_tool_setup_treeview (KaplanMeierToolState *state)
{
	guint i;
	GtkCellRenderer *renderer;
	GtkWidget *scrolled = go_gtk_builder_get_widget (state->base.gui, "groups-scrolled");
	GtkTreeSelection  *selection;

	state->groups_treeview = GTK_TREE_VIEW (go_gtk_builder_get_widget
						(state->base.gui,
						 "groups-tree"));
	state->groups_list = gtk_list_store_new (GROUP_COLUMNS,
						 G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_OBJECT, G_TYPE_OBJECT);
	state->groups_treeview = GTK_TREE_VIEW (gtk_tree_view_new_with_model
						(GTK_TREE_MODEL (state->groups_list)));
	g_object_unref (state->groups_list);
	selection = gtk_tree_view_get_selection (state->groups_treeview);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

	for (i = 0; i<2; i++)
		dialog_kaplan_meier_tool_treeview_add_item (state, i);

	g_signal_connect (selection,
			  "changed",
			  G_CALLBACK (cb_selection_changed), state);

	renderer = gtk_cell_renderer_text_new ();
	g_object_set (G_OBJECT (renderer),
		      "editable", TRUE,
		      NULL);
	gtk_tree_view_insert_column_with_attributes (state->groups_treeview,
						     -1, _("Group"),
						     renderer,
						     "text", GROUP_NAME,
						     NULL);
	g_signal_connect (G_OBJECT (renderer), "edited",
			  G_CALLBACK (cb_group_name_edited), state);

	renderer = gtk_cell_renderer_spin_new ();

	g_object_set (G_OBJECT (renderer), "editable", TRUE, "xalign", 1.0,
		      "digits", 0, NULL);
	g_signal_connect (G_OBJECT (renderer), "edited",
			  G_CALLBACK (cb_change_from), state);
	gtk_tree_view_insert_column_with_attributes (state->groups_treeview,
						     -1, _("From"),
						     renderer,
						     "text", GROUP_FROM,
						     "adjustment", GROUP_ADJUSTMENT_FROM,
						     NULL);

	renderer = gtk_cell_renderer_spin_new ();
	g_object_set (G_OBJECT (renderer), "editable", TRUE, "xalign", 1.0,
		      "digits", 0, NULL);
	g_signal_connect (G_OBJECT (renderer), "edited",
			  G_CALLBACK (cb_change_to), state);
	gtk_tree_view_insert_column_with_attributes (state->groups_treeview,
						     -1, _("To"),
						     renderer,
						     "text", GROUP_TO,
						     "adjustment", GROUP_ADJUSTMENT_TO,
						     NULL);

	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (state->groups_treeview));

	cb_selection_changed (selection, state);
}

static gboolean
kaplan_meier_tool_add_group_cb (G_GNUC_UNUSED GtkWidget *dummy,
				KaplanMeierToolState *state)
{
	dialog_kaplan_meier_tool_treeview_add_item
		(state, gtk_tree_model_iter_n_children (GTK_TREE_MODEL (state->groups_list),
                                                        NULL));
	return FALSE;
}

static gboolean
kaplan_meier_tool_remove_group_cb (G_GNUC_UNUSED GtkWidget *dummy,
				KaplanMeierToolState *state)
{
	GtkTreeSelection  *selection;
	GtkTreeIter iter;

	selection = gtk_tree_view_get_selection (state->groups_treeview);

	if (gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		gtk_list_store_remove ( state->groups_list, &iter);
	}

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
	GtkWidget *widget;
	char const * plugins[] = { "Gnumeric_fnstat",
				   "Gnumeric_fnlookup",
				   "Gnumeric_fnmath",
				   "Gnumeric_fninfo",
				   "Gnumeric_fnlogical",
				   NULL};

	if ((wbcg == NULL) ||
	    gnm_check_for_plugins_missing (plugins, wbcg_toplevel (wbcg)))
		return 1;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, KAPLAN_MEIER_KEY))
		return 0;

	state = g_new0 (KaplanMeierToolState, 1);

	if (dialog_tool_init (&state->base, wbcg, sheet,
			      GNUMERIC_HELP_LINK_KAPLAN_MEIER,
			      "res:ui/kaplan-meier.ui", "KaplanMeier",
			      _("Could not create the Kaplan Meier Tool dialog."),
			      KAPLAN_MEIER_KEY,
			      G_CALLBACK (kaplan_meier_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (kaplan_meier_tool_update_sensitivity_cb),
			      0))
	{
		g_free(state);
		return 0;
	}



	state->censorship_button = GTK_WIDGET (go_gtk_builder_get_widget
						  (state->base.gui,
						   "censor-button"));
	state->censor_spin_from = GTK_WIDGET (go_gtk_builder_get_widget
					      (state->base.gui,
					       "censored-spinbutton1"));
	gtk_spin_button_set_range (GTK_SPIN_BUTTON (state->censor_spin_from), 0.,G_MAXSHORT);
	state->censor_spin_to = GTK_WIDGET (go_gtk_builder_get_widget
					    (state->base.gui,
					     "censored-spinbutton2"));
	gtk_spin_button_set_range (GTK_SPIN_BUTTON (state->censor_spin_to), 0.,G_MAXSHORT);
	state->graph_button = GTK_WIDGET (go_gtk_builder_get_widget
						  (state->base.gui,
						   "graph-button"));
	state->tick_button = GTK_WIDGET (go_gtk_builder_get_widget
						  (state->base.gui,
						   "tick-button"));
	state->add_group_button = GTK_WIDGET (go_gtk_builder_get_widget
						  (state->base.gui,
						   "add-button"));
	state->remove_group_button = GTK_WIDGET (go_gtk_builder_get_widget
						  (state->base.gui,
						   "remove-button"));
	state->std_error_button = GTK_WIDGET (go_gtk_builder_get_widget
						  (state->base.gui,
						   "std-error-button"));
	state->logrank_button = GTK_WIDGET (go_gtk_builder_get_widget
						  (state->base.gui,
						   "logrank-button"));

	state->groups_check = GTK_WIDGET (go_gtk_builder_get_widget
						  (state->base.gui,
						   "groups-check"));
	state->groups_grid = GTK_WIDGET (go_gtk_builder_get_widget
						  (state->base.gui,
						   "groups-grid"));
	state->groups_input = gnm_expr_entry_new (state->base.wbcg, TRUE);
	gnm_expr_entry_set_flags (state->groups_input, GNM_EE_FORCE_ABS_REF,
				  GNM_EE_MASK);
	gtk_grid_attach (GTK_GRID (state->groups_grid),
	                 GTK_WIDGET (state->groups_input), 1, 1, 2, 1);

	dialog_kaplan_meier_tool_setup_treeview (state);

	g_signal_connect_after (G_OBJECT (state->groups_check),
		"toggled",
		G_CALLBACK (kaplan_meier_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->censorship_button),
		"toggled",
		G_CALLBACK (kaplan_meier_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->graph_button),
		"toggled",
		G_CALLBACK (kaplan_meier_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->std_error_button),
		"toggled",
		G_CALLBACK (kaplan_meier_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->groups_input),
				"changed",
				G_CALLBACK (kaplan_meier_tool_update_sensitivity_cb),
				state);

	g_signal_connect_after (G_OBJECT (state->groups_check),
		"toggled",
		G_CALLBACK (kaplan_meier_tool_update_groups_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->tick_button),
		"toggled",
		G_CALLBACK (kaplan_meier_tool_set_graph_cb), state);
	g_signal_connect_after (G_OBJECT (state->add_group_button),
		"clicked",
		G_CALLBACK (kaplan_meier_tool_add_group_cb), state);
	g_signal_connect_after (G_OBJECT (state->remove_group_button),
		"clicked",
		G_CALLBACK (kaplan_meier_tool_remove_group_cb), state);
	g_signal_connect_after (G_OBJECT (state->censor_spin_from),
		"value-changed",
		G_CALLBACK (kaplan_meier_tool_set_censor_from_cb), state);
	g_signal_connect_after (G_OBJECT (state->censor_spin_to),
		"value-changed",
		G_CALLBACK (kaplan_meier_tool_set_censor_cb), state);
	g_signal_connect (G_OBJECT
			  (gnm_expr_entry_get_entry (
				  GNM_EXPR_ENTRY (state->base.input_entry_2))),
			  "focus-in-event",
			  G_CALLBACK (kaplan_meier_tool_set_censorship_cb), state);
	g_signal_connect (G_OBJECT
			  (gnm_expr_entry_get_entry (
				  GNM_EXPR_ENTRY (state->groups_input))),
			  "focus-in-event",
			  G_CALLBACK (kaplan_meier_tool_set_groups_cb), state);

	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
					  GTK_WIDGET (state->groups_input));

	widget = go_gtk_builder_get_widget (state->base.gui, "groups-label");
	gtk_label_set_mnemonic_widget (GTK_LABEL (widget),
				       GTK_WIDGET (state->groups_input));
	go_atk_setup_label (widget, GTK_WIDGET (state->groups_input));

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	kaplan_meier_tool_update_sensitivity_cb (NULL, state);
	kaplan_meier_tool_update_groups_sensitivity_cb (NULL, state);
	tool_load_selection ((GnmGenericToolState *)state, TRUE);

	gtk_widget_show_all (GTK_WIDGET (state->base.dialog));
	/* And to hide the in-place button again */
	gnm_dao_set_inplace ( GNM_DAO (state->base.gdao), NULL);

        return 0;
}
