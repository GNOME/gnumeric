/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-style.c :
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
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-object-xml.h>
#include <goffice/utils/go-color.h>

#include <src/gui-util.h>
#include <glade/glade-xml.h>
#include <gtk/gtkspinbutton.h>
#include <widgets/widget-color-combo.h>
#include <widgets/color-palette.h>
#include <widgets/widget-pixmap-combo.h>
#include <gdk-pixbuf/gdk-pixdata.h>
#include <libart_lgpl/art_render_gradient.h>

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>
#include <string.h>

typedef GObjectClass GogStyleClass;

static GObjectClass *parent_klass;

/**
 * I would have liked to do this differently and have a tighter binding between theme element and style
 * 	eg gog_style_new (theme_element)
 * However that will not work easily in the context of xls import where we do
 * not know what the type is destined for until later.  This structure melds
 * smoothly with both approaches at the expense of a bit of power.
 **/
/*************************************************************************/

typedef struct {
	GladeXML  *gui;
	GogObject *obj;
	gboolean  enable_edit;
	struct {
		struct {
			GtkWidget *fore, *back, *combo;
		} pattern;
		struct {
			GtkWidget *start, *end, *combo;
		} gradient;
		struct {
		} image;
	} fill;
} StylePrefState;

/* utility routines */
static GogStyle *
gog_object_get_style (GogObject *obj)
{
	GogStyle *style;
	g_object_get (G_OBJECT (obj), "style", &style, NULL);
	return style;
}

static GogStyle *
gog_object_dup_style (GogObject *obj)
{
	return gog_style_dup (gog_object_get_style (obj));
}

static void
gog_object_set_style (GogObject *obj, GogStyle *style)
{
	g_object_set (G_OBJECT (obj), "style", style, NULL);
	g_object_unref (style);
}

static GtkWidget *
create_color_combo (StylePrefState *state, GOColor initial_val,
		    char const *group, char const *label_name,
		    GCallback func)
{
	GtkWidget *w = color_combo_new (NULL, _("Automatic"),
		NULL, color_group_fetch (group, NULL));
	color_combo_set_instant_apply (COLOR_COMBO (w), FALSE);
	gtk_combo_box_set_tearable (GTK_COMBO_BOX (w), FALSE);
	gnome_color_picker_set_use_alpha (COLOR_COMBO (w)->palette->picker, TRUE);
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (glade_xml_get_widget (state->gui, label_name)), w);
	color_combo_set_gocolor (w, initial_val);
	g_signal_connect (G_OBJECT (w),
		"color_changed",
		G_CALLBACK (func), state);
	return w;
}

static GOColor
gog_style_get_fill_color (GogStyle const *style, unsigned i)
{
	GOColor first = RGBA_BLACK;
	GOColor second = RGBA_WHITE;
	switch (style->fill.type) {
	case GOG_FILL_STYLE_PATTERN:
		first = style->fill.u.pattern.pat.fore;
		second = style->fill.u.pattern.pat.back;
		break;
	case GOG_FILL_STYLE_GRADIENT:
		first = style->fill.u.gradient.start;
		second = style->fill.u.gradient.end;
		break;
	default :
		break;
	}
	return (i == 1) ? first : second;
}

/************************************************************************/
static void
cb_outline_size_changed (GtkAdjustment *adj, StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	g_return_if_fail (style != NULL);
	style->outline.width = adj->value;
	gog_object_set_style (state->obj, style);
}

static void
cb_outline_color_changed (GtkWidget *cc,
			  G_GNUC_UNUSED GdkColor *color,	G_GNUC_UNUSED gboolean  is_custom,
			  G_GNUC_UNUSED gboolean  by_user,	G_GNUC_UNUSED gboolean  is_default,
			  StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	g_return_if_fail (style != NULL);
	style->outline.color = color_combo_get_gocolor (cc);
	gog_object_set_style (state->obj, style);
}

