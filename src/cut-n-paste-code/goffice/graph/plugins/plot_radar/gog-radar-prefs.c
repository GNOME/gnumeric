/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-radar-prefs.c
 *
 * Copyright (C) 2003 Michael Devine (mdevine@cs.stanford.edu)
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
#include "gog-radar.h"
#include <src/plugin.h>
#include <src/gui-util.h>

#include <glade/glade-xml.h>
#include <gtk/gtkspinbutton.h>

GtkWidget *gog_radar_plot_pref (GogRadarPlot *plot, GnmCmdContext *cc);

static void
cb_rotation_changed (GtkAdjustment *adj, GObject *radar)
{
	g_object_set (radar, "initial_angle", adj->value, NULL);
}

static void
gog_radar_plot_pref_signal_connect (GogRadarPlot *radar, GladeXML *gui)
{
	GtkWidget *w;
	
	w = glade_xml_get_widget (gui, "rotation_spinner");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (w), radar->initial_angle);
	g_signal_connect (G_OBJECT (gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (w))),
		"value_changed",
		G_CALLBACK (cb_rotation_changed), radar);
}

GtkWidget *
gog_radar_plot_pref (GogRadarPlot *radar, GnmCmdContext *cc)
{
	GtkWidget  *w;
	char const *dir = gnm_plugin_get_dir_name (
		plugins_get_plugin_by_id ("GOffice_plot_radar"));
	char	 *path = g_build_filename (dir, "gog-radar-prefs.glade", NULL);
	GladeXML *gui = gnm_glade_xml_new (cc, path, "gog_radar_prefs", NULL);

	g_free (path);
        if (gui == NULL)
                return NULL;

	gog_radar_plot_pref_signal_connect (radar, gui);

	w = glade_xml_get_widget (gui, "gog_radar_prefs");
	g_object_set_data_full (G_OBJECT (w),
		"state", gui, (GDestroyNotify)g_object_unref);

	return w;
}
