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
	Workbook  *wb;
	GladeXML  *gui;
	GtkWidget *dia;
	GtkList   *list;
	GtkEntry  *name;
	GtkEntry  *value;
	GList     *expr_names;
	gint       selected;
} state_t;

static void
update_edit (state_t *state)
{
	/* ICK!  parse names as if we are in A1 ?? Why ? */
	static CellPos const pos = {0,0};
	gint          i = state->selected;
	NamedExpression     *expr_name;
	Sheet        *sheet;
	EvalPos  ep;
	char         *txt;

	sheet = state->wb->current_sheet;
	g_return_if_fail (sheet != NULL);

	eval_pos_init (&ep, sheet, &pos);

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
	GList *items = NULL;

	g_return_if_fail (state != NULL);
	g_return_if_fail (state->list != NULL);

	state->selected   = -1;

	/* FIXME: scoping issues here */
	state->expr_names = names = expr_name_list (state->wb, NULL, FALSE);

	while (names) {
		NamedExpression *expr_name = names->data;
		GtkWidget *li = gtk_list_item_new_with_label (expr_name->name->str);
		gtk_object_set_data (GTK_OBJECT (li), LIST_KEY, expr_name);
		gtk_widget_show (GTK_WIDGET (li));
		items = g_list_append (items, li);
		names = g_list_next (names);
	}
	gtk_list_append_items (state->list, items);
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
/*	gint i;
	g_return_if_fail (state != NULL);
	g_return_if_fail (widget != NULL);

	i = state->selected;
	if (i >= 0) {
		GList *na           = g_list_nth (state->expr_names, i);
		GList *it           = g_list_nth (state->list_items, i);
		GList *l;

		g_return_if_fail (it != NULL && na != NULL);
		g_return_if_fail (it->data != NULL && na->data != NULL);

		expr_name_remove (na->data);
		state->expr_names = g_list_remove (state->expr_names, na->data);

		l = g_list_append (NULL, it->data);
		gtk_list_remove_items (state->list, l);
		g_list_free (l);
		state->list_items = g_list_remove (state->list_items, it->data);

		gtk_entry_set_text (state->name, "");
		gtk_entry_set_text (state->value, "");
		}*/
	g_warning ("Unimplemented, need to sweep sheets to check for usage");
}

static gboolean
grab_text_ok (state_t *state, gboolean update_list)
{
	gchar        *name;
	gchar        *value;
	NamedExpression     *expr_name;
	char         *error;

	g_return_val_if_fail (state != NULL, FALSE);

	value = gtk_entry_get_text (state->value);
	name  = gtk_entry_get_text (state->name);

	if (!name || (name[0] == '\0'))
		return TRUE;

	/* FIXME: we need to be able to select names scope ideally */
	expr_name = expr_name_lookup (state->wb, NULL, name);
	if (expr_name)
		expr_name_remove (expr_name);

	/* FIXME: and here */
	expr_name = expr_name_create (state->wb, NULL, name,
				      value, &error);

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
