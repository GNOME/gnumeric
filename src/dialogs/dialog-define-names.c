/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* vim: set sw=8: */
/*
 * dialog-define-name.c: Edit named regions.
 *
 * Author:
 *	Jody Goldberg <jody@gnome.org>
 *	Michael Meeks <michael@imaginator.com>
 *	Chema Celorio <chema@celorio.com>
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

#include <expr.h>
#include <str.h>
#include <expr-name.h>
#include <sheet.h>
#include <sheet-view.h>
#include <workbook.h>
#include <workbook-control.h>
#include <workbook-edit.h>
#include <gui-util.h>
#include <parse-util.h>
#include <value.h>
#include <commands.h>
#include <widgets/gnumeric-expr-entry.h>

#include <glade/glade.h>

#define DEFINE_NAMES_KEY "define-names-dialog"

typedef struct {
	GladeXML		*gui;
	GtkWidget		*dialog;
	GtkWidget		*treeview;
	GtkListStore		*model;
	GtkTreeSelection	*selection;
	GtkEntry		*name;
	GnumericExprEntry	*expr_entry;
	GtkToggleButton		*sheet_scope, *wb_scope;

	GtkWidget *ok_button;
	GtkWidget *add_button;
	GtkWidget *close_button;
	GtkWidget *delete_button;
	GtkWidget *update_button;

	Sheet			*sheet;
	SheetView		*sv;
	Workbook		*wb;
	WorkbookControlGUI	*wbcg;
	GList			*expr_names;
	GnmNamedExpr		*cur_name;
	ParsePos		 pp;
	gboolean	 	 updating;
} NameGuruState;

static gboolean
name_guru_scope_is_sheet (NameGuruState *state)
{
	return gtk_toggle_button_get_active (state->sheet_scope);
}

/**
 * name_guru_warned_if_used:
 * @state:
 *
 * If the expresion that is about to be deleted is beeing used,
 * warn the user about it. Ask if we should procede or not
 *
 * Return Value: TRUE if users confirms deletion, FALSE otherwise
 **/
static gboolean
name_guru_warn (NameGuruState *state)
{
	return TRUE;
}

static void
cb_scope_changed (GtkToggleButton *button, NameGuruState *state)
{
	if (state->updating || state->cur_name == NULL)
		return;
	expr_name_set_scope (state->cur_name,
		name_guru_scope_is_sheet (state) ? state->sheet : NULL);
}

static void
name_guru_display_scope (NameGuruState *state)
{
	GnmNamedExpr const *nexpr = state->cur_name;

	state->updating = TRUE;
	if (nexpr == NULL || nexpr->pos.sheet == NULL)
		gtk_toggle_button_set_active (state->wb_scope, TRUE);
	else
		gtk_toggle_button_set_active (state->sheet_scope, TRUE);
	state->updating = FALSE;
}

/**
 * name_guru_set_expr:
 * @state:
 * @nexpr: Expression to set in the entries, NULL to clear entries
 *
 * Set the entries in the dialog from an GnmNamedExpr
 **/
static void
name_guru_set_expr (NameGuruState *state, GnmNamedExpr *nexpr)
{
	state->updating = TRUE;
	if (nexpr) {
		char *txt = expr_name_as_string (nexpr, &state->pp);
		gnm_expr_entry_load_from_text  (state->expr_entry, txt);
		g_free (txt);
		gtk_entry_set_text (state->name, nexpr->name->str);
	} else {
		gnm_expr_entry_load_from_text (state->expr_entry, "");
		gtk_entry_set_text (state->name, "");
	}
	state->updating = FALSE;

	name_guru_display_scope (state);
}


static GnmNamedExpr *
name_guru_in_list (NameGuruState *state, char const *name,
		   gboolean ignore_placeholders)
{
	GnmNamedExpr *nexpr;
	GList *list;

	for (list = state->expr_names; list; list = list->next) {
		nexpr = (GnmNamedExpr *) list->data;

		g_return_val_if_fail (nexpr != NULL, NULL);
		g_return_val_if_fail (nexpr->name != NULL, NULL);
		g_return_val_if_fail (nexpr->name->str != NULL, NULL);

		if (ignore_placeholders && expr_name_is_placeholder (nexpr))
			continue;

		/* no need for utf8 or collation magic, just equality */
		if (strcmp (name, nexpr->name->str) == 0)
			return nexpr;
	}

	return NULL;
}



