/* vim: set sw=8: */

/*
 * dialog-graph-guru.c:  The Graph guru
 *
 * Copyright (C) 2000-2001 Jody Goldberg (jgoldberg@home.com)
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
#include "gnumeric-graph.h"
#include "gui-util.h"
#include "ranges.h"
#include "selection.h"
#include "expr.h"
#include "sheet.h"
#include "workbook-edit.h"
#include "sheet-control-gui.h"
#include "sheet-object.h"
#include "dialogs.h"
#include "widgets/gnumeric-combo-text.h"

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <gal/util/e-xml-utils.h>

typedef struct _GraphGuruState GraphGuruState;
typedef struct
{
	GraphGuruState	*state;
	xmlChar	  	*element;
	int	   	 index;
	gboolean   	 is_optional, is_shared;
	gboolean   	 changed;
	GnmGraphVector	*vector;

	GtkWidget	  *name_label;
	GnumericExprEntry *entry;
} VectorState;

struct _GraphGuruState
{
	GnmGraph	*graph;
	Bonobo_Control	 control;

	/* GUI accessors */
	GladeXML    *gui;
	GtkWidget   *dialog;
	GtkWidget   *button_cancel;
	GtkWidget   *button_prev;
	GtkWidget   *button_next;
	GtkWidget   *button_finish;
	GtkNotebook *steps;

	GtkWidget *sample_frame;
	GtkWidget *plot_selector;
	GtkWidget *plot_add;
	GtkWidget *plot_remove;
	GtkWidget *series_selector;
	GtkWidget *series_add;
	GtkWidget *series_remove;
	GtkWidget *series_details;
	GtkWidget *shared_series_details;
	GtkWidget *sample;
	GtkWidget *selection_table;
	GtkWidget *shared_separator;

	GPtrArray *shared, *unshared;

	/* internal state */
	int current_page, initial_page;
	gboolean valid;
	gboolean updating;
	VectorState *current_vector;

	gboolean  is_columns;
	xmlDoc 	 *xml_doc;
	xmlNode	 *current_plot;
	xmlNode  *current_series;

	/* external state */
	WorkbookControlGUI *wbcg;
	SheetControlGUI	   *scg;
	Workbook	   *wb;
	Sheet		   *sheet;
};

static void
graph_guru_clear_sample (GraphGuruState *state)
{
	if (state->sample != NULL) {
		gtk_object_destroy (GTK_OBJECT (state->sample));
		state->sample = NULL;
	}
}

static void
vector_state_set_entry (VectorState *vs)
{
	gnumeric_expr_entry_set_rangesel_from_dep (
		vs->entry,
		gnm_graph_vector_get_dependent (vs->vector));
	gnumeric_expr_entry_set_flags (vs->entry,
		GNUM_EE_ABS_COL|GNUM_EE_ABS_ROW, GNUM_EE_MASK);
}

static void
vector_state_fill (VectorState *vs, xmlNode *series)
{
	xmlNode *dim;
	int id;

	g_return_if_fail (!vs->state->updating);

	/* clear beforehand to make error handling simpler */
	vs->state->updating = TRUE;
	gnumeric_expr_entry_clear (vs->entry);
	gnumeric_expr_entry_set_flags (vs->entry,
		GNUM_EE_ABS_COL|GNUM_EE_ABS_ROW, GNUM_EE_MASK);
	vs->state->updating = FALSE;

	vs->vector = NULL;
	dim = gnm_graph_series_get_dimension (series, vs->element);
	if (dim != NULL) {
		id = e_xml_get_integer_prop_by_name_with_default (dim, "ID", -1);
		if (id >= 0) {
			vs->vector = gnm_graph_get_vector (vs->state->graph, id);
			vector_state_set_entry (vs);
		}
	}
}

