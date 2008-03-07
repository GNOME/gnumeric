/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-function-wizard.c:  The formula guru
 *
 * Authors:
 *  Jody Goldberg <jody@gnome.org>
 *  Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * Copyright (C) 2000 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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
#include "dialogs.h"
#include "help.h"

#include <parse-util.h>
#include <gui-util.h>
#include <workbook.h>
#include <sheet.h>
#include <sheet-view.h>
#include <wbc-gtk.h>
#include <workbook-control.h>
#include <cell.h>
#include <expr.h>
#include <expr-impl.h>
#include <func.h>
#include <gnm-format.h>
#include <goffice/utils/go-locale.h>
#include <widgets/gnumeric-expr-entry.h>
#include <widgets/gnumeric-cell-renderer-expr-entry.h>

#include <gtk/gtk.h>
#include <locale.h>
#include <string.h>

#define FORMULA_GURU_KEY "formula-guru-dialog"
#define FORMULA_GURU_KEY_DIALOG "formula-guru-dialog"

#define MIN_VAR_ARGS_DISPLAYED 2
#define AV_FORMULA_SIZE 100

typedef struct
{
	WBCGtk  *wbcg;
	Workbook  *wb;

	GladeXML  *gui;
	GtkWidget *dialog;
	GtkWidget *ok_button;
	GtkWidget *selector_button;
	GtkWidget *clear_button;
	GtkWidget *zoom_button;
	GtkWidget *array_button;
	GtkWidget *main_button_area;
	GtkTreePath* active_path;
	char * prefix;
	char * suffix;
	GnmParsePos  *pos;

	GtkTreeStore  *model;
	GtkTreeView   *treeview;

	gint old_height;
	gint old_width;
	gint old_height_request;
	gint old_width_request;

	GnumericCellRendererExprEntry *cellrenderer;
	GtkTreeViewColumn *column;
	GtkCellEditable *editable;
} FormulaGuruState;

enum {
	FUN_ARG_ENTRY,
	IS_NON_FUN,
	ARG_NAME,
	ARG_TYPE,
	MIN_ARG,
	MAX_ARG,
	FUNCTION,
	NUM_COLMNS
};

static void dialog_formula_guru_update_parent (GtkTreeIter *child, FormulaGuruState *state,
					       GtkTreePath *origin,
					       gint sel_start, gint sel_length);

static void
dialog_formula_guru_write (GString *text, FormulaGuruState *state, gint sel_start,
			   gint sel_length)
{
	GtkEntry *entry;

	entry = wbcg_get_entry (state->wbcg);
	if (state->prefix) {
		sel_start += g_utf8_strlen (state->prefix, -1);
		g_string_prepend (text, state->prefix);
	}
	if (state->suffix)
		g_string_append (text, state->suffix);
	gtk_entry_set_text (entry, text->str);
	gtk_editable_select_region (GTK_EDITABLE (entry), sel_start, sel_start + sel_length);
}

static void
dialog_formula_guru_delete_children (GtkTreeIter *parent, FormulaGuruState *state)
{
	GtkTreeIter iter;

	while (gtk_tree_model_iter_children (GTK_TREE_MODEL(state->model),
					     &iter, parent))
		gtk_tree_store_remove (state->model, &iter);
}

