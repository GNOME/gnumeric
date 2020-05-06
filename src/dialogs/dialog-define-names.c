/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-define-names.c: Edit named regions.
 *
 * Author:
 *	Jody Goldberg <jody@gnome.org>
 *	Michael Meeks <michael@ximian.com>
 *	Chema Celorio <chema@celorio.com>
 *      Andreas J. Guelzow <aguelzow@pyrshep.ca>
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

#include <expr.h>
#include <expr-name.h>
#include <selection.h>
#include <sheet.h>
#include <sheet-view.h>
#include <workbook.h>
#include <workbook-control.h>
#include <wbc-gtk.h>
#include <workbook-view.h>
#include <gui-util.h>
#include <parse-util.h>
#include <commands.h>
#include <widgets/gnm-expr-entry.h>
#include <widgets/gnm-cell-renderer-expr-entry.h>
#include <widgets/gnm-cell-renderer-toggle.h>

#include <string.h>

#define DEFINE_NAMES_KEY "define-names-dialog"
#define PASTE_NAMES_KEY "paste-names-dialog"

typedef struct {
	GtkBuilder		*gui;
	GtkWidget		*dialog;
	GtkWidget		*treeview;
	GtkTreeStore		*model;
	GtkTreeModel    	*model_f;

	GtkWidget *close_button;
	GtkWidget *paste_button;
	GtkWidget *search_entry;

	Sheet			*sheet;
	SheetView		*sv;
	Workbook		*wb;
	WBCGtk	                *wbcg;
	GnmParsePos		 pp;

	GdkPixbuf               *image_add;
	GdkPixbuf               *image_delete;
	GdkPixbuf               *image_lock;
	GdkPixbuf               *image_up;
	GdkPixbuf               *image_down;
	GdkPixbuf               *image_paste;

	gboolean                 is_paste_dialog;
	gboolean                 has_pasted;
} NameGuruState;

enum {
	ITEM_NAME,
	ITEM_NAME_POINTER,
	ITEM_CONTENT,
	ITEM_TYPE,
	ITEM_CONTENT_IS_EDITABLE,
	ITEM_NAME_IS_EDITABLE,
	ITEM_UPDOWN_IMAGE,
	ITEM_ADDDELETE_IMAGE,
	ITEM_UPDOWN_ACTIVE,
	ITEM_ADDDELETE_ACTIVE,
	ITEM_PASTABLE,
	ITEM_PASTE_IMAGE,
	ITEM_VISIBLE,
	NUM_COLUMNS
};

typedef enum {
	item_type_workbook = 0,
	item_type_main_sheet,
	item_type_other_sheet,
	item_type_locked_name,
	item_type_available_wb_name,
	item_type_available_sheet_name,
	item_type_foreign_name,
	item_type_new_unsaved_wb_name,
	item_type_new_unsaved_sheet_name,
} item_type_t;


/**
 * name_guru_translate_pathstring_to_iter:
 * @state:
 * @path_string: in the filter_model
 * @iter:        in the base model
 *
 **/

static gboolean
name_guru_translate_pathstring_to_iter (NameGuruState *state,
					GtkTreeIter *iter,
					gchar const *path_string)
{
	GtkTreeIter iter_f;

	if (!gtk_tree_model_get_iter_from_string
	    (state->model_f, &iter_f, path_string))
		return FALSE;

	gtk_tree_model_filter_convert_iter_to_child_iter
		(GTK_TREE_MODEL_FILTER (state->model_f), iter, &iter_f);
	return TRUE;
}




/**
 * name_guru_expand_at_iter:
 * @state:
 * @iter:
 *
 * expand the treeview at the given iter.
 *
 **/
static void
name_guru_expand_at_iter (NameGuruState *state, GtkTreeIter *iter)
{
	GtkTreePath *path;

	path = gtk_tree_model_get_path
		(GTK_TREE_MODEL (state->model), iter);
	gtk_tree_view_expand_to_path
		(GTK_TREE_VIEW (state->treeview), path);
	gtk_tree_path_free (path);

}

/**
 * name_guru_warned_if_used:
 * @state:
 * @nexpr: expression to be deleted
 *
 * If the expression that is about to be deleted is being used,
 * warn the user about it. Ask if we should proceed or not
 *
 * Returns: %TRUE if users confirms deletion, %FALSE otherwise
 **/

static gboolean
name_guru_warn (NameGuruState *state,
		GnmNamedExpr *nexpr)
{
	return (!expr_name_in_use (nexpr) ||
		 go_gtk_query_yes_no
		(GTK_WINDOW (state->dialog), FALSE,
		 "The defined name '%s' is in use. "
		 "Do you really want to delete it?",
		 expr_name_name (nexpr)));
}

static gboolean
cb_name_guru_show_all (G_GNUC_UNUSED GtkTreeModel *model,
		       G_GNUC_UNUSED GtkTreePath *path,
		       GtkTreeIter *iter, gpointer data)
{
	NameGuruState *state = data;
	gtk_tree_store_set (state->model, iter,
			    ITEM_VISIBLE, TRUE,
			    -1);
	return FALSE;
}

