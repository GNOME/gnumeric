/*
 * dialog-data-slicer.c:  Edit DataSlicers
 *
 * (c) Copyright 2008-2009 Jody Goldberg <jody@gnome.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
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
#include <sheet-view.h>
#include <workbook-cmd-format.h>
#include <gnm-sheet-slicer.h>
#include <go-data-slicer.h>
#include <go-data-slicer-field.h>
#include <go-data-cache.h>

typedef struct {
	GtkWidget		*dialog;
	WBCGtk			*wbcg;
	SheetView		*sv;

	GnmSheetSlicer		*slicer;
	GODataCache		*cache;
	GODataCacheSource	*source;

	GtkWidget		*notebook;
	GnmExprEntry		*source_expr;

	GtkTreeView		*treeview;
	GtkTreeSelection	*selection;
} DialogDataSlicer;

enum {
	FIELD,
	FIELD_TYPE,
	FIELD_NAME,
	FIELD_HEADER_INDEX,
	NUM_COLUMNS
};
#define DIALOG_KEY "dialog-data-slicer"

static void
cb_dialog_data_slicer_destroy (DialogDataSlicer *state)
{
	if (NULL != state->slicer)	{ g_object_unref (state->slicer);	state->slicer = NULL; }
	if (NULL != state->cache)	{ g_object_unref (state->cache);	state->cache = NULL; }
	if (NULL != state->source)	{ g_object_unref (state->source);	state->source = NULL; }
	state->dialog = NULL;
	g_free (state);
}

static void
cb_dialog_data_slicer_ok (G_GNUC_UNUSED GtkWidget *button,
			  DialogDataSlicer *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_dialog_data_slicer_cancel (G_GNUC_UNUSED GtkWidget *button,
			      DialogDataSlicer *state)
{
	gtk_widget_destroy (state->dialog);
}

static gint
cb_sort_by_header_index (GtkTreeModel *model,
			 GtkTreeIter  *a,
			 GtkTreeIter  *b,
			 gpointer      user_data)
{
#if 0
	GtkTreeIter child_a, child_b;
	GtkRecentInfo *info_a, *info_b;
	gboolean is_folder_a, is_folder_b;

	gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (model), &child_a, a);
	gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (model), &child_b, b);

	gtk_tree_model_get (GTK_TREE_MODEL (impl->recent_model), &child_a,
			    RECENT_MODEL_COL_IS_FOLDER, &is_folder_a,
			    RECENT_MODEL_COL_INFO, &info_a,
			    -1);
	gtk_tree_model_get (GTK_TREE_MODEL (impl->recent_model), &child_b,
			    RECENT_MODEL_COL_IS_FOLDER, &is_folder_b,
			    RECENT_MODEL_COL_INFO, &info_b,
			    -1);

	if (!info_a)
		return 1;

	if (!info_b)
		return -1;

	/* folders always go first */
	if (is_folder_a != is_folder_b)
		return is_folder_a ? 1 : -1;

	if (gtk_recent_info_get_modified (info_a) < gtk_recent_info_get_modified (info_b))
		return -1;
	else if (gtk_recent_info_get_modified (info_a) > gtk_recent_info_get_modified (info_b))
		return 1;
	else
#endif
		return 0;
}

static void
cb_dialog_data_slicer_create_model (DialogDataSlicer *state)
{
	struct {
		GODataSlicerFieldType	type;
		char const *		type_name;
		GtkTreeIter iter;
	} field_type_labels[] = {
		{ GDS_FIELD_TYPE_PAGE,	N_("Filter") },
		{ GDS_FIELD_TYPE_ROW,	N_("Row") },
		{ GDS_FIELD_TYPE_COL,	N_("Column") },
		{ GDS_FIELD_TYPE_DATA,	N_("Data") },
		/* Must be last */
		{ GDS_FIELD_TYPE_UNSET,	N_("Unused") }
	};

	unsigned int	 i, j, n;
	GtkTreeStore	*model;
	GtkTreeModel	*smodel;

	model = gtk_tree_store_new (NUM_COLUMNS,
				    G_TYPE_POINTER,	/* field */
				    G_TYPE_INT,		/* field-type */
				    G_TYPE_STRING,	/* field-name */
				    G_TYPE_INT);	/* field-header-index */
	smodel = gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (model));
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (smodel),
		FIELD_HEADER_INDEX, cb_sort_by_header_index, NULL, NULL);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (smodel),
		FIELD_HEADER_INDEX, GTK_SORT_ASCENDING);

	for (i = 0 ; i < G_N_ELEMENTS (field_type_labels) ; i++) {
		gtk_tree_store_append (model, &field_type_labels[i].iter, NULL);
		gtk_tree_store_set (model, &field_type_labels[i].iter,
			FIELD,			NULL,
			FIELD_TYPE,		field_type_labels[i].type,
			FIELD_NAME,		_(field_type_labels[i].type_name),
			FIELD_HEADER_INDEX,	-1,
			-1);
	}
	n = go_data_slicer_num_fields (GO_DATA_SLICER (state->slicer));
	for (i = 0 ; i < n ; i++) {
		GtkTreeIter child_iter;
		GODataSlicerField *field =
			go_data_slicer_get_field (GO_DATA_SLICER (state->slicer), i);
		GOString *name = go_data_slicer_field_get_name (field);
		gboolean used = FALSE;

		for (j = 0 ; j < G_N_ELEMENTS (field_type_labels) ; j++) {
			int header_index = (GDS_FIELD_TYPE_UNSET != field_type_labels[j].type)
				? go_data_slicer_field_get_field_type_pos (field, field_type_labels[j].type)
				: (used ? -1 : 0);
			if (header_index >= 0) {
				used = TRUE;
				gtk_tree_store_append (model, &child_iter, &field_type_labels[j].iter);
				gtk_tree_store_set (model, &child_iter,
					FIELD,			field,
					FIELD_TYPE,		field_type_labels[j].type,
					FIELD_NAME,		name->str,
					FIELD_HEADER_INDEX,	header_index,
					-1);
			}
		}
	}
	gtk_tree_view_set_model (state->treeview, smodel);
}

