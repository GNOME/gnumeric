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

#include <bonobo.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <gal/util/e-xml-utils.h>
#include <gnome-xml/parser.h>

#define CONFIG_GURU		GNOME_Gnumeric_Graph_v1_ConfigGuru
#define CONFIG_GURU1(suffix)	GNOME_Gnumeric_Graph_v1_ConfigGuru_ ## suffix
#define DATA_GURU		GNOME_Gnumeric_Graph_v1_DataGuru
#define DATA_GURU1(suffix)	GNOME_Gnumeric_Graph_v1_DataGuru_ ## suffix

typedef struct _GraphGuruState GraphGuruState;
typedef struct
{
	GraphGuruState	*state;
	xmlChar	  	*element;
	int	   	 dim_index, series_index;
	gboolean   	 is_optional, is_shared;
	gboolean   	 changed;
	GnmGraphVector	*vector;

	GtkWidget	  *name_label;
	GnumericExprEntry *entry;
} VectorState;

struct _GraphGuruState
{
	GnmGraph	*graph;

	/* GUI accessors */
	GladeXML    *gui;
	GtkWidget   *dialog;
	GtkWidget   *button_cancel;
	GtkWidget   *button_prev;
	GtkWidget   *button_next;
	GtkWidget   *button_finish;
	GtkNotebook *steps;

	GtkWidget *plot_selector;
	GtkWidget *plot_add;
	GtkWidget *plot_remove;
	GtkWidget *series_selector;
	GtkWidget *series_add;
	GtkWidget *series_remove;
	GtkWidget *series_details;
	GtkWidget *shared_series_details;
	GtkWidget *sample_frame;

	GtkWidget *selection_table;
	GtkWidget *shared_separator;

	CONFIG_GURU type_selector;
	DATA_GURU   data_guru;
	GtkWidget *sample;
	GPtrArray *shared, *unshared;

	/* internal state */
	VectorState *current_vector;
	int current_plot, current_series;
	int current_page, initial_page;
	gboolean valid;
	gboolean updating;
	xmlDoc *xml_doc;

	gboolean  is_columns;

	/* external state */
	WorkbookControlGUI *wbcg;
	SheetControlGUI	   *scg;
	Workbook	   *wb;
	Sheet		   *sheet;
};

static void graph_guru_select_plot (GraphGuruState *s, xmlNode *plot);

static void
graph_guru_clear_xml (GraphGuruState *state)
{
	if (state->xml_doc != NULL) {
		xmlFreeDoc (state->xml_doc);
		state->xml_doc = NULL;
	}
}

static xmlNode *
graph_guru_get_plot (GraphGuruState *s, int indx)
{
	xmlDoc *xml_doc = s->xml_doc;
	xmlNode *plot = e_xml_get_child_by_name (xml_doc->xmlRootNode, "Plots");

	g_return_val_if_fail (plot != NULL, NULL);

	for (plot = plot->xmlChildrenNode; plot; plot = plot->next) {
		if (strcmp (plot->name, "Plot"))
			continue;
		if (indx == e_xml_get_integer_prop_by_name_with_default (plot, "index", -1))
			return plot;
	}
	return NULL;
}