static void
name_guru_erase_search_entry (GtkEntry *entry,
			      G_GNUC_UNUSED GtkEntryIconPosition icon_pos,
			      G_GNUC_UNUSED GdkEvent *event,
			      gpointer data)
{
	NameGuruState *state = data;
	gtk_entry_set_text (entry, "");
	gtk_tree_model_foreach (GTK_TREE_MODEL (state->model),
				cb_name_guru_show_all, state);
}

static gboolean
cb_name_guru_search (GtkTreeModel *model,
		     G_GNUC_UNUSED GtkTreePath *path,
		     GtkTreeIter *iter, gpointer data)
{
	char const *text = data;
	gchar *name;
	gboolean visible = TRUE, was_visible;
	item_type_t type;

	gtk_tree_model_get (model, iter,
			    ITEM_TYPE, &type,
			    ITEM_NAME, &name,
			    ITEM_VISIBLE, &was_visible,
			    -1);

	if (type != item_type_workbook &&
	    type != item_type_main_sheet &&
	    type != item_type_other_sheet) {
		gchar *name_n, *name_cf, *text_n, *text_cf;

		text_n = g_utf8_normalize (text, -1, G_NORMALIZE_ALL);
		text_cf = g_utf8_casefold(text_n, -1);
		name_n = g_utf8_normalize (name, -1, G_NORMALIZE_ALL);
		name_cf = g_utf8_casefold(name_n, -1);
		visible = (NULL != g_strstr_len (name_cf, -1, text_cf));
		g_free (text_n);
		g_free (text_cf);
		g_free (name_n);
		g_free (name_cf);
	}

	if (visible != was_visible)
		gtk_tree_store_set (GTK_TREE_STORE (model), iter,
				    ITEM_VISIBLE, visible,
				    -1);

	g_free (name);
	return FALSE;
}

static void
name_guru_search (GtkEntry *entry, gpointer data)
{
	gchar const *text;
	NameGuruState *state = data;

	if (0 == gtk_entry_get_text_length (entry)){
		name_guru_erase_search_entry
			(entry,
			 GTK_ENTRY_ICON_SECONDARY, NULL,
			 data);
		return;
	}
	text = gtk_entry_get_text (entry);
	gtk_tree_model_foreach (GTK_TREE_MODEL (state->model),
				cb_name_guru_search, (gpointer) text);
}

static void
cb_get_names (G_GNUC_UNUSED gpointer key, GnmNamedExpr *nexpr,
	      GList **accum)
{
	if (!nexpr->is_hidden)
		*accum = g_list_prepend (*accum, nexpr);
}

static GList *
name_guru_get_available_sheet_names (Sheet const *sheet)
{
	GList *res = NULL;

	gnm_sheet_foreach_name (sheet, (GHFunc) cb_get_names, &res);
	return g_list_sort (res, (GCompareFunc)expr_name_cmp_by_name);
}

static GList *
name_guru_get_available_wb_names (Workbook const *wb)
{
	GList *res = NULL;

	workbook_foreach_name (wb, TRUE,
			       (GHFunc) cb_get_names,
			       &res);
	return g_list_sort (res, (GCompareFunc)expr_name_cmp_by_name);
}

static void
name_guru_set_images (NameGuruState *state, GtkTreeIter	*name_iter,
		      item_type_t type, gboolean pastable)
{
	GdkPixbuf *button1 = NULL, *button2 = NULL;

	switch (type) {
	case item_type_workbook:
	case item_type_main_sheet:
		button2 = state->image_add;
		break;
	case item_type_locked_name:
		button2 = state->image_lock;
		break;
	case item_type_available_wb_name:
	case item_type_new_unsaved_wb_name:
		button1 = state->image_down;
		button2 = state->image_delete;
		break;
	case item_type_available_sheet_name:
	case item_type_new_unsaved_sheet_name:
		button1 = state->image_up;
		button2 = state->image_delete;
		break;
	case item_type_other_sheet:
	case item_type_foreign_name:
	default:
		break;
	}

	gtk_tree_store_set (state->model, name_iter,
			    ITEM_UPDOWN_IMAGE, button1,
			    ITEM_ADDDELETE_IMAGE, button2,
			    ITEM_PASTE_IMAGE,
			    pastable ?  state->image_paste : NULL,
			    ITEM_UPDOWN_ACTIVE, button1 != NULL,
			    ITEM_ADDDELETE_ACTIVE, button2 != NULL,
			    -1);
}

static void
name_guru_store_names (GList            *list,
		       GtkTreeIter	*iter,
		       NameGuruState    *state,
		       item_type_t       type)
{
	GtkTreeIter	 name_iter;
	char            *content;
	item_type_t      adj_type;
	GList           *l;

	for (l = list; l != NULL; l = l->next) {
		GnmNamedExpr    *nexpr = l->data;
		gboolean         ciseditable, ispastable;

		if (nexpr->is_hidden || expr_name_is_placeholder (nexpr))
			continue;

		ispastable = ciseditable =
			type == item_type_available_wb_name
			|| type == item_type_available_sheet_name;

		if (nexpr->is_permanent) {
			adj_type =  item_type_locked_name;
			ciseditable = FALSE;
		} else
			adj_type = type;

		content = expr_name_as_string (nexpr, &state->pp,
					       sheet_get_conventions (state->sheet));


		gtk_tree_store_append (state->model, &name_iter,
				       iter);
		gtk_tree_store_set (state->model, &name_iter,
				    ITEM_NAME, expr_name_name (nexpr),
				    ITEM_NAME_POINTER, nexpr,
				    ITEM_CONTENT, content,
				    ITEM_TYPE, adj_type,
				    ITEM_CONTENT_IS_EDITABLE, ciseditable,
				    ITEM_NAME_IS_EDITABLE, FALSE,
				    ITEM_PASTABLE, ispastable,
				    ITEM_VISIBLE, TRUE,
				    -1);
		g_free (content);

		name_guru_set_images (state, &name_iter, adj_type, ispastable);
	}
	g_list_free (list);
}

