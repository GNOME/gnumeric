/* vim: set sw=8: */

/*
 * dialog-graph-guru.c:  The Graph guru
 *
 * Copyright (C) 2000-2001 Jody Goldberg (jody@gnome.org)
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
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gnumeric-graph.h>
#include <gui-util.h>
#include <ranges.h>
#include <selection.h>
#include <expr.h>
#include <workbook-edit.h>
#include <sheet.h>
#include <sheet-view.h>
#include <sheet-control-gui.h>
#include <sheet-object.h>
#include <widgets/gnumeric-combo-text.h>

#include <gal/util/e-xml-utils.h>
#include <libxml/parser.h>
#include <libgnomecanvas/gnome-canvas.h>
#include <libgnomecanvas/gnome-canvas-pixbuf.h>
#include <libgnomecanvas/gnome-canvas-rect-ellipse.h>
#include <gdk/gdkkeysyms.h>

#define CONFIG_GURU		GNOME_Gnumeric_Graph_v2_ConfigGuru
#define CONFIG_GURU1(suffix)	GNOME_Gnumeric_Graph_v2_ConfigGuru_ ## suffix
#define DATA_GURU		GNOME_Gnumeric_Graph_v2_DataGuru
#define DATA_GURU1(suffix)	GNOME_Gnumeric_Graph_v2_DataGuru_ ## suffix

typedef struct _GraphGuruState		GraphGuruState;
typedef struct _GraphGuruTypeSelector	GraphGuruTypeSelector;

typedef struct
{
	GraphGuruState	*state;
	xmlChar	  	*dim_name;
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
	GtkWidget *series_delete;
	GtkWidget *series_details;
	GtkWidget *shared_series_details;
	GtkWidget *sample_frame;

	GtkWidget *selection_table;
	GtkWidget *shared_separator;

#if 0
	CONFIG_GURU format_guru;
	DATA_GURU   data_guru;
#endif
	GraphGuruTypeSelector *type_selector;
	GtkWidget *sample;
	GPtrArray *shared, *unshared;

	/* internal state */
	VectorState *current_vector;
	int current_plot, current_series;
	int current_page, initial_page;
	gboolean valid;
	gboolean updating;
	xmlDoc *xml_doc;

	/* external state */
	WorkbookControlGUI *wbcg;
	Workbook	   *wb;
	SheetControlGUI	   *scg;
	SheetView	   *sv;
	Sheet		   *sheet;
};

struct _GraphGuruTypeSelector {
	GtkWidget	*notebook;
	GtkWidget	*canvas;
	GtkWidget	*sample_button;
	GtkLabel	*label;
	GtkTreeView	*list_view;
	GtkListStore	*model;
	GnomeCanvasItem *selector;

	GnomeCanvasItem *sample_plot;
	GnmGraphPlot	*plot;	/* potentially NULL */
	GnmGraph	*graph;

	GnomeCanvasGroup *plot_group;

	xmlNode const *plots, *current_major, *current_minor;
	GnomeCanvasGroup const *current_major_item;
	GnomeCanvasItem const  *current_minor_item;
};

enum {
	MAJOR_TYPE_IMAGE,
	MAJOR_TYPE_NAME,
	MAJOR_TYPE_CANVAS_GROUP,
	NUM_COLUMNS
};

#define MINOR_PIXMAP_WIDTH	64
#define MINOR_PIXMAP_HEIGHT	60
#define BORDER	5

#define MINOR_KEY		"minor_chart_type"
#define MAJOR_KEY		"major_chart_type"
#define FIRST_MINOR_TYPE	"first_minor_type"

static GdkPixbuf *
get_pixbuf (xmlNode const *node)
{
	static GHashTable *cache = NULL;
	xmlChar *sample_image_file;
	xmlNode *sample_image_file_node;
	GdkPixbuf *pixbuf;

	g_return_val_if_fail (node != NULL, NULL);

	if (cache != NULL) {
		pixbuf = g_hash_table_lookup (cache, node);
		if (pixbuf != NULL)
			return pixbuf;
	} else
		cache = g_hash_table_new (g_direct_hash, g_direct_equal);

	sample_image_file_node = e_xml_get_child_by_name (node, "sample_image_file");

	g_return_val_if_fail (sample_image_file_node != NULL, NULL);

	sample_image_file = xmlNodeGetContent (sample_image_file_node);
	g_return_val_if_fail (sample_image_file != NULL, NULL);

	pixbuf = gnumeric_load_pixbuf (sample_image_file);
	xmlFree (sample_image_file);
	g_hash_table_insert (cache, (gpointer)node, pixbuf);

	return pixbuf;
}

static void
get_pos (int col, int row, double *x, double *y)
{
	*x = (col-1) * (MINOR_PIXMAP_WIDTH + BORDER) + BORDER;
	*y = (row-1) * (MINOR_PIXMAP_HEIGHT + BORDER) + BORDER;
}

/*
 * graph_typeselect_minor :
 *
 * @typesel :
 * @item : A CanvasItem in the selector.
 *
 * Moves the typesel::selector overlay above the @item.
 * Assumes that the item is visible
 */
static void
graph_typeselect_minor (GraphGuruTypeSelector *typesel,
			GnomeCanvasItem *item)
{
	xmlNode *minor, *tmp;
	double x1, y1, x2, y2;
	char *description;

	if (typesel->current_minor_item == item)
		return;

	minor = g_object_get_data (G_OBJECT (item), MINOR_KEY);

	g_return_if_fail(minor != NULL);

	/* clear the current plot */
	if (typesel->sample_plot != NULL) {
		gtk_object_destroy (GTK_OBJECT (typesel->sample_plot));
		typesel->sample_plot = NULL;
	}

	tmp = e_xml_get_child_by_name_by_lang_list (
	       minor, "description", NULL);
	description = (tmp != NULL) ? xmlNodeGetContent (tmp) : NULL;
	typesel->current_minor = minor;
	typesel->current_minor_item = item;
	gnome_canvas_item_get_bounds (item, &x1, &y1, &x2, &y2);
	gnome_canvas_item_set (GNOME_CANVAS_ITEM (typesel->selector),
		"x1", x1-1., "y1", y1-1.,
		"x2", x2+1., "y2", y2+1.,
		NULL);
	gtk_label_set_text (typesel->label, description);
	gtk_widget_set_sensitive (typesel->sample_button, TRUE);

	if (description != NULL)
		xmlFree (description);

	gnm_graph_plot_set_type (typesel->plot, minor);
}

