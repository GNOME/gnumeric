/*
 * color.c: Color allocation on the Gnumeric spreadsheet
 *
 * Author:
 *  Miguel de Icaza (miguel@kernel.org)
 *
 * We keep our own color context, as the color allocation might take place
 * before any of our Canvases are realized.
 */
#include <config.h>
#include <gtk/gtk.h>
#include "style-color.h"
#include <gal/widgets/e-colors.h>

static gboolean color_inited = FALSE;

/* Public colors: shared by all of our items in Gnumeric */
GdkColor gs_white, gs_black, gs_light_gray, gs_dark_gray, gs_red, gs_lavender;
static GHashTable *style_color_hash;

StyleColor *
style_color_new_name (char const *name)
{
	GdkColor c;

	gdk_color_parse (name, &c);
	return style_color_new (c.red, c.green, c.blue);
}

StyleColor *
style_color_new (gushort red, gushort green, gushort blue)
{
	StyleColor *sc;
	StyleColor key;

	key.red   = red;
	key.green = green;
	key.blue  = blue;

	sc = g_hash_table_lookup (style_color_hash, &key);
	if (!sc) {
		sc = g_new (StyleColor, 1);

		key.color.red = red;
		key.color.green = green;
		key.color.blue = blue;
		sc->color = key.color;
		sc->red = red;
		sc->green = green;
		sc->blue = blue;
		sc->name = NULL;
		sc->color.pixel = e_color_alloc (red, green, blue);

		/* Make a contrasting selection color with an alpha of .5 */
		red   += (gs_lavender.red   - red)/2;
		green += (gs_lavender.green - green)/2;
		blue  += (gs_lavender.blue  - blue)/2;
		sc->selected_color.red = red;
		sc->selected_color.green = green;
		sc->selected_color.blue = blue;
		sc->selected_color.pixel = e_color_alloc (red, green, blue);

		g_hash_table_insert (style_color_hash, sc, sc);
		sc->ref_count = 0;
	}
	sc->ref_count++;

	return sc;
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

static gint
color_equal (gconstpointer v, gconstpointer v2)
{
	const StyleColor *k1 = (const StyleColor *) v;
	const StyleColor *k2 = (const StyleColor *) v2;

	if (k1->red   == k2->red &&
	    k1->green == k2->green &&
	    k1->blue  == k2->blue)
		return 1;

	return 0;
}

static guint
color_hash (gconstpointer v)
{
	const StyleColor *k = (const StyleColor *)v;

	return (k->red << 16) | (k->green << 8) | (k->blue);
}

void
gnumeric_color_init (void)
{
	GdkColormap *colormap = gtk_widget_get_default_colormap ();

	e_color_init ();

	/* Allocate the default colors */
	gdk_color_white (colormap, &gs_white);
	gdk_color_black (colormap, &gs_black);

	e_color_alloc_name ("gray78",  &gs_light_gray);
	e_color_alloc_name ("gray20",  &gs_dark_gray);
	e_color_alloc_name ("red",     &gs_red);
	e_color_alloc_name ("lavender",&gs_lavender);

	style_color_hash  = g_hash_table_new (color_hash, color_equal);

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
