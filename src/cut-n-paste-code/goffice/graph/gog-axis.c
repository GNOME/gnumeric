/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-axis.c :
 *
 * Copyright (C) 2003 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
#include <goffice/graph/gog-axis.h>
#include <goffice/graph/gog-styled-object.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-data-set.h>
#include <goffice/graph/gog-data-allocator.h>
#include <goffice/graph/gog-view.h>
#include <goffice/graph/gog-chart.h>
#include <goffice/graph/gog-renderer.h>
#include <goffice/graph/go-data.h>

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>
#include <src/gui-util.h>
#include <gtk/gtktable.h>
#include <gtk/gtkcheckbutton.h>
#include <glade/glade-xml.h>

enum {
	AXIS_ELEM_MIN = 0,
	AXIS_ELEM_MAX,
	AXIS_ELEM_MAJOR_TICK,
	AXIS_ELEM_MINOR_TICK,
	AXIS_ELEM_LAST_ENTRY
};

struct _GogAxis {
	GogStyledObject	 base;

	GogAxisType	 type;
	GogAxisPosition	 pos;
	GSList		*i_cross, *crosses_me, *contributors;

	GogDatasetElement source [AXIS_ELEM_LAST_ENTRY];
	GogAxisTickLevel  tick_level;

	double		min_val, max_val;
	gpointer	min_contrib, max_contrib; /* NULL means use the manual sources */
};

typedef GogStyledObjectClass GogAxisClass;

static GType gog_axis_view_get_type (void);

static GObjectClass *parent_klass;

enum {
	AXIS_PROP_0,
	AXIS_PROP_TYPE,
	AXIS_PROP_POS,
	AXIS_PROP_POS_STR
};

static void
gog_axis_set_property (GObject *obj, guint param_id,
		       GValue const *value, GParamSpec *pspec)
{
	GogAxis *axis = GOG_AXIS (obj);

	switch (param_id) {
	case AXIS_PROP_TYPE:
		axis->type = g_value_get_int (value);
		break;
	case AXIS_PROP_POS:
		axis->pos = g_value_get_int (value);
		break;
	case AXIS_PROP_POS_STR: {
		char const *str = g_value_get_string (value);
		if (str == NULL)
			return;
		else if (!g_ascii_strcasecmp (str, "low"))
			axis->pos = GOG_AXIS_AT_LOW;
		else if (!g_ascii_strcasecmp (str, "middle"))
			axis->pos = GOG_AXIS_IN_MIDDLE;
		else if (!g_ascii_strcasecmp (str, "high"))
			axis->pos = GOG_AXIS_AT_HIGH;
		else
			return;
		break;
	}
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}
	gog_object_emit_changed (GOG_OBJECT (obj), FALSE);
}

