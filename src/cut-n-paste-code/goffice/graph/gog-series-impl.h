/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-series-impl.h :  
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

#ifndef GO_SERIES_IMPL_H
#define GO_SERIES_IMPL_H

#include <goffice/graph/goffice-graph.h>
#include <goffice/graph/gog-styled-object.h>
#include <goffice/graph/gog-series.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
	GOG_SERIES_REQUIRED,  /* it must be there */
	GOG_SERIES_SUGGESTED, /* allocator will fill it in, but use need not */
	GOG_SERIES_OPTIONAL
} GogSeriesPriority;

struct _GogSeriesDimDesc {
	char const *name;
	GogSeriesPriority	priority;
	gboolean		is_shared;
	GogDimType		val_type;
	GogAxisType		axis_requirement;
	GogMSDimType		ms_type;
};

struct _GogSeriesDesc {
	unsigned num_dim;
	GogSeriesDimDesc const *dim;
};

typedef struct {
	GOData	  *data;
	GogSeries *series;
	int	   dim_i;
	gulong	   handler;
} GogDim;

struct _GogSeries {
	GogStyledObject base;

	unsigned index;
	unsigned is_valid : 1;
	unsigned needs_recalc : 1;

	GogPlot	  *plot;
	GogDim	  *values;

	GogSeriesElementStyleList *element_style_overrides;
};

typedef struct {
	GogObjectClass base;

	/* Virtuals */
	void (*dim_changed) (GogSeries *series, int dim_i);
} GogSeriesClass;

#define GOG_SERIES_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST ((k), GOG_SERIES_TYPE, GogSeriesClass))
#define IS_GOG_SERIES_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GOG_SERIES_TYPE))
#define GOG_SERIES_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GOG_SERIES_TYPE, GogSeriesClass))

/* protected */
// unsigned go_plot_series_num_
void gog_series_check_validity   (GogSeries *series);
void gog_series_set_dim_internal (GogSeries *series,
				  int dim_i, GOData *val, GogGraph *graph);

G_END_DECLS

#endif /* GO_SERIES_IMPL_H */