static void
outline_init (StylePrefState *state, GogStyle const *style, gboolean enable)
{
	GtkWidget *w, *table;

	if (!enable) {
		gtk_widget_hide (glade_xml_get_widget (state->gui, "outline_outer_table"));
		return;
	}

	w = glade_xml_get_widget (state->gui, "outline_size_spin");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (w), style->outline.width);
	g_signal_connect (gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (w)),
		"value_changed",
		G_CALLBACK (cb_outline_size_changed), state);

	w = create_color_combo (state, style->outline.color,
		"outline_color", "outline_color_label", 
		G_CALLBACK (cb_outline_color_changed));
	table = glade_xml_get_widget (state->gui, "outline_table");
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 0, 1, 0, 0, 0, 0);
	gtk_widget_show_all (table);
}


/************************************************************************/

static void
cb_line_size_changed (GtkAdjustment *adj, StylePrefState const *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	g_return_if_fail (style != NULL);
	style->line.width = adj->value;
	gog_object_set_style (state->obj, style);
}

static void
cb_line_color_changed (GtkWidget *cc,
		       G_GNUC_UNUSED GdkColor *color,	G_GNUC_UNUSED gboolean  is_custom,
		       G_GNUC_UNUSED gboolean  by_user,	G_GNUC_UNUSED gboolean  is_default,
		       StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	g_return_if_fail (style != NULL);
	style->line.color = color_combo_get_gocolor (cc);
	gog_object_set_style (state->obj, style);
}

static void
line_init (StylePrefState *state, GogStyle const *style, gboolean enable)
{
	GtkWidget *w, *table;

	if (!enable) {
		gtk_widget_hide (glade_xml_get_widget (state->gui, "line_outer_table"));
		return;
	}

	/* Size */
	w = glade_xml_get_widget (state->gui, "line_size_spin");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (w), style->line.width);
	g_signal_connect (gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (w)),
		"value_changed",
		G_CALLBACK (cb_line_size_changed), state);

	/* Colour */
	w = create_color_combo (state, style->line.color,
		"line_color", "line_color_label",
		G_CALLBACK (cb_line_color_changed));
	table = glade_xml_get_widget (state->gui, "line_table");
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 0, 1, 0, 0, 0, 0);
	gtk_widget_show_all (table);
}

/************************************************************************/

static void
cb_pattern_type_changed (GtkWidget *cc, int index, StylePrefState const *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);

	g_return_if_fail (style != NULL);

	style->fill.u.pattern.pat.pattern =
		(index / 10 - 1)  * 6 + index % 10 - 1;
	gog_object_set_style (state->obj, style);
}

static void
populate_pattern_combo (StylePrefState *state, GogStyle const *style)
{
	GtkWidget *table, *combo;
	GOPatternType type = GO_PATTERN_SOLID;

	if (state->fill.pattern.combo != NULL)
		gtk_widget_destroy (state->fill.pattern.combo);

	state->fill.pattern.combo = combo = go_pattern_selector (
		gog_style_get_fill_color (style, 1),
		gog_style_get_fill_color (style, 2));

	table = glade_xml_get_widget (state->gui, "fill_pattern_table");
	gtk_table_attach (GTK_TABLE (table), combo, 1, 2, 0, 1, 0, 0, 0, 0);
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (glade_xml_get_widget (state->gui, "fill_pattern_type_label")), combo);

	if (style->fill.type == GOG_FILL_STYLE_PATTERN)
		type = style->fill.u.pattern.pat.pattern;
	pixmap_combo_select_pixmap (PIXMAP_COMBO(combo), type);
	g_signal_connect (G_OBJECT (combo),
		"changed",
		G_CALLBACK (cb_pattern_type_changed), state);
	gtk_widget_show (combo);
}

static void
cb_fg_color_changed (GtkWidget *cc,
		     G_GNUC_UNUSED GdkColor *color,	G_GNUC_UNUSED gboolean is_custom,
		     G_GNUC_UNUSED gboolean by_user,	G_GNUC_UNUSED gboolean is_default,
		     StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	g_return_if_fail (style != NULL);
	style->fill.u.pattern.pat.fore = color_combo_get_gocolor (cc);
	gog_object_set_style (state->obj, style);
	populate_pattern_combo (state, style);
}