static void
name_guru_populate_list (NameGuruState *state)
{
	GtkTreeIter	 iter;

	g_return_if_fail (state != NULL);
	g_return_if_fail (state->treeview != NULL);

	gtk_tree_store_clear (state->model);

	gtk_tree_store_append (state->model, &iter, NULL);
	gtk_tree_store_set (state->model, &iter,
			    ITEM_NAME, _("Workbook"),
			    ITEM_NAME_POINTER, NULL,
			    ITEM_TYPE, item_type_workbook,
			    ITEM_CONTENT_IS_EDITABLE, FALSE,
			    ITEM_NAME_IS_EDITABLE, FALSE,
			    ITEM_PASTABLE, FALSE,
			    ITEM_VISIBLE, TRUE,
			    -1);
	name_guru_set_images (state, &iter, item_type_workbook, FALSE);
	name_guru_store_names (name_guru_get_available_wb_names (state->wb),
			       &iter,
			       state,
			       item_type_available_wb_name);
	name_guru_expand_at_iter (state, &iter);

	gtk_tree_store_append (state->model, &iter, NULL);
	gtk_tree_store_set (state->model, &iter,
			    ITEM_NAME,  state->sheet->name_unquoted,
			    ITEM_NAME_POINTER,  state->sheet,
			    ITEM_TYPE, item_type_main_sheet,
			    ITEM_CONTENT_IS_EDITABLE, FALSE,
			    ITEM_NAME_IS_EDITABLE, FALSE,
			    ITEM_PASTABLE, FALSE,
			    ITEM_VISIBLE, TRUE,
			    -1);
	name_guru_set_images (state, &iter, item_type_main_sheet, FALSE);

	name_guru_store_names (name_guru_get_available_sheet_names
			       (state->sheet),
		       &iter,
		       state,
		       item_type_available_sheet_name);
	name_guru_expand_at_iter (state, &iter);

	WORKBOOK_FOREACH_SHEET
		(state->wb, sheet,
		 {
			 if (sheet == state->sheet)
				 continue;

			 gtk_tree_store_append (state->model, &iter, NULL);
			 gtk_tree_store_set (state->model, &iter,
					     ITEM_NAME, sheet->name_unquoted,
					     ITEM_NAME_POINTER, sheet,
					     ITEM_TYPE, item_type_other_sheet,
					     ITEM_CONTENT_IS_EDITABLE, FALSE,
					     ITEM_NAME_IS_EDITABLE, FALSE,
					     ITEM_VISIBLE, TRUE,
					     ITEM_PASTABLE, FALSE,
					     -1);

			 name_guru_store_names
				 (name_guru_get_available_sheet_names (sheet),
				  &iter, state, item_type_foreign_name);
		 });
}

static gboolean
name_guru_paste (NameGuruState *state, GtkTreeIter *iter)
{
        char *name;
	gboolean is_pastable;

	gtk_tree_model_get (GTK_TREE_MODEL (state->model),
			    iter,
			    ITEM_PASTABLE, &is_pastable,
			    ITEM_NAME, &name,
			    -1);

	if (!is_pastable)
		return FALSE;

	if (wbcg_edit_start (state->wbcg, FALSE, FALSE)) {
		GtkEntry *entry;
		gint position;
		entry = wbcg_get_entry (state->wbcg);

		position = gtk_entry_get_text_length (entry);
		if (position == 0)
			gtk_editable_insert_text (GTK_EDITABLE (entry), "=",
					  -1, &position);
		else {
			gtk_editable_delete_selection (GTK_EDITABLE (entry));
			position = gtk_editable_get_position
				(GTK_EDITABLE (entry));
		}
		if (state->has_pasted) {
			char sep = go_locale_get_arg_sep ();
			gtk_editable_insert_text (GTK_EDITABLE (entry), &sep,
					  1, &position);
		}
		gtk_editable_insert_text (GTK_EDITABLE (entry), name,
					  -1, &position);
		gtk_editable_set_position (GTK_EDITABLE (entry), position);
	}

	g_free (name);

	state->has_pasted = TRUE;
	return TRUE;
}




