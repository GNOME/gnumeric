/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * about.c: Shows the contributors to Gnumeric.
 *
 * Author:
 *  Jody Goldberg <jody@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <libfoocanvas/foo-canvas.h>
#include <goffice/graph/gog-object.h>
#include <goffice/graph/gog-styled-object.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-graph.h>
#include <goffice/graph/gog-chart.h>
#include <goffice/graph/gog-plot.h>
#include <goffice/graph/gog-series.h>
#include <goffice/graph/gog-data-set.h>
#include <goffice/graph/gog-control-foocanvas.h>
#include <goffice/graph/go-data-simple.h>
#include <goffice/utils/go-color.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkdialog.h>

#define ABOUT_KEY          "about-dialog"

typedef enum {
	GNM_CORE		= 1 << 0,	/* All round hacking */
	GNM_FEATURE_HACKER	= 1 << 1,	/* Implement specific feature */
	GNM_ANALYTICS		= 1 << 2,
	GNM_IMPORT_EXPORT	= 1 << 3,
	GNM_SCRIPTING		= 1 << 4,
	GNM_GUI			= 1 << 5,
	GNM_USABILITY		= 1 << 6,
	GNM_DOCUMENTATION	= 1 << 7,
	GNM_TRANSLATION		= 1 << 8,
	GNM_QA			= 1 << 9,
	GNM_ART			= 1 << 10,
	GNM_PACKAGING		= 1 << 11
} ContribTypes;
#define GNM_ABOUT_NUM_TYPES	       12
static char const *about_types[GNM_ABOUT_NUM_TYPES] = {
	N_("Core"),
	N_("Features"),
	N_("Analytics"),
	N_("Import Export"),
	N_("Scripting"),
	N_("UI"),
	N_("Usability"),
	N_("Documentation"),
	N_("Translation"),
	N_("QA"),
	N_("Art"),
	N_("Packaging")
};

static struct {
	char const *name;
	unsigned contributions;
	char const *details; /* optionally NULL */
} const contributors[] = {
	{ "Sean Atkinson",		GNM_ANALYTICS | GNM_IMPORT_EXPORT,
		N_("Functions and X-Base importing.") },
	{ "Michel Berkelaar",		GNM_ANALYTICS,
		N_("Simplex algorithm for Solver (LP Solve).") },
	{ "Grandma Chema Celorio",	GNM_FEATURE_HACKER|GNM_USABILITY|GNM_QA,
		N_("Quality Assurance and sheet copy.") },
	{ "Frank Chiulli",		GNM_IMPORT_EXPORT,
		N_("OLE2 support.") },
	{ "Kenneth Christiansen",	GNM_TRANSLATION,
		N_("Localization.") },
	{ "Zbigniew Chyla",		GNM_CORE,
		N_("Plugin system, localization.") },
	{ "J.H.M. Dassen (Ray)",	GNM_PACKAGING,
		N_("Debian packaging.") },
	{ "Jeroen Dirks",		GNM_ANALYTICS,
		N_("Simplex algorithm for Solver (LP Solve).") },
	{ "Tom Dyas",			GNM_FEATURE_HACKER,
		N_("Original plugin engine.") },
	{ "Gergo Erdi",			GNM_GUI,
		N_("Custom UI tools") },
	{ "John Gotts",			GNM_PACKAGING,
		N_("RPM packaging") },
	{ "Andreas J. G\xc3\xbclzow",	GNM_CORE|GNM_FEATURE_HACKER|GNM_ANALYTICS|GNM_IMPORT_EXPORT|GNM_GUI|GNM_USABILITY|GNM_DOCUMENTATION|GNM_TRANSLATION|GNM_QA,
		N_("Statistics and GUI master") },
	{ "Jon K\xc3\xa5re Hellan",	GNM_CORE|GNM_FEATURE_HACKER|GNM_ANALYTICS|GNM_IMPORT_EXPORT|GNM_SCRIPTING|GNM_GUI|GNM_USABILITY|GNM_DOCUMENTATION|GNM_TRANSLATION|GNM_QA,
		N_("UI Polish and all round bug fixer") },
	{ "Ross Ihaka",			GNM_ANALYTICS,
		N_("pgamma fucntion") },
	{ "Jukka-Pekka Iivonen",	GNM_ANALYTICS|GNM_GUI|GNM_FEATURE_HACKER,
		N_("Solver, lots of worksheet functions, and general trailblazer") },
	{ "Jakub Jelinek",		GNM_CORE,
		N_("One of the original core contributors") },
	{ "Jakub Steiner",		GNM_ART,
		N_("Icons and Images") },
	{ "Chris Lahey",		GNM_FEATURE_HACKER,
		N_("The original value format engine") },
	{ "Takashi Matsuda",		GNM_FEATURE_HACKER,
		N_("The original text plugin") },
	{ "Michael Meeks",		GNM_CORE|GNM_IMPORT_EXPORT,
		N_("Started the MS Excel import/export engine, and 'GnmStyle'") },
	{ "Lutz Muller",		GNM_FEATURE_HACKER,
		N_("SheetObject improvement") },
	{ "Federico M. Quintero",	GNM_CORE,
		N_("canvas support") },
	{ "Mark Probst",		GNM_SCRIPTING,
		N_("Guile support") },
	{ "Rasca",			GNM_IMPORT_EXPORT,
		N_("HTML, troff, LaTeX exporters") },
	{ "Vincent Renardias",		GNM_IMPORT_EXPORT|GNM_TRANSLATION,
		N_("original CSV support, French localization") },
	{ "Ariel Rios",			GNM_SCRIPTING,
		N_("Guile support") },
	{ "Arturo Tena",		GNM_IMPORT_EXPORT,
		N_("Initial work on OLE2 for libgsf") },
	{ "Almer S. Tigelaar",		GNM_FEATURE_HACKER|GNM_IMPORT_EXPORT,
		N_("Consolidation and Structured Text importer") },
	{ "Bruno Unna",			GNM_IMPORT_EXPORT,
		N_("Pieces of MS Excel import") },
	{ "Daniel Veillard",		GNM_IMPORT_EXPORT,
		N_("Initial XML support") },
	{ "Vladimir Vuksan",		GNM_ANALYTICS,
		N_("Some financial functions") },
	{ "Morten Welinder",		GNM_CORE|GNM_FEATURE_HACKER|GNM_ANALYTICS|GNM_IMPORT_EXPORT|GNM_SCRIPTING|GNM_GUI|GNM_USABILITY|GNM_TRANSLATION|GNM_QA,
		N_("All round powerhouse") },
	{ "Kevin Breit",		GNM_DOCUMENTATION, NULL },
	{ "Thomas Canty",		GNM_DOCUMENTATION, NULL },
	{ "Adrian Custer",		GNM_DOCUMENTATION, NULL },
	{ "Adrian Likins",		GNM_DOCUMENTATION, NULL },
	{ "Aaron Weber",		GNM_DOCUMENTATION, NULL },
	{ "Alexander Kirillov",		GNM_DOCUMENTATION, NULL },
};