static void
cb_bg_color_changed (GtkWidget *cc,
		     G_GNUC_UNUSED GdkColor *color,	G_GNUC_UNUSED gboolean is_custom,
		     G_GNUC_UNUSED gboolean by_user,	G_GNUC_UNUSED gboolean is_default,
		     StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	g_return_if_fail (style != NULL);
	style->fill.u.pattern.pat.back = color_combo_get_gocolor (cc);
	gog_object_set_style (state->obj, style);
	populate_pattern_combo (state, style);
}

static void
fill_pattern_init (StylePrefState *state, GogStyle const *style)
{
	GtkWidget *w, *table =
		glade_xml_get_widget (state->gui, "fill_pattern_table");

	state->fill.pattern.fore = w = create_color_combo (state,
		gog_style_get_fill_color (style, 1),
		"pattern_foreground", "fill_pattern_foreground_label",
		G_CALLBACK (cb_fg_color_changed));
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 1, 2, 0, 0, 0, 0);

	state->fill.pattern.back = w = create_color_combo (state,
		gog_style_get_fill_color (style, 2),
		"pattern_background", "fill_pattern_background_label",
		G_CALLBACK (cb_bg_color_changed));
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 2, 3, 0, 0, 0, 0);

	populate_pattern_combo (state, style);
	gtk_widget_show_all (table);
}

/************************************************************************/

static void
cb_gradient_type_changed (GtkWidget *cc, int index, StylePrefState const *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	g_return_if_fail (style != NULL);
	style->fill.u.gradient.dir =  (index / 10 - 1)  * 4 + index % 10 - 1;
	gog_object_set_style (state->obj, style);
}

