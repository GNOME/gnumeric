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
#include <string.h>

#define BUG_105322
#undef DEBUG_BOUNDING_BOX

#ifndef USE_RV_POOLS
#define USE_RV_POOLS 1
#endif

#if USE_RV_POOLS
/* Memory pool for strings.  */
static GnmMemChunk *rendered_value_pool;
#define CHUNK_ALLOC(T,p) ((T*)gnm_mem_chunk_alloc (p))
#define CHUNK_FREE(p,v) gnm_mem_chunk_free ((p), (v))
#else
#define CHUNK_ALLOC(T,c) g_new (T,1)
#define CHUNK_FREE(p,v) g_free ((v))
#endif


static guint16
calc_indent (PangoContext *context, const GnmStyle *mstyle, Sheet *sheet)
{
	int indent = 0;
	if (mstyle_is_element_set (mstyle, MSTYLE_INDENT)) {
		indent = mstyle_get_indent (mstyle);
		if (indent) {
			GnmFont *style_font =
				scg_get_style_font (context, sheet, mstyle);
			indent *= style_font->approx_width.pixels.digit;
			style_font_unref (style_font);
		}
	}
	return MIN (indent, 65535);
}

/**
 * rendered_value_render :
 *
 * returns TRUE if the result depends on the width of the cell
 **/
static gboolean
rendered_value_render (GString *str,
		       GnmCell const *cell,
		       PangoContext *context, GnmStyle const *mstyle,
		       gboolean allow_variable_width, gboolean *display_formula,
		       GnmColor **color)
{
	Sheet const *sheet = cell->base.sheet;

	/* Is the format variable,  we may ignore it, but we still need to know
	 * if it is possible */
	gboolean is_variable_width = FALSE;

	*display_formula = cell_has_expr (cell) && sheet && sheet->display_formulas;

	if (*display_formula) {
		GnmParsePos pp;
		g_string_append_c (str, '=');
		gnm_expr_as_gstring (str, cell->base.expression,
				     parse_pos_init_cell (&pp, cell),
				     gnm_expr_conventions_default);
		*color = NULL;
	} else if (sheet && sheet->hide_zero && cell_is_zero (cell)) {
		*color = NULL;
	} else if (mstyle_is_element_set (mstyle, MSTYLE_FORMAT)) {
		double col_width = -1.;
		/* entered text CAN be null if called by set_value */
		GnmFormat *format = mstyle_get_format (mstyle);

		/* For format general approximate the cell width in characters */
		if (style_format_is_general (format)) {
			gboolean is_rotated = (mstyle_get_rotation (mstyle) != 0);
			is_variable_width = !is_rotated &&
				(VALUE_FMT (cell->value) == NULL ||
				 style_format_is_general (VALUE_FMT (cell->value)));
			if (is_variable_width && allow_variable_width) {
				GnmFont *style_font =
					scg_get_style_font (context, sheet, mstyle);
				double wdigit = style_font->approx_width.pts.digit;

				if (wdigit > 0.0) {
					double cell_width;
					if (cell_is_merged (cell)) {
						GnmRange const *merged =
							sheet_merge_is_corner (sheet, &cell->pos);

						cell_width = sheet_col_get_distance_pts (sheet,
							merged->start.col, merged->end.col + 1);
					} else
						cell_width = cell->col_info->size_pts;
					cell_width -= cell->col_info->margin_a + cell->col_info->margin_b;

#if 0 /* too restrictive for now, do something more accurate later */
					/*
					 * FIXME: we should really pass these to the format function,
					 * so we can measure actual characters, not just what might be
					 * there.
					 */
					cell_width -= MAX (0.0, style_font->approx_width.pts.e - wdigit);
					cell_width -= MAX (0.0, style_font->approx_width.pts.decimal - wdigit);
					cell_width -= 2 * MAX (0.0, style_font->approx_width.pts.sign - wdigit);
#endif
					col_width = cell_width / wdigit;
				}
				style_font_unref (style_font);
			} else
				format = VALUE_FMT (cell->value);
		}
		format_value_gstring (str, format, cell->value, color,
				      col_width,
				      sheet ? workbook_date_conv (sheet->workbook) : NULL);
	} else {
		g_warning ("No format: serious error");
	}
	return is_variable_width;
}


