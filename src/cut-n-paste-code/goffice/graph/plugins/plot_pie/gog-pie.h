/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-pie.h
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

#ifndef GOG_PIE_PLOT_H
#define GOG_PIE_PLOT_H

#include <goffice/graph/gog-plot-impl.h>
#include <goffice/graph/gog-series-impl.h>

G_BEGIN_DECLS

typedef struct {
	GogPlot	base;

	int	 initial_angle;	 /* degrees counterclockwise from 3 o'clock */
	float	 default_separation;	/* as a percentage of the radius */
	gboolean vary_style_by_element;
	gboolean in_3d;
} GogPiePlot;

#define GOG_PIE_PLOT_TYPE	(gog_pie_plot_get_type ())
#define GOG_PIE_PLOT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_PIE_PLOT_TYPE, GogPiePlot))
#define GOG_IS_PIE_PLOT(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_PIE_PLOT_TYPE))

GType gog_pie_plot_get_type (void);

typedef struct {
	GogSeries base;

	float	 initial_angle;	/* degrees counterclockwise from 3 o'clock */
	float	 separation;	/* as a percentage of the radius */

	unsigned num_elements;
	double   total;
	float	*extensions;
	float    max_extension; /* as a multiple of radius */
} GogPieSeries;

#define GOG_PIE_SERIES_TYPE	(gog_pie_series_get_type ())
#define GOG_PIE_SERIES(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_PIE_SERIES_TYPE, GogPieSeries))
#define GOG_IS_PIE_SERIES(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_PIE_SERIES_TYPE))

GType gog_pie_series_get_type (void);

G_END_DECLS

#endif /* GOG_PIE_SERIES_H */
