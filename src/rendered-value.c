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
#include "application.h"
#include "str.h"

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

/**
 * rendered_value_new:
 * @cell:   The cell
 * @mstyle: The mstyle associated with the cell
 * @dynamic_width : Allow format to depend on column width.
 *
 * Formats the value of the cell according to the format style given in @mstyle
 *
 * Return value: a new RenderedValue
 **/
RenderedValue *
rendered_value_new (Cell *cell, MStyle const *mstyle, gboolean dynamic_width)
{
	RenderedValue	*res;
	Sheet		*sheet;
	StyleColor	*color;
	double		 col_width = -1.;
	char *str;

	g_return_val_if_fail (cell != NULL, NULL);
	g_return_val_if_fail (cell->value != NULL, NULL);

	sheet = cell->base.sheet;

	if (cell_has_expr (cell) && sheet != NULL && sheet->display_formulas) {
		ParsePos pp;
		char *tmpstr = gnm_expr_as_string (cell->base.expression,
						    parse_pos_init_cell (&pp, cell));
		str = g_strconcat ("=", tmpstr, NULL);
		g_free (tmpstr);
		color = NULL;
		dynamic_width = FALSE;
	} else if (mstyle_is_element_set (mstyle, MSTYLE_FORMAT)) {
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
							sheet_merge_is_corner (cell->base.sheet, &cell->pos);

						cell_width = sheet_col_get_distance_pts (cell->base.sheet,
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
		str = format_value (format, cell->value, &color, col_width);
	} else {
		g_warning ("No format: serious error");
		str = g_strdup ("Error");
	}

	g_return_val_if_fail (str != NULL, NULL);

	res = CHUNK_ALLOC (RenderedValue, rendered_value_pool);
	res->rendered_text = string_get (str);
	res->render_color = color;
	res->width_pixel = res->height_pixel = res->offset_pixel = 0;
	res->dynamic_width = dynamic_width;
	g_free (str);

	return res;
}

void
rendered_value_destroy (RenderedValue *rv)
{
	if (rv->rendered_text) {
		string_unref (rv->rendered_text);
		rv->rendered_text = NULL;
	}

	if (rv->render_color) {
		style_color_unref (rv->render_color);
		rv->render_color = NULL;
	}

	CHUNK_FREE (rendered_value_pool, rv);
}

/**
 * rendered_value_calc_size:
 * @cell:
 *
 * Calls upon rendered_value_calc_size_ext
 **/
void
rendered_value_calc_size (Cell const *cell)
{
	MStyle *mstyle = cell_get_mstyle (cell);
	rendered_value_calc_size_ext (cell, mstyle);
}

/*
 * rendered_value_calc_size_ext
 * @cell:      The cell we are working on.
 * @style:     the style formatting constraints (font, alignments)
 * @text:      the string contents.
 *
 * Computes the width and height used by the cell based on alignments
 * constraints in the style using the font specified on the style.
 *
 * NOTE :
 * The line splitting code is VERY similar to cell-draw.c:cell_split_text
 * please keep it that way.
 */
void
rendered_value_calc_size_ext (Cell const *cell, MStyle *mstyle)
{
	Sheet *sheet = cell->base.sheet;
	RenderedValue *rv = cell->rendered_value;
	StyleFont *style_font = scg_get_style_font (sheet, mstyle);
	int font_height = style_font_get_height (style_font);
	int const cell_w = COL_INTERNAL_WIDTH (cell->col_info);
	StyleHAlignFlags const halign = mstyle_get_align_h (mstyle);
	int text_width;
	char *text;

	g_return_if_fail (mstyle != NULL);
	g_return_if_fail (rv != NULL);

	text       = rv->rendered_text->str;
	text_width = style_font_string_width (style_font,text);

	/* embedded newlines will cause the width to be narrow,
	 * but still require multiple lines */
	if ((!mstyle_get_wrap_text (mstyle) && text_width < cell_w) ||
	    (cell_is_number (cell) &&
	     sheet != NULL && !sheet->display_formulas)) {
		rv->width_pixel  = text_width;
		rv->height_pixel = font_height;
	} else if (halign == HALIGN_JUSTIFY ||
		   mstyle_get_align_v (mstyle) == VALIGN_JUSTIFY ||
		   mstyle_get_wrap_text (mstyle)) {
		char const *p, *next, *line_begin;
		char const *first_whitespace = NULL;
		char const *last_whitespace = NULL;
		gboolean prev_was_space = FALSE;
		int used = 0, used_last_space = 0;
		int h = 0;
		int len_current;

		rv->width_pixel  = cell_w;

		for (line_begin = p = text; *p; p = next) {
			next = g_utf8_next_char (p);
			len_current = style_font_text_width (style_font, p, next - p);

			/* Wrap if there is an embeded newline, or we have overflowed */
			if (*p == '\n' || used + len_current > cell_w){
				char const *begin = line_begin;
				int len;

				if (*p == '\n'){
					/* start after newline, preserve whitespace */
					line_begin = p+1;
					len = p - begin;
					used = 0;
				} else if (last_whitespace != NULL){
					/* Split at the run of whitespace */
					line_begin = last_whitespace + 1;
					len = first_whitespace - begin;
					used = len_current + used - used_last_space;
				} else {
					/* Split before the current character */
					line_begin = p; /* next line starts here */
					len = p - begin;
					used = len_current;
				}

				h += font_height;
				first_whitespace = last_whitespace = NULL;
				prev_was_space = FALSE;
				continue;
			}

			used += len_current;
			if (*p == '-') {
				used_last_space = used;
				last_whitespace = p;
				first_whitespace = p+1;
				prev_was_space = TRUE;
			} else if (g_unichar_isspace (g_utf8_get_char (p))) {
				used_last_space = used;
				last_whitespace = p;
				if (!prev_was_space)
					first_whitespace = p;
				prev_was_space = TRUE;
			} else
				prev_was_space = FALSE;
		}

		/* Catch the final bit that did not wrap */
		if (*line_begin)
			h += font_height;

		rv->height_pixel = h;
	} else {
		rv->width_pixel  = text_width;
		rv->height_pixel = font_height;
	}

	/* 2*width seems to be pretty close to XL's notion */
	/* FIXME: uses digit -- what should it use?  */
	if (halign == HALIGN_LEFT || halign == HALIGN_RIGHT)
		rv->offset_pixel = rint (mstyle_get_indent (mstyle) *
			2 * style_font->approx_width.pixels.digit);
	style_font_unref (style_font);
}

/* Return the value as a single string without format infomation.
 * Caller is responsible for freeing the result
 */
char *
rendered_value_get_text (RenderedValue const *rv)
{
	g_return_val_if_fail (rv != NULL && rv->rendered_text != NULL, g_strdup ("ERROR"));
	return g_strdup (rv->rendered_text->str);
}

/**
 * cell_get_rendered_text:
 * @cell: the cell from which we want to pull the content from
 *
 * This returns a g_malloc()ed region of memory with a text representation
 * of the cell contents.
 *
 * This will return a text expression if the cell contains a formula, or
 * a string representation of the value.
 */
char *
cell_get_rendered_text (Cell const *cell)
{
	g_return_val_if_fail (cell != NULL, g_strdup("ERROR"));

	/* A precursor to just in time rendering Ick! */
	if (cell->rendered_value == NULL)
		cell_render_value ((Cell *)cell, TRUE);

	return rendered_value_get_text (cell->rendered_value);
}

/**
 * cell_get_render_color:
 * @cell: the cell from which we want to pull the color from
 *
 * the returned value is a pointer to the StyleColor.
 * no reference has been added!
 */
StyleColor  *
cell_get_render_color (Cell const *cell)
{
	g_return_val_if_fail (cell != NULL, NULL);

	/* A precursor to just in time rendering Ick! */
	if (cell->rendered_value == NULL)
		cell_render_value ((Cell *)cell, TRUE);

	return cell->rendered_value->render_color;
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
		char *func, *ret;
		ParsePos pp;

		func = gnm_expr_as_string (cell->base.expression,
			parse_pos_init_cell (&pp, cell));
		ret = g_strconcat ("=", func, NULL);
		g_free (func);

		return ret;
	}

	if (cell->value != NULL) {
		if (cell->value->type == VALUE_STRING) {
			/* Try to be reasonably smart about adding a leading quote */
			char const *tmp = cell->value->v_str.val->str;
			if (NULL == gnm_expr_char_start_p (tmp)) {
				Value *val = format_match_number (tmp,
					cell_get_format	(cell));
				if (val == NULL)
					return g_strdup (tmp);
				value_release (val);
			}
			return g_strconcat ("\'", tmp, NULL);
		}
		return format_value (NULL, cell->value, NULL, -1);
	}

	g_warning ("A cell with no expression, and no value ??");
	return g_strdup ("<ERROR>");
}

int
cell_rendered_height (Cell const *cell)
{
	if (cell == NULL)
		return 0;

	/* If we are not rendered and are an expressions just use the font height */
	if (cell->rendered_value == NULL) {
		if (cell_has_expr (cell)) {
			MStyle const *style = sheet_style_get (cell->base.sheet,
				cell->pos.col, cell->pos.row);
			StyleFont *style_font = scg_get_style_font (cell->base.sheet, style);
			int res = style_font_get_height (style_font);
			style_font_unref (style_font);
			return res;
		}
		return 0;
	}
	return cell->rendered_value->height_pixel;
}
int
cell_rendered_width (Cell const *cell)
{
	if (!cell || !cell->rendered_value)
		return 0;
	else
		return cell->rendered_value->width_pixel;
}
int
cell_rendered_offset (Cell const * cell)
{
	if (!cell || !cell->rendered_value)
		return 0;
	else
		return cell->rendered_value->offset_pixel;
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