#if 0
some quicky code to dump an attr list
we can use this to export them as text
static gboolean
cb_dump (PangoAttribute *a)
{
	g_print ("dump : %d..%d\n", a->start_index, a->end_index);
	return FALSE;
}

			pango_attr_list_filter (attrs,
				(PangoAttrFilterFunc) cb_dump, NULL);
#endif

/* Gets the bounds of a layout in pango units.  Mostly copied from gtklabel.c */
static void
get_rotated_layout_bounds (PangoLayout *layout, int *width, int *height)
{
	PangoContext *context = pango_layout_get_context (layout);
	const PangoMatrix *matrix = pango_context_get_matrix (context);
	gdouble x_min = 0, x_max = 0, y_min = 0, y_max = 0; /* quiet gcc */
	PangoRectangle logical_rect;
	gint i, j;

	pango_layout_get_extents (layout, NULL, &logical_rect);

	for (i = 0; i < 2; i++)
	{
		gdouble x = (i == 0) ? logical_rect.x : logical_rect.x + logical_rect.width;
		for (j = 0; j < 2; j++)
		{
			gdouble y = (j == 0) ? logical_rect.y : logical_rect.y + logical_rect.height;

			gdouble xt = (x * matrix->xx + y * matrix->xy) + matrix->x0 * PANGO_SCALE;
			gdouble yt = (x * matrix->yx + y * matrix->yy) + matrix->y0 * PANGO_SCALE;

			if (i == 0 && j == 0)
			{
				x_min = x_max = xt;
				y_min = y_max = yt;
			}
			else
			{
				if (xt < x_min)
					x_min = xt;
				if (yt < y_min)
					y_min = yt;
				if (xt > x_max)
					x_max = xt;
				if (yt > y_max)
					y_max = yt;
			}
		}
	}

	*width = ceil (x_max) - floor (x_min);
	*height = ceil (y_max) - floor (y_min);
}

void
rendered_value_remeasure (RenderedValue *rv)
{
	if (rv->rotation) {
		PangoMatrix rotmat = PANGO_MATRIX_INIT;
		PangoContext *context = pango_layout_get_context (rv->layout);

		pango_matrix_rotate (&rotmat, rv->rotation);
		pango_context_set_matrix (context, &rotmat);
		pango_layout_context_changed (rv->layout);
		get_rotated_layout_bounds (rv->layout, &rv->layout_natural_width, &rv->layout_natural_height);
		pango_context_set_matrix (context, NULL);
		pango_layout_context_changed (rv->layout);
	} else {
		pango_layout_get_size (rv->layout,
				       &rv->layout_natural_width,
				       &rv->layout_natural_height);
	}
}


/**
 * rendered_value_new:
 * @cell:   The cell
 * @mstyle: The mstyle associated with the cell
 * @allow_variable_width : Allow format to depend on column width.
 * @context: A pango context for layout.
 *
 * Formats the value of the cell according to the format style given in @mstyle
 *
 * Return value: a new RenderedValue
 **/