static void
cb_dialog_data_slicer_selection_changed (GtkTreeSelection *selection,
					 DialogDataSlicer *state)
{
}

static void
cb_source_expr_changed (DialogDataSlicer *state)
{
        GnmValue *range;
       	range = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->source_expr), sv_sheet (state->sv));
#warning "FIXME: Now what?"
	value_release (range);
}

void
dialog_data_slicer (WBCGtk *wbcg, gboolean create)
{
	static GtkTargetEntry row_targets[] = {
		{ (char *)"GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_APP, 0 }
	};
	DialogDataSlicer *state;
	GtkBuilder *gui;
	GtkWidget *w;

	g_return_if_fail (wbcg != NULL);

	if (gnm_dialog_raise_if_exists (wbcg, DIALOG_KEY))
		return;

	gui = gnm_gtk_builder_load ("res:ui/data-slicer.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (NULL == gui)
		return;

	state = g_new0 (DialogDataSlicer, 1);
	state->wbcg	= wbcg;
	state->sv	= wb_control_cur_sheet_view (GNM_WBC (wbcg));

	state->dialog	= go_gtk_builder_get_widget (gui, "dialog_data_slicer");
	state->notebook = go_gtk_builder_get_widget (gui, "notebook");
	state->slicer	= create ? NULL : gnm_sheet_view_editpos_in_slicer (state->sv);
	state->cache	= NULL;
	state->source	= NULL;

	if (NULL == state->slicer) {
		state->slicer = g_object_new (GNM_SHEET_SLICER_TYPE, NULL);
	} else {
		g_object_ref (state->slicer);
		g_object_get (G_OBJECT (state->slicer), "cache", &state->cache, NULL);
		if (NULL != state->cache &&
		    NULL != (state->source = go_data_cache_get_source (state->cache)))
		    g_object_ref (state->source);
	}

	state->source_expr = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->source_expr,
		GNM_EE_SINGLE_RANGE, GNM_EE_MASK);
	g_signal_connect_swapped (G_OBJECT (state->source_expr),
		"changed", G_CALLBACK (cb_source_expr_changed), state);
	w = go_gtk_builder_get_widget (gui, "source_vbox");
	gtk_box_pack_start (GTK_BOX (w), GTK_WIDGET (state->source_expr), FALSE, FALSE, 0);
	gtk_widget_show (GTK_WIDGET (state->source_expr));

	w = go_gtk_builder_get_widget (gui, "ok_button");
	g_signal_connect (G_OBJECT (w), "clicked",
		G_CALLBACK (cb_dialog_data_slicer_ok), state);
	w = go_gtk_builder_get_widget (gui, "cancel_button");
	g_signal_connect (G_OBJECT (w), "clicked",
		G_CALLBACK (cb_dialog_data_slicer_cancel), state);

	state->treeview = GTK_TREE_VIEW (go_gtk_builder_get_widget (gui, "field_tree"));
	gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (state->treeview), GDK_BUTTON1_MASK,
		row_targets, G_N_ELEMENTS (row_targets), GDK_ACTION_MOVE);
	gtk_tree_view_enable_model_drag_dest (GTK_TREE_VIEW (state->treeview),
		row_targets, G_N_ELEMENTS (row_targets), GDK_ACTION_MOVE);
	state->selection = gtk_tree_view_get_selection (state->treeview);
	gtk_tree_selection_set_mode (state->selection, GTK_SELECTION_SINGLE);
	g_signal_connect (state->selection, "changed",
		G_CALLBACK (cb_dialog_data_slicer_selection_changed), state);

	gtk_tree_view_append_column (state->treeview,
		gtk_tree_view_column_new_with_attributes ("",
			gtk_cell_renderer_text_new (), "text", FIELD_NAME, NULL));
	cb_dialog_data_slicer_create_model (state);

	g_signal_connect (state->treeview, "realize", G_CALLBACK (gtk_tree_view_expand_all), NULL);

	gtk_notebook_set_current_page (GTK_NOTEBOOK (state->notebook), create ? 0 : 1);

	/* a candidate for merging into attach guru */
	gnm_init_help_button (go_gtk_builder_get_widget (gui, "help_button"),
		GNUMERIC_HELP_LINK_DATA_SLICER);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_dialog_data_slicer_destroy);
	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	gnm_keyed_dialog (wbcg, GTK_WINDOW (state->dialog), DIALOG_KEY);
	gtk_widget_show (state->dialog);
	g_object_unref (gui);
}