/**
 * name_guru_update_sensitivity:
 * @state:
 * @update_entries:
 *
 * Update the dialog widgets sensitivity
 **/
static void
name_guru_update_sensitivity (NameGuruState *state, gboolean update_entries)
{
	gboolean selection;
	gboolean update;
	gboolean add;
	GnmNamedExpr *in_list = NULL;
	char const *name;

	if (state->updating)
		return;

	name  = gtk_entry_get_text (state->name);

	/** Add is active if :
	 *  - We have a name in the entry to add
	 *  - Either we don't have a current Name or if we have a current
	 *     name, the name is different than what we are going to add
	 **/
	in_list = name_guru_in_list (state, name, TRUE);
	add    = name != NULL && name[0] != '\0' && in_list == NULL;
	update = name != NULL && name[0] != '\0' && !add;
	selection = gtk_tree_selection_get_selected (state->selection, NULL, NULL);

	gtk_widget_set_sensitive (state->delete_button, selection && in_list != NULL);
	gtk_widget_set_sensitive (state->add_button,    add);
	gtk_widget_set_sensitive (state->update_button, update);

	if (!selection && update_entries)
		name_guru_set_expr (state, NULL);

	if (selection && in_list == NULL) {
		state->updating = TRUE;
		gtk_tree_selection_unselect_all (state->selection);
		state->updating = FALSE;
	}
}

/**
 * cb_name_guru_update_sensitivity:
 * @dummy:
 * @state:
 *
 **/
static void
cb_name_guru_update_sensitivity (GtkWidget *dummy, NameGuruState *state)
{
	name_guru_update_sensitivity (state, FALSE);
}

static void
cb_name_guru_select_name (GtkTreeSelection *ignored, NameGuruState *state)
{
	GnmNamedExpr *nexpr;
	GtkTreeIter  iter;

	if (state->updating ||
	    !gtk_tree_selection_get_selected (state->selection, NULL, &iter))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (state->model), &iter, 1, &nexpr, -1);

	g_return_if_fail (nexpr != NULL);
	g_return_if_fail (nexpr->name != NULL);
	g_return_if_fail (nexpr->name->str != NULL);

	state->cur_name = nexpr;

	name_guru_set_expr (state, nexpr);
	name_guru_update_sensitivity (state, FALSE);
}


static void
name_guru_populate_list (NameGuruState *state)
{
	GnmNamedExpr	*nexpr;
	GList		*ptr;
	GtkTreeIter	 iter;

	g_return_if_fail (state != NULL);
	g_return_if_fail (state->treeview != NULL);

	state->cur_name = NULL;

	gtk_list_store_clear (state->model);

	g_list_free (state->expr_names);
	state->expr_names =
		g_list_sort (sheet_names_get_available (state->sheet),
			     (GCompareFunc)expr_name_by_name);

	for (ptr = state->expr_names ; ptr != NULL ; ptr = ptr->next) {
		nexpr = ptr->data;

		/* ignore placeholders for unknown names */
		if (expr_name_is_placeholder (nexpr))
			continue;

		gtk_list_store_append (state->model, &iter);
		if (nexpr->pos.sheet != NULL) {
			char *name = g_strdup_printf ("%s!%s",
						      nexpr->pos.sheet->name_unquoted,
						      nexpr->name->str);
			gtk_list_store_set (state->model,
					    &iter, 0, name, 1, nexpr, -1);
			g_free (name);
		} else
			gtk_list_store_set (state->model,
					    &iter, 0, nexpr->name->str, 1, nexpr, -1);
	}
	name_guru_update_sensitivity (state, TRUE);
}

/**
 * name_guru_remove:
 * @ignored:
 * @state:
 *
 * Remove the state->cur_name
 **/
static void
name_guru_remove (GtkWidget *ignored, NameGuruState *state)
{
	g_return_if_fail (state != NULL);
	g_return_if_fail (state->cur_name != NULL);

	if (!name_guru_warn (state))
		return;

	state->expr_names = g_list_remove (state->expr_names, state->cur_name);
	expr_name_remove (state->cur_name);
	state->cur_name = NULL;

	name_guru_populate_list (state);
}

