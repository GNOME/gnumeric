/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* vim: set sw=8: */
/*
 * dialog-define-name.c: Edit named regions.
 *
 * Author:
 * 	Jody Goldberg <jgoldberg@home.com>
 *	Michael Meeks <michael@imaginator.com>
 *	Chema Celorio <chema@celorio.com>
 */
#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "dialogs.h"
#include "expr.h"
#include "str.h"
#include "expr-name.h"
#include "sheet.h"
#include "workbook.h"
#include "workbook-control.h"
#include "workbook-edit.h"
#include "gnumeric-util.h"
#include "parse-util.h"
#include "widgets/gnumeric-expr-entry.h"

#define LIST_KEY "name_list_data"

typedef enum {
	NAME_GURU_SCOPE_SHEET,
	NAME_GURU_SCOPE_WORKBOOK,
} NameGuruScope;

typedef struct {
	GladeXML  *gui;
	GtkWidget *dialog;
	GtkList   *list;
	GtkEntry  *name;
	GnumericExprEntry *expr_text;
	GtkCombo  *scope;
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

	gboolean updating;
} NameGuruState;

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
	static gboolean warned = FALSE;
	if (!warned)
		g_warning ("Implement me !. name_guru_warned_if_used\n");
	warned = TRUE;

	return TRUE;
}

/**
 * name_guru_scope_change:
 * @state:
 * @scope:
 *
 * Change the scope of state->cur_name. Ask the user if we want to procede with the
 * change if we are going to invalidate expressions in the sheet.
 *
 * Return Value: FALSE, if the user cancels the scope change
 **/
static gboolean
name_guru_scope_change (NameGuruState *state, NameGuruScope scope)
{
	NamedExpression *expression;

	g_return_val_if_fail (state != NULL, FALSE);
	expression = state->cur_name;
	if (expression == NULL)
		return TRUE;

	/* get the current values for the expression */
	if (scope == NAME_GURU_SCOPE_WORKBOOK) {
		expr_name_sheet2wb (expression);
	}

	if (scope == NAME_GURU_SCOPE_SHEET) {
		if (!name_guru_warn (state))
			return FALSE;

		expr_name_wb2sheet (expression, state->sheet);
	}

	return TRUE;
}

static void
cb_scope_changed (GtkEntry *entry, NameGuruState *state)
{
	NamedExpression *expression;

	if (state->updating)
		return;

	expression = state->cur_name;
	if (expression == NULL)
		return;

	if (!name_guru_scope_change (state,
				     (expression->sheet == NULL) ?
				     NAME_GURU_SCOPE_SHEET :
				     NAME_GURU_SCOPE_WORKBOOK))
		g_print ("Here we toggle the scope back to what it was\n"
			 "The user cancelled the scope change.\n");

}