static void
cb_plot_resize (FooCanvas *canvas, GtkAllocation *alloc, FooCanvasItem *ctrl)
{
	foo_canvas_item_set (ctrl,
		"w", (double)alloc->width,
		"h", (double)alloc->height,
		"logical_width_pts",  ((double)alloc->width)  * 72. / 96.,
		"logical_height_pts", ((double)alloc->height) * 72. / 96.,
		NULL);
}
static void
cb_canvas_realized (GtkLayout *canvas, FooCanvasItem *ctrl)
{
	gdk_window_set_back_pixmap (canvas->bin_window, NULL, FALSE);
	cb_plot_resize (FOO_CANVAS (canvas),
		&GTK_WIDGET (canvas)->allocation, ctrl);
}

typedef struct {
        GtkDialog *about;
	GtkWidget *canvas;
	FooCanvasItem *ctrl;
	GogObject *graph;
	GogStyle  *contributor_style;
	GOData *contribs_data, *individual_data, *contributor_name;

	guint	 timer;

	double	 contribs [GNM_ABOUT_NUM_TYPES];
	double	 individual [GNM_ABOUT_NUM_TYPES];
	unsigned item_index;
	int	 fade_state;
	gboolean dec;
} GnmAboutState;

static void
gnm_about_state_free (GnmAboutState *state)
{
	if (state->timer != 0) {
		g_source_remove (state->timer);
		state->timer = 0;
	}
	g_object_unref (state->graph);
	g_free (state);
}

#define FADE_STATES	15
#define MAX_FADE_STATE	(FADE_STATES*2)

