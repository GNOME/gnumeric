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
#include "workbook-control-gui-priv.h"
#include "workbook.h"
#include "sheet.h"
#include "workbook-edit.h"
#include "sheet-object.h"
#include "sheet-object-container.h"
#include "sheet-object-bonobo.h"
#include "sheet-control-gui.h"
#include "selection.h"
#include "ranges.h"
#include "value.h"
#include "cell.h"
#include "graph-vector.h"
#include "idl/gnumeric-graphs.h"
#include <liboaf/liboaf.h>

typedef struct
{
	BonoboObjectClient		*manager_client;
	GNOME_Gnumeric_Graph_Manager	 manager;
	Bonobo_Control		 	 control;

	/* GUI accessors */
	GladeXML    *gui;
	GtkWidget   *dialog;
	GtkWidget   *button_cancel;
	GtkWidget   *button_prev;
	GtkWidget   *button_next;
	GtkWidget   *button_finish;
	GtkNotebook *steps;

	/* Data widgets */
	GtkWidget   *data_notebook;

	/* simple selection */
	GtkWidget   *data_is_cols_radio;
	GtkWidget   *data_is_rows_radio;
	GtkWidget   *data_range;

	/* complex selection */
	GtkWidget   *data_series_list;
	GtkWidget   *data_series_name;
	GtkWidget   *data_series_range;
	GtkWidget   *data_add_series;
	GtkWidget   *data_remove_series;

	/* internal state */
	int current_page;
	gboolean valid;

	gboolean is_columns;
	GPtrArray *vectors;
	GSList	  *ranges;

	/* external state */
	WorkbookControlGUI *wbcg;
	SheetControlGUI	   *scg;
	Workbook *wb;
	Sheet	 *sheet;
} GraphGuruState;

static void
graph_guru_clear_vectors (GraphGuruState *state, gboolean explicit_remove)
{
	/* Release the vectors objects */
	if (state->vectors != NULL) {
		int i = state->vectors->len;
		while (i-- > 0) {
			GraphVector *vectors = g_ptr_array_index (state->vectors, i);

			if (explicit_remove)
				graph_vector_unsubscribe (vectors);
			gtk_object_unref (GTK_OBJECT (vectors));
		}
		g_ptr_array_free (state->vectors, TRUE);
		state->vectors = NULL;
	}
}

static void
graph_guru_state_destroy (GraphGuruState *state)
{
	g_return_if_fail (state != NULL);

	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		gtk_object_unref (GTK_OBJECT (state->gui));
		state->gui = NULL;
	}

	graph_guru_clear_vectors (state, FALSE);

	if (state->ranges != NULL) {
		GSList *ptr = state->ranges;

		for (; ptr != NULL; ptr = ptr->next)
			g_free (ptr->data);
		g_slist_free (state->ranges);
		state->ranges = NULL;
	}

	if (state->control != CORBA_OBJECT_NIL) {
		CORBA_Environment ev;

		CORBA_exception_init (&ev);
		Bonobo_Control_unref (state->control, &ev);
		if (ev._major != CORBA_NO_EXCEPTION)
			g_warning ("Problems releasing the graph selector control");
		CORBA_exception_free (&ev);

		state->control = CORBA_OBJECT_NIL;
	}
	if (state->manager_client != NULL) {
		bonobo_object_unref (BONOBO_OBJECT(state->manager_client));
		state->manager_client = NULL;
		state->manager = CORBA_OBJECT_NIL;
	}

	/* Handle window manger closing the dialog.
	 * This will be ignored if we are being destroyed differently.
	 */
	wbcg_edit_finish (state->wbcg, FALSE);

	state->dialog = NULL;

	g_free (state);
}

static gboolean
cb_graph_guru_destroy (GtkObject *w, GraphGuruState *state)
{
	graph_guru_state_destroy (state);
	return FALSE;
}

