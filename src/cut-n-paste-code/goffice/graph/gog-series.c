/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-graph-data.c :
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
#include <goffice/graph/gog-series-impl.h>
#include <goffice/graph/gog-data-allocator.h>
#include <goffice/graph/gog-plot-impl.h>
#include <goffice/graph/gog-graph.h>
#include <goffice/graph/gog-theme.h>
#include <goffice/graph/go-data.h>

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>
#include <src/gui-util.h>
#include <gtk/gtktable.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkhseparator.h>

static GObjectClass *parent_klass;

static void
gog_series_finalize (GObject *obj)
{
	GogGraph	*graph = gog_object_get_graph (GOG_OBJECT (obj));
	GogSeries *series = GOG_SERIES (obj);

	if (series->values != NULL) {
		int i = series->plot->desc.series.num_dim;
		while (i-- >= 0) /* including the name */
			gog_series_set_dim_internal (series, i, NULL, graph);
		g_free (series->values - 1); /* it was aliased */
		series->values = NULL;
	}

	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (obj);
}

static char const *
gog_series_type_name (GogObject const *gobj)
{
	return "Series";
}

static unsigned
make_dim_editor (GtkTable *table, unsigned row, GtkWidget *editor,
		 char const *name, GogSeriesPriority priority, gboolean is_shared)
{
	char *txt = g_strdup_printf (
		((priority != GOG_SERIES_REQUIRED) ? "(_%s):" : "_%s:"), _(name));
	GtkWidget *label = gtk_label_new_with_mnemonic (txt);
	g_free (txt);

	gtk_table_attach (table, label,
		0, 1, row, row+1, GTK_FILL, 0, 5, 3);
	gtk_table_attach (table, editor,
		1, 2, row, row+1, GTK_FILL | GTK_EXPAND, 0, 5, 3);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), editor);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);

	gnm_setup_label_atk (GTK_LABEL (label), editor);

	return row + 1;
}

static gpointer
gog_series_editor (GogObject *gobj,
		   GogDataAllocator *dalloc,
		   CommandContext *cc)
{
	GtkWidget *w;
	GtkTable  *table;
	unsigned i, row = 0;
	gboolean has_shared = FALSE;
	GogSeries *series = GOG_SERIES (gobj);
	GogDataset *set = GOG_DATASET (gobj);
	GogSeriesDesc const *desc;

	g_return_val_if_fail (series->plot != NULL, NULL);

	/* Are there any shared dimensions */
	desc = &series->plot->desc.series;
	for (i = 0; i < desc->num_dim; i++)
		if (desc->dim[i].is_shared) {
			has_shared = TRUE;
			break;
		}

	w = gtk_table_new (desc->num_dim + (has_shared ? 2 : 1), 2, FALSE);
	table = GTK_TABLE (w);

	row = make_dim_editor (table, row,
		gog_data_allocator_editor (dalloc, set, -1),
		N_("Name"), TRUE, FALSE);

	/* first the unshared entries */
	for (i = 0; i < desc->num_dim; i++)
		if (!desc->dim[i].is_shared)
			row = make_dim_editor (table, row,
				gog_data_allocator_editor (dalloc, set, i),
				desc->dim[i].name, desc->dim[i].priority, FALSE);

	if (has_shared) {
		gtk_table_attach (table, gtk_hseparator_new (),
			0, 2, row, row+1, GTK_FILL, 0, 5, 3);
		row++;
	}

	/* then the shared entries */
	for (i = 0; i < desc->num_dim; i++)
		if (desc->dim[i].is_shared)
			row = make_dim_editor (table, row,
				gog_data_allocator_editor (dalloc, set, i),
				desc->dim[i].name, desc->dim[i].priority, TRUE);

	gtk_widget_show_all (GTK_WIDGET (table));
	return table;
}

static void
gog_series_parent_changed (GogObject *child, gboolean was_set)
{
	GogGraph *graph;
	GogSeries *series = GOG_SERIES (child);
	GOData *dat;
	int i;

	if (series->values == NULL) /* during construction */
		return;

	graph = gog_object_get_graph (child);
	for (i = series->plot->desc.series.num_dim; i-- >= 0;) { /* incl name */
		dat = series->values[i].data;
		if (dat == NULL)
			continue;
		if (was_set) {
			series->values[i].data = NULL; /* disable the short circuit */
			gog_series_set_dim_internal (series, i, dat, graph);
			g_object_unref (dat);
		} else {
			g_object_ref (dat);
			gog_series_set_dim_internal (series, i, NULL, graph);
			series->values[i].data = dat;
		}
	}
	if (was_set)
		gog_object_request_update (child);
}