static void
cb_name_guru_clicked (GtkWidget *button, NameGuruState *state)
{
	if (state->dialog == NULL)
		return;

	wbcg_set_entry (state->wbcg, NULL);

	if (button == state->close_button) {
		gtk_widget_destroy (state->dialog);
		return;
	}
	if (button == state->paste_button) {
		GtkTreeIter iter_f;
		GtkTreeIter iter;
		if (gtk_tree_selection_get_selected
		    (gtk_tree_view_get_selection
		     (GTK_TREE_VIEW (state->treeview)), NULL, &iter_f)) {
			gtk_tree_model_filter_convert_iter_to_child_iter
				(GTK_TREE_MODEL_FILTER (state->model_f),
				 &iter, &iter_f);
			if (name_guru_paste (state, &iter))
				gtk_widget_destroy (state->dialog);
		}
		return;
	}
}

static GtkWidget *
name_guru_init_button (NameGuruState *state, char const *name)
{
	GtkWidget *tmp = go_gtk_builder_get_widget (state->gui, name);

	g_return_val_if_fail (tmp != NULL, NULL);

	g_signal_connect (G_OBJECT (tmp),
		"clicked",
		G_CALLBACK (cb_name_guru_clicked), state);
	return tmp;
}

static void
cb_name_guru_destroy (NameGuruState *state)
{
	WorkbookControl *wbc = GNM_WBC (state->wbcg);

	wb_view_selection_desc (wb_control_view (wbc), TRUE, wbc);
	g_clear_object (&state->gui);
	g_clear_object (&state->model);

	if (!state->is_paste_dialog)
		wbcg_edit_finish (state->wbcg, WBC_EDIT_REJECT, NULL);

	g_clear_object (&state->image_paste);
	g_clear_object (&state->image_add);
	g_clear_object (&state->image_delete);
	g_clear_object (&state->image_lock);
	g_clear_object (&state->image_up);
	g_clear_object (&state->image_down);

	state->dialog = NULL;
	g_free (state);
}


static void
cb_name_guru_paste (G_GNUC_UNUSED GtkCellRendererToggle *cell,
			 gchar                 *path_string,
			 gpointer               data)
{
	NameGuruState *state = data;
	GtkTreeIter iter;

	if (name_guru_translate_pathstring_to_iter
	    (state, &iter, path_string))
		name_guru_paste (state, &iter);
}

static void
name_guru_add (NameGuruState *state, GtkTreeIter *iter, gchar const *path_string)
{
	GtkTreeIter	 name_iter;
	char            *content;
	GtkTreePath     *path;
	item_type_t      type;

	path = gtk_tree_path_new_from_string (path_string);

	type = ((gtk_tree_path_get_indices (path))[0] == 0) ?
		item_type_new_unsaved_wb_name :
		item_type_new_unsaved_sheet_name;
	content =  selection_to_string (state->sv, FALSE);

	gtk_tree_store_insert (state->model, &name_iter,
			       iter, 0);
	gtk_tree_store_set (state->model, &name_iter,
			    ITEM_NAME, _("<new name>"),
			    ITEM_NAME_POINTER, NULL,
			    ITEM_CONTENT,
			    ((content == NULL) ? "#REF!" : content),
			    ITEM_TYPE, type,
			    ITEM_CONTENT_IS_EDITABLE, TRUE,
			    ITEM_NAME_IS_EDITABLE, TRUE,
			    ITEM_PASTABLE, FALSE,
			    ITEM_VISIBLE, TRUE,
			    -1);
	name_guru_set_images (state, &name_iter, type, FALSE);
	name_guru_expand_at_iter (state, iter);
	g_free (content);
}

static void
name_guru_delete (NameGuruState *state, GtkTreeIter *iter, item_type_t type)
{
	GnmNamedExpr *nexpr;

	if (type != item_type_new_unsaved_wb_name &&
	    type != item_type_new_unsaved_sheet_name) {
		gtk_tree_model_get (GTK_TREE_MODEL (state->model),
				    iter,
				    ITEM_NAME_POINTER, &nexpr,
				    -1);

		if (!name_guru_warn (state, nexpr))
			return;

		cmd_remove_name (GNM_WBC (state->wbcg), nexpr);
	}
	gtk_tree_store_remove (state->model, iter);
}


static void
cb_name_guru_add_delete (G_GNUC_UNUSED GtkCellRendererToggle *cell,
			 gchar                 *path_string,
			 gpointer               data)
{
	NameGuruState *state = data;
	GtkTreeIter iter;

	if (name_guru_translate_pathstring_to_iter
	    (state, &iter, path_string)) {
		item_type_t type;

		gtk_tree_model_get (GTK_TREE_MODEL (state->model),
				    &iter,
				    ITEM_TYPE, &type,
				    -1);

		switch (type) {
		case item_type_workbook:
		case item_type_main_sheet:
			name_guru_add (state, &iter, path_string);
			break;
		case item_type_available_wb_name:
		case item_type_available_sheet_name:
		case item_type_new_unsaved_wb_name:
		case item_type_new_unsaved_sheet_name:
			name_guru_delete (state, &iter, type);
			break;
		case item_type_other_sheet:
		case item_type_locked_name:
		case item_type_foreign_name:
		default:
			break;
		}
	}
}

