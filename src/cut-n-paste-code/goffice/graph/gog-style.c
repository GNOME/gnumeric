/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-style.c :
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
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-styled-object.h>
#include <goffice/utils/go-color.h>
#include <goffice/utils/go-font.h>
#include <goffice/utils/go-file.h>
#include <goffice/utils/go-marker.h>

#include <goffice/gui-utils/go-color-palette.h>
#include <goffice/gui-utils/go-combo-color.h>
#include <goffice/gui-utils/go-combo-pixmaps.h>

#include <src/gui-util.h>
#include <glade/glade-xml.h>
#include <gtk/gtkcheckbutton.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkimage.h>
#include <gtk/gtktable.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkrange.h>
#include <gtk/gtkcombobox.h>
#include <gtk/gtknotebook.h>
#include <widgets/widget-font-selector.h>
#include <gui-file.h>
#include <gdk-pixbuf/gdk-pixdata.h>

#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n.h>
#include <string.h>

#define HSCALE 100
#define VSCALE 120

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
	GladeXML  	*gui;
	GogStyle  	*style;
	GogStyle  	*default_style;
	GogStyledObject *parent;
	gboolean   	 enable_edit;
	gulong     	 style_changed_handler;
	struct {
		struct {
			GtkWidget *fore, *back, *combo;
		} pattern;
		struct {
			GtkWidget *start, *end, *end_label, *combo;
			GtkWidget *brightness, *brightness_box;
			guint	   timer;
		} gradient;
		struct {
			GdkPixbuf *image;
		} image;
	} fill;
	struct {
		GtkWidget *combo;
	} marker;
} StylePrefState;

static void
cb_style_changed (GogStyledObject *obj, GogStyle *style, StylePrefState *state)
{
	g_warning ("Hmm, nothing to worry about, but this should not happen.  How did a style change externally while it was visible ?");
}

static void
set_style (StylePrefState const *state)
{
	if (state->parent != NULL) {
		g_signal_handler_block (state->parent, state->style_changed_handler);
		g_object_set (G_OBJECT (state->parent), "style", state->style, NULL);
		g_signal_handler_unblock (state->parent, state->style_changed_handler);
	}
}

static GtkWidget *
create_go_combo_color (StylePrefState *state,
		       GOColor initial_val, GOColor default_val,
		       char const *group, char const *label_name,
		       GCallback func)
{
	GtkWidget *w;

	w = go_combo_color_new (NULL, _("Automatic"), default_val,
		go_color_group_fetch (group, NULL));
	go_combo_color_set_instant_apply (GO_COMBO_COLOR (w), FALSE);
	go_combo_color_set_allow_alpha (GO_COMBO_COLOR (w), TRUE);
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (glade_xml_get_widget (state->gui, label_name)), w);
	go_combo_color_set_gocolor (GO_COMBO_COLOR (w), initial_val);
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

static void
gog_style_set_image_preview (GdkPixbuf *pix, StylePrefState *state)
{
	GdkPixbuf *scaled;
	int width, height;
	char *size;
	GtkWidget *w;

	if (state->fill.image.image != pix) {
		if (state->fill.image.image != NULL)
			g_object_unref (state->fill.image.image);
		state->fill.image.image = pix;
		if (state->fill.image.image != NULL)
			g_object_ref (state->fill.image.image);
	}

	w = glade_xml_get_widget (state->gui, "fill_image_sample");

	scaled = gnm_pixbuf_intelligent_scale (pix, HSCALE, VSCALE);
	gtk_image_set_from_pixbuf (GTK_IMAGE (w), scaled);
	g_object_unref (scaled);

	w = glade_xml_get_widget (state->gui, "image-size-label");
	width = gdk_pixbuf_get_width (pix);
	height = gdk_pixbuf_get_height (pix);

	size = g_strdup_printf (_("%d x %d"), width, height);
	gtk_label_set_text (GTK_LABEL (w), size);
	g_free (size);
}

/************************************************************************/
static void
cb_outline_size_changed (GtkAdjustment *adj, StylePrefState *state)
{
	GogStyle *style = state->style;

	g_return_if_fail (style != NULL);

	style->outline.width = adj->value;
	set_style (state);
}

static void
cb_outline_color_changed (G_GNUC_UNUSED GOComboColor *cc, GOColor color,
			  G_GNUC_UNUSED gboolean  is_custom,
			  G_GNUC_UNUSED gboolean  by_user,
			  gboolean  is_default, StylePrefState *state)
{
	GogStyle *style = state->style;

	g_return_if_fail (style != NULL);

	style->outline.color = color;
	style->outline.auto_color = is_default;
	set_style (state);
}

static void
outline_init (StylePrefState *state, gboolean enable)
{
	GogStyle *style = state->style;
	GogStyle *default_style = state->default_style;
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

	w = create_go_combo_color (state,
		style->outline.color, default_style->outline.color,
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
	GogStyle *style = state->style;

	g_return_if_fail (style != NULL);

	style->line.width = adj->value;
	set_style (state);
}

static void
cb_line_color_changed (G_GNUC_UNUSED GOComboColor *cc, GOColor color,
		       G_GNUC_UNUSED gboolean  is_custom,
		       G_GNUC_UNUSED gboolean  by_user,
		       gboolean  is_default, StylePrefState *state)
{
	GogStyle *style = state->style;

	g_return_if_fail (style != NULL);

	style->line.color = color;
	style->line.auto_color = is_default;
	set_style (state);
}

static void
line_init (StylePrefState *state, gboolean enable)
{
	GogStyle *style = state->style;
	GogStyle *default_style = state->default_style;
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
	w = create_go_combo_color (state,
		style->line.color, default_style->line.color,
		"line_color", "line_color_label",
		G_CALLBACK (cb_line_color_changed));
	table = glade_xml_get_widget (state->gui, "line_table");
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 0, 1, 0, 0, 0, 0);
	gtk_widget_show_all (table);
}

/************************************************************************/

static void
cb_pattern_type_changed (GtkWidget *cc, int pattern, StylePrefState const *state)
{
	GogStyle *style = state->style;
	gboolean is_auto = pattern < 0;

	if (is_auto)
		pattern = -pattern;
	style->fill.u.pattern.pat.pattern = pattern;
	set_style (state);
}

static void
populate_pattern_combo (StylePrefState *state)
{
	GogStyle *style = state->style;
	GogStyle *default_style = state->default_style;
	GtkWidget *table, *combo;
	GOPatternType type = GO_PATTERN_SOLID;

	if (state->fill.pattern.combo != NULL)
		gtk_widget_destroy (state->fill.pattern.combo);

	state->fill.pattern.combo = combo = go_pattern_selector (
		gog_style_get_fill_color (style, 1),
		gog_style_get_fill_color (style, 2),
		default_style->fill.u.pattern.pat.pattern);

	table = glade_xml_get_widget (state->gui, "fill_pattern_table");
	gtk_table_attach (GTK_TABLE (table), combo, 1, 2, 0, 1, 0, 0, 0, 0);
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (glade_xml_get_widget (state->gui, "fill_pattern_type_label")), combo);

	if (style->fill.type == GOG_FILL_STYLE_PATTERN)
		type = style->fill.u.pattern.pat.pattern;
	go_combo_pixmaps_select_id (GO_COMBO_PIXMAPS(combo), type);
	g_signal_connect (G_OBJECT (combo),
		"changed",
		G_CALLBACK (cb_pattern_type_changed), state);
	gtk_widget_show (combo);
}

