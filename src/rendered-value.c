/* vim: set sw=8: */

/*
 * rendered-value.c: Management & utility routines for formated
 *     colored text.
 *
 * Copyright (C) 2000, 2001 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "rendered-value.h"

#include "expr.h"
#include "gutils.h"
#include "cell.h"
#include "style.h"
#include "style-color.h"
#include "number-match.h"
#include "sheet.h"
#include "sheet-merge.h"
#include "sheet-style.h"
#include "format.h"
#include "value.h"
#include "parse-util.h"
#include "sheet-control-gui.h"
#include "str.h"
#include "workbook.h"

#include <math.h>

#ifndef USE_RV_POOLS
#define USE_RV_POOLS 1
#endif

#if USE_RV_POOLS
/* Memory pool for strings.  */
static gnm_mem_chunk *rendered_value_pool;
#define CHUNK_ALLOC(T,p) ((T*)gnm_mem_chunk_alloc (p))
#define CHUNK_FREE(p,v) gnm_mem_chunk_free ((p), (v))
#else
#define CHUNK_ALLOC(T,c) g_new (T,1)
#define CHUNK_FREE(p,v) g_free ((v))
#endif

static guint16
calc_indent (const MStyle *mstyle, Sheet *sheet)
{
	int indent = 0;
	if (mstyle_is_element_set (mstyle, MSTYLE_INDENT)) {
		indent = mstyle_get_indent (mstyle);
		if (indent) {
			StyleFont *style_font =
				scg_get_style_font (sheet, mstyle);
			indent *= style_font->approx_width.pixels.digit;
			style_font_unref (style_font);
		}
	}
	return MIN (indent, 65535);
}


/**
 * rendered_value_new:
 * @cell:   The cell
 * @mstyle: The mstyle associated with the cell
 * @dynamic_width : Allow format to depend on column width.
 * @context: A pango context for layout.
 *
 * Formats the value of the cell according to the format style given in @mstyle
 *
 * Return value: a new RenderedValue
 **/