static void
gog_axis_get_property (GObject *obj, guint param_id,
		       GValue *value, GParamSpec *pspec)
{
	GogAxis *axis = GOG_AXIS (obj);

	switch (param_id) {
	case AXIS_PROP_TYPE:
		g_value_set_int (value, axis->type);
		break;
	case AXIS_PROP_POS:
		g_value_set_int (value, axis->pos);
		break;
	case AXIS_PROP_POS_STR:
		switch (axis->pos) {
		case GOG_AXIS_AT_LOW:
			g_value_set_static_string (value, "low");
			break;
		case GOG_AXIS_IN_MIDDLE:
			g_value_set_static_string (value, "middle");
			break;
		case GOG_AXIS_AT_HIGH:
			g_value_set_static_string (value, "high");
			break;
		}
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static void
gog_axis_finalize (GObject *obj)
{
	GogAxis *axis = GOG_AXIS (obj);

	g_slist_free (axis->i_cross);	 	axis->i_cross = NULL;
	g_slist_free (axis->crosses_me); 	axis->crosses_me = NULL;
	g_slist_free (axis->contributors);	axis->contributors = NULL;

	gog_dataset_finalize (GOG_DATASET (axis));
	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (obj);
}

static void
gog_axis_update (GogObject *obj)
{
	GSList *ptr;
	GogAxis *axis = GOG_AXIS (obj);

	gog_debug (0, g_warning ("axis::update"););

	for (ptr = axis->contributors ; ptr != NULL ; ptr = ptr->next) {
	}
}

static void
make_dim_editor (GtkTable *table, unsigned row, char const *name, GtkWidget *editor)
{
	char *txt = g_strconcat (name, ":", NULL);
	GtkWidget *label = gtk_check_button_new_with_mnemonic (txt);
	g_free (txt);

	gtk_table_attach (table, label,
		0, 1, row, row+1, GTK_FILL, 0, 5, 3);
	gtk_table_attach (table, editor,
		1, 2, row, row+1, GTK_FILL | GTK_EXPAND, 0, 5, 3);
}

static void
cb_pos_changed (GtkToggleButton *toggle_button, GObject *axis)
{
	g_object_set (axis,
		"pos_str", gtk_toggle_button_get_active (toggle_button) ? "low" : "high",
		NULL);
}

static gpointer
gog_axis_editor (GogObject *gobj, GogDataAllocator *dalloc, CommandContext *cc)
{
	static char const *name[] = {
		N_("M_in"), N_("M_ax"),
		N_("Ma_jor Ticks"),
		N_("Mi_nor Ticks")
	};
	GtkWidget *w, *notebook;
	GtkTable  *table;
	unsigned row = 0;
	GogAxis *axis = GOG_AXIS (gobj);
	GogDataset *set = GOG_DATASET (gobj);
	GladeXML *gui;

	gui = gnm_glade_xml_new (cc, "gog-axis-prefs.glade", "axis_pref_table", NULL);
	if (gui == NULL)
		return NULL;
	/* Bounds Page */
	notebook = gtk_notebook_new ();
	w = gtk_table_new (1, 2, FALSE);
	table = GTK_TABLE (w);
	gtk_table_attach (table, gtk_label_new (_("Automatic")),
		0, 1, 0, 1, GTK_FILL, 0, 5, 3);
	for (row = AXIS_ELEM_MIN; row <= axis->tick_level; row++)
		make_dim_editor (table, row+1, _(name[row]),
			gog_data_allocator_editor (dalloc, set, row, TRUE));
	gtk_widget_show_all (w);
	gtk_notebook_prepend_page (GTK_NOTEBOOK (notebook), w,
		gtk_label_new (_("Bounds")));

	gtk_notebook_prepend_page (GTK_NOTEBOOK (notebook),
		glade_xml_get_widget (gui, "axis_pref_table"),
		gtk_label_new (_("Details")));
	gog_style_editor (gobj, cc, notebook, GOG_STYLE_LINE),

	w = glade_xml_get_widget (gui, "axis_low"),
	g_signal_connect_object (G_OBJECT (w),
		"toggled",
		G_CALLBACK (cb_pos_changed), axis, 0);

	g_object_set_data_full (G_OBJECT (notebook), "gui", gui,
				(GDestroyNotify)g_object_unref);

	gtk_widget_show (GTK_WIDGET (notebook));
	return notebook;
}
static unsigned
gog_axis_interesting_fields (GogStyledObject *obj)
{
	return GOG_STYLE_LINE | GOG_STYLE_FONT;
}

static void
gog_axis_class_init (GObjectClass *gobject_klass)
{
	static GogObjectRole const roles[] = {
		{ N_("Label"), "GogLabel", 0,
		  GOG_POSITION_COMPASS, GOG_POSITION_N|GOG_POSITION_ALIGN_CENTER, GOG_OBJECT_NAME_BY_ROLE,
		  NULL, NULL, NULL, NULL, NULL, NULL },
	};
	GogObjectClass *gog_klass = (GogObjectClass *) gobject_klass;
	GogStyledObjectClass *style_klass = (GogStyledObjectClass *) gog_klass;

	parent_klass = g_type_class_peek_parent (gobject_klass);
	gobject_klass->set_property = gog_axis_set_property;
	gobject_klass->get_property = gog_axis_get_property;
	gobject_klass->finalize	    = gog_axis_finalize;

	/* no need to persist, the role handles that */
	g_object_class_install_property (gobject_klass, AXIS_PROP_TYPE,
		g_param_spec_int ("type", "Type",
			"GogAxisType",
			GOG_AXIS_X, GOG_AXIS_TYPES, GOG_AXIS_TYPES, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_klass, AXIS_PROP_POS,
		g_param_spec_int ("pos", "pos",
			"GogAxisPosition",
			GOG_AXIS_AT_LOW, GOG_AXIS_AT_HIGH, GOG_AXIS_AT_LOW, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_klass, AXIS_PROP_POS_STR,
		g_param_spec_string ("pos_str", "pos_str",
			"Where to position an axis low, high, or crossing",
			"low", G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));

	gog_object_register_roles (gog_klass, roles, G_N_ELEMENTS (roles));
	gog_klass->update	= gog_axis_update;
	gog_klass->editor	= gog_axis_editor;
	gog_klass->view_type	= gog_axis_view_get_type ();
	style_klass->interesting_fields = gog_axis_interesting_fields;
}

static void
gog_axis_init (GogAxis *axis)
{
	axis->type	 = GOG_AXIS_UNKNOWN;
	axis->pos	 = GOG_AXIS_AT_LOW;
	axis->tick_level = AXIS_ELEM_MINOR_TICK,
	axis->i_cross =	axis->crosses_me = axis->contributors = NULL;

	/* yes we want min = MAX */
	axis->min_val = DBL_MAX;
	axis->max_val = DBL_MIN;
	axis->min_contrib = axis->max_contrib = NULL;
}

static void
gog_axis_dataset_dims (GogDataset const *set, int *first, int *last)
{
	*first = AXIS_ELEM_MIN;
	*last  = AXIS_ELEM_MINOR_TICK;
}

static GogDatasetElement *
gog_axis_dataset_get_elem (GogDataset const *set, int dim_i)
{
	GogAxis *axis = GOG_AXIS (set);
	if (AXIS_ELEM_MIN <= dim_i && dim_i <= AXIS_ELEM_MINOR_TICK)
		return &axis->source[dim_i];
	return NULL;
}

static void
gog_axis_dim_changed (GogDataset *set, int dim_i)
{
	gog_object_emit_changed (GOG_OBJECT (set), TRUE);
}

static void
gog_axis_dataset_init (GogDatasetClass *iface)
{
	iface->dims	   = gog_axis_dataset_dims;
	iface->get_elem	   = gog_axis_dataset_get_elem;
	iface->dim_changed = gog_axis_dim_changed;
}

GSF_CLASS_FULL (GogAxis, gog_axis,
		gog_axis_class_init, gog_axis_init,
		GOG_STYLED_OBJECT_TYPE, 0,
		GSF_INTERFACE (gog_axis_dataset_init, GOG_DATASET_TYPE))


GogAxisType
gog_axis_get_atype (GogAxis const *axis)
{
	g_return_val_if_fail (GOG_AXIS (axis) != NULL, GOG_AXIS_UNKNOWN);
	return axis->type;
}

GogAxisPosition
gog_axis_get_pos (GogAxis const *axis)
{
	g_return_val_if_fail (GOG_AXIS (axis) != NULL, GOG_AXIS_IN_MIDDLE);
	return axis->pos;
}

gboolean
gog_axis_get_bounds (GogAxis const *axis, double *min_val, double *max_val)
{
	g_return_val_if_fail (GOG_AXIS (axis) != NULL, FALSE);

	*min_val = axis->min_val;
	*max_val = axis->max_val;

	return TRUE;
}

/**
 * gog_axis_add_contributor :
 * @axis : #GogAxis
 * @contrib : #GogObject (can we relax this to use an interface ?)
 *
 * Register @contrib as taking part in the negotiation of @axis's bounds.
 **/
void
gog_axis_add_contributor (GogAxis *axis, GogObject *contrib)
{
	g_return_if_fail (GOG_AXIS (axis) != NULL);
	g_return_if_fail (g_slist_find (axis->contributors, contrib) == NULL);

	axis->contributors = g_slist_prepend (axis->contributors, contrib);
}

/**
 * gog_axis_del_contributor :
 * @axis : #GogAxis
 * @contrib : #GogObject (can we relax this to use an interface ?)
 *
 * @contrib no longer takes part in the negotiation of @axis's bounds.
 **/
void
gog_axis_del_contributor (GogAxis *axis, GogObject *contrib)
{
	g_return_if_fail (GOG_AXIS (axis) != NULL);
	g_return_if_fail (g_slist_find (axis->contributors, contrib) != NULL);

	axis->contributors = g_slist_remove (axis->contributors, contrib);
}

/**
 * gog_axis_bound_changed :
 * @axis : #GogAxis
 * @contrib : #GogObject
 * @low :
 * @high :
**/
void
gog_axis_bound_changed (GogAxis *axis, GogObject *contrib,
			double low, double high)
{
	g_return_if_fail (GOG_AXIS (axis) != NULL);

#warning crap
	axis->min_val = low;
	axis->max_val = high;

	gog_object_request_update (GOG_OBJECT (axis));
}

static unsigned
gog_axis_num_markers (GogAxis *axis)
{
#warning bogus quicky
	return 2;
}

static char const *
gog_axis_get_marker (GogAxis *axis, unsigned i)
{
#warning bogus quicky
	if (i == 0)
		return "low";
	return "high";
}

/****************************************************************************/

typedef GogView		GogAxisView;
typedef GogViewClass	GogAxisViewClass;

#define GOG_AXIS_VIEW_TYPE	(gog_axis_view_get_type ())
#define GOG_AXIS_VIEW(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_AXIS_VIEW_TYPE, GogAxisView))
#define IS_GOG_AXIS_VIEW(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_AXIS_VIEW_TYPE))

static GogViewClass *aview_parent_klass;

static void
gog_axis_view_size_request (GogView *view, GogViewRequisition *req)
{
	GogAxis *axis = GOG_AXIS (view->model);
	GogViewRequisition tmp;
	gboolean const is_horiz = axis->type == GOG_AXIS_X;
	int i;
	double total = 0., max = 0.;
	double line_width = gog_renderer_line_size (
		view->renderer, axis->base.style->line.width);

/* TODO : Think about rotating things or dropping markers periodically if
 * things are too big */
	gog_renderer_push_style (view->renderer, axis->base.style);
	for (i = gog_axis_num_markers (axis) ; i-- > 0 ; ) {
		gog_renderer_measure_text (view->renderer,
			gog_axis_get_marker (axis, i), &tmp);
		if (is_horiz) {
			total += tmp.w;
			if (max < tmp.h)
				max = tmp.h;
		} else {
			total += tmp.h;
			if (max < tmp.w)
				max = tmp.w;
		}
	}
	gog_renderer_pop_style (view->renderer);

	max += line_width;
	if (is_horiz) {
		req->w = total;
		req->h = max;
	} else {
		req->h = total;
		req->w = max;
	}
}

static void
gog_axis_view_render (GogView *view, GogViewAllocation const *bbox)
{
	GogViewAllocation const *area = &view->residual;
	ArtVpath path[3];
	GogAxis *axis = GOG_AXIS (view->model);
	double pre, post;
	double line_width = gog_renderer_line_size (
		view->renderer, axis->base.style->line.width) / 2;

	(aview_parent_klass->render) (view, bbox);

	g_return_if_fail (axis->pos != GOG_AXIS_IN_MIDDLE);

	switch (axis->type) {
	case GOG_AXIS_X:
		gog_chart_view_get_indents (view->parent, &pre, &post);
		path[0].x = area->x + pre;
		path[1].x = area->x + area->w - post;

		switch (axis->pos) {
		case GOG_AXIS_AT_LOW:
			path[0].y = path[1].y = area->y + line_width;
			break;

		case GOG_AXIS_AT_HIGH:
			path[0].y = path[1].y = area->y + area->h - line_width;
			break;
		default :
			break;
		}
		break;

	case GOG_AXIS_Y:
		path[0].y = area->y;
		path[1].y = area->y + area->h;
		switch (axis->pos) {
		case GOG_AXIS_AT_LOW:
			path[0].x = path[1].x = area->x + area->w - line_width;
			break;
		case GOG_AXIS_AT_HIGH:
			path[0].x = path[1].x = area->x + line_width;
			break;
		default :
			break;
		}
		break;
	default :
		break;
	}

	path[0].code = ART_MOVETO;
	path[1].code = ART_LINETO;
	path[2].code = ART_END;
	gog_renderer_push_style (view->renderer, axis->base.style);
	gog_renderer_draw_path (view->renderer, path);
	gog_renderer_pop_style (view->renderer);
}

static void
gog_axis_view_class_init (GogAxisViewClass *gview_klass)
{
	GogViewClass *view_klass    = (GogViewClass *) gview_klass;

	aview_parent_klass = g_type_class_peek_parent (gview_klass);
	view_klass->size_request    = gog_axis_view_size_request;
	view_klass->render	    = gog_axis_view_render;
}

static GSF_CLASS (GogAxisView, gog_axis_view,
		  gog_axis_view_class_init, NULL,
		  GOG_VIEW_TYPE)
