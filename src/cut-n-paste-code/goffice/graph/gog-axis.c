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
#include <goffice/graph/gog-theme.h>
#include <goffice/graph/gog-data-set.h>
#include <goffice/graph/gog-data-allocator.h>
#include <goffice/graph/gog-view.h>
#include <goffice/graph/gog-chart.h>
#include <goffice/graph/gog-label.h>
#include <goffice/graph/gog-plot.h>
#include <goffice/graph/gog-renderer.h>
#include <goffice/graph/go-data.h>

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>
#include <src/gui-util.h>
#include <src/mathfunc.h>
#include <gtk/gtktable.h>
#include <gtk/gtkcheckbutton.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkmisc.h>
#include <glade/glade-xml.h>

struct _GogAxis {
	GogStyledObject	 base;

	GogAxisType	 type;
	GogAxisPosition	 pos;
	GSList		*contributors;

	GogDatasetElement source [AXIS_ELEM_MAX_ENTRY];
	double		  auto_bound [AXIS_ELEM_MAX_ENTRY];
	struct {
		gboolean tick_in, tick_out;
		int size_pts;
	} major, minor;
	gboolean major_tick_labeled;
	gboolean inverted, log_scale;

	double		min_val, max_val;
	double		logical_min_val, logical_max_val;
	gpointer	min_contrib, max_contrib; /* NULL means use the manual sources */
	gboolean	is_discrete;
	GODataVector	*labels;
};

typedef GogStyledObjectClass GogAxisClass;

static GType gog_axis_view_get_type (void);

static GObjectClass *parent_klass;

enum {
	AXIS_PROP_0,
	AXIS_PROP_TYPE,
	AXIS_PROP_POS,
	AXIS_PROP_POS_STR,
	AXIS_PROP_INVERT,
	AXIS_PROP_LOG_SCALE,
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
role_label_post_add (GogObject *parent, GogObject *label)
{
	GogAxis const *axis = GOG_AXIS (parent);
	if (axis->pos == GOG_AXIS_AT_LOW)
		label->position = (axis->type == GOG_AXIS_X)
			? (GOG_POSITION_S|GOG_POSITION_ALIGN_CENTER)
			: (GOG_POSITION_W|GOG_POSITION_ALIGN_CENTER);
	else
		label->position = (axis->type == GOG_AXIS_X)
			? (GOG_POSITION_N|GOG_POSITION_ALIGN_CENTER)
			: (GOG_POSITION_E|GOG_POSITION_ALIGN_CENTER);
}

#if 0
static gboolean
role_label_can_add (GogObject const *parent)
{
	GogAxis const *axis = GOG_AXIS (parent);
	return axis->type == GOG_AXIS_X;
}
#endif

static gboolean
gog_axis_set_pos (GogAxis *axis, GogAxisPosition pos)
{
	GSList *ptr;
	if (axis->pos == pos)
		return FALSE;
	axis->pos = pos;

	for (ptr = GOG_OBJECT (axis)->children ; ptr != NULL ; ptr = ptr->next)
		if (IS_GOG_LABEL (ptr->data))
			role_label_post_add (GOG_OBJECT (axis), ptr->data);
	return TRUE;
}

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
		resized = gog_axis_set_pos (axis, g_value_get_int (value));
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
		resized = gog_axis_set_pos (axis, itmp);
		break;
	}
	case AXIS_PROP_INVERT:
		axis->inverted = g_value_get_boolean (value);
		break;
	case AXIS_PROP_LOG_SCALE:
		axis->log_scale = g_value_get_boolean (value);
		break;

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
	case AXIS_PROP_INVERT:
		g_value_set_boolean (value, axis->inverted);
		break;
	case AXIS_PROP_LOG_SCALE:
		g_value_set_boolean (value, axis->log_scale);
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

	g_slist_free (axis->contributors);	axis->contributors = NULL;

	gog_dataset_finalize (GOG_DATASET (axis));
	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (obj);
}

static double
axis_get_entry (GogAxis const *axis, unsigned i, gboolean *user_defined)
{
	GOData *dat = axis->source [i].data;
	if (dat != NULL && IS_GO_DATA_SCALAR (dat)) {
		double tmp = go_data_scalar_get_value (GO_DATA_SCALAR (dat));
		if (finite (tmp)) {
			if (user_defined)
				*user_defined = TRUE;
			return tmp;
		}
	}
	
	if (user_defined)
		*user_defined = FALSE;

	return axis->auto_bound [i];
}

