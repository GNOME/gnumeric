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
#include <goffice/utils/go-font.h>
#include <goffice/utils/go-marker.h>

#include <src/gui-util.h>
#include <glade/glade-xml.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkrange.h>
#include <gtk/gtkoptionmenu.h>
#include <gtk/gtknotebook.h>
#include <widgets/widget-color-combo.h>
#include <widgets/color-palette.h>
#include <widgets/widget-pixmap-combo.h>
#include <widgets/widget-font-selector.h>
#include <widgets/preview-file-selection.h>
#include <gdk-pixbuf/gdk-pixdata.h>

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>
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
	GladeXML  *gui;
	GogObject *obj;
	gboolean  enable_edit;
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

/* utility routines */
static GogStyle *
gog_object_get_style (GogObject *obj)
{
	GogStyle *style;
	g_object_get (G_OBJECT (obj), "style", &style, NULL);
	g_object_unref (style);
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
	gnm_combo_box_set_tearable (GNM_COMBO_BOX (w), FALSE);
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
	GogStyle *style = gog_object_dup_style (state->obj);
	g_return_if_fail (style != NULL);
	style->outline.width = adj->value;
	gog_object_set_style (state->obj, style);
}

static void
cb_outline_color_changed (GtkWidget *cc,
			  G_GNUC_UNUSED GdkColor *color,	gboolean  is_custom,
			  G_GNUC_UNUSED gboolean  by_user,	gboolean  is_default,
			  StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	g_return_if_fail (style != NULL);
	style->outline.color = color_combo_get_gocolor (cc, is_custom);
	style->outline.auto_color = is_default;
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
		       G_GNUC_UNUSED GdkColor *color,	gboolean  is_custom,
		       G_GNUC_UNUSED gboolean  by_user,	gboolean  is_default,
		       StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	g_return_if_fail (style != NULL);
	style->line.color = color_combo_get_gocolor (cc, is_custom);
	style->line.auto_color = is_default;
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
		     G_GNUC_UNUSED GdkColor *color,	gboolean is_custom,
		     G_GNUC_UNUSED gboolean by_user,	G_GNUC_UNUSED gboolean is_default,
		     StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	g_return_if_fail (style != NULL);
	g_return_if_fail (GOG_FILL_STYLE_PATTERN == style->fill.type);
	style->fill.u.pattern.pat.fore = color_combo_get_gocolor (cc, is_custom);
	style->fill.pattern_fore_auto = is_default;
	style->fill.is_auto = (style->fill.pattern_fore_auto & 
			       style->fill.pattern_back_auto);
	gog_object_set_style (state->obj, style);
	populate_pattern_combo (state, style);
}

