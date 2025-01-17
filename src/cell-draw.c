/*
 * cell-draw.c: Cell drawing on screen
 *
 * Author:
 *    Miguel de Icaza 1998, 1999 (miguel@kernel.org)
 *    Jody Goldberg 2000-2002    (jody@gnome.org)
 *    Morten Welinder 2003       (terra@gnome.org)
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <cell-draw.h>

#include <style.h>
#include <cell.h>
#include <sheet.h>
#include <gnm-format.h>
#include <rendered-value.h>
#include <parse-util.h>
#include <sheet-merge.h>
#include <goffice/goffice.h>

#include <gdk/gdk.h>
#include <string.h>
#include <math.h>

static char const hashes[] =
"################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################";

static gboolean
cell_draw_simplify_cb (PangoAttribute *attribute,
		       gboolean *recalc_height)
{
	gboolean full_width = (attribute->start_index == 0 &&
			       attribute->end_index > G_MAXINT / 2);
	// Full-width scale is zooming -- keep that

	if (attribute->klass->type == PANGO_ATTR_RISE ||
	    (attribute->klass->type == PANGO_ATTR_SCALE && !full_width)) {
		*recalc_height = TRUE;
		return TRUE;
	}
	return (attribute->klass->type == PANGO_ATTR_SHAPE);
}

static void
cell_draw_simplify_attributes (GnmRenderedValue *rv)
{
	PangoAttrList *pal = pango_attr_list_copy (pango_layout_get_attributes (rv->layout));
	gboolean recalc_height = FALSE;
	PangoAttrList *excess = pango_attr_list_filter
		(pal, (PangoAttrFilterFunc) cell_draw_simplify_cb, &recalc_height);
	pango_attr_list_unref (excess);
	pango_layout_set_attributes (rv->layout, pal);
	pango_attr_list_unref (pal);

	if (recalc_height)
		pango_layout_get_size (rv->layout, NULL,
				       &rv->layout_natural_height);
}

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
 * @h_center: The number of pango units from x1 marking the logical center
 *             of the cell.  NOTE This can be asymmetric.  Passing
 *             <= 0 will use width / 2
 */