static void
cb_fg_color_changed (G_GNUC_UNUSED GOComboColor *cc, GOColor color,
		     G_GNUC_UNUSED gboolean is_custom,
		     G_GNUC_UNUSED gboolean by_user,
		     gboolean is_default, StylePrefState *state)
{
	GogStyle *style = state->style;

	g_return_if_fail (style != NULL);
	g_return_if_fail (GOG_FILL_STYLE_PATTERN == style->fill.type);

	style->fill.u.pattern.pat.fore = color;
	style->fill.pattern_fore_auto = is_default;
	style->fill.is_auto = (style->fill.pattern_fore_auto &
			       style->fill.pattern_back_auto);
	set_style (state);
	populate_pattern_combo (state);
}

static void
cb_bg_color_changed (G_GNUC_UNUSED GOComboColor *cc, GOColor color,
		     G_GNUC_UNUSED gboolean is_custom,
		     G_GNUC_UNUSED gboolean by_user,
		     gboolean is_default, StylePrefState *state)
{
	GogStyle *style = state->style;

	g_return_if_fail (style != NULL);
	g_return_if_fail (GOG_FILL_STYLE_PATTERN == style->fill.type);

	style->fill.u.pattern.pat.back = color;
	style->fill.pattern_back_auto = is_default;
	style->fill.is_auto = (style->fill.pattern_fore_auto &
			       style->fill.pattern_back_auto);
	set_style (state);
	populate_pattern_combo (state);
}

static void
fill_pattern_init (StylePrefState *state)
{
	GogStyle *style = state->style;
	GogStyle *default_style = state->default_style;

	GtkWidget *w, *table =
		glade_xml_get_widget (state->gui, "fill_pattern_table");

	state->fill.pattern.fore = w = create_go_combo_color (state,
		gog_style_get_fill_color (style, 1),
		gog_style_get_fill_color (default_style, 1),
		"pattern_foreground", "fill_pattern_foreground_label",
		G_CALLBACK (cb_fg_color_changed));
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 1, 2, 0, 0, 0, 0);

	state->fill.pattern.back = w = create_go_combo_color (state,
		gog_style_get_fill_color (style, 2),
		gog_style_get_fill_color (default_style, 2),
		"pattern_background", "fill_pattern_background_label",
		G_CALLBACK (cb_bg_color_changed));
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 2, 3, 0, 0, 0, 0);

	populate_pattern_combo (state);
	gtk_widget_show_all (table);
}

/************************************************************************/

static GOGradientDirection default_to_last_selected_type = GO_GRADIENT_N_TO_S;
static void
cb_gradient_type_changed (GtkWidget *cc, int id, StylePrefState const *state)
{
	GogStyle *style = state->style;
	style->fill.u.gradient.dir = default_to_last_selected_type = id;
	set_style (state);
}

static void
populate_gradient_combo (StylePrefState *state)
{
	GogStyle *style = state->style;
	GtkWidget *combo, *table;

	if (state->fill.gradient.combo != NULL)
		gtk_widget_destroy (state->fill.gradient.combo);

	state->fill.gradient.combo = combo = go_gradient_selector (
		gog_style_get_fill_color (style, 1),
		gog_style_get_fill_color (style, 2));
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (glade_xml_get_widget (state->gui, "fill_gradient_direction_label")), combo);

	table = glade_xml_get_widget (state->gui, "fill_gradient_table");
	gtk_table_attach (GTK_TABLE (table), combo, 1, 2, 0, 1, 0, 0, 0, 0);
	go_combo_pixmaps_select_id (GO_COMBO_PIXMAPS (combo),
		(style->fill.type == GOG_FILL_STYLE_GRADIENT)
			? style->fill.u.gradient.dir : default_to_last_selected_type);

	g_signal_connect (G_OBJECT (combo),
		"changed",
		G_CALLBACK (cb_gradient_type_changed), state);
	gtk_widget_show (combo);
}

static void
cb_fill_gradient_start_color (G_GNUC_UNUSED GOComboColor *cc, GOColor  color,
			      G_GNUC_UNUSED gboolean is_custom,
			      G_GNUC_UNUSED gboolean by_user,
			      gboolean is_default, StylePrefState *state)
{
	GogStyle *style = state->style;
	style->fill.u.gradient.start = color;
	style->fill.gradient_start_auto = is_default;
	style->fill.is_auto = (style->fill.gradient_start_auto &
			       style->fill.gradient_end_auto);
	set_style (state);
	populate_gradient_combo (state);
}

static gboolean
cb_delayed_gradient_combo_update (StylePrefState *state)
{
	state->fill.gradient.timer = 0;
	populate_gradient_combo (state);
	return FALSE;
}

static void
cb_fill_gradient_end_color (G_GNUC_UNUSED GOComboColor *cc, GOColor color,
			    G_GNUC_UNUSED gboolean is_custom,
			    G_GNUC_UNUSED gboolean by_user,
			    gboolean is_default, StylePrefState *state)
{
	GogStyle *style = state->style;

	style->fill.u.gradient.end = color;
	style->fill.gradient_end_auto = is_default;
	style->fill.is_auto = (style->fill.gradient_start_auto &
			       style->fill.gradient_end_auto);
	set_style (state);

	if (by_user)
		populate_gradient_combo (state);
	else {
		if (state->fill.gradient.timer != 0)
			g_source_remove (state->fill.gradient.timer);
		state->fill.gradient.timer = g_timeout_add (100,
			(GSourceFunc) cb_delayed_gradient_combo_update, state);
	}
}

static void
cb_gradient_brightness_value_changed (GtkWidget *w, StylePrefState *state)
{
	GogStyle *style = state->style;

	gog_style_set_fill_brightness (style,
		gtk_range_get_value (GTK_RANGE (w)));
	go_combo_color_set_gocolor (GO_COMBO_COLOR (state->fill.gradient.end),
		style->fill.u.gradient.end);
	set_style (state);
}

static void
cb_gradient_style_changed (GtkWidget *w, StylePrefState *state)
{
	GogStyle *style = state->style;

	GtkWidget *val = glade_xml_get_widget (state->gui,
		"fill_gradient_brightness");
	GtkWidget *box = glade_xml_get_widget (state->gui,
		"fill_gradient_brightness_box");

	gboolean two_color = gtk_combo_box_get_active (GTK_COMBO_BOX (w)) == 0;

	if (two_color) {
		style->fill.u.gradient.brightness = -1;
		gtk_widget_hide (box);
	} else {
		gtk_widget_show (box);
		gog_style_set_fill_brightness (style,
			gtk_range_get_value (GTK_RANGE (val)));
		go_combo_color_set_gocolor (GO_COMBO_COLOR (state->fill.gradient.end),
			style->fill.u.gradient.end);
	}
	gtk_widget_set_sensitive (state->fill.gradient.end, two_color);
	gtk_widget_set_sensitive (state->fill.gradient.end_label, two_color);
	set_style (state);
}