static void
dialog_formula_guru_update_this_parent (GtkTreeIter *parent, FormulaGuruState *state,
					GtkTreePath *origin, gint sel_start, gint sel_length)
{
	GString  *text = g_string_sized_new  (AV_FORMULA_SIZE);
	gboolean is_non_fun;
	GnmFunc const *fd;
	GtkTreeIter iter;
	gboolean not_first = FALSE;
	int arg_min, arg_num = 0;
	gboolean find_origin = TRUE;

	gtk_tree_model_get (GTK_TREE_MODEL(state->model), parent,
			    IS_NON_FUN, &is_non_fun,
			    FUNCTION, &fd,
			    MIN_ARG, &arg_min,
			    -1);

	g_return_if_fail (!is_non_fun);
	g_return_if_fail (fd != NULL);

	text = g_string_append (text, gnm_func_get_name (fd));
	text = g_string_append (text, "(");

	if (gtk_tree_model_iter_children (GTK_TREE_MODEL(state->model), &iter, parent)) {
		do {
			char *argument;
			gtk_tree_model_get (GTK_TREE_MODEL(state->model), &iter,
					    FUN_ARG_ENTRY, &argument,
					    -1);
			if ((argument == NULL  || g_utf8_strlen (argument, -1) == 0) && arg_num > arg_min) {
				g_free (argument);
				break;
			}

			if (not_first) {
				text = g_string_append_c (text, go_locale_get_arg_sep ());
			}

			if (find_origin && origin != NULL) {
				GtkTreePath *b = gtk_tree_model_get_path
					(GTK_TREE_MODEL (state->model), &iter);
				if (0 == gtk_tree_path_compare (origin, b)) {
					sel_start += g_utf8_strlen (text->str, text->len);
					gtk_tree_path_free (origin);
					origin = gtk_tree_model_get_path
						(GTK_TREE_MODEL (state->model), parent);
					find_origin = FALSE;
				}
				gtk_tree_path_free (b);
			}
			if (argument && strlen (argument) > 0)
				text = g_string_append (text, argument);
			g_free (argument);
			not_first = TRUE;
			arg_num++;

		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL(state->model), &iter));
	}

	text = g_string_append_c (text, ')');

	gtk_tree_store_set (state->model, parent,
			    FUN_ARG_ENTRY, text->str,
			    -1);
	if (origin == NULL) {
		sel_start = 0;
		sel_length = g_utf8_strlen (text->str, text->len);
		origin = gtk_tree_model_get_path (GTK_TREE_MODEL(state->model), parent);
	}

	if (0 ==  gtk_tree_store_iter_depth (state->model, parent))
		dialog_formula_guru_write (text, state, sel_start, sel_length);

	g_string_free (text, TRUE);

	dialog_formula_guru_update_parent (parent, state, origin, sel_start, sel_length);
}

static void
dialog_formula_guru_update_parent (GtkTreeIter *child, FormulaGuruState *state,
				   GtkTreePath *origin, gint sel_start, gint sel_length)
{
	GtkTreeIter iter;

	if (gtk_tree_model_iter_parent (GTK_TREE_MODEL (state->model), &iter,
					child)) {
		dialog_formula_guru_update_this_parent (&iter, state, origin, sel_start,
							sel_length);
	} else
		gtk_tree_path_free (origin);
}

static void
dialog_formula_guru_update_this_child (GtkTreeIter *child, FormulaGuruState *state,
				   GtkTreePath *origin, gint sel_start, gint sel_length)
{
	GtkTreeIter iter;
	char *text;

	if (gtk_tree_model_iter_parent (GTK_TREE_MODEL (state->model), &iter,
					child)) {
		if (origin == NULL) {
			sel_start = 0;
			gtk_tree_model_get (GTK_TREE_MODEL(state->model), child,
					    FUN_ARG_ENTRY, &text,
					    -1);
			sel_length = g_utf8_strlen (text, -1);
			g_free (text);
			origin = gtk_tree_model_get_path (GTK_TREE_MODEL(state->model), child);
		}
		dialog_formula_guru_update_this_parent (&iter, state, origin, sel_start,
						   sel_length);
	}
}