static void
populate_gradient_combo (StylePrefState *state, GogStyle const *style)
{
	/*PixmapComboElement data for the graident combo, inline_gdkpixbuf initially set to NULL*/
	static PixmapComboElement gog_gradient_elts[] = {
		{NULL, NULL, 11},
		{NULL, NULL, 12},
		{NULL, NULL, 13},
		{NULL, NULL, 14},
		{NULL, NULL, 21},
		{NULL, NULL, 22},
		{NULL, NULL, 23},
		{NULL, NULL, 24},
		{NULL, NULL, 31},
		{NULL, NULL, 32},
		{NULL, NULL, 33},
		{NULL, NULL, 34},
		{NULL, NULL, 41},
		{NULL, NULL, 42},
		{NULL, NULL, 43},
		{NULL, NULL, 44},
	};

	GtkWidget *w, *table;
	guint i, length;
	GdkPixbuf *pixbuf;
	GdkPixdata pixdata;
	gpointer data;
	ArtRender *render;
	ArtGradientLinear gradient;
	ArtGradientStop stops[] = {
		{ 0., { 0, 0, 0, 0 }},
		{ 1., { 0, 0, 0, 0 }}
	};

	if (state->fill.gradient.combo != NULL)
		gtk_widget_destroy (state->fill.gradient.combo);

	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 20, 20);
	for (i = 0; i < G_N_ELEMENTS (gog_gradient_elts); i++) {
		memset (gdk_pixbuf_get_pixels (pixbuf), 0, gdk_pixbuf_get_rowstride (pixbuf) * 20);
		render = art_render_new (0, 0, 20, 20,
			gdk_pixbuf_get_pixels (pixbuf),
			gdk_pixbuf_get_rowstride (pixbuf),
			gdk_pixbuf_get_n_channels (pixbuf) - 1,
			8, ART_ALPHA_SEPARATE, NULL);
		if (gog_gradient_elts[i].inline_gdkpixbuf != NULL)
			g_free ((gpointer) gog_gradient_elts[i].inline_gdkpixbuf);
		if (i < 4) {
			gradient.a = 0.;
			/*The values used in the two lines below might seem strange
			but using the more natural 1./19. and 0. give very strange results*/
			gradient.b = .998 / 19.;
			gradient.c =  0.001;
		} else if (i < 8) {
			gradient.a = 1. / 19.;
			gradient.b = 0.;
			gradient.c = 0.;
		} else if (i < 12) {
			gradient.a = 1. / 39.;
			gradient.b = 1. / 39.;
			gradient.c = 0;
		} else {
			gradient.a = -1. / 39.;
			gradient.b = 1. / 39.;
			/* Note: this gradient is anchored at (x1,y0).  */
			gradient.c = 0.5;
		}

		switch (i % 4) {
		case 0:
			gradient.spread = ART_GRADIENT_REPEAT;
			gradient.n_stops = G_N_ELEMENTS (stops);
			gradient.stops = stops;
			go_color_to_artpix (stops[0].color,
						style->fill.u.gradient.start);
			go_color_to_artpix (stops[1].color,
						style->fill.u.gradient.end);
			break;
		case 1:
			gradient.spread = ART_GRADIENT_REPEAT;
			gradient.n_stops = G_N_ELEMENTS (stops);
			gradient.stops = stops;
			go_color_to_artpix (stops[0].color,
						style->fill.u.gradient.end);
			go_color_to_artpix (stops[1].color,
						style->fill.u.gradient.start);
			break;
		case 2:
			gradient.spread = ART_GRADIENT_REFLECT;
			gradient.n_stops = G_N_ELEMENTS (stops);
			gradient.stops = stops;
			go_color_to_artpix (stops[0].color,
						style->fill.u.gradient.start);
			go_color_to_artpix (stops[1].color,
						style->fill.u.gradient.end);
			gradient.a *= 39. / 19.;
			gradient.b *= 39. / 19.;
			gradient.c *= 39. / 19.;
			break;
		case 3:
			gradient.spread = ART_GRADIENT_REFLECT;
			gradient.n_stops = G_N_ELEMENTS (stops);
			gradient.stops = stops;
			go_color_to_artpix (stops[0].color,
					    style->fill.u.gradient.end);
			go_color_to_artpix (stops[1].color,
					    style->fill.u.gradient.start);
			gradient.a *= 2.;
			gradient.b *= 2.;
			gradient.c *= 2.;
			break;
		}
		art_render_gradient_linear (render,
			&gradient, ART_FILTER_NEAREST);
		art_render_invoke (render);

		data = gdk_pixdata_from_pixbuf (&pixdata, pixbuf, FALSE);
		gog_gradient_elts[i].inline_gdkpixbuf = gdk_pixdata_serialize (&pixdata, &length);
		g_free (data);
	}
	g_object_unref (pixbuf);

	state->fill.gradient.combo = w =
		pixmap_combo_new (gog_gradient_elts, 4, 4);
	gtk_combo_box_set_tearable (GTK_COMBO_BOX (w), FALSE);
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (glade_xml_get_widget (state->gui, "fill_gradient_direction_label")), w);

	table = glade_xml_get_widget (state->gui, "fill_gradient_table");
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 0, 1, 0, 0, 0, 0);
	if (style->fill.type == GOG_FILL_STYLE_GRADIENT)
		pixmap_combo_select_pixmap (PIXMAP_COMBO (w), style->fill.u.gradient.dir);
	g_signal_connect (G_OBJECT (w),
		"changed",
		G_CALLBACK (cb_gradient_type_changed), state);
	gtk_widget_show (w);
}

static void
cb_fill_gradient_start_color (GtkWidget *cc,
			      G_GNUC_UNUSED GdkColor *color,	G_GNUC_UNUSED gboolean is_custom,
			      G_GNUC_UNUSED gboolean by_user,	G_GNUC_UNUSED gboolean is_default,
			      StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	g_return_if_fail (style != NULL);
	style->fill.u.gradient.start = color_combo_get_gocolor (cc);
	gog_object_set_style (state->obj, style);
	populate_gradient_combo (state, style);
}