/**
 * name_guru_add:
 * @state:
 *
 * Update or add a GnmNamedExpr from the values in the gtkentries.
 *
 * Return Value: FALSE if the expression was invalid, TRUE otherwise
 **/
static gboolean
name_guru_add (NameGuruState *state)
{
	GnmNamedExpr *nexpr;
	GnmExpr	const *expr;
	ParseError	 perr;
	char const *name;

	g_return_val_if_fail (state != NULL, FALSE);

	name  = gtk_entry_get_text (state->name);

	if (!name || (name[0] == '\0'))
		return TRUE;

	expr = gnm_expr_entry_parse (state->expr_entry,
		&state->pp, parse_error_init (&perr), FALSE);
	if (expr == NULL) {
		gnumeric_notice (state->wbcg, GTK_MESSAGE_ERROR, perr.err->message);
		gtk_widget_grab_focus (GTK_WIDGET (state->expr_entry));
		parse_error_free (&perr);
		return FALSE;
	}

	/* don't allow user to define a nexpr that looks like a placeholder
	 * because it will be would disappear from the lists.
	 */
	if (gnm_expr_is_err (expr, gnumeric_err_NAME)) {
		gnumeric_notice (state->wbcg, GTK_MESSAGE_ERROR,
			_("Why would you want to define a name to be #NAME?"));
		gtk_widget_grab_focus (GTK_WIDGET (state->expr_entry));
		parse_error_free (&perr);
		return FALSE;
	}

	nexpr = expr_name_lookup (&state->pp, name);
	if (nexpr) {
		/* This means that the expresion was updated.
		 * FIXME: if the scope has been changed too, call scope
		 * changed first.
		 */
		cmd_define_name (WORKBOOK_CONTROL (state->wbcg), name,
				 &state->pp, expr, nexpr);
	} else {
		ParsePos pp;
		parse_pos_init (&pp, NULL, state->sheet,
			state->pp.eval.col, state->pp.eval.row);
		if (!name_guru_scope_is_sheet (state))
			pp.sheet = NULL;
		cmd_define_name (WORKBOOK_CONTROL (state->wbcg), name,
				 &pp, expr, NULL);
	}
	
	name_guru_populate_list (state);
	gtk_widget_grab_focus (GTK_WIDGET (state->name));

	return TRUE;
}

static void
cb_name_guru_clicked (GtkWidget *button, NameGuruState *state)
{
	if (state->dialog == NULL)
		return;

	wbcg_set_entry (state->wbcg, NULL);

	if (button == state->delete_button) {
		name_guru_remove (NULL, state);
		return;
	}

	if (button == state->add_button ||
	    button == state->update_button ||
	    button == state->ok_button) {
		/* If adding the name failed, do not exit */
		if (!name_guru_add (state)) {
			return;
		}
	}

	if (button == state->close_button || button == state->ok_button) {
		gtk_widget_destroy (state->dialog);
		return;
	}

}

static GtkWidget *
name_guru_init_button (NameGuruState *state, char const *name)
{
	GtkWidget *tmp = glade_xml_get_widget (state->gui, name);

	g_return_val_if_fail (tmp != NULL, NULL);

	g_signal_connect (G_OBJECT (tmp),
		"clicked",
		G_CALLBACK (cb_name_guru_clicked), state);
	return tmp;
}

static void
cb_name_guru_destroy (NameGuruState *state)
{
	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	wbcg_edit_finish (state->wbcg, FALSE);

	state->dialog = NULL;

	g_list_free (state->expr_names);
	state->expr_names = NULL;

	g_free (state);
}

static void
cb_entry_activate (GtkWidget *item, NameGuruState *state)
{
	char const *name = gtk_entry_get_text (state->name);

	if (name == NULL || *name == '\0' ||
	    gnm_expr_entry_is_blank (state->expr_entry))
		gtk_widget_destroy (state->dialog);
	name_guru_add (state);
}