static void
vector_state_init (VectorState *vs, xmlNode *descriptor)
{
	xmlChar *tmp;
	char *name;
	gboolean required;

	if (vs->element != NULL)
		xmlFree (vs->element);
	vs->element = xmlGetProp (descriptor, "element");

	required = e_xml_get_bool_prop_by_name_with_default (descriptor,
		"required", FALSE);

	tmp = xmlNodeGetContent (descriptor);
	name = required ? g_strdup (tmp) : g_strdup_printf ("(%s)", tmp);
	gtk_label_set_text (GTK_LABEL (vs->name_label), name);
	xmlFree (tmp);
	g_free (name);

	gtk_widget_show (vs->name_label);
	gtk_widget_show (GTK_WIDGET (vs->entry));
}

static void
cb_graph_guru_entry_changed (GtkEditable *editable, VectorState *vs)
{
	if (!vs->state->updating)
		vs->changed = TRUE;
}

static VectorState *
vector_state_new (GraphGuruState *state, gboolean shared, int indx)
{
	VectorState *vs;
	GtkTable    *table;
	GtkWidget   *alignment = gtk_alignment_new (1., .5, 0., 0.);

	vs = g_new0 (VectorState, 1);
	vs->state = state;
	vs->index = indx;
	vs->element  = NULL;
	vs->is_shared = shared;
	vs->changed = FALSE;
	vs->vector  = NULL;

	table = GTK_TABLE (shared
			   ? state->shared_series_details
			   : state->series_details);

	vs->name_label = gtk_label_new ("");
	gtk_container_add (GTK_CONTAINER (alignment), vs->name_label);
	gtk_table_attach (table, alignment,
		0, 1, indx, indx+1, GTK_FILL, 0, 5, 3);

	vs->entry = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_scg (vs->entry, state->scg);
	gtk_table_attach (table, GTK_WIDGET (vs->entry),
		1, 2, indx, indx+1, GTK_EXPAND|GTK_FILL, 0, 5, 3);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
		GTK_EDITABLE (vs->entry));

	/* flag when things change so we'll know if we need to update the vector */
	gtk_signal_connect (GTK_OBJECT (vs->entry),
		"changed",
		GTK_SIGNAL_FUNC (cb_graph_guru_entry_changed), vs);
	gtk_object_set_data (GTK_OBJECT (vs->entry), "VectorState", vs);

	return vs;
}

static void
vector_state_destroy (VectorState *vs, gboolean destroywidgets)
{
	vs->vector = NULL;
	if (vs->element) {
		xmlFree (vs->element);
		vs->element = NULL;
	}

	if (destroywidgets) {
		gtk_widget_destroy (GTK_WIDGET (vs->entry));
		gtk_widget_destroy (GTK_WIDGET (vs->name_label));
	}

	g_free (vs);
}

static void
vector_state_array_shorten (GPtrArray *a, int len)
{
	int i = a->len;
	while (len < i--) {
		vector_state_destroy (g_ptr_array_index (a, i), TRUE);
		g_ptr_array_remove_index_fast (a, i);
	}
}