static void
cb_bg_color_changed (GtkWidget *cc,
		     G_GNUC_UNUSED GdkColor *color,	gboolean is_custom,
		     G_GNUC_UNUSED gboolean by_user,	G_GNUC_UNUSED gboolean is_default,
		     StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	g_return_if_fail (style != NULL);
	g_return_if_fail (GOG_FILL_STYLE_PATTERN == style->fill.type);
	style->fill.u.pattern.pat.back = color_combo_get_gocolor (cc, is_custom);
	style->fill.pattern_back_auto = is_default;
	style->fill.is_auto = (style->fill.pattern_fore_auto & 
			       style->fill.pattern_back_auto);
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
cb_gradient_type_changed (GtkWidget *cc, int dir, StylePrefState const *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	style->fill.u.gradient.dir = dir; /* pre mapped */
	gog_object_set_style (state->obj, style);
}

static void
populate_gradient_combo (StylePrefState *state, GogStyle const *style)
{
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
	if (style->fill.type == GOG_FILL_STYLE_GRADIENT)
		pixmap_combo_select_pixmap (PIXMAP_COMBO (combo), style->fill.u.gradient.dir);
	g_signal_connect (G_OBJECT (combo),
		"changed",
		G_CALLBACK (cb_gradient_type_changed), state);
	gtk_widget_show (combo);
}

static void
cb_fill_gradient_start_color (GtkWidget *cc,
			      G_GNUC_UNUSED GdkColor *color,	gboolean is_custom,
			      G_GNUC_UNUSED gboolean by_user,	gboolean is_default,
			      StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	style->fill.u.gradient.start = color_combo_get_gocolor (cc, is_custom);
	style->fill.gradient_start_auto = is_default;
	style->fill.is_auto = (style->fill.gradient_start_auto & 
			       style->fill.gradient_end_auto);
	gog_object_set_style (state->obj, style);
	populate_gradient_combo (state, style);
}

static gboolean
cb_delayed_gradient_combo_update (StylePrefState *state)
{
	state->fill.gradient.timer = 0;
	populate_gradient_combo (state, gog_object_get_style (state->obj));
	return FALSE;
}

static void
cb_fill_gradient_end_color (GtkWidget *cc,
			    G_GNUC_UNUSED GdkColor *color,	gboolean is_custom,
			    gboolean by_user,			gboolean is_default,
			    StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	style->fill.u.gradient.end = color_combo_get_gocolor (cc, is_custom);
	style->fill.gradient_end_auto = is_default;
	style->fill.is_auto = (style->fill.gradient_start_auto & 
			       style->fill.gradient_end_auto);
	gog_object_set_style (state->obj, style);

	if (by_user)
		populate_gradient_combo (state, style);
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
	GogStyle *style = gog_object_dup_style (state->obj);
	gog_style_set_fill_brightness (style,
		gtk_range_get_value (GTK_RANGE (w)));
	color_combo_set_gocolor (state->fill.gradient.end,
		style->fill.u.gradient.end);
	gog_object_set_style (state->obj, style);
}

static void
cb_gradient_style_changed (GtkWidget *w, StylePrefState *state)
{
	GtkWidget *val = glade_xml_get_widget (state->gui,
		"fill_gradient_brightness");
	GtkWidget *box = glade_xml_get_widget (state->gui,
		"fill_gradient_brightness_box");
	GogStyle  *style = gog_object_dup_style (state->obj);
	gboolean two_color = gtk_option_menu_get_history (GTK_OPTION_MENU (w)) == 0;

	if (two_color) {
		style->fill.u.gradient.brightness = -1;
		gtk_widget_hide (box);
	} else {
		gtk_widget_show (box);
		gog_style_set_fill_brightness (style,
			gtk_range_get_value (GTK_RANGE (val)));
		color_combo_set_gocolor (state->fill.gradient.end,
			style->fill.u.gradient.end);
	}
	gtk_widget_set_sensitive (state->fill.gradient.end, two_color);
	gtk_widget_set_sensitive (state->fill.gradient.end_label, two_color);
	gog_object_set_style (state->obj, style);
}

static void
fill_gradient_init (StylePrefState *state, GogStyle const *style)
{
	GtkWidget *w, *table = glade_xml_get_widget (state->gui, "fill_gradient_table");
	GtkWidget *type = glade_xml_get_widget (state->gui, "fill_gradient_type");

	state->fill.gradient.start = w = create_color_combo (state,
		gog_style_get_fill_color (style, 1),
		"gradient_start", "fill_gradient_start_label",
		G_CALLBACK (cb_fill_gradient_start_color));
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 2, 3, 0, 0, 0, 0);
	gtk_widget_show (w);

	state->fill.gradient.end = w = create_color_combo (state,
		gog_style_get_fill_color (style, 2),
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
		gtk_option_menu_set_history (GTK_OPTION_MENU (type), 0);
		gtk_widget_hide (state->fill.gradient.brightness_box);
	} else {
		gtk_option_menu_set_history (GTK_OPTION_MENU (type), 1);
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

	populate_gradient_combo (state, style);
	gtk_widget_show (table);
}

/************************************************************************/

static void
cb_image_file_select (GtkWidget *cc, StylePrefState *state)
{
	GogStyle *style = gog_object_get_style (state->obj);
	GtkFileSelection *fs;
	GtkWidget *w;

	g_return_if_fail (style != NULL);
	g_return_if_fail (GOG_FILL_STYLE_IMAGE == style->fill.type);

	fs = GTK_FILE_SELECTION (preview_file_selection_new (_("Select an image"), TRUE));
	gtk_window_set_modal (GTK_WINDOW (fs), TRUE);
	gtk_file_selection_hide_fileop_buttons (fs);

	if (style->fill.u.image.filename != NULL)
		preview_file_selection_set_filename (fs, style->fill.u.image.filename);

	/* 
	 * should not be modal
	 **/
	if (gnumeric_dialog_file_selection (NULL, fs)) {
		style = gog_style_dup (style);

		if (style->fill.u.image.image != NULL)
			g_object_unref (style->fill.u.image.image);
		g_free (style->fill.u.image.filename);

		style->fill.u.image.filename =
			g_strdup (gtk_file_selection_get_filename (fs));
		style->fill.u.image.image = (style->fill.u.image.filename != NULL) 
			? gdk_pixbuf_new_from_file (style->fill.u.image.filename, NULL)
			: NULL;

		w = glade_xml_get_widget (state->gui, "fill_image_sample");
		g_object_set_data (G_OBJECT (w), "filename",
				   style->fill.u.image.filename);
			
		gog_style_set_image_preview (style->fill.u.image.image, state);

		gog_object_set_style (state->obj, style);
	}
	gtk_widget_destroy (GTK_WIDGET (fs));
}

static void
cb_image_style_changed (GtkWidget *w, StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	g_return_if_fail (style != NULL);
	g_return_if_fail (GOG_FILL_STYLE_IMAGE == style->fill.type);
	style->fill.u.image.type = gtk_option_menu_get_history (GTK_OPTION_MENU (w));
	gog_object_set_style (state->obj, style);
}

static void
fill_image_init (StylePrefState *state, GogStyle const *style)
{
	GtkWidget *w, *sample, *type;

	w = glade_xml_get_widget (state->gui, "fill_image_select_picture");
	g_signal_connect (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_image_file_select), state);

	sample = glade_xml_get_widget (state->gui, "fill_image_sample");
	gtk_widget_set_size_request (sample, HSCALE + 10, VSCALE + 10);
	type   = glade_xml_get_widget (state->gui, "fill_image_fit");

	state->fill.image.image = NULL;

	if (GOG_FILL_STYLE_IMAGE == style->fill.type) {
		gtk_option_menu_set_history (GTK_OPTION_MENU (type),
			style->fill.u.image.type);
		gog_style_set_image_preview (style->fill.u.image.image, state);
		state->fill.image.image = style->fill.u.image.image;
		if (state->fill.image.image)
			g_object_ref (state->fill.image.image);
		g_object_set_data (G_OBJECT (sample), "filename",
				   style->fill.u.image.filename);
	}
	g_signal_connect (G_OBJECT (type),
		"changed",
		G_CALLBACK (cb_image_style_changed), state);
}