RenderedValue *
rendered_value_new (Cell *cell, MStyle const *mstyle,
		    gboolean dynamic_width,
		    PangoContext *context)
{
	RenderedValue	*res;
	Sheet		*sheet;
	StyleColor	*color;
	char            *str;
	PangoLayout     *layout;
	PangoAttrList   *attrs;
	gboolean        display_formula;

	g_return_val_if_fail (cell != NULL, NULL);
	g_return_val_if_fail (cell->value != NULL, NULL);
	g_return_val_if_fail (context != NULL, NULL);

	sheet = cell->base.sheet;
	display_formula = cell_has_expr (cell) && sheet && sheet->display_formulas;

	if (display_formula) {
		str = cell_get_entered_text (cell);
		color = NULL;
		dynamic_width = FALSE;
	} else if (sheet && sheet->hide_zero && cell_is_zero (cell)) {
		str = g_strdup ("");
		color = NULL;
		dynamic_width = FALSE;
	} else if (mstyle_is_element_set (mstyle, MSTYLE_FORMAT)) {
		double col_width = -1.;
		/* entered text CAN be null if called by set_value */
		StyleFormat *format = mstyle_get_format (mstyle);

		/* For format general approximate the cell width in characters */
		if (style_format_is_general (format)) {
			if (dynamic_width &&
			    (VALUE_FMT (cell->value) == NULL ||
			     style_format_is_general (VALUE_FMT (cell->value)))) {
				StyleFont *style_font =
					scg_get_style_font (sheet, mstyle);
				double wdigit = style_font->approx_width.pts.digit;

				if (wdigit > 0.0) {
					double cell_width;
					if (cell_is_merged (cell)) {
						Range const *merged =
							sheet_merge_is_corner (sheet, &cell->pos);

						cell_width = sheet_col_get_distance_pts (sheet,
							merged->start.col, merged->end.col + 1);
					} else
						cell_width = cell->col_info->size_pts;
					cell_width -= cell->col_info->margin_a + cell->col_info->margin_b;

					/*
					 * FIXME: we should really pass these to the format function,
					 * so we can measure actual characters, not just what might be
					 * there.
					 */
					cell_width -= MAX (0.0, style_font->approx_width.pts.e - wdigit);
					cell_width -= MAX (0.0, style_font->approx_width.pts.decimal - wdigit);
					cell_width -= 2 * MAX (0.0, style_font->approx_width.pts.sign - wdigit);
					col_width = cell_width / wdigit;
				}
				style_font_unref (style_font);
			} else {
				format = VALUE_FMT (cell->value);
				dynamic_width = FALSE;
			}
		} else
			dynamic_width = FALSE;
		str = format_value (format, cell->value, &color, col_width,
				    sheet ? workbook_date_conv (sheet->workbook) : NULL);
	} else {
		g_warning ("No format: serious error");
		str = g_strdup ("Error");
	}

	g_return_val_if_fail (str != NULL, NULL);

	res = CHUNK_ALLOC (RenderedValue, rendered_value_pool);
	res->rendered_text = string_get_nocopy (str);
	res->dynamic_width = dynamic_width;
	res->indent_left = res->indent_right = 0;
	res->numeric_overflow = FALSE;
	res->hfilled = FALSE;
	res->vfilled = FALSE;
	res->wrap_text = mstyle_get_effective_wrap_text (mstyle);
	res->effective_halign = style_default_halign (mstyle, cell);
	res->effective_valign = mstyle_get_align_v (mstyle);
	res->display_formula = display_formula;

	res->layout = layout = pango_layout_new (context);
	str = res->rendered_text->str;
	pango_layout_set_text (layout, str, strlen (str));

	attrs = mstyle_get_pango_attrs (mstyle, color);
	if (color)
		style_color_unref (color);
	pango_layout_set_attributes (res->layout, attrs);
	pango_attr_list_unref (attrs);

	switch (res->effective_halign) {
	case HALIGN_LEFT:
		res->indent_left = calc_indent (mstyle, sheet);
		pango_layout_set_alignment (layout, PANGO_ALIGN_LEFT);
		break;

	case HALIGN_JUSTIFY:
		/*
		 * The code here should work, but pango doesn't:
		 * http://bugzilla.gnome.org/show_bug.cgi?id=64538
		 */
		pango_layout_set_justify (layout, TRUE);
		pango_layout_set_alignment (layout, PANGO_ALIGN_LEFT);
		break;

	case HALIGN_FILL:
		/*
		 * A bit weird, but seems to match XL.  The effect is to
		 * render newlines as visible characters.
		 */
		pango_layout_set_single_paragraph_mode (layout, TRUE);
		pango_layout_set_alignment (layout, PANGO_ALIGN_LEFT);
		break;

	case HALIGN_RIGHT:
		res->indent_right = calc_indent (mstyle, sheet);
		pango_layout_set_alignment (layout, PANGO_ALIGN_RIGHT);
		break;

	case HALIGN_CENTER:
	case HALIGN_CENTER_ACROSS_SELECTION:
		pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
		break;

	default:
		g_warning ("Line justification style not supported.");
	}

	pango_layout_get_pixel_size (layout,
				     &res->layout_natural_width,
				     &res->layout_natural_height);

	return res;
}

void
rendered_value_destroy (RenderedValue *rv)
{
	if (rv->rendered_text) {
		string_unref (rv->rendered_text);
		rv->rendered_text = NULL;
	}

	if (rv->layout) {
		g_object_unref (G_OBJECT (rv->layout));
		rv->layout = NULL;
	}

	CHUNK_FREE (rendered_value_pool, rv);
}

/* Return the value as a single string without format infomation.
 */
const char *
rendered_value_get_text (RenderedValue const *rv)
{
	g_return_val_if_fail (rv != NULL && rv->rendered_text != NULL, g_strdup ("ERROR"));
	return pango_layout_get_text (rv->layout);
}