static void
graph_guru_state_destroy (GraphGuruState *state)
{
	g_return_if_fail (state != NULL);

	wbcg_edit_detach_guru (state->wbcg);

	if (state->graph != NULL) {
		gtk_object_unref (GTK_OBJECT (state->graph));
		state->graph = NULL;
	}

	if (state->gui != NULL) {
		gtk_object_unref (GTK_OBJECT (state->gui));
		state->gui = NULL;
	}

	if (state->shared != NULL) {
		int i = state->shared->len;
		while (i-- > 0)
			vector_state_destroy (g_ptr_array_index (state->shared, i), FALSE);
		g_ptr_array_free (state->shared, TRUE);
		state->shared = NULL;
	}

	if (state->unshared != NULL) {
		int i = state->unshared->len;
		while (i-- > 0)
			vector_state_destroy (g_ptr_array_index (state->unshared, i), FALSE);
		g_ptr_array_free (state->unshared, TRUE);
		state->unshared = NULL;
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

static char *
graph_guru_series_name (GraphGuruState *s, xmlNode *series)
{
	int i;
	xmlChar *name = xmlGetProp (series, "name");

	if (name != NULL) {
		char *res = g_strdup (name);
		xmlFree (name);
		return res;
	}
	i = e_xml_get_integer_prop_by_name_with_default (series, "index", -1);

	g_return_val_if_fail (i >= 0, g_strdup ("ERROR Missing Index"));

	return g_strdup_printf ("Series%d", i+1);
}

static void
graph_guru_select_series (GraphGuruState *s, xmlNode *series)
{
	int i;
	char *name;

	if (s->updating || series == s->current_series)
		return;

	name = graph_guru_series_name (s, series);
	s->updating = TRUE;
	gnm_combo_text_set_text (GNM_COMBO_TEXT (s->series_selector),
		name, GNM_COMBO_TEXT_CURRENT);
	s->updating = FALSE;

	for (i = s->unshared->len; i--> 0 ; )
		vector_state_fill (g_ptr_array_index (s->unshared, i), series);
	for (i = s->shared->len; i--> 0 ; )
		vector_state_fill (g_ptr_array_index (s->shared, i), series);

	s->current_series = series;
}

static char *
graph_guru_plot_name (GraphGuruState *s, xmlNode *plot)
{
	char const *t = "Plot";
	int i = e_xml_get_integer_prop_by_name_with_default (plot, "index", -1);
	xmlNode *type = e_xml_get_child_by_name (plot, "Type");

	g_return_val_if_fail (i >= 0, g_strdup ("ERROR Missing Index"));

	if (type != NULL && type->xmlChildrenNode)
		t = type->xmlChildrenNode->name;

	return g_strdup_printf ("%s%d", t, i+1);
}

static void
graph_guru_select_plot (GraphGuruState *s, xmlNode *plot)
{
	xmlNode *layout, *series;
	char *name;
	GtkWidget *item;
	int shared, unshared;

	if (s->updating || plot == s->current_plot)
		return;

	g_return_if_fail (series != NULL);

	/* clear out the old */
	if (s->current_plot != NULL)
		gnm_combo_text_clear (GNM_COMBO_TEXT (s->series_selector));
	s->current_plot = plot;
	s->current_series = NULL;

	/* Init the expr entries */
	layout = e_xml_get_child_by_name (plot, "DataLayout");

	g_return_if_fail (layout != NULL);

	shared = unshared = 0;
	for (layout = layout->xmlChildrenNode; layout; layout = layout->next) {
		int indx;
		gboolean is_shared;
		GPtrArray   *container;
		VectorState *vs;

		if (strcmp (layout->name, "Dimension"))
			continue;

		is_shared = e_xml_get_bool_prop_by_name_with_default (layout,
			"shared", FALSE);
		if (is_shared) {
			container = s->shared;
			indx = shared++;
		} else {
			container = s->unshared;
			indx = unshared++;
		}

		if (indx >= (int)(container->len)) {
			vs = vector_state_new (s, is_shared, indx);
			g_ptr_array_add (container, vs);
		} else
			vs = g_ptr_array_index (container, indx);
		vector_state_init (vs, layout);
	}
	vector_state_array_shorten (s->unshared, unshared);
	vector_state_array_shorten (s->shared, shared);
	if (shared > 0)
		gtk_widget_show (s->shared_separator);
	else
		gtk_widget_hide (s->shared_separator);

	/* Init lists of series */
	series = e_xml_get_child_by_name (plot, "Data");

	g_return_if_fail (series != NULL);

	for (series = series->xmlChildrenNode; series; series = series->next) {
		if (strcmp (series->name, "Series"))
			continue;
		name = graph_guru_series_name (s, series);
		item = gnm_combo_text_add_item (
			GNM_COMBO_TEXT (s->series_selector), name);
		gtk_object_set_data (GTK_OBJECT (item), "xml_node", series);
		g_free (name);
		if (s->current_series == NULL)
			graph_guru_select_series (s, series);
	}

	s->updating = TRUE;
	name = graph_guru_plot_name (s, plot);
	gnm_combo_text_set_text (GNM_COMBO_TEXT (s->plot_selector),
		name, GNM_COMBO_TEXT_CURRENT);
	g_free (name);
	s->updating = FALSE;

	gtk_widget_show_all (s->series_details);
	gtk_widget_show_all (s->shared_series_details);

	g_return_if_fail (s->current_series != NULL);
}

static void
graph_guru_init_data_page (GraphGuruState *s)
{
	xmlNode *plot;
	char *name;
	GtkWidget *item;

	g_return_if_fail (s->xml_doc != NULL);
#if 0
	xmlDocDump (stdout, s->xml_doc);
#endif

	graph_guru_clear_sample (s);

	s->sample = gnm_graph_get_config_control (s->graph, "Sample"),
	gtk_container_add (GTK_CONTAINER (s->sample_frame), s->sample);
	gtk_widget_show_all (s->sample_frame);

	/* Init lists of plots */
	plot = e_xml_get_child_by_name (s->xml_doc->xmlRootNode, "Plots");

	g_return_if_fail (plot != NULL);

	for (plot = plot->xmlChildrenNode; plot; plot = plot->next) {
		if (strcmp (plot->name, "Plot"))
			continue;
		name = graph_guru_plot_name (s, plot);
		item = gnm_combo_text_add_item (
			GNM_COMBO_TEXT (s->plot_selector), name);
		gtk_object_set_data (GTK_OBJECT (item), "xml_node", plot);
		g_free (name);
		if (s->current_plot == NULL)
			graph_guru_select_plot (s, plot);
	}
}

static void
graph_guru_set_page (GraphGuruState *state, int page)
{
	char *name;
	gboolean prev_ok = TRUE, next_ok = TRUE;

	if (state->current_page == page)
		return;

	if (page != 0)
		state->xml_doc = gnm_graph_get_spec (state->graph);

	switch (page) {
	case 0: name = _("Step 1 of 3: Select graph type");
		prev_ok = FALSE;
		graph_guru_clear_sample (state);
		state->current_plot = NULL;
		break;
	case 1:
		if (state->initial_page == 0) {
			name = _("Step 2 of 3: Select data ranges");
		} else {
			name = _("Graph Data");
			next_ok = prev_ok = FALSE;
		}
		graph_guru_init_data_page (state);
		break;
	case 2: name = _("Step 3 of 3: Customize graph");
		next_ok = FALSE;
		break;

	default:
		g_warning ("Invalid Graph Guru page");
		return;
	}

	state->current_page = page;
	gtk_notebook_set_page (state->steps, page - state->initial_page);
	gtk_window_set_title (GTK_WINDOW (state->dialog), name);
	gtk_widget_set_sensitive (state->button_prev, prev_ok);
	gtk_widget_set_sensitive (state->button_next, next_ok);
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
		gtk_object_ref (GTK_OBJECT (state->graph));
		scg_mode_create_object (state->scg, SHEET_OBJECT (state->graph));
	}

	gtk_object_destroy (GTK_OBJECT (state->dialog));
}

static GtkWidget *
graph_guru_init_button  (GraphGuruState *state, const char *widget_name)
{
	GtkWidget *tmp = glade_xml_get_widget (state->gui, widget_name);
	gtk_signal_connect (GTK_OBJECT (tmp),
		"clicked",
		GTK_SIGNAL_FUNC (cb_graph_guru_clicked), state);

	return tmp;
}


static gboolean
cb_series_entry_changed (GtkWidget *caller, char *new_text, GraphGuruState *s)
{
	if (s->updating)
		return TRUE;
	return TRUE;
}
static gboolean
cb_series_selection_changed (GtkWidget *ct, GtkWidget *item, GraphGuruState *s)
{
	graph_guru_select_series (s,
		gtk_object_get_data (GTK_OBJECT (item), "xml_node"));
	return FALSE;
}

static gboolean
cb_plot_entry_changed (GtkWidget *caller, char *new_text, GraphGuruState *s)
{
	if (s->updating)
		return TRUE;
	return TRUE;
}
static gboolean
cb_plot_selection_changed (GtkWidget *ct, GtkWidget *item, GraphGuruState *s)
{
	graph_guru_select_plot (s,
		gtk_object_get_data (GTK_OBJECT (item), "xml_node"));
	return FALSE;
}

static GtkWidget *
graph_guru_selector_init (GraphGuruState *s, char const *name, int i,
			  GtkSignalFunc	entry_changed,
			  GtkSignalFunc	selection_changed)
{
	GtkWidget    *w  = gnm_combo_text_new (NULL);
	GnmComboText *ct = GNM_COMBO_TEXT (w);
	gtk_table_attach_defaults (GTK_TABLE (s->selection_table),
		w, 1, 2, i, i+1);
	gtk_combo_box_set_title (GTK_COMBO_BOX (ct), _(name));
	gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (ct), GTK_RELIEF_NONE);
	gtk_signal_connect (GTK_OBJECT (ct), "entry_changed",
		entry_changed, s);
	gtk_signal_connect (GTK_OBJECT (ct), "selection_changed",
		selection_changed, s);

	return w;
}

