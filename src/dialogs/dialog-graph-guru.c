/* vim: set sw=8: */

/*
 * dialog-graph-guru.c:  The Graph guru
 *
 * Copyright (C) 2000 Jody Goldberg (jgoldberg@home.com)
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
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "dialogs.h"
#include "gnumeric-util.h"
#include "workbook.h"
#include "workbook-edit.h"
#include "workbook-private.h" /* FIXME Ick */
#include "sheet-object.h"

typedef struct
{
	/* GUI accessors */
	GladeXML    *gui;
	GtkWidget   *dialog;
	GtkWidget   *button_cancel;
	GtkWidget   *button_prev;
	GtkWidget   *button_next;
	GtkWidget   *button_finish;
	GtkNotebook *steps;

	/* internal state */
	int current_page;
	gboolean valid;

	/* external state */
	Workbook *wb;
} GraphGuruState;

static gboolean
cb_graph_guru_destroy (GtkObject *w, GraphGuruState *state)
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

static  gint
cb_graph_guru_key_press (GtkWidget *widget, GdkEventKey *event,
			 GraphGuruState *state)
{
	if (event->keyval == GDK_Escape) {
		workbook_finish_editing (state->wb, FALSE);
		return TRUE;
	} else
		return FALSE;
}

static void
graph_guru_set_page (GraphGuruState *state, int page)
{
	char *name;
	gboolean prev_ok = TRUE, next_ok = TRUE;

	if (state->current_page == page)
		return;

	switch (page) {
	case 0:
		name = _("Step 1 of 3: Select graphic type");
		prev_ok = FALSE;
		break;
	case 1:
		name = _("Step 2 of 3: Select data ranges");
		break;
	case 2:
		name = _("Step 3 of 3: Customize graphic");
		next_ok = FALSE;
		break;

	default:
		g_warning ("Invalid Graph Guru page");
		return;
	}

	state->current_page = page;
	gtk_notebook_set_page (state->steps, page);
	gtk_window_set_title (GTK_WINDOW (state->dialog), name);
	gtk_widget_set_sensitive (state->button_prev, prev_ok);
	gtk_widget_set_sensitive (state->button_next, next_ok);
}

static void
cb_graph_guru_clicked (GtkWidget *button, GraphGuruState *state)
{
	if (state->dialog == NULL)
		return;

	workbook_set_entry (state->wb, NULL);

	if (button == state->button_prev) {
		graph_guru_set_page (state, state->current_page - 1);
		return;
	}

	if (button == state->button_next) {
		graph_guru_set_page (state, state->current_page + 1);
		return;
	}

	if (button == state->button_finish) {
#if 0
		GList *l;

		sheet_set_mode_type_full (state->wb->current_sheet,
					  SHEET_MODE_CREATE_GRAPH, state->client_site);

		for (l = state->data_range_list; l; l = l->next) {
			DataRange *r = l->data;

			sheet_vector_attach (r->vector, state->wb->current_sheet);
		}
#endif
	}

	gtk_object_destroy (GTK_OBJECT(state->dialog));
}

static GtkWidget *
graph_guru_init_button  (GraphGuruState *state, const char *widget_name)
{
	GtkWidget *tmp = glade_xml_get_widget (state->gui, widget_name);
	gtk_signal_connect ( GTK_OBJECT (tmp), "clicked",
			    GTK_SIGNAL_FUNC (cb_graph_guru_clicked),
			    state);

	return tmp;
}

static gboolean
graph_guru_init (GraphGuruState *state)
{
	BonoboClientSite *client_site;
	GtkWidget        *control;

	state->gui = gnumeric_glade_xml_new (workbook_command_context_gui (state->wb),
					     "graph-guru.glade");
        if (state->gui == NULL)
                return TRUE;

	state->dialog = glade_xml_get_widget (state->gui, "GraphGuru");
	state->steps  = GTK_NOTEBOOK (glade_xml_get_widget (state->gui, "main-notebook"));

	/* Buttons */
	state->button_cancel = graph_guru_init_button (state, "button_cancel");
	state->button_prev = graph_guru_init_button (state, "button_prev");
	state->button_next = graph_guru_init_button (state, "button_next");
	state->button_finish = graph_guru_init_button (state, "button_finish");

	/* Lifecyle management */
	workbook_edit_attach_guru (state->wb, state->dialog);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (cb_graph_guru_destroy),
			    state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "key_press_event",
			    GTK_SIGNAL_FUNC (cb_graph_guru_key_press),
			    state);

	/* Configure container */
	client_site = bonobo_client_site_new (state->wb->priv->bonobo_container);
	bonobo_container_add (state->wb->priv->bonobo_container,
			      BONOBO_OBJECT (client_site));

	control = bonobo_widget_new_control (
		"OAFIID:guppi_chart_selector:da7874cd-e72b-4458-b685-6718caf3d573",
		bonobo_object_corba_objref (BONOBO_OBJECT (client_site)));
	gtk_notebook_prepend_page (state->steps, control, NULL);
	gtk_widget_show (state->dialog);
	gtk_widget_show (control);

	/* Select first page */
	state->current_page = -1;
	graph_guru_set_page (state, 0);

	return FALSE;
}

/**
 * dialog_graph_guru
 * @wb : The workbook to use as a parent window.
 *
 * Pop up a function selector then a graph guru.
 */
void
dialog_graph_guru (Workbook *wb)
{
	GraphGuruState *state;

	g_return_if_fail (wb != NULL);

	state = g_new(GraphGuruState, 1);
	state->wb	= wb;
	state->valid	= FALSE;
	if (graph_guru_init (state)) {
		g_free (state);
		return;
	}

	/* Ok everything is hooked up. Let-er rip */
	state->valid = TRUE;
}