static void
fill_gradient_init (StylePrefState *state)
{
	GogStyle *style = state->style;
	GogStyle *default_style = state->default_style;
	GtkWidget *w, *table = glade_xml_get_widget (state->gui, "fill_gradient_table");
	GtkWidget *type = glade_xml_get_widget (state->gui, "fill_gradient_type");

	state->fill.gradient.start = w = create_go_combo_color (state,
		gog_style_get_fill_color (style, 1),
		gog_style_get_fill_color (default_style, 1),
		"gradient_start", "fill_gradient_start_label",
		G_CALLBACK (cb_fill_gradient_start_color));
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 2, 3, 0, 0, 0, 0);
	gtk_widget_show (w);

	state->fill.gradient.end = w = create_go_combo_color (state,
		gog_style_get_fill_color (style, 2),
		gog_style_get_fill_color (default_style, 2),
		"gradient_end", "fill_gradient_end_label",
		G_CALLBACK (cb_fill_gradient_end_color));
	gtk_table_attach (GTK_TABLE (table), w, 3, 4, 2, 3, 0, 0, 0, 0);
	gtk_widget_show (w);

	state->fill.gradient.end_label = glade_xml_get_widget (state->gui,
		"fill_gradient_end_label");
	state->fill.gradient.brightness = glade_xml_get_widget (state->gui,
		"fill_gradient_brightness");
	state->fill.gradient.brightness_box = glade_xml_get_widget (state->gui,
		"fill_gradient_brightness_box");

	if ((style->fill.type != GOG_FILL_STYLE_GRADIENT) ||
	    (style->fill.u.gradient.brightness < 0)) {
		gtk_combo_box_set_active (GTK_COMBO_BOX (type), 0);
		gtk_widget_hide (state->fill.gradient.brightness_box);
	} else {
		gtk_combo_box_set_active (GTK_COMBO_BOX (type), 1);
		gtk_widget_show (state->fill.gradient.brightness_box);
		gtk_range_set_value (GTK_RANGE (state->fill.gradient.brightness),
			style->fill.u.gradient.brightness);
		gtk_widget_set_sensitive (state->fill.gradient.end, FALSE);
		gtk_widget_set_sensitive (state->fill.gradient.end_label, FALSE);
	}

	g_signal_connect (G_OBJECT (type),
		"changed",
		G_CALLBACK (cb_gradient_style_changed), state);
	g_signal_connect (G_OBJECT (state->fill.gradient.brightness),
		"value_changed",
		G_CALLBACK (cb_gradient_brightness_value_changed), state);

	populate_gradient_combo (state);
	gtk_widget_show (table);
}

/************************************************************************/

static void
cb_image_file_select (GtkWidget *cc, StylePrefState *state)
{
	GogStyle *style = state->style;
	char *filename, *uri, *old_uri;
	GtkWidget *w;

	g_return_if_fail (style != NULL);
	g_return_if_fail (GOG_FILL_STYLE_IMAGE == style->fill.type);

	filename = style->fill.u.image.filename;
	old_uri = filename ? go_filename_to_uri (filename) : NULL;
	uri = gui_image_file_select (NULL, old_uri, FALSE);
	g_free (old_uri);
	if (uri == NULL)
		return;
	filename = go_filename_from_uri (uri);
	g_free (uri);
	if (filename == NULL) {
		g_warning ("Sorry -- cannot handle URIs here right now.");
		return;
	}
#warning "Handle URIs here."

	gog_style_set_fill_image_filename (style, filename);

	w = glade_xml_get_widget (state->gui, "fill_image_sample");
	g_object_set_data (G_OBJECT (w), "filename",
			   style->fill.u.image.filename);

	gog_style_set_image_preview (style->fill.u.image.image, state);
	set_style (state);
}

static void
cb_image_style_changed (GtkWidget *w, StylePrefState *state)
{
	GogStyle *style = state->style;
	g_return_if_fail (style != NULL);
	g_return_if_fail (GOG_FILL_STYLE_IMAGE == style->fill.type);
	style->fill.u.image.type = gtk_combo_box_get_active (GTK_COMBO_BOX (w));
	set_style (state);
}

static void
fill_image_init (StylePrefState *state)
{
	GtkWidget *w, *sample, *type;
	GogStyle *style = state->style;

	w = glade_xml_get_widget (state->gui, "fill_image_select_picture");
	g_signal_connect (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_image_file_select), state);

	sample = glade_xml_get_widget (state->gui, "fill_image_sample");
	gtk_widget_set_size_request (sample, HSCALE + 10, VSCALE + 10);
	type   = glade_xml_get_widget (state->gui, "fill_image_fit");

	state->fill.image.image = NULL;

	if (GOG_FILL_STYLE_IMAGE == style->fill.type) {
		gtk_combo_box_set_active (GTK_COMBO_BOX (type),
			style->fill.u.image.type);
		gog_style_set_image_preview (style->fill.u.image.image, state);
		state->fill.image.image = style->fill.u.image.image;
		if (state->fill.image.image)
			g_object_ref (state->fill.image.image);
		g_object_set_data (G_OBJECT (sample), "filename",
				   style->fill.u.image.filename);
	} else
		gtk_combo_box_set_active (GTK_COMBO_BOX (type), 0);
	g_signal_connect (G_OBJECT (type),
		"changed",
		G_CALLBACK (cb_image_style_changed), state);
}

/************************************************************************/

static void
cb_fill_type_changed (GtkWidget *menu, StylePrefState *state)
{
	GogStyle *style = state->style;
	GtkWidget *w;
	unsigned page;

	page = gtk_combo_box_get_active (GTK_COMBO_BOX (menu));

	if (page != style->fill.type &&
	    GOG_FILL_STYLE_IMAGE == style->fill.type &&
	    style->fill.u.image.image)
		g_object_unref (style->fill.u.image.image);

	switch (page) {
	case GOG_FILL_STYLE_NONE:
		break;
	case GOG_FILL_STYLE_PATTERN:
		style->fill.u.pattern.pat.fore = go_combo_color_get_color (
			GO_COMBO_COLOR (state->fill.pattern.fore), FALSE);
		style->fill.u.pattern.pat.back = go_combo_color_get_color (
			GO_COMBO_COLOR (state->fill.pattern.back), FALSE);
		style->fill.u.pattern.pat.pattern = go_combo_pixmaps_get_selected (
			(GOComboPixmaps*)state->fill.pattern.combo, NULL);
		break;

	case GOG_FILL_STYLE_GRADIENT:
		style->fill.u.gradient.start = go_combo_color_get_color (
			GO_COMBO_COLOR (state->fill.gradient.start), FALSE);
		style->fill.u.gradient.end = go_combo_color_get_color (
			GO_COMBO_COLOR (state->fill.gradient.end), FALSE);
		style->fill.u.gradient.dir = go_combo_pixmaps_get_selected (
			(GOComboPixmaps*)state->fill.gradient.combo, NULL);
		w = glade_xml_get_widget (state->gui, "fill_gradient_type");
		if (gtk_combo_box_get_active (GTK_COMBO_BOX (w))) {
			w = glade_xml_get_widget (state->gui, "fill_gradient_brightness");
			style->fill.u.gradient.brightness = gtk_range_get_value (GTK_RANGE (w));
		} else {
			style->fill.u.gradient.brightness = -1.;
		}
		break;

	case GOG_FILL_STYLE_IMAGE:
		w = glade_xml_get_widget (state->gui, "fill_image_sample");
		style->fill.u.image.image = state->fill.image.image;
		if (NULL != style->fill.u.image.image)
			g_object_ref (style->fill.u.image.image);
		style->fill.u.image.filename = g_object_get_data (G_OBJECT (w), "filename");
		w = glade_xml_get_widget (state->gui, "fill_image_fit");
		style->fill.u.image.type = gtk_combo_box_get_active (GTK_COMBO_BOX (w));
		break;
	}
	style->fill.type = page;
	set_style (state);

	w = glade_xml_get_widget (state->gui, "fill_notebook");
	gtk_notebook_set_current_page (GTK_NOTEBOOK (w), page);
}