static void
cb_fill_gradient_end_color (GtkWidget *cc,
			    G_GNUC_UNUSED GdkColor *color,	G_GNUC_UNUSED gboolean is_custom,
			    G_GNUC_UNUSED gboolean by_user,	G_GNUC_UNUSED gboolean is_default,
			    StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	g_return_if_fail (style != NULL);
	style->fill.u.gradient.end = color_combo_get_gocolor (cc);
	gog_object_set_style (state->obj, style);
	populate_gradient_combo (state, style);
}

static void
fill_gradient_init (StylePrefState *state, GogStyle const *style)
{
	GtkWidget *w, *table = glade_xml_get_widget (state->gui, "fill_gradient_table");

	state->fill.gradient.start = w = create_color_combo (state,
		gog_style_get_fill_color (style, 1),
		"gradient_start", "fill_gradient_start_label",
		G_CALLBACK (cb_fill_gradient_start_color));
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 1, 2, 0, 0, 0, 0);

	state->fill.gradient.end = w = create_color_combo (state,
		gog_style_get_fill_color (style, 2),
		"gradient_end", "fill_gradient_end_label",
		G_CALLBACK (cb_fill_gradient_end_color));
	gtk_table_attach (GTK_TABLE (table), w, 3, 4, 1, 2, 0, 0, 0, 0);

	populate_gradient_combo (state, style);
	gtk_widget_show_all (table);
}

static void
cb_fill_type_changed (GtkWidget *menu, StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	GtkWidget *w;
	unsigned page;
	char const *filename;

	page = gtk_option_menu_get_history (GTK_OPTION_MENU (menu));

	if (page != style->fill.type &&
	    GOG_FILL_STYLE_IMAGE == style->fill.type)
		g_free (style->fill.u.image.image_file);

	switch (page) {
	case GOG_FILL_STYLE_NONE:
		break;
	case GOG_FILL_STYLE_PATTERN:
		style->fill.u.pattern.pat.fore =
			color_combo_get_gocolor (state->fill.pattern.fore);
		style->fill.u.pattern.pat.back =
			color_combo_get_gocolor (state->fill.pattern.back);
		style->fill.u.pattern.pat.pattern =
			((PixmapCombo*)state->fill.pattern.combo)->last_index;
		break;

	case GOG_FILL_STYLE_GRADIENT:
		style->fill.u.gradient.start =
			color_combo_get_gocolor (state->fill.gradient.start);
		style->fill.u.gradient.end =
			color_combo_get_gocolor (state->fill.gradient.end);
		style->fill.u.gradient.dir =
			((PixmapCombo*)state->fill.pattern.combo)->last_index;
		break;

	case GOG_FILL_STYLE_IMAGE:
		w = glade_xml_get_widget (state->gui, "file_image_table");
		filename = gtk_entry_get_text (GTK_ENTRY (w));
		style->fill.u.image.image_file = (filename && *filename)? g_strdup (filename): NULL;
		w = glade_xml_get_widget (state->gui, "image_option");
		style->fill.u.image.type = gtk_option_menu_get_history (GTK_OPTION_MENU (w));
		break;
	}
	style->fill.type = page;
	gog_object_set_style (state->obj, style);

	w = glade_xml_get_widget (state->gui, "fill_notebook");
	gtk_notebook_set_current_page (GTK_NOTEBOOK (w), page);
}

/************************************************************************/

#if 0
static gboolean
cb_image_filename_changed (GtkWidget *cc, GdkEventFocus *ev, GogObject *obj)
{
	GogStyle *style = NULL, *newstyle;
	char const *filename;

	g_object_get (G_OBJECT (obj), "style", &style, NULL);
	newstyle = gog_style_dup (style);
	g_object_unref (style);
	g_return_val_if_fail (newstyle != NULL, FALSE);

	filename = gtk_entry_get_text (GTK_ENTRY (cc));

	newstyle->fill.u.image.image_file = g_strdup (filename);
	g_object_set (G_OBJECT (obj), "style", newstyle, NULL);
	g_object_unref (newstyle);

	return FALSE;
}

