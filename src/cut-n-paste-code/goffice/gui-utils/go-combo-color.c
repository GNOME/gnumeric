/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * widget-color-combo.c - A color selector combo box
 * Copyright 2000-2004, Ximian, Inc.
 *
 * Authors:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Dom Lachowicz (dominicl@seas.upenn.edu)
 *
 * Reworked and split up into a separate ColorPalette object:
 *   Michael Levy (mlevy@genoscope.cns.fr)
 *
 * And later revised and polished by:
 *   Almer S. Tigelaar (almer@gnome.org)
 *   Jody Goldberg (jody@gnome.org)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <gnumeric-config.h>

#include "go-combo-color.h"
#include "go-marshalers.h"
#include "go-combo-box.h"
#include "go-color-palette.h"

#include <gsf/gsf-impl-utils.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkwindow.h>
#include <gdk/gdkcolor.h>

struct _ColorCombo {
	GOComboBox     combo_box;

	ColorPalette    *palette;
	GtkWidget       *preview_button;
	GtkWidget	*preview_image;
	gboolean	 preview_is_icon;
	gboolean	 instant_apply;

        GOColor  default_color;
};

typedef struct {
	GOComboBoxClass base;
	void (*color_changed) (ColorCombo *cc, GOColor color,
			       gboolean custom, gboolean by_user, gboolean is_default);
	void (*display_custom_dialog) (ColorCombo *cc, GtkWidget *dialog);
} ColorComboClass;

enum {
	COLOR_CHANGED,
	DISPLAY_CUSTOM_DIALOG,
	LAST_SIGNAL
};

static guint color_combo_signals [LAST_SIGNAL] = { 0, };

static GObjectClass *color_combo_parent_class;

#define PREVIEW_SIZE 20

static void
color_combo_set_color_internal (ColorCombo *cc, GOColor color, gboolean is_default)
{
	guint color_y, color_height;
	guint height, width;
	GdkPixbuf *pixbuf;
	GdkPixbuf *color_pixbuf;
	gboolean   add_an_outline;

	pixbuf = gtk_image_get_pixbuf (GTK_IMAGE (cc->preview_image));
	if (!pixbuf)
		return;
	width = gdk_pixbuf_get_width (pixbuf);
	height = gdk_pixbuf_get_height (pixbuf);
	if (cc->preview_is_icon) {
		color_y = height - 4;
		color_height = 4;
	} else {
		color_y = 0;
		color_height = height;
	}

	color_pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8,
				       width, color_height);

	/* mostly transparent things should have an outline */
	add_an_outline = (UINT_RGBA_A (color) < 0x80);
	gdk_pixbuf_fill (color_pixbuf, add_an_outline ? RGBA_GREY (0x33) : color);
	gdk_pixbuf_copy_area (color_pixbuf, 0, 0, width, color_height,
		pixbuf, 0, color_y);
	if (add_an_outline) {
		gdk_pixbuf_fill (color_pixbuf, color);
		gdk_pixbuf_copy_area (color_pixbuf, 0, 0, width - 2, color_height -2,
			pixbuf, 1, color_y + 1);
	}

	g_object_unref (color_pixbuf);
	gtk_widget_queue_draw (cc->preview_image);
}

static void
cb_screen_changed (ColorCombo *cc, GdkScreen *previous_screen)
{
	GtkWidget *w = GTK_WIDGET (cc);
	GdkScreen *screen = gtk_widget_has_screen (w)
		? gtk_widget_get_screen (w)
		: NULL;

	if (screen) {
		GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (cc->palette));
		gtk_window_set_screen (GTK_WINDOW (toplevel), screen);
	}
}

static void
emit_color_changed (ColorCombo *cc, GOColor color,
		    gboolean is_custom, gboolean by_user, gboolean is_default)
{
  	g_signal_emit (cc,
		       color_combo_signals [COLOR_CHANGED], 0,
		       color, is_custom, by_user, is_default);
	go_combo_box_popup_hide (GO_COMBO_BOX (cc));
}

static void
cb_preview_clicked (GtkWidget *button, ColorCombo *cc)
{
	if (_go_combo_is_updating (GO_COMBO_BOX (cc)))
		return;
	if (cc->instant_apply) {
		gboolean is_default;
		GOColor color = color_palette_get_current_color (cc->palette, &is_default);
		emit_color_changed (cc, color, FALSE, TRUE, is_default);
	} else
		go_combo_box_popup_display (GO_COMBO_BOX (cc));
}

static void
color_combo_init (ColorCombo *cc)
{
	cc->instant_apply = FALSE;
	cc->preview_is_icon = FALSE;
	cc->preview_button = gtk_toggle_button_new ();

	g_signal_connect (G_OBJECT (cc),
		"screen-changed",
		G_CALLBACK (cb_screen_changed), NULL);
	g_signal_connect (cc->preview_button,
		"clicked",
		G_CALLBACK (cb_preview_clicked), cc);
}

static void
color_combo_set_title (GOComboBox *combo, char const *title)
{
	color_palette_set_title	(COLOR_COMBO (combo)->palette, title);
}