gboolean
cell_calc_layout (G_GNUC_UNUSED GnmCell const *cell, GnmRenderedValue *rv, int y_direction,
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
		cell_draw_simplify_attributes (rv);
		rv->numeric_overflow = TRUE;
		rv->variable_width = TRUE;
		rv->hfilled = TRUE;
	}

	/* Special handling of error dates.  */
	if (!was_drawn && rv->numeric_overflow) {
		pango_layout_set_text (layout, hashes, -1);
		cell_draw_simplify_attributes (rv);
		rv->variable_width = TRUE;
		rv->hfilled = TRUE;
	}

	if (rv->rotation && !rv->noborders) {
		GnmRenderedRotatedValue const *rrv = (GnmRenderedRotatedValue *)rv;
		if (rv->wrap_text) {
			/* quick fix for #394, may be not perfect */
			double rot = rv->rotation / 180. * M_PI, actual_width;
			actual_width = MAX (0, width - indent) * cos (rot) + height * fabs (sin (rot));
			if (actual_width > pango_layout_get_width (layout)) {
				pango_layout_set_wrap (layout, PANGO_WRAP_WORD_CHAR);
				pango_layout_set_width (layout, actual_width);
				gnm_rendered_value_remeasure (rv);
			}
		}
		if (rrv->sin_a_neg) {
			hoffset += (width - indent) - rv->layout_natural_width;
		}
	} else if (!rv->rotation && rv->wrap_text
		   && (rv->effective_halign != GNM_HALIGN_FILL)) {
		int wanted_width = MAX (0, width - indent);
		if (wanted_width != pango_layout_get_width (layout)) {
			pango_layout_set_wrap (layout, PANGO_WRAP_WORD_CHAR);
			pango_layout_set_width (layout, wanted_width);
			gnm_rendered_value_remeasure (rv);
		}
	} else {
		switch (rv->effective_halign) {
		case GNM_HALIGN_RIGHT:
			hoffset += (width - indent) - rv->layout_natural_width;
			break;
		case GNM_HALIGN_DISTRIBUTED:
		case GNM_HALIGN_CENTER:
			if (h_center == -1)
				h_center = width / 2;
			hoffset += h_center + (-indent - rv->layout_natural_width) / 2;
			break;
		case GNM_HALIGN_CENTER_ACROSS_SELECTION:
			hoffset += ((width - indent) - rv->layout_natural_width) / 2;
			break;
		case GNM_HALIGN_FILL: {
			PangoDirection dir = PANGO_DIRECTION_LTR;
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
				PangoAttrList *attr = pango_layout_get_attributes (layout);

				dir = pango_find_base_dir (copy1, -1);
				for (i = 0; i < copies; i++) {
					if (i)
						g_string_append_unichar (multi, UNICODE_ZERO_WIDTH_SPACE_C);
					g_string_append_len (multi, copy1, len1);
				}
				pango_layout_set_text (layout, multi->str, multi->len);
				g_string_free (multi, TRUE);

				if (attr != NULL && !go_pango_attr_list_is_empty (attr)) {
					PangoAttrList *attr_c = pango_attr_list_copy (attr);
					size_t len = len1 + UNICODE_ZERO_WIDTH_SPACE_C_UTF8_LENGTH;
					for (i = 1; i < copies;
					     i++, len += len1 + UNICODE_ZERO_WIDTH_SPACE_C_UTF8_LENGTH)
						pango_attr_list_splice (attr, attr_c, len, len1);
					pango_attr_list_unref (attr_c);
				}
			} else
				dir = pango_find_base_dir (pango_layout_get_text (layout), -1);
			/* right align if text is RTL */
			if (dir == PANGO_DIRECTION_RTL) {
				PangoRectangle r;
				pango_layout_get_extents (layout, NULL, &r);
				hoffset += (width - indent) - r.width;
			}

			rv->hfilled = TRUE;
			break;
		}

#ifndef DEBUG_SWITCH_ENUM
		default:
#endif
		case GNM_HALIGN_GENERAL:
			g_warning ("Unhandled horizontal alignment.");
		case GNM_HALIGN_LEFT:
			break;
		}
	}

	/* Note that Excel always truncates the cell content only at the */
	/* bottom even if the request is to align it at the bottom or to */
	/* center it. We do the same for compatibilities sake. Also see  */
	/* bug #662368 */
	switch (rv->effective_valign) {
#ifndef DEBUG_SWITCH_ENUM
	default:
		g_warning ("Unhandled vertical alignment.");
		/* Fall through.  */
#endif
	case GNM_VALIGN_TOP:
		text_base = rect_y;
		break;

	case GNM_VALIGN_BOTTOM: {
		int dh = height - rv->layout_natural_height;
		if (rv->rotation == 0 && dh < 0)
			dh = 0;
		text_base = rect_y + y_direction * dh;
		break;
	}

	case GNM_VALIGN_DISTRIBUTED: /* dunno what this does yet */
	case GNM_VALIGN_CENTER: {
		int dh = (height - rv->layout_natural_height) / 2;
		if (rv->rotation == 0 && dh < 0)
                        dh = 0;
		text_base = rect_y + y_direction * dh;
		break;
	}

	case GNM_VALIGN_JUSTIFY:
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

	*res_color = gnm_rendered_value_get_color (rv);
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
	GnmRenderedValue *cell_rv;

	cell_rv = gnm_cell_get_rendered_value (cell);

	if (!rv)
		rv = cell_rv;

	if (rv->drawn)
		return;

	if (rv->variable_width && rv == cell_rv &&
	    !go_format_is_general (gnm_cell_get_format (cell))) {
		/*
		 * We get here when entering a new value in a cell
		 * with a format that has a filler, for example
		 * one of the standard accounting formats.  We need
		 * to rerender such that the filler gets a chance
		 * to expand.
		 */
		rv = gnm_cell_render_value (cell, TRUE);
	}

	might_overflow = rv->might_overflow;
	if (inhibit_overflow) rv->might_overflow = FALSE;
	cell_calc_layout (cell, rv, -1, col_width * PANGO_SCALE,
		dummy_height, dummy_h_center, &dummy_fore_color,
		&dummy_x, &dummy_y);
	rv->might_overflow = might_overflow;
}


static void
cell_draw_extension_mark_bottom (cairo_t *cr, GnmCellDrawStyle const *style,
				 int x1, int y1, int height, int h_center)
{
	double s = style->extension_marker_size;
	gdk_cairo_set_source_rgba (cr, &style->extension_marker_color);
	cairo_new_path (cr);
	cairo_move_to (cr, x1 + h_center, y1 + height);
	cairo_rel_line_to (cr, s / -2, s / -2);
	cairo_rel_line_to (cr, s, 0);
	cairo_close_path (cr);
	cairo_fill (cr);
}

static void
cell_draw_extension_mark_left (cairo_t *cr, GnmCellDrawStyle const *style,
			       int x1, int y1, int height)
{
	double s = style->extension_marker_size;
	gdk_cairo_set_source_rgba (cr, &style->extension_marker_color);
	cairo_new_path (cr);
	cairo_move_to (cr, x1, y1 + height/2);
	cairo_rel_line_to (cr, s / 2, s / -2);
	cairo_rel_line_to (cr, 0, s);
	cairo_close_path (cr);
	cairo_fill (cr);
}

static void
cell_draw_extension_mark_right (cairo_t *cr, GnmCellDrawStyle const *style,
				int x1, int y1, int width, int height)
{
	double s = style->extension_marker_size;
	gdk_cairo_set_source_rgba (cr, &style->extension_marker_color);
	cairo_new_path (cr);
	cairo_move_to (cr, x1 + width, y1 + height/2);
	cairo_rel_line_to (cr, s / -2, s / -2);
	cairo_rel_line_to (cr, 0, s);
	cairo_close_path (cr);
	cairo_fill (cr);

}


