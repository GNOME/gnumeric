/**
 * dialog-summary.c:  Implements the summary info stuff
 *
 * Author:
 *        Michael Meeks <michael@imaginator.com>
 *
 **/
#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "dialogs.h"
#include "expr.h"
#include "expr-name.h"
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
	GList     *expr_names;
	gint       selected;

	GtkWidget *ok_button;
	GtkWidget *add_button;
	GtkWidget *close_button;
	GtkWidget *delete_button;

	Workbook  *wb;
} NameGuruState;

static void
update_edit (NameGuruState *state)
{
	gint          i = state->selected;
	NamedExpression     *expr_name;
	Sheet        *sheet;
	char         *txt;

	sheet = state->wb->current_sheet;
	g_return_if_fail (sheet != NULL);

	expr_name = g_list_nth (state->expr_names, i)->data;
	if (expr_name->name && expr_name->name->str)
		gtk_entry_set_text (state->name, expr_name->name->str);
	else
		gtk_entry_set_text (state->name, "");

	txt = expr_name_value (expr_name);
	gtk_entry_set_text (state->value, txt);
	g_free (txt);
}

static void
select_name (GtkWidget *w, NameGuruState *state)
{
	guint     i    = 0;
	GList    *sel  = GTK_LIST(w)->selection;
	GList    *p    = state->expr_names;
	NamedExpression *name;

	if (sel == NULL)
		return;

	g_return_if_fail (sel->data != NULL);

	name = gtk_object_get_data (GTK_OBJECT (sel->data), LIST_KEY);

	while (p) {
		if (p->data == name) {
			state->selected = i;
			update_edit (state);
			return;
		}
		i++;
		p = g_list_next (p);
	}
}

static void
fill_list (NameGuruState *state)
{
	GList *names;
	GtkContainer *list;

	g_return_if_fail (state != NULL);
	g_return_if_fail (state->list != NULL);

	list = GTK_CONTAINER (state->list);
	g_return_if_fail (list != NULL);

	state->selected   = -1;

	/* FIXME: scoping issues here */
	names = state->expr_names = expr_name_list (state->wb, NULL, FALSE);

	for (; names != NULL ; names = g_list_next (names)) {
		NamedExpression *expr_name = names->data;
		GtkWidget *li = gtk_list_item_new_with_label (expr_name->name->str);
		gtk_object_set_data (GTK_OBJECT (li), LIST_KEY, expr_name);
		gtk_container_add (list, li);
	}
	gtk_widget_show_all (GTK_WIDGET (state->list));
}

static void
clear_state (NameGuruState *state)
{
	if (state->expr_names)
		g_list_free (state->expr_names);
	state->expr_names = NULL;
	state->selected = -1;
}

static void
empty_list (NameGuruState *state)
{
	if (state->list)
		gtk_list_clear_items (state->list, 0, -1);
	state->list = NULL;
	clear_state (state);
}

static void
remove_name (GtkWidget *ignored, NameGuruState *state)
{
	gint i;
	g_return_if_fail (state != NULL);

	i = state->selected;
	if (i >= 0) {
		GList *na = g_list_nth (state->expr_names, i);

		g_return_if_fail (na != NULL);
		g_return_if_fail (na->data != NULL);

		expr_name_remove (na->data);
		state->expr_names = g_list_remove (state->expr_names, na->data);

		gtk_entry_set_text (state->name, "");
		gtk_entry_set_text (state->value, "");
	}
}

static gboolean
grab_text_ok (NameGuruState *state, gboolean update_list)
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
	pp = parse_pos_init (&pos, state->wb, NULL, 0, 0);

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

	if (update_list) {
		empty_list (state);
		fill_list  (state);
	}

	return TRUE;
}

static void
cb_name_guru_clicked (GtkWidget *button, NameGuruState *state)
{
	if (state->dialog == NULL)
		return;

	if (button == state->delete_button) {
		remove_name (NULL, state);
		return;
	}

	if (button == state->add_button || button == state->ok_button)
		grab_text_ok (state, TRUE);

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
name_guru_init (NameGuruState *state)
{
	state->gui = gnumeric_glade_xml_new (workbook_command_context_gui (state->wb),
					     "names.glade");
        if (state->gui == NULL)
                return TRUE;

	state->dialog = glade_xml_get_widget (state->gui, "NamesDialog");
	state->name  = GTK_ENTRY (glade_xml_get_widget (state->gui, "name"));
	state->value = GTK_ENTRY (glade_xml_get_widget (state->gui, "value"));
	state->list  = GTK_LIST  (glade_xml_get_widget (state->gui, "name_list"));
	state->expr_names = NULL;
	state->selected   = -1;

	state->ok_button = name_guru_init_button (state, "ok_button");
	state->close_button = name_guru_init_button (state, "close_button");
	state->add_button = name_guru_init_button (state, "add_button");
	state->delete_button = name_guru_init_button (state, "delete_button");

 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE(state->name));
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->value));

	fill_list (state);

	gtk_signal_connect (GTK_OBJECT (state->list), "selection_changed",
			    GTK_SIGNAL_FUNC (select_name), &state);

	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (cb_name_guru_destroy),
			    state);

	gtk_window_set_transient_for (GTK_WINDOW (state->dialog),
				      GTK_WINDOW (state->wb->toplevel));

	workbook_set_entry (state->wb, state->value);
	workbook_edit_attach_guru (state->wb, state->dialog);
	workbook_edit_select_absolute (state->wb);

	return FALSE;
}

void
dialog_define_names (Workbook *wb)
{
	NameGuruState *state;

	g_return_if_fail (wb != NULL);

	state = g_new (NameGuruState, 1);
	state->wb  = wb;
	if (name_guru_init (state)) {
		g_free (state);
		return;
	}

	gtk_widget_show (state->dialog);
}
