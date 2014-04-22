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

static GHashTable *style_color_hash;
static GnmColor *sc_black;
static GnmColor *sc_white;
static GnmColor *sc_grid;
static GnmColor *sc_auto_back;
static GnmColor *sc_auto_font;
static GnmColor *sc_auto_pattern;

static GnmColor *
gnm_color_new_uninterned (GOColor c, gboolean is_auto)
{
	GnmColor *sc = g_new (GnmColor, 1);

	sc->go_color = c;
	sc->is_auto = !!is_auto;
	sc->ref_count = 1;

	return sc;
}

GnmColor *
gnm_color_new_rgba16 (guint16 red, guint16 green, guint16 blue, guint16 alpha)
{
	return gnm_color_new_rgba8 (red >> 8, green >> 8, blue >> 8, alpha >> 8);
}

GnmColor *
gnm_color_new_pango (PangoColor const *c)
{
	return gnm_color_new_rgba16 (c->red, c->green, c->blue, 0xffff);
}

GnmColor *
gnm_color_new_gdk (GdkRGBA const *c)
{
	/*
	 * The important property here is that a color #rrggbb
	 * (i.e., an 8-bit color) roundtrips correctly when
	 * translated into GdkRGBA using /255 and back.  Using
	 * multiplication by 256 here makes rounding unnecessary.
	 */

	guint8 r8 = CLAMP (c->red * 256, 0, 255);
	guint8 g8 = CLAMP (c->green * 256, 0, 255);
	guint8 b8 = CLAMP (c->blue * 256, 0, 255);
	guint8 a8 = CLAMP (c->alpha * 256, 0, 255);

	return gnm_color_new_rgba8 (r8, g8, b8, a8);
}

GnmColor *
gnm_color_new_rgba8 (guint8 red, guint8 green, guint8 blue, guint8 alpha)
{
	return gnm_color_new_go (GO_COLOR_FROM_RGBA (red, green, blue, alpha));
}

GnmColor *
gnm_color_new_rgb8 (guint8 red, guint8 green, guint8 blue)
{
	return gnm_color_new_rgba8 (red, green, blue, 0xff);
}

GnmColor *
gnm_color_new_go (GOColor c)
{
	GnmColor *sc;
	GnmColor key;

	key.go_color = c;
	key.is_auto = FALSE;

	sc = g_hash_table_lookup (style_color_hash, &key);
	if (!sc) {
		sc = gnm_color_new_uninterned (c, FALSE);
		g_hash_table_insert (style_color_hash, sc, sc);
	} else
		sc->ref_count++;

	return sc;
}

GnmColor *
style_color_black (void)
{
	if (!sc_black)
		sc_black = gnm_color_new_rgb8 (0, 0, 0);
	return style_color_ref (sc_black);
}

GnmColor *
style_color_white (void)
{
	if (!sc_white)
		sc_white = gnm_color_new_rgb8 (0xff, 0xff, 0xff);
	return style_color_ref (sc_white);
}

GnmColor *
style_color_grid (void)
{
	if (!sc_grid)
		sc_grid = gnm_color_new_rgb8 (0xc7, 0xc7, 0xc7);
	return style_color_ref (sc_grid);
}

/*
 * Support for Excel auto-colors.
 */

/**
 * style_color_auto_font:
 *
 * Always black, as far as we know.
 */
GnmColor *
style_color_auto_font (void)
{
	if (!sc_auto_font)
		sc_auto_font = gnm_color_new_uninterned (GO_COLOR_BLACK, TRUE);
	return style_color_ref (sc_auto_font);
}

/**
 * style_color_auto_back:
 *
 * Always white, as far as we know.
 */
GnmColor *
style_color_auto_back (void)
{
	if (!sc_auto_back)
		sc_auto_back = gnm_color_new_uninterned (GO_COLOR_WHITE, TRUE);
	return style_color_ref (sc_auto_back);
}

/**
 * style_color_auto_pattern:
 *
 * Normally black, but follows grid color if so told.
 */
GnmColor *
style_color_auto_pattern (void)
{
	if (!sc_auto_pattern)
		sc_auto_pattern = gnm_color_new_uninterned (GO_COLOR_BLACK, TRUE);
	return style_color_ref (sc_auto_pattern);
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

GType
gnm_color_get_type (void)
{
	static GType t = 0;

	if (t == 0)
		t = g_boxed_type_register_static ("GnmColor",
			 (GBoxedCopyFunc)style_color_ref,
			 (GBoxedFreeFunc)style_color_unref);
	return t;
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

	if (sc_auto_back) {
		style_color_unref (sc_auto_back);
		sc_auto_back = NULL;
	}

	if (sc_auto_font) {
		style_color_unref (sc_auto_font);
		sc_auto_font = NULL;
	}

	if (sc_auto_pattern) {
		style_color_unref (sc_auto_pattern);
		sc_auto_pattern = NULL;
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