static void
fill_init (StylePrefState *state, gboolean enable)
{
	GtkWidget *w;

	if (!enable) {
		gtk_widget_hide (glade_xml_get_widget (state->gui, "fill_outer_table"));
		return;
	}

	fill_pattern_init (state);
	fill_gradient_init (state);
	fill_image_init (state);

	w = glade_xml_get_widget (state->gui, "fill_notebook");
	gtk_notebook_set_current_page (GTK_NOTEBOOK (w), state->style->fill.type);
	w = glade_xml_get_widget (state->gui, "fill_type_menu");
	gtk_combo_box_set_active (GTK_COMBO_BOX (w), state->style->fill.type);
	g_signal_connect (G_OBJECT (w),
		"changed",
		G_CALLBACK (cb_fill_type_changed), state);

	w = glade_xml_get_widget (state->gui, "fill_outer_table");
	gtk_widget_show (GTK_WIDGET (w));
}

/************************************************************************/


static void
cb_marker_shape_changed (GtkWidget *cc, int shape, StylePrefState const *state)
{
	GogStyle *style = state->style;
	gboolean is_auto = shape < 0;

	if (is_auto)
		shape = -shape;
	go_marker_set_shape (style->marker.mark, shape);
	style->marker.auto_shape = is_auto;
	set_style (state);
}

static void
populate_marker_combo (StylePrefState *state)
{
	GogStyle *style = state->style;
	GtkWidget *combo, *table;

	if (state->marker.combo != NULL)
		gtk_widget_destroy (state->marker.combo);

	state->marker.combo = combo = go_marker_selector (
	        go_marker_get_outline_color (style->marker.mark),
		go_marker_get_fill_color (style->marker.mark),
		go_marker_get_shape (state->default_style->marker.mark));
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (glade_xml_get_widget (state->gui, "marker_shape_label")), combo);

	table = glade_xml_get_widget (state->gui, "marker_table");
	gtk_table_attach (GTK_TABLE (table), combo, 1, 2, 0, 1, 0, 0, 0, 0);
	go_combo_pixmaps_select_id (GO_COMBO_PIXMAPS (combo),
		go_marker_get_shape (style->marker.mark));
	g_signal_connect (G_OBJECT (combo),
		"changed",
		G_CALLBACK (cb_marker_shape_changed), state);
	gtk_widget_show (combo);
}

static void
cb_marker_outline_color_changed (G_GNUC_UNUSED GOComboColor *cc, GOColor color,
				 G_GNUC_UNUSED gboolean is_custom,
				 G_GNUC_UNUSED gboolean by_user,
				 gboolean is_auto, StylePrefState *state)
{
	GogStyle *style = state->style;
	if (is_auto)
		color = go_marker_get_outline_color (state->default_style->marker.mark);
	go_marker_set_outline_color (style->marker.mark, color);
	style->marker.auto_outline_color = is_auto;
	set_style (state);
	populate_marker_combo (state);
}

static void
cb_marker_fill_color_changed (G_GNUC_UNUSED GOComboColor *cc, GOColor color,
			      G_GNUC_UNUSED gboolean is_custom,
			      G_GNUC_UNUSED gboolean by_user,
			      gboolean is_auto, StylePrefState *state)
{
	GogStyle *style = state->style;
	if (is_auto)
		color = go_marker_get_fill_color (state->default_style->marker.mark);
	go_marker_set_fill_color (style->marker.mark, color);
	style->marker.auto_fill_color = is_auto;
	set_style (state);
	populate_marker_combo (state);
}

static void
cb_marker_size_changed (GtkAdjustment *adj, StylePrefState *state)
{
	go_marker_set_size (state->style->marker.mark, adj->value);
	set_style (state);
}

static void
marker_init (StylePrefState *state, gboolean enable)
{
	GogStyle *style = state->style;
	GogStyle *default_style = state->default_style;
	GtkWidget *table, *w;

	if (!enable) {
		gtk_widget_hide (glade_xml_get_widget (state->gui, "marker_outer_table"));
		return;
	}

	populate_marker_combo (state);
	table = glade_xml_get_widget (state->gui, "marker_table");

	w = create_go_combo_color (state,
		go_marker_get_fill_color (style->marker.mark),
		go_marker_get_fill_color (default_style->marker.mark),
		"pattern_foreground", "marker_fill_label",
		G_CALLBACK (cb_marker_fill_color_changed));
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 1, 2, 0, 0, 0, 0);

	w = create_go_combo_color (state,
		go_marker_get_outline_color (style->marker.mark),
		go_marker_get_outline_color (default_style->marker.mark),
		"pattern_foreground", "marker_outline_label",
		G_CALLBACK (cb_marker_outline_color_changed));
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 2, 3, 0, 0, 0, 0);

	w = glade_xml_get_widget (state->gui, "marker_size_spin");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (w),
		go_marker_get_size (style->marker.mark));
	g_signal_connect (G_OBJECT (gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (w))),
		"value_changed",
		G_CALLBACK (cb_marker_size_changed), state);

	gtk_widget_show_all (table);
}

/************************************************************************/

static void
cb_font_changed (FontSelector *fs, G_GNUC_UNUSED gpointer mstyle,
		 StylePrefState *state)
{
	GogStyle *style = state->style;
	PangoFontDescription *new_font = pango_font_description_copy (style->font.font->desc);

	font_selector_get_pango (fs, new_font);
	gog_style_set_font (style, new_font);
	set_style (state);
}

static void
font_init (StylePrefState *state, guint32 enable, gpointer optional_notebook)
{
	GogStyle *style = state->style;
	GtkWidget *w, *box;

	if (!enable)
		return;

	g_return_if_fail (style->font.font != NULL);
	g_return_if_fail (GTK_NOTEBOOK (optional_notebook) != NULL);

	box = gtk_vbox_new (FALSE, 5);

#if 0
	w = gtk_check_button_new_with_label (_("Automatic"));
	gtk_box_pack_start (GTK_BOX (box), w, FALSE, TRUE, 0);
#endif
	gtk_widget_show_all (box);

	w = font_selector_new ();
	font_selector_set_from_pango  (FONT_SELECTOR (w), style->font.font->desc);
	g_signal_connect (G_OBJECT (w),
		"font_changed",
		G_CALLBACK (cb_font_changed), state);
	gtk_box_pack_end (GTK_BOX (box), w, TRUE, TRUE, 0);
	gtk_widget_show (w);

	gtk_notebook_prepend_page (GTK_NOTEBOOK (optional_notebook), box,
		gtk_label_new (_("Font")));
	gtk_widget_show (GTK_WIDGET (optional_notebook));
}

/************************************************************************/

static void
cb_parent_is_gone (StylePrefState *state, GObject *where_the_object_was)
{
	state->style_changed_handler = 0;
	state->parent = NULL;
}

static void
gog_style_pref_state_free (StylePrefState *state)
{
	if (state->style_changed_handler) {
		g_signal_handler_disconnect (state->parent,
			state->style_changed_handler);
		g_object_weak_unref (G_OBJECT (state->parent),
			(GWeakNotify) cb_parent_is_gone, state);
	}
	g_object_unref (state->style);
	g_object_unref (state->default_style);
	g_object_unref (state->gui);
	if (state->fill.gradient.timer != 0) {
		g_source_remove (state->fill.gradient.timer);
		state->fill.gradient.timer = 0;
	}
	if (state->fill.image.image != NULL)
		g_object_unref (state->fill.image.image);
	g_free (state);
}

