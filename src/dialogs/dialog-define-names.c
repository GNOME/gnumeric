/* vim: set sw=8: */

/*
 * dialog-define-name.c: Edit named regions.
 *
 * Author:
 * 	Jody Goldberg <jgoldberg@home.com> 
 *	Michael Meeks <michael@imaginator.com>
 */
#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "dialogs.h"
#include "expr.h"
#include "expr-name.h"
#include "sheet.h"
#include "workbook.h"
#include "workbook-edit.h"
#include "gnumeric-util.h"

#define LIST_KEY "name_list_data"

typedef struct {
	GladeXML  *gui;
	GtkWidget *dialog;
	GtkList   *list;
	GtkEntry  *name;
	GtkEntry  *value;
	GtkCombo  *scope;
	GList     *expr_names;
	NamedExpression *cur_name;

	GtkWidget *ok_button;
	GtkWidget *add_button;
	GtkWidget *close_button;
	GtkWidget *delete_button;

	Workbook  *wb;
	Sheet	  *sheet;
} NameGuruState;

static void
cb_scope_changed (GtkEntry *entry, NameGuruState *state)
{
	if (state->cur_name != NULL)
		puts("changed");
}

static void
name_guru_init_scope (NameGuruState *state)
{
	NamedExpression *cur = state->cur_name;
	GList *list = NULL;

	if (cur != NULL && cur->sheet) {
		list = g_list_prepend (list, _("Workbook"));
		list = g_list_prepend (list, cur->sheet->name_unquoted);
	} else {
		list = g_list_prepend (list, state->sheet->name_unquoted);
		list = g_list_prepend (list, _("Workbook"));
	}

	state->cur_name = NULL;
	gtk_combo_set_popdown_strings (state->scope, list);
	g_list_free (list);
	state->cur_name = cur;
}

static void
cb_name_guru_select_name (GtkWidget *list, NameGuruState *state)
{
	NamedExpression *expr_name;
	GList *sel = GTK_LIST(list)->selection;
	char *txt;

	if (sel == NULL)
		return;

	g_return_if_fail (sel->data != NULL);

	expr_name = gtk_object_get_data (GTK_OBJECT (sel->data), LIST_KEY);

	g_return_if_fail (expr_name != NULL);
	g_return_if_fail (expr_name->name != NULL);
	g_return_if_fail (expr_name->name->str != NULL);

	state->cur_name = expr_name;

	/* Display the name */
	gtk_entry_set_text (state->name, expr_name->name->str);

	/* Display the value */
	txt = expr_name_value (expr_name);
	gtk_entry_set_text (state->value, txt);
	g_free (txt);

	/* Init the scope combo box */
	name_guru_init_scope (state);
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
}

static void
cb_name_guru_remove (GtkWidget *ignored, NameGuruState *state)
{
	g_return_if_fail (state != NULL);

	if (state->cur_name != NULL) {
		GList *na = g_list_remove (state->expr_names, state->cur_name);

		g_return_if_fail (na != NULL);
		g_return_if_fail (na->data != NULL);

		expr_name_remove (na->data);
		state->expr_names = g_list_remove (state->expr_names, na->data);

		gtk_entry_set_text (state->name, "");
		gtk_entry_set_text (state->value, "");
	}
}

static gboolean
cb_name_guru_add (NameGuruState *state)
{
	NamedExpression     *expr_name;
	ExprTree     *expr;
	gchar        *name, *value, *error;
	ParsePos      pos, *pp;

	g_return_val_if_fail (state != NULL, FALSE);

	value = gtk_entry_get_text (state->value);
	name  = gtk_entry_get_text (state->name);

	if (!name || (name[0] == '\0'))
		return TRUE;

	/* FIXME : Need to be able to control scope.
	 *        1) sheet
	 *        2) workbook
	 *        3) global
	 *
	 * default to workbook for now.
	 */
	pp = parse_pos_init (&pos, state->wb, state->sheet,
			     state->sheet->cursor.edit_pos.col,
			     state->sheet->cursor.edit_pos.row);

	expr_name = expr_name_lookup (pp, name);

	expr = expr_parse_string (value, pp, NULL, &error);

	/* If name already exists replace the its content */
	if (expr_name) {
		if (!expr_name->builtin) {
			expr_tree_unref (expr_name->t.expr_tree);
			expr_name->t.expr_tree = expr;
		} else
			gnumeric_notice (state->wb, GNOME_MESSAGE_BOX_ERROR,
					 _("You can not redefine a builtin name."));
	} else
		expr_name = expr_name_add (state->wb, NULL, name, expr, &error);

	if (expr_name == NULL) {
		if (error)
			gnumeric_notice (state->wb, GNOME_MESSAGE_BOX_ERROR, error);
		else
			g_warning ("serious name error");
		return FALSE;
	}

	gtk_list_clear_items (state->list, 0, -1);
	name_guru_populate_list (state);

	return TRUE;
}