/************************************************************************/

static void
cb_fill_type_changed (GtkWidget *menu, StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style (state->obj);
	GtkWidget *w;
	unsigned page;

	page = gtk_option_menu_get_history (GTK_OPTION_MENU (menu));

	if (page != style->fill.type &&
	    GOG_FILL_STYLE_IMAGE == style->fill.type &&
	    style->fill.u.image.image)
		g_object_unref (style->fill.u.image.image);

	switch (page) {
	case GOG_FILL_STYLE_NONE:
		break;
	case GOG_FILL_STYLE_PATTERN:
		style->fill.u.pattern.pat.fore =
			color_combo_get_gocolor (state->fill.pattern.fore, FALSE);
		style->fill.u.pattern.pat.back =
			color_combo_get_gocolor (state->fill.pattern.back, FALSE);
		style->fill.u.pattern.pat.pattern =
			((PixmapCombo*)state->fill.pattern.combo)->last_index;
		break;

	case GOG_FILL_STYLE_GRADIENT:
		style->fill.u.gradient.start =
			color_combo_get_gocolor (state->fill.gradient.start, FALSE);
		style->fill.u.gradient.end =
			color_combo_get_gocolor (state->fill.gradient.end, FALSE);
		style->fill.u.gradient.dir =
			((PixmapCombo*)state->fill.gradient.combo)->last_index;
		w = glade_xml_get_widget (state->gui, "fill_gradient_type");
		if (gtk_option_menu_get_history (GTK_OPTION_MENU (w))) {
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
		style->fill.u.image.type = gtk_option_menu_get_history (GTK_OPTION_MENU (w));
		break;
	}
	style->fill.type = page;
	gog_object_set_style (state->obj, style);

	w = glade_xml_get_widget (state->gui, "fill_notebook");
	gtk_notebook_set_current_page (GTK_NOTEBOOK (w), page);
}

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

	w = glade_xml_get_widget (state->gui, "fill_notebook");
	gtk_notebook_set_current_page (GTK_NOTEBOOK (w), style->fill.type);
	w = glade_xml_get_widget (state->gui, "fill_type_menu");
	gtk_option_menu_set_history (GTK_OPTION_MENU (w), style->fill.type);
	g_signal_connect (G_OBJECT (w),
		"changed",
		G_CALLBACK (cb_fill_type_changed), state);

	w = glade_xml_get_widget (state->gui, "fill_outer_table");
	gtk_widget_show (GTK_WIDGET (w));
}