static void
dialog_formula_guru_adjust_children (GtkTreeIter *parent, GnmFunc const *fd,
				     FormulaGuruState *state)
{
	gboolean is_non_fun;
	GtkTreeIter iter;
	gint min_arg, max_arg, args = 0, i;
	char *arg_name;


	if (fd == NULL) {
		gtk_tree_model_get (GTK_TREE_MODEL(state->model), parent,
				    IS_NON_FUN, &is_non_fun,
				    FUNCTION, &fd,
				    -1);
		if (is_non_fun) {
			while  (gtk_tree_model_iter_children (GTK_TREE_MODEL(state->model),
						       &iter, parent))
				gtk_tree_store_remove (state->model, &iter);
			return;
		}
	}
	g_return_if_fail (fd != NULL);

	gtk_tree_model_get (GTK_TREE_MODEL(state->model), parent,
			    MIN_ARG, &min_arg,
			    MAX_ARG, &max_arg,
			    -1);
	if (max_arg == G_MAXINT) {
		args = MAX (MIN_VAR_ARGS_DISPLAYED + min_arg,
			    gtk_tree_model_iter_n_children (GTK_TREE_MODEL(state->model),
							    parent));
	} else
		args = max_arg;

	while  (gtk_tree_model_iter_nth_child (GTK_TREE_MODEL(state->model),
					       &iter, parent, args))
		gtk_tree_store_remove (state->model, &iter);
	for (i = 0; i < args; i++) {
		if (!gtk_tree_model_iter_nth_child (GTK_TREE_MODEL(state->model),
						    &iter, parent, i)) {
			gtk_tree_store_append (state->model, &iter, parent);
			gtk_tree_store_set (state->model, &iter,
					    FUN_ARG_ENTRY, "",
					    IS_NON_FUN, TRUE,
					    FUNCTION, NULL,
					    MIN_ARG, 0,
					    MAX_ARG, 0,
					    -1);
		}
		arg_name = function_def_get_arg_name (fd, i);
		if (i >= min_arg && arg_name != NULL) {
			char *mod_name = g_strdup_printf (_("[%s]"), arg_name);
			g_free (arg_name);
			arg_name = mod_name;
		}
		gtk_tree_store_set (state->model, &iter,
				    ARG_NAME, arg_name,
				    ARG_TYPE, function_def_get_arg_type_string (fd, i),
				    -1);
		g_free (arg_name);
	}

	dialog_formula_guru_update_this_parent (parent, state, NULL, 0, 0);
}


static void
dialog_formula_guru_adjust_varargs (GtkTreeIter *iter, FormulaGuruState *state)
{
	GtkTreeIter new_iter, parent;
	char *arg_name, *arg_type;
	gint max_arg;

	new_iter = *iter;
	if (!gtk_tree_model_iter_next (GTK_TREE_MODEL (state->model), &new_iter) &&
	    gtk_tree_model_iter_parent (GTK_TREE_MODEL (state->model), &parent, iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &parent,
				    MAX_ARG, &max_arg,
				    -1);
		if (max_arg == G_MAXINT) {
			gtk_tree_model_get (GTK_TREE_MODEL (state->model), iter,
					    ARG_NAME, &arg_name,
					    ARG_TYPE, &arg_type,
					    -1);
			gtk_tree_store_insert_after (state->model, &new_iter, &parent, iter);
			gtk_tree_store_set (state->model, &new_iter,
					    FUN_ARG_ENTRY, "",
					    IS_NON_FUN, TRUE,
					    FUNCTION, NULL,
					    ARG_NAME, arg_name,
					    ARG_TYPE, arg_type,
					    MIN_ARG, 0,
					    MAX_ARG, 0,
					    -1);
			g_free (arg_name);
			g_free (arg_type);
		}
	}
}


static gint
dialog_formula_guru_load_fd (GtkTreePath *path, GnmFunc const *fd,
			     FormulaGuruState *state)
{
	GtkTreeIter iter;
	TokenizedHelp *help = tokenized_help_new (fd);
	char const *f_syntax = tokenized_help_find (help, "SYNTAX");
	gint min_arg, max_arg;
	GtkTreePath *new_path;

	if (path == NULL) {
		gtk_tree_store_clear (state->model);
		gtk_tree_store_append (state->model, &iter, NULL);
	} else if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model), &iter, path)) {
		GtkTreePath *new_path = gtk_tree_path_copy (path);
		if (gtk_tree_path_prev (new_path) &&
		    gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model),
				      &iter, new_path)) {
			dialog_formula_guru_adjust_varargs (&iter, state);
			if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model),
						      &iter, path)) {
				gtk_tree_store_clear (state->model);
				gtk_tree_path_free (new_path);
				return 0;
			}
		} else {
			gtk_tree_store_clear (state->model);
			gtk_tree_path_free (new_path);
			return 0;
		}
		gtk_tree_path_free (new_path);
	}

	function_def_count_args (fd, &min_arg, &max_arg);

	gtk_tree_store_set (state->model, &iter,
			    FUN_ARG_ENTRY, f_syntax,
			    IS_NON_FUN, FALSE,
			    FUNCTION, fd,
			    MIN_ARG, min_arg,
			    MAX_ARG, max_arg,
			    -1);
	tokenized_help_destroy (help);

	dialog_formula_guru_adjust_children (&iter, fd, state);
	dialog_formula_guru_adjust_varargs (&iter, state);

	new_path = gtk_tree_model_get_path (GTK_TREE_MODEL (state->model),
                                             &iter);
	gtk_tree_view_expand_row (state->treeview, new_path, FALSE);
	gtk_tree_path_free (new_path);

	return max_arg;
}

