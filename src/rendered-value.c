/* vim: set sw=8: */

/*
 * rendered-value.c: Management & utility routines for formated
 *     colored text.
 *
 * Copyright (C) 2000 Jody Goldberg (jgoldberg@home.com)
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
#include "config.h"
#include "rendered-value.h"
#include "expr.h"
#include "cell.h"
#include "style.h"
#include "sheet.h"
#include "format.h"
#include "value.h"
#include "parse-util.h"
#include "sheet-view.h"

/*
 * rendered_value_new
 * @cell: The cell whose value needs to be rendered
 *
 * Returns a RenderedValue displaying the value of the cell formated according
 * to the format style. If formulas are being displayed the text of the a
 * formula instead of its value.
 */
RenderedValue *
rendered_value_new (Cell *cell, GList *styles)
{
	RenderedValue	*res;
	StyleColor	*color;
	char *str;

	g_return_val_if_fail (cell != NULL, NULL);
	g_return_val_if_fail (cell->value != NULL, NULL);

	if (cell_has_expr (cell) &&
	    cell->base.sheet != NULL && cell->base.sheet->display_formulas) {
		ParsePos pp;
		char *tmpstr = expr_tree_as_string (cell->u.expression,
			parse_pos_init_cell (&pp, cell));
		str = g_strconcat ("=", tmpstr, NULL);
		g_free (tmpstr);
		color = NULL;
	} else {
		MStyle *mstyle = (styles != NULL)
			? sheet_style_compute_from_list (styles, cell->pos.col, cell->pos.row)
			: cell_get_mstyle (cell);

		if (mstyle_is_element_set (mstyle, MSTYLE_FORMAT)) {
			/* entered text CAN be null if called by
			 * set_value
			 */
			char const *entered =
				(!cell_has_expr (cell) && cell->u.entered_text != NULL)
				? cell->u.entered_text->str : NULL;
			StyleFormat *format = mstyle_get_format (mstyle);

			if  (style_format_is_general(format) && cell->format)
				format = cell->format;
			str = format_value (format, cell->value, &color, entered);
		} else {
			g_warning ("No format: serious error");
			str = g_strdup ("Error");
		}
		mstyle_unref (mstyle);
	}

	g_return_val_if_fail (str != NULL, NULL);

	res = g_new (RenderedValue, 1);
	res->rendered_text = string_get (str);
	res->render_color = color;
	res->width_pixel = res->height_pixel = 0;
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

	g_free (rv);
}

/*
 * rendered_value_calc_size
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
rendered_value_calc_size (Cell const *cell)
{
	RenderedValue *rv = cell->rendered_value;
	MStyle *mstyle = cell_get_mstyle (cell);
	StyleFont * const style_font =
	    sheet_view_get_style_font (cell->base.sheet, mstyle);
	GdkFont * const gdk_font = style_font_gdk_font (style_font);
	int const font_height    = style_font_get_height (style_font);
	int const cell_w = COL_INTERNAL_WIDTH (cell->col_info);
	int text_width;
	char *text;

	g_return_if_fail (rv != NULL);
	
	text       = rv->rendered_text->str;
	text_width = gdk_string_measure (gdk_font, text);
	
	if (text_width < cell_w || cell_is_number (cell)) {
		rv->width_pixel  = text_width;
		rv->height_pixel = font_height;
	} else if (mstyle_get_align_h (mstyle) == HALIGN_JUSTIFY ||
		   mstyle_get_align_v (mstyle) == VALIGN_JUSTIFY ||
		   mstyle_get_fit_in_cell (mstyle)) {
		char const *p, *line_begin;
		char const *first_whitespace = NULL;
		char const *last_whitespace = NULL;
		gboolean prev_was_space = FALSE;
		int used = 0, used_last_space = 0;
		int h = 0;

		rv->width_pixel  = cell_w;

		for (line_begin = p = text; *p; p++){
			int const len_current = gdk_text_width (gdk_font, p, 1);

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
			if (*p == ' '){
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
	style_font_unref (style_font);
	mstyle_unref (mstyle);
}

/* Return the value as a single string without format infomation.
 * Caller is responsible for freeing the result
 */
char *
rendered_value_get_text (RenderedValue const *rv)
{
	g_return_val_if_fail (rv->rendered_text != NULL,
			      g_strdup ("ERROR"));
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
	g_return_val_if_fail (cell != NULL,
			      g_strdup("ERROR"));
	return rendered_value_get_text (cell->rendered_value);
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

		func = expr_tree_as_string (cell->u.expression,
			parse_pos_init_cell (&pp, cell));
		ret = g_strconcat ("=", func, NULL);
		g_free (func);

		return ret;
	}

	/*
	 * Return the value without parsing.
	 */
	if (cell->u.entered_text != NULL)
		return g_strdup (cell->u.entered_text->str);

	/* Getting desperate, no need to check for the rendered version.
	 * This should not happen.
	 */
	else if (cell->value != NULL)
		return value_get_as_string (cell->value);
	else {
		g_warning ("A cell with no expression, no value, and no entered_text ??");
		return g_strdup ("<ERROR>");
	}
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
cell_rendered_height (Cell const *cell)
{
	if (!cell || !cell->rendered_value)
		return 0;
	else
		return cell->rendered_value->height_pixel;
}