static void
cell_draw_h_extension_markers (cairo_t *cr,
			       GnmCellDrawStyle const *style,
			       GnmRenderedValue *rv,
			       int x1, int y1,
			       int width, int height)
{
	switch (rv->effective_halign) {
	case GNM_HALIGN_GENERAL:
	case GNM_HALIGN_LEFT:
		cell_draw_extension_mark_right (cr, style, x1, y1, width, height);
		break;
	case GNM_HALIGN_RIGHT:
		cell_draw_extension_mark_left (cr, style, x1, y1, height);
		break;
	case GNM_HALIGN_DISTRIBUTED:
	case GNM_HALIGN_CENTER:
	case GNM_HALIGN_CENTER_ACROSS_SELECTION:
		cell_draw_extension_mark_right (cr, style, x1, y1, width, height);
		cell_draw_extension_mark_left (cr, style, x1, y1, height);
		break;
	case GNM_HALIGN_FILL:
	default:
		break;
	}
}

static void
cell_draw_v_extension_markers (cairo_t *cr,
			       GnmCellDrawStyle const *style,
			       int x1, int y1,
			       int width, int height,
			       int h_center)
{
	if (h_center == -1)
		h_center = width / 2;
	cell_draw_extension_mark_bottom (cr, style, x1, y1, height, h_center);
}

/**
 * cell_draw:
 * @cell: #GnmCell const
 * @cr: #cairo_t
 * @x:
 * @y:
 * @width: including margins and leading grid line
 * @height: including margins and leading grid line
 * @h_center:
 * @style: (nullable):
 **/
void
cell_draw (GnmCell const *cell, cairo_t *cr,
	   int x1, int y1, int width, int height, int h_center,
	   gboolean show_extension_markers,
	   GnmCellDrawStyle const *style)
{
	GOColor fore_color;
	gint x;
	gint y;
	GnmRenderedValue *rv;

	g_return_if_fail (!show_extension_markers || style != NULL);

	/* Get the sizes exclusive of margins and grids */
	/* Note: +1 because size_pixels includes leading gridline.  */
	height -= GNM_ROW_MARGIN + GNM_ROW_MARGIN + 1;
	width  -= GNM_COL_MARGIN + GNM_COL_MARGIN + 1;

	if (h_center > GNM_COL_MARGIN)
		h_center = h_center - GNM_COL_MARGIN - 1 + (h_center % 2);

	rv = gnm_cell_fetch_rendered_value (cell, TRUE);

	if (cell_calc_layout (cell, rv, +1,
			      width * PANGO_SCALE,
			      height * PANGO_SCALE,
			      h_center == -1 ? -1 : (h_center * PANGO_SCALE),
			      &fore_color, &x, &y)) {

		cairo_save (cr);

		/*
		 * HACK -- do not clip rotated cells.  This gives an
		 * approximation to the right effect.  (The right way
		 * would be to create a proper cellspan type.)
		 */
		if (!rv->rotation) {
			cairo_new_path (cr);
			/* +1 to get past left grid-line.  */
			cairo_rectangle (cr, x1 + 1 + GNM_COL_MARGIN,
					 y1 + 1 + GNM_ROW_MARGIN,
					 width, height);

			cairo_clip (cr);
		}

		/* See http://bugzilla.gnome.org/show_bug.cgi?id=105322 */
		cairo_set_source_rgba (cr, GO_COLOR_TO_CAIRO (fore_color));

		if (rv->rotation) {
			GnmRenderedRotatedValue *rrv = (GnmRenderedRotatedValue *)rv;
			struct GnmRenderedRotatedValueInfo const *li = rrv->lines;
			GSList *lines;

			for (lines = pango_layout_get_lines (rv->layout);
			     lines;
			     lines = lines->next, li++) {
				cairo_save (cr);
				cairo_move_to (cr, x1 + PANGO_PIXELS (x + li->dx), y1 + PANGO_PIXELS (y + li->dy));
				cairo_rotate (cr, rv->rotation * (-M_PI / 180));
				pango_cairo_show_layout_line (cr, lines->data);
				cairo_restore (cr);
			}
		} else {
			cairo_save (cr);
			cairo_translate (cr, x1 + PANGO_PIXELS (x), y1 + PANGO_PIXELS (y));
			pango_cairo_show_layout (cr, rv->layout);
			cairo_restore (cr);

			if (show_extension_markers &&
			    width < PANGO_PIXELS (rv->layout_natural_width)) {
				cairo_save (cr);
				cell_draw_h_extension_markers
					(cr, style, rv,
					 x1 + 1 + GNM_COL_MARGIN,
					 y1 + 1 + GNM_ROW_MARGIN,
					 width, height);
				cairo_restore (cr);
			}

			if (show_extension_markers &&
			    height < PANGO_PIXELS (rv->layout_natural_height)) {
				cairo_save (cr);
				cell_draw_v_extension_markers
					(cr, style,
					 x1 + 1 + GNM_COL_MARGIN,
					 y1 + 1 + GNM_ROW_MARGIN,
					 width, height, h_center);
				cairo_restore (cr);
			}
		}
		cairo_restore (cr);
	}
}
