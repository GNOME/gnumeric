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
#include "selection.h"
#include "graph-series.h"
#include "idl/gnumeric-graphs.h"
#include <liboaf/liboaf.h>

typedef struct
{
	BonoboObjectClient		*object_server;
	GNOME_Gnumeric_Graph_Manager	 manager;

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
		BonoboClientSite *client_site;

		/* Configure our container */
		client_site = bonobo_client_site_new (state->wb->priv->bonobo_container);
		bonobo_container_add (state->wb->priv->bonobo_container,
				      BONOBO_OBJECT (client_site));

		if (bonobo_client_site_bind_embeddable (client_site, state->object_server)) {
			Sheet *sheet = state->wb->current_sheet;
			sheet_mode_create_object (
				sheet_object_container_new_bonobo (sheet,
								   client_site));
		}
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

static GtkWidget *
get_selector_control (GraphGuruState *state)
{
	CORBA_Environment	 ev;
	Bonobo_Control		 control;
	Bonobo_Unknown           corba_uih;
	GtkWidget *res = NULL;

	CORBA_exception_init (&ev);
	control = GNOME_Gnumeric_Graph_Manager_getTypeSelectControl (state->manager, &ev);
	if (ev._major != CORBA_NO_EXCEPTION)
		return NULL;

	corba_uih = bonobo_object_corba_objref (BONOBO_OBJECT (state->wb->priv->uih));
	res =  bonobo_widget_new_control_from_objref (control, corba_uih);

	CORBA_exception_free (&ev);

	return res;
}

static gboolean
graph_guru_init (GraphGuruState *state)
{
	GtkWidget *control;

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

	control = get_selector_control (state);
	gtk_notebook_prepend_page (state->steps, control, NULL);

	gtk_widget_show (state->dialog);
	gtk_widget_show (control);

	/* Select first page */
	state->current_page = -1;
	graph_guru_set_page (state, 0);

	return FALSE;
}

static gboolean
graph_guru_init_manager (GraphGuruState *state)
{
	CORBA_Environment	 ev;
	Bonobo_Unknown		 o;

	CORBA_exception_init (&ev);
	o = (Bonobo_Unknown)oaf_activate ("repo_ids.has('IDL:Gnome/Gnumeric/Graph/Manager:1.0')",
					  NULL, 0, NULL, &ev);

	state->manager = CORBA_OBJECT_NIL;
	if (o != CORBA_OBJECT_NIL) {
		state->object_server = bonobo_object_client_from_corba (o);
		if (state->object_server != NULL)
			state->manager = bonobo_object_query_interface (
				BONOBO_OBJECT (state->object_server),
				"IDL:GNOME/Gnumeric/Graph/Manager:1.0");
	}

	CORBA_exception_free (&ev);

	return (state->manager == CORBA_OBJECT_NIL);
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
	CORBA_Environment ev;
	GNOME_Gnumeric_VectorScalarNotify subscriber;

	GraphGuruState *state;
	Range const * r;
	GraphSeries *series;
	Sheet *sheet;

	g_return_if_fail (wb != NULL);

	state = g_new(GraphGuruState, 1);
	state->wb	= wb;
	state->valid	= FALSE;
	if (graph_guru_init_manager (state) || graph_guru_init (state)) {
		g_free (state);
		return;
	}

	/* Ok everything is hooked up. Let-er rip */
	state->valid = TRUE;

	sheet = state->wb->current_sheet;

	/* FIXME : add the logic. to autodetect a series */
	r = selection_first_range (sheet, TRUE);
	series = graph_series_new (sheet, r);

	CORBA_exception_init (&ev);
	subscriber = GNOME_Gnumeric_Graph_Manager_addVectorScalar (
		state->manager, graph_series_servant (series), &ev);
	graph_series_set_subscriber (series, subscriber);
	CORBA_exception_free (&ev);
}