static gpointer
style_editor (GogStyle *style,
	      GogStyle *default_style,
	      GnmCmdContext *cc,
	      gpointer optional_notebook,
	      GogStyledObject *parent)
{
	GogStyleFlag enable;
	GtkWidget *w;
	GladeXML *gui;
	StylePrefState *state;

	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (default_style != NULL, NULL);

	enable = style->interesting_fields;

	gui = gnm_glade_xml_new (cc, "gog-style-prefs.glade", "gog_style_prefs", NULL);
	if (gui == NULL)
		return NULL;

	g_object_ref (style);
	g_object_ref (default_style);

	state = g_new0 (StylePrefState, 1);
	state->gui = gui;
	state->style = style;
	state->default_style = default_style;
	state->parent = parent;
	state->enable_edit = FALSE;

	outline_init (state, enable & GOG_STYLE_OUTLINE);
	line_init    (state, enable & GOG_STYLE_LINE);
	fill_init    (state, enable & GOG_STYLE_FILL);
	marker_init  (state, enable & GOG_STYLE_MARKER);
	font_init    (state, enable & GOG_STYLE_FONT, optional_notebook);

	state->enable_edit = TRUE;

	if (parent != NULL) {
		state->style_changed_handler = g_signal_connect (G_OBJECT (parent),
			"style-changed",
			G_CALLBACK (cb_style_changed), state);

		g_object_weak_ref (G_OBJECT (parent),
			(GWeakNotify) cb_parent_is_gone, state);
	}

 	w = glade_xml_get_widget (gui, "gog_style_prefs");
	g_object_set_data_full (G_OBJECT (w),
		"state", state, (GDestroyNotify) gog_style_pref_state_free);

	if (optional_notebook != NULL) {
		gtk_notebook_prepend_page (GTK_NOTEBOOK (optional_notebook), w,
					   gtk_label_new (_("Style")));
		return GTK_WIDGET (optional_notebook);
	}
	return w;
}

gpointer
gog_style_editor (GogStyle *style,
		  GogStyle *default_style,
		  GnmCmdContext *cc,
		  gpointer optional_notebook)
{
	return style_editor (style, default_style,
			     cc, optional_notebook,
			     NULL);
}

gpointer
gog_styled_object_editor (GogStyledObject *gso, GnmCmdContext *cc, gpointer optional_notebook)
{
	gpointer editor;

	GogStyle *style = gog_style_dup (gog_styled_object_get_style (gso));
	GogStyle *default_style = gog_styled_object_get_auto_style (gso);

	editor = style_editor (style, default_style, cc, optional_notebook, gso);

	g_object_unref (style);
	g_object_unref (default_style);

	return editor;
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
	GogStyle *dst;

	g_return_val_if_fail (GOG_STYLE (src) != NULL, NULL);

	dst = gog_style_new ();
	gog_style_assign (dst, src);
	return dst;
}

void
gog_style_assign (GogStyle *dst, GogStyle const *src)
{
	if (src == dst)
		return;

	g_return_if_fail (GOG_STYLE (src) != NULL);
	g_return_if_fail (GOG_STYLE (dst) != NULL);

	if (GOG_FILL_STYLE_IMAGE == src->fill.type &&
	    src->fill.u.image.image != NULL)
		g_object_ref (src->fill.u.image.image);
	if (GOG_FILL_STYLE_IMAGE == dst->fill.type) {
		if (dst->fill.u.image.image != NULL)
			g_object_unref (dst->fill.u.image.image);
		g_free (dst->fill.u.image.filename);
	}

	if (src->font.font != NULL)
		go_font_ref (src->font.font);
	if (dst->font.font != NULL)
		go_font_unref (dst->font.font);

	dst->outline = src->outline;
	dst->fill    = src->fill;
	dst->line    = src->line;
	if (dst->marker.mark)
		g_object_unref (dst->marker.mark);
	dst->marker = src->marker;
	dst->marker.mark = go_marker_dup (src->marker.mark);
	dst->font    = src->font;
	dst->line    = src->line;

	if (GOG_FILL_STYLE_IMAGE == dst->fill.type)
		dst->fill.u.image.filename = g_strdup (dst->fill.u.image.filename);

	dst->interesting_fields = src->interesting_fields;
	dst->needs_obj_defaults = src->needs_obj_defaults;
}

/**
 * gog_style_apply_theme :
 * @dst : #GogStyle
 * @src :  #GogStyle
 *
 * Merge the attributes from @src onto the elements of @dst that were not user
 * assigned (is_auto)
 **/
void
gog_style_apply_theme (GogStyle *dst, GogStyle const *src)
{
	if (src == dst)
		return;

	g_return_if_fail (GOG_STYLE (src) != NULL);
	g_return_if_fail (GOG_STYLE (dst) != NULL);

	if (dst->outline.auto_color)
		dst->outline.color = src->outline.color;
	if (dst->fill.is_auto) {
		switch (dst->fill.type) {
		case GOG_FILL_STYLE_PATTERN:
			dst->fill.u.pattern.pat.fore
				= src->fill.u.pattern.pat.fore;
			dst->fill.u.pattern.pat.back
				= src->fill.u.pattern.pat.back;
			break;
		case GOG_FILL_STYLE_GRADIENT:
			dst->fill.u.gradient.start
				= src->fill.u.gradient.start;
			dst->fill.u.gradient.end
				= src->fill.u.gradient.end;
		default:
			break;
		}
	}
	if (dst->line.auto_color)
		dst->line.color = src->line.color;
	if (dst->marker.auto_shape)
		go_marker_set_shape (dst->marker.mark,
			go_marker_get_shape (src->marker.mark));
	if (dst->marker.auto_outline_color)
		go_marker_set_outline_color (dst->marker.mark,
			go_marker_get_outline_color (src->marker.mark));
	if (dst->marker.auto_fill_color)
		go_marker_set_fill_color (dst->marker.mark,
			go_marker_get_fill_color (src->marker.mark));

#if 0
	/* Fonts are not themed until we have some sort of auto mechanism
	 * stronger than 'auto_size' */
	if (src->font.font != NULL)
		go_font_ref (src->font.font);
	if (dst->font.font != NULL)
		go_font_unref (dst->font.font);
	dst->font = src->font;
#endif
}