static gboolean
graph_typeselect_minor_x_y (GraphGuruTypeSelector *typesel,
			    double x, double y)
{
	GnomeCanvasItem *item = gnome_canvas_get_item_at (
		GNOME_CANVAS (typesel->canvas), x, y);

	if (item != NULL && item != typesel->selector) {
		graph_typeselect_minor (typesel, item);
		return TRUE;
	}

	return FALSE;
}

static void
minor_chart_type_get_pos (xmlNode const *node, int *col, int *row)
{
	*col = *row = -1;
	node = e_xml_get_child_by_name (node, "position");

	g_return_if_fail (node != NULL);

	*col = e_xml_get_integer_prop_by_name (node, "col");
	*row = e_xml_get_integer_prop_by_name (node, "row");
}

static gboolean
cb_key_press_event (__attribute__((unused)) GtkWidget *wrapper,
		    GdkEventKey *event,
		    GraphGuruTypeSelector *typesel)
{
	GtkCornerType corner;
	int row, col;
	double x, y;
	xmlNode const *minor = g_object_get_data (
		G_OBJECT (typesel->current_minor_item), MINOR_KEY);

	g_return_val_if_fail (minor != NULL, FALSE);

	minor_chart_type_get_pos (minor, &col, &row);

	switch (event->keyval){
	case GDK_KP_Left:	case GDK_Left:
		corner = GTK_CORNER_BOTTOM_RIGHT;
		--col;
		break;

	case GDK_KP_Up:	case GDK_Up:
		corner = GTK_CORNER_BOTTOM_RIGHT;
		--row;
		break;

	case GDK_KP_Right:	case GDK_Right:
		corner = GTK_CORNER_TOP_LEFT;
		++col;
		break;

	case GDK_KP_Down:	case GDK_Down:
		corner = GTK_CORNER_TOP_LEFT;
		++row;
		break;

	default:
		return FALSE;
	}

	get_pos (col, row, &x, &y);
	graph_typeselect_minor_x_y (typesel, x, y);

	return TRUE;
}

static gint
cb_button_press_event (GtkWidget *widget, GdkEventButton *event,
		       GraphGuruTypeSelector *typesel)
{
	if (event->button == 1) {
		GnomeCanvas *c = GNOME_CANVAS (widget);
		double x, y;

		gnome_canvas_window_to_world (c, event->x, event->y, &x, &y);

		graph_typeselect_minor_x_y (typesel, x, y);
	}

	return FALSE;
}

static void
cb_selection_changed (__attribute__((unused)) GtkTreeSelection *ignored,
		      GraphGuruTypeSelector *typesel)
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection (typesel->list_view);
	GtkTreeIter  iter;
	GnomeCanvasItem *item;
	GnomeCanvasGroup *group;

	if (typesel->current_major_item != NULL)
		gnome_canvas_item_hide (GNOME_CANVAS_ITEM (typesel->current_major_item));
	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;
	gtk_tree_model_get (GTK_TREE_MODEL (typesel->model), &iter,
		MAJOR_TYPE_CANVAS_GROUP, &group,
		-1);

	gnome_canvas_item_show (GNOME_CANVAS_ITEM (group));
	typesel->current_major_item = group;

	gnome_canvas_item_hide (GNOME_CANVAS_ITEM (typesel->selector));
	item = g_object_get_data (G_OBJECT (group), FIRST_MINOR_TYPE);
	if (item != NULL)
		graph_typeselect_minor (typesel, item);
	gnome_canvas_item_show (GNOME_CANVAS_ITEM (typesel->selector));
}

static void
cb_sample_pressed (__attribute__((unused)) GtkWidget *button,
		   GraphGuruTypeSelector *typesel)
{
	if (typesel->current_major_item == NULL)
		return;

	if (typesel->sample_plot == NULL) {
#if 0
		GuppiRootGroupView *plot = gup_gnm_graph_get_view (state->graph);

		g_return_if_fail (plot != NULL);

		typesel->sample_plot = guppi_element_view_make_canvas_item (
			GUPPI_ELEMENT_VIEW (plot),
			GNOME_CANVAS (typesel->canvas),
			typesel->plot_group);
		guppi_root_group_item_set_resize_semantics (
			GUPPI_ROOT_GROUP_ITEM (typesel->sample_plot),
			ROOT_GROUP_RESIZE_FILL_SPACE);
#endif
	}

	gnome_canvas_item_hide (GNOME_CANVAS_ITEM (typesel->current_major_item));
	gnome_canvas_item_hide (GNOME_CANVAS_ITEM (typesel->selector));
	gnome_canvas_item_show (GNOME_CANVAS_ITEM (typesel->plot_group));
	/* guppi_root_group_item_best_fit (GUPPI_ROOT_GROUP_ITEM(typesel->sample_plot)); */
}

static void
cb_sample_released (__attribute__((unused)) GtkWidget *button,
		    GraphGuruTypeSelector *typesel)
{
	if (typesel->current_major_item == NULL)
		return;

	gnome_canvas_item_hide (GNOME_CANVAS_ITEM (typesel->plot_group));
	gnome_canvas_item_show (GNOME_CANVAS_ITEM (typesel->current_major_item));
	gnome_canvas_item_show (GNOME_CANVAS_ITEM (typesel->selector));
	gnome_canvas_set_scroll_region (GNOME_CANVAS (typesel->canvas), 0, 0,
		MINOR_PIXMAP_WIDTH*3 + BORDER*5,
		MINOR_PIXMAP_HEIGHT*3 + BORDER*5);
}

