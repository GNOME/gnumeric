/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-barcol.h
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

#ifndef GOG_BARCOL_H
#define GOG_BARCOL_H

#include <goffice/graph/gog-plot-impl.h>
#include <goffice/graph/gog-series-impl.h>

G_BEGIN_DECLS

#define GOG_BARCOL_PLOT_TYPE	(gog_barcol_plot_get_type ())
#define GOG_BARCOL_PLOT(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_BARCOL_PLOT_TYPE, GogBarColPlot))
#define GOG_IS_PLOT_BARCOL(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_BARCOL_PLOT_TYPE))

typedef enum {
	GOG_BARCOL_NORMAL,
	GOG_BARCOL_STACKED,
	GOG_BARCOL_AS_PERCENTAGE
} GogBarColType;

typedef struct {
	GogPlot	base;
	GogBarColType type;
	gboolean horizontal;
	int	 overlap_percentage;
	int	 gap_percentage;

	/* cached content */
	unsigned num_series, num_elements;
	double   minimum, maximum; /* meaning varies depending on type */
} GogBarColPlot;

GType gog_barcol_plot_get_type (void);

typedef struct {
	GogSeries base;
	unsigned num_elements;
} GogBarColSeries;

#define GOG_BARCOL_SERIES_TYPE	(gog_barcol_series_get_type ())
#define GOG_BARCOL_SERIES(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_BARCOL_SERIES_TYPE, GogBarColSeries))
#define GOG_IS_BARCOL_SERIES(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_BARCOL_SERIES_TYPE))
GType gog_barcol_series_get_type (void);

G_END_DECLS

#endif /* GOG_BARCOL_H */
