/*
 * pattern.c : Support and specifications for patterns.
 *
 * Author:
 *     Jody Goldberg <jody@gnome.org>
 *
 *  (C) 1999-2003 Jody Goldberg
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <pattern.h>

#include <style-color.h>

#include <goffice/utils/go-pattern.h>

static const GOPatternType patterns[] = {
	GO_PATTERN_SOLID, /* dummy filler */
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

/**
 * gnm_pattern_background_set:
 * @mstyle:
 * @cr:
 * @is_selected:
 * @ctxt:
 *
 * Returns: %TRUE if there is a background to paint.
 *
 * Set up a cairo context to paint the background of a cell.
 */

gboolean
gnm_pattern_background_set (GnmStyle const *mstyle, cairo_t *cr,
			    gboolean const is_selected, GtkStyleContext *ctxt)
{
	int pattern;

	g_return_val_if_fail (!is_selected || ctxt != NULL, FALSE);

	/*
	 * Draw the background if the PATTERN is non 0
	 * Draw a stipple too if the pattern is > 1
	 */
	pattern = gnm_style_get_pattern (mstyle);
	if (pattern > 0 && pattern < GNM_PATTERNS_MAX) {
		GOPattern gopat;
		cairo_pattern_t *crpat;
		gopat.pattern = patterns[pattern];
		gopat.fore = gnm_style_get_pattern_color (mstyle)->go_color;
		gopat.back = gnm_style_get_back_color (mstyle)->go_color;
		if (is_selected) {
			GOColor light;
			GdkRGBA rgba;
			gtk_style_context_get_background_color
				(ctxt, GTK_STATE_FLAG_SELECTED, &rgba);
			light = GO_COLOR_FROM_GDK_RGBA (rgba);
			gopat.fore = GO_COLOR_INTERPOLATE (light, gopat.fore, .5);
			gopat.back = GO_COLOR_INTERPOLATE (light, gopat.back, .5);
		}
		crpat = go_pattern_create_cairo_pattern (&gopat, cr);
		if (crpat)
			cairo_set_source (cr, crpat);
		cairo_pattern_destroy (crpat);
		return TRUE;
	} else if (is_selected) {
		GdkRGBA rgba;
		GOColor color;

		gtk_style_context_get_background_color (ctxt, GTK_STATE_FLAG_SELECTED, &rgba);
		color = GO_COLOR_FROM_GDK_RGBA (rgba);
		/* Make a lighter version. */
		color = GO_COLOR_INTERPOLATE (GO_COLOR_WHITE, color, .5);
		cairo_set_source_rgba (cr, GO_COLOR_TO_CAIRO (color));
	}
	return FALSE;
}
