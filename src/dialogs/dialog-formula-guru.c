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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <parse-util.h>
#include <gui-util.h>
#include <workbook.h>
#include <sheet.h>
#include <workbook-edit.h>
#include <workbook-control.h>
#include <cell.h>
#include <expr.h>
#include <expr-impl.h>
#include <func.h>
#include <format.h>
#include <widgets/gnumeric-expr-entry.h>
#include <widgets/gnumeric-cell-renderer-expr-entry.h>

#include <gdk/gdkkeysyms.h>
#include <locale.h>
#include <string.h>

#define GLADE_FILE "formula-guru.glade"

#define FORMULA_GURU_KEY "formula-guru-dialog"
#define FORMULA_GURU_KEY_DIALOG "formula-guru-dialog"

#define MIN_VAR_ARGS_DISPLAYED 2
#define AV_FORMULA_SIZE 100

typedef struct
{
	WorkbookControlGUI  *wbcg;
	Workbook  *wb;

	GladeXML  *gui;
	GtkWidget *dialog;
	GtkWidget *ok_button;
	GtkWidget *selector_button;
	GtkWidget *clear_button;
	GtkWidget *zoom_button;
	GtkWidget *main_button_area;
	GtkTreePath* active_path;
	char * prefix;
	char * suffix;
	ParsePos  *pos;

	GtkTreeStore  *model;
	GtkTreeView   *treeview;

	gint old_height;
	gint old_width;
	gint old_height_request;
	gint old_width_request;

	GnumericCellRendererExprEntry *cellrenderer;
	GtkTreeViewColumn *column;
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
	FunctionDefinition const *fd;
	GtkTreeIter iter;
	char *argument;
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

	text = g_string_append (text, function_def_get_name (fd));
	text = g_string_append (text, " (");

	if (gtk_tree_model_iter_children (GTK_TREE_MODEL(state->model), &iter, parent)) {
		do {
			gtk_tree_model_get (GTK_TREE_MODEL(state->model), &iter,
					    FUN_ARG_ENTRY, &argument,
					    -1);
			if ((argument == NULL  || g_utf8_strlen (argument, -1) == 0) && arg_num > arg_min)
				break;
			if (not_first) {
				text = g_string_append_c (text, format_get_arg_sep ());
				text = g_string_append_c (text, ' ');
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
dialog_formula_guru_adjust_children (GtkTreeIter *parent, FunctionDefinition const *fd,
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


static void
dialog_formula_guru_load_fd (GtkTreePath *path, FunctionDefinition const *fd, 
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
				return;
			}
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
	char *text;
	GSList *args;
	GtkTreeIter iter;
	int i;

	if (parent_path == NULL)
		path = gtk_tree_path_new_first ();
	else {
		/* gtk_tree_path_copy should have a const argument */
		path = gtk_tree_path_copy ((GtkTreePath *) parent_path);
		gtk_tree_path_append_index (path, child_num);
	}

	switch (expr->any.oper) {
	case GNM_EXPR_OP_FUNCALL:
		dialog_formula_guru_load_fd (path, expr->func.func, state);		
		for (args = expr->func.arg_list, i = 0; args; args = args->next, i++)
			dialog_formula_guru_load_expr (path, i, 
						       (GnmExpr const *) args->data, 
						       state);
		gtk_tree_path_append_index (path, i - 1);
		if (gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model),
                                             &iter, path))
			dialog_formula_guru_adjust_varargs (&iter, state);
		
		break;
	case GNM_EXPR_OP_ANY_BINARY:
	case GNM_EXPR_OP_UNARY_NEG:
	default:
		text = gnm_expr_as_string (expr, state->pos);
		dialog_formula_guru_load_string (path, text, state);
		g_free (text);
		break;
		
	}
	gtk_tree_path_free (path);
}


/**
 * dialog_function_select_destroy:
 * @window:
 * @state:
 *
 * Destroy the dialog and associated data structures.
 *
 **/
static gboolean
dialog_formula_guru_destroy (GtkObject *w, FormulaGuruState *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	wbcg_edit_finish (state->wbcg, FALSE);
	wbcg_edit_detach_guru (state->wbcg);

	g_free (state->prefix);
	state->prefix = NULL;
	g_free (state->suffix);
	state->suffix = NULL;
	g_free (state->pos);
	state->pos = NULL;

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}
	state->dialog = NULL;
	g_free (state);

	return FALSE;
}


/**
 * cb_dialog_formula_guru_cancel_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_dialog_formula_guru_cancel_clicked (GtkWidget *button, FormulaGuruState *state)
{
	wbcg_edit_finish (state->wbcg, FALSE);
	return;
}

/**
 * cb_dialog_formula_guru_zoom_toggled:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
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
cb_dialog_formula_guru_selector_clicked (GtkWidget *button, FormulaGuruState *state)
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
cb_dialog_formula_guru_clear_clicked (GtkWidget *button, FormulaGuruState *state)
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


/**
 * cb_dialog_formula_guru_ok_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_dialog_formula_guru_ok_clicked (GtkWidget *button, FormulaGuruState *state)
{
	if (state->cellrenderer->entry)
		gnumeric_cell_renderer_expr_entry_editing_done (
			GTK_CELL_EDITABLE (state->cellrenderer->entry),
			state->cellrenderer);
	wbcg_edit_finish (state->wbcg, TRUE);
	return;
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

/* We shouln't need that if it weren't for a GTK+ bug*/
static void        
cb_dialog_formula_guru_row_collapsed (GtkTreeView *treeview, GtkTreeIter *iter,
				      GtkTreePath *path, FormulaGuruState *state)
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection (state->treeview);

	cb_dialog_formula_guru_selection_changed (selection, state);
}


