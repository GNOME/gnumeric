/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* vim: set sw=8: */
/*
 * dialog-define-name.c: Edit named regions.
 *
 * Author:
 *	Jody Goldberg <jody@gnome.org>
 *	Michael Meeks <michael@imaginator.com>
 *	Chema Celorio <chema@celorio.com>
 */
#include <config.h>
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>
#include "dialogs.h"
#include "expr.h"
#include "str.h"
#include "expr-name.h"
#include "sheet.h"
#include "workbook.h"
#include "workbook-control.h"
#include "workbook-edit.h"
#include "gui-util.h"
#include "parse-util.h"
#include "widgets/gnumeric-expr-entry.h"

#define LIST_KEY "name_list_data"
#define DEFINE_NAMES_KEY "define-names-dialog"

typedef struct {
	GladeXML  *gui;
	GtkWidget *dialog;
	GtkList   *list;
	GtkEntry  *name;
	GnumericExprEntry *expr_text;
	GtkToggleButton *sheet_scope;
	GtkToggleButton *wb_scope;
	GList     *expr_names;
	NamedExpression *cur_name;

	GtkWidget *ok_button;
	GtkWidget *add_button;
	GtkWidget *close_button;
	GtkWidget *delete_button;
	GtkWidget *update_button;

	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
	ParsePos   pp;

	gboolean updating;
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
	NamedExpression const *nexpr = state->cur_name;

	state->updating = TRUE;
	if (nexpr == NULL || nexpr->pos.sheet == NULL)
		gtk_toggle_button_set_active (state->wb_scope, TRUE);
	else
		gtk_toggle_button_set_active (state->sheet_scope, TRUE);
	state->updating = FALSE;
}

static void cb_name_guru_select_name (GtkWidget *list, NameGuruState *state);

/**
 * name_guru_set_expr:
 * @state:
 * @expr_name: Expression to set in the entries, NULL to clear entries
 *
 * Set the entries in the dialog from an NamedExpression
 **/
static void
name_guru_set_expr (NameGuruState *state, NamedExpression *expr_name)
{
	state->updating = TRUE;
	if (expr_name) {
		gchar *txt;

		/* Display the name */
		gtk_entry_set_text (state->name, expr_name->name->str);

		/* Display the expr_text */
		txt = expr_name_as_string (expr_name, &state->pp);
		gtk_entry_set_text (GTK_ENTRY (state->expr_text), txt);
		g_free (txt);
	} else {
		gtk_entry_set_text (state->name, "");
		gtk_entry_set_text (GTK_ENTRY (state->expr_text), "");
	}
	state->updating = FALSE;

	name_guru_display_scope (state);
}

/**
 * name_guru_clear_selection:
 * @state:
 *
 * Clear the selection of the gtklist
 **/
static void
name_guru_clear_selection (NameGuruState *state)
{
	g_return_if_fail (state != NULL);

	state->updating = TRUE;
	gtk_list_unselect_all (state->list);
	state->updating = FALSE;
}

/**
 * name_guru_in_list:
 * @name:
 * @state:
 *
 * Given a name, it searches for it inside the list of Names
 *
 * Return Value: TRUE if name is already defined, FALSE otherwise
 **/
