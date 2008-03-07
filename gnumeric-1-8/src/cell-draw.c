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
#include "gnm-format.h"
#include "rendered-value.h"
#include "parse-util.h"
#include "sheet-merge.h"
#include <goffice/utils/go-color.h>

#include <gdk/gdk.h>
#include <string.h>
#include <math.h>

static char const hashes[] =
"################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################";

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
 * @h_center : The number of pango units from x1 marking the logical center
 *             of the cell.  NOTE This can be asymetric.  Passing
 *             <= 0 will use width / 2
 */
gboolean
cell_calc_layout (GnmCell const *cell, GnmRenderedValue *rv, int y_direction,
		  int width, int height, int h_center,
		  GOColor *res_color, gint *res_x, gint *res_y)
{
	int text_base;
	PangoLayout *layout;
	int indent;
	int hoffset;
	int rect_x, rect_y;
	gboolean was_drawn;

	g_return_val_if_fail (rv != NULL, FALSE);

	layout = rv->layout;
	indent = (rv->indent_left + rv->indent_right) * PANGO_SCALE;

	was_drawn = rv->drawn;
	rv->drawn = TRUE;

	if (width <= 0 || height <= 0)
		return FALSE;

	hoffset = rv->indent_left * PANGO_SCALE;

#if 0
	g_print ("%s: w=%d  h=%d\n", cell_name (cell), width, height);
#endif

	/* This rectangle has the whole area used by this cell
	 * excluding the surrounding grid lines and margins */
	rect_x = PANGO_SCALE * (1 + GNM_COL_MARGIN);
	rect_y = PANGO_SCALE * y_direction * (1 + GNM_ROW_MARGIN);

	/* if a number overflows, do special drawing */
	if (rv->layout_natural_width > width - indent &&
	    rv->might_overflow &&
	    !rv->numeric_overflow) {
		char const *text = pango_layout_get_text (layout);
		size_t textlen = strlen (text);
#if 0
		g_print ("nat=%d  w=%d  i=%d\n", rv->layout_natural_width, width, indent);
#endif
		/* This assumes that two hash marks are wider than
		   the characters in the number.  Probably ok.  */
		pango_layout_set_text (layout, hashes,
				       MIN (sizeof (hashes) - 1, 2 * textlen));
		rv->numeric_overflow = TRUE;
		rv->variable_width = TRUE;
		rv->hfilled = TRUE;
	}

	/* Special handling of error dates.  */
	if (!was_drawn && rv->numeric_overflow) {
		pango_layout_set_text (layout, hashes, -1);
		rv->variable_width = TRUE;
		rv->hfilled = TRUE;
	}

	if (rv->rotation && !rv->noborders) {
		GnmRenderedRotatedValue const *rrv = (GnmRenderedRotatedValue *)rv;
		double sin_a = rrv->rotmat.xy;
		if (sin_a < 0) {
			hoffset += (width - indent) - rv->layout_natural_width;
		}
	} else if (!rv->rotation && rv->wrap_text) {
		int wanted_width = MAX (0, width - indent);
		if (wanted_width != pango_layout_get_width (layout)) {
			pango_layout_set_wrap (layout, PANGO_WRAP_WORD_CHAR);
			pango_layout_set_width (layout, wanted_width);
			gnm_rendered_value_remeasure (rv);
		}
	} else {
		switch (rv->effective_halign) {
		case HALIGN_RIGHT:
			hoffset += (width - indent) - rv->layout_natural_width;
			break;
		case HALIGN_DISTRIBUTED:
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
						g_string_append_unichar (multi, UNICODE_ZERO_WIDTH_SPACE_C);
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

	case VALIGN_BOTTOM: {
		int dh = height - rv->layout_natural_height;
		if (rv->rotation == 0 && dh < 0)
			dh = 0;
		text_base = rect_y + y_direction * dh;
		break;
	}

	case VALIGN_DISTRIBUTED: /* dunno what this does yet */
	case VALIGN_CENTER: {
		int dh = (height - rv->layout_natural_height) / 2;
		if (rv->rotation == 0 && dh < 0)
                        dh = 0;
		text_base = rect_y + y_direction * dh;
		break;
	}

	case VALIGN_JUSTIFY:
		text_base = rect_y;
		if (!rv->vfilled && height > rv->layout_natural_height) {
			int line_count = pango_layout_get_line_count (layout);
			if (line_count > 1) {
				int spacing = (height - rv->layout_natural_height) /
					(line_count - 1);
				pango_layout_set_spacing (layout, spacing);
				gnm_rendered_value_remeasure (rv);
			}
		}
		rv->vfilled = TRUE;
		break;
	}

#if 0
	if (rv->rotation)
		g_print ("hoffset=%d,  text_base=%d,  n_width=%d, n_height=%d\n",
			 hoffset, text_base,
			 rv->layout_natural_width, rv->layout_natural_height);
#endif

	*res_color = rv->go_fore_color;
	*res_x = rect_x + hoffset;
	*res_y = text_base;

	return TRUE;
}

/*
 * This finishes a layout by pretending to draw it.  The effect is to
 * handler numerical overflow, filling, etc.
 * (Doesn't currently handle vertical fill.)
 */
void
cell_finish_layout (GnmCell *cell, GnmRenderedValue *rv,
		    int col_width,
		    gboolean inhibit_overflow)
{
	gint dummy_x, dummy_y;
	GOColor dummy_fore_color;
	int dummy_h_center = -1;  /* Affects position only.  */
	int dummy_height = 1;  /* Unhandled.  */
	gboolean might_overflow;

	if (!rv)
		rv = cell->rendered_value;

	if (rv->drawn)
		return;

	if (rv->variable_width && rv == cell->rendered_value) {
		GnmStyle const *mstyle = gnm_cell_get_style (cell);
		GOFormat const *fmt = gnm_cell_get_format (cell);
		if (!go_format_is_general (fmt)) {
			/*
			 * We get here when entering a new value in a cell
			 * with a format that has a filler, for example
			 * one of the standard accounting formats.  We need
			 * to rerender such that the filler gets a chance
			 * to expand.
			 */
			cell->rendered_value =
				gnm_rendered_value_new (cell, mstyle, TRUE,
							pango_layout_get_context (rv->layout),
							cell->base.sheet->last_zoom_factor_used);
			gnm_rendered_value_destroy (rv);
			rv = cell->rendered_value;
		}
	}

	might_overflow = rv->might_overflow;
	if (inhibit_overflow) rv->might_overflow = FALSE;
	cell_calc_layout (cell, rv, -1, col_width * PANGO_SCALE,
		dummy_height, dummy_h_center, &dummy_fore_color,
		&dummy_x, &dummy_y);
	rv->might_overflow = might_overflow;
}


/**
 * cell_draw:
 * @cell : #GnmCell const
 * @gc   : #GdkGC
 * @drawable : #GdkDrawable
 * @x1 :
 * @y1 :
 * @width : including margins and leading grid line
 * @height : including margins and leading grid line
 * @h_center :
 **/
void
cell_draw (GnmCell const *cell, GdkGC *gc, GdkDrawable *drawable,
	   int x1, int y1, int width, int height, int h_center)
{
	GOColor fore_color;
	gint x;
	gint y;
	GnmRenderedValue *rv;

	/* Get the sizes exclusive of margins and grids */
	/* Note: +1 because size_pixels includes leading gridline.  */
	height -= GNM_ROW_MARGIN + GNM_ROW_MARGIN + 1;
	width  -= GNM_COL_MARGIN + GNM_COL_MARGIN + 1;

	/* render as necessary */
	if (cell->rendered_value == NULL)
		gnm_cell_render_value ((GnmCell *)cell, TRUE);

	rv = cell->rendered_value;

	if (cell_calc_layout (cell, rv, +1,
			      width * PANGO_SCALE,
			      height * PANGO_SCALE,
			      h_center == -1 ? -1 : (h_center * PANGO_SCALE),
			      &fore_color, &x, &y)) {
		GdkColor fore_gdk;

		/* +1 to get past left grid-line.  */
		GdkRectangle rect;
		rect.x = x1 + 1 + GNM_COL_MARGIN;
		rect.y = y1 + 1 + GNM_ROW_MARGIN;
		rect.width = width;
		rect.height = height;

#if 0
		gdk_gc_set_clip_rectangle (gc, &rect);
		if (cell->pos.col == 3 && cell->pos.row == 10) {
			GdkColor col = { 0, 0, 0xffff, 0 };
			gdk_gc_set_rgb_fg_color (gc, &col);
			gdk_draw_rectangle (drawable, gc, TRUE,
					    rect.x, rect.y,
					    rect.width, rect.height);
		}
#endif

		/*
		 * HACK -- do not clip rotated cells.  This gives an
		 * approximation to the right effect.  (The right way
		 * would be to create a proper cellspan type.)
		 */
		gdk_gc_set_clip_rectangle (gc,
					   rv->rotation ? NULL : &rect);

		/* See http://bugzilla.gnome.org/show_bug.cgi?id=105322 */
		go_color_to_gdk (fore_color, &fore_gdk);
		gdk_gc_set_rgb_fg_color (gc, &fore_gdk);

		if (rv->rotation) {
			GnmRenderedRotatedValue *rrv = (GnmRenderedRotatedValue *)rv;
			PangoContext *context = pango_layout_get_context (rv->layout);
			struct GnmRenderedRotatedValueInfo const *li = rrv->lines;
			GSList *lines;

			pango_context_set_matrix (context, &rrv->rotmat);
			pango_layout_context_changed (rv->layout);
			for (lines = pango_layout_get_lines (rv->layout);
			     lines;
			     lines = lines->next, li++) {
				gdk_draw_layout_line (drawable, gc,
					x1 + PANGO_PIXELS (x + li->dx),
					y1 + PANGO_PIXELS (y + li->dy),
					lines->data);
			}
			pango_context_set_matrix (context, NULL);
			pango_layout_context_changed (rv->layout);
		} else
			gdk_draw_layout (drawable, gc,
				x1 + PANGO_PIXELS (x),
				y1 + PANGO_PIXELS (y),
				rv->layout);
	}
}