typedef struct {
	GraphGuruTypeSelector	*typesel;
	GnomeCanvasGroup	*group;
	GnomeCanvasItem		*current_item;
	xmlNode 		*current_minor;
	int col, row;
} minor_list_closure;

static void
minor_list_init (xmlNode *minor, minor_list_closure *closure)
{
	double x1, y1, w, h;
	GnomeCanvasItem *item;
	int col, row;
	GdkPixbuf *image = get_pixbuf (minor);

	g_return_if_fail (image != NULL);

	minor_chart_type_get_pos (minor, &col, &row);

	get_pos (col, row, &x1, &y1);
	w = gdk_pixbuf_get_width (image);
	if (w > MINOR_PIXMAP_WIDTH)
		w = MINOR_PIXMAP_WIDTH;
	h = gdk_pixbuf_get_height (image);
	if (h > MINOR_PIXMAP_HEIGHT)
		h = MINOR_PIXMAP_HEIGHT;

	item = gnome_canvas_item_new (closure->group,
		gnome_canvas_pixbuf_get_type (),
		"x", x1, "y", y1,
		"width", w, "height", h,
		"pixbuf", image,
		NULL);
	g_object_set_data (G_OBJECT (item), MINOR_KEY, (gpointer)minor);

	if (closure->current_minor == NULL ||
	    closure->row > row ||
	    (closure->row == row && closure->col > col)) {
		closure->current_minor = minor;
		closure->current_item = item;
		closure->col = col;
		closure->row = row;
	}
}

static void
major_list_init (GraphGuruTypeSelector *typesel, xmlNode *major)
{
	xmlChar			*name;
	xmlNode			*node;
	GnomeCanvasGroup	*group;
	GtkTreeIter iter;
	minor_list_closure	 closure;

	/* be really anal when parsing a user editable file */
	node = e_xml_get_child_by_name_by_lang_list (major, "name", NULL);
	g_return_if_fail (node != NULL);

	name = xmlNodeGetContent (node);
	g_return_if_fail (name != NULL);

	/* Define a canvas group for all the minor types */
	group = GNOME_CANVAS_GROUP (gnome_canvas_item_new (
		gnome_canvas_root (GNOME_CANVAS (typesel->canvas)),
		gnome_canvas_group_get_type (),
		"x", 0.0,
		"y", 0.0,
		NULL));
	gnome_canvas_item_hide (GNOME_CANVAS_ITEM (group));
	g_object_set_data (G_OBJECT (group), MAJOR_KEY, (gpointer)major);

	gtk_list_store_append (typesel->model, &iter);
	gtk_list_store_set (typesel->model, &iter,
		MAJOR_TYPE_IMAGE,	 get_pixbuf (major),
		MAJOR_TYPE_NAME,	 name,
		MAJOR_TYPE_CANVAS_GROUP, group,
		-1);
	xmlFree (name);

	closure.typesel = typesel;
	closure.group = group;
	closure.current_minor = NULL;

	/* Init the list and the canvas group for each major type */
	for (node = major->xmlChildrenNode; node != NULL; node = node->next)
		if (!strcmp (node->name, "Minor"))
			minor_list_init (node, &closure);

	g_object_set_data (G_OBJECT (group), FIRST_MINOR_TYPE,
		closure.current_item);
}

static void
cb_canvas_realized (__attribute__((unused)) GtkWidget *widget,
		    __attribute__((unused)) gpointer data)
{
	/*gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window, NULL, FALSE); */
}

static void graph_guru_select_plot (GraphGuruState *s, xmlNode *plot, int seriesID);

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
	xmlNode *plot = e_xml_get_child_by_name (xml_doc->xmlRootNode, (xmlChar *)"Plots");

	g_return_val_if_fail (plot != NULL, NULL);

	for (plot = plot->xmlChildrenNode; plot; plot = plot->next) {
		if (strcmp (plot->name, "Plot"))
			continue;
		if (indx == e_xml_get_integer_prop_by_name_with_default (plot, (xmlChar *)"index", -1))
			return plot;
	}
	return NULL;
}

static xmlNode *
graph_guru_get_series (GraphGuruState *s, int indx)
{
	xmlNode *plot = graph_guru_get_plot (s, s->current_plot);
	xmlNode *series = e_xml_get_child_by_name (plot, (xmlChar *)"Data");

	g_return_val_if_fail (series != NULL, NULL);

	for (series = series->xmlChildrenNode; series; series = series->next) {
		if (strcmp (series->name, "Series"))
			continue;
		if (indx == e_xml_get_integer_prop_by_name_with_default (series, (xmlChar *)"index", -1))
			return series;
	}
	return NULL;
}

static char *
graph_guru_plot_name (__attribute__((unused)) GraphGuruState *s,
		      xmlNode *plot)
{
	char *name;
	xmlChar *t;
	int i = e_xml_get_integer_prop_by_name_with_default (plot, (xmlChar *)"index", -1);
	xmlNode *type = e_xml_get_child_by_name (plot, (xmlChar *)"Type");

	g_return_val_if_fail (i >= 0, g_strdup ("ERROR Missing Index"));

	t = xml_node_get_cstr (type, "name");
	if (t == NULL)
		t = xmlMemStrdup ("Plot");

	name = g_strdup_printf ("%s%d", t, i+1);
	xmlFree (t);
	return name;
}

static void
graph_guru_get_spec (GraphGuruState *s)
{
	int indx;
	xmlNode *plot;
	char *name;
	GtkWidget *item;
	xmlDoc *xml_doc = NULL;
#ifdef GNOME2_CONVERSION_COMPLETE
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
				(char *)(spec->_buffer), spec->_length, NULL);
			xmlParseChunk (pctxt, "", 0, TRUE);
			xml_doc = pctxt->myDoc;
			xmlFreeParserCtxt (pctxt);
		}
		CORBA_free (spec);
	} else {
		g_warning ("'%s' : getting the spec from data_guru %p",
			   gnm_graph_exception (&ev), s->data_guru);
	}