/************************************************************************/

/* copy the style and the marker to avoid stepping on anyone sharing the marker */
static GogStyle *
gog_object_dup_style_and_marker (GogObject *obj)
{
	GogStyle *res = gog_style_dup (gog_object_get_style (obj));
	gog_style_set_marker (res, go_marker_dup (res->marker));
	return res;
}
static void
cb_marker_shape_changed (GtkWidget *cc, int shape, StylePrefState const *state)
{
	GogStyle *style = gog_object_dup_style_and_marker (state->obj);
	go_marker_set_shape (style->marker, shape);
	gog_object_set_style (state->obj, style);
}

static void
populate_marker_combo (StylePrefState *state, GogStyle const *style)
{
	GtkWidget *combo, *table;

	if (state->marker.combo != NULL)
		gtk_widget_destroy (state->marker.combo);

	state->marker.combo = combo = go_marker_selector (
	        go_marker_get_outline_color (style->marker),
		go_marker_get_fill_color (style->marker));
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (glade_xml_get_widget (state->gui, "marker_shape_label")), combo);

	table = glade_xml_get_widget (state->gui, "marker_table");
	gtk_table_attach (GTK_TABLE (table), combo, 1, 2, 0, 1, 0, 0, 0, 0);
	pixmap_combo_select_pixmap (PIXMAP_COMBO (combo), 
				    go_marker_get_shape (style->marker));
	g_signal_connect (G_OBJECT (combo),
		"changed",
		G_CALLBACK (cb_marker_shape_changed), state);
	gtk_widget_show (combo);
}

static void
cb_marker_outline_color_changed (GtkWidget *cc,
				 G_GNUC_UNUSED GdkColor *color,		gboolean is_custom,
				 G_GNUC_UNUSED gboolean by_user,	gboolean is_default,
				 StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style_and_marker (state->obj);
	if (is_default)
		go_marker_set_outline_color (style->marker, style->marker->defaults.outline_color);
	else
		go_marker_set_outline_color (style->marker,
			color_combo_get_gocolor (cc, is_custom));
	gog_object_set_style (state->obj, style);
	populate_marker_combo (state, style);
}

static void
cb_marker_fill_color_changed (GtkWidget *cc,
			      G_GNUC_UNUSED GdkColor *color,	gboolean is_custom,
			      G_GNUC_UNUSED gboolean by_user,	gboolean is_default,
			      StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style_and_marker (state->obj);
	if (is_default)
		go_marker_set_fill_color (style->marker, style->marker->defaults.fill_color);
	else
		go_marker_set_fill_color (style->marker,
			color_combo_get_gocolor (cc, is_custom));
	gog_object_set_style (state->obj, style);
	populate_marker_combo (state, style);
}

static void
cb_marker_size_changed (GtkAdjustment *adj, StylePrefState *state)
{
	GogStyle *style = gog_object_dup_style_and_marker (state->obj);
	go_marker_set_size (style->marker, adj->value);
	gog_object_set_style (state->obj, style);
}

