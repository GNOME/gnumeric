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
#include <goffice/graph/gog-plot.h>
#include <goffice/graph/gog-renderer.h>
#include <goffice/graph/go-data.h>

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>
#include <src/gui-util.h>
#include <src/mathfunc.h>
#include <gtk/gtktable.h>
#include <gtk/gtkcheckbutton.h>
#include <glade/glade-xml.h>

enum {
	AXIS_ELEM_MIN = 0,
	AXIS_ELEM_MAX,
	AXIS_ELEM_MAJOR_TICK,
	AXIS_ELEM_MINOR_TICK,
	AXIS_ELEM_MAX_ENTRY
};

struct _GogAxis {
	GogStyledObject	 base;

	GogAxisType	 type;
	GogAxisPosition	 pos;
	GSList		*i_cross, *crosses_me, *contributors;

	GogDatasetElement source [AXIS_ELEM_MAX_ENTRY];
	struct {
		gboolean tick_in, tick_out;
		int size_pts;
	} major, minor;
	gboolean major_tick_labeled;

	double		bound_min, bound_max, bound_step;
	double		min_val, max_val;
	double		logical_min_val, logical_max_val;
	gpointer	min_contrib, max_contrib; /* NULL means use the manual sources */
};

typedef GogStyledObjectClass GogAxisClass;

static GType gog_axis_view_get_type (void);

static GObjectClass *parent_klass;

enum {
	AXIS_PROP_0,
	AXIS_PROP_TYPE,
	AXIS_PROP_POS,
	AXIS_PROP_POS_STR,
	AXIS_PROP_MAJOR_TICK_LABELED,
	AXIS_PROP_MAJOR_TICK_IN,
	AXIS_PROP_MAJOR_TICK_OUT,
	AXIS_PROP_MAJOR_TICK_SIZE_PTS,
	AXIS_PROP_MINOR_TICK_IN,
	AXIS_PROP_MINOR_TICK_OUT,
	AXIS_PROP_MINOR_TICK_SIZE_PTS
};

#define TICK_LABEL_PAD 5

static void
gog_axis_set_property (GObject *obj, guint param_id,
		       GValue const *value, GParamSpec *pspec)
{
	GogAxis *axis = GOG_AXIS (obj);
	gboolean resized = FALSE;
	int itmp;

	switch (param_id) {
	case AXIS_PROP_TYPE:
		itmp = g_value_get_int (value);
		if (axis->type != itmp) {
			axis->type = itmp;
			resized = TRUE;
		}
		break;
	case AXIS_PROP_POS:
		itmp = g_value_get_int (value);
		if (axis->pos != itmp) {
			axis->type = itmp;
			resized = TRUE;
		}
		break;
	case AXIS_PROP_POS_STR: {
		char const *str = g_value_get_string (value);
		if (str == NULL)
			return;
		else if (!g_ascii_strcasecmp (str, "low"))
			itmp = GOG_AXIS_AT_LOW;
		else if (!g_ascii_strcasecmp (str, "middle"))
			itmp = GOG_AXIS_IN_MIDDLE;
		else if (!g_ascii_strcasecmp (str, "high"))
			itmp = GOG_AXIS_AT_HIGH;
		else
			return;
		if (axis->pos != itmp) {
			axis->type = itmp;
			resized = TRUE;
		}
		break;
	}
	case AXIS_PROP_MAJOR_TICK_LABELED:
		itmp = g_value_get_boolean (value);
		if (axis->major_tick_labeled != itmp) {
			axis->major_tick_labeled = itmp;
			resized = TRUE;
		}
		break;
	case AXIS_PROP_MAJOR_TICK_IN :
		axis->major.tick_in = g_value_get_boolean (value);
		break;
	case AXIS_PROP_MAJOR_TICK_OUT :
		itmp = g_value_get_boolean (value);
		if (axis->major.tick_out != itmp) {
			axis->major.tick_out = itmp;
			resized = axis->major.size_pts > 0;
		}
		break;
	case AXIS_PROP_MAJOR_TICK_SIZE_PTS:
		itmp = g_value_get_int (value);
		if (axis->major.size_pts != itmp) {
			axis->major.size_pts = itmp;
			resized = axis->major.tick_out;
		}
		break;

	case AXIS_PROP_MINOR_TICK_IN :
		axis->minor.tick_in = g_value_get_boolean (value);
		break;
	case AXIS_PROP_MINOR_TICK_OUT :
		itmp = g_value_get_boolean (value);
		if (axis->minor.tick_out != itmp) {
			axis->minor.tick_out = itmp;
			resized = axis->minor.size_pts > 0;
		}
		break;
	case AXIS_PROP_MINOR_TICK_SIZE_PTS:
		itmp = g_value_get_int (value);
		if (axis->minor.size_pts != itmp) {
			axis->minor.size_pts = itmp;
			resized = axis->minor.tick_out;
		}
		break;

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}
	gog_object_emit_changed (GOG_OBJECT (obj), resized);
}