static void
cb_graph_guru_focus (GtkWindow *window, GtkWidget *focus, GraphGuruState *state)
{
	VectorState *vs = state->current_vector;

	if (vs != NULL && vs->changed) {
		char const *str = gtk_entry_get_text (GTK_ENTRY (vs->entry));
		state->current_vector = NULL;
		vs->changed = FALSE;

		printf ("parse (%s): %s\n", vs->element, str);

		/* Are we adding a dimension that was not there previously ? */
		if (*str) {
			if (vs->vector != NULL) {
				puts ("changing an existing");
			} else {
				puts ("Adding a new dimension");
			}
		} else if (vs->vector != NULL) {
			/* removing an existing dimension */
			if (vs->is_optional) {
				puts ("Remove an optional dimension");
			} else {
				/* We need this, reset to original content */
				state->updating = TRUE;
				vector_state_set_entry (vs);
				state->updating = FALSE;
			}
		}
	}

	if (focus != NULL) {
		vs = gtk_object_get_data (GTK_OBJECT (focus), "VectorState");
		if (vs != NULL) {
			state->current_vector = vs;
			vs->changed = FALSE;
			wbcg_set_entry (vs->state->wbcg, vs->entry);
		}
	}
}

static gboolean
graph_guru_init (GraphGuruState *s)
{
	s->gui = gnumeric_glade_xml_new (s->wbcg, "graph-guru.glade");
        if (s->gui == NULL)
                return TRUE;

	s->dialog = glade_xml_get_widget (s->gui, "GraphGuru");
	s->steps  = GTK_NOTEBOOK (glade_xml_get_widget (s->gui, "notebook"));

	/* Buttons */
	s->button_cancel = graph_guru_init_button (s, "button_cancel");
	s->button_prev = graph_guru_init_button (s, "button_prev");
	s->button_next = graph_guru_init_button (s, "button_next");
	s->button_finish = graph_guru_init_button (s, "button_finish");

	s->plot_add	    = glade_xml_get_widget (s->gui, "plot_add");
	s->plot_remove	    = glade_xml_get_widget (s->gui, "plot_remove");
	s->series_add	    = glade_xml_get_widget (s->gui, "series_add");
	s->series_remove    = glade_xml_get_widget (s->gui, "series_remove");
	s->series_details   = glade_xml_get_widget (s->gui, "series_details");
	s->selection_table  = glade_xml_get_widget (s->gui, "selection_table");
	s->shared_separator = glade_xml_get_widget (s->gui, "shared_separator");
	s->sample_frame	    = glade_xml_get_widget (s->gui, "sample_frame");
	s->shared_series_details = glade_xml_get_widget (s->gui, "shared_series_details");

	s->plot_selector    = graph_guru_selector_init (s, N_("Plot name"), 0,
		GTK_SIGNAL_FUNC (cb_plot_entry_changed),
		GTK_SIGNAL_FUNC (cb_plot_selection_changed));
	s->series_selector  = graph_guru_selector_init (s, N_("Series name"), 1,
		GTK_SIGNAL_FUNC (cb_series_entry_changed),
		GTK_SIGNAL_FUNC (cb_series_selection_changed));

	/* Lifecyle management */
	wbcg_edit_attach_guru (s->wbcg, s->dialog);
	gtk_signal_connect (GTK_OBJECT (s->dialog),
		"destroy",
		GTK_SIGNAL_FUNC (cb_graph_guru_destroy), s);
	gtk_signal_connect (GTK_OBJECT (s->dialog),
		"key_press_event",
		GTK_SIGNAL_FUNC (cb_graph_guru_key_press), s);
	gtk_signal_connect (GTK_OBJECT (s->dialog),
		"set-focus",
		GTK_SIGNAL_FUNC (cb_graph_guru_focus), s);

	return FALSE;
}

