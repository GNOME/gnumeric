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
#include "gnumeric-util.h"

#define LIST_KEY "name_list_data"

typedef struct {
	GladeXML  *gui;
	GtkWidget *dia;
	GtkList   *list;
	GtkEntry  *name;
	GtkEntry  *value;
	GList     *expr_names;
	gint       selected;

	Workbook  *wb;
} state_t;

static void
update_edit (state_t *state)
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
select_name (GtkWidget *w, state_t *state)
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
fill_list (state_t *state)
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
destroy_state (state_t *state)
{
	if (state->expr_names)
		g_list_free (state->expr_names);
	state->expr_names = NULL;
	state->selected = -1;
}

static void
empty_list (state_t *state)
{
	if (state->list)
		gtk_list_clear_items (state->list, 0, -1);
	state->list = NULL;
	destroy_state (state);
}

static void
remove_name (GtkWidget *widget, state_t *state)
{
	gint i;
	g_return_if_fail (state != NULL);
	g_return_if_fail (widget != NULL);

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
grab_text_ok (state_t *state, gboolean update_list)
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
destroy_dialog (state_t *state)
{
	if (state->dia)
		gnome_dialog_close (GNOME_DIALOG (state->dia));
	state->dia   = NULL;
	state->list  = NULL;
	state->name  = NULL;
	state->value = NULL;
}

static void
add_name (GtkWidget *widget, state_t *state)
{
	grab_text_ok (state, TRUE);
}

static void
close_name (GtkWidget *widget, state_t *state)
{
	destroy_state  (state);
	destroy_dialog (state);
}

static void
ok_name (GtkWidget *widget, state_t *state)
{
	if (grab_text_ok (state, FALSE))
		close_name (widget, state);
}

void
dialog_define_names (Workbook *wb)
{
	gint       v;
	state_t    state;
	GtkWidget *w;

	g_return_if_fail (wb != NULL);

	state.wb  = wb;
	state.gui = gnumeric_glade_xml_new (workbook_command_context_gui (wb),
				      "names.glade");
        if (state.gui == NULL)
                return;

	state.name  = GTK_ENTRY (glade_xml_get_widget (state.gui, "name"));
	state.value = GTK_ENTRY (glade_xml_get_widget (state.gui, "value"));
	state.list  = GTK_LIST  (glade_xml_get_widget (state.gui, "name_list"));
	state.expr_names = NULL;
	state.selected   = -1;

	w = glade_xml_get_widget (state.gui, "ok");
	gtk_signal_connect (GTK_OBJECT (w), "clicked",
			    GTK_SIGNAL_FUNC (ok_name), &state);

	w = glade_xml_get_widget (state.gui, "close");
	gtk_signal_connect (GTK_OBJECT (w), "clicked",
			    GTK_SIGNAL_FUNC (close_name), &state);

	w = glade_xml_get_widget (state.gui, "add");
	gtk_signal_connect (GTK_OBJECT (w), "clicked",
			    GTK_SIGNAL_FUNC (add_name), &state);

	w = glade_xml_get_widget (state.gui, "delete");
	gtk_signal_connect (GTK_OBJECT (w), "clicked",
			    GTK_SIGNAL_FUNC (remove_name), &state);

	state.dia = glade_xml_get_widget (state.gui, "NamesDialog");
	if (!state.dia) {
		printf ("Corrupt file names.glade\n");
		return;
	}
 	gnome_dialog_editable_enters(GNOME_DIALOG(state.dia),
				     GTK_EDITABLE(state.name));
 	gnome_dialog_editable_enters(GNOME_DIALOG(state.dia),
				     GTK_EDITABLE(state.value));

	fill_list (&state);

	gtk_signal_connect (GTK_OBJECT (state.list), "selection_changed",
			    GTK_SIGNAL_FUNC (select_name), &state);

	v = gnumeric_dialog_run (wb, GNOME_DIALOG (state.dia));

	if (v == -1)
		destroy_state (&state);
	else
		destroy_dialog (&state);

	gtk_object_unref (GTK_OBJECT (state.gui));
}