static void
dialog_formula_guru_load_string (GtkTreePath * path,
				 char const *argument, FormulaGuruState *state)
{
	GtkTreeIter iter;
	gboolean okay = TRUE;

	g_return_if_fail (path != NULL);

	if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model),
				      &iter, path)) {
		GtkTreePath *new_path = gtk_tree_path_copy (path);

		if (gtk_tree_path_prev (new_path) &&
		    gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model),
				      &iter, new_path)) {
			dialog_formula_guru_adjust_varargs (&iter, state);
			okay = gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model),
							&iter, path);
		} else
			okay = FALSE;
		gtk_tree_path_free (new_path);
	}

	g_return_if_fail (okay);

	dialog_formula_guru_delete_children (&iter, state);
	gtk_tree_store_set (state->model, &iter,
			    FUN_ARG_ENTRY, argument ? argument : "",
			    IS_NON_FUN, TRUE,
			    FUNCTION, NULL,
			    MIN_ARG, 0,
			    MAX_ARG, 0,
			    -1);

	dialog_formula_guru_update_parent (&iter, state, gtk_tree_model_get_path
					   (GTK_TREE_MODEL (state->model), &iter),
					   0, argument ? g_utf8_strlen (argument, -1) : 0);
}

static void
dialog_formula_guru_load_expr (GtkTreePath const *parent_path, gint child_num,
			       GnmExpr const *expr, FormulaGuruState *state)
{
	GtkTreePath *path;

	if (parent_path == NULL)
		path = gtk_tree_path_new_first ();
	else {
		/* gtk_tree_path_copy should have a const argument */
		path = gtk_tree_path_copy ((GtkTreePath *) parent_path);
		gtk_tree_path_append_index (path, child_num);
	}

	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_FUNCALL: {
		int i, max_arg;
		GtkTreeIter iter;

		max_arg = dialog_formula_guru_load_fd (path, expr->func.func, state);
		if (max_arg > expr->func.argc)
			max_arg = expr->func.argc;
		for (i = 0; i < max_arg; i++)
			dialog_formula_guru_load_expr (path, i,
						       expr->func.argv[i],
						       state);
		gtk_tree_path_append_index (path, MAX (0, i - 1));
		if (gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model),
                                             &iter, path))
			dialog_formula_guru_adjust_varargs (&iter, state);

		break;
	}

	case GNM_EXPR_OP_ANY_BINARY:
	case GNM_EXPR_OP_UNARY_NEG:
	default: {
		char *text = gnm_expr_as_string (expr, state->pos,
			 sheet_get_conventions (state->pos->sheet));
		dialog_formula_guru_load_string (path, text, state);
		g_free (text);
		break;
	}

	}
	gtk_tree_path_free (path);
}

static void
cb_dialog_formula_guru_destroy (FormulaGuruState *state)
{
	wbcg_edit_finish (state->wbcg, WBC_EDIT_REJECT, NULL);

	if (state->model != NULL)
		g_object_unref (G_OBJECT (state->model));
	g_free (state->prefix);
	g_free (state->suffix);
	g_free (state->pos);
	if (state->editable)
		g_object_unref (state->editable);
	if (state->gui != NULL)
		g_object_unref (G_OBJECT (state->gui));
	g_free (state);
}


static void
cb_dialog_formula_guru_cancel_clicked (FormulaGuruState *state)
{
	wbcg_edit_finish (state->wbcg, WBC_EDIT_REJECT, NULL);
}