/**
 * dialog_graph_guru
 * @wb : The workbook to use as a parent window.
 * @graph : the graph to edit
 * @page : the page to start on.
 *
 * Pop up a graph guru.
 */
void
dialog_graph_guru (WorkbookControlGUI *wbcg, GnmGraph *graph, int page)
{
	GraphGuruState *state;

	g_return_if_fail (wbcg != NULL);

	state = g_new0 (GraphGuruState, 1);
	state->wbcg	= wbcg;
	state->scg	= wb_control_gui_cur_sheet (wbcg);
	state->wb	= wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet	= wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	state->valid	= FALSE;
	state->updating = FALSE;
	state->gui	= NULL;
	state->control  = CORBA_OBJECT_NIL;
	state->xml_doc  = NULL;
	state->sample   = NULL;
	state->shared   = g_ptr_array_new ();
	state->unshared = g_ptr_array_new ();
	state->current_page = -1;
	state->current_plot   = NULL;
	state->current_series = NULL;
	state->current_vector = NULL;

	if (graph != NULL) {
		g_return_if_fail (IS_GNUMERIC_GRAPH (graph));

		state->graph = graph;
		gtk_object_ref (GTK_OBJECT (state->graph));
	} else
		state->graph = gnm_graph_new (state->wb);

	if (page == 0) {
		GList *ptr;
		Range const * r = selection_first_range (state->sheet,
			NULL, NULL);
		int num_cols = range_width (r);
		int num_rows = range_height (r);

		gnm_graph_clear_vectors (state->graph);

		/* Excel docs claim that rows == cols uses rows */
		state->is_columns = num_cols < num_rows;
		for (ptr = state->sheet->selections; ptr != NULL; ptr = ptr->next)
			gnm_graph_range_to_vectors (state->graph, state->sheet,
			ptr->data, state->is_columns);
		gnm_graph_arrange_vectors (state->graph);
	}

	if (graph_guru_init (state)) {
		graph_guru_state_destroy (state);
		return;
	}

	/* Ok everything is hooked up. Let-er rip */
	state->valid = TRUE;

	state->initial_page = page;
	if (page == 0) {
		GtkWidget *control = gnm_graph_get_config_control (state->graph, "Type");
		gtk_notebook_prepend_page (state->steps, control, NULL);
	}

	gtk_widget_show_all (state->dialog);

	graph_guru_set_page (state, page);
}