#endif

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
	plot = e_xml_get_child_by_name (xml_doc->xmlRootNode, (xmlChar *)"Plots");

	g_return_if_fail (plot != NULL);

	for (plot = plot->xmlChildrenNode; plot; plot = plot->next) {
		if (strcmp (plot->name, "Plot"))
			continue;
		name = graph_guru_plot_name (s, plot);
		item = gnm_combo_text_add_item (
			GNM_COMBO_TEXT (s->plot_selector), name);
		g_free (name);

		indx = e_xml_get_integer_prop_by_name_with_default (plot, (xmlChar *)"index", -1);
		g_return_if_fail (indx >= 0);
		g_object_set_data (G_OBJECT (item), "index",
			GINT_TO_POINTER (indx));

		if (s->current_plot < 0 || s->current_plot == indx)
			graph_guru_select_plot (s, plot, s->current_series);
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
vector_state_series_set_dimension (VectorState *vs, GnmExpr const *expr)
{
	int vector_id = -1;

	if (vs->state == NULL)
		return;

	if (expr != NULL)
		vector_id = gnm_graph_add_vector (vs->state->graph,
			expr, GNM_VECTOR_AUTO, vs->state->sheet);

#if 0
	CORBA_Environment  ev;

	if (vs->state->data_guru == CORBA_OBJECT_NIL)
		return;

	CORBA_exception_init (&ev);

	/* We need to create the series */
	if (vs->series_index < 0) {
		vs->series_index = DATA_GURU1 (seriesAdd) (vs->state->data_guru,
			vs->state->current_plot, &ev);
		if (ev._major != CORBA_NO_EXCEPTION) {
			g_warning ("'%s' : adding a series to graph data_guru %p",
				   gnm_graph_exception (&ev), vs->state->data_guru);
			CORBA_exception_free (&ev);
			return;
		}
	}

	/* Future simplification.  If we are changing an unshared dimension we
	 * can do the substitution in place.  and just tweak the expression.
	 */
	DATA_GURU1 (seriesSetDimension) (vs->state->data_guru,
		vs->series_index, (CORBA_char *)vs->dim_name, vector_id, &ev);
	if (ev._major == CORBA_NO_EXCEPTION)
		graph_guru_get_spec (vs->state);
	else {
		g_warning ("'%s' : changing a dimension from graph data_guru %p",
			   gnm_graph_exception (&ev), vs->state->data_guru);
	}
	CORBA_exception_free (&ev);
#endif
}

static void
vector_state_fill (VectorState *vs, xmlNode *series)
{
	xmlNode *dim;
	int id;

	g_return_if_fail (!vs->state->updating);

	/* clear beforehand to make error handling simpler */
	vs->state->updating = TRUE;
	gnm_expr_entry_load_from_text (vs->entry, "");
	gnm_expr_entry_set_flags (vs->entry,
		GNUM_EE_ABS_COL|GNUM_EE_ABS_ROW, GNUM_EE_MASK);
	vs->state->updating = FALSE;

	if (series == NULL)
		return;

	vs->series_index =
		e_xml_get_integer_prop_by_name_with_default (series, (xmlChar *)"index", -1);
	vs->vector = NULL;
	dim = gnm_graph_series_get_dimension (series, vs->dim_name);
	if (dim != NULL) {
		id = e_xml_get_integer_prop_by_name_with_default (dim, (xmlChar *)"ID", -1);
		if (id >= 0) {
			vs->vector = gnm_graph_get_vector (vs->state->graph, id);
			gnm_expr_entry_load_from_dep (vs->entry,
				gnm_graph_vector_get_dependent (vs->vector));
			gnm_expr_entry_set_flags (vs->entry,
				GNUM_EE_ABS_COL|GNUM_EE_ABS_ROW, GNUM_EE_MASK);
		}
	}
}

static void
vector_state_apply_changes (VectorState *vs)
{
	GnmExpr const *expr = NULL;
	gboolean changed;

	if (vs == NULL || !vs->changed)
		return;

	/* If we are setting something */
	if (!gnm_expr_entry_is_blank (vs->entry)) {
		ParsePos pos;
		parse_pos_init (&pos, NULL, vs->state->sheet, 0, 0);
		expr = gnm_expr_entry_parse (vs->entry, &pos, NULL, TRUE);

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

	if (vs->dim_name != NULL)
		xmlFree (vs->dim_name);
	vs->dim_name = xmlGetProp (descriptor, (xmlChar *)"dim_name");

	required = e_xml_get_bool_prop_by_name_with_default (descriptor,
		(xmlChar *)"required", FALSE);

	tmp = xmlNodeGetContent (descriptor);
	name = required ? g_strdup ((char *)tmp) : g_strdup_printf ("(%s)", tmp);
	gtk_label_set_text (GTK_LABEL (vs->name_label), name);
	xmlFree (tmp);
	g_free (name);

	gtk_widget_show (vs->name_label);
	gtk_widget_show (GTK_WIDGET (vs->entry));
}

static void
cb_entry_changed (__attribute__((unused)) GtkEditable *editable,
		  VectorState *vs)
{
	if (!vs->state->updating)
		vs->changed = TRUE;
}

static void
cb_entry_rangesel_drag_finished (__attribute__((unused)) GnumericExprEntry *gee,
				 VectorState *vs)
{
	vector_state_apply_changes (vs);
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
	vs->dim_name  = NULL;
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

	vs->entry = gnumeric_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_scg (vs->entry, state->scg);
	gtk_table_attach (table, GTK_WIDGET (vs->entry),
		1, 2, dim_indx, dim_indx+1, GTK_EXPAND|GTK_FILL, 0, 5, 3);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
		GTK_WIDGET (vs->entry));

	g_object_set_data (G_OBJECT (vs->entry), "VectorState", vs);

	/* flag when things change so we'll know if we need to update the vector */
	g_signal_connect (G_OBJECT (vs->entry),
		"changed",
		G_CALLBACK (cb_entry_changed), vs);
	g_signal_connect (G_OBJECT (vs->entry),
		"rangesel_drag_finished",
		G_CALLBACK (cb_entry_rangesel_drag_finished), vs);

	return vs;
}

static void
vector_state_destroy (VectorState *vs, gboolean destroywidgets)
{
	vs->vector = NULL;
	if (vs->dim_name) {
		xmlFree (vs->dim_name);
		vs->dim_name = NULL;
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

	graph_guru_select_plot (state, NULL, -1);

	if (state->plot_selector) {
		g_signal_handlers_disconnect_matched (
			G_OBJECT (state->plot_selector),
			G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, state);
		state->plot_selector = NULL;
	}
	if (state->series_selector != NULL) {
		g_signal_handlers_disconnect_matched (
			G_OBJECT (state->series_selector),
			G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, state);
			state->series_selector = NULL;
	}

	wbcg_edit_detach_guru (state->wbcg);

	if (state->graph != NULL) {
		g_object_unref (G_OBJECT (state->graph));
		state->graph = NULL;
	}

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
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
cb_graph_guru_destroy (__attribute__((unused)) GtkObject *w,
		       GraphGuruState *state)
{
	graph_guru_state_destroy (state);
	return FALSE;
}

static  gint
cb_graph_guru_key_press (__attribute__((unused)) GtkWidget *widget,
			 GdkEventKey *event,
			 GraphGuruState *state)
{
	if (event->keyval == GDK_Escape) {
		wbcg_edit_finish (state->wbcg, FALSE);
		return TRUE;
	} else
		return FALSE;
}

static char *
graph_guru_series_name (__attribute__((unused)) GraphGuruState *s,
			xmlNode *series)
{
	int i;
	xmlChar *name = xmlGetProp (series, (xmlChar *)"name");

	if (name != NULL) {
		char *res = g_strdup ((char *)name);
		xmlFree (name);
		return res;
	}
	i = e_xml_get_integer_prop_by_name_with_default (series, (xmlChar *)"index", -1);

	g_return_val_if_fail (i >= 0, g_strdup ("ERROR Missing Index"));

	return g_strdup_printf ("Series%d", i+1);
}

static void
graph_guru_select_series (GraphGuruState *s, xmlNode *series)
{
	int i;
	char *name;

	g_return_if_fail (series != NULL);

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
		e_xml_get_integer_prop_by_name_with_default (series, (xmlChar *)"index", -1);
}

static void
graph_guru_select_plot (GraphGuruState *s, xmlNode *plot, int seriesID)
{
	xmlNode *layout, *series;
	char *name;
	GtkWidget *item;
	int shared, unshared;
	int indx, count = 0;

	if (s->updating)
		return;

	/* clear out the old */
	if (s->current_plot >= 0) {
		gnm_combo_text_clear (GNM_COMBO_TEXT (s->series_selector));
		s->current_plot = -1;
	}

	if (plot == NULL) {
		s->current_series = -1;
		return;
	}
	s->current_series = seriesID;

	s->current_plot =
		e_xml_get_integer_prop_by_name_with_default (plot, (xmlChar *)"index", -1);

	/* Init the expr entries */
	layout = e_xml_get_child_by_name (plot, (xmlChar *)"DataLayout");

	g_return_if_fail (layout != NULL);

	shared = unshared = 0;
	for (layout = layout->xmlChildrenNode; layout; layout = layout->next) {
		gboolean is_shared;
		GPtrArray   *container;
		VectorState *vs;

		if (strcmp (layout->name, "Dimension"))
			continue;

		is_shared = e_xml_get_bool_prop_by_name_with_default (layout,
			(xmlChar *)"shared", FALSE);
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
	series = e_xml_get_child_by_name (plot, (xmlChar *)"Data");

	g_return_if_fail (series != NULL);

	for (series = series->xmlChildrenNode; series; series = series->next) {
		if (strcmp (series->name, "Series"))
			continue;
		name = graph_guru_series_name (s, series);
		item = gnm_combo_text_add_item (
			GNM_COMBO_TEXT (s->series_selector), name);
		g_free (name);
		indx = e_xml_get_integer_prop_by_name_with_default (series, (xmlChar *)"index", -1);

		g_return_if_fail (indx >= 0);
		g_object_set_data (G_OBJECT (item), "index",
			GINT_TO_POINTER (indx));
		if (s->current_series < 0)
			graph_guru_select_series (s, series);
		count++;
	}

	s->updating = TRUE;
	name = graph_guru_plot_name (s, plot);
	gnm_combo_text_set_text (GNM_COMBO_TEXT (s->plot_selector),
		name, GNM_COMBO_TEXT_CURRENT);
	g_free (name);
	s->updating = FALSE;

	gtk_widget_show_all (s->series_details);
	gtk_widget_show_all (s->shared_series_details);

	/* It is ok for the current_series to do -1,
	 * that indicates that there are no series yet
	 */
	/* g_return_if_fail (s->current_series >= 0); */
	gtk_widget_set_sensitive (s->series_add, TRUE);
	gtk_widget_set_sensitive (s->series_delete, count > 1);
}

static void
graph_guru_apply_changes (__attribute__((unused)) GraphGuruState *state)
{
#if 0
	CORBA_Environment  ev;

	CORBA_exception_init (&ev);
	switch (state->current_page) {
	case 0: if (state->type_selector != CORBA_OBJECT_NIL)
			CONFIG_GURU1 (applyChanges) (state->type_selector, &ev);
		break;
	case 1: if (state->data_guru != CORBA_OBJECT_NIL)
			CONFIG_GURU1 (applyChanges) (state->data_guru, &ev);
		break;
	case 2: if (state->format_guru != CORBA_OBJECT_NIL)
			CONFIG_GURU1 (applyChanges) (state->format_guru, &ev);
		break;

	default : /* it is ok to be invalid when intializing */
		break;
	}
	CORBA_exception_free (&ev);
#endif
}

static void
graph_guru_init_data_page (__attribute__((unused)) GraphGuruState *s)
{
#if 0
	if (s->data_guru != CORBA_OBJECT_NIL)
		return;

	s->data_guru = gnm_graph_get_config_control (
		s->graph, "DataGuru");

	g_return_if_fail (s->data_guru != CORBA_OBJECT_NIL);

	s->sample = bonobo_widget_new_control_from_objref (s->data_guru,
		CORBA_OBJECT_NIL);
	gtk_container_add (GTK_CONTAINER (s->sample_frame), s->sample);
	gtk_widget_show_all (s->sample_frame);
	graph_guru_get_spec (s);
#endif
}

static void
graph_guru_init_format_page (__attribute__((unused)) GraphGuruState *s)
{
#if 0
	GtkWidget *w;

	if (s->format_guru != CORBA_OBJECT_NIL)
		return;

	s->format_guru = gnm_graph_get_config_control (
		s->graph, "FormatGuru");

	g_return_if_fail (s->format_guru != CORBA_OBJECT_NIL);

	w = bonobo_widget_new_control_from_objref (
		s->format_guru, CORBA_OBJECT_NIL);
	gtk_widget_show_all (w);
	if (s->initial_page == 0)
		gtk_notebook_append_page (s->steps, w, NULL);
	else
		gtk_notebook_prepend_page (s->steps, w, NULL);
#endif
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
		graph_guru_select_plot (state, NULL, -1);
		break;
	case 1:
		if (state->initial_page == 0)
			name = _("Step 2 of 3: Select data ranges");
		else
			name = _("Graph Data");
		graph_guru_init_data_page (state);
		break;
	case 2:
		if (state->initial_page == 0)
			name = _("Step 3 of 3: Customize graph");
		else
			name = _("Format Graph");
		graph_guru_init_format_page (state);
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
		if (state->initial_page == 0)
			scg_mode_create_object (state->scg, SHEET_OBJECT (state->graph));
	}

	gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static GtkWidget *
graph_guru_init_button  (GraphGuruState *state, char const *widget_name)
{
	GtkWidget *tmp = glade_xml_get_widget (state->gui, widget_name);
	g_signal_connect (G_OBJECT (tmp),
		"clicked",
		G_CALLBACK (cb_graph_guru_clicked), state);

	return tmp;
}


static gboolean
cb_series_entry_changed (GtkWidget *ct, char *new_text, GraphGuruState *s)
{
	/* If the entry is not found then rename the current series */
	if (!gnm_combo_text_set_text (GNM_COMBO_TEXT (ct), new_text,
		GNM_COMBO_TEXT_NEXT)) {
		int i = s->unshared->len; /*label is always unshared */
		while (i-- > 0) {
			VectorState *vs = g_ptr_array_index (s->unshared, i);
			if (!strcmp (vs->dim_name, "labels")) {
				gtk_entry_set_text (GTK_ENTRY (vs->entry), new_text);
				vector_state_apply_changes (vs);
				break;
			}
		}
	}

	return FALSE;
}

static gboolean
cb_series_selection_changed (__attribute__((unused)) GtkWidget *ct,
			     GtkWidget *item, GraphGuruState *s)
{
	if (!s->updating) {
		gpointer *tmp = g_object_get_data (G_OBJECT (item), "index");
		graph_guru_select_series (s, graph_guru_get_series (s,
			GPOINTER_TO_INT (tmp)));
	}

	return FALSE;
}

static gboolean
cb_plot_entry_changed (GtkWidget *ct, char *new_text,
		       __attribute__((unused)) GraphGuruState *s)
{
	if (!gnm_combo_text_set_text (GNM_COMBO_TEXT (ct), new_text,
		GNM_COMBO_TEXT_NEXT)) {
		g_warning ("renaming a plot is not yet supported");
	}
	return TRUE;
}

static gboolean
cb_plot_selection_changed (__attribute__((unused)) GtkWidget *ct,
			   GtkWidget *item, GraphGuruState *s)
{
	if (!s->updating) {
		gpointer *tmp = g_object_get_data (G_OBJECT (item), "index");
		graph_guru_select_plot (s, graph_guru_get_plot (s,
			GPOINTER_TO_INT (tmp)), -1);
	}
	return FALSE;
}

static GtkWidget *
graph_guru_selector_init (GraphGuruState *s, char const *name, int i,
			  GCallback	entry_changed,
			  GCallback	selection_changed)
{
	GtkWidget    *w  = gnm_combo_text_new (NULL);
	GnmComboText *ct = GNM_COMBO_TEXT (w);
	gtk_table_attach_defaults (GTK_TABLE (s->selection_table),
		w, 1, 2, i, i+1);
	gtk_combo_box_set_title (GTK_COMBO_BOX (ct), _(name));
	gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (ct), GTK_RELIEF_NONE);
	g_signal_connect (G_OBJECT (ct),
		"entry_changed",
		G_CALLBACK (entry_changed), s);
	g_signal_connect (G_OBJECT (ct),
		"selection_changed",
		G_CALLBACK (selection_changed), s);

	return w;
}

static void
cb_graph_guru_focus (__attribute__((unused)) GtkWindow *window,
		     GtkWidget *focus, GraphGuruState *state)
{
	vector_state_apply_changes (state->current_vector);

	if (focus != NULL) {
		VectorState *vs = g_object_get_data (G_OBJECT (focus), "VectorState");
		if (vs != NULL) {
			state->current_vector = vs;
			vs->changed = FALSE;
			wbcg_set_entry (state->wbcg, vs->entry);
			return;
		}
	}
	wbcg_set_entry (state->wbcg, NULL);
}

static void
cb_graph_guru_series_add (__attribute__((unused)) GtkWidget *button,
			  __attribute__((unused)) GraphGuruState *s)
{
#if 0
	int new_index;
	CORBA_Environment  ev;

	CORBA_exception_init (&ev);

	new_index = DATA_GURU1 (seriesAdd) (s->data_guru, s->current_plot, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("'%s' : adding a series to graph data_guru %p",
			   gnm_graph_exception (&ev), s->data_guru);
		CORBA_exception_free (&ev);
		return;
	}
	CORBA_exception_free (&ev);
	graph_guru_get_spec (s);
	graph_guru_select_series (s, graph_guru_get_series (s, new_index));
#endif
}

static void
cb_graph_guru_series_delete (__attribute__((unused)) GtkWidget *button,
			     GraphGuruState *s)
{
	if (s->current_series >= 0) {
#ifdef GNOME2_CONVERSION_COMPLETE
		CORBA_Environment  ev;
		CORBA_exception_init (&ev);
		DATA_GURU1 (seriesDelete) (s->data_guru, s->current_series, &ev);

		if (ev._major != CORBA_NO_EXCEPTION) {
			g_warning ("'%s' : deleting a series from graph data_guru %p",
				   gnm_graph_exception (&ev), s->data_guru);
		} else {
			s->current_series = -1;
			graph_guru_get_spec (s);
		}
		CORBA_exception_free (&ev);
#else
		s->current_series = -1;
		graph_guru_get_spec (s);
#endif
	}
}

static GtkWidget *
graph_guru_type_selector_new (void)
{
	static xmlDoc *doc;
	xmlNode *major;
	GtkTreeSelection  *selection;
	GraphGuruTypeSelector *typesel;
	GtkWidget *tmp, *vbox, *hbox;
	guint32 select_color;

	typesel = g_new0 (GraphGuruTypeSelector, 1);
	typesel->current_major_item = NULL;
	typesel->current_minor_item = NULL;
	typesel->current_minor = NULL;
	typesel->sample_plot = NULL;

	hbox = gtk_hbox_new (FALSE, 5);

	/* List of major types */
	typesel->model = gtk_list_store_new (NUM_COLUMNS,
					     GDK_TYPE_PIXBUF,
					     G_TYPE_STRING,
					     G_TYPE_POINTER);
	typesel->list_view = GTK_TREE_VIEW (gtk_tree_view_new_with_model (
		GTK_TREE_MODEL (typesel->model)));

	gtk_tree_view_append_column (typesel->list_view,
		gtk_tree_view_column_new_with_attributes ("",
			gtk_cell_renderer_pixbuf_new (),
			"pixbuf", MAJOR_TYPE_IMAGE, 
			NULL));
	gtk_tree_view_append_column (typesel->list_view,
		gtk_tree_view_column_new_with_attributes (_("Plot Type"),
			gtk_cell_renderer_text_new (),
			"text", MAJOR_TYPE_NAME, 
			NULL));
	gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (typesel->list_view),
			    TRUE, TRUE, 0);

	selection = gtk_tree_view_get_selection (typesel->list_view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	g_signal_connect (selection,
		"changed",
		G_CALLBACK (cb_selection_changed), typesel);

	/* Setup an aa canvas to display the sample image & the sample plot. */
	typesel->canvas = gnome_canvas_new_aa ();
	g_signal_connect (G_OBJECT (typesel->canvas),
		"realize",
		G_CALLBACK (cb_canvas_realized), typesel);

	typesel->plot_group = GNOME_CANVAS_GROUP (gnome_canvas_item_new (
		gnome_canvas_root (GNOME_CANVAS (typesel->canvas)),
		gnome_canvas_group_get_type (),
		"x", 0.0,
		"y", 0.0,
		NULL));

	/*guppi_plot_canvas_set_insensitive (GUPPI_PLOT_CANVAS (typesel->canvas)); */
	gtk_widget_set_usize (typesel->canvas,
		MINOR_PIXMAP_WIDTH*3 + BORDER*5,
		MINOR_PIXMAP_HEIGHT*3 + BORDER*5);
	gnome_canvas_set_scroll_region (GNOME_CANVAS (typesel->canvas), 0, 0,
		MINOR_PIXMAP_WIDTH*3 + BORDER*5,
		MINOR_PIXMAP_HEIGHT*3 + BORDER*5);

	g_signal_connect_after (G_OBJECT (typesel->canvas),
		"key_press_event",
		G_CALLBACK (cb_key_press_event), typesel);
	g_signal_connect (GTK_OBJECT (typesel->canvas),
		"button_press_event",
		G_CALLBACK (cb_button_press_event), typesel);

	tmp = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (tmp), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (tmp), typesel->canvas);
	gtk_box_pack_start (GTK_BOX (hbox), tmp, FALSE, TRUE, 0);

	if (doc == NULL) {
		char *plots_path = g_build_filename (
			gnumeric_sys_data_dir (NULL), "plot-types.xml", NULL);
		doc = xmlParseFile (plots_path);
		g_free (plots_path);
	}

	g_return_val_if_fail (doc != NULL, NULL);

	/* Init the list and the canvas group for each major type */
	major = e_xml_get_child_by_name (doc->xmlRootNode, "MajorMinor");
	for (major = major->xmlChildrenNode; major != NULL; major = major->next)
		if (!strcmp (major->name, "Major"))
			major_list_init (typesel, major);

#if 1
	/* hard code for now until I figure out where to get a decent colour */
	select_color = 0xe090f840;
#else
	{
		GdkColor *color = typesel->canvas->style->base + GTK_STATE_SELECTED;

		select_color |= ((color->red >> 8) & 0xff)   << 24;
		select_color |= ((color->green >> 8) & 0xff) << 16;
		select_color |= ((color->blue >> 8) & 0xff)  << 8;
		select_color = 0x40; /* alpha of 25% */
	}
#endif

	/* The alpha blended selection box */
	typesel->selector = gnome_canvas_item_new (
		gnome_canvas_root (GNOME_CANVAS (typesel->canvas)),
		gnome_canvas_rect_get_type (),
		"fill_color_rgba",	select_color,
		"outline_color_rgba",	0x000000ff,	/* black */
		"width_pixels", 1,
		NULL);

	/* Setup the description label */
	typesel->label = GTK_LABEL (gtk_label_new (""));
	gtk_label_set_justify (typesel->label, GTK_JUSTIFY_LEFT);
	gtk_label_set_line_wrap (typesel->label, TRUE);
	gtk_misc_set_alignment (GTK_MISC (typesel->label), 0., .2);
	gtk_misc_set_padding (GTK_MISC (typesel->label), 5, 5);

	/* ICK ! How can I set the number of lines without looking at fonts */
	gtk_widget_set_usize (GTK_WIDGET (typesel->label), 350, 65);

	vbox = gtk_vbox_new (FALSE, 5);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);

	hbox = gtk_hbox_new (FALSE, 5);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);

	/* Set up sample button */
	typesel->sample_button = gtk_button_new_with_label (_("Show\nSample"));
	gtk_widget_set_sensitive (typesel->sample_button, FALSE);
	g_signal_connect (G_OBJECT (typesel->sample_button),
		"pressed",
		G_CALLBACK (cb_sample_pressed), typesel);
	g_signal_connect (G_OBJECT (typesel->sample_button), "released",
		G_CALLBACK (cb_sample_released), typesel);

	tmp = gtk_frame_new (_("Description"));
	gtk_container_add (GTK_CONTAINER (tmp), GTK_WIDGET (typesel->label));
	gtk_box_pack_start (GTK_BOX (hbox), tmp, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), typesel->sample_button, FALSE, TRUE, 0);

	typesel->notebook = gtk_notebook_new ();
	gtk_notebook_append_page (GTK_NOTEBOOK (typesel->notebook),
		vbox, gtk_label_new (_("Basic Types")));

	return typesel->notebook;
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
	s->series_delete    = glade_xml_get_widget (s->gui, "series_remove");
	s->series_details   = glade_xml_get_widget (s->gui, "series_details");
	s->selection_table  = glade_xml_get_widget (s->gui, "selection_table");
	s->shared_separator = glade_xml_get_widget (s->gui, "shared_separator");
	s->sample_frame	    = glade_xml_get_widget (s->gui, "sample_frame");
	s->shared_series_details = glade_xml_get_widget (s->gui, "shared_series_details");

	g_signal_connect (G_OBJECT (s->series_add),
		"clicked",
		G_CALLBACK (cb_graph_guru_series_add), s);
	g_signal_connect (G_OBJECT (s->series_delete),
		"clicked",
		G_CALLBACK (cb_graph_guru_series_delete), s);

	s->plot_selector    = graph_guru_selector_init (s, N_("Plot name"), 0,
		G_CALLBACK (cb_plot_entry_changed),
		G_CALLBACK (cb_plot_selection_changed));
	s->series_selector  = graph_guru_selector_init (s, N_("Series name"), 1,
		G_CALLBACK (cb_series_entry_changed),
		G_CALLBACK (cb_series_selection_changed));

	/* Lifecyle management */
	wbcg_edit_attach_guru (s->wbcg, s->dialog);
	g_signal_connect (G_OBJECT (s->dialog),
		"destroy",
		G_CALLBACK (cb_graph_guru_destroy), s);
	g_signal_connect (G_OBJECT (s->dialog),
		"key_press_event",
		G_CALLBACK (cb_graph_guru_key_press), s);
	g_signal_connect (G_OBJECT (s->dialog),
		"set-focus",
		G_CALLBACK (cb_graph_guru_focus), s);

	gnumeric_set_transient (s->wbcg, GTK_WINDOW (s->dialog));

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
	state->wb	= wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->scg	= wbcg_cur_scg (wbcg);
	state->sv	= sc_view (SHEET_CONTROL (state->scg));
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
#if 0
	state->type_selector	= CORBA_OBJECT_NIL;
	state->data_guru	= CORBA_OBJECT_NIL;
	state->format_guru	= CORBA_OBJECT_NIL;