static  gint
cb_graph_guru_key_press (GtkWidget *widget, GdkEventKey *event,
			 GraphGuruState *state)
{
	if (event->keyval == GDK_Escape) {
		wbcg_edit_finish (state->wbcg, FALSE);
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

static gboolean
cb_graph_vector_destroy (GtkObject *w, gpointer vector)
{
	printf ("vector destroy %p\n", vector);
	return FALSE;
}

static void
cb_graph_guru_clicked (GtkWidget *button, GraphGuruState *state)
{
	if (state->dialog == NULL)
		return;

	wbcg_set_entry (state->wbcg, NULL);

	if (button == state->button_prev) {
		graph_guru_set_page (state, state->current_page - 1);
		return;
	}

	if (button == state->button_next) {
		graph_guru_set_page (state, state->current_page + 1);
		return;
	}

	if (button == state->button_finish) {
		SheetObject *so = sheet_object_container_new (state->sheet);

		if (sheet_object_bonobo_set_server (SHEET_OBJECT_BONOBO (so),
						    state->manager_client)) {

			scg_mode_create_object (state->scg, so);

			/* Add a reference to the vector so that they continue to exist
			 * when the dialog goes away.  Then tie them to the destruction of
			 * the sheet object.
			 */
			if (state->vectors != NULL) {
				int i = state->vectors->len;
				while (i-- > 0) {
					gpointer *elem = g_ptr_array_index (state->vectors, i);
					gtk_object_ref (GTK_OBJECT (elem));
					gtk_signal_connect (GTK_OBJECT (so), "destroy",
						GTK_SIGNAL_FUNC (cb_graph_vector_destroy),
						elem);
				}
			}
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
	Bonobo_Unknown           corba_uih;
	GtkWidget *res = NULL;

	CORBA_exception_init (&ev);
	state->control = GNOME_Gnumeric_Graph_Manager_getTypeSelectControl (state->manager, &ev);
	if (ev._major != CORBA_NO_EXCEPTION)
		return NULL;
	CORBA_exception_free (&ev);

	corba_uih = bonobo_ui_component_get_container (state->wbcg->uic);
	res =  bonobo_widget_new_control_from_objref (state->control, corba_uih);

	return res;
}

static gboolean
graph_guru_init (GraphGuruState *state)
{
	GtkWidget *control;

	state->gui = gnumeric_glade_xml_new (state->wbcg, "graph-guru.glade");
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
	wbcg_edit_attach_guru (state->wbcg, state->dialog);
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

static void
cb_graph_manager_destroy (BonoboObjectClient *manager_client, gpointer ignored)
{
	printf ("GNUMERIC : unref the manager\n");
	bonobo_object_client_unref (manager_client, NULL);
}

static gboolean
graph_guru_init_manager (GraphGuruState *state)
{
	CORBA_Environment	 ev;
	Bonobo_Unknown		 o;

	CORBA_exception_init (&ev);
	o = (Bonobo_Unknown)oaf_activate ("repo_ids.has('IDL:Gnome/Gnumeric/Graph/Manager:1.0')",
					  NULL, 0, NULL, &ev);

	if (o != CORBA_OBJECT_NIL) {
		state->manager_client = bonobo_object_client_from_corba (o);

		if (state->manager_client != NULL) {
			/* Catch destroy so that we can unref the remote object */
			gtk_signal_connect (
				GTK_OBJECT (state->manager_client), "destroy",
				GTK_SIGNAL_FUNC (cb_graph_manager_destroy), NULL);

			state->manager = bonobo_object_query_interface (
				BONOBO_OBJECT (state->manager_client),
				"IDL:GNOME/Gnumeric/Graph/Manager:1.0");
		}
	}

	CORBA_exception_free (&ev);

	return (state->manager == CORBA_OBJECT_NIL);
}

static void
graph_guru_create_vectors_from_range (GraphGuruState *state, Range const *src)
{
	GraphVector *g_vector;
	int i, count;
	gboolean const has_header = range_has_header (state->sheet, src,
						      state->is_columns);
	Range vector = *src;

	if (state->is_columns) {
		if (has_header)
			vector.start.row++;
		count = vector.end.col - vector.start.col;
		vector.end.col = vector.start.col;
	} else {
		if (has_header)
			vector.start.col++;
		count = vector.end.row - vector.start.row;
		vector.end.row = vector.start.row;
	}

	for (i = 0 ; i <= count ; i++) {
		char *name = NULL;
		Cell const *cell = NULL;

		if (has_header)
			cell = (state->is_columns)
				? sheet_cell_get (state->sheet,
						  vector.start.col,
						  vector.start.row-1)
				: sheet_cell_get (state->sheet,
						  vector.start.col-1,
						  vector.start.row);

		/* Create a default name if need be */
		if (cell == NULL)
			name = g_strdup_printf (_("series%d"), state->vectors->len+1);
		else
			name = value_get_as_string (cell->value);

		g_vector = graph_vector_new (state->sheet, &vector, name);
		graph_vector_set_subscriber (g_vector, state->manager);
		g_ptr_array_add (state->vectors, g_vector);

		if (state->is_columns)
			vector.end.col = ++vector.start.col;
		else
			vector.end.row = ++vector.start.row;
	}
}

static void
cb_data_simple_page (GtkNotebook *notebook, GtkNotebookPage *page,
		     gint page_num, gpointer user_data)
{
	puts ("data selector");
}

static void
cb_data_simple_col_row_toggle (GtkToggleButton *button, GraphGuruState *state)
{
	GSList	*ptr;
	state->is_columns = gtk_toggle_button_get_active (button);

	graph_guru_clear_vectors (state, TRUE);

	state->vectors = g_ptr_array_new ();
	for (ptr = state->ranges; ptr != NULL ; ptr = ptr->next)
		graph_guru_create_vectors_from_range (state, ptr->data);
}

/**
 * graph_guru_init_vectors :
 *
 * Guess at what to plot based on the current selection.
 * Then initialize the data page of the guru
 */
static gboolean
graph_guru_init_vectors (GraphGuruState *state)
{
	GSList	*ptr;
	Range const * r;
	int num_rows, num_cols;

	r = selection_first_range (state->sheet, NULL, NULL);
	num_cols = range_width (r);
	num_rows = range_height (r);

	/* Excel docs claim that rows == cols uses rows */
	state->is_columns = num_cols < num_rows;
	state->ranges = selection_get_ranges (state->sheet, TRUE);

	state->vectors = g_ptr_array_new ();
	for (ptr = state->ranges; ptr != NULL ; ptr = ptr->next)
		graph_guru_create_vectors_from_range (state, ptr->data);

	/* simple selection */
	state->data_is_cols_radio = glade_xml_get_widget (state->gui, "data_is_cols");
	state->data_is_rows_radio = glade_xml_get_widget (state->gui, "data_is_rows");
	state->data_range = glade_xml_get_widget (state->gui, "data_range");

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->data_is_cols_radio),
				      state->is_columns);

	/* Only need to set it for is_cols due to the radio ness */
	gtk_signal_connect (GTK_OBJECT (state->data_is_cols_radio),
			    "toggled", GTK_SIGNAL_FUNC (cb_data_simple_col_row_toggle),
			    state);

	/* complex selection */
	state->data_series_list = glade_xml_get_widget (state->gui, "data_series_list");
	state->data_series_name = glade_xml_get_widget (state->gui, "data_series_name");
	state->data_series_range = glade_xml_get_widget (state->gui, "data_series_range");
	state->data_add_series = glade_xml_get_widget (state->gui, "data_add_series");
	state->data_remove_series = glade_xml_get_widget (state->gui, "data_remove_series");

	/* Data widgets */
	state->data_notebook = glade_xml_get_widget (state->gui, "data_notebook");
	gtk_notebook_set_page (GTK_NOTEBOOK (state->data_notebook),
			       (state->ranges->next != NULL) ? 1 : 0);
	gtk_signal_connect (GTK_OBJECT (state->data_notebook),
			    "switch_page", GTK_SIGNAL_FUNC (cb_data_simple_page),
			    state);

	return FALSE;
}

/**
 * dialog_graph_guru
 * @wb : The workbook to use as a parent window.
 *
 * Pop up a graph guru.
 */
void
dialog_graph_guru (WorkbookControlGUI *wbcg)
{
	GraphGuruState *state;

	g_return_if_fail (wbcg != NULL);

	state = g_new0(GraphGuruState, 1);
	state->wbcg	= wbcg;
	state->scg	= wb_control_gui_cur_sheet (wbcg);
	state->wb	= wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet	= wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	state->valid	= FALSE;
	state->vectors  = NULL;
	state->ranges   = NULL;
	state->gui	= NULL;
	state->control = CORBA_OBJECT_NIL;
	state->manager = CORBA_OBJECT_NIL;
	state->manager_client = NULL;

	if (graph_guru_init_manager (state) || graph_guru_init (state) ||
	    graph_guru_init_vectors (state)) {
		graph_guru_state_destroy (state);
		return;
	}

	/* Ok everything is hooked up. Let-er rip */
	state->valid = TRUE;
}
