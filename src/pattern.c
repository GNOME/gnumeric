/*
 * pattern.c : Support and specifications for patterns.
 *
 * Author:
 *     Jody Goldberg <jgoldberg@home.com>
 *
 *  (C) 1999, 2000 Jody Goldberg
 */
#include "config.h"
#include "pattern.h"
#include "color.h"

typedef struct {
	char pattern [8];
} gnumeric_sheet_pattern_t;

static gnumeric_sheet_pattern_t const
gnumeric_sheet_patterns [GNUMERIC_SHEET_PATTERNS] = {
	{ /* Solid */
	  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ /* 75% */
	  { 0xbb, 0xee, 0xbb, 0xee, 0xbb, 0xee, 0xbb, 0xee } },
	{ /* 50% */
	  { 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55 } },
	{ /* 25% */
	  { 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88 } },
	{ /* 12.5% */
	  { 0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00 } },
	{ /* 6.25% */
	  { 0x20, 0x00, 0x02, 0x00, 0x20, 0x00, 0x02, 0x00 } },
	{ /* Horizontal Stripe */
	  { 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff } },
	{ /* Vertical Stripe */
	  { 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33 } },
	{ /* Reverse Diagonal Stripe */
	  { 0xcc, 0x66, 0x33, 0x99, 0xcc, 0x66, 0x33, 0x99 } },
	{ /* Diagonal Stripe */
	  { 0x33, 0x66, 0xcc, 0x99, 0x33, 0x66, 0xcc, 0x99 } },
	{ /* Diagonal Crosshatch */
	  { 0x99, 0x66, 0x66, 0x99, 0x99, 0x66, 0x66, 0x99 } },
	{ /* Thick Diagonal Crosshatch */
	  { 0xff, 0x66, 0xff, 0x99, 0xff, 0x66, 0xff, 0x99 } },
	{ /* Thin Horizontal Stripe */
	  { 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00  } },
	{ /* Thin Vertical Stripe */
	  { 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22 } },
	{ /* Thin Reverse Diagonal Stripe */
	  { 0x11, 0x22, 0x44, 0x88, 0x11, 0x22, 0x44, 0x88 } },
	{ /* Thin Diagonal Stripe */
	  { 0x88, 0x44, 0x22, 0x11, 0x88, 0x44, 0x22, 0x11 } },
	{ /* Thin Crosshatch */
	  { 0x22, 0x22, 0xff, 0x22, 0x22, 0x22, 0xff, 0x22 } },
	{ /* Thin Diagonal Crosshatch */
	  { 0x88, 0x55, 0x22, 0x55, 0x88, 0x55, 0x22, 0x55, } },
};

GdkPixmap *
gnumeric_pattern_get_stipple (gint const index)
{
	static GdkPixmap *patterns [GNUMERIC_SHEET_PATTERNS];
	static gboolean	  need_init = TRUE;

	/* Initialize the patterns to NULL */
	if (need_init) {
		int i;
		for (i = GNUMERIC_SHEET_PATTERNS; --i >= 0 ;)
			patterns [i] = NULL;
	}

	g_return_val_if_fail (index >= 0, NULL);
	g_return_val_if_fail (index <= GNUMERIC_SHEET_PATTERNS, NULL);

	if (index == 0)
		return NULL;

	if (patterns [index-1] == NULL) {
		patterns [index-1] = gdk_bitmap_create_from_data (
			NULL,
			gnumeric_sheet_patterns [index-1].pattern,
			8, 8);
	}

	return patterns [index-1];
}

/*
 * gnumeric_background_set_gc : Set up a GdkGC to paint the background
 *                              of a cell.
 * return : TRUE if there is a background to paint.
 */
gboolean
gnumeric_background_set_gc (MStyle *mstyle, GdkGC *gc,
			    GnomeCanvas *canvas,
			    gboolean const is_selected)
{
	int pattern;

	/*
	 * Draw the background if the PATTERN is non 0
	 * Draw a stipple too if the pattern is > 1
	 */
	if (!mstyle_is_element_set (mstyle, MSTYLE_PATTERN))
		return FALSE;
	pattern = mstyle_get_pattern (mstyle);
	if (pattern > 0) {
		GdkColor   *back;
		StyleColor *back_col =
			mstyle_get_color (mstyle, MSTYLE_COLOR_BACK);
		g_return_val_if_fail (back_col != NULL, FALSE);

		back = is_selected ? &back_col->color : &back_col->selected_color;

		if (pattern > 1) {
			StyleColor *pat_col =
				mstyle_get_color (mstyle, MSTYLE_COLOR_PATTERN);
			g_return_val_if_fail (pat_col != NULL, FALSE);

			gdk_gc_set_fill (gc, GDK_OPAQUE_STIPPLED);
			gdk_gc_set_foreground (gc, &pat_col->color);
			gdk_gc_set_background (gc, back);
			gdk_gc_set_stipple (gc, gnumeric_pattern_get_stipple (pattern));
			gnome_canvas_set_stipple_origin (canvas, gc);
		} else {
			gdk_gc_set_fill (gc, GDK_SOLID);
			gdk_gc_set_foreground (gc, back);
		}
		return TRUE;
	} else {
		/* Set this in case we have a spanning column */
		gdk_gc_set_fill (gc, GDK_SOLID);
		gdk_gc_set_foreground (gc, is_selected ? &gs_lavender : &gs_white);
	}
	return is_selected;
}