static void
gog_series_class_init (GogSeriesClass *klass)
{
	GObjectClass *gobject_klass = (GObjectClass *) klass;
	GogObjectClass *gog_klass = (GogObjectClass *) klass;

	parent_klass = g_type_class_peek_parent (klass);
	gobject_klass->finalize		= gog_series_finalize;
	gog_klass->parent_changed	= gog_series_parent_changed;
	gog_klass->type_name		= gog_series_type_name;
	gog_klass->editor		= gog_series_editor;
}

static void
gog_series_init (GogSeries *series)
{
	series->is_valid = FALSE;
	series->plot = NULL;
	series->values = NULL;
}

static GOData *
gog_series_get_dim (GogDataset const *set, int dim_i)
{
	GogSeries const *series = GOG_SERIES (set);
	g_return_val_if_fail ((int)series->plot->desc.series.num_dim > dim_i, NULL);
	g_return_val_if_fail (dim_i >= -1, NULL);
	return series->values[dim_i].data;
}

static void
gog_series_set_dim_dataset (GogDataset *set, int dim_i,
			    GOData *val, GError **err)
{
	GogSeriesDesc const *desc;
	GogSeries *series = GOG_SERIES (set);
	GogGraph *graph;

	g_return_if_fail (GOG_PLOT (series->plot) != NULL);
	g_return_if_fail (val == NULL || GO_DATA (val) != NULL);

	if (val == gog_series_get_dim (set, dim_i))
		return;

	if (dim_i < 0) {
		gog_series_set_name (series, GO_DATA_SCALAR (val), err);
		return;
	}

	graph = gog_object_get_graph (GOG_OBJECT (series));
	gog_series_set_dim_internal (series, dim_i, val, graph);

	/* absorb ref to orig, simplifies life cycle easier for new GODatas */
	if (val != NULL)
		g_object_unref (val);

	/* clone shared dimensions into other series in the plot, and
	 * invalidate if necessary */
	desc = &series->plot->desc.series;
	if (desc->dim[dim_i].is_shared) {
		gboolean mark_invalid = (val == NULL && desc->dim[dim_i].priority == GOG_SERIES_REQUIRED);
		GSList *ptr = series->plot->series;

		val = series->values[dim_i].data;
		for (; ptr != NULL ; ptr = ptr->next) {
			GogSeries *dst = ptr->data;
			gog_series_set_dim_internal (dst, dim_i, val, graph);
			if (mark_invalid)
				dst->is_valid = FALSE;
		}
	}

	gog_series_check_validity (series);
}

void
gog_series_set_dim (GogSeries *series, int dim_i, GOData *val, GError **err)
{
	gog_dataset_set_dim (GOG_DATASET (series), dim_i, val, err);
}

static void
gog_series_dataset_init (GogDatasetClass *iface)
{
	iface->get_dim = gog_series_get_dim;
	iface->set_dim = gog_series_set_dim_dataset;
}

GSF_CLASS_FULL (GogSeries, gog_series,
		gog_series_class_init, gog_series_init,
		GOG_STYLED_OBJECT_TYPE, 0,
		GSF_INTERFACE (gog_series_dataset_init, GOG_DATASET_TYPE))

/**
 * gog_series_is_valid :
 * @series : #GogSeries
 *
 * Returns the current cached validity.  Does not recheck
 **/
gboolean
gog_series_is_valid (GogSeries const *series)
{
	g_return_val_if_fail (GOG_SERIES (series) != NULL, FALSE);
	return series->is_valid;
}

/**
 * gog_series_get_name :
 * @series : #GogSeries
 *
 * Returns the _src_ of the name associated with the series
 * NOTE : this is _NOT_ the actual name
 * no references are added on the result.
 **/
GODataScalar *
gog_series_get_name (GogSeries const *series)
{
	g_return_val_if_fail (GOG_SERIES (series) != NULL, NULL);
	return GO_DATA_SCALAR (series->values[-1].data);
}

/**
 * gog_series_set_name :
 * @series : #GogSeries
 * @name_src : #GOData
 * @err : #Gerror
 *
 * Absorbs a ref to @name_src
 **/