static void
marker_init (StylePrefState *state, GogStyle const *style, gboolean enable)
{
	GtkWidget *table, *w;
	
	if (!enable) {
		gtk_widget_hide (glade_xml_get_widget (state->gui, "marker_outer_table"));
		return;
	}
	
	table = glade_xml_get_widget (state->gui, "marker_table");

	populate_marker_combo (state, style);

	w = create_color_combo (state, style->marker->outline_color,
		"pattern_foreground", "marker_outline_label",
		G_CALLBACK (cb_marker_outline_color_changed));
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 1, 2, 0, 0, 0, 0);
	
	w = create_color_combo (state, style->marker->fill_color,
		"pattern_foreground", "marker_fill_label",
		G_CALLBACK (cb_marker_fill_color_changed));
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 2, 3, 0, 0, 0, 0);

	w = glade_xml_get_widget (state->gui, "marker_size_spin");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (w), style->marker->size);
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
	GogStyle *style = gog_object_dup_style (state->obj);
	PangoFontDescription *new_font = pango_font_description_copy (style->font.font->desc);

	g_return_if_fail (style != NULL);

	font_selector_get_pango (fs, new_font);
	gog_style_set_font (style, new_font);
	gog_object_set_style (state->obj, style);
}

static void
font_init (StylePrefState *state, GogStyle const *style, guint32 enable, GtkWidget *optional_notebook)
{
	GtkWidget *w;

	if (!enable)
		return;

	g_return_if_fail (style->font.font != NULL);
	g_return_if_fail (GTK_NOTEBOOK (optional_notebook) != NULL);

	w = font_selector_new ();
	font_selector_set_from_pango  (FONT_SELECTOR (w), style->font.font->desc);
	g_signal_connect (G_OBJECT (w),
		"font_changed",
		G_CALLBACK (cb_font_changed), state);
	gtk_notebook_prepend_page (GTK_NOTEBOOK (optional_notebook), w,
		gtk_label_new (_("Font")));
	gtk_widget_show (w);
	gtk_widget_show (optional_notebook);
}

static void
gog_style_pref_state_free (StylePrefState *state)
{
	g_object_unref (state->gui);
	if (state->fill.gradient.timer != 0) {
		g_source_remove (state->fill.gradient.timer);
		state->fill.gradient.timer = 0;
	}
	if (state->fill.image.image != NULL)
		g_object_unref (state->fill.image.image);
	g_free (state);
}

GtkWidget *
gog_style_editor (GogObject *obj, CommandContext *cc,
		  GtkWidget *optional_notebook, guint32 enable)
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
	font_init    (state, style, enable & GOG_STYLE_FONT, optional_notebook);

	state->enable_edit = TRUE;

 	w = glade_xml_get_widget (gui, "gog_style_prefs");
	g_object_set_data_full (G_OBJECT (w),
		"state", state, (GDestroyNotify) gog_style_pref_state_free);

	if (optional_notebook != NULL) {
		gtk_notebook_prepend_page (GTK_NOTEBOOK (optional_notebook), w,
			gtk_label_new (_("Style")));
		return optional_notebook;
	}
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
	if (dst->marker)
		g_object_unref (G_OBJECT (dst->marker));
	dst->marker  = go_marker_dup (src->marker);
	dst->font    = src->font;
	dst->line    = src->line;

	if (GOG_FILL_STYLE_IMAGE == dst->fill.type)
		dst->fill.u.image.filename = g_strdup (dst->fill.u.image.filename);

	dst->interesting_fields = src->interesting_fields;
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
	if (src == dst)
		return;

	g_return_if_fail (GOG_STYLE (src) != NULL);
	g_return_if_fail (GOG_STYLE (dst) != NULL);

	if (src->font.font != NULL)
		go_font_ref (src->font.font);
	if (dst->font.font != NULL)
		go_font_unref (dst->font.font);

	if (dst->outline.auto_color)
		dst->outline = src->outline;
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
		dst->line    = src->line;
	if (!dst->marker || go_marker_is_auto (dst->marker)) {
		if (dst->marker)
			g_object_unref (G_OBJECT (dst->marker));
		dst->marker  = go_marker_dup (src->marker);
	}
	dst->font    = src->font; /* FIXME: No way to tell if this is auto */
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

	if (style->marker != NULL)
	{
		g_object_unref (style->marker);
		style->marker = NULL;
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
	style->marker = go_marker_new ();
	style->outline.auto_color =
	style->line.auto_color =
	style->fill.is_auto = 
	style->fill.pattern_fore_auto =
	style->fill.pattern_back_auto =
	style->fill.gradient_start_auto =
	style->fill.gradient_end_auto = TRUE;
	style->fill.type = GOG_FILL_STYLE_PATTERN;
	go_pattern_set_solid (&style->fill.u.pattern.pat, 0);
	style->font.font = go_font_new_by_index (0);
	style->font.color = RGBA_BLACK;
}