static gboolean
cb_about_animate (GnmAboutState *state)
{
	int i;
	float alpha;

	if (state->fade_state == MAX_FADE_STATE) {
		state->fade_state = 0;
		state->item_index++;
		if (state->item_index >= G_N_ELEMENTS (contributors)) {
			state->item_index = 0;
			state->dec = !state->dec;
		}

		for (i = 0 ; i < GNM_ABOUT_NUM_TYPES ; i++)
			if (contributors [state->item_index].contributions & (1 << i)) {
				state->contribs [i] += state->dec ? -1 : 1;
				state->individual [i] = 1;
			} else
				state->individual [i] = 0;
	} else
		state->fade_state++;

	/* 1-((x-25)/25)**4 */
	alpha = (state->fade_state - FADE_STATES) / (double)FADE_STATES;
	alpha *= alpha;
	state->contributor_style->font.color = UINT_RGBA_CHANGE_A (
		state->contributor_style->font.color, (unsigned)(255 * (1. - alpha)));
	go_data_scalar_str_set_str (GO_DATA_SCALAR_STR (state->contributor_name),
		contributors [state->item_index].name, FALSE);
	go_data_emit_changed (GO_DATA (state->contribs_data));
	go_data_emit_changed (GO_DATA (state->individual_data));
	return TRUE;
}

