/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * goffice.c : a bogus little init file to pull all the parts together
 *
 * Copyright (C) 2003-2004 Jody Goldberg (jody@gnome.org)
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

#include <goffice/goffice-config.h>
#include <goffice/goffice.h>
#include <goffice/goffice-priv.h>
#include <goffice/graph/gog-series.h>
#include <goffice/graph/gog-plot.h>
#include <goffice/graph/gog-plot-engine.h>
#include <goffice/graph/gog-chart.h>
#include <goffice/graph/gog-graph.h>
#include <goffice/graph/gog-axis.h>
#include <goffice/graph/gog-legend.h>
#include <goffice/graph/gog-label.h>
#include <goffice/graph/gog-grid.h>
#include <goffice/graph/gog-grid-line.h>
#include <goffice/graph/gog-theme.h>
#include <goffice/graph/gog-error-bar.h>
#include <goffice/graph/go-data-simple.h>
#include <goffice/utils/go-font.h>
#include <goffice/utils/go-math.h>
#include <goffice/app/go-plugin-service.h>
#include <gsf/gsf-utils.h>

#warning remove when we get a configure.in
#include <src/gnumeric-paths.h>

#include <libintl.h>

int goffice_graph_debug_level = 0;

static char const *libgoffice_data_dir   = GNUMERIC_DATADIR;
static char const *libgoffice_icon_dir   = GNUMERIC_ICONDIR;
static char const *libgoffice_locale_dir = GNUMERIC_LOCALEDIR;
/* static char const *libgoffice_lib_dir    = GNUMERIC_LIBDIR; */

char const *
go_sys_data_dir ()
{
	return libgoffice_data_dir;
}
char const *
go_sys_icon_dir ()
{
	return libgoffice_data_dir;
}

void
libgoffice_init (void)
{
	static gboolean initialized = FALSE;

	if (initialized)
		return;

#ifdef G_OS_WIN32
{
	char *dir = g_win32_get_package_installation_directory (NULL, NULL);
	libgoffice_data_dir = g_build_filename (dir,
		"share", "goffice", LIBGOFFICE_VERSION, NULL);
	libgoffice_icon_dir = g_build_filename (dir,
		"share", "pixmaps", "gnumeric", NULL);
	libgoffice_locale_dir = g_build_filename (dir,
		"share", "locale", NULL);
	libgoffice_lib_dir = g_build_filename (dir,
		"lib", "goffice", LIBGOFFICE_VERSION, NULL);
}
#endif

	bindtextdomain ("libgoffice", libgoffice_locale_dir);
	go_fonts_init ();
	go_math_init ();
	gsf_init ();

	/* keep trigger happy linkers from leaving things out */
	plugin_services_init ();
	gog_plugin_services_init ();
	(void) GOG_GRAPH_TYPE;
	(void) GOG_CHART_TYPE;
	(void) GOG_PLOT_TYPE;
	(void) GOG_SERIES_TYPE;
	(void) GOG_SERIES_ELEMENT_TYPE;
	(void) GOG_LEGEND_TYPE;
	(void) GOG_AXIS_TYPE;
	(void) GOG_LABEL_TYPE;
	(void) GOG_GRID_TYPE;
	(void) GOG_GRID_LINE_TYPE;
	(void) GOG_ERROR_BAR_TYPE;
	(void) GO_DATA_SCALAR_VAL_TYPE;
	(void) GO_DATA_SCALAR_STR_TYPE;
	gog_themes_init	();
}

void
libgoffice_shutdown (void)
{
	gog_themes_shutdown ();
	go_fonts_shutdown ();
	gog_plugin_services_shutdown ();
}
