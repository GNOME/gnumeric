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
#include <goffice/graph/gog-theme.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/go-data.h>

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>
#include <src/gui-util.h>
#include <gtk/gtktable.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkhseparator.h>

#include <string.h>

static GObjectClass *parent_klass;

static void
gog_series_finalize (GObject *obj)
{
	GogSeries *series = GOG_SERIES (obj);

	if (series->values != NULL) {
		gog_dataset_finalize (GOG_DATASET (obj));
		g_free (series->values - 1); /* it was aliased */
		series->values = NULL;
	}

	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (obj);
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
		gog_data_allocator_editor (dalloc, set, -1, TRUE),
		N_("Name"), TRUE, FALSE);

	/* first the unshared entries */
	for (i = 0; i < desc->num_dim; i++)
		if (!desc->dim[i].is_shared)
			row = make_dim_editor (table, row,
				gog_data_allocator_editor (dalloc, set, i, FALSE),
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
				gog_data_allocator_editor (dalloc, set, i, FALSE),
				desc->dim[i].name, desc->dim[i].priority, TRUE);

	gtk_widget_show_all (GTK_WIDGET (table));

	w = gtk_notebook_new ();
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (w), GTK_POS_LEFT);
	gtk_notebook_prepend_page (GTK_NOTEBOOK (w), GTK_WIDGET (table),
		gtk_label_new (_("Data")));
	gtk_notebook_prepend_page (GTK_NOTEBOOK (w),
		gog_style_editor (gobj, cc, GOG_STYLE_OUTLINE | GOG_STYLE_FILL),
		gtk_label_new (_("Style")));
	return w;
}

static void
gog_series_class_init (GogSeriesClass *klass)
{
	GObjectClass *gobject_klass = (GObjectClass *) klass;
	GogObjectClass *gog_klass = (GogObjectClass *) klass;

	parent_klass = g_type_class_peek_parent (klass);
	gobject_klass->finalize		= gog_series_finalize;
	gog_klass->editor		= gog_series_editor;
}

static void
gog_series_init (GogSeries *series)
{
	/* series do not have views, so just forward signals from the plot */
	GOG_OBJECT (series)->use_parent_as_proxy = TRUE;

	series->is_valid = FALSE;
	series->plot = NULL;
	series->values = NULL;
}

static void
gog_series_dataset_dims (GogDataset const *set, int *first, int *last)
{
	GogSeries const *series = GOG_SERIES (set);
	*first = -1;
	if (series->plot == NULL || series->values == NULL)
		*last = -2;
	else
		*last = (series->plot->desc.series.num_dim - 1);
}

static GogDatasetElement *
gog_series_dataset_get_elem (GogDataset const *set, int dim_i)
{
	GogSeries const *series = GOG_SERIES (set);
	g_return_val_if_fail ((int)series->plot->desc.series.num_dim > dim_i, NULL);
	g_return_val_if_fail (dim_i >= -1, NULL);
	return series->values + dim_i;
}

static void
gog_series_dataset_set_dim (GogDataset *set, int dim_i,
			    GOData *val, GError **err)
{
	GogSeriesDesc const *desc;
	GogSeries *series = GOG_SERIES (set);
	GogGraph *graph = gog_object_get_graph (GOG_OBJECT (series));

	g_return_if_fail (GOG_PLOT (series->plot) != NULL);

	if (dim_i < 0) {
		char *name = NULL;
		if (NULL != series->values[-1].data)
			name = g_strdup (go_data_scalar_get_str (
				GO_DATA_SCALAR (series->values[-1].data)));
		gog_object_set_name (GOG_OBJECT (series), name, err);
		return;
	}

	gog_series_check_validity (series);

	/* clone shared dimensions into other series in the plot, and
	 * invalidate if necessary */
	desc = &series->plot->desc.series;
	if (desc->dim[dim_i].is_shared) {
		GSList *ptr = series->plot->series;

		val = series->values[dim_i].data;
		for (; ptr != NULL ; ptr = ptr->next) {
			gog_dataset_set_dim_internal (GOG_DATASET (ptr->data),
				dim_i, val, graph);
			gog_series_check_validity (GOG_SERIES (ptr->data));
		}
	}
}

static void
gog_series_dataset_dim_changed (GogDataset *set, int dim_i)
{
	GogSeries *series = GOG_SERIES (set);
	if (dim_i >= 0) {
		GogSeriesClass	*klass = GOG_SERIES_GET_CLASS (set);

		if (!series->needs_recalc) {
			series->needs_recalc = TRUE;
			gog_object_emit_changed (GOG_OBJECT (set), FALSE);
		}
		if (klass->dim_changed != NULL)
			(klass->dim_changed) (GOG_SERIES (set), dim_i);

		gog_object_request_update (GOG_OBJECT (set));
	} else {
		GOData *name_src = series->values[-1].data;
		char const *name = (name_src != NULL) 
			? go_data_scalar_get_str (GO_DATA_SCALAR (name_src)) : NULL;
		gog_object_set_name (GOG_OBJECT (set), g_strdup (name), NULL);
	}
}

static void
gog_series_dataset_init (GogDatasetClass *iface)
{
	iface->get_elem	   = gog_series_dataset_get_elem;
	iface->set_dim	   = gog_series_dataset_set_dim;
	iface->dims	   = gog_series_dataset_dims;
	iface->dim_changed = gog_series_dataset_dim_changed;
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
		series->base.style, GOG_OBJECT (series), ind);
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
gog_series_set_name (GogSeries *series, GODataScalar *name_src, GError **err)
{
	gog_dataset_set_dim (GOG_DATASET (series),
		-1, GO_DATA (name_src), NULL);
}

/**
 * gog_series_set_dim :
 * @series : #GogSeries
 * @dim_i :
 * @val   : #GOData
 * @err : optional #Gerror pointer
 *
 * Absorbs a ref to @val
 **/
void
gog_series_set_dim (GogSeries *series, int dim_i, GOData *val, GError **err)
{
	gog_dataset_set_dim (GOG_DATASET (series), dim_i, val, err);
}