static void
name_guru_find_place (NameGuruState *state, GtkTreeIter *iter,
		      GtkTreeIter *parent_iter, GnmNamedExpr *nexpr)
{
	GtkTreeIter next_iter;
	GnmNamedExpr *next_nexpr;
	if (nexpr != NULL &&
	    gtk_tree_model_iter_children (GTK_TREE_MODEL (state->model),
					  &next_iter,
					  parent_iter)) {
		do {
			gtk_tree_model_get (GTK_TREE_MODEL (state->model),
					    &next_iter,
					    ITEM_NAME_POINTER, &next_nexpr,
					    -1);
			if (next_nexpr != NULL &&
			    expr_name_cmp_by_name (nexpr, next_nexpr) < 0) {
				gtk_tree_store_insert_before
					(state->model,
					 iter,
					 parent_iter,
                                         &next_iter);
				return;
			}
		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (state->model),
						   &next_iter));

		gtk_tree_store_append (state->model, iter,
				       parent_iter);
	} else {
		gtk_tree_store_prepend (state->model, iter,
					parent_iter);
	}
}


static void
name_guru_move_record (NameGuruState *state, GtkTreeIter *from_iter,
		       GtkTreeIter *new_parent_iter, item_type_t new_type)
{
	GnmNamedExpr *nexpr;
	gchar *name, *content;
	gboolean ceditable, neditable, pastable, visible;
	GtkTreeIter new_iter;

	gtk_tree_model_get (GTK_TREE_MODEL (state->model),
			    from_iter,
			    ITEM_NAME, &name,
			    ITEM_NAME_POINTER, &nexpr,
			    ITEM_CONTENT, &content,
			    ITEM_CONTENT_IS_EDITABLE, &ceditable,
			    ITEM_NAME_IS_EDITABLE, &neditable,
			    ITEM_PASTABLE, &pastable,
			    ITEM_VISIBLE, &visible,
			    -1);

	gtk_tree_store_remove (state->model, from_iter);

	name_guru_find_place (state, &new_iter, new_parent_iter, nexpr);

	gtk_tree_store_set (state->model, &new_iter,
			    ITEM_NAME, name,
			    ITEM_NAME_POINTER, nexpr,
			    ITEM_CONTENT, content,
			    ITEM_TYPE, new_type,
			    ITEM_CONTENT_IS_EDITABLE, ceditable,
			    ITEM_NAME_IS_EDITABLE, neditable,
			    ITEM_PASTABLE, pastable,
			    ITEM_VISIBLE, visible,
			    -1);
	name_guru_set_images (state, &new_iter, new_type, pastable);
	name_guru_expand_at_iter (state, &new_iter);
	g_free (name);
	g_free (content);
}

static void
cb_name_guru_switch_scope (G_GNUC_UNUSED GtkCellRendererToggle *cell,
			   gchar                 *path_string,
			   gpointer               data)
{
	NameGuruState *state = data;
	GtkTreeIter iter;

	if (name_guru_translate_pathstring_to_iter
	    (state, &iter, path_string)) {
		item_type_t type, new_type;
		gchar const *new_path;
		GnmNamedExpr *nexpr;
		GtkTreeIter new_parent_iter;

		gtk_tree_model_get (GTK_TREE_MODEL (state->model),
				    &iter,
				    ITEM_TYPE, &type,
				    ITEM_NAME_POINTER, &nexpr,
				    -1);

		switch (type) {
		case item_type_available_wb_name:
			if (cmd_rescope_name
			    (GNM_WBC (state->wbcg),
			     nexpr, state->sheet))
				return;
			new_path  = "1";
			new_type  = item_type_available_sheet_name;
			break;
		case item_type_new_unsaved_wb_name:
			new_path  = "1";
			new_type  = item_type_new_unsaved_sheet_name;
			break;
		case item_type_available_sheet_name:
			if (cmd_rescope_name
			    (GNM_WBC (state->wbcg),
			     nexpr, NULL))
				return;
			new_path  = "0";
			new_type  = item_type_available_wb_name;
			break;
		case item_type_new_unsaved_sheet_name:
			new_path  = "0";
			new_type  = item_type_new_unsaved_wb_name;
			break;
		case item_type_workbook:
		case item_type_main_sheet:
		case item_type_other_sheet:
		case item_type_locked_name:
		case item_type_foreign_name:
		default:
			return;
		}

		if (gtk_tree_model_get_iter_from_string
		    (GTK_TREE_MODEL (state->model),
		     &new_parent_iter, new_path)) {
			name_guru_move_record
				(state, &iter, &new_parent_iter, new_type);
		}
	}
}

static gboolean
name_guru_parse_pos_init (NameGuruState *state,
			  GnmParsePos *pp, item_type_t type)
{
	switch (type) {
	case item_type_available_wb_name:
	case item_type_new_unsaved_wb_name:
		parse_pos_init (pp, state->wb, NULL,
				state->pp.eval.col, state->pp.eval.row);
		return TRUE;
	case item_type_available_sheet_name:
	case item_type_new_unsaved_sheet_name:
		parse_pos_init (pp, state->wb, state->sheet,
				state->pp.eval.col, state->pp.eval.row);
		return TRUE;
	case item_type_workbook:
	case item_type_main_sheet:
	case item_type_other_sheet:
	case item_type_locked_name:
	case item_type_foreign_name:
	default:
		return FALSE;
	}
}