static void
cb_name_guru_value_focus (GtkWidget *w, GdkEventFocus *ev, NameGuruState *state)
{
	GtkEntry *entry = GTK_ENTRY (w);
	if (entry == state->value) {
		workbook_set_entry (state->wb, state->value);
		workbook_edit_select_absolute (state->wb);
	} else
		workbook_set_entry (state->wb, NULL);
}

static void
cb_name_guru_clicked (GtkWidget *button, NameGuruState *state)
{
	if (state->dialog == NULL)
		return;

	workbook_set_entry (state->wb, NULL);

	if (button == state->delete_button) {
		cb_name_guru_remove (NULL, state);
		return;
	}

	if (button == state->add_button || button == state->ok_button)
		cb_name_guru_add (state);

	if (button == state->close_button || button == state->ok_button)
		gtk_widget_destroy (state->dialog);
}

static GtkWidget *
name_guru_init_button (NameGuruState *state, char const *name)
{
	GtkWidget *tmp = glade_xml_get_widget (state->gui, name);
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

	workbook_edit_detach_guru (state->wb);

	if (state->gui != NULL) {
		gtk_object_unref (GTK_OBJECT (state->gui));
		state->gui = NULL;
	}

	/* Handle window manger closing the dialog.
	 * This will be ignored if we are being destroyed differently.
	 */
	workbook_finish_editing (state->wb, FALSE);

	state->dialog = NULL;

	g_free (state);
	return FALSE;
}

static gboolean
name_guru_init (NameGuruState *state, Workbook *wb)
{
	state->wb  = wb;
	state->sheet = wb->current_sheet;
	state->gui = gnumeric_glade_xml_new (workbook_command_context_gui (state->wb),
					     "names.glade");
        if (state->gui == NULL)
                return TRUE;

	state->dialog = glade_xml_get_widget (state->gui, "NameGuru");
	state->name  = GTK_ENTRY (glade_xml_get_widget (state->gui, "name"));
	state->value = GTK_ENTRY (glade_xml_get_widget (state->gui, "value"));
	state->scope = GTK_COMBO (glade_xml_get_widget (state->gui, "scope_combo"));
	state->list  = GTK_LIST  (glade_xml_get_widget (state->gui, "name_list"));
	state->expr_names = NULL;
	state->cur_name   = NULL;

	/* Init the scope combo box */
	name_guru_init_scope (state);
	gtk_signal_connect (GTK_OBJECT (state->scope->entry), "changed",
			    GTK_SIGNAL_FUNC (cb_scope_changed), state);

	state->ok_button = name_guru_init_button (state, "ok_button");
	state->close_button = name_guru_init_button (state, "close_button");
	state->add_button = name_guru_init_button (state, "add_button");
	state->delete_button = name_guru_init_button (state, "delete_button");

	gtk_signal_connect (GTK_OBJECT (state->list), "selection_changed",
			    GTK_SIGNAL_FUNC (cb_name_guru_select_name), state);
	gtk_signal_connect (GTK_OBJECT (state->name), "focus-in-event",
			    GTK_SIGNAL_FUNC (cb_name_guru_value_focus), state);
	gtk_signal_connect (GTK_OBJECT (state->value), "focus-in-event",
			    GTK_SIGNAL_FUNC (cb_name_guru_value_focus), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (cb_name_guru_destroy), state);

 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE(state->name));
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->value));
	gnumeric_combo_enters (GTK_WINDOW (state->dialog),
			       state->scope);
	gnumeric_non_modal_dialog (state->wb, GTK_DIALOG (state->dialog));

	workbook_edit_attach_guru (state->wb, state->dialog);

	return FALSE;
}

void
dialog_define_names (Workbook *wb)
{
	NameGuruState *state;

	g_return_if_fail (wb != NULL);

	state = g_new (NameGuruState, 1);
	if (name_guru_init (state, wb)) {
		g_free (state);
		return;
	}

	name_guru_populate_list (state);
	gtk_widget_show (state->dialog);
}