/**
 * cell_get_render_color:
 * @cell: the cell from which we want to pull the color from
 *
 * The returned value is a pointer to a PangoColor describing
 * the foreground colour.
 */
const PangoColor *
cell_get_render_color (Cell const *cell)
{
	PangoAttrList *attrs;
	PangoAttrIterator *it;
	PangoAttribute *attr;

	g_return_val_if_fail (cell != NULL, NULL);

	/* A precursor to just in time rendering Ick! */
	if (cell->rendered_value == NULL)
		cell_render_value ((Cell *)cell, TRUE);

	attrs = pango_layout_get_attributes (cell->rendered_value->layout);
	g_return_val_if_fail (attrs != NULL, NULL);

	it = pango_attr_list_get_iterator (attrs);
	attr = pango_attr_iterator_get (it, PANGO_ATTR_FOREGROUND);
	pango_attr_iterator_destroy (it);
	if (!attr)
		return NULL;

	return &((PangoAttrColor *)attr)->color;
}

/**
 * cell_get_entered_text:
 * @cell: the cell from which we want to pull the content from
 *
 * This returns a g_malloc()ed region of memory with a text representation
 * of the cell contents.
 *
 * This will return a text expression if the cell contains a formula, or
 * a string representation of the value.
 */
char *
cell_get_entered_text (Cell const *cell)
{
	g_return_val_if_fail (cell != NULL, NULL);

	if (cell_has_expr (cell)) {
		ParsePos pp;
		GString *res = g_string_new ("=");

		gnm_expr_as_gstring (res, cell->base.expression,
				     parse_pos_init_cell (&pp, cell),
				     gnm_expr_conventions_default);
		return g_string_free (res, FALSE);
	}

	if (cell->value != NULL) {
		if (cell->value->type == VALUE_STRING) {
			/* Try to be reasonably smart about adding a leading quote */
			char const *tmp = cell->value->v_str.val->str;
			if (NULL == gnm_expr_char_start_p (tmp)) {
				Value *val = format_match_number (tmp,
					cell_get_format	(cell),
					workbook_date_conv (cell->base.sheet->workbook));
				if (val == NULL)
					return g_strdup (tmp);
				value_release (val);
			}
			return g_strconcat ("\'", tmp, NULL);
		}
		return format_value (NULL, cell->value, NULL, -1,
			workbook_date_conv (cell->base.sheet->workbook));
	}

	g_warning ("A cell with no expression, and no value ??");
	return g_strdup ("<ERROR>");
}

int
cell_rendered_height (Cell const *cell)
{
	if (!cell || !cell->rendered_value)
		return 0;
	else
		return cell->rendered_value->layout_natural_height;
}

int
cell_rendered_width (Cell const *cell)
{
	if (!cell || !cell->rendered_value)
		return 0;
	else
		return cell->rendered_value->layout_natural_width;
}

int
cell_rendered_offset (Cell const * cell)
{
	if (!cell || !cell->rendered_value)
		return 0;
	else
		return cell->rendered_value->indent_left +
			cell->rendered_value->indent_right;
}

void
rendered_value_init (void)
{
#if USE_RV_POOLS
	rendered_value_pool =
		gnm_mem_chunk_new ("rendered value pool",
				   sizeof (RenderedValue),
				   16 * 1024 - 128);
#endif
}

#if USE_RV_POOLS
static void
cb_rendered_value_pool_leak (gpointer data, gpointer user)
{
	RenderedValue *rendered_value = data;
	fprintf (stderr, "Leaking rendered value at %p [%s].\n",
		 rendered_value, rendered_value->rendered_text->str);
}
#endif

void
rendered_value_shutdown (void)
{
#if USE_RV_POOLS
	gnm_mem_chunk_foreach_leak (rendered_value_pool, cb_rendered_value_pool_leak, NULL);
	gnm_mem_chunk_destroy (rendered_value_pool, FALSE);
	rendered_value_pool = NULL;
#endif
}
