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
#include <gnome.h>
#include "color.h"
#include <gal/widgets/e-colors.h>

static int color_inited;

/* Public colors: shared by all of our items in Gnumeric */
GdkColor gs_white, gs_black, gs_light_gray, gs_dark_gray, gs_red, gs_lavender;

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

	color_inited = 1;
}
