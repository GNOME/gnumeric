/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * color.c: Color allocation on the Gnumeric spreadsheet
 *
 * Author:
 *  Miguel de Icaza (miguel@kernel.org)
 *
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "style-color.h"
#include "style-border.h"
#include <gtk/gtk.h>

/* Public _unallocated_ colours, i.e., no valid .pixel.  */
GdkColor gs_black      = { 0, 0x0000, 0x0000, 0x0000 };    /* "Black" */
GdkColor gs_white      = { 0, 0xffff, 0xffff, 0xffff };    /* "White" */
GdkColor gs_yellow     = { 0, 0xffff, 0xffff, 0xe0e0 };    /* "LightYellow" */
GdkColor gs_lavender   = { 0, 0xe6e6, 0xe6e6, 0xfafa };    /* "lavender" */
GdkColor gs_dark_gray  = { 0, 0x3333, 0x3333, 0x3333 };    /* "gray20" */
GdkColor gs_light_gray = { 0, 0xc7c7, 0xc7c7, 0xc7c7 };    /* "gray78" */

static GHashTable *style_color_hash;
static GnmColor *sc_black;
static GnmColor *sc_white;
static GnmColor *sc_grid;

GnmColor *
style_color_new_name (char const *name)
{
	GdkColor c;
	gdk_color_parse (name, &c);
	return style_color_new_gdk (&c);
}

static GnmColor *
style_color_new_uninterned (GOColor c, gboolean is_auto)
{
	GnmColor *sc = g_new (GnmColor, 1);

	sc->go_color = c;
	sc->is_auto = !!is_auto;
	sc->ref_count = 1;

	return sc;
}

GnmColor *
style_color_new_i16 (gushort red, gushort green, gushort blue)
{
	return style_color_new_i8 (red >> 8, green >> 8, blue >> 8);
}

GnmColor *
style_color_new_pango (PangoColor const *c)
{
	return style_color_new_i16 (c->red, c->green, c->blue);
}

GnmColor *
style_color_new_gdk (GdkColor const *c)
{
	return style_color_new_i16 (c->red, c->green, c->blue);
}

GnmColor *
style_color_new_i8 (guint8 red, guint8 green, guint8 blue)
{
	return style_color_new_go (GO_COLOR_FROM_RGBA (red, green, blue, 0xff));
}

GnmColor *
style_color_new_go (GOColor c)
{
	GnmColor *sc;
	GnmColor key;

	key.go_color = c;
	key.is_auto = FALSE;

	sc = g_hash_table_lookup (style_color_hash, &key);
	if (!sc) {
		sc = style_color_new_uninterned (c, FALSE);
		g_hash_table_insert (style_color_hash, sc, sc);
	} else
		sc->ref_count++;

	return sc;
}

GnmColor *
style_color_black (void)
{
	if (!sc_black)
		sc_black = style_color_new_i8 (0, 0, 0);
	return style_color_ref (sc_black);
}

GnmColor *
style_color_white (void)
{
	if (!sc_white)
		sc_white = style_color_new_i8 (0xff, 0xff, 0xff);
	return style_color_ref (sc_white);
}

GnmColor *
style_color_grid (void)
{
	if (!sc_grid)
		sc_grid = style_color_new_i8 (0xc7, 0xc7, 0xc7);
	return style_color_ref (sc_grid);
}

/**
 * Support for Excel auto-colors.
 */

/**
 * Always black, as far as we know.
 */
GnmColor *
style_color_auto_font (void)
{
	static GnmColor *color = NULL;

	if (!color)
		color = style_color_new_uninterned (GO_COLOR_BLACK, TRUE);
	return style_color_ref (color);
}

/**
 * Always white, as far as we know.
 */
GnmColor *
style_color_auto_back (void)
{
	static GnmColor *color = NULL;

	if (!color)
		color = style_color_new_uninterned (GO_COLOR_WHITE, TRUE);
	return style_color_ref (color);
}

/**
 * Normally black, but follows grid color if so told.
 */
GnmColor *
style_color_auto_pattern (void)
{
	static GnmColor *color = NULL;

	if (!color)
		color = style_color_new_uninterned (GO_COLOR_BLACK, TRUE);
	return style_color_ref (color);
}

GnmColor *
style_color_ref (GnmColor *sc)
{
	if (sc != NULL)
		sc->ref_count++;

	return sc;
}

void
style_color_unref (GnmColor *sc)
{
	if (sc == NULL)
		return;

	g_return_if_fail (sc->ref_count > 0);

	sc->ref_count--;
	if (sc->ref_count != 0)
		return;

	g_hash_table_remove (style_color_hash, sc);
	g_free (sc);
}

gint
style_color_equal (GnmColor const *k1, GnmColor const *k2)
{
	return (k1->go_color == k2->go_color &&
		k1->is_auto == k2->is_auto);
}

static guint
color_hash (gconstpointer v)
{
	GnmColor const *k = (GnmColor const *)v;
	return k->go_color ^ k->is_auto;
}

void
gnm_color_init (void)
{
	GdkColor error;

	if (gdk_screen_get_default () != NULL) {
		/*
		 * Make sure we can see bogus attempt at getting the pixel
		 * value.  This is, by nature, not multi-head safe.
		 */
		gdk_color_parse ("cyan", &error);
		gdk_rgb_find_color (
			gdk_screen_get_default_colormap (
				    gdk_screen_get_default ()),
			&error);
	} else
		error.pixel = 0;

	gs_black.pixel = error.pixel;
	gs_white.pixel = error.pixel;
	gs_yellow.pixel = error.pixel;
	gs_lavender.pixel = error.pixel;
	gs_dark_gray.pixel = error.pixel;
	gs_light_gray.pixel = error.pixel;

	style_color_hash = g_hash_table_new (color_hash,
					     (GEqualFunc)style_color_equal);
}

static void
cb_color_leak (gpointer key, gpointer value, gpointer user_data)
{
	GnmColor *color = value;

	g_printerr ("Leaking style-color at %p [%08x].\n",
		    (void *)color,
		    color->go_color);
}

void
gnm_color_shutdown (void)
{
	/*
	 * FIXME: this doesn't really belong here, but style-border.c isn't
	 * able to clean itself up yet.
	 */
	GnmBorder *none = gnm_style_border_none ();
	style_color_unref (none->color);
	none->color = NULL;

	if (sc_black) {
		style_color_unref (sc_black);
		sc_black = NULL;
	}

	if (sc_white) {
		style_color_unref (sc_white);
		sc_white = NULL;
	}

	if (sc_grid) {
		style_color_unref (sc_grid);
		sc_grid = NULL;
	}

	g_hash_table_foreach (style_color_hash, cb_color_leak, NULL);
	g_hash_table_destroy (style_color_hash);
	style_color_hash = NULL;
}

GType
gnm_style_color_get_type (void)
{
	static GType our_type = 0;

	if (our_type == 0)
		our_type = g_boxed_type_register_static
			("GnmStyleColor",
			 (GBoxedCopyFunc)style_color_ref,
			 (GBoxedFreeFunc)style_color_unref);
	return our_type;
}