static void
cb_dialog_formula_guru_zoom_toggled (GtkWidget *button, FormulaGuruState *state)
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection (state->treeview);
	GtkTreeIter iter;
	GtkTreePath *path;


	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
		gtk_widget_hide (state->main_button_area);
		gtk_widget_hide (state->clear_button);
		gtk_widget_hide (state->selector_button);
		gtk_tree_view_set_headers_visible (state->treeview, FALSE);
		gtk_widget_get_size_request (state->dialog,
					     &state->old_width_request,
					     &state->old_height_request);
		gtk_window_get_size (GTK_WINDOW (state->dialog),
				     &state->old_width,
				     &state->old_height);
		gtk_widget_set_size_request (state->dialog,state->old_width_request,100);

		/* FIXME: the ideal `shrunk size' should probably not be hardcoded.*/
		gtk_window_resize (GTK_WINDOW (state->dialog),state->old_width_request,100);
		gtk_window_set_resizable (GTK_WINDOW (state->dialog), FALSE);
	} else {
		gtk_widget_show (state->main_button_area);
		gtk_widget_show (state->clear_button);
		gtk_widget_show (state->selector_button);
		gtk_tree_view_set_headers_visible (state->treeview, TRUE);
		gtk_window_set_resizable (GTK_WINDOW (state->dialog), TRUE);
		gtk_widget_set_size_request (state->dialog,
					     state->old_width_request,
					     state->old_height_request);
		gtk_window_resize (GTK_WINDOW (state->dialog), state->old_width,
				   state->old_height);
	}
	/* FIXME: this should keep the selection in sight, unfortunately it does not for */
	/* the size reduction case.                                                      */
	if (gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		path = gtk_tree_model_get_path (GTK_TREE_MODEL (state->model), &iter);
		gtk_tree_view_scroll_to_cell (state->treeview, path, NULL,
                                             FALSE, 0, 0);
		gtk_tree_path_free (path);
	}

	return;
}

/**
 * cb_dialog_formula_guru_selector_clicked:
 * @button:
 * @state:
 *
 **/
static void
cb_dialog_formula_guru_selector_clicked (G_GNUC_UNUSED GtkWidget *button,
					 FormulaGuruState *state)
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection (state->treeview);
	GtkTreeModel *model;
	GtkTreeIter iter;

	g_return_if_fail (state->active_path == NULL);

	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		state->active_path = gtk_tree_model_get_path (model, &iter);
		gtk_widget_hide (state->dialog);
		dialog_function_select (state->wbcg, FORMULA_GURU_KEY);
	} else
	    g_warning ("We should never be here!?");

	return;
}

/**
 * cb_dialog_formula_guru_clear_clicked:
 * @button:
 * @state:
 *
 **/
static void
cb_dialog_formula_guru_clear_clicked (G_GNUC_UNUSED GtkWidget *button,
				      FormulaGuruState *state)
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection (state->treeview);
	GtkTreeModel *model;
	GtkTreeIter parent;

	g_return_if_fail (state->active_path == NULL);

	if (gtk_tree_selection_get_selected (selection, &model, &parent)) {
		gtk_tree_store_set (state->model, &parent,
				    FUN_ARG_ENTRY, "",
				    IS_NON_FUN, TRUE,
				    FUNCTION, NULL,
				    MIN_ARG, 0,
				    MAX_ARG, 0,
				    -1);
		dialog_formula_guru_delete_children (&parent, state);
		dialog_formula_guru_update_parent (&parent, state, gtk_tree_model_get_path
					   (GTK_TREE_MODEL (state->model), &parent), 0, 0);
	} else
	    g_warning ("We should never be here!?");
	return;
}

static gboolean
dialog_formula_guru_is_array (FormulaGuruState *state)
{
	return gtk_toggle_button_get_active 
		(GTK_TOGGLE_BUTTON (state->array_button));
}