static void
cb_dialog_formula_guru_edited (GtkCellRendererText *cell,
	gchar               *path_string,
	gchar               *new_text,
        FormulaGuruState    *state)
{
	GtkTreeIter iter;
	GtkTreePath *path;

	path = gtk_tree_path_new_from_string (path_string);
	
	gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model), &iter, path);
	gtk_tree_store_set (state->model, &iter, FUN_ARG_ENTRY, new_text, -1);
	
	if (g_utf8_strlen (new_text, -1) > 0)
		dialog_formula_guru_adjust_varargs (&iter, state);

	gtk_tree_path_free (path);

	dialog_formula_guru_update_parent (&iter, state, gtk_tree_model_get_path 
					   (GTK_TREE_MODEL (state->model), &iter), 
					   0, g_utf8_strlen (new_text, -1));
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
  gtk_widget_grab_focus (GTK_WIDGET (idle_data->state->treeview));
  gtk_tree_view_set_cursor (idle_data->state->treeview,
			    idle_data->path,
			    idle_data->state->column,
			    TRUE);

  gtk_tree_path_free (idle_data->path);
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

      if (!is_non_fun)
	      return FALSE;

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

	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "cancel_button")),
		"clicked",
		G_CALLBACK (cb_dialog_formula_guru_cancel_clicked), state);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		"cell-sort.html");

	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (dialog_formula_guru_destroy), state);

	wbcg_edit_attach_guru (state->wbcg, state->dialog);

	return FALSE;
}

static void
dialog_formula_guru_show (FormulaGuruState *state)
{
	GtkTreeIter iter;

	if ((!gtk_tree_model_get_iter_first   (GTK_TREE_MODEL (state->model), &iter)) || 
	    gtk_tree_model_iter_n_children (GTK_TREE_MODEL(state->model), &iter) == 0)
		wbcg_edit_finish (state->wbcg, TRUE);			
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
dialog_formula_guru (WorkbookControlGUI *wbcg, FunctionDefinition const *fd)
{
	SheetView *sv;
	Cell	  *cell;
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

	state = g_new (FormulaGuruState, 1);
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->active_path = NULL;
	state->pos = NULL;
	
	sv = wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg));
	cell = sheet_cell_get (sv_sheet (sv), sv->edit_pos.col, sv->edit_pos.row);
	if (cell != NULL && cell_has_expr (cell))
		expr = gnm_expr_first_func (cell->base.expression);

	if (expr == NULL) {
		wbcg_edit_start (wbcg, TRUE, TRUE);
		state->prefix = g_strdup ("= ");
		state->suffix = NULL;
	} else {
		char const *sub_str;
		char const *full_str = gtk_entry_get_text (wbcg_get_entry (wbcg));
		char *func_str;
		
		state->pos = g_new (ParsePos, 1);
		func_str = gnm_expr_as_string (expr,
			parse_pos_init_cell (state->pos, cell));

		wbcg_edit_start (wbcg, FALSE, TRUE);
		fd = gnm_expr_get_func_def (expr);

		sub_str = strstr (full_str, func_str);

		g_return_if_fail (sub_str != NULL);
		
		state->prefix = g_strndup (full_str, sub_str - full_str);
		state->suffix = g_strdup (sub_str + strlen (func_str));
		g_free (func_str);		
	}

	/* Get the dialog and check for errors */
	state->gui = gnumeric_glade_xml_new (wbcg, GLADE_FILE);
	if (state->gui == NULL) {
		g_warning ("glade file missing or corrupted");
			g_free (state);
			return;
	}
	
	state->dialog = glade_xml_get_widget (state->gui, "formula_guru");
	
	if (dialog_formula_guru_init (state)) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
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
	return;
}