static void
gog_axis_update (GogObject *obj)
{
	GSList *ptr;
	GogAxis *axis = GOG_AXIS (obj);
	int expon;
	double range, step, mant;
	double minima, maxima, logical_min, logical_max;
	double old_min = axis->auto_bound [AXIS_ELEM_MIN];
	double old_max = axis->auto_bound [AXIS_ELEM_MAX];
	GOData *labels;
	gboolean is_discrete;
	gboolean user_defined;
	double tmp;

	gog_debug (0, g_warning ("axis::update"););

	axis->is_discrete = TRUE;
	axis->labels   = NULL;
	axis->min_val  =  DBL_MAX;
	axis->max_val  = -DBL_MAX;
	axis->min_contrib = axis->max_contrib = NULL;
	for (ptr = axis->contributors ; ptr != NULL ; ptr = ptr->next) {
		labels = gog_plot_get_axis_bounds (GOG_PLOT (ptr->data), axis->type,
			&minima, &maxima, &logical_min, &logical_max, &is_discrete);

		/* value dimensions have more information than index dimensions.
		 * At least thats what I am guessing today*/
		if (!is_discrete)
			axis->is_discrete = FALSE;
		else if (axis->labels == NULL && labels != NULL)
			axis->labels = GO_DATA_VECTOR (labels);

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

	tmp = axis_get_entry (GOG_AXIS (obj), AXIS_ELEM_MIN, &user_defined);
	if (user_defined) minima = tmp;

	tmp = axis_get_entry (GOG_AXIS (obj), AXIS_ELEM_MAX, &user_defined);
	if (user_defined) maxima = tmp;

	if (minima < maxima) {
		range = fabs (maxima - minima);
		if (gnumeric_sub_epsilon (range) < 0.) {
			minima *= .9;
			maxima *= 1.1;
			range = fabs (maxima - minima);
		}
		step  = pow (10, gnumeric_fake_floor (log10 (range)));
		if (range/step < 1.6)
			step /= 5.;	/* .2 .4 .6 */
		else if (range/step < 3)
			step /= 2.;	/* 0 5 10 */
		else if (range/step > 8)
			step *= 2.;	/* 2 4 6 */

		/* we want the bounds to be loose so jump up a step if we get too close */
		mant = frexpgnum (minima/step, &expon);
		axis->auto_bound [AXIS_ELEM_MIN] = step *
			floor (ldexpgnum (mant - GNUM_EPSILON, expon));
		mant = frexpgnum (maxima/step, &expon);
		axis->auto_bound [AXIS_ELEM_MAX] = step *
			ceil (ldexpgnum (mant + GNUM_EPSILON, expon));
		axis->auto_bound [AXIS_ELEM_MAJOR_TICK] = step;
		axis->auto_bound [AXIS_ELEM_MINOR_TICK] = step / 5.;

		/* pull to zero if its nearby (do not pull both directions to 0) */
		if (axis->auto_bound [AXIS_ELEM_MIN] > 0 &&
		    (axis->auto_bound [AXIS_ELEM_MIN] - 10. * step) < 0)
			axis->auto_bound [AXIS_ELEM_MIN] = 0;
		else if (axis->auto_bound [AXIS_ELEM_MAX] < 0 &&
		    (axis->auto_bound [AXIS_ELEM_MAX] + 10. * step) < 0)
			axis->auto_bound [AXIS_ELEM_MAX] = 0;

		/* The epsilon shift can pull us away from a zero we want to
		 * keep (eg percentage bars withno negative elements) */
		if (axis->auto_bound [AXIS_ELEM_MIN] < 0 && minima >= 0.)
			axis->auto_bound [AXIS_ELEM_MIN] = 0;
		else if (axis->auto_bound [AXIS_ELEM_MAX] > 0 && maxima <= 0.)
			axis->auto_bound [AXIS_ELEM_MAX] = 0;

		if (finite (axis->logical_min_val) &&
		    axis->auto_bound [AXIS_ELEM_MIN] < axis->logical_min_val)
			axis->auto_bound [AXIS_ELEM_MIN] = axis->logical_min_val;
		if (finite (axis->logical_max_val) &&
		    axis->auto_bound [AXIS_ELEM_MAX] > axis->logical_max_val)
			axis->auto_bound [AXIS_ELEM_MAX] = axis->logical_max_val;
	} else
		axis->auto_bound [AXIS_ELEM_MIN] =
		axis->auto_bound [AXIS_ELEM_MAX] =
		axis->auto_bound [AXIS_ELEM_MAJOR_TICK] = 0.;

	if (old_min != axis->auto_bound [AXIS_ELEM_MIN] ||
	    old_max != axis->auto_bound [AXIS_ELEM_MAX])
		gog_object_emit_changed (GOG_OBJECT (obj), TRUE);
}

static void
cb_pos_changed (GtkToggleButton *toggle_button, GObject *axis)
{
	g_object_set (axis,
		"pos_str", gtk_toggle_button_get_active (toggle_button) ? "low" : "high",
		NULL);
}

static void
cb_axis_toggle_changed (GtkToggleButton *toggle_button, GObject *axis)
{
	g_object_set (axis,
		gtk_widget_get_name (GTK_WIDGET (toggle_button)),
		gtk_toggle_button_get_active (toggle_button),
		NULL);
}

typedef struct {
	GtkWidget *editor;
	GtkToggleButton *toggle;
	GogDataset *set;
	unsigned dim;
} ElemToggleData;

static void
cb_enable_dim (GtkToggleButton *toggle_button, ElemToggleData *closure)
{
	gboolean is_auto = gtk_toggle_button_get_active (toggle_button);
	double bound = GOG_AXIS (closure->set)->auto_bound [closure->dim];

	gtk_widget_set_sensitive (closure->editor, !is_auto);

	if (is_auto) /* clear the data */
		gog_dataset_set_dim (closure->set, closure->dim, NULL, NULL);

	if (finite (bound) && DBL_MAX > bound && bound > -DBL_MAX) {
		char *str = g_strdup_printf ("%g", bound);
		g_object_set (closure->editor, "text", str, NULL);
		g_free (str);
	} else
		g_object_set (closure->editor, "text", "", NULL);
}

static void
cb_axis_bound_changed (GogObject *axis, gboolean resize, ElemToggleData *closure)
{
	if (gtk_toggle_button_get_active (closure->toggle)) {
		double bound = GOG_AXIS (closure->set)->auto_bound [closure->dim];
		if (finite (bound) && DBL_MAX > bound && bound > -DBL_MAX) {
			char *str = g_strdup_printf ("%g", bound);
			g_object_set (closure->editor, "text", str, NULL);
			g_free (str);
		} else
			g_object_set (closure->editor, "text", "", NULL);
	}
}

static void
make_dim_editor (GogDataset *set, GtkTable *table, unsigned dim,
		 GogDataAllocator *dalloc)
{
	static char const *dim_name[] = {
		N_("M_in"),
		N_("M_ax"),
		N_("Ma_jor Ticks"),
		N_("Mi_nor Ticks"),
		N_("_Cross")
	};
	ElemToggleData *info;
	GClosure *closure;
	GtkWidget *editor = gog_data_allocator_editor (dalloc, set, dim, TRUE);
	char *txt = g_strconcat (_(dim_name [dim]), ":", NULL);
	GtkWidget *toggle = gtk_check_button_new_with_mnemonic (txt);
	g_free (txt);

	info = g_new0 (ElemToggleData, 1);
	info->editor = editor;
	info->set = set;
	info->dim = dim;
	info->toggle = GTK_TOGGLE_BUTTON (toggle);
	g_signal_connect (G_OBJECT (toggle),
		"toggled",
		G_CALLBACK (cb_enable_dim), info);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle),
		gog_dataset_get_dim (set, dim) == NULL);

	closure = g_cclosure_new (G_CALLBACK (cb_axis_bound_changed),
				  info, (GClosureNotify)g_free);
	g_object_watch_closure (G_OBJECT (toggle), closure);
	g_signal_connect_closure (G_OBJECT (set),
		"changed",
		closure, FALSE);

	gtk_table_attach (table, toggle,
		0, 1, dim + 1, dim + 2, GTK_FILL, 0, 5, 3);
	gtk_table_attach (table, editor,
		1, 2, dim + 1, dim + 2, GTK_FILL | GTK_EXPAND, 0, 5, 3);
}