static void
gog_axis_get_property (GObject *obj, guint param_id,
		       GValue *value, GParamSpec *pspec)
{
	GogAxis const *axis = GOG_AXIS (obj);

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

	case AXIS_PROP_MAJOR_TICK_LABELED:
		g_value_set_boolean (value, axis->major_tick_labeled);
		break;
	case AXIS_PROP_MAJOR_TICK_IN:
		g_value_set_boolean (value, axis->major.tick_in);
		break;
	case AXIS_PROP_MAJOR_TICK_OUT:
		g_value_set_boolean (value, axis->major.tick_out);
		break;
	case AXIS_PROP_MAJOR_TICK_SIZE_PTS:
		g_value_set_int (value, axis->major.size_pts);
		break;

	case AXIS_PROP_MINOR_TICK_IN:
		g_value_set_boolean (value, axis->minor.tick_in);
		break;
	case AXIS_PROP_MINOR_TICK_OUT:
		g_value_set_boolean (value, axis->minor.tick_out);
		break;
	case AXIS_PROP_MINOR_TICK_SIZE_PTS:
		g_value_set_int (value, axis->minor.size_pts);
		break;

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static void
gog_axis_finalize (GObject *obj)
{
	GogAxis *axis = GOG_AXIS (obj);

	gog_axis_clear_contributors (axis);

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
	double minima, maxima, logical_min, logical_max;
	double range, step;
	double old_min = axis->bound_min;
	double old_max = axis->bound_max;

	gog_debug (0, g_warning ("axis::update"););

	axis->min_val = DBL_MAX;
	axis->max_val = DBL_MIN;
	axis->min_contrib = axis->max_contrib = NULL;
	for (ptr = axis->contributors ; ptr != NULL ; ptr = ptr->next) {
		gog_plot_get_axis_bounds (GOG_PLOT (ptr->data), axis->type,
			&minima, &maxima, &logical_min, &logical_max);

		if (axis->min_val > minima) {
			axis->min_val = minima;
			axis->logical_min_val = logical_min;
			axis->min_contrib = ptr->data;
		} else if (axis->min_contrib == ptr->data) {
			axis->min_contrib = NULL;
			axis->min_val = minima;
		}

		if (axis->max_val < maxima) {
			axis->max_val = maxima;
			axis->logical_max_val = logical_max;
			axis->max_contrib = ptr->data;
		} else if (axis->max_contrib == ptr->data) {
			axis->max_contrib = NULL;
			axis->max_val = maxima;
		}
	}

	minima = axis->min_val;
	maxima = axis->max_val;
	if (minima < maxima) {
		range = fabs (maxima - minima);
		if (gnumeric_sub_epsilon (range) < 0.) {
			minima *= .9;
			maxima *= 1.1;
			range = fabs (maxima - minima);
		}
		step  = pow (10, gnumeric_fake_trunc (log10 (range)));
		if (range/step < 3)
			step /= 2.;	/* 0 5 10 */
		else if (range/step > 8)
			step *= 2.;	/* 2 4 6 */

		/* we want the bounds to be loose so jump up a step if we get too close */
		axis->bound_min = step * floor (gnumeric_sub_epsilon (minima/step));
		axis->bound_max = step * ceil (gnumeric_add_epsilon (maxima/step));
		axis->bound_step = step;

		/* pull to zero if its nearby */
		if (axis->bound_min > 0 && (axis->bound_min - 10. * step) < 0)
			axis->bound_min = 0;
		if (axis->bound_max < 0 && (axis->bound_max + 10. * step) < 0)
			axis->bound_max = 0;
	} else
		axis->bound_min = axis->bound_max = axis->bound_step = 0.;

	if (old_min != axis->bound_min || old_max != axis->bound_max)
		gog_object_emit_changed (GOG_OBJECT (obj), TRUE);
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
	for (row = AXIS_ELEM_MIN; row < AXIS_ELEM_MAX_ENTRY ; row++)
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
			GOG_AXIS_UNKNOWN, GOG_AXIS_TYPES, GOG_AXIS_UNKNOWN, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_klass, AXIS_PROP_POS,
		g_param_spec_int ("pos", "pos",
			"GogAxisPosition",
			GOG_AXIS_AT_LOW, GOG_AXIS_AT_HIGH, GOG_AXIS_AT_LOW, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_klass, AXIS_PROP_POS_STR,
		g_param_spec_string ("pos_str", "pos_str",
			"Where to position an axis low, high, or crossing",
			"low", G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));

	g_object_class_install_property (gobject_klass, AXIS_PROP_MAJOR_TICK_LABELED,
		g_param_spec_boolean ("major-tick-labeled", NULL,
			"Show labels for major ticks",
			TRUE, G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));
	g_object_class_install_property (gobject_klass, AXIS_PROP_MAJOR_TICK_IN,
		g_param_spec_boolean ("major-tick-in", NULL,
			"Major tick marks inside the axis",
			FALSE, G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));
	g_object_class_install_property (gobject_klass, AXIS_PROP_MAJOR_TICK_OUT,
		g_param_spec_boolean ("major-tick-out", NULL,
			"Major tick marks outside the axis",
			TRUE, G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));
	g_object_class_install_property (gobject_klass, AXIS_PROP_MAJOR_TICK_SIZE_PTS,
		g_param_spec_int ("major-tick-size-pts", "major-tick-size-pts",
			"Size of the major tick marks in pts",
			0, 20, 4, G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));

	g_object_class_install_property (gobject_klass, AXIS_PROP_MINOR_TICK_IN,
		g_param_spec_boolean ("minor-tick-in", NULL,
			"Minor tick marks inside the axis",
			FALSE, G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));
	g_object_class_install_property (gobject_klass, AXIS_PROP_MINOR_TICK_OUT,
		g_param_spec_boolean ("minor-tick-out", NULL,
			"Minor tick marks outside the axis",
			FALSE, G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));
	g_object_class_install_property (gobject_klass, AXIS_PROP_MINOR_TICK_SIZE_PTS,
		g_param_spec_int ("minor-tick-size-pts", "minor-tick-size-pts",
			"Size of the minor tick marks in pts",
			0, 15, 2, G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));

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
	axis->i_cross =	axis->crosses_me = axis->contributors = NULL;
	axis->minor.tick_in = axis->minor.tick_out = axis->major.tick_in = FALSE;
	axis->major.tick_out = TRUE;
	axis->major_tick_labeled = TRUE;
	axis->major.size_pts = 4;
	axis->minor.size_pts = 2;

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
gog_axis_get_bounds (GogAxis const *axis, double *min_bound, double *max_bound)
{
	g_return_val_if_fail (GOG_AXIS (axis) != NULL, FALSE);

	*min_bound = axis->bound_min;
	*max_bound = axis->bound_max;

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

	gog_object_request_update (GOG_OBJECT (axis));
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
	gboolean update = FALSE;

	g_return_if_fail (GOG_AXIS (axis) != NULL);
	g_return_if_fail (g_slist_find (axis->contributors, contrib) != NULL);

	if (axis->min_contrib == contrib) {
		axis->min_contrib = NULL;
		update = TRUE;
	}
	if (axis->max_contrib == contrib) {
		axis->max_contrib = NULL;
		update = TRUE;
	}
	axis->contributors = g_slist_remove (axis->contributors, contrib);

	if (update)
		gog_object_request_update (GOG_OBJECT (axis));
}

void
gog_axis_clear_contributors (GogAxis *axis)
{
	GSList *ptr, *list;
	GogAxisSet filter;

	g_return_if_fail (GOG_AXIS (axis) != NULL);

	filter = 1 << axis->type;
	list = g_slist_copy (axis->contributors);
	for (ptr = list; ptr != NULL ; ptr = ptr->next)
		gog_plot_axis_clear (GOG_PLOT (ptr->data), filter);
	g_slist_free (list);
}

GSList const *
gog_axis_contributors (GogAxis *axis)
{
	g_return_val_if_fail (GOG_AXIS (axis) != NULL, NULL);

	return axis->contributors;
}

/**
 * gog_axis_bound_changed :
 * @axis : #GogAxis
 * @contrib : #GogObject
**/
void
gog_axis_bound_changed (GogAxis *axis, GogObject *contrib)
{
	g_return_if_fail (GOG_AXIS (axis) != NULL);

	gog_object_request_update (GOG_OBJECT (axis));
}

static unsigned
gog_axis_num_markers (GogAxis *axis)
{
	if (axis->bound_step <= 0.)
		return 0;

	return 1 + fabs (axis->bound_max - axis->bound_min) / (double)axis->bound_step;
}

static char *
gog_axis_get_marker (GogAxis *axis, unsigned i)
{
	return g_strdup_printf ("%g", axis->bound_min + ((double)i) * axis->bound_step);
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
	double total = 0., max = 0., tick_major = 0., tick_minor = 0.;
	double line_width = gog_renderer_line_size (
		view->renderer, axis->base.style->line.width);

/* TODO : Think about rotating things or dropping markers periodically if
 * things are too big */
	if (axis->major_tick_labeled) {
		gog_renderer_push_style (view->renderer, axis->base.style);
		for (i = gog_axis_num_markers (axis) ; i-- > 0 ; ) {
			char *txt = gog_axis_get_marker (axis, i);
			gog_renderer_measure_text (view->renderer, txt, &tmp);
			g_free (txt);
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
	}

	max += line_width;
	if (is_horiz) {
		if (axis->major.tick_out)
			tick_major = gog_renderer_pt2r_y (view->renderer,
				axis->major.size_pts + TICK_LABEL_PAD);
		if (axis->minor.tick_out)
			tick_minor = gog_renderer_pt2r_y (view->renderer,
							  axis->minor.size_pts);
		req->w = total;
		req->h = max + MAX (tick_major, tick_minor);
	} else {
		if (axis->major.tick_out)
			tick_major = gog_renderer_pt2r_x (view->renderer,
				axis->major.size_pts + TICK_LABEL_PAD);
		if (axis->minor.tick_out)
			tick_minor = gog_renderer_pt2r_x (view->renderer,
							  axis->minor.size_pts);
		req->h = total;
		req->w = max + MAX (tick_major, tick_minor);
	}
}

static void
gog_axis_view_render (GogView *view, GogViewAllocation const *bbox)
{
	GtkAnchorType anchor;
	GogViewAllocation const *area = &view->residual;
	GogViewRequisition size;
	ArtVpath path[3], major_path[3], minor_path[3];
	ArtPoint pos;
	GogAxis *axis = GOG_AXIS (view->model);
	unsigned n;
	double pre, post, step = 0, bound, tick_len, tick_pad;
	double line_width = gog_renderer_line_size (
		view->renderer, axis->base.style->line.width) / 2;

	(aview_parent_klass->render) (view, bbox);

	g_return_if_fail (axis->pos != GOG_AXIS_IN_MIDDLE);

	gog_renderer_push_style (view->renderer, axis->base.style);
	path[0].code = major_path[0].code = minor_path[0].code = ART_MOVETO;
	path[1].code = major_path[1].code = minor_path[1].code = ART_LINETO;
	path[2].code = major_path[2].code = minor_path[2].code = ART_END;

	n = gog_axis_num_markers (axis);
	switch (axis->type) {
	case GOG_AXIS_X:
		gog_chart_view_get_indents (view->parent, &pre, &post);
		path[0].x = area->x + pre;
		pos.x = path[1].x = area->x + area->w - post;
		tick_pad = gog_renderer_pt2r_y (view->renderer, TICK_LABEL_PAD);
		tick_len = gog_renderer_pt2r_y (view->renderer,
						axis->major.size_pts);

		switch (axis->pos) {
		case GOG_AXIS_AT_LOW:
			anchor = GTK_ANCHOR_N;
			path[0].y = path[1].y = area->y + line_width;
			major_path[0].y = path[0].y  + line_width;
			break;

		case GOG_AXIS_AT_HIGH:
			anchor = GTK_ANCHOR_S;
			path[0].y = path[1].y = area->y + area->h - line_width;
			major_path[0].y = path[0].y  - line_width;
			tick_len *= -1; tick_pad *= -1;
			break;
		default :
			break;
		}
		major_path[1].y = major_path[0].y + tick_len;
		pos.y  = major_path[1].y + tick_pad;

		if (n > 1)
			step = (area->w - pre - post) / (n - 1);
		for (bound = DBL_MAX ; n-- > 0 ;) {
			if (axis->major_tick_labeled && pos.x < bound) {
				char *txt = gog_axis_get_marker (axis, n);

				size.h = area->h - line_width;
				size.w = -1;
				gog_renderer_draw_text (view->renderer, &pos, anchor, txt, &size);
				g_free (txt);
				bound = pos.x - size.w;
			}
			major_path[1].x = major_path[0].x = pos.x;
			gog_renderer_draw_path (view->renderer, major_path);
			pos.x -= step;
		}
		break;

	case GOG_AXIS_Y:
		pos.y = path[0].y = area->y;
		path[1].y = area->y + area->h;
		tick_pad = gog_renderer_pt2r_x (view->renderer, TICK_LABEL_PAD);
		tick_len = gog_renderer_pt2r_x (view->renderer,
						axis->major.size_pts);
		switch (axis->pos) {
		case GOG_AXIS_AT_LOW:
			anchor = GTK_ANCHOR_E;
			path[0].x = path[1].x = area->x + area->w - line_width;
			major_path[0].x = path[0].x  - line_width;
			tick_len *= -1; tick_pad *= -1;
			break;
		case GOG_AXIS_AT_HIGH:
			anchor = GTK_ANCHOR_W;
			path[0].x = path[1].x = area->x + line_width;
			major_path[0].x = path[0].x  + line_width;
			break;
		default :
			break;
		}
		major_path[1].x = major_path[0].x + tick_len;
		pos.x  = major_path[1].x + tick_pad;

		if (n > 1)
			step = area->h / (n - 1);
		for (bound = -1 ; n-- > 0 ;) {
			if (axis->major_tick_labeled && pos.y > bound) {
				char *txt = gog_axis_get_marker (axis, n);

				size.w = area->w - line_width;
				size.h = -1;
				gog_renderer_draw_text (view->renderer, &pos,
							anchor, txt, &size);
				g_free (txt);
				bound = pos.y + size.h;
			}
			major_path[1].y = major_path[0].y = pos.y;
			gog_renderer_draw_path (view->renderer, major_path);
			pos.y += step;
		}
		break;
	default :
		break;
	}

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
