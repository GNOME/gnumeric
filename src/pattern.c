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

#include <goffice/utils/go-pattern.h>

static GOPatternType patterns[] = {
	GO_PATTERN_SOLID,
	GO_PATTERN_GREY75,
	GO_PATTERN_GREY50,
	GO_PATTERN_GREY25,
	GO_PATTERN_GREY125,
	GO_PATTERN_GREY625,
	GO_PATTERN_HORIZ,
	GO_PATTERN_VERT,
	GO_PATTERN_DIAG,
	GO_PATTERN_REV_DIAG,
	GO_PATTERN_DIAG_CROSS,
	GO_PATTERN_THICK_DIAG_CROSS,
	GO_PATTERN_THIN_HORIZ,
	GO_PATTERN_THIN_VERT,
	GO_PATTERN_THIN_REV_DIAG,
	GO_PATTERN_THIN_DIAG,
	GO_PATTERN_THIN_HORIZ_CROSS,
	GO_PATTERN_THIN_DIAG_CROSS,
	GO_PATTERN_SMALL_CIRCLES,
	GO_PATTERN_SEMI_CIRCLES,
	GO_PATTERN_THATCH,
	GO_PATTERN_LARGE_CIRCLES,
	GO_PATTERN_BRICKS,
	GO_PATTERN_FOREGROUND_SOLID
};

/*
 * gnumeric_background_set : Set up a cairo context to paint the background
 *                              of a cell.
 * return : TRUE if there is a background to paint.
 */

static double 
gnm_get_light (guint16 c)
{
	return ((1 + c/65535.)/2);
}

gboolean
gnumeric_background_set (GnmStyle const *mstyle, cairo_t *cr,
			 gboolean const is_selected, GtkStyle *theme)
{
	int pattern;

	/*
	 * Draw the background if the PATTERN is non 0
	 * Draw a stipple too if the pattern is > 1
	 */
	pattern = gnm_style_get_pattern (mstyle);
	if (pattern > 0) {
		GOPattern gopat;
		cairo_pattern_t *crpat;
		gopat.pattern = patterns[pattern - 1];
		gopat.fore = gnm_style_get_pattern_color (mstyle)->go_color;
		gopat.back = gnm_style_get_back_color (mstyle)->go_color;
		crpat = go_pattern_create_cairo_pattern (&gopat, cr);
		cairo_set_source (cr, crpat);
		cairo_pattern_destroy (crpat);
		return TRUE;
	} else if (is_selected) {
		if (theme == NULL)
			cairo_set_source_rgb 
				(cr, .901960784, .901960784, .980392157);
		else {
			GdkColor color = theme->light[GTK_STATE_SELECTED];
			cairo_set_source_rgb
				(cr, gnm_get_light (color.red), 
				 gnm_get_light (color.green), 
				 gnm_get_light (color.blue));
		}
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
			GOColor c = back_col->go_color;
			double f = grey[pattern];

			cairo_set_source_rgb (context,
					      GO_COLOR_DOUBLE_R (c) * f,
					      GO_COLOR_DOUBLE_G (c) * f,
					      GO_COLOR_DOUBLE_B (c) * f);
		}

		/* This is a special case where the user has selected
		 * 'foreground solid', so we need to paint it the pattern
		 * color.
		 */
		 else if (pattern == 24) {
			GnmColor const *pat_col = gnm_style_get_pattern_color (mstyle);
			g_return_val_if_fail (pat_col != NULL, FALSE);

			cairo_set_source_rgba (context,
					       GO_COLOR_TO_CAIRO (pat_col->go_color));
		} else {
			GOPattern gopat;
			cairo_pattern_t *crpat;
			gopat.pattern = patterns[pattern - 1];
			gopat.fore = gnm_style_get_pattern_color (mstyle)->go_color;
			gopat.back = gnm_style_get_back_color (mstyle)->go_color;
			crpat = go_pattern_create_cairo_pattern (&gopat, context);
			cairo_set_source (context, crpat);
			cairo_pattern_destroy (crpat);
		}
		return TRUE;
	}

	return FALSE;
}