/**
 * cb_dialog_formula_guru_ok_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_dialog_formula_guru_ok_clicked (G_GNUC_UNUSED GtkWidget *button,
				   FormulaGuruState *state)
{
	if (state->cellrenderer->entry)
		gnumeric_cell_renderer_expr_entry_editing_done (
			GTK_CELL_EDITABLE (state->cellrenderer->entry),
			state->cellrenderer);
	wbcg_edit_finish (state->wbcg, 
			  dialog_formula_guru_is_array (state)
			  ? WBC_EDIT_ACCEPT_ARRAY
			  : WBC_EDIT_ACCEPT, NULL);
}

static void
cb_dialog_formula_guru_selection_changed (GtkTreeSelection *the_selection,
					     FormulaGuruState *state)
{
	GtkTreeIter iter;
	GtkTreeModel *model;

	if (!gtk_tree_selection_get_selected (the_selection, &model, &iter)) {
		gtk_widget_set_sensitive (state->clear_button, FALSE);
		gtk_widget_set_sensitive (state->selector_button, FALSE);
		return;
	}

	gtk_widget_set_sensitive (state->clear_button,
				  0 != gtk_tree_store_iter_depth (state->model,
								  &iter));
	gtk_widget_set_sensitive (state->selector_button, TRUE);
	dialog_formula_guru_update_this_child (&iter, state,
						NULL, 0, 0);
}

/* We shouldn't need that if it weren't for a GTK+ bug*/
static void
cb_dialog_formula_guru_row_collapsed (G_GNUC_UNUSED GtkTreeView *treeview,
				      G_GNUC_UNUSED GtkTreeIter *iter,
				      G_GNUC_UNUSED GtkTreePath *path,
				      FormulaGuruState *state)
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection (state->treeview);

	cb_dialog_formula_guru_selection_changed (selection, state);
}


static void
cb_dialog_formula_guru_edited (G_GNUC_UNUSED GtkCellRendererText *cell,
			       gchar               *path_string,
			       gchar               *new_text,
			       FormulaGuruState    *state)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	gboolean have_iter = FALSE;

	path = gtk_tree_path_new_from_string (path_string);

	have_iter = gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model), 
					     &iter, path);
	gtk_tree_path_free (path);
	if (!have_iter) 
		return;
	gtk_tree_store_set (state->model, &iter, FUN_ARG_ENTRY, new_text, -1);

	if (g_utf8_strlen (new_text, -1) > 0)
		dialog_formula_guru_adjust_varargs (&iter, state);


	dialog_formula_guru_update_parent (&iter, state, gtk_tree_model_get_path
					   (GTK_TREE_MODEL (state->model), &iter),
					   0, g_utf8_strlen (new_text, -1));
}

static void
cb_dialog_formula_guru_editing_started (GtkCellRenderer *cell,
					GtkCellEditable *editable,
					const gchar *path,
					FormulaGuruState *state)
{
	g_object_ref (editable);
	if (state->editable)
		g_object_unref (state->editable);
	state->editable = editable;
}

/* Bad bad hack to be removed with Gtk2.2 */
/* The idea to this code is due to Jonathan Blandford */

typedef struct
{
	GtkTreePath *path;
	FormulaGuruState *state;
} IdleData;

static gboolean
real_start_editing_cb (IdleData *idle_data)
{
	FormulaGuruState *state = idle_data->state;
	GtkTreePath *path = idle_data->path;

	if (state->editable)
		gtk_cell_editable_editing_done (state->editable);

	gtk_widget_grab_focus (GTK_WIDGET (state->treeview));
	gtk_tree_view_set_cursor (state->treeview,
				  path,
				  state->column,
				  TRUE);

	gtk_tree_path_free (path);
	g_free (idle_data);
	return FALSE;
}

static gboolean
start_editing_cb (GtkTreeView      *tree_view,
		  GdkEventButton   *event,
		  FormulaGuruState *state)
{
	GtkTreePath *path;
	GtkTreeIter iter;

	if (event->window != gtk_tree_view_get_bin_window (tree_view))
		return FALSE;
	if (state->treeview != tree_view)
		return FALSE;

	if (gtk_tree_view_get_path_at_pos (tree_view,
					   (gint) event->x,
					   (gint) event->y,
					   &path, NULL,
					   NULL, NULL) &&
	    gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model),
				     &iter, path))
	{
		IdleData *idle_data;
		gboolean is_non_fun;

		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &iter,
				    IS_NON_FUN, &is_non_fun,
				    -1);

		if (!is_non_fun) {
			gtk_tree_path_free (path);
			return FALSE;
		}

		idle_data = g_new (IdleData, 1);
		idle_data->path = path;
		idle_data->state = state;

		g_signal_stop_emission_by_name (G_OBJECT (tree_view), "button_press_event");
		g_idle_add ((GSourceFunc) real_start_editing_cb, idle_data);
		return TRUE;
	}
	return FALSE;
}

/* End of bad bad hack*/

