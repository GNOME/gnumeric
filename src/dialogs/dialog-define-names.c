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
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "dialogs.h"

typedef struct {
	Workbook  *wb;
	GladeXML  *gui;
	GtkWidget *dia;
	GtkWidget *list;
	GtkWidget *name;
	GtkWidget *value;
	GList     *expr_names;
	GList     *list_items;
	gint       selected;
} state_t;

static void
select_name (GtkWidget *w, state_t *state)
{
	guint  i = 0;
	GList *p = state->list_items;
	while (p) {
		if (p->data == w) {
			state->selected = i;
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

	g_return_if_fail (state);
	g_return_if_fail (state->list);

	state->list_items = NULL;
	state->selected   = 0;

	state->expr_names = names = expr_name_list (state->wb, FALSE);
	
	while (names) {
		ExprName *expr_name = names->data;
		GtkWidget *li = gtk_list_item_new_with_label (expr_name->name->str);
		gtk_signal_connect (GTK_OBJECT (li), "toggle-focus-row",
				    GTK_SIGNAL_FUNC (select_name), &state);
				   
		state->list_items = g_list_append (state->list_items, li);
		names = g_list_next (names);
	}
	gtk_list_insert_items (GTK_LIST (state->list), state->list_items, 0);
}

static void
remove_name (GtkWidget *widget, state_t *state)
{
	
}

void
dialog_define_names (Workbook *wb)
{
	gint       v;
	state_t    state;
	GtkWidget *w;

	g_return_if_fail (wb != NULL);

	state.wb  = wb;
	state.gui = glade_xml_new (GNUMERIC_GLADEDIR "/names.glade", NULL);
	if (!state.gui) {
		printf ("Could not find names.glade\n");
		return;
	}
	
	state.name  = glade_xml_get_widget (state.gui, "name");
	state.value = glade_xml_get_widget (state.gui, "value");
	state.list  = glade_xml_get_widget (state.gui, "name_list");

	fill_list (&state);

	w = glade_xml_get_widget (state.gui, "delete");
	gtk_signal_connect (GTK_OBJECT (w), "clicked",
			    GTK_SIGNAL_FUNC (remove_name), &state);
	
	state.dia = glade_xml_get_widget (state.gui, "NamesDialog");
	if (!state.dia) {
		printf ("Corrupt file names.glade\n");
		return;
	}

	v = gnome_dialog_run (GNOME_DIALOG (state.dia));
	if (v == 0) { /* OK */
	}

	if (v != -1)
		gtk_object_destroy (GTK_OBJECT (state.dia));

	gtk_object_unref (GTK_OBJECT (state.gui));
}


