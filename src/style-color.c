/*
 * color.c: Color allocation on the Gnumeric spreadsheet
 *
 * Author:
 *  Miguel de Icaza (miguel@kernel.org)
 *
 * We keep our own color context, as the color allocation might take place
 * before any of our Canvases are realized.
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "style-color.h"

#include <gtk/gtk.h>
#include <gal/widgets/e-colors.h>

static gboolean color_inited = FALSE;

/* Public colors: shared by all of our items in Gnumeric */
GdkColor gs_white, gs_black, gs_light_gray, gs_dark_gray, gs_red, gs_lavender, gs_green;
static GHashTable *style_color_hash;

StyleColor *
style_color_new_name (char const *name)
{
	GdkColor c;

	gdk_color_parse (name, &c);
	return style_color_new (c.red, c.green, c.blue);
}

static StyleColor *
style_color_new_uninterned (gushort red, gushort green, gushort blue,
			    gboolean is_auto)
{
	StyleColor *sc = g_new (StyleColor, 1);

	sc->color.red = red;
	sc->color.green = green;
	sc->color.blue = blue;
	sc->red = red;
	sc->green = green;
	sc->blue = blue;
	sc->name = NULL;
	sc->color.pixel = e_color_alloc (red, green, blue);
	sc->is_auto = is_auto;

	/* Make a contrasting selection color with an alpha of .5 */
	red   += (gs_lavender.red   - red)/2;
	green += (gs_lavender.green - green)/2;
	blue  += (gs_lavender.blue  - blue)/2;
	sc->selected_color.red = red;
	sc->selected_color.green = green;
	sc->selected_color.blue = blue;
	sc->selected_color.pixel = e_color_alloc (red, green, blue);

	sc->ref_count = 1;

	return sc;
}

StyleColor *
style_color_new (gushort red, gushort green, gushort blue)
{
	StyleColor *sc;
	StyleColor key;

	key.red   = red;
	key.green = green;
	key.blue  = blue;
	key.is_auto = FALSE;

	sc = g_hash_table_lookup (style_color_hash, &key);
	if (!sc) {
		sc = style_color_new_uninterned (red, green, blue, FALSE);
		g_hash_table_insert (style_color_hash, sc, sc);
	} else
		sc->ref_count++;

	return sc;
}

/* scale 8 bit/color ->  16 bit/color by cloning */
StyleColor *
style_color_new_i8 (guint8 red, guint8 green, guint8 blue)
{
	gushort red16, green16, blue16;

	red16 =   ((gushort) red) << 8   | red;
	green16 = ((gushort) green) << 8 | green;
	blue16 =  ((gushort) blue) << 8  | blue;

	return style_color_new (red16, green16, blue16);
}

StyleColor *
style_color_black (void)
{
	static StyleColor *color = NULL;

	if (!color)
		color = style_color_new (0, 0, 0);
	return style_color_ref (color);
}

StyleColor *
style_color_white (void)
{
	static StyleColor *color = NULL;

	if (!color)
		color = style_color_new (0xffff, 0xffff, 0xffff);
	return style_color_ref (color);
}

StyleColor *
style_color_grid (void)
{
	static StyleColor *color = NULL;

	if (!color) /* Approx gray78 */
		color = style_color_new (0xc7c7, 0xc7c7, 0xc7c7);
	return style_color_ref (color);
}

/**
 * Support for Excel auto-colors.
 */

/**
 * Always black, as far as we know.
 */
StyleColor *
style_color_auto_font (void)
{
	static StyleColor *color = NULL;

	if (!color)
		color = style_color_new_uninterned (0, 0, 0, TRUE);
	return style_color_ref (color);
}

/**
 * Always white, as far as we know.
 */
StyleColor *
style_color_auto_back (void)
{
	static StyleColor *color = NULL;

	if (!color)
		color = style_color_new_uninterned (0xffff, 0xffff, 0xffff,
						  TRUE);
	return style_color_ref (color);
}

/**
 * Normally black, but follows grid color if so told.
 */
StyleColor *
style_color_auto_pattern (void)
{
	static StyleColor *color = NULL;

	if (!color)
		color = style_color_new_uninterned (0, 0, 0, TRUE);
	return style_color_ref (color);
}

StyleColor *
style_color_ref (StyleColor *sc)
{
	if (sc != NULL)
		sc->ref_count++;

	return sc;
}

void
style_color_unref (StyleColor *sc)
{
	if (sc == NULL)
		return;

	g_return_if_fail (sc->ref_count > 0);

	sc->ref_count--;
	if (sc->ref_count != 0)
		return;

	/*
	 * There is no need to deallocate colors, as they come from
	 * the GDK Color Context
	 */
	g_hash_table_remove (style_color_hash, sc);
	g_free (sc);
}

gint
style_color_equal (const StyleColor *k1, const StyleColor *k2)
{
	if (k1->red   == k2->red &&
	    k1->green == k2->green &&
	    k1->blue  == k2->blue &&
	    k1->is_auto == k2->is_auto)
		return 1;

	return 0;
}

static guint
color_hash (gconstpointer v)
{
	const StyleColor *k = (const StyleColor *)v;

	return (k->red << 24) | (k->green << 16) | (k->blue << 8) |
		(k->is_auto);
}

void
gnumeric_color_init (void)
{
	GdkColormap *colormap = gtk_widget_get_default_colormap ();

	e_color_init ();

	/* Allocate the default colors */
	gdk_color_white (colormap, &gs_white);
	gdk_color_black (colormap, &gs_black);

	e_color_alloc_name (NULL, "gray78",  &gs_light_gray);
	e_color_alloc_name (NULL, "gray20",  &gs_dark_gray);
	e_color_alloc_name (NULL, "red",     &gs_red);
	e_color_alloc_name (NULL, "lavender",&gs_lavender);
	e_color_alloc_name (NULL, "DarkSeaGreen",&gs_green);

	style_color_hash  = g_hash_table_new (color_hash,
					      (GEqualFunc) style_color_equal);

	color_inited = TRUE;
}

void
gnumeric_color_shutdown (void)
{
	g_return_if_fail (color_inited);

	color_inited = FALSE;
	g_hash_table_destroy (style_color_hash);
	style_color_hash = NULL;
}