void
dialog_about (WorkbookControlGUI *wbcg)
{
	GnmAboutState *state;
	GogObject *chart, *tmp;
	GogPlot   *plot;
	GogSeries *series;
	GOData *labels;
	int i;

	/* Ensure we only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, ABOUT_KEY))
		return;

	state = g_new0 (GnmAboutState, 1);
        state->about = (GtkDialog *)gtk_dialog_new_with_buttons (_("About Gnumeric"), NULL,
		GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_NO_SEPARATOR,
		GTK_STOCK_OK,		GTK_RESPONSE_OK,
		NULL);
	state->fade_state = MAX_FADE_STATE;	/* prime things to start at item 0 */
	state->item_index = -1;
	state->dec = FALSE;
	for (i = GNM_ABOUT_NUM_TYPES ; i-- > 0 ; )
		state->contribs[i] = state->individual[i] = 0.;
	g_object_set_data_full (G_OBJECT (state->about),
		"state", state, (GDestroyNotify)gnm_about_state_free);

	state->graph = g_object_new (GOG_GRAPH_TYPE, NULL);
	GOG_STYLED_OBJECT (state->graph)->style->fill.type = GOG_FILL_STYLE_GRADIENT;
	GOG_STYLED_OBJECT (state->graph)->style->fill.pattern.back = 0xFFFF99FF;
	GOG_STYLED_OBJECT (state->graph)->style->fill.gradient.dir = GO_GRADIENT_W_TO_E_MIRRORED;
	GOG_STYLED_OBJECT (state->graph)->style->outline.width = 0; /* hairline */
	GOG_STYLED_OBJECT (state->graph)->style->outline.color = RGBA_BLACK;
	gog_style_set_fill_brightness (
		GOG_STYLED_OBJECT (state->graph)->style, 70.);
#if 0
	gog_style_set_fill_image_filename (GOG_STYLED_OBJECT (state->graph)->style,
		gnumeric_icondir ("gnumeric-about.png"));
#endif

	/* A bar plot of the current contributors activities */
	chart = gog_object_add_by_name (state->graph, "Chart", NULL);
	GOG_STYLED_OBJECT (chart)->style->outline.width = -1;
	GOG_STYLED_OBJECT (chart)->style->fill.type = GOG_FILL_STYLE_NONE;
	plot = gog_plot_new_by_name ("GogBarColPlot");
	if (!plot) {
		/* This can happen if plugins are not available.  */
		gnm_about_state_free (state);
		return;
	}
	g_object_set (G_OBJECT (plot),
		"horizontal",			TRUE,
		"vary_style_by_element",	TRUE,
		NULL);
	gog_object_add_by_name (chart, "Plot", GOG_OBJECT (plot));
	series = gog_plot_new_series (plot);
	labels = go_data_vector_str_new ( about_types, G_N_ELEMENTS (about_types));
	go_data_vector_str_set_translation_domain (GO_DATA_VECTOR_STR (labels), NULL);
	g_object_ref (labels); /* set_dim absorbs the ref, add an extra for next plot */
	gog_series_set_dim (series, 0, labels, NULL);
	state->individual_data = go_data_vector_val_new (
		state->individual, G_N_ELEMENTS (state->individual));
	gog_series_set_dim (series, 1, state->individual_data, NULL);
	GOG_STYLED_OBJECT (series)->style->outline.width = -1;
	GOG_STYLED_OBJECT (series)->style->fill.type = GOG_FILL_STYLE_GRADIENT;
	GOG_STYLED_OBJECT (series)->style->fill.gradient.dir = GO_GRADIENT_N_TO_S_MIRRORED;
	gog_style_set_fill_brightness (
		GOG_STYLED_OBJECT (series)->style, 70.);

	/* remove the default grid, its ugly here */
	tmp = gog_object_get_child_by_role (chart,
		gog_object_find_role_by_name (chart, "Grid"));
	gog_object_clear_parent (tmp);
	g_object_unref (tmp);

	/* hide the X-axis */
	tmp = gog_object_get_child_by_role (chart,
		gog_object_find_role_by_name (chart, "X-Axis"));
	g_object_set (G_OBJECT (tmp),
		"major-tick-labeled",	FALSE,
		"major-tick-out",	FALSE,
		NULL);
	GOG_STYLED_OBJECT (tmp)->style->line.width = -1;
	tmp = gog_object_get_child_by_role (chart,
		gog_object_find_role_by_name (chart, "Y-Axis"));
	gog_style_set_font (GOG_STYLED_OBJECT (tmp)->style,
		pango_font_description_from_string ("Sans 10"));

	tmp = gog_object_add_by_name (chart, "Title", NULL);
	gog_object_set_pos (tmp, GOG_POSITION_N | GOG_POSITION_ALIGN_START);
	state->contributor_name = go_data_scalar_str_new ("", FALSE);
	gog_dataset_set_dim (GOG_DATASET (tmp), 0, state->contributor_name, NULL);
	state->contributor_style = GOG_STYLED_OBJECT (tmp)->style;
	gog_style_set_font (GOG_STYLED_OBJECT (tmp)->style,
		pango_font_description_from_string ("Sans Bold 10"));

	/* A pie of the cumulative contributions */
	chart = gog_object_add_by_name (state->graph, "Chart", NULL);
	GOG_STYLED_OBJECT (chart)->style->outline.width = -1;
	GOG_STYLED_OBJECT (chart)->style->fill.type = GOG_FILL_STYLE_NONE;
	gog_chart_set_position  (GOG_CHART (chart), 1, 0, 1, 1);
	plot = gog_plot_new_by_name ("GogPiePlot");
	if (!plot) {
		/* This can happen if plugins are not available.  */
		gnm_about_state_free (state);
		return;
	}
	gog_object_add_by_name (chart, "Plot", GOG_OBJECT (plot));
	series = gog_plot_new_series (plot);
	gog_series_set_dim (series, 0, labels, NULL);
	state->contribs_data = go_data_vector_val_new (
		state->contribs, G_N_ELEMENTS (state->contribs));
	gog_series_set_dim (series, 1, state->contribs_data, NULL);
	GOG_STYLED_OBJECT (series)->style->outline.width = -1;
	GOG_STYLED_OBJECT (series)->style->fill.type = GOG_FILL_STYLE_GRADIENT;
	GOG_STYLED_OBJECT (series)->style->fill.gradient.dir = GO_GRADIENT_NW_TO_SE;
	gog_style_set_fill_brightness (
		GOG_STYLED_OBJECT (series)->style, 70.);

	tmp = gog_object_add_by_name (state->graph, "Title", NULL);
	gog_object_set_pos (tmp, GOG_POSITION_S | GOG_POSITION_ALIGN_END);
	gog_dataset_set_dim (GOG_DATASET (tmp), 0,
		go_data_scalar_str_new (
			"Copyright \xc2\xa9 2001-2004 Jody Goldberg\n"
			"Copyright \xc2\xa9 1998-2000 Miguel de Icaza", FALSE),
		NULL);
	gog_style_set_font (GOG_STYLED_OBJECT (tmp)->style,
		pango_font_description_from_string ("Sans Bold 12"));

	state->canvas = foo_canvas_new ();
	gtk_widget_set_size_request (state->canvas, 400, 300);
	foo_canvas_scroll_to (FOO_CANVAS (state->canvas), 0, 0);

	state->ctrl = foo_canvas_item_new (foo_canvas_root (FOO_CANVAS (state->canvas)),
			GOG_CONTROL_FOOCANVAS_TYPE,
			"model", state->graph,
			NULL);
	g_object_connect (state->canvas,
		"signal::realize",	 G_CALLBACK (cb_canvas_realized), state->ctrl,
		"signal::size_allocate", G_CALLBACK (cb_plot_resize), state->ctrl,
		NULL);
	gtk_box_pack_start (GTK_BOX (state->about->vbox), state->canvas, TRUE, TRUE, 0);

	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (state->about), ABOUT_KEY);
	gtk_widget_show_all (GTK_WIDGET (state->about));
	g_signal_connect (state->about, "response",
		G_CALLBACK (gtk_widget_destroy), NULL);

	state->timer = g_timeout_add (100,
		(GSourceFunc) cb_about_animate, state);
}