static void
cb_image_file_select (GtkWidget *cc, GogObject *obj)
{
	GogStyle *style = NULL, *newstyle;
	GtkWidget *fs, *w;
	gint result;
	const gchar* filename;

	g_object_get (G_OBJECT (obj), "style", &style, NULL);
	newstyle = gog_style_dup (style);
	g_object_unref (style);
	g_return_if_fail (newstyle != NULL);

	fs = gtk_file_selection_new (_("Select an image file"));
	gtk_window_set_modal (GTK_WINDOW (fs), TRUE);
	if (newstyle->fill.u.image.image_file)
		gtk_file_selection_set_filename (GTK_FILE_SELECTION (fs), newstyle->fill.u.image.image_file);
	result = gtk_dialog_run (GTK_DIALOG (fs));
	if (result == GTK_RESPONSE_OK) {
		filename = gtk_file_selection_get_filename (GTK_FILE_SELECTION (fs));
		if (filename && !*filename) filename = NULL;
		g_free (newstyle->fill.u.image.image_file);
		newstyle->fill.u.image.image_file = g_strdup (filename);
		w = glade_xml_get_widget (state->gui, "image_filename");
		gtk_entry_set_text (GTK_ENTRY (w), (filename)? filename: "");
		g_object_set (G_OBJECT (obj), "style", newstyle, NULL);
	}
	g_object_unref (newstyle);
	gtk_widget_destroy (fs);
}
#endif

static void
cb_image_style_changed (GtkWidget *w, StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	g_return_if_fail (style != NULL);
	style->fill.u.image.type = gtk_option_menu_get_history (GTK_OPTION_MENU (w));
	gog_object_set_style (state->obj, style);
}

static void
fill_image_init (StylePrefState *state, GogStyle const *style)
{
	GtkWidget *w;
#if 0
		w = glade_xml_get_widget (state->gui, "image_filename");
		gtk_entry_set_text (GTK_ENTRY (w), style->fill.u.image.image_file);
		w = glade_xml_get_widget (state->gui, "image_option");
		gtk_option_menu_set_history (GTK_OPTION_MENU (w), style->fill.u.image.type);

	/* initialization of the image related widgets */
	w = glade_xml_get_widget (state->gui, "image_filename");
	g_signal_connect (G_OBJECT (w),
		"focus_out_event",
		G_CALLBACK (cb_image_filename_changed), obj);
	w = glade_xml_get_widget (state->gui, "image_button");
	g_signal_connect (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_image_file_select), state);
#endif

	w = glade_xml_get_widget (state->gui, "fill_image_fit");
	g_signal_connect (G_OBJECT (w),
		"changed",
		G_CALLBACK (cb_image_style_changed), state);
}

/************************************************************************/

static void
fill_init (StylePrefState *state, GogStyle const *style, gboolean enable)
{
	GtkWidget *w;

	if (!enable) {
		gtk_widget_hide (glade_xml_get_widget (state->gui, "fill_outer_table"));
		return;
	}

	fill_pattern_init (state, style);
	fill_gradient_init (state, style);
	fill_image_init (state, style);

	w = glade_xml_get_widget (state->gui, "fill_type_menu");
	gtk_option_menu_set_history (GTK_OPTION_MENU (w), style->fill.type);

	g_signal_connect (G_OBJECT (w),
		"changed",
		G_CALLBACK (cb_fill_type_changed), state);
	cb_fill_type_changed (w, state); /* ensure it gets initialized */

	w = glade_xml_get_widget (state->gui, "fill_outer_table");
	gtk_widget_show (GTK_WIDGET (w));
}

/************************************************************************/

static void
marker_init (StylePrefState *state, GogStyle const *style, gboolean enable)
{
	GtkWidget *w, *table =
		glade_xml_get_widget (state->gui, "marker_table");


	if (!enable) {
		gtk_widget_hide (glade_xml_get_widget (state->gui, "marker_outer_table"));
		return;
	}

	w = create_color_combo (state,
		gog_style_get_fill_color (style, 1),
		"pattern_foreground", "marker_foreground_label",
		G_CALLBACK (cb_fg_color_changed));
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 1, 2, 0, 0, 0, 0);

	w = create_color_combo (state,
		gog_style_get_fill_color (style, 2),
		"pattern_background", "marker_background_label",
		G_CALLBACK (cb_bg_color_changed));
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 2, 3, 0, 0, 0, 0);

	/* populate_marker_combo (state, style); */
	gtk_widget_show_all (table);
}