static gboolean
name_guru_init (NameGuruState *state, WorkbookControlGUI *wbcg)
{
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	GtkTable *table2;
	GtkTreeViewColumn *column;

	state->wbcg  = wbcg;
	state->wb   = wb;
	state->sv = wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg));
	state->sheet = sv_sheet (state->sv);
	state->gui = gnumeric_glade_xml_new (state->wbcg, "names.glade");
        if (state->gui == NULL)
                return TRUE;

	parse_pos_init (&state->pp, state->wb, state->sheet,
			state->sv->edit_pos.col,
			state->sv->edit_pos.row);

	state->dialog = glade_xml_get_widget (state->gui, "NameGuru");
	table2 = GTK_TABLE (glade_xml_get_widget (state->gui, "table2"));
	state->name  = GTK_ENTRY (glade_xml_get_widget (state->gui, "name"));
	state->expr_entry = gnumeric_expr_entry_new (state->wbcg, TRUE);
	gtk_table_attach (table2, GTK_WIDGET (state->expr_entry),
			  1, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gnm_expr_entry_set_absolute (state->expr_entry);
	gtk_widget_show (GTK_WIDGET (state->expr_entry));
	state->sheet_scope = GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "sheet_scope"));
	state->wb_scope = GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "workbook_scope"));
	state->expr_names = NULL;
	state->cur_name   = NULL;
	state->updating   = FALSE;

	state->model	 = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_POINTER); 
	state->treeview  = glade_xml_get_widget (state->gui, "name_list");
	gtk_tree_view_set_model (GTK_TREE_VIEW (state->treeview),
				 GTK_TREE_MODEL (state->model));
	column = gtk_tree_view_column_new_with_attributes (_("Name"),
			gtk_cell_renderer_text_new (),
			"text", 0, 
			NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (state->treeview), column);

	state->selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (state->treeview));
	gtk_tree_selection_set_mode (state->selection, GTK_SELECTION_BROWSE);

	gtk_label_set_text (GTK_LABEL (GTK_BIN (state->sheet_scope)->child),
		state->sheet->name_unquoted);
	name_guru_display_scope (state);
	g_signal_connect (G_OBJECT (state->sheet_scope),
		"toggled",
		G_CALLBACK (cb_scope_changed), state);

	state->ok_button     = name_guru_init_button (state, "ok_button");
	state->close_button  = name_guru_init_button (state, "close_button");
	state->add_button    = name_guru_init_button (state, "add_button");
	state->delete_button = name_guru_init_button (state, "delete_button");
	state->update_button = name_guru_init_button (state, "update_button");

	g_signal_connect (G_OBJECT (state->selection),
		"changed",
		G_CALLBACK (cb_name_guru_select_name), state);
	g_signal_connect (G_OBJECT (state->name),
		"changed",
		G_CALLBACK (cb_name_guru_update_sensitivity), state);
	g_signal_connect (G_OBJECT (state->name),
		"activate",
		G_CALLBACK (cb_entry_activate), state);

	/* We need to connect after because this is an expresion, and it will
	 * be changed by the mouse selecting a range, update after the entry
	 * is updated with the new text.
	 */
	g_signal_connect_after (G_OBJECT (state->expr_entry),
		"changed",
		G_CALLBACK (cb_name_guru_update_sensitivity), state);
	g_signal_connect (G_OBJECT (gnm_expr_entry_get_entry (state->expr_entry)),
		"activate",
		G_CALLBACK (cb_entry_activate), state);

	gnm_expr_entry_set_scg (state->expr_entry, wbcg_cur_scg (wbcg));

	name_guru_populate_list (state);

	/* a candidate for merging into attach guru */
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       DEFINE_NAMES_KEY);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_name_guru_destroy);
	gnumeric_non_modal_dialog (state->wbcg, GTK_WINDOW (state->dialog));
	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gtk_widget_show_all (GTK_WIDGET (state->dialog));

	return FALSE;
}

/**
 * dialog_define_names:
 * @wbcg:
 *
 * Create and show the define names dialog.
 **/
void
dialog_define_names (WorkbookControlGUI *wbcg)
{
	NameGuruState *state;

	g_return_if_fail (wbcg != NULL);

	/* Only one guru per workbook. */
	if (wbcg_edit_has_guru (wbcg))
		return;

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, DEFINE_NAMES_KEY))
		return;

	state = g_new0 (NameGuruState, 1);
	if (name_guru_init (state, wbcg)) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
				 _("Could not create the Name Guru."));
		g_free (state);
		return;
	}
}