void
gog_series_set_name (GogSeries *series,
		     GODataScalar *name_src, GError **err)
{
	char *name = NULL;

	g_return_if_fail (GOG_SERIES (series) != NULL);

	gog_series_set_dim_internal (series, -1, GO_DATA (name_src),
				     gog_object_get_graph (GOG_OBJECT (series)));

	/* absorb ref to orig, simplifies life cycle easier for new GODatas */
	if (name_src != NULL)
		g_object_unref (name_src);

	if (NULL != series->values[-1].data)
		name = g_strdup (go_data_scalar_get_str (
			GO_DATA_SCALAR (series->values[-1].data)));
	gog_object_set_name (GOG_OBJECT (series), name, err);
}

/**
 * gog_series_check_validity :
 * @series : #GogSeries
 *
 * Updates the is_valid flag for a series.
 * This is an internal utility that should not really be necessary for general
 * usage.
 **/
void
gog_series_check_validity (GogSeries *series)
{
	unsigned i;
	GogSeriesDesc const *desc;

	g_return_if_fail (GOG_SERIES (series) != NULL);
	g_return_if_fail (GOG_PLOT (series->plot) != NULL);

	desc = &series->plot->desc.series;
	for (i = series->plot->desc.series.num_dim; i-- > 0; )
		if (series->values[i].data == NULL &&
		    desc->dim[i].priority == GOG_SERIES_REQUIRED) {
			series->is_valid = FALSE;
			return;
		}
	series->is_valid = TRUE;
}

static void
cb_series_name_changed (GODataScalar *name_src, GogDim *dimension)
{
	gog_object_set_name (GOG_OBJECT (dimension->series),
		g_strdup (go_data_scalar_get_str (name_src)), NULL);
}

static void
cb_series_dim_changed (GOData *dat, GogDim *dimension)
{
	GogSeriesClass *klass;

	if (!dimension->series->needs_recalc) {
		dimension->series->needs_recalc = TRUE;
		gog_object_emit_changed (GOG_OBJECT (dimension->series), FALSE);
	}
	klass = GOG_SERIES_GET_CLASS (dimension->series);
	if (klass->dim_changed != NULL)
		(klass->dim_changed) (dimension->series, dimension->dim_i);

	gog_object_request_update (GOG_OBJECT (dimension->series));
}

/**
 * gog_series_set_dim_internal :
 * 
 * and internal routine to handle signal setup and teardown
 **/
void
gog_series_set_dim_internal (GogSeries *series,
			     int dim_i, GOData *val,
			     GogGraph *graph)
{
	GOData *old = series->values[dim_i].data;

	if (val == old)
		return;

	if (graph != NULL) {
		GCallback func = (dim_i < 0)
			? G_CALLBACK (cb_series_name_changed)
			: G_CALLBACK (cb_series_dim_changed);

		if (val != NULL)
			val = gog_graph_ref_data (graph, val);
		if (val == old)
			return;
		if (old != NULL) {
			g_signal_handler_disconnect (G_OBJECT (old),
				series->values[dim_i].handler);
			gog_graph_unref_data (graph, old);
		}
		if (val != NULL)
			series->values[dim_i].handler = g_signal_connect (
				G_OBJECT (val), "changed",
				func, series->values + dim_i);
	} else {
		if (val != NULL)
			g_object_ref (val);
		if (old != NULL)
			g_object_unref (old);
	}
	series->values[dim_i].data   = val;
	series->values[dim_i].series = series;
	series->values[dim_i].dim_i  = dim_i;
	gog_object_request_update (GOG_OBJECT (series));
}

/**
 * gog_series_set_index :
 * @series : #GogSeries
 * @index :
 * @is_manual : 
 *
 * if @index >= 0 attempt to assign the new index.  Auto
 * indicies (@is_manual == FALSE) will not override the current
 * index if it is manual.  An @index < 0, will reset the index to
 * automatic and potentially queue a revaluation of the parent
 * chart's cardinality.
 **/
void
gog_series_set_index (GogSeries *series, int ind, gboolean is_manual)
{
	g_return_if_fail (GOG_SERIES (series) != NULL);

	if (ind < 0) {
		if (series->manual_index && series->plot != NULL)
			gog_plot_request_cardinality_update (series->plot);
		series->manual_index = FALSE;
		return;
	}

	if (is_manual)
		series->manual_index = TRUE;
	else if (series->manual_index)
		return;

	if (series->index == (unsigned)ind)
		return;
	series->index = ind;

	gog_theme_init_style (gog_object_get_theme (GOG_OBJECT (series)),
		series->base.style, G_OBJECT_GET_CLASS (series), ind);
}