/************************************************************************/

static void
font_init (StylePrefState *state, GogStyle const *style, gboolean enable)
{
#if 0
	GtkWidget *w, *table;

	if (!enable) {
		gtk_widget_hide (glade_xml_get_widget (state->gui, "line_frame"));
		return;
	}
#endif
}

static void
gog_style_pref_state_free (StylePrefState *state)
{
	g_object_unref (state->gui);
	g_free (state);
}

GtkWidget *
gog_style_editor (GogObject *obj, CommandContext *cc, guint32 enable)
{
	GogStyle *style = gog_object_get_style (obj);
	GtkWidget *w;
	GladeXML *gui;
	StylePrefState *state;

	g_return_val_if_fail (style != NULL, NULL);

	gui = gnm_glade_xml_new (cc, "gog-style-prefs.glade", "gog_style_prefs", NULL);
	if (gui == NULL)
		return NULL;

	state = g_new0 (StylePrefState, 1);
	state->gui = gui;
	state->obj = obj;
	state->enable_edit = FALSE;

	outline_init (state, style, enable & GOG_STYLE_OUTLINE);
	line_init    (state, style, enable & GOG_STYLE_LINE);
	fill_init    (state, style, enable & GOG_STYLE_FILL);
	marker_init  (state, style, enable & GOG_STYLE_MARKER);
	font_init    (state, style, enable & GOG_STYLE_FONT);

	state->enable_edit = TRUE;

 	w = glade_xml_get_widget (gui, "gog_style_prefs");
	g_object_set_data_full (G_OBJECT (w),
		"state", state, (GDestroyNotify) gog_style_pref_state_free);
	return w;
}

/*****************************************************************************/

GogStyle *
gog_style_new (void)
{
	return g_object_new (GOG_STYLE_TYPE, NULL);
}

/**
 * gog_style_dup :
 * @src : #GogStyle
 *
 **/
GogStyle *
gog_style_dup (GogStyle const *src)
{
	GogStyle *dst = gog_style_new ();
	gog_style_assign (dst, src);
	return dst;
}

void
gog_style_assign (GogStyle *dst, GogStyle const *src)
{
	char *old_image = NULL;

	g_return_if_fail (GOG_STYLE (src) != NULL);
	g_return_if_fail (GOG_STYLE (dst) != NULL);

	if (GOG_FILL_STYLE_IMAGE == dst->fill.type)
		old_image = dst->fill.u.image.image_file;
	if (GOG_FILL_STYLE_IMAGE == dst->fill.type)
		dst->fill.u.image.image_file = g_strdup (src->fill.u.image.image_file);
	g_free (old_image);

	if (src->font.desc != NULL)
		g_object_ref (src->font.desc);
	if (dst->font.desc != NULL)
		g_object_unref (dst->font.desc);

	dst->outline = src->outline;
	dst->fill    = src->fill;
	dst->marker  = src->marker;
	dst->font    = src->font;
}

/**
 * gog_style_merge :
 * @dst : #GogStyle
 * @src :  #GogStyle
 *
 * Merge the attributes from @src onto the elements of @dst that were not user
 * assigned (is_auto)
 **/
void
gog_style_merge	(GogStyle *dst, GogStyle const *src)
{
}

static void
gog_style_finalize (GObject *obj)
{
	GogStyle *style = GOG_STYLE (obj);

	if (GOG_FILL_STYLE_IMAGE == style->fill.type &&
	    style->fill.u.image.image_file)
		g_free (style->fill.u.image.image_file);

	if (style->font.desc != NULL) {
		g_object_unref (style->font.desc);
		style->font.desc = NULL;
	}
	(parent_klass->finalize) (obj);
}