static struct {
	GogFillStyle fstyle;
	const gchar  *name;
} fill_names[] = {
	{ GOG_FILL_STYLE_NONE,     "none" },
	{ GOG_FILL_STYLE_PATTERN,  "pattern" },
	{ GOG_FILL_STYLE_GRADIENT, "gradient" },
	{ GOG_FILL_STYLE_IMAGE,    "image" }
};

static GogFillStyle
str_as_fill_style (const gchar *name)
{
	unsigned i;
	GogFillStyle ret = GOG_FILL_STYLE_PATTERN;

	for (i = 0; i < sizeof fill_names / sizeof fill_names[0]; i++) {
		if (strcmp (fill_names[i].name, name) == 0) {
			ret = fill_names[i].fstyle;
			break;
		}
	}
	return ret;
}

static const gchar *
fill_style_as_str (GogFillStyle fstyle)
{
	unsigned i;
	const gchar *ret = "pattern";

	for (i = 0; i < sizeof fill_names / sizeof fill_names[0]; i++) {
		if (fill_names[i].fstyle == fstyle) {
			ret = fill_names[i].name;
			break;
		}
	}
	return ret;
}

static void
gog_style_line_load (xmlNode *node, GogStyleLine *line)
{
	char *str;
	
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
	str = xmlGetProp (node, "auto-color");
	if (str != NULL) { 
		line->auto_color =
			g_ascii_tolower (*str) == 't' ||
			g_ascii_tolower (*str) == 'y' ||
			strtol (str, NULL, 0);
		xmlFree (str);
	}
}

