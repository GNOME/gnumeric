/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * lib.c
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
#include <goffice/goffice.h>
#include <goffice/graph/gog-series.h>
#include <goffice/graph/gog-plot.h>
#include <goffice/graph/gog-plot-engine.h>
#include <goffice/graph/gog-chart.h>
#include <goffice/graph/gog-graph.h>
#include <goffice/graph/gog-axis.h>
#include <goffice/graph/gog-legend.h>
#include <goffice/graph/gog-label.h>
#include <goffice/graph/gog-theme.h>
#include <goffice/utils/go-font.h>

int goffice_graph_debug_level = 0;

void
libgoffice_init (void)
{
	go_font_init ();

	/* keep trigger happy linkers from leaving things out */
	gog_plugin_services_init ();
	(void) GOG_GRAPH_TYPE;
	(void) GOG_CHART_TYPE;
	(void) GOG_PLOT_TYPE;
	(void) GOG_SERIES_TYPE;
	(void) GOG_LEGEND_TYPE;
	(void) GOG_AXIS_TYPE;
	(void) GOG_LABEL_TYPE;
	gog_themes_init	();
}

void
libgoffice_shutdown (void)
{
	gog_themes_shutdown ();
	go_font_shutdown ();
}