static gpointer
gog_axis_editor (GogObject *gobj, GogDataAllocator *dalloc, GnmCmdContext *cc)
{
	static guint axis_pref_page = 0;
	static char const *toggle_props[] = {
		"invert-axis",
		"major-tick-labeled",
		"major-tick-out",
		"major-tick-in",
		"minor-tick-out",
		"minor-tick-in"
	};
	GtkWidget *w, *notebook;
	GtkTable  *table;
	gboolean cur_val;
	unsigned i = 0;
	GogAxis *axis = GOG_AXIS (gobj);
	GogDataset *set = GOG_DATASET (gobj);
	GladeXML *gui;

	gui = gnm_glade_xml_new (cc, "gog-axis-prefs.glade", "axis_pref_table", NULL);
	if (gui == NULL)
		return NULL;
	notebook = gtk_notebook_new ();

	if (!axis->is_discrete) {
		/* Bounds Page */
		w = gtk_table_new (1, 2, FALSE);
		table = GTK_TABLE (w);
		w = gtk_label_new (_("Automatic"));
		gtk_misc_set_alignment (GTK_MISC (w), 0., .5);
		gtk_table_attach (table, w, 0, 1, 0, 1, GTK_FILL, 0, 5, 3);
		for (i = AXIS_ELEM_MIN; i < AXIS_ELEM_MAX_ENTRY ; i++)
			make_dim_editor (set, table, i, dalloc);
		gtk_widget_show_all (GTK_WIDGET (table));
		gtk_notebook_prepend_page (GTK_NOTEBOOK (notebook), GTK_WIDGET (table),
			gtk_label_new (_("Bounds")));
	}

	gtk_notebook_prepend_page (GTK_NOTEBOOK (notebook),
		glade_xml_get_widget (gui, "axis_pref_table"),
		gtk_label_new (_("Details")));
	gog_style_editor (GOG_STYLED_OBJECT (gobj), cc, notebook);

	w = glade_xml_get_widget (gui, "axis_low");
	if (axis->pos == GOG_AXIS_AT_LOW)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), TRUE);
	else
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
			glade_xml_get_widget (gui, "axis_high")), TRUE);
	g_signal_connect_object (G_OBJECT (w),
		"toggled",
		G_CALLBACK (cb_pos_changed), axis, 0);

	for (i = 0; i < G_N_ELEMENTS (toggle_props) ; i++) {
		w = glade_xml_get_widget (gui, toggle_props[i]);
		g_object_get (G_OBJECT (gobj), toggle_props[i], &cur_val, NULL);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), cur_val);
		g_signal_connect_object (G_OBJECT (w),
			"toggled",
			G_CALLBACK (cb_axis_toggle_changed), axis, 0);
	}

	g_object_set_data_full (G_OBJECT (notebook), "gui", gui,
				(GDestroyNotify)g_object_unref);

	gog_style_handle_notebook (notebook, &axis_pref_page);
	gtk_widget_show (GTK_WIDGET (notebook));
	return notebook;
}