RenderedValue *
rendered_value_new (GnmCell *cell, GnmStyle const *mstyle,
		    gboolean allow_variable_width,
		    PangoContext *context)
{
	RenderedValue	*res;
	Sheet		*sheet;
	GnmColor	*color;
	PangoLayout     *layout;
	PangoAttrList   *attrs;
	GnmColor const *fore;
	gboolean        display_formula;
	double          zoom;

	/* This screws thread safety (which we don't need).  */
	static GString  *str = NULL;

	g_return_val_if_fail (cell != NULL, NULL);
	g_return_val_if_fail (cell->value != NULL, NULL);
	g_return_val_if_fail (context != NULL, NULL);

	/* Special handling for manual recalc.
	 * If a cell has a new expression and something tries to display it we
	 * need to recalc the value */
	if (cell->base.flags & CELL_HAS_NEW_EXPR) {
		cell_eval (cell);
	}

	sheet = cell->base.sheet;

	if (str)
		g_string_truncate (str, 0);
	else
		str = g_string_sized_new (100);

	res = CHUNK_ALLOC (RenderedValue, rendered_value_pool);
	res->variable_width = rendered_value_render (str, cell, context, mstyle,
		allow_variable_width, &display_formula, &color);
	res->indent_left = res->indent_right = 0;
	res->numeric_overflow = FALSE;
	res->hfilled = FALSE;
	res->vfilled = FALSE;
	res->wrap_text = mstyle_get_effective_wrap_text (mstyle);
	res->effective_halign = style_default_halign (mstyle, cell);
	res->effective_valign = mstyle_get_align_v (mstyle);
	res->rotation = mstyle_get_rotation (mstyle);
	res->might_overflow =
		cell_is_number (cell) &&
		!display_formula &&
		mstyle_get_rotation (mstyle) == 0;

	res->layout = layout = pango_layout_new (context);
	pango_layout_set_text (layout, str->str, str->len);

	/* FIXME: this should be per view.  */
	zoom = sheet ? sheet->last_zoom_factor_used : 1;

	attrs = mstyle_get_pango_attrs (mstyle, context, zoom);
#ifdef BUG_105322
	/* See http://bugzilla.gnome.org/show_bug.cgi?id=105322 */
	fore = color ? color : mstyle_get_color (mstyle, MSTYLE_COLOR_FORE);
	res->color = fore->color;
	if (color) style_color_unref (color);
#else
	if (color) {
		PangoAttrList *new_attrs = pango_attr_list_copy (attrs);
		PangoAttribute *attr;

		pango_attr_list_unref (attrs);
		attrs = new_attrs;
		attr = pango_attr_foreground_new (color->red, color->green, color->blue);
		attr->start_index = 0;
		attr->end_index = -1;
		pango_attr_list_insert (attrs, attr);
		style_color_unref (color);
	}
#endif

#ifdef DEBUG_BOUNDING_BOX
	{
		PangoAttrList *new_attrs = pango_attr_list_copy (attrs);
		PangoAttribute *attr;

		pango_attr_list_unref (attrs);
		attrs = new_attrs;
		attr = pango_attr_background_new (0xffff, 0, 0);
		attr->start_index = 0;
		attr->end_index = -1;
		pango_attr_list_insert (attrs, attr);
		style_color_unref (color);
	}
#endif

	if (cell->value != NULL) {
		GnmFormat const *fmt = VALUE_FMT (cell->value);
		if (fmt != NULL && style_format_is_markup (fmt)) {
			PangoAttrList *orig = attrs;
			attrs = pango_attr_list_copy (attrs);
			pango_attr_list_splice (attrs, fmt->markup, 0, 0);
			pango_attr_list_unref (orig);
		}
	}
	pango_layout_set_attributes (res->layout, attrs);
	pango_attr_list_unref (attrs);

	switch (res->effective_halign) {
	case HALIGN_LEFT:
		res->indent_left = calc_indent (context, mstyle, sheet);
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
		res->indent_right = calc_indent (context, mstyle, sheet);
		pango_layout_set_alignment (layout, PANGO_ALIGN_RIGHT);
		break;

	case HALIGN_CENTER:
	case HALIGN_CENTER_ACROSS_SELECTION:
		pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
		break;

	default:
		g_warning ("Line justification style not supported.");
	}

	rendered_value_remeasure (res);

	return res;
}

void
rendered_value_destroy (RenderedValue *rv)
{
	if (rv->layout) {
		g_object_unref (G_OBJECT (rv->layout));
		rv->layout = NULL;
	}

	CHUNK_FREE (rendered_value_pool, rv);
}