static void
gog_style_class_init (GogStyleClass *klass)
{
	GObjectClass *gobject_klass = (GObjectClass *)klass;
	parent_klass = g_type_class_peek_parent (klass);
	gobject_klass->finalize	= gog_style_finalize;
}

static void
gog_style_init (GogStyle *style)
{
	style->outline.auto_color =
	style->line.auto_color =
	style->marker.is_auto.fore =
	style->marker.is_auto.back =
	style->marker.is_auto.mark =
	style->fill.is_auto = TRUE;
	style->fill.type = GOG_FILL_STYLE_PATTERN;
	go_pattern_set_solid (&style->fill.u.pattern.pat, 0);
}

static gboolean
gog_style_persist_dom_load (GogPersistDOM *gpd, xmlNode *node)
{
#warning TODO
	g_warning ("TODO import style from dom");
	return TRUE;
}

static void
gog_style_persist_dom_save (GogPersistDOM *gpd, xmlNode *parent)
{
#warning TODO
	g_warning ("TODO persist style to dom");
}

static void
gog_style_persist_dom_init (GogPersistDOMClass *iface)
{
	iface->load = gog_style_persist_dom_load;
	iface->save = gog_style_persist_dom_save;
}

GSF_CLASS_FULL (GogStyle, gog_style,
		gog_style_class_init, gog_style_init,
		G_TYPE_OBJECT, 0,
		GSF_INTERFACE (gog_style_persist_dom_init, GOG_PERSIST_DOM_TYPE))

gboolean
gog_style_is_different_size (GogStyle const *a, GogStyle const *b)
{
	if (a == NULL || b == NULL)
		return TRUE;
	return a->outline.width != b->outline.width;
}

/************************************************************************/

static GogSeriesElementStyle *
go_series_element_style_new (GogStyle *style, unsigned i)
{
	GogSeriesElementStyle *pt = g_new (GogSeriesElementStyle, 1);
	pt->i = pt->i;
	pt->style = pt->style;
	g_object_ref (pt->style);
	return pt;
}

static int
go_series_element_style_cmp (GogSeriesElementStyle const *a,
			     GogSeriesElementStyle const *b)
{
	return a->i - b->i;
}

static void
go_series_element_style_free (GogSeriesElementStyle *pt)
{
	g_return_if_fail (pt != NULL);

	if (pt->style != NULL) {
		g_object_unref (pt->style);
		pt->style = NULL;
	}
	g_free (pt);
}

void
gog_series_element_style_list_free (GogSeriesElementStyleList *list)
{
	g_slist_foreach (list, (GFunc) go_series_element_style_free, NULL);
	g_slist_free (list);
}

/**
 * go_series_element_style_list_copy :
 * @list : #GogSeriesElementStyleList
 *
 * Returns a copy of the list that references the same styles as the original
 **/
GogSeriesElementStyleList *
gog_series_element_style_list_copy (GogSeriesElementStyleList *list)
{
	GogSeriesElementStyle *pt = NULL;
	GSList *ptr, *res = NULL;

	for (ptr = list; ptr != NULL ; ptr = ptr->next) {
		pt = ptr->data;
		res = g_slist_prepend (res,
			go_series_element_style_new (pt->style, pt->i));
	}
	return g_slist_reverse (res);
}

GogSeriesElementStyleList *
gog_series_element_style_list_add (GogSeriesElementStyleList *list,
				   unsigned i, GogStyle *style)
{
	GogSeriesElementStyle *pt = NULL;
	GSList *ptr;

	for (ptr = list ; ptr != NULL ; ptr = ptr->next) {
		pt = ptr->data;
		if (pt->i == i)
			break;
	}

	/* Setting NULL clears existing */
	if (style == NULL) {
		if (ptr != NULL) {
			go_series_element_style_free (pt);
			return g_slist_remove (list, pt);
		}
		return list;
	}
	pt = go_series_element_style_new (style, i);
	return g_slist_insert_sorted  (list, pt,
				       (GCompareFunc) go_series_element_style_cmp);
}
