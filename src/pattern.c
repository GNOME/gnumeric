/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * pattern.c : Support and specifications for patterns.
 *
 * Author:
 *     Jody Goldberg <jody@gnome.org>
 *
 *  (C) 1999-2003 Jody Goldberg
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "pattern.h"

#include "style-color.h"

typedef struct {
	int const x, y;
	char const pattern[8];
} gnumeric_sheet_pattern_t;

static gnumeric_sheet_pattern_t const
gnumeric_sheet_patterns[] = {
/* 0 */	{ 8, 8, /* DUMMY PLACEHOLDER */
	  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
/* 1 */	{ 8, 8, /* Solid */
	  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
/* 2 */	{ 8, 8, /* 75% */
	  { 0xbb, 0xee, 0xbb, 0xee, 0xbb, 0xee, 0xbb, 0xee } },
/* 3 */	{ 8, 8, /* 50% */
	  { 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55 } },
/* 4 */	{ 8, 8, /* 25% */
	  { 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88 } },
/* 5 */	{ 8, 8, /* 12.5% */
	  { 0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00 } },
/* 6 */	{ 8, 8, /* 6.25% */
	  { 0x20, 0x00, 0x02, 0x00, 0x20, 0x00, 0x02, 0x00 } },
/* 7 */	{ 8, 8, /* Horizontal Stripe */
	  { 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff } },
/* 8 */	{ 8, 8, /* Vertical Stripe */
	  { 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33 } },
/* 9 */	{ 8, 8, /* Reverse Diagonal Stripe */
	  { 0xcc, 0x66, 0x33, 0x99, 0xcc, 0x66, 0x33, 0x99 } },
/* 10*/	{ 8, 8, /* Diagonal Stripe */
	  { 0x33, 0x66, 0xcc, 0x99, 0x33, 0x66, 0xcc, 0x99 } },
/* 11*/	{ 8, 8, /* Diagonal Crosshatch */
	  { 0x99, 0x66, 0x66, 0x99, 0x99, 0x66, 0x66, 0x99 } },
/* 12*/	{ 8, 8, /* Thick Diagonal Crosshatch */
	  { 0xff, 0x66, 0xff, 0x99, 0xff, 0x66, 0xff, 0x99 } },
/* 13*/	{ 8, 8, /* Thin Horizontal Stripe */
	  { 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00 } },
/* 14*/	{ 8, 8, /* Thin Vertical Stripe */
	  { 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22 } },
/* 15*/	{ 8, 8, /* Thin Reverse Diagonal Stripe */
	  { 0x11, 0x22, 0x44, 0x88, 0x11, 0x22, 0x44, 0x88 } },
/* 16*/	{ 8, 8, /* Thin Diagonal Stripe */
	  { 0x88, 0x44, 0x22, 0x11, 0x88, 0x44, 0x22, 0x11 } },
/* 17*/	{ 8, 8, /* Thin Crosshatch */
	  { 0x22, 0x22, 0xff, 0x22, 0x22, 0x22, 0xff, 0x22 } },
/* 18*/	{ 8, 8, /* Thin Diagonal Crosshatch */
	  { 0x88, 0x55, 0x22, 0x55, 0x88, 0x55, 0x22, 0x55 } },
/* 19*/	{ 8, 8, /* Applix small circle */
	  { 0x99, 0x55, 0x33, 0xff, 0x99, 0x55, 0x33, 0xff } },
/* 20*/	{ 8, 8, /* Applix semicircle */
	  { 0x10, 0x10, 0x28, 0xc7, 0x01, 0x01, 0x82, 0x7c } },
/* 21*/	{ 8, 8, /* Applix small thatch */
	  { 0x22, 0x74, 0xf8, 0x71, 0x22, 0x17, 0x8f, 0x47 } },
/* 22*/	{ 8, 8, /* Applix round thatch */
	  { 0xc1, 0x80, 0x1c, 0x3e, 0x3e, 0x3e, 0x1c, 0x80 } },
/* 23*/	{ 8, 8, /* Applix Brick */
	  { 0x20, 0x20, 0x20, 0xff, 0x02, 0x02, 0x02, 0xff } },
/* 24*/	{ 8, 8, /* 100% */
	  { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } },
/* 25*/	{ 6, 6, /* 87.5% */
	  { 0xfe, 0xef, 0xfb, 0xdf, 0xfd, 0xf7, 0x00, 0x00 } }
};

static GdkPixmap *
gnumeric_pattern_get_stipple (GdkDrawable *drawable, gint index)
{
	static GdkPixmap *patterns[GNUMERIC_SHEET_PATTERNS + 1];
	static GdkDrawable *last_drawable[GNUMERIC_SHEET_PATTERNS + 1];

	g_return_val_if_fail (index >= 0, NULL);
	g_return_val_if_fail (index <= GNUMERIC_SHEET_PATTERNS, NULL);
	g_return_val_if_fail (drawable != NULL, NULL);

	if (index == 0)
		return NULL;

	if (drawable != last_drawable[index] && patterns[index]) {
		g_object_unref (patterns[index]);
		patterns[index] = NULL;
	}

	if (patterns[index] == NULL) {
		gnumeric_sheet_pattern_t const * pat = gnumeric_sheet_patterns + index;
		patterns[index] = gdk_bitmap_create_from_data (
			drawable, pat->pattern, pat->x, pat->y);
		last_drawable[index] = drawable;
	}

	return patterns[index];
}

/*
 * gnumeric_background_set_gc : Set up a GdkGC to paint the background
 *                              of a cell.
 * return : TRUE if there is a background to paint.
 */
gboolean
gnumeric_background_set_gc (GnmStyle const *mstyle, GdkGC *gc,
			    FooCanvas *canvas,
			    gboolean const is_selected)
{
	GdkColormap *cmap = gdk_gc_get_colormap (gc);
	int pattern;

	/*
	 * Draw the background if the PATTERN is non 0
	 * Draw a stipple too if the pattern is > 1
	 */
	pattern = gnm_style_get_pattern (mstyle);
	if (pattern > 0) {
		const GdkColor *back;
		GnmColor const *back_col = gnm_style_get_back_color (mstyle);
		g_return_val_if_fail (back_col != NULL, FALSE);

		back = is_selected ? &back_col->gdk_selected_color : &back_col->gdk_color;

		if (pattern > 1) {
			GdkScreen *screen = gtk_widget_get_screen (GTK_WIDGET (canvas));
			GdkDrawable *drawable = gdk_screen_get_root_window (screen);
			GdkGCValues values;
			GnmColor const *pat_col = gnm_style_get_pattern_color (mstyle);
			g_return_val_if_fail (pat_col != NULL, FALSE);

			values.fill = GDK_OPAQUE_STIPPLED;
			values.foreground = pat_col->gdk_color;
			gdk_rgb_find_color (cmap, &values.foreground);
			values.background = *back;
			gdk_rgb_find_color (cmap, &values.background);
			values.stipple =
				gnumeric_pattern_get_stipple (drawable, pattern);
			gdk_gc_set_values (gc, &values,
					   GDK_GC_FILL | GDK_GC_FOREGROUND |
					   GDK_GC_BACKGROUND | GDK_GC_STIPPLE);
			foo_canvas_set_stipple_origin (canvas, gc);
		} else {
			GdkGCValues values;

			values.fill = GDK_SOLID;
			values.foreground = *back;
			gdk_rgb_find_color (cmap, &values.foreground);
			gdk_gc_set_values (gc, &values, GDK_GC_FILL | GDK_GC_FOREGROUND);
		}
		return TRUE;
	} else if (is_selected) {
		GdkGCValues values;

		values.foreground = gs_lavender;
		gdk_rgb_find_color (cmap, &values.foreground);
		values.fill = GDK_SOLID;
		gdk_gc_set_values (gc, &values, GDK_GC_FILL | GDK_GC_FOREGROUND);
	}
	return FALSE;
}

/*
 * gnumeric_background_set_gtk : Set up a cairo context to paint the
 *                              background of a cell when printing.
 * return : TRUE if there is a background to paint.
 */

gboolean
gnumeric_background_set_gtk (GnmStyle const *mstyle, cairo_t *context)
{
	int pattern;

	/*
	 * Draw the background if the PATTERN is non 0
	 * Draw a stipple too if the pattern is > 1
	 */
	pattern = gnm_style_get_pattern (mstyle);
	if (pattern > 0) {
		GnmColor const *back_col = gnm_style_get_back_color (mstyle);

		g_return_val_if_fail (back_col != NULL, FALSE);

		/* Support printing grey scale patterns.
		 * This effectively applies a brightness threshold to get
		 * the desired results.
		 * The array used provides good real-life results.
		 * The true_grey array is theoretically correct but doesn't
		 * distinguish the shades clearly.
		 *
		 * Note: The first element of the grey array isn't used.
		 *
		 * FIXME: This code assumes the pattern colour is black, which
		 * is normally true (gnumeric selects it automatically).
		 * But correctly we should mix the pattern color against
		 * the background color. We handle the easy (pattern == 24)
		 * case below.
		 */
		if (pattern >= 1 && pattern <= 6) {
			static double const grey[] = { 1.0, 1.0, .30, .45, .60, .75, .90};
#if 0
			static double const true_grey[] = { 1.0, 1.0, .0625, .125, .25, .50, .75};
#endif

			cairo_set_source_rgb (context,
				back_col->gdk_color.red * grey[pattern]    / (double) 0xffff,
				back_col->gdk_color.green * grey[pattern]  / (double) 0xffff,
				back_col->gdk_color.blue * grey[pattern]   / (double) 0xffff);
		}

		/* This is a special case where the user has selected
		 * 'foreground solid', so we need to paint it the pattern
		 * color.
		 */
		if (pattern == 24) {
			GnmColor const *pat_col = gnm_style_get_pattern_color (mstyle);
			g_return_val_if_fail (pat_col != NULL, FALSE);

			cairo_set_source_rgb (context,
				pat_col->gdk_color.red   / (double) 0xffff,
				pat_col->gdk_color.green / (double) 0xffff,
				pat_col->gdk_color.blue  / (double) 0xffff);

		}
#if 0
		/* FIXME: How to do the other patterns? */
		if (pattern > 1) {
			GnmColor const *pat_col = gnm_style_get_pattern_color (mstyle);
			g_return_val_if_fail (pat_col != NULL, FALSE);

			gdk_gc_set_fill (gc, GDK_OPAQUE_STIPPLED);
			gdk_gc_set_rgb_fg_color (gc, &pat_col->color);
			gdk_gc_set_rgb_bg_color (gc, back);
			gdk_gc_set_stipple (gc,
					    gnumeric_pattern_get_stipple (XXX,
									  pattern));
			foo_canvas_set_stipple_origin (canvas, gc);
		} else {
			gdk_gc_set_fill (gc, GDK_SOLID);
			gdk_gc_set_rgb_fg_color (gc, back);
		}
#endif
		return TRUE;
	}

	return FALSE;
}