static xmlNode *
graph_guru_get_series (GraphGuruState *s, int indx)
{
	xmlNode *plot = graph_guru_get_plot (s, s->current_plot);
	xmlNode *series = e_xml_get_child_by_name (plot, "Data");

	g_return_val_if_fail (series != NULL, NULL);

	for (series = series->xmlChildrenNode; series; series = series->next) {
		if (strcmp (series->name, "Series"))
			continue;
		if (indx == e_xml_get_integer_prop_by_name_with_default (series, "index", -1))
			return series;
	}
	return NULL;
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
graph_guru_get_spec (GraphGuruState *s)
{
	int indx;
	xmlNode *plot;
	char *name;
	GtkWidget *item;
	xmlDoc *xml_doc = NULL;
	GNOME_Gnumeric_Buffer *spec;
	CORBA_Environment  ev;

	if (s->data_guru == CORBA_OBJECT_NIL)
		return;
	CORBA_exception_init (&ev);
	spec = DATA_GURU1 (_get_spec) (s->data_guru, &ev);
	if (ev._major == CORBA_NO_EXCEPTION) {
		xmlParserCtxtPtr pctxt;

		/* A limit in libxml */
		if (spec->_length >= 4) {
			pctxt = xmlCreatePushParserCtxt (NULL, NULL,
				spec->_buffer, spec->_length, NULL);
			xmlParseChunk (pctxt, "", 0, TRUE);
			xml_doc = pctxt->myDoc;
			xmlFreeParserCtxt (pctxt);
		}
		GNOME_Gnumeric_Buffer__free (spec, 0, TRUE);
	} else {
		g_warning ("'%s' : getting the spec from data_guru %p",
			   gnm_graph_exception (&ev), s->data_guru);
	}

	g_return_if_fail (xml_doc != NULL);
#if 0
	xmlDocDump (stdout, xml_doc);
#endif

	graph_guru_clear_xml (s);
	s->xml_doc = xml_doc;

	s->updating = TRUE;
	gnm_combo_text_clear (GNM_COMBO_TEXT (s->plot_selector));
	s->updating = FALSE;

	/* Init lists of plots */
	plot = e_xml_get_child_by_name (xml_doc->xmlRootNode, "Plots");

	g_return_if_fail (plot != NULL);

	for (plot = plot->xmlChildrenNode; plot; plot = plot->next) {
		if (strcmp (plot->name, "Plot"))
			continue;
		name = graph_guru_plot_name (s, plot);
		item = gnm_combo_text_add_item (
			GNM_COMBO_TEXT (s->plot_selector), name);
		g_free (name);

		indx = e_xml_get_integer_prop_by_name_with_default (plot, "index", -1);
		g_return_if_fail (indx >= 0);
		gtk_object_set_data (GTK_OBJECT (item), "index", 
			GINT_TO_POINTER (indx));

		if (s->current_plot < 0 || s->current_plot == indx)
			graph_guru_select_plot (s, plot);
	}
}

#if 0
static void
graph_guru_series_delete (GraphGuruState *state, int series_id)
{
	gboolean ok;
	CORBA_Environment  ev;

	if (state->data_guru == CORBA_OBJECT_NIL)
		return;

	CORBA_exception_init (&ev);
	DATA_GURU1 (seriesDelete) (state->data_guru, series_id, &ev);
	ok = (ev._major == CORBA_NO_EXCEPTION);
	if (ok) {
	} else {
		g_warning ("'%s' : deleting a series from graph data_guru %p",
			   gnm_graph_exception (&ev), state->data_guru);
	}
	CORBA_exception_free (&ev);
}
#endif

static void
vector_state_series_set_dimension (VectorState *vs, ExprTree *expr)
{
	CORBA_Environment  ev;
	int vector_id = -1;

	if (vs->state == NULL || vs->state->data_guru == CORBA_OBJECT_NIL)
		return;

	if (expr != NULL)
		vector_id = gnm_graph_add_vector (vs->state->graph,
			expr, GNM_VECTOR_AUTO, vs->state->sheet);

	/* Future simplification.  If we are changing an unshared dimension we
	 * can do the substitution in place.  and just tweak the expression.
	 */
	CORBA_exception_init (&ev);
	DATA_GURU1 (seriesSetDimension) (vs->state->data_guru,
		vs->series_index, vs->element, vector_id, &ev);
	if (ev._major == CORBA_NO_EXCEPTION)
		graph_guru_get_spec (vs->state);
	else {
		g_warning ("'%s' : changing a dimension from graph data_guru %p",
			   gnm_graph_exception (&ev), vs->state->data_guru);
	}
	CORBA_exception_free (&ev);
}

static void
vector_state_fill (VectorState *vs, xmlNode *series)
{
	xmlNode *dim;
	int id;

	g_return_if_fail (!vs->state->updating);

	/* clear beforehand to make error handling simpler */
	vs->series_index =
		e_xml_get_integer_prop_by_name_with_default (series, "index", -1);
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
			gnumeric_expr_entry_set_rangesel_from_dep (
				vs->entry,
				gnm_graph_vector_get_dependent (vs->vector));
			gnumeric_expr_entry_set_flags (vs->entry,
				GNUM_EE_ABS_COL|GNUM_EE_ABS_ROW, GNUM_EE_MASK);
		}
	}
}

