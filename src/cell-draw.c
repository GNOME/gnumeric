/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * cell-draw.c: Cell drawing on screen
 *
 * Author:
 *    Miguel de Icaza 1998, 1999 (miguel@kernel.org)
 *    Jody Goldberg 2000-2002    (jody@gnome.org)
 *    Morten Welinder 2003       (terra@gnome.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "cell-draw.h"

#include "style.h"
#include "cell.h"
#include "sheet.h"
#include "rendered-value.h"
#include "parse-util.h"

#include <gdk/gdk.h>
#include <string.h>

static char const hashes[] =
"################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################";

gunichar const zero_width_space = 0x200b;

/*
 *             G      G
 *             r      r
 *             i      i
 *             d      d
 *
 *  Grid line  a------+
 *             |mmmmmm|
 *             |m    m|
 *             |mmmmmm|
 *  Grid line  +------+
 *
 *  'm' == margin
 *  ' ' == space for contents
 *
 * @h_center : The number of pixels from x1 marking the logical center
 *             of the cell.  NOTE This can be asymetric.  Passing
 *             <= 0 will use width / 2
 */

gboolean
cell_calc_layout (GnmCell const *cell, RenderedValue *rv, int y_direction,
		  int width, int height, int h_center,
		  GdkColor **res_color, gint *res_x, gint *res_y)
{
	ColRowInfo const * const ci = cell->col_info; /* DEPRECATED */
	ColRowInfo const * const ri = cell->row_info; /* DEPRECATED */
	int text_base;
	PangoLayout *layout;
	int indent;
	int hoffset;
	int rect_x, rect_y;

	g_return_val_if_fail (rv != NULL, FALSE);

	layout = rv->layout;
	indent = rv->indent_left + rv->indent_right;

	if (width <= 0 || height <= 0)
		return FALSE;

	hoffset = rv->indent_left;

	/* This rectangle has the whole area used by this cell
	 * excluding the surrounding grid lines and margins */
	rect_x = 1 + ci->margin_a;
	rect_y = y_direction * (1 + ri->margin_a);

	/* if a number overflows, do special drawing */
	if (rv->layout_natural_width > width - indent &&
	    cell_is_number (cell) &&
	    !rv->numeric_overflow &&
	    !rv->display_formula) {
		char const *text = pango_layout_get_text (layout);
		/* This assumes that hash marks are wider than
		   the characters in the number.  Probably ok.  */
		pango_layout_set_text (layout, hashes,
				       MIN (sizeof (hashes) - 1, strlen (text)));
		rv->numeric_overflow = TRUE;
		rv->variable_width = TRUE;
		rv->hfilled = TRUE;
	}

	if (rv->wrap_text) {
		int wanted_width = MAX (0, (width - indent) * PANGO_SCALE);
		if (wanted_width != pango_layout_get_width (layout)) {
			pango_layout_set_width (layout, wanted_width);
			rendered_value_remeasure (rv);
		}
	} else {
		switch (rv->effective_halign) {
		case HALIGN_RIGHT:
			hoffset += (width - indent) - rv->layout_natural_width;
			break;
		case HALIGN_CENTER:
			if (h_center == -1)
				h_center = width / 2;
			hoffset += h_center + (-indent - rv->layout_natural_width) / 2;
			break;
		case HALIGN_CENTER_ACROSS_SELECTION:
			hoffset += ((width - indent) - rv->layout_natural_width) / 2;
			break;
		case HALIGN_FILL:
			if (!rv->hfilled &&
			    rv->layout_natural_width > 0 &&
			    width - indent >= 2 * rv->layout_natural_width) {
				/*
				 * We ignore kerning between copies in calculating the number
				 * of copies needed.  Instead we toss in a zero-width-space.
				 */
				int copies = (width - indent) / rv->layout_natural_width;
				char const *copy1 = pango_layout_get_text (layout);
				size_t len1 = strlen (copy1);
				GString *multi = g_string_sized_new ((len1 + 6) * copies);
				int i;
				for (i = 0; i < copies; i++) {
					if (i)
						g_string_append_unichar (multi, zero_width_space);
					g_string_append_len (multi, copy1, len1);
				}
				pango_layout_set_text (layout, multi->str, multi->len);
				g_string_free (multi, TRUE);
			}
			rv->hfilled = TRUE;
			break;

#ifndef DEBUG_SWITCH_ENUM
		default:
#endif
		case HALIGN_GENERAL:
			g_warning ("Unhandled horizontal alignment.");
		case HALIGN_LEFT:
			break;
		}
	}

	switch (rv->effective_valign) {
#ifndef DEBUG_SWITCH_ENUM
	default:
		g_warning ("Unhandled vertical alignment.");
		/* Fall through.  */
#endif
	case VALIGN_TOP:
		text_base = rect_y;
		break;

	case VALIGN_BOTTOM:
		text_base = rect_y + y_direction * MAX (0, height - rv->layout_natural_height);
		break;

	case VALIGN_CENTER:
		text_base = rect_y + y_direction * MAX (0, (height - rv->layout_natural_height) / 2);
		break;

	case VALIGN_JUSTIFY:
		text_base = rect_y;
		if (!rv->vfilled && height > rv->layout_natural_height) {
			int line_count = pango_layout_get_line_count (layout);
			if (line_count > 1) {
				int spacing = PANGO_SCALE * (height - rv->layout_natural_height) /
					(line_count - 1);
				pango_layout_set_spacing (layout, spacing);
				rendered_value_remeasure (rv);
			}
		}
		rv->vfilled = TRUE;
		break;
	}

#if 0
	g_print ("width=%d, n_width=%d, h_center=%d\n",
		 width, rv->layout_natural_width, h_center);
#endif

	*res_color = &rv->color;
	*res_x = rect_x + hoffset;
	*res_y = text_base;

	return TRUE;
}


void
cell_draw (GnmCell const *cell, GdkGC *gc, GdkDrawable *drawable,
	   int x1, int y1, int width, int height, int h_center)
{
	GdkColor *color; 
	gint x;
	gint y;
	RenderedValue *rv = cell->rendered_value;
	ColRowInfo const * const ci = cell->col_info;
	ColRowInfo const * const ri = cell->row_info;

	/* Get the sizes exclusive of margins and grids */
	/* FIXME : all callers will eventually pass in their cell size */
	/* Note: +1 because size_pixels includes right gridline.  */
	if (width < 0) /* DEPRECATED */
		width  = ci->size_pixels - (ci->margin_b + ci->margin_a + 1);
	if (height < 0) /* DEPRECATED */
		height = ri->size_pixels - (ri->margin_b + ri->margin_a + 1);

	if (cell_calc_layout (cell, rv, +1,
			      width, height, h_center,
			      &color, &x, &y)) {
		/* +1 to get past left grid-line.  */
		GdkRectangle rect;
		rect.x = x1 + 1 + ci->margin_a;
		rect.y = y1 + 1 + ri->margin_a;
		rect.width = width;
		rect.height = height;

		gdk_gc_set_clip_rectangle (gc, &rect);

#if 0
		if (cell->pos.col == 3 && cell->pos.row == 10) {
			GdkColor col = { 0, 0, 0xffff, 0 };
			gdk_gc_set_rgb_fg_color (gc, &col);
			gdk_draw_rectangle (drawable, gc, TRUE,
					    rect.x, rect.y,
					    rect.width, rect.height);
		}
#endif

		gdk_gc_set_clip_rectangle (gc, &rect);
		
		/* See http://bugzilla.gnome.org/show_bug.cgi?id=105322 */
		gdk_gc_set_rgb_fg_color (gc, color);
		
		gdk_draw_layout (drawable, gc, x1 + x, y1 + y, rv->layout);
	}
}