static void
gog_axis_init_style (GogStyledObject *gso, GogStyle *style)
{
	style->interesting_fields = GOG_STYLE_LINE | GOG_STYLE_FONT;
	gog_theme_init_style (gog_object_get_theme (GOG_OBJECT (gso)),
		style, GOG_OBJECT (gso), 0);
}

static void
gog_axis_class_init (GObjectClass *gobject_klass)
{
	static GogObjectRole const roles[] = {
		{ N_("Label"), "GogLabel", 0,
		  GOG_POSITION_COMPASS, GOG_POSITION_S|GOG_POSITION_ALIGN_CENTER, GOG_OBJECT_NAME_BY_ROLE,
		  NULL, NULL, NULL, role_label_post_add, NULL, NULL, { -1 } }
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

	g_object_class_install_property (gobject_klass, AXIS_PROP_INVERT,
		g_param_spec_boolean ("invert-axis", NULL,
			"Scale from high to low rather than low to high",
			FALSE, G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));
	g_object_class_install_property (gobject_klass, AXIS_PROP_LOG_SCALE,
		g_param_spec_boolean ("log-scale", NULL,
			"Use a logarithmic scale",
			FALSE, G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));
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
	style_klass->init_style = gog_axis_init_style;
}

static void
gog_axis_init (GogAxis *axis)
{
	axis->type	 = GOG_AXIS_UNKNOWN;
	axis->pos	 = GOG_AXIS_AT_LOW;
	axis->contributors = NULL;
	axis->minor.tick_in = axis->minor.tick_out = axis->major.tick_in = FALSE;
	axis->major.tick_out = TRUE;
	axis->major_tick_labeled = TRUE;
	axis->inverted = FALSE;
	axis->log_scale = FALSE;
	axis->major.size_pts = 4;
	axis->minor.size_pts = 2;

	/* yes we want min = MAX */
	axis->min_val =  DBL_MAX;
	axis->max_val = -DBL_MAX;
	axis->min_contrib = axis->max_contrib = NULL;
	axis->is_discrete = FALSE;
	axis->labels = NULL;
}

static void
gog_axis_dataset_dims (GogDataset const *set, int *first, int *last)
{
	*first = AXIS_ELEM_MIN;
	*last  = AXIS_ELEM_CROSS_POINT;
}

static GogDatasetElement *
gog_axis_dataset_get_elem (GogDataset const *set, int dim_i)
{
	GogAxis *axis = GOG_AXIS (set);
	if (AXIS_ELEM_MIN <= dim_i && dim_i <= AXIS_ELEM_CROSS_POINT)
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

/**
 * gog_axis_is_discrete :
 * @axis : #GogAxis
 * 
 * Returns TRUE if @axis enumerates a set of discrete items, rather than a
 * continuous value
 **/ 
gboolean
gog_axis_is_discrete (GogAxis const *axis)
{
	return axis->is_discrete;
}

/**
 * gog_axis_get_bounds :
 * @axis : #GogAxis
 * @minima : result
 * @maxima : result
 *
 * return TRUE if the bounds stored in @minima and @maxima are sane
 **/
gboolean
gog_axis_get_bounds (GogAxis const *axis, double *minima, double *maxima)
{
	g_return_val_if_fail (GOG_AXIS (axis) != NULL, FALSE);

	*minima = axis_get_entry (axis, AXIS_ELEM_MIN, NULL);
	*maxima = axis_get_entry (axis, AXIS_ELEM_MAX, NULL);

	return finite (*minima) && finite (*maxima) && *minima < *maxima;
}

/**
 * gog_axis_get_ticks :
 * @axis : #GogAxis
 *
 * @major : result
 * @major : result
 *
 * Retreive the major and minor 'ticks' (increments) used to step from the
 * minimum to the maximum.
 **/
void
gog_axis_get_ticks (GogAxis const *axis, double *major, double *minor)
{
	g_return_if_fail (GOG_AXIS (axis) != NULL);

	if (major != NULL)
		*major = axis_get_entry (axis, AXIS_ELEM_MAJOR_TICK, NULL);
	if (minor != NULL)
		*minor = axis_get_entry (axis, AXIS_ELEM_MINOR_TICK, NULL);
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
gog_axis_num_markers (GogAxis *axis, double *step)
{
	double minima, maxima;

	if (axis->is_discrete) {
		int n = 0;
		if (axis->labels != NULL)
			n = go_data_vector_get_len (axis->labels);
		else if (axis->max_val >= axis->min_val)	/* case there is no data */
			n = gnumeric_fake_trunc (axis->max_val);
		if (n < 1)
			n = 1;
		if (step != NULL)
			*step = 1. / n;
		return n;
	} else {
		double major_tick = axis_get_entry (axis, AXIS_ELEM_MAJOR_TICK, NULL);
		if (major_tick <= 0. ||
		    !gog_axis_get_bounds (axis, &minima, &maxima)) {
			if (step != NULL)
				*step = 1.;
			return 0;
		}
		if (step != NULL)
			*step = major_tick / fabs (maxima - minima);
		return 1.5 + fabs (maxima - minima) / major_tick;
	}
}

static char *
gog_axis_get_marker (GogAxis *axis, unsigned i)
{
	if (axis->is_discrete) {
		if (axis->labels != NULL)
			return go_data_vector_get_str (axis->labels, i);
		return g_strdup_printf ("%d", i+1);
	} else {
		double major_tick = axis_get_entry (axis, AXIS_ELEM_MAJOR_TICK, NULL);
		double val = axis_get_entry (axis, AXIS_ELEM_MIN, NULL) +
			(((double)i) * major_tick);

		/* force display to 0 if it is within less than a  step */
		if (fabs (val) < major_tick / 10.0)
			val = 0.;

		return g_strdup_printf ("%g", val);
	}
}

/****************************************************************************/

typedef GogView		GogAxisView;
typedef GogViewClass	GogAxisViewClass;

#define GOG_AXIS_VIEW_TYPE	(gog_axis_view_get_type ())
#define GOG_AXIS_VIEW(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_AXIS_VIEW_TYPE, GogAxisView))
#define IS_GOG_AXIS_VIEW(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_AXIS_VIEW_TYPE))

static GogViewClass *aview_parent_klass;

static void
gog_axis_view_size_request (GogView *v, GogViewRequisition *req)
{
	GogAxis *axis = GOG_AXIS (v->model);
	GogViewRequisition tmp;
	gboolean const is_horiz = axis->type == GOG_AXIS_X;
	char *label;
	int i;
	double total = 0., max = 0., tick_major = 0., tick_minor = 0., pad = 0.;
	double line_width = gog_renderer_line_size (
		v->renderer, axis->base.style->line.width);

/* TODO : Think about rotating things or dropping markers periodically if
 * things are too big */
	if (axis->major_tick_labeled) {
		gog_renderer_push_style (v->renderer, axis->base.style);
		for (i = gog_axis_num_markers (axis, NULL) ; i-- > 0 ; ) {
			label = gog_axis_get_marker (axis, i);
			gog_renderer_measure_text (v->renderer, label, &tmp);
			g_free (label);
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
		gog_renderer_pop_style (v->renderer);
	}

	tmp = *req; /* store available */
	max += line_width;
	if (is_horiz) {
		if (axis->major_tick_labeled)
			pad = gog_renderer_pt2r_y (v->renderer, TICK_LABEL_PAD);
		if (axis->major.tick_out)
			tick_major = gog_renderer_pt2r_y (v->renderer,
				axis->major.size_pts);
		if (axis->minor.tick_out)
			tick_minor = gog_renderer_pt2r_y (v->renderer,
							  axis->minor.size_pts);
		req->w = total;
		req->h = max + MAX (tick_major, tick_minor) + pad;
	} else {
		if (axis->major_tick_labeled)
			pad = gog_renderer_pt2r_x (v->renderer, TICK_LABEL_PAD);
		if (axis->major.tick_out)
			tick_major = gog_renderer_pt2r_x (v->renderer,
				axis->major.size_pts);
		if (axis->minor.tick_out)
			tick_minor = gog_renderer_pt2r_x (v->renderer,
							  axis->minor.size_pts);
		req->h = total;
		req->w = max + MAX (tick_major, tick_minor) + pad;
	}

	gog_view_size_child_request (v, &tmp, req);
}

static void
gog_axis_view_render (GogView *v, GogViewAllocation const *bbox)
{
	GtkAnchorType anchor;
	GogViewAllocation const *area = &v->residual;
	GogViewAllocation label_pos, label_result;
	ArtVpath axis_path[3], major_path[3], minor_path[3];
	GogAxis *axis = GOG_AXIS (v->model);
	unsigned i, n;
	char *label;
	gboolean draw_major, draw_minor;
	double pre, post, bound, tick_len, label_pad, dir, center, start;
	double step, line_width = gog_renderer_line_size (
		v->renderer, axis->base.style->line.width) / 2;

	(aview_parent_klass->render) (v, bbox);

	g_return_if_fail (axis->pos != GOG_AXIS_IN_MIDDLE);

	gog_renderer_push_style (v->renderer, axis->base.style);
	axis_path[0].code = major_path[0].code = minor_path[0].code = ART_MOVETO;
	axis_path[1].code = major_path[1].code = minor_path[1].code = ART_LINETO;
	axis_path[2].code = major_path[2].code = minor_path[2].code = ART_END;

	draw_major = axis->major.tick_out || axis->major.tick_in;
	draw_minor = axis->minor.tick_out || axis->minor.tick_in;
	n = gog_axis_num_markers (axis, &step);
	if (axis->is_discrete)
		n++;
	switch (axis->type) {
	case GOG_AXIS_X:
		gog_chart_view_get_indents (v->parent, &pre, &post);
		switch (axis->pos) {
		case GOG_AXIS_AT_LOW:
			anchor = GTK_ANCHOR_N;
			dir = 1.;
			center = area->y + line_width;
			break;

		case GOG_AXIS_AT_HIGH:
			anchor = GTK_ANCHOR_S;
			dir = -1.;
			center = area->y + area->h - line_width;
			break;
		default : break;
		}
		step *= (area->w - pre - post);

		axis_path[0].y = axis_path[1].y = center;
		axis_path[0].x = start = area->x + pre;
		axis_path[1].x = area->x + area->w - post;

		/* set major tick height */
		tick_len = gog_renderer_pt2r_y (v->renderer, axis->major.size_pts);
		if (axis->major.tick_out)
			major_path[0].y = center + dir * (line_width + tick_len);
		else
			major_path[0].y = center - dir * line_width;
		if (axis->major.tick_in)
			major_path[1].y = center - dir * (line_width + tick_len);
		else
			major_path[1].y = center + dir * line_width;

		if (axis->major_tick_labeled) {
			label_pad = gog_renderer_pt2r_y (v->renderer, TICK_LABEL_PAD);
			label_pos.y = (axis->major.tick_out)
				? major_path[0].y + dir * label_pad
				: center + dir * (line_width + label_pad);
			label_pos.h  = area->h - line_width;
			label_pos.w  = -1;
		}

		for (bound = -1, i = 0 ; i < n ; i++) {
			if (i*step > (area->w - pre - post)) /* clip */
				continue;
			if (draw_major) {
				major_path[1].x = major_path[0].x = axis_path[0].x + i * step;
				gog_renderer_draw_path (v->renderer, major_path, NULL);
			}
			if (axis->major_tick_labeled) {
				label_pos.x = axis_path[0].x + i * step;
				if (label_pos.x < bound)
					continue;
				if (axis->is_discrete) {
					if (i == 0)
						continue;
					label_pos.x -= step/2;
					label = gog_axis_get_marker (axis, i-1);
				} else
					label = gog_axis_get_marker (axis, i);
				gog_renderer_draw_text (v->renderer, label,
					&label_pos, anchor, &label_result);
				g_free (label);
				bound = label_pos.x + label_result.w;
			}
		}
		break;

	case GOG_AXIS_Y:
		switch (axis->pos) {
		case GOG_AXIS_AT_LOW:
			anchor = GTK_ANCHOR_E;
			dir = -1.;
			center = area->x + area->w - line_width;
			break;
		case GOG_AXIS_AT_HIGH:
			anchor = GTK_ANCHOR_W;
			dir = 1.;
			center = area->x + line_width;
			break;
		default : break;
		}
		step *= area->h;

		axis_path[0].x = axis_path[1].x = center;
		axis_path[0].y = start = area->y + area->h;
		axis_path[1].y = area->y;

		/* set major tick width */
		tick_len = gog_renderer_pt2r_x (v->renderer, axis->major.size_pts);
		if (axis->major.tick_out)
			major_path[0].x = center + dir * (line_width + tick_len);
		else
			major_path[0].x = center - dir * line_width;
		if (axis->major.tick_in)
			major_path[1].x = center - dir * (line_width + tick_len);
		else
			major_path[1].x = center + dir * line_width;

		if (axis->major_tick_labeled) {
			label_pad = gog_renderer_pt2r_x (v->renderer, TICK_LABEL_PAD);
			label_pos.x = (axis->major.tick_out)
				? major_path[0].x + dir * label_pad
				: center + dir * (line_width + label_pad);
			label_pos.w  = area->w - line_width;
			label_pos.h  = -1;
		}

		for (bound = DBL_MAX, i = 0 ; i < n ; i++) {
			if (i*step > area->h) /* clip */
				continue;
			if (draw_major) {
				major_path[1].y = major_path[0].y = axis_path[0].y - i * step;
				gog_renderer_draw_path (v->renderer, major_path, NULL);
			}
			if (axis->major_tick_labeled) {
				label_pos.y = axis_path[0].y - i * step;
				if (label_pos.y > bound)
					continue;
				if (axis->is_discrete) {
					if (i == 0)
						continue;
					label_pos.y += step/2;
					label = gog_axis_get_marker (axis, i-1);
				} else
					label = gog_axis_get_marker (axis, i);

				gog_renderer_draw_text (v->renderer, label,
					&label_pos, anchor, &label_result);
				g_free (label);
				bound = label_pos.y - label_result.h;
			}
		}
		break;
	default :
		break;
	}

	if (line_width > 0)
		gog_renderer_draw_path (v->renderer, axis_path, NULL);
	gog_renderer_pop_style (v->renderer);
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