static void
color_combo_class_init (GObjectClass *gobject_class)
{
	color_combo_parent_class = g_type_class_ref (GO_COMBO_BOX_TYPE);

	GO_COMBO_BOX_CLASS (gobject_class)->set_title = color_combo_set_title;

	color_combo_signals [COLOR_CHANGED] =
		g_signal_new ("color_changed",
			      G_OBJECT_CLASS_TYPE (gobject_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ColorComboClass, color_changed),
			      NULL, NULL,
			      go__VOID__INT_BOOLEAN_BOOLEAN_BOOLEAN,
			      G_TYPE_NONE, 4, G_TYPE_POINTER,
			      G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
	color_combo_signals [DISPLAY_CUSTOM_DIALOG] =
		g_signal_new ("display-custom-dialog",
			      G_OBJECT_CLASS_TYPE (gobject_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ColorComboClass, display_custom_dialog),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, G_TYPE_OBJECT);
}

GSF_CLASS (ColorCombo, color_combo,
	   color_combo_class_init, color_combo_init,
	   GO_COMBO_BOX_TYPE)

static void
cb_palette_color_changed (ColorPalette *P, GOColor color,
			  gboolean custom, gboolean by_user, gboolean is_default,
			  ColorCombo *cc)
{
	color_combo_set_color_internal (cc, color, is_default);
	emit_color_changed (cc, color, custom, by_user, is_default);
}

static void
cb_proxy_custom_dialog (ColorPalette *pal, GtkWidget *dialog, ColorCombo *cc)
{
	g_signal_emit (cc, color_combo_signals [DISPLAY_CUSTOM_DIALOG], 0,
		       dialog);
}

static void
color_table_setup (ColorCombo *cc,
		   char const *no_color_label, GOColorGroup *color_group)
{
	g_return_if_fail (cc != NULL);

	/* Tell the palette that we will be changing it's custom colors */
	cc->palette = COLOR_PALETTE (color_palette_new (no_color_label,
							cc->default_color,
							color_group));
	g_signal_connect (cc->palette,
		"color_changed",
		G_CALLBACK (cb_palette_color_changed), cc);
	g_signal_connect (cc->palette,
		"display-custom-dialog",
		G_CALLBACK (cb_proxy_custom_dialog), cc);
	gtk_widget_show_all (GTK_WIDGET (cc->palette));
}

/* color_combo_get_color:
 *
 * Return current color
 */
GOColor
color_combo_get_color (ColorCombo *cc, gboolean *is_default)
{
	g_return_val_if_fail (IS_COLOR_COMBO (cc), RGBA_BLACK);
	return color_palette_get_current_color (cc->palette, is_default);
}

/**
 * color_combo_set_color
 * @cc     The combo
 * @color  The color
 *
 * Set the color of the combo to the given color. Causes the color_changed
 * signal to be emitted.
 */
void
color_combo_set_color (ColorCombo *cc, GdkColor *color)
{
#warning convert to GOColor
	g_return_if_fail (IS_COLOR_COMBO (cc));
	
	color_palette_set_current_color (cc->palette, GDK_TO_UINT (*color));
}

void
color_combo_set_gocolor (ColorCombo *cc, GOColor c)
{
#warning delete this now
	color_palette_set_current_color (cc->palette, c);
}

/**
 * color_combo_set_instant_apply
 * @cc     The combo
 * @active Whether instant apply should be active or not
 *
 * Turn instant apply behaviour on or off. Instant apply means that pressing
 * the button applies the current color. When off, pressing the button opens
 * the combo.
 */
void
color_combo_set_instant_apply (ColorCombo *cc, gboolean active)
{
	g_return_if_fail (IS_COLOR_COMBO (cc));

	cc->instant_apply = active;
}

/**
 * color_combo_set_allow_alpha :
 * @cc : #ColorCombo
 * @allow_alpha : 
 *
 * Should the custom colour selector allow the use of opacity.
 **/
void
color_combo_set_allow_alpha (ColorCombo *cc, gboolean allow_alpha)
{
	color_palette_set_allow_alpha (cc->palette, allow_alpha);
}

/**
 * color_combo_set_color_to_default
 * @cc  The combo
 *
 * Set the color of the combo to the default color. Causes the color_changed
 * signal to be emitted.
 */
void
color_combo_set_color_to_default (ColorCombo *cc)
{
	g_return_if_fail (IS_COLOR_COMBO (cc));

	color_palette_set_color_to_default (cc->palette);
}

/**
 * color_combo_new :
 * icon : optionally NULL.
 * no_color_label :
 *
 * Default constructor. Pass an optional icon and an optional label for the
 * no/auto color button.
 */
GtkWidget *
color_combo_new (GdkPixbuf *icon, char const *no_color_label,
		 GOColor default_color,
		 GOColorGroup *color_group)
{
	GOColor     color;
	gboolean    is_default;
	GdkPixbuf  *pixbuf = NULL;
	ColorCombo *cc = g_object_new (COLOR_COMBO_TYPE, NULL);

        cc->default_color = default_color;
	if (icon != NULL &&
	    gdk_pixbuf_get_width (icon) > 4 && 
	    gdk_pixbuf_get_height (icon) > 4) {
		cc->preview_is_icon = TRUE;
		pixbuf = gdk_pixbuf_copy (icon);
	} else
		pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 
					 PREVIEW_SIZE, PREVIEW_SIZE);

	cc->preview_image = gtk_image_new_from_pixbuf (pixbuf);
	g_object_unref (pixbuf);
	gtk_widget_show (cc->preview_image);
	gtk_container_add (GTK_CONTAINER (cc->preview_button), cc->preview_image);

	color_table_setup (cc, no_color_label, color_group);
	gtk_widget_show_all (cc->preview_button);

	go_combo_box_construct (GO_COMBO_BOX (cc),
		cc->preview_button, GTK_WIDGET (cc->palette), GTK_WIDGET (cc->palette));

	color = color_palette_get_current_color (cc->palette, &is_default);
	color_combo_set_color_internal (cc, color, is_default);

	return GTK_WIDGET (cc);
}