/*
 * Return the expression if it is acceptable.
 * The parse position will be initialized then.
 */

static  GnmExprTop const*
name_guru_check_expression (NameGuruState *state, gchar *text,
			    GnmParsePos *pp, item_type_t type)
{
	GnmExprTop const *texpr;
	GnmParseError	  perr;

	if (!name_guru_parse_pos_init (state, pp, type))
		return NULL; /* We should have never gotten here. */

	if (text == NULL || text[0] == '\0') {
		go_gtk_notice_dialog (GTK_WINDOW (state->dialog),
				      GTK_MESSAGE_ERROR,
				      _("Why would you want to define a "
					"name for the empty string?"));
		return NULL;
	}

	texpr = gnm_expr_parse_str (text, pp,
				    GNM_EXPR_PARSE_DEFAULT |
				    GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_INVALID |
				    GNM_EXPR_PARSE_PERMIT_MULTIPLE_EXPRESSIONS,
				    NULL,
				    parse_error_init (&perr));
	if (texpr == NULL) {
		if (perr.err == NULL)
			return NULL;

		go_gtk_notice_dialog (GTK_WINDOW (state->dialog),
				      GTK_MESSAGE_ERROR,
				      "%s", perr.err->message);
		parse_error_free (&perr);
		return NULL;
	}
	/* don't allow user to define a nexpr that looks like a placeholder *
	 * because it will be would disappear from the lists.               */
	if (gnm_expr_top_is_err (texpr, GNM_ERROR_NAME)) {
		go_gtk_notice_dialog (GTK_WINDOW (state->dialog), GTK_MESSAGE_ERROR,
			_("Why would you want to define a name to be #NAME?"));
		parse_error_free (&perr);
		gnm_expr_top_unref (texpr);
		return NULL;
	}

	return texpr;
}


static void
cb_name_guru_content_edited
(G_GNUC_UNUSED GnmCellRendererExprEntry *cell,
 gchar               *path_string,
 gchar               *new_text,
 NameGuruState       *state)
{
	GtkTreeIter       iter;
	item_type_t       type;
	GnmParsePos       pp;
	GnmExprTop const *texpr;
	GnmNamedExpr     *nexpr;

	if (!name_guru_translate_pathstring_to_iter
	    (state, &iter, path_string))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (state->model), &iter,
			    ITEM_TYPE, &type,
			    ITEM_NAME_POINTER, &nexpr,
			    -1);

	/* check whether the content is valid */


	texpr = name_guru_check_expression (state, new_text, &pp , type);
	if (texpr == NULL)
		return;

	/* content is valid */

	if (type != item_type_new_unsaved_wb_name
	    && type != item_type_new_unsaved_sheet_name) {
		/* save the changes (if the name is already saved) */
		cmd_define_name (GNM_WBC (state->wbcg),
				 expr_name_name (nexpr),
				 &pp, texpr, NULL);
	} else
		gnm_expr_top_unref (texpr);

	/* set the model */
	gtk_tree_store_set (state->model, &iter, ITEM_CONTENT, new_text, -1);
}

static void
cb_name_guru_name_edited (G_GNUC_UNUSED GtkCellRendererText *cell,
			     gchar               *path_string,
			     gchar               *new_text,
			     NameGuruState       *state)
{
	GtkTreeIter       iter;
	GtkTreeIter       parent_iter;
	item_type_t       type;
	GnmParsePos       pp;
	GnmExprTop const *texpr;
	gchar            *content;
	GnmNamedExpr     *nexpr;

	g_return_if_fail (new_text != NULL);

	if (!name_guru_translate_pathstring_to_iter
	    (state, &iter, path_string))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (state->model), &iter,
			    ITEM_TYPE, &type,
			    ITEM_CONTENT, &content,
			    -1);

	if (type != item_type_new_unsaved_wb_name &&
	    type != item_type_new_unsaved_sheet_name)
		return;

	name_guru_parse_pos_init (state, &pp, type);
	nexpr = expr_name_lookup (&pp, new_text);

	if (nexpr != NULL && !nexpr->is_placeholder) {
		Sheet *scope = nexpr->pos.sheet;
		if ((type == item_type_new_unsaved_wb_name &&
		     scope == NULL) ||
		    (type == item_type_new_unsaved_sheet_name)) {
			go_gtk_notice_dialog
				(GTK_WINDOW (state->dialog),
				 GTK_MESSAGE_ERROR,
				 _("This name is already in use!"));
			return;
		}
	}

	texpr = name_guru_check_expression (state, content, &pp , type);
	if (texpr == NULL)
		return;

	if (!cmd_define_name (GNM_WBC (state->wbcg),
			      new_text, &pp,
			      texpr, NULL)) {
		nexpr = expr_name_lookup (&pp, new_text);

		type = (type == item_type_new_unsaved_wb_name) ?
			item_type_available_wb_name :
			item_type_available_sheet_name;

		gtk_tree_store_set
			(state->model, &iter,
			 ITEM_NAME, new_text,
			 ITEM_NAME_POINTER, nexpr,
			 ITEM_TYPE, type,
			 ITEM_PASTABLE, TRUE,
			 ITEM_NAME_IS_EDITABLE, FALSE,
			 -1);
		name_guru_set_images (state, &iter, type, TRUE);

		if (gtk_tree_model_iter_parent (GTK_TREE_MODEL (state->model),
						&parent_iter, &iter))
			name_guru_move_record (state, &iter, &parent_iter, type);
	}
}