#endif

	if (graph != NULL) {
		g_return_if_fail (IS_GNUMERIC_GRAPH (graph));

		state->graph = graph;
		g_object_ref (G_OBJECT (state->graph));
	} else
		state->graph = GNUMERIC_GRAPH (gnm_graph_new (state->wb));

	if (state->graph != NULL && page == 0) {
		GList *ptr = g_list_last (state->sv->selections);
		Range const *r = ptr->data;
		int num_cols = range_width (r);
		int num_rows = range_height (r);
		gboolean default_to_cols = (num_cols < num_rows);

		gnm_graph_clear_vectors (state->graph);

		/* Excel docs claim that rows == cols uses rows */

		/* selections are in reverse order */
		ptr = g_list_last (state->sv->selections);
		for (; ptr != NULL; ptr = ptr->prev)
			gnm_graph_range_to_vectors (state->graph, state->sheet,
				ptr->data, default_to_cols);
		gnm_graph_arrange_vectors (state->graph);
	}

	if (state->graph == NULL || graph_guru_init (state)) {
		graph_guru_state_destroy (state);
		return;
	}

	/* Ok everything is hooked up. Let-er rip */
	state->valid = TRUE;

	state->initial_page = page;
	if (page == 0) {
		GtkWidget *w = graph_guru_type_selector_new ();
		gtk_notebook_prepend_page (state->steps, w, NULL);
		gtk_widget_show_all (w);
	}

	gtk_widget_show_all (state->dialog);

	if (page > 0) {
		gtk_widget_hide (state->button_prev);
		gtk_widget_hide (state->button_next);
	}
	graph_guru_set_page (state, page);
}