static void
gog_style_line_save (xmlNode *parent, xmlChar const *name, 
		     GogStyleLine *line)
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
		    line->auto_color ? "TRUE" : "FALSE");
	xmlAddChild (parent, node);
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
gog_style_gradient_save (xmlNode *parent, GogStyle *style)
{
	gchar *str;
	xmlNode *node =  xmlNewDocNode (parent->doc, NULL, "gradient", NULL);

	xmlSetProp (node, (xmlChar const *) "direction", 
		    go_gradient_dir_as_str (style->fill.u.gradient.dir));
	/* FIXME: According to gog-style.h, condition is >= 0 */
	if (style->fill.u.gradient.brightness > 0) {
		str = g_strdup_printf ("%f",  
				       style->fill.u.gradient.brightness);
		xmlSetProp (node, (xmlChar const *) "brightness", str);
		g_free (str);
	} else {
		str = go_color_as_str (style->fill.u.gradient.start);
		xmlSetProp (node, (xmlChar const *) "start-color", str);
		g_free (str);
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
	char    *str = xmlGetProp (node, "type");

	if (str == NULL)
		return;
	style->fill.type = str_as_fill_style (str);
	xmlFree (str);

	str = xmlGetProp (node, "is-auto");
	if (str != NULL) { 
		style->fill.is_auto =
			g_ascii_tolower (*str) == 't' ||
			g_ascii_tolower (*str) == 'y' ||
			strtol (str, NULL, 0);
		xmlFree (str);
	}

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
gog_style_fill_save (xmlNode *parent, GogStyle *style)
{
	gchar *str;
	xmlNode *node = xmlNewDocNode (parent->doc, NULL, "fill", NULL);
	xmlNode *child;
	xmlSetProp (node, (xmlChar const *) "type", 
		    fill_style_as_str (style->fill.type));
	xmlSetProp (node, (xmlChar const *) "is-auto", 
		    style->fill.is_auto ? "TRUE" : "FALSE");
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
		gog_style_gradient_save (node, style);
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
	GOMarker *marker = go_marker_dup (style->marker);

	str = xmlGetProp (node, "shape");
	if (str != NULL) {
		go_marker_set_shape (marker, go_marker_shape_from_str (str));
		xmlFree (str);
	}
	str = xmlGetProp (node, "outline-color");
	if (str != NULL) {
		go_marker_set_outline_color (marker, go_color_from_str (str));
		xmlFree (str);
	}
	str = xmlGetProp (node, "fill-color");
	if (str != NULL) {
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
gog_style_marker_save (xmlNode *parent, GogStyle *style)
{
	gchar *str;
	xmlNode *node = xmlNewDocNode (parent->doc, NULL, "marker", NULL);

	xmlSetProp (node, (xmlChar const *) "shape", 
		    go_marker_shape_as_str (go_marker_get_shape (style->marker)));
	str = go_color_as_str (go_marker_get_outline_color (style->marker));
	xmlSetProp (node, (xmlChar const *) "outline-color", str);
	g_free (str);
	str = go_color_as_str (go_marker_get_fill_color (style->marker));
	xmlSetProp (node, (xmlChar const *) "fill-color", str);
	g_free (str);
	str = g_strdup_printf ("%d", go_marker_get_size (style->marker));
	xmlSetProp (node, (xmlChar const *) "size", str);
	g_free (str);

	xmlAddChild (parent, node);
}

static void
gog_style_font_load (xmlNode *node, GogStyle *style)
{
	char *str;

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
	str = xmlGetProp (node, "auto-scale");
	if (str != NULL) { 
		style->font.auto_scale =
			g_ascii_tolower (*str) == 't' ||
			g_ascii_tolower (*str) == 'y' ||
			strtol (str, NULL, 0);
		xmlFree (str);
	}
}

static void
gog_style_font_save (xmlNode *parent, GogStyle *style)
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

static gboolean
gog_style_persist_dom_load (GogPersistDOM *gpd, xmlNode *node)
{
	GogStyle *style = GOG_STYLE (gpd);
	xmlNode *ptr;

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
gog_style_persist_dom_save (GogPersistDOM *gpd, xmlNode *parent)
{
	GogStyle *style = GOG_STYLE (gpd);

	xmlSetProp (parent, (xmlChar const *) "type", 
		G_OBJECT_TYPE_NAME (style));

	if (style->interesting_fields & GOG_STYLE_OUTLINE)
		gog_style_line_save (parent, "outline", &style->outline);
	if (style->interesting_fields & GOG_STYLE_LINE)
		gog_style_line_save (parent, "line", &style->line);
	if (style->interesting_fields & GOG_STYLE_FILL)
		gog_style_fill_save (parent, style);
	if (style->interesting_fields & GOG_STYLE_MARKER)
		gog_style_marker_save (parent, style);
	if (style->interesting_fields & GOG_STYLE_FONT)
		gog_style_font_save (parent, style);
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
	return a->outline.width != b->outline.width ||
		!go_font_eq (a->font.font, b->font.font);
}

gboolean
gog_style_is_marker_visible (GogStyle const *style)
{
#warning TODO : make this smarter
	return (style->interesting_fields & GOG_STYLE_MARKER) &&
		style->marker != NULL &&
		go_marker_get_shape (style->marker) != GO_MARKER_NONE;
}

gboolean
gog_style_is_line_visible (GogStyle const *style)
{
#warning TODO : make this smarter
	return style->line.width >= 0;
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

	if (style->marker != marker) {
		if (style->marker != NULL)
			g_object_unref (style->marker);
		style->marker = marker;
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
	style->fill.u.gradient.brightness = brightness;
	style->fill.u.gradient.end = (brightness < 50.)
		? UINT_INTERPOLATE(style->fill.u.gradient.start, RGBA_WHITE, 1. - brightness / 50.)
		: UINT_INTERPOLATE(style->fill.u.gradient.start, RGBA_BLACK, brightness / 50. - 1.);
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