static void
name_guru_update_sensitivity (GtkTreeSelection *treeselection,
			      gpointer          user_data)
{
	NameGuruState *state = user_data;
	gboolean is_pastable = FALSE;
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected
	    (treeselection, NULL, &iter))
		gtk_tree_model_get (state->model_f, &iter,
				    ITEM_PASTABLE, &is_pastable,
				    -1);
	gtk_widget_set_sensitive (GTK_WIDGET (state->paste_button),
				  is_pastable);

}

static gboolean
cb_name_guru_selection_function (G_GNUC_UNUSED GtkTreeSelection *selection,
				 GtkTreeModel *model,
				 GtkTreePath *path,
				 gboolean path_currently_selected,
				 G_GNUC_UNUSED gpointer data)
{
	GtkTreeIter iter;

	if (path_currently_selected)
		return TRUE;
	if (gtk_tree_model_get_iter (model, &iter, path)) {
		gboolean is_pastable, is_editable;
		gtk_tree_model_get (model,
				    &iter,
				    ITEM_PASTABLE, &is_pastable,
				    ITEM_CONTENT_IS_EDITABLE, &is_editable,
				    -1);
		return (is_pastable || is_editable);
	}
	return FALSE;
}

static gboolean
name_guru_init (NameGuruState *state, WBCGtk *wbcg, gboolean is_paste_dialog)
{
	Workbook *wb = wb_control_get_workbook (GNM_WBC (wbcg));
	GtkTreeViewColumn *column;
	GtkCellRenderer   *renderer;
	GtkTreeSelection  *selection;
	GtkWidget *widget = GTK_WIDGET (wbcg_toplevel (wbcg));

	state->is_paste_dialog = is_paste_dialog;
	state->has_pasted = FALSE;

	state->gui = gnm_gtk_builder_load ("res:ui/define-name.ui", NULL,
	                                  GO_CMD_CONTEXT (wbcg));
        if (state->gui == NULL)
                return TRUE;

	state->wbcg  = wbcg;
	state->wb   = wb;
	state->sv = wb_control_cur_sheet_view (GNM_WBC (wbcg));
	state->sheet = sv_sheet (state->sv);
	parse_pos_init_editpos (&state->pp, state->sv);

	state->dialog = go_gtk_builder_get_widget (state->gui, "NameGuru");

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog),
					   state->wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);
	state->model	 = gtk_tree_store_new
		(NUM_COLUMNS,
		 G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_STRING,
		 G_TYPE_INT, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
		 GDK_TYPE_PIXBUF, GDK_TYPE_PIXBUF,
		 G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
		 GDK_TYPE_PIXBUF, G_TYPE_BOOLEAN);

	state->treeview  = go_gtk_builder_get_widget (state->gui, "name_list");

	state->model_f = gtk_tree_model_filter_new
		(GTK_TREE_MODEL (state->model), NULL);
	gtk_tree_model_filter_set_visible_column
		(GTK_TREE_MODEL_FILTER (state->model_f), ITEM_VISIBLE);

	gtk_tree_view_set_model (GTK_TREE_VIEW (state->treeview),
				 state->model_f);
	g_object_unref (state->model_f);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (state->treeview),
					   FALSE);
	gtk_tree_view_set_grid_lines (GTK_TREE_VIEW (state->treeview),
				      GTK_TREE_VIEW_GRID_LINES_NONE);
	gtk_tree_view_set_reorderable (GTK_TREE_VIEW (state->treeview),
				       FALSE);

	renderer = gtk_cell_renderer_text_new ();
	g_signal_connect (G_OBJECT (renderer), "edited",
			  G_CALLBACK (cb_name_guru_name_edited), state);
	column = gtk_tree_view_column_new_with_attributes
		("name",
		 renderer,
		 "text", ITEM_NAME,
		 "editable", ITEM_NAME_IS_EDITABLE,
		 NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (state->treeview), column);

	if (is_paste_dialog) {
		renderer = gnm_cell_renderer_toggle_new ();
		g_signal_connect (G_OBJECT (renderer),
				  "toggled",
				  G_CALLBACK (cb_name_guru_paste), state);
		column = gtk_tree_view_column_new_with_attributes
			("Paste",
			 renderer,
			 "active", ITEM_PASTABLE,
			 "pixbuf", ITEM_PASTE_IMAGE,
			 NULL);
		gtk_tree_view_append_column (GTK_TREE_VIEW (state->treeview),
					     column);
	} else {
		renderer = gnm_cell_renderer_toggle_new ();
		g_signal_connect (G_OBJECT (renderer),
				  "toggled",
				  G_CALLBACK (cb_name_guru_add_delete), state);
		column = gtk_tree_view_column_new_with_attributes
			("Lock",
			 renderer,
			 "active", ITEM_ADDDELETE_ACTIVE,
			 "pixbuf", ITEM_ADDDELETE_IMAGE,
			 NULL);
		gtk_tree_view_append_column (GTK_TREE_VIEW (state->treeview),
					     column);

		renderer = gnm_cell_renderer_toggle_new ();
		g_signal_connect (G_OBJECT (renderer),
				  "toggled",
				  G_CALLBACK (cb_name_guru_switch_scope),
				  state);
		column = gtk_tree_view_column_new_with_attributes
			("Scope",
			 renderer,
			 "active", ITEM_UPDOWN_ACTIVE,
			 "pixbuf", ITEM_UPDOWN_IMAGE,
			 NULL);
		gtk_tree_view_append_column (GTK_TREE_VIEW (state->treeview),
					     column);
	}

	renderer = gnm_cell_renderer_expr_entry_new (state->wbcg);
	g_signal_connect (G_OBJECT (renderer), "edited",
			  G_CALLBACK (cb_name_guru_content_edited), state);
	column = gtk_tree_view_column_new_with_attributes
		(_("content"),
		 renderer,
		 "text", ITEM_CONTENT,
		 "editable", ITEM_CONTENT_IS_EDITABLE,
		 NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (state->treeview), column);


	selection = gtk_tree_view_get_selection
		(GTK_TREE_VIEW (state->treeview));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	gtk_tree_selection_set_select_function
		(selection, cb_name_guru_selection_function,
		 state, NULL);

	state->close_button  = name_guru_init_button (state, "close_button");
	state->paste_button  = name_guru_init_button (state, "paste_button");

	if (is_paste_dialog) {
		g_signal_connect (G_OBJECT (selection),
				  "changed",
				  G_CALLBACK (name_guru_update_sensitivity),
				  state);
		state->image_paste = go_gtk_widget_render_icon_pixbuf (widget, "edit-paste", GTK_ICON_SIZE_MENU);
		state->image_add    = NULL;
		state->image_delete = NULL;
		state->image_lock   = NULL;
		state->image_up     = NULL;
		state->image_down   = NULL;
	} else {
		state->image_paste = NULL;
		state->image_add = go_gtk_widget_render_icon_pixbuf (widget, "list-add", GTK_ICON_SIZE_MENU);
		state->image_delete = go_gtk_widget_render_icon_pixbuf (widget, "list-remove", GTK_ICON_SIZE_MENU);
		state->image_lock = go_gtk_widget_render_icon_pixbuf (widget, "gnumeric-protection-yes", GTK_ICON_SIZE_MENU);
		state->image_up = go_gtk_widget_render_icon_pixbuf (widget, "go-up", GTK_ICON_SIZE_MENU);
		state->image_down = go_gtk_widget_render_icon_pixbuf (widget, "go-down", GTK_ICON_SIZE_MENU);
	}

	state->search_entry = go_gtk_builder_get_widget (state->gui, "search_entry");

	g_signal_connect (G_OBJECT (state->search_entry),
			  "icon-press",
			  G_CALLBACK (name_guru_erase_search_entry),
			  state);

	g_signal_connect (G_OBJECT (state->search_entry),
			  "activate",
			  G_CALLBACK (name_guru_search),
			  state);

	name_guru_populate_list (state);
	name_guru_update_sensitivity (selection, state);

	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help_button"),
		is_paste_dialog ? GNUMERIC_HELP_LINK_PASTE_NAMES
		: GNUMERIC_HELP_LINK_DEFINE_NAMES);

	/* a candidate for merging into attach guru */
	gnm_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       is_paste_dialog ? PASTE_NAMES_KEY
			       : DEFINE_NAMES_KEY);
	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
				   GTK_WINDOW (state->dialog));

	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_name_guru_destroy);

	if (is_paste_dialog) {
		gtk_window_set_title (GTK_WINDOW (state->dialog),
				      _("Paste Defined Names"));
		gtk_widget_show_all (GTK_WIDGET (state->dialog));
	} else {
		wbc_gtk_attach_guru (state->wbcg, state->dialog);
		gtk_widget_show (GTK_WIDGET (state->dialog));
	}

	return FALSE;
}

/**
 * dialog_define_names:
 * @wbcg:
 *
 * Create and show the define names dialog.
 **/
void
dialog_define_names (WBCGtk *wbcg)
{
	NameGuruState *state;

	g_return_if_fail (wbcg != NULL);

	/* Only one guru per workbook. */
	if (wbc_gtk_get_guru (wbcg))
		return;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, DEFINE_NAMES_KEY))
		return;

	state = g_new0 (NameGuruState, 1);
	if (name_guru_init (state, wbcg, FALSE)) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				 _("Could not create the Name Guru."));
		g_free (state);
		return;
	}
}

/**
 * dialog_paste_names:
 * @wbcg:
 *
 * Create and show the define names dialog.
 **/
void
dialog_paste_names (WBCGtk *wbcg)
{
	NameGuruState *state;

	g_return_if_fail (wbcg != NULL);

	/* Only one guru per workbook. */
	if (wbc_gtk_get_guru (wbcg))
		return;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, PASTE_NAMES_KEY))
		return;

	state = g_new0 (NameGuruState, 1);
	if (name_guru_init (state, wbcg, TRUE)) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				 _("Could not create the Name Guru."));
		g_free (state);
		return;
	}
}