static gboolean
dialog_formula_guru_init (FormulaGuruState *state)
{
	GtkWidget *scrolled;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;
	GtkCellRenderer *renderer;

	g_object_set_data (G_OBJECT (state->dialog), FORMULA_GURU_KEY_DIALOG,
			   state);

	/* Set-up treeview */
	scrolled = glade_xml_get_widget (state->gui, "scrolled");
	state->model = gtk_tree_store_new (NUM_COLMNS, G_TYPE_STRING, G_TYPE_BOOLEAN,
					   G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_INT, G_TYPE_INT, G_TYPE_POINTER);
	state->treeview = GTK_TREE_VIEW (
		gtk_tree_view_new_with_model (GTK_TREE_MODEL (state->model)));
	g_signal_connect (state->treeview,
		"row_collapsed",
		G_CALLBACK (cb_dialog_formula_guru_row_collapsed), state);
	selection = gtk_tree_view_get_selection (state->treeview);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	g_signal_connect (selection,
		"changed",
		G_CALLBACK (cb_dialog_formula_guru_selection_changed), state);

	column = gtk_tree_view_column_new_with_attributes (_("Name"),
							   gnumeric_cell_renderer_text_new (),
							   "text", ARG_NAME, NULL);
	gtk_tree_view_append_column (state->treeview, column);
	column = gtk_tree_view_column_new_with_attributes (_("Type"),
							   gnumeric_cell_renderer_text_new (),
							   "text", ARG_TYPE, NULL);
	gtk_tree_view_append_column (state->treeview, column);
	renderer = gnumeric_cell_renderer_expr_entry_new (state->wbcg);
	state->cellrenderer = GNUMERIC_CELL_RENDERER_EXPR_ENTRY (renderer);
	g_signal_connect (G_OBJECT (renderer), "edited",
			  G_CALLBACK (cb_dialog_formula_guru_edited), state);
	state->editable = NULL;
	g_signal_connect (G_OBJECT (renderer), "editing-started",
			  G_CALLBACK (cb_dialog_formula_guru_editing_started), state);
	column = gtk_tree_view_column_new_with_attributes (_("Function/Argument"),
							   renderer,
							   "text", FUN_ARG_ENTRY,
							   "editable", IS_NON_FUN,
							   NULL);
	state->column = column;
	gtk_tree_view_append_column (state->treeview, column);
	gtk_tree_view_set_headers_visible (state->treeview, TRUE);
	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (state->treeview));

	g_signal_connect (state->treeview,
			  "button_press_event",
			  G_CALLBACK (start_editing_cb), state),

	/* Finished set-up of treeview */

	state->array_button = glade_xml_get_widget (state->gui, 
						    "array_button");
	gtk_widget_set_sensitive (state->array_button, TRUE);

	state->ok_button = glade_xml_get_widget (state->gui, "ok_button");
	gtk_widget_set_sensitive (state->ok_button, TRUE);
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_dialog_formula_guru_ok_clicked), state);

	state->selector_button = glade_xml_get_widget (state->gui, "select_func");
	gtk_widget_set_sensitive (state->selector_button, FALSE);
	g_signal_connect (G_OBJECT (state->selector_button),
		"clicked",
		G_CALLBACK (cb_dialog_formula_guru_selector_clicked), state);

	state->clear_button = glade_xml_get_widget (state->gui, "trash");
	gtk_widget_set_sensitive (state->clear_button, FALSE);
	g_signal_connect (G_OBJECT (state->clear_button),
		"clicked",
		G_CALLBACK (cb_dialog_formula_guru_clear_clicked), state);

	state->zoom_button = glade_xml_get_widget (state->gui, "zoom");
	gtk_widget_set_sensitive (state->zoom_button, TRUE);
	g_signal_connect (G_OBJECT (state->zoom_button),
		"toggled",
		G_CALLBACK (cb_dialog_formula_guru_zoom_toggled), state);

	state->main_button_area = glade_xml_get_widget (state->gui, "dialog-action_area2");

	g_signal_connect_swapped (G_OBJECT (glade_xml_get_widget (state->gui, "cancel_button")),
		"clicked",
		G_CALLBACK (cb_dialog_formula_guru_cancel_clicked), state);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_FORMULA_GURU);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_dialog_formula_guru_destroy);

	return FALSE;
}