static void
name_guru_init_scope (NameGuruState *state)
{
	NamedExpression *expression;
	GList *list = NULL;

	expression  = state->cur_name;
	if (expression != NULL && expression->wb) {
		list = g_list_prepend (list, state->sheet->name_unquoted);
		list = g_list_prepend (list, _("Workbook"));
	} else {
		list = g_list_prepend (list, _("Workbook"));
		list = g_list_prepend (list, state->sheet->name_unquoted);
	}

	state->cur_name = NULL;
	state->updating = TRUE;
	gtk_combo_set_popdown_strings (state->scope, list);
	g_list_free (list);
	state->updating = FALSE;
	state->cur_name = expression;
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
	g_return_if_fail (state != NULL);

	/* Don't recur.  */
	state->updating = TRUE;

	if (expr_name) {
		gchar *txt;

		/* Display the name */
		gtk_entry_set_text (state->name, expr_name->name->str);

		/* Display the expr_text */
		txt = expr_name_value (expr_name);
		gtk_entry_set_text (GTK_ENTRY (state->expr_text), txt);
		g_free (txt);
	} else {
		gtk_entry_set_text (state->name, "");
		gtk_entry_set_text (GTK_ENTRY (state->expr_text), "");
	}

	/* unblock them */
	state->updating = FALSE;

	/* Init the scope combo box */
	name_guru_init_scope (state);

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
 * name_guru_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 **/
static void
name_guru_update_sensitivity_cb (GtkWidget *dummy, NameGuruState *state)
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
	GList *sel = GTK_LIST(list)->selection;

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
	if (state->expr_names != NULL)
		g_list_free (state->expr_names);

	state->expr_names = expr_name_list (state->wb, state->sheet, FALSE);

	list = GTK_CONTAINER (state->list);
	for (names = state->expr_names ; names != NULL ; names = g_list_next (names)) {
		NamedExpression *expr_name = names->data;
		GtkWidget *li;
		if (expr_name->sheet != NULL) {
			char *name = g_strdup_printf ("%s!%s",
						      expr_name->sheet->name_unquoted,
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
 * name_guru_scope_get:
 * @state:
 *
 * Get the selected Scope from the combo box
 *
 * Return Value:
 **/
static NameGuruScope
name_guru_scope_get (NameGuruState *state)
{
	gchar *text;

	text = gtk_entry_get_text (GTK_ENTRY (state->scope->entry));

	g_return_val_if_fail (text != NULL, NAME_GURU_SCOPE_WORKBOOK);

	if (strcmp (text, _("Workbook"))==0)
	    return NAME_GURU_SCOPE_WORKBOOK;
	else
	    return NAME_GURU_SCOPE_SHEET;
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

	if (state->cur_name != NULL) {
		if (!name_guru_warn (state))
			return;
		state->expr_names = g_list_remove (state->expr_names, state->cur_name);
		expr_name_remove (state->cur_name);
		state->cur_name = NULL;

		gtk_list_clear_items (state->list, 0, -1);
		name_guru_populate_list (state);
	} else {
		g_warning ("Why is the delete button sensitive ? ...\n");
	}

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
	ParsePos      pos, *pp;
	ExprTree *expr;
	char const *name, *expr_text, *tmp;
	gchar *error;

	g_return_val_if_fail (state != NULL, FALSE);

	expr_text = gtk_entry_get_text (GTK_ENTRY (state->expr_text));
	name  = gtk_entry_get_text (state->name);

	if (!name || (name[0] == '\0'))
		return TRUE;

	pp = parse_pos_init (&pos, state->wb, state->sheet, 0, 0);
	/*
	 * expr name uses 0,0 for now.  Figure out what to do with this
	 * eventually.
			     state->sheet->edit_pos.col,
			     state->sheet->edit_pos.row);
         */

	expr_name = expr_name_lookup (pp, name);

	if (NULL != (tmp = gnumeric_char_start_expr_p (expr_text)))
		expr_text = tmp;
	expr = expr_parse_string (expr_text, pp, NULL, &error);

	/* If the expression is invalid */
	if (expr == NULL) {
		gnumeric_notice (state->wbcg, GNOME_MESSAGE_BOX_ERROR, error);
		gtk_widget_grab_focus (GTK_WIDGET (state->expr_text));
		return FALSE;
	} else if (expr_name) {
		if (!expr_name->builtin) {
			/* This means that the expresion was updated updated.
			 * FIXME: if the scope has been changed too, call scope
			 * chaned first.
			 */
			expr_tree_unref (expr_name->t.expr_tree);
			expr_name->t.expr_tree = expr;
		} else
			gnumeric_notice (state->wbcg, GNOME_MESSAGE_BOX_ERROR,
					 _("You cannot redefine a builtin name."));
	} else {
		if (name_guru_scope_get (state) == NAME_GURU_SCOPE_WORKBOOK)
			expr_name = expr_name_add (state->wb, NULL, name, expr, &error);
		else
			expr_name = expr_name_add (NULL, state->sheet, name, expr, &error);
	}

	g_return_val_if_fail (expr_name != NULL, FALSE);

	gtk_list_clear_items (state->list, 0, -1);
	name_guru_populate_list (state);
	gtk_widget_grab_focus (GTK_WIDGET (state->name));

	return TRUE;
}

static void
cb_name_guru_value_focus (GtkWidget *w, GdkEventFocus *ev, NameGuruState *state)
{

	if (w == GTK_WIDGET (state->expr_text)) {
		workbook_set_entry (state->wbcg, state->expr_text);
		gnumeric_expr_entry_set_absolute (state->expr_text);
	} else
		workbook_set_entry (state->wbcg, NULL);
}

static void
cb_name_guru_clicked (GtkWidget *button, NameGuruState *state)
{
	if (state->dialog == NULL)
		return;

	workbook_set_entry (state->wbcg, NULL);

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

	workbook_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		gtk_object_unref (GTK_OBJECT (state->gui));
		state->gui = NULL;
	}

	workbook_finish_editing (state->wbcg, FALSE);

	state->dialog = NULL;

	g_free (state);

	return FALSE;
}

static void
cb_name_guru_set_focus (GtkWidget *window, GtkWidget *focus_widget,
		 NameGuruState *state)
{
	if (GNUMERIC_IS_EXPR_ENTRY (focus_widget)) {
		workbook_set_entry (state->wbcg,
				    GNUMERIC_EXPR_ENTRY (focus_widget));
		gnumeric_expr_entry_set_absolute (state->expr_text);
	} else
		workbook_set_entry (state->wbcg, NULL);
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

	state->dialog = glade_xml_get_widget (state->gui, "NameGuru");
	table2 = GTK_TABLE (glade_xml_get_widget (state->gui, "table2"));
	state->name  = GTK_ENTRY (glade_xml_get_widget (state->gui, "name"));
	state->expr_text = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new ());
	gtk_table_attach (table2, GTK_WIDGET (state->expr_text),
			  1, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gtk_widget_show (GTK_WIDGET (state->expr_text));
	state->scope = GTK_COMBO (glade_xml_get_widget (state->gui, "scope_combo"));
	state->list  = GTK_LIST  (glade_xml_get_widget (state->gui, "name_list"));
	state->expr_names = NULL;
	state->cur_name   = NULL;
	state->updating   = FALSE;

	/* Init the scope combo box */
	name_guru_init_scope (state);
	gtk_signal_connect (GTK_OBJECT (state->scope->entry), "changed",
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
/*  	gtk_signal_connect (GTK_OBJECT (state->expr_text), "focus-in-event", */
/*  			    GTK_SIGNAL_FUNC (cb_name_guru_value_focus), state); */
	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (cb_name_guru_destroy), state);
	gtk_signal_connect (GTK_OBJECT (state->name), "changed",
			    GTK_SIGNAL_FUNC (name_guru_update_sensitivity_cb), state);
	/* We need to connect after because this is an expresion, and it will
	 * be changed by the mouse selecting a range, update after the entry
	 * is updated with the new text.
	 */
	gtk_signal_connect_after (GTK_OBJECT (state->expr_text), "changed",
				  GTK_SIGNAL_FUNC (name_guru_update_sensitivity_cb), state);

 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE(state->name));
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->expr_text));
	gnumeric_combo_enters (GTK_WINDOW (state->dialog),
			       state->scope);
	gnumeric_non_modal_dialog (state->wbcg, GTK_WINDOW (state->dialog));

	gnumeric_expr_entry_set_scg (state->expr_text,
				     wb_control_gui_cur_sheet (wbcg));
	workbook_edit_attach_guru (state->wbcg, state->dialog);

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

	state = g_new (NameGuruState, 1);
	if (name_guru_init (state, wbcg)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Name Guru."));
		g_free (state);
		return;
	}

	name_guru_populate_list (state);
	gtk_widget_show (state->dialog);
}
