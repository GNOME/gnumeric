/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-plot-impl.h : implementation details for the abstract 'plot' interface
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

#ifndef GOG_PLOT_IMPL_H
#define GOG_PLOT_IMPL_H

#include <goffice/graph/goffice-graph.h>
#include <goffice/graph/gog-plot.h>
#include <goffice/graph/gog-series-impl.h>
#include <goffice/graph/gog-object.h>
#include <goffice/graph/gog-view.h>
#include <glib-object.h>

G_BEGIN_DECLS

struct _GogPlotDesc {
	unsigned  num_series_min, num_series_max;
	unsigned  num_axis;
	GogSeriesDesc series;
};

struct _GogPlot {
	GogObject	 base;
	GogChart	*chart;		/* potentially NULL */

	GSList		*series;
	unsigned	 cardinality;
	gboolean	 cardinality_valid;
	unsigned	 index_num;
	gboolean	 vary_style_by_element;

	GogAxis		*axis[GOG_AXIS_TYPES];

	/* Usually a copy from the class but its here to allow a GogPlotType to
	 * override a things without requiring a completely new class */
	GogPlotDesc	desc;
};

typedef struct {
	GogObjectClass base;

	GogPlotDesc	desc;
	GType		series_type;

	/* Virtuals */
	unsigned   (*cardinality)  (GogPlot *plot);
	gboolean   (*foreach_elem) (GogPlot *plot,
				    GogEnumFunc handler, gpointer data);

	GogAxisSet (*axis_set_pref)     (GogPlot const *plot);
	gboolean   (*axis_set_is_valid) (GogPlot const *plot, GogAxisSet type);
	gboolean   (*axis_set_assign)   (GogPlot *plot, GogAxisSet type);
	GOData	  *(*axis_bounds) 	(GogPlot *plot, GogAxisType axis,
					 double *min, double *max,
					 double *logical_min, double *logical_max,
					 gboolean *is_discrete);

	gboolean   (*supports_vary_style_by_element) (GogPlot const *plot);
} GogPlotClass;

#define GOG_PLOT_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST ((k), GOG_PLOT_TYPE, GogPlotClass))
#define IS_GOG_PLOT_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), GOG_PLOT_TYPE))
#define GOG_PLOT_ITEM_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GOG_PLOT_TYPE, GogPlotClass))

/* protected */

/*****************************************************************************/

#define GOG_PLOT_VIEW_TYPE	(gog_plot_view_get_type ())
#define GOG_PLOT_VIEW(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_PLOT_VIEW_TYPE, GogPlotView))
#define IS_GOG_PLOT_VIEW(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_PLOT_VIEW_TYPE))

typedef GogView		GogPlotView;
typedef GogViewClass	GogPlotViewClass;
GType gog_plot_view_get_type (void);

G_END_DECLS

#endif /* GOG_PLOT_GROUP_IMPL_H */