static void
dialog_formula_guru_show (FormulaGuruState *state)
{
	GtkTreeIter iter;

	if ((!gtk_tree_model_get_iter_first   (GTK_TREE_MODEL (state->model), &iter)) ||
	    gtk_tree_model_iter_n_children (GTK_TREE_MODEL(state->model), &iter) == 0)
		wbcg_edit_finish (state->wbcg, WBC_EDIT_ACCEPT, NULL);
	else
		gtk_widget_show_all (state->dialog);
}

/**
 * dialog_formula_guru
 * @wbcg : The workbook to use as a parent window.
 *
 * Pop up a function selector then a formula guru.
 */
void
dialog_formula_guru (WBCGtk *wbcg, GnmFunc const *fd)
{
	SheetView *sv;
	GladeXML  *gui;
	GnmCell	  *cell;
	GtkWidget *dialog;
	FormulaGuruState *state;
	GnmExpr const *expr = NULL;

	g_return_if_fail (wbcg != NULL);

	dialog = gnumeric_dialog_raise_if_exists (wbcg, FORMULA_GURU_KEY);

	if (dialog) {
		/* We already exist */
		state = g_object_get_data (G_OBJECT (dialog),
					   FORMULA_GURU_KEY_DIALOG);
		if (fd) {
			if (state->active_path) {
				dialog_formula_guru_load_fd (state->active_path, fd, state);
				gtk_tree_path_free (state->active_path);
				state->active_path = NULL;
			} else
				dialog_formula_guru_load_fd (NULL, fd, state);
			dialog_formula_guru_show (state);
		} else {
			if (state->active_path) {
				gtk_tree_path_free (state->active_path);
				state->active_path = NULL;
			}

			if (0 == gtk_tree_model_iter_n_children (GTK_TREE_MODEL(state->model),
								 NULL))
				gtk_widget_destroy (state->dialog);
			else
				dialog_formula_guru_show (state);
		}
		return;
	}

	/* Get the dialog and check for errors */
	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"formula-guru.glade", NULL, NULL);
	if (gui == NULL)
		return;

	state = g_new (FormulaGuruState, 1);
	state->wbcg  = wbcg;
	state->wb    = wb_control_get_workbook (WORKBOOK_CONTROL (wbcg));
	state->gui   = gui;
	state->active_path = NULL;
	state->pos = NULL;

	sv = wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg));
	cell = sheet_cell_get (sv_sheet (sv), sv->edit_pos.col, sv->edit_pos.row);
	if (cell != NULL && gnm_cell_has_expr (cell))
		expr = gnm_expr_top_first_funcall (cell->base.texpr);

	if (expr == NULL) {
		wbcg_edit_start (wbcg, TRUE, TRUE);
		state->prefix = g_strdup ("=");
		state->suffix = NULL;
	} else {
		char const *sub_str;
		char const *full_str = gtk_entry_get_text (wbcg_get_entry (wbcg));
		char *func_str;

		state->pos = g_new (GnmParsePos, 1);
		func_str = gnm_expr_as_string (expr,
			 parse_pos_init_cell (state->pos, cell),
			 sheet_get_conventions (sv_sheet (sv)));

		wbcg_edit_start (wbcg, FALSE, TRUE);
		fd = gnm_expr_get_func_def (expr);

		sub_str = strstr (full_str, func_str);

		g_return_if_fail (sub_str != NULL);

		state->prefix = g_strndup (full_str, sub_str - full_str);
		state->suffix = g_strdup (sub_str + strlen (func_str));
		g_free (func_str);
	}

	state->dialog = glade_xml_get_widget (state->gui, "formula_guru");

	if (dialog_formula_guru_init (state)) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				 _("Could not create the formula guru."));
		g_free (state);
		return;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       FORMULA_GURU_KEY);

	gtk_widget_show_all (GTK_DIALOG (state->dialog)->vbox);
	gtk_widget_realize (state->dialog);

	if (fd == NULL) {
		dialog_function_select (wbcg, FORMULA_GURU_KEY);
		return;
	}

	if (expr == NULL)
		dialog_formula_guru_load_fd (NULL, fd, state);
	else {
		GtkTreeIter iter;
		gtk_tree_store_append (state->model, &iter, NULL);
		dialog_formula_guru_load_expr (NULL, 0, expr, state);
	}

	gtk_widget_show_all (state->dialog);
}