static void
vector_state_apply_changes (VectorState *vs)
{
	char const *str;
	ExprTree *expr = NULL;
	gboolean changed;

	if (vs == NULL || !vs->changed)
		return;

	str = gtk_entry_get_text (GTK_ENTRY (vs->entry));
	/* If we are setting something */
	if (*str != '\0') {
		ParsePos pos;
		expr = expr_parse_string (str,
			parse_pos_init (&pos, NULL, vs->state->sheet, 0, 0),
			NULL, NULL);
		/* TODO : add some error dialogs split out
		 * the code in workbok_edit.
		 */
		changed = (expr != NULL);
	} else
		/* or we are clearing something optional */
		changed = (vs->is_optional && vs->vector != NULL);

	if (changed)
		vector_state_series_set_dimension (vs, expr);

	vector_state_fill (vs, 
		graph_guru_get_series (vs->state, vs->series_index));

	vs->state->current_vector = NULL;
	vs->changed = FALSE;
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
vector_state_new (GraphGuruState *state, gboolean shared, int dim_indx)
{
	VectorState *vs;
	GtkTable    *table;
	GtkWidget   *alignment = gtk_alignment_new (1., .5, 0., 0.);

	vs = g_new0 (VectorState, 1);
	vs->state = state;
	vs->dim_index = dim_indx;
	vs->series_index = -1;
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
		0, 1, dim_indx, dim_indx+1, GTK_FILL, 0, 5, 3);

	vs->entry = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_scg (vs->entry, state->scg);
	gtk_table_attach (table, GTK_WIDGET (vs->entry),
		1, 2, dim_indx, dim_indx+1, GTK_EXPAND|GTK_FILL, 0, 5, 3);
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

	graph_guru_select_plot (state, NULL);

	if (state->plot_selector) {
		gtk_signal_disconnect_by_data (
			GTK_OBJECT (state->plot_selector), state);
		state->plot_selector = NULL;
	}
	if (state->series_selector != NULL) {
		gtk_signal_disconnect_by_data (
			GTK_OBJECT (state->series_selector), state);
			state->series_selector = NULL;
	}

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

	graph_guru_clear_xml (state);

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

	if (s->updating)
		return;

	name = graph_guru_series_name (s, series);
	s->updating = TRUE;
	gnm_combo_text_set_text (GNM_COMBO_TEXT (s->series_selector),
		name, GNM_COMBO_TEXT_CURRENT);
	g_free (name);
	s->updating = FALSE;

	for (i = s->unshared->len; i--> 0 ; )
		vector_state_fill (g_ptr_array_index (s->unshared, i), series);
	for (i = s->shared->len; i--> 0 ; )
		vector_state_fill (g_ptr_array_index (s->shared, i), series);

	s->current_series =
		e_xml_get_integer_prop_by_name_with_default (series, "index", -1);
}