RenderedValue *
rendered_value_recontext (RenderedValue *rv, PangoContext *context)
{
	RenderedValue *res;
	PangoLayout *layout, *olayout;

	res = CHUNK_ALLOC (RenderedValue, rendered_value_pool);

	*res = *rv;
	res->layout = layout = pango_layout_new (context);
	olayout = rv->layout;

	pango_layout_set_text (layout, pango_layout_get_text (olayout), -1);
	pango_layout_set_alignment (layout, pango_layout_get_alignment (olayout));
	pango_layout_set_attributes (layout, pango_layout_get_attributes (olayout));
	pango_layout_set_single_paragraph_mode (layout, pango_layout_get_single_paragraph_mode (olayout));
	pango_layout_set_justify (layout, pango_layout_get_justify (olayout));
	pango_layout_set_width (layout, pango_layout_get_width (olayout));
	pango_layout_set_spacing (layout, pango_layout_get_spacing (olayout));
	/*
	 * We really want to keep the line breaks, but currently pango
	 * does not support that.  One one-line layouts, however, we
	 * can simply turn off the wrapping.
	 */
	pango_layout_set_wrap (layout,
			       pango_layout_get_wrap (olayout) &&
			       pango_layout_get_line_count (olayout) > 1);
	pango_layout_set_indent (layout, pango_layout_get_indent (olayout));
	pango_layout_set_auto_dir (layout, pango_layout_get_auto_dir (olayout));
#ifdef HAVE_PANGO_LAYOUT_SET_ELLIPSIZE
	pango_layout_set_ellipsize (layout, pango_layout_get_ellipsize (olayout));
#endif
	// pango_layout_set_font_description???
	// ignore tabs

	rendered_value_remeasure (res);
	return res;
}


/* Return the value as a single string without format infomation.
 */
const char *
rendered_value_get_text (RenderedValue const *rv)
{
	g_return_val_if_fail (rv != NULL, "ERROR");
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
cell_get_render_color (GnmCell const *cell)
{
#ifndef BUG_105322
	PangoAttrList *attrs;
	PangoAttrIterator *it;
	PangoAttribute *attr;
#endif

	g_return_val_if_fail (cell != NULL, NULL);

	/* A precursor to just in time rendering Ick! */
	if (cell->rendered_value == NULL)
		cell_render_value ((GnmCell *)cell, TRUE);

#ifdef BUG_105322
	{
		static PangoColor ick;
		ick.red = cell->rendered_value->color.red;
		ick.green = cell->rendered_value->color.green;
		ick.blue = cell->rendered_value->color.blue;
		return &ick;
	}
#else
	attrs = pango_layout_get_attributes (cell->rendered_value->layout);
	g_return_val_if_fail (attrs != NULL, NULL);

	it = pango_attr_list_get_iterator (attrs);
	attr = pango_attr_iterator_get (it, PANGO_ATTR_FOREGROUND);
	pango_attr_iterator_destroy (it);
	if (!attr)
		return NULL;

	return &((PangoAttrColor *)attr)->color;
#endif
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
cell_get_entered_text (GnmCell const *cell)
{
	g_return_val_if_fail (cell != NULL, NULL);

	if (cell_has_expr (cell)) {
		GnmParsePos pp;
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

			if (tmp[0] != '\'' && !gnm_expr_char_start_p (tmp)) {
				GnmValue *val = format_match_number (tmp,
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

/*
 * Return the height of the rendered layout after rotation.
 */
int
cell_rendered_height (GnmCell const *cell)
{
	const RenderedValue *rv;

	g_return_val_if_fail (cell != NULL, 0);

	rv = cell->rendered_value;
	if (!rv)
		return 0;

	return PANGO_PIXELS (cell->rendered_value->layout_natural_height);
}

/*
 * Return the width of the rendered layout after rotation.
 */
int
cell_rendered_width (GnmCell const *cell)
{
	const RenderedValue *rv;

	g_return_val_if_fail (cell != NULL, 0);

	rv = cell->rendered_value;
	if (!rv)
		return 0;

	return PANGO_PIXELS (cell->rendered_value->layout_natural_width);
}

int
cell_rendered_offset (GnmCell const * cell)
{
	if (!cell || !cell->rendered_value)
		return 0;

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
		 rendered_value, pango_layout_get_text (rendered_value->layout));
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