static void
gog_style_finalize (GObject *obj)
{
	GogStyle *style = GOG_STYLE (obj);

	if (GOG_FILL_STYLE_IMAGE == style->fill.type &&
	    style->fill.u.image.image != NULL)
		g_object_unref (style->fill.u.image.image);

	if (style->font.font != NULL) {
		go_font_unref (style->font.font);
		style->font.font = NULL;
	}

	if (style->marker.mark != NULL) {
		g_object_unref (style->marker.mark);
		style->marker.mark = NULL;
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
	style->interesting_fields = GOG_STYLE_ALL;
	style->needs_obj_defaults = TRUE;
	style->marker.mark = go_marker_new ();
	style->marker.auto_shape =
	style->marker.auto_outline_color =
	style->marker.auto_fill_color =
	style->outline.auto_color =
	style->line.auto_color =
	style->fill.is_auto =
	style->fill.pattern_fore_auto =
	style->fill.pattern_back_auto =
	style->fill.gradient_start_auto =
	style->fill.gradient_end_auto = TRUE;
	style->fill.type = GOG_FILL_STYLE_PATTERN;
	go_pattern_set_solid (&style->fill.u.pattern.pat, 0);

	style->font.auto_scale = TRUE;
	style->font.font = go_font_new_by_index (0);
	style->font.color = RGBA_BLACK;
}

static struct {
	GogFillStyle fstyle;
	char const *name;
} fill_names[] = {
	{ GOG_FILL_STYLE_NONE,     "none" },
	{ GOG_FILL_STYLE_PATTERN,  "pattern" },
	{ GOG_FILL_STYLE_GRADIENT, "gradient" },
	{ GOG_FILL_STYLE_IMAGE,    "image" }
};

static gboolean
bool_prop (xmlNode *node, char const *name, gboolean *res)
{
	char *str = xmlGetProp (node, name);
	if (str != NULL) {
		*res = g_ascii_tolower (*str) == 't' ||
			g_ascii_tolower (*str) == 'y' ||
			strtol (str, NULL, 0);
		xmlFree (str);
		return TRUE;
	}
	return FALSE;
}

static GogFillStyle
str_as_fill_style (char const *name)
{
	unsigned i;
	for (i = 0; i < G_N_ELEMENTS (fill_names); i++)
		if (strcmp (fill_names[i].name, name) == 0)
			return fill_names[i].fstyle;
	return GOG_FILL_STYLE_PATTERN;
}

static char const *
fill_style_as_str (GogFillStyle fstyle)
{
	unsigned i;
	for (i = 0; i < G_N_ELEMENTS (fill_names); i++)
		if (fill_names[i].fstyle == fstyle)
			return fill_names[i].name;
	return "pattern";
}

static void
gog_style_line_load (xmlNode *node, GogStyleLine *line)
{
	char *str;
	gboolean tmp;

	str = xmlGetProp (node, "width");
	if (str != NULL) {
		line->width = g_strtod (str, NULL);
		xmlFree (str);
	}
	str = xmlGetProp (node, "color");
	if (str != NULL) {
		line->color = go_color_from_str (str);
		xmlFree (str);
	}
	if (bool_prop (node, "auto-color", &tmp))
		line->auto_color = tmp;
}

static void
gog_style_line_dom_save (xmlNode *parent, xmlChar const *name,
			 GogStyleLine const *line)
{
	gchar *str;
	xmlNode *node = xmlNewDocNode (parent->doc, NULL, name, NULL);

	str = g_strdup_printf ("%f",  line->width);
	xmlSetProp (node, (xmlChar const *) "width", str);
	g_free (str);
	str = go_color_as_str (line->color);
	xmlSetProp (node, (xmlChar const *) "color", str);
	g_free (str);
	xmlSetProp (node, (xmlChar const *) "auto-color",
		    line->auto_color ? "true" : "false");
	xmlAddChild (parent, node);
}
static void
gog_style_line_sax_save (GsfXMLOut *output, char const *name,
			 GogStyleLine const *line)
{
	gsf_xml_out_start_element (output, name);
	gsf_xml_out_add_float (output, "width", line->width, 1);
	go_xml_out_add_color (output, "color", line->color);
	gsf_xml_out_add_bool (output, "auto-color", line->auto_color);
	gsf_xml_out_end_element (output);
}

static void
gog_style_gradient_sax_save (GsfXMLOut *output, GogStyle const *style)
{
	gsf_xml_out_start_element (output, "gradient");
	gsf_xml_out_add_cstr_unchecked (output, "direction",
		    go_gradient_dir_as_str (style->fill.u.gradient.dir));
	go_xml_out_add_color (output, "start-color",
		style->fill.u.gradient.start);
	if (style->fill.u.gradient.brightness > 0)
		gsf_xml_out_add_float (output, "brightness",
			style->fill.u.gradient.brightness, 2);
	else 
		go_xml_out_add_color (output, "end-color",
			style->fill.u.gradient.end);
	gsf_xml_out_end_element (output);
}

static void
gog_style_fill_sax_save (GsfXMLOut *output, GogStyle const *style)
{
	gsf_xml_out_start_element (output, "fill");
	gsf_xml_out_add_cstr_unchecked (output, "type",
		    fill_style_as_str (style->fill.type));
	gsf_xml_out_add_bool (output, "is-auto", style->fill.is_auto);

	switch (style->fill.type) {
	case GOG_FILL_STYLE_NONE: break;
	case GOG_FILL_STYLE_PATTERN:
		gsf_xml_out_start_element (output, "pattern");
		gsf_xml_out_add_cstr_unchecked (output, "type",
			go_pattern_as_str (style->fill.u.pattern.pat.pattern));
		go_xml_out_add_color (output, "fore",
			style->fill.u.pattern.pat.fore);
		go_xml_out_add_color (output, "back",
			style->fill.u.pattern.pat.back);
		gsf_xml_out_end_element (output);
		break;

	case GOG_FILL_STYLE_GRADIENT:
		gog_style_gradient_sax_save (output, style);
		break;
	case GOG_FILL_STYLE_IMAGE:
		/* FIXME: TODO */
		break;
	default:
		break;
	}
	gsf_xml_out_end_element (output);
}

static void
gog_style_gradient_load (xmlNode *node, GogStyle *style)
{
	char    *str = xmlGetProp (node, "direction");
	if (str != NULL) {
		style->fill.u.gradient.dir
			= go_gradient_dir_from_str (str);
		xmlFree (str);
	}
	str = xmlGetProp (node, "brightness");
	if (str != NULL) {
		style->fill.u.gradient.brightness
			= g_strtod (str, NULL);
		xmlFree (str);
	} else {
		str = xmlGetProp (node, "start-color");
		if (str != NULL) {
			style->fill.u.gradient.start
				= go_color_from_str (str);
			xmlFree (str);
		}
		str = xmlGetProp (node, "end-color");
		if (str != NULL) {
			style->fill.u.gradient.end
				= go_color_from_str (str);
			xmlFree (str);
		}
	}
}

static void
gog_style_gradient_dom_save (xmlNode *parent, GogStyle const *style)
{
	gchar *str;
	xmlNode *node =  xmlNewDocNode (parent->doc, NULL, "gradient", NULL);

	xmlSetProp (node, (xmlChar const *) "direction",
		    go_gradient_dir_as_str (style->fill.u.gradient.dir));
	str = go_color_as_str (style->fill.u.gradient.start);
	xmlSetProp (node, (xmlChar const *) "start-color", str);
	g_free (str);
	if (style->fill.u.gradient.brightness >= 0.) {
		str = g_strdup_printf ("%f",
				       style->fill.u.gradient.brightness);
		xmlSetProp (node, (xmlChar const *) "brightness", str);
		g_free (str);
	} else {
		str = go_color_as_str (style->fill.u.gradient.end);
		xmlSetProp (node, (xmlChar const *) "end-color", str);
		g_free (str);
	}
	xmlAddChild (parent, node);
}

static void
gog_style_fill_load (xmlNode *node, GogStyle *style)
{
	xmlNode *ptr;
	gboolean tmp;
	char    *str = xmlGetProp (node, "type");

	if (str == NULL)
		return;
	style->fill.type = str_as_fill_style (str);
	xmlFree (str);

	if (bool_prop (node, "is-auto", &tmp))
		style->fill.is_auto = tmp;

	switch (style->fill.type) {
	case GOG_FILL_STYLE_PATTERN:
		for (ptr = node->xmlChildrenNode ;
		     ptr != NULL ; ptr = ptr->next) {
			if (xmlIsBlankNode (ptr) || ptr->name == NULL)
				continue;
			if (strcmp (ptr->name, "pattern") == 0) {
				str = xmlGetProp (ptr, "type");
				if (str != NULL) {
					style->fill.u.pattern.pat.pattern
						= go_pattern_from_str (str);
					xmlFree (str);
				}
				str = xmlGetProp (ptr, "fore");
				if (str != NULL) {
					style->fill.u.pattern.pat.fore
						 = go_color_from_str (str);
					xmlFree (str);
				}
				str = xmlGetProp (ptr, "back");
				if (str != NULL) {
					style->fill.u.pattern.pat.back
						 = go_color_from_str (str);
					xmlFree (str);
				}
			}
		}
		break;
	case GOG_FILL_STYLE_GRADIENT:
		for (ptr = node->xmlChildrenNode ;
		     ptr != NULL ; ptr = ptr->next) {
			if (xmlIsBlankNode (ptr) || ptr->name == NULL)
				continue;
			if (strcmp (ptr->name, "gradient") == 0)
				gog_style_gradient_load (ptr, style);
		}
		break;
	case GOG_FILL_STYLE_IMAGE:
		/* FIXME: TODO */
		break;
	default:
		break;
	}
}

static void
gog_style_fill_dom_save (xmlNode *parent, GogStyle const *style)
{
	gchar *str;
	xmlNode *node = xmlNewDocNode (parent->doc, NULL, "fill", NULL);
	xmlNode *child;
	xmlSetProp (node, (xmlChar const *) "type",
		    fill_style_as_str (style->fill.type));
	xmlSetProp (node, (xmlChar const *) "is-auto",
		    style->fill.is_auto ? "true" : "false");
	switch (style->fill.type) {
	case GOG_FILL_STYLE_NONE:
		break;
	case GOG_FILL_STYLE_PATTERN:
		child =  xmlNewDocNode (parent->doc, NULL, "pattern", NULL);
		xmlSetProp (child, (xmlChar const *) "type",
			    go_pattern_as_str (style->fill.u.pattern.pat.pattern));
		str = go_color_as_str (style->fill.u.pattern.pat.fore);
		xmlSetProp (child, (xmlChar const *) "fore", str);
		g_free (str);
		str = go_color_as_str (style->fill.u.pattern.pat.back);
		xmlSetProp (child, (xmlChar const *) "back", str);
		g_free (str);
		xmlAddChild (node, child);
		break;
	case GOG_FILL_STYLE_GRADIENT:
		gog_style_gradient_dom_save (node, style);
		break;
	case GOG_FILL_STYLE_IMAGE:
		/* FIXME: TODO */
		break;
	default:
		break;
	}
	xmlAddChild (parent, node);
}

static void
gog_style_marker_load (xmlNode *node, GogStyle *style)
{
	char *str;
	GOMarker *marker = go_marker_dup (style->marker.mark);

	str = xmlGetProp (node, "shape");
	if (str != NULL) {
		style->marker.auto_shape = TRUE;
		bool_prop (node, "auto-shape", &style->marker.auto_shape);
		go_marker_set_shape (marker, go_marker_shape_from_str (str));
		xmlFree (str);
	}
	str = xmlGetProp (node, "outline-color");
	if (str != NULL) {
		style->marker.auto_outline_color = TRUE;
		bool_prop (node, "auto-outline", &style->marker.auto_outline_color);
		go_marker_set_outline_color (marker, go_color_from_str (str));
		xmlFree (str);
	}
	str = xmlGetProp (node, "fill-color");
	if (str != NULL) {
		style->marker.auto_fill_color = TRUE;
		bool_prop (node, "auto-fill", &style->marker.auto_fill_color);
		go_marker_set_fill_color (marker, go_color_from_str (str));
		xmlFree (str);
	}
	str = xmlGetProp (node, "size");
	if (str != NULL) {
		go_marker_set_size (marker, g_strtod (str, NULL));
		xmlFree (str);
	}
	gog_style_set_marker (style, marker);
}

static void
gog_style_marker_dom_save (xmlNode *parent, GogStyle const *style)
{
	gchar *str;
	xmlNode *node = xmlNewDocNode (parent->doc, NULL, "marker", NULL);

	xmlSetProp (node, (xmlChar const *) "auto-shape",
		style->marker.auto_shape ? "true" : "false");
	xmlSetProp (node, (xmlChar const *) "shape",
		go_marker_shape_as_str (go_marker_get_shape (style->marker.mark)));

	xmlSetProp (node, (xmlChar const *) "auto-outline",
		style->marker.auto_outline_color ? "true" : "false");
	str = go_color_as_str (go_marker_get_outline_color (style->marker.mark));
	xmlSetProp (node, (xmlChar const *) "outline-color", str);
	g_free (str);

	xmlSetProp (node, (xmlChar const *) "auto-fill",
		style->marker.auto_fill_color ? "true" : "false");
	str = go_color_as_str (go_marker_get_fill_color (style->marker.mark));
	xmlSetProp (node, (xmlChar const *) "fill-color", str);
	g_free (str);

	str = g_strdup_printf ("%d", go_marker_get_size (style->marker.mark));
	xmlSetProp (node, (xmlChar const *) "size", str);
	g_free (str);

	xmlAddChild (parent, node);
}

static void
gog_style_marker_sax_save (GsfXMLOut *output, GogStyle const *style)
{
	gsf_xml_out_start_element (output, "marker");
	gsf_xml_out_add_bool (output, "auto-shape", style->marker.auto_shape);
	gsf_xml_out_add_cstr (output, "shape",
		go_marker_shape_as_str (go_marker_get_shape (style->marker.mark)));
	gsf_xml_out_add_bool (output, "auto-outline", 
		style->marker.auto_outline_color);
	go_xml_out_add_color (output, "outline-color",
		go_marker_get_outline_color (style->marker.mark));
	gsf_xml_out_add_bool (output, "auto-fill", style->marker.auto_fill_color);
	go_xml_out_add_color (output, "fill-color",
		go_marker_get_fill_color (style->marker.mark));
	gsf_xml_out_add_int (output, "size",
		go_marker_get_size (style->marker.mark));
	gsf_xml_out_end_element (output);
}

static void
gog_style_font_load (xmlNode *node, GogStyle *style)
{
	char *str;
	gboolean tmp;

	str = xmlGetProp (node, "color");
	if (str != NULL) {
		style->font.color = go_color_from_str (str);
		xmlFree (str);
	}
	str = xmlGetProp (node, "font");
	if (str != NULL) {
		PangoFontDescription *desc;

		desc = pango_font_description_from_string (str);
		if (desc != NULL)
			gog_style_set_font (style, desc);
		xmlFree (str);
	}
	if (bool_prop (node, "auto-scale", &tmp))
		style->font.auto_scale = tmp;
}

static void
gog_style_font_dom_save (xmlNode *parent, GogStyle const *style)
{
	gchar *str;
	xmlNode *node = xmlNewDocNode (parent->doc, NULL, "font", NULL);

	str = go_color_as_str (style->font.color);
	xmlSetProp (node, (xmlChar const *) "color", str);
	g_free (str);
	str = go_font_as_str (style->font.font);
	xmlSetProp (node, (xmlChar const *) "font", str);
	g_free (str);
	xmlSetProp (node, (xmlChar const *) "auto-scale",
		    style->font.auto_scale ? "true" : "false");

	xmlAddChild (parent, node);
}
static void
gog_style_font_sax_save (GsfXMLOut *output, GogStyle const *style)
{
	char *str;
	gsf_xml_out_start_element (output, "font");
	go_xml_out_add_color (output, "color", style->font.color);
	str = go_font_as_str (style->font.font);
	gsf_xml_out_add_cstr_unchecked (output, "font", str);
	g_free (str);
	gsf_xml_out_add_bool (output, "auto-scale", style->font.auto_scale);
	gsf_xml_out_end_element (output);
}

static gboolean
gog_style_persist_dom_load (GogPersist *gp, xmlNode *node)
{
	GogStyle *style = GOG_STYLE (gp);
	xmlNode *ptr;

	/* while reloading no need to reapply settings */
	style->needs_obj_defaults = FALSE;
	for (ptr = node->xmlChildrenNode ; ptr != NULL ; ptr = ptr->next) {
		if (xmlIsBlankNode (ptr) || ptr->name == NULL)
			continue;
		if (strcmp (ptr->name, "outline") == 0)
			gog_style_line_load (ptr, &style->outline);
		else if (strcmp (ptr->name, "line") == 0)
			gog_style_line_load (ptr, &style->line);
		else if (strcmp (ptr->name, "fill") == 0)
			gog_style_fill_load (ptr, style);
		else if (strcmp (ptr->name, "marker") == 0)
			gog_style_marker_load (ptr, style);
		else if (strcmp (ptr->name, "font") == 0)
			gog_style_font_load (ptr, style);
	}
	return TRUE;
}

static void
gog_style_persist_dom_save (GogPersist const *gp, xmlNode *parent)
{
	GogStyle const *style = GOG_STYLE (gp);

	xmlSetProp (parent, (xmlChar const *) "type",
		G_OBJECT_TYPE_NAME (style));

	if (style->interesting_fields & GOG_STYLE_OUTLINE)
		gog_style_line_dom_save (parent, "outline", &style->outline);
	if (style->interesting_fields & GOG_STYLE_LINE)
		gog_style_line_dom_save (parent, "line", &style->line);
	if (style->interesting_fields & GOG_STYLE_FILL)
		gog_style_fill_dom_save (parent, style);
	if (style->interesting_fields & GOG_STYLE_MARKER)
		gog_style_marker_dom_save (parent, style);
	if (style->interesting_fields & GOG_STYLE_FONT)
		gog_style_font_dom_save (parent, style);
}

static void
gog_style_persist_sax_save (GogPersist const *gp, GsfXMLOut *output)
{
	GogStyle const *style = GOG_STYLE (gp);

	gsf_xml_out_add_cstr_unchecked (output, "type",
		G_OBJECT_TYPE_NAME (style));

	if (style->interesting_fields & GOG_STYLE_OUTLINE)
		gog_style_line_sax_save (output, "outline", &style->outline);
	if (style->interesting_fields & GOG_STYLE_LINE)
		gog_style_line_sax_save (output, "line", &style->line);
	if (style->interesting_fields & GOG_STYLE_FILL)
		gog_style_fill_sax_save (output, style);
	if (style->interesting_fields & GOG_STYLE_MARKER)
		gog_style_marker_sax_save (output, style);
	if (style->interesting_fields & GOG_STYLE_FONT)
		gog_style_font_sax_save (output, style);
}

static void
gog_style_persist_init (GogPersistClass *iface)
{
	iface->dom_load = gog_style_persist_dom_load;
	iface->dom_save = gog_style_persist_dom_save;
	iface->sax_save = gog_style_persist_sax_save;
}

GSF_CLASS_FULL (GogStyle, gog_style,
		gog_style_class_init, gog_style_init,
		G_TYPE_OBJECT, 0,
		GSF_INTERFACE (gog_style_persist_init, GOG_PERSIST_TYPE))

gboolean
gog_style_is_different_size (GogStyle const *a, GogStyle const *b)
{
	if (a == NULL || b == NULL)
		return TRUE;
	return	a->outline.width != b->outline.width ||
		a->line.width != b->line.width ||
		!go_font_eq (a->font.font, b->font.font);
}

gboolean
gog_style_is_marker_visible (GogStyle const *style)
{
#warning TODO : make this smarter
	return (style->interesting_fields & GOG_STYLE_MARKER) &&
		go_marker_get_shape (style->marker.mark) != GO_MARKER_NONE;
}

gboolean
gog_style_is_line_visible (GogStyle const *style)
{
#warning TODO : make this smarter
	return style->line.width >= 0 && UINT_RGBA_A (style->line.color) > 0;
}

/**
 * gog_style_set_marker :
 * @style : #GogStyle
 * @marker : #GOMarker
 *
 * Absorb a reference to @marker and assign it to @style.
 **/
void
gog_style_set_marker (GogStyle *style, GOMarker *marker)
{
	g_return_if_fail (GOG_STYLE (style) != NULL);
	g_return_if_fail (GO_MARKER (marker) != NULL);

	if (style->marker.mark != marker) {
		if (style->marker.mark != NULL)
			g_object_unref (style->marker.mark);
		style->marker.mark = marker;
	}
}

void
gog_style_set_font (GogStyle *style, PangoFontDescription *desc)
{
	GOFont const *font;

	g_return_if_fail (GOG_STYLE (style) != NULL);

	font = go_font_new_by_desc (desc);
	if (font != NULL) {
		go_font_unref (style->font.font);
		style->font.font = font;
	}
}

void
gog_style_set_fill_brightness (GogStyle *style, float brightness)
{
	g_return_if_fail (GOG_STYLE (style) != NULL);
	g_return_if_fail (style->fill.type == GOG_FILL_STYLE_GRADIENT);

	style->fill.u.gradient.brightness = brightness;
	style->fill.u.gradient.end = (brightness < 50.)
		? UINT_INTERPOLATE(style->fill.u.gradient.start, RGBA_WHITE, 1. - brightness / 50.)
		: UINT_INTERPOLATE(style->fill.u.gradient.start, RGBA_BLACK, brightness / 50. - 1.);
}

/**
 * gog_style_set_fill_image_filename :
 * @style : #GogStyle
 * @filename :
 *
 * absorb the string and eventually free it.
 **/
void
gog_style_set_fill_image_filename (GogStyle *style, char *filename)
{
	g_return_if_fail (GOG_STYLE (style) != NULL);

	if (style->fill.type == GOG_FILL_STYLE_IMAGE) {
		if (style->fill.u.image.image != NULL)
			g_object_unref (style->fill.u.image.image);
		g_free (style->fill.u.image.filename);
	} else {
		style->fill.type = GOG_FILL_STYLE_IMAGE;
		style->fill.u.image.type = GOG_IMAGE_CENTERED;
	}

	style->fill.u.image.filename = filename;
	style->fill.u.image.image = gdk_pixbuf_new_from_file (filename, NULL);
}

static void
cb_switch_page (G_GNUC_UNUSED GtkNotebook *n, G_GNUC_UNUSED GtkNotebookPage *p,
		guint page_num, guint *store_page)
{
		*store_page = page_num;
}

void
gog_style_handle_notebook (gpointer notebook, guint *page)
{
	g_return_if_fail (GTK_NOTEBOOK (notebook) != NULL);
	g_return_if_fail (page != NULL);

	gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), *page);
	g_signal_connect (G_OBJECT (notebook),
		"switch_page",
		G_CALLBACK (cb_switch_page), page);
}