static void
graph_guru_select_plot (GraphGuruState *s, xmlNode *plot)
{
	xmlNode *layout, *series;
	char *name;
	GtkWidget *item;
	int shared, unshared;
	int indx;

	if (s->updating)
		return;

	/* clear out the old */
	if (s->current_plot >= 0) {
		gnm_combo_text_clear (GNM_COMBO_TEXT (s->series_selector));
		s->current_plot = -1;
	}
	s->current_series = -1;

	if (plot == NULL)
		return;

	s->current_plot =
		e_xml_get_integer_prop_by_name_with_default (plot, "index", -1);

	/* Init the expr entries */
	layout = e_xml_get_child_by_name (plot, "DataLayout");

	g_return_if_fail (layout != NULL);

	shared = unshared = 0;
	for (layout = layout->xmlChildrenNode; layout; layout = layout->next) {
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
		g_free (name);
		indx = e_xml_get_integer_prop_by_name_with_default (series, "index", -1);

		g_return_if_fail (indx >= 0);
		gtk_object_set_data (GTK_OBJECT (item), "index", 
			GINT_TO_POINTER (indx));
		if (s->current_series < 0)
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

	g_return_if_fail (s->current_series >= 0);
}

static void
graph_guru_apply_changes (GraphGuruState *state)
{
	CORBA_Environment  ev;

	CORBA_exception_init (&ev);
	switch (state->current_page) {
	case 0: if (state->type_selector != CORBA_OBJECT_NIL)
			CONFIG_GURU1 (applyChanges) (state->type_selector, &ev);
		break;
	case 1: if (state->data_guru != CORBA_OBJECT_NIL)
			CONFIG_GURU1 (applyChanges) (state->data_guru, &ev);
		break;
	case 2:
		break;

	default : /* it is ok to be invalid when intializing */
		break;
	}
	CORBA_exception_free (&ev);
}

static void
graph_guru_init_data_page (GraphGuruState *s)
{
	if (s->data_guru != CORBA_OBJECT_NIL)
		return;

	s->data_guru = gnm_graph_get_config_control (s->graph, "DataGuru"),

	g_return_if_fail (s->data_guru != CORBA_OBJECT_NIL);

	s->sample = bonobo_widget_new_control_from_objref (s->data_guru,
		CORBA_OBJECT_NIL);
	gtk_container_add (GTK_CONTAINER (s->sample_frame), s->sample);
	gtk_widget_show_all (s->sample_frame);
	graph_guru_get_spec (s);
}

static void
graph_guru_set_page (GraphGuruState *state, int page)
{
	char *name;
	gboolean prev_ok = TRUE, next_ok = TRUE;

	if (state->current_page == page)
		return;

	graph_guru_apply_changes (state);

	switch (page) {
	case 0: name = _("Step 1 of 3: Select graph type");
		prev_ok = FALSE;
		graph_guru_select_plot (state, NULL);
		break;
	case 1:
		if (state->initial_page == 0)
			name = _("Step 2 of 3: Select data ranges");
		else
			name = _("Graph Data");
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

	/* send any pending data edits to the guru */
	if (button != state->button_cancel)
		vector_state_apply_changes (state->current_vector);

	if (button == state->button_prev) {
		graph_guru_set_page (state, state->current_page - 1);
		return;
	}

	if (button == state->button_next) {
		graph_guru_set_page (state, state->current_page + 1);
		return;
	}

	if (button == state->button_finish) {
		/* apply the changes in the guru back to the main graph */
		graph_guru_apply_changes (state);
		if (state->initial_page == 0) {
			gtk_object_ref (GTK_OBJECT (state->graph));
			scg_mode_create_object (state->scg, SHEET_OBJECT (state->graph));
		}
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
cb_series_entry_changed (GtkWidget *ct, char *new_text, GraphGuruState *s)
{
	if (!gnm_combo_text_set_text (GNM_COMBO_TEXT (ct), new_text,
		GNM_COMBO_TEXT_NEXT)) {
		g_warning ("renaming a series is not yet supported");
	}

	return FALSE;
}
static gboolean
cb_series_selection_changed (GtkWidget *ct, GtkWidget *item, GraphGuruState *s)
{
	if (!s->updating) {
		gpointer *tmp = gtk_object_get_data (GTK_OBJECT (item), "index");
		graph_guru_select_series (s, graph_guru_get_series (s,
			GPOINTER_TO_INT (tmp)));
	}

	return FALSE;
}

static gboolean
cb_plot_entry_changed (GtkWidget *ct, char *new_text, GraphGuruState *s)
{
	if (!gnm_combo_text_set_text (GNM_COMBO_TEXT (ct), new_text,
		GNM_COMBO_TEXT_NEXT)) {
		g_warning ("renaming a plot is not yet supported");
	}
	return TRUE;
}

static gboolean
cb_plot_selection_changed (GtkWidget *ct, GtkWidget *item, GraphGuruState *s)
{
	if (!s->updating) {
		gpointer *tmp = gtk_object_get_data (GTK_OBJECT (item), "index");
		graph_guru_select_plot (s, graph_guru_get_plot (s,
			GPOINTER_TO_INT (tmp)));
	}
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
	vector_state_apply_changes (state->current_vector);

	if (focus != NULL) {
		VectorState *vs = gtk_object_get_data (GTK_OBJECT (focus), "VectorState");
		if (vs != NULL) {
			state->current_vector = vs;
			vs->changed = FALSE;
			wbcg_set_entry (state->wbcg, vs->entry);
			return;
		}
	}
	wbcg_set_entry (state->wbcg, NULL);
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
	state->sample   = NULL;
	state->shared   = g_ptr_array_new ();
	state->unshared	= g_ptr_array_new ();
	state->xml_doc	= NULL;
	state->plot_selector	= NULL;
	state->series_selector  = NULL;
	state->current_vector	= NULL;
	state->current_page	= -1;
	state->current_plot	= -1;
	state->current_series	= -1;
	state->type_selector	= CORBA_OBJECT_NIL;
	state->data_guru	= CORBA_OBJECT_NIL;

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
		state->type_selector = gnm_graph_get_config_control (
			state->graph, "TypeSelector");
		gtk_notebook_prepend_page (state->steps, 
			bonobo_widget_new_control_from_objref (
				state->type_selector, CORBA_OBJECT_NIL), NULL);
	}

	gtk_widget_show_all (state->dialog);

	if (page > 0) {
		gtk_widget_hide (state->button_prev);
		gtk_widget_hide (state->button_next);
	}
	graph_guru_set_page (state, page);
}