static gboolean
name_guru_in_list (const gchar *name, NameGuruState *state)
{
	NamedExpression *expression;
	GList *list;

	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	for (list = state->expr_names; list; list = list->next) {
		expression = (NamedExpression *) list->data;
		g_return_val_if_fail (expression != NULL, FALSE);
		g_return_val_if_fail (expression->name != NULL, FALSE);
		g_return_val_if_fail (expression->name->str != NULL, FALSE);
		if (strcasecmp (name, expression->name->str) == 0) {
			return TRUE;
		}
	}

	return FALSE;
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
	gboolean in_list = FALSE;
	char const *expr_text;
	char const *name;

	g_return_if_fail (state->list != NULL);

	if (state->updating)
		return;

	name  = gtk_entry_get_text (state->name);
	expr_text = gtk_entry_get_text (GTK_ENTRY (state->expr_text));

	/** Add is active if :
	 *  - We have a name in the entry to add
	 *  - Either we don't have a current Name or if we have a current
	 *     name, the name is different than what we are going to add
	 **/
	add = (name != NULL &&
	       name[0] != '\0' &&
	       !(in_list = name_guru_in_list (name, state)));
	selection = (g_list_length (state->list->selection) > 0);
	update = (name && *name && !add);

	gtk_widget_set_sensitive (state->delete_button, selection && in_list);
	gtk_widget_set_sensitive (state->add_button,    add);
	gtk_widget_set_sensitive (state->update_button, update);

	if (!selection && update_entries)
		name_guru_set_expr (state, NULL);

	if (selection && !in_list)
		name_guru_clear_selection (state);
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

/**
 * cb_name_guru_select_name:
 * @list:
 * @state:
 *
 * Set the expression from the selected row in the gtklist
 **/
static void
cb_name_guru_select_name (GtkWidget *list, NameGuruState *state)
{
	NamedExpression *expr_name;
	GList *sel = GTK_LIST (list)->selection;

	if (sel == NULL || state->updating)
		return;

	g_return_if_fail (sel->data != NULL);

	expr_name = gtk_object_get_data (GTK_OBJECT (sel->data), LIST_KEY);

	g_return_if_fail (expr_name != NULL);
	g_return_if_fail (expr_name->name != NULL);
	g_return_if_fail (expr_name->name->str != NULL);

	state->cur_name = expr_name;

	name_guru_set_expr (state, expr_name);
	name_guru_update_sensitivity (state, FALSE);
}


static void
name_guru_populate_list (NameGuruState *state)
{
	GList *names;
	GtkContainer *list;

	g_return_if_fail (state != NULL);
	g_return_if_fail (state->list != NULL);

	state->cur_name = NULL;

	gtk_list_clear_items (state->list, 0, -1);

	g_list_free (state->expr_names);
	state->expr_names = sheet_get_available_names (state->sheet);

	list = GTK_CONTAINER (state->list);
	for (names = state->expr_names ; names != NULL ; names = g_list_next (names)) {
		NamedExpression *expr_name = names->data;
		GtkWidget *li;
		if (expr_name->pos.sheet != NULL) {
			char *name = g_strdup_printf ("%s!%s",
						      expr_name->pos.sheet->name_unquoted,
						      expr_name->name->str);
			li = gtk_list_item_new_with_label (name);
			g_free (name);
		} else
			li = gtk_list_item_new_with_label (expr_name->name->str);
		gtk_object_set_data (GTK_OBJECT (li), LIST_KEY, expr_name);
		gtk_container_add (list, li);
	}
	gtk_widget_show_all (GTK_WIDGET (state->list));
	name_guru_update_sensitivity (state, TRUE);
}

/**
 * cb_name_guru_remove:
 * @ignored:
 * @state:
 *
 * Remove the state->cur_name
 **/
static void
cb_name_guru_remove (GtkWidget *ignored, NameGuruState *state)
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
 * cb_name_guru_add:
 * @state:
 *
 * Update or add a NamedExpression from the values in the gtkentries.
 *
 * Return Value: FALSE if the expression was invalid, TRUE otherwise
 **/
static gboolean
cb_name_guru_add (NameGuruState *state)
{
	NamedExpression *expr_name;
	ParseError    perr;
	ExprTree *expr;
	char const *name, *expr_text, *tmp;
	gboolean dirty = FALSE;

	g_return_val_if_fail (state != NULL, FALSE);

	expr_text = gtk_entry_get_text (GTK_ENTRY (state->expr_text));
	name  = gtk_entry_get_text (state->name);

	if (!name || (name[0] == '\0'))
		return TRUE;

	expr_name = expr_name_lookup (&state->pp, name);

	/* strip off optional preceding '=' */
	if (NULL != (tmp = gnumeric_char_start_expr_p (expr_text)))
		expr_text = tmp;
	expr = expr_parse_str (expr_text, &state->pp,
		GNM_PARSER_DEFAULT, NULL, &perr);

	/* If the expression is invalid */
	if (expr == NULL) {
		gnumeric_notice (state->wbcg, GNOME_MESSAGE_BOX_ERROR, perr.message);
		gtk_widget_grab_focus (GTK_WIDGET (state->expr_text));
		parse_error_free (&perr);
		return FALSE;
	} else if (expr_name) {
		if (!expr_name->builtin) {
			/* This means that the expresion was updated.
			 * FIXME: if the scope has been changed too, call scope
			 * chaned first.
			 */
			expr_name_set_expr (expr_name, expr);
			dirty = TRUE;
		} else
			gnumeric_notice (state->wbcg, GNOME_MESSAGE_BOX_ERROR,
					 _("You cannot redefine a builtin name."));
	} else {
		char const *error = NULL;
		ParsePos pos;
		if (name_guru_scope_is_sheet (state))
			parse_pos_init (&pos, NULL, state->sheet,
					state->pp.eval.col,
					state->pp.eval.row);
		else
			parse_pos_init (&pos, state->wb, NULL,
					state->pp.eval.col,
					state->pp.eval.row);

		expr_name = expr_name_add (&pos, name, expr, &error);
		if (expr_name == NULL) {
			g_return_val_if_fail (error != NULL, FALSE);
			gnumeric_notice (state->wbcg, GNOME_MESSAGE_BOX_ERROR, error);
			gtk_widget_grab_focus (GTK_WIDGET (state->expr_text));
			return FALSE;
		}
		dirty = TRUE;
	}

	g_return_val_if_fail (expr_name != NULL, FALSE);

	name_guru_populate_list (state);
	gtk_widget_grab_focus (GTK_WIDGET (state->name));

	if (dirty)
		sheet_set_dirty (state->sheet, TRUE);

	return TRUE;
}

static void
cb_name_guru_clicked (GtkWidget *button, NameGuruState *state)
{
	if (state->dialog == NULL)
		return;

	wbcg_set_entry (state->wbcg, NULL);

	if (button == state->delete_button) {
		cb_name_guru_remove (NULL, state);
		return;
	}

	if (button == state->add_button ||
	    button == state->update_button ||
	    button == state->ok_button) {
		/* If adding the name failed, do not exit */
		if (!cb_name_guru_add (state)) {
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

	gtk_signal_connect (GTK_OBJECT (tmp), "clicked",
			    GTK_SIGNAL_FUNC (cb_name_guru_clicked),
			    state);
	return tmp;
}

static gboolean
cb_name_guru_destroy (GtkObject *w, NameGuruState *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		gtk_object_unref (GTK_OBJECT (state->gui));
		state->gui = NULL;
	}

	wbcg_edit_finish (state->wbcg, FALSE);

	state->dialog = NULL;

	g_list_free (state->expr_names);
	state->expr_names = NULL;

	g_free (state);

	return FALSE;
}

static void
cb_name_guru_set_focus (GtkWidget *window, GtkWidget *focus_widget,
			NameGuruState *state)
{
	if (IS_GNUMERIC_EXPR_ENTRY (focus_widget)) {
		wbcg_set_entry (state->wbcg,
				    GNUMERIC_EXPR_ENTRY (focus_widget));
		gnumeric_expr_entry_set_absolute (state->expr_text);
	} else
		wbcg_set_entry (state->wbcg, NULL);
}

static gboolean
name_guru_init (NameGuruState *state, WorkbookControlGUI *wbcg)
{
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	GtkTable *table2;

	state->wbcg  = wbcg;
	state->wb   = wb;
	state->sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	state->gui = gnumeric_glade_xml_new (state->wbcg, "names.glade");
        if (state->gui == NULL)
                return TRUE;

	parse_pos_init (&state->pp, state->wb, state->sheet,
			state->sheet->edit_pos.col,
			state->sheet->edit_pos.row);


	state->dialog = glade_xml_get_widget (state->gui, "NameGuru");
	table2 = GTK_TABLE (glade_xml_get_widget (state->gui, "table2"));
	state->name  = GTK_ENTRY (glade_xml_get_widget (state->gui, "name"));
	state->expr_text = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gtk_table_attach (table2, GTK_WIDGET (state->expr_text),
			  1, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gtk_widget_show (GTK_WIDGET (state->expr_text));
	state->sheet_scope = GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "sheet_scope"));
	state->wb_scope = GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "workbook_scope"));
	state->list  = GTK_LIST (glade_xml_get_widget (state->gui, "name_list"));
	state->expr_names = NULL;
	state->cur_name   = NULL;
	state->updating   = FALSE;

	gtk_label_set_text (GTK_LABEL (GTK_BIN (state->sheet_scope)->child),
		state->sheet->name_unquoted);
	name_guru_display_scope (state);
	gtk_signal_connect (GTK_OBJECT (state->sheet_scope),
		"toggled",
		GTK_SIGNAL_FUNC (cb_scope_changed), state);

	state->ok_button     = name_guru_init_button (state, "ok_button");
	state->close_button  = name_guru_init_button (state, "close_button");
	state->add_button    = name_guru_init_button (state, "add_button");
	state->delete_button = name_guru_init_button (state, "delete_button");
	state->update_button = name_guru_init_button (state, "update_button");

	gtk_signal_connect (GTK_OBJECT (state->list), "selection_changed",
			    GTK_SIGNAL_FUNC (cb_name_guru_select_name), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "set-focus",
			    GTK_SIGNAL_FUNC (cb_name_guru_set_focus), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (cb_name_guru_destroy), state);
	gtk_signal_connect (GTK_OBJECT (state->name), "changed",
			    GTK_SIGNAL_FUNC (cb_name_guru_update_sensitivity), state);
	/* We need to connect after because this is an expresion, and it will
	 * be changed by the mouse selecting a range, update after the entry
	 * is updated with the new text.
	 */
	gtk_signal_connect_after (GTK_OBJECT (state->expr_text), "changed",
				  GTK_SIGNAL_FUNC (cb_name_guru_update_sensitivity), state);

	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->name));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->expr_text));
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       DEFINE_NAMES_KEY);

	gnumeric_expr_entry_set_scg (state->expr_text,
				     wb_control_gui_cur_sheet (wbcg));
	wbcg_edit_attach_guru (state->wbcg, state->dialog);

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
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Name Guru."));
		g_free (state);
		return;
	}

	name_guru_populate_list (state);
	gtk_widget_show (state->dialog);
}
