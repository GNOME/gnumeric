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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "rendered-value.h"

#include "expr.h"
#include "cell.h"
#include "style.h"
#include "style-color.h"
#include "style-font.h"
#include "style-border.h"
#include "sheet.h"
#include "sheet-merge.h"
#include "gnm-format.h"
#include "value.h"
#include "parse-util.h"
#include "workbook.h"

#include <string.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-font.h>

#undef DEBUG_BOUNDING_BOX

#ifndef USE_RV_POOLS
#ifdef HAVE_G_SLICE_ALLOC
#define USE_RV_POOLS 0
#else
#define USE_RV_POOLS 1
#endif
#endif

#if USE_RV_POOLS
/* Memory pool for GnmRenderedValue.  */
static GOMemChunk *rendered_value_pool;
static GOMemChunk *rendered_rotated_value_pool;
#define CHUNK_ALLOC(T,p) ((T*)go_mem_chunk_alloc (p))
#define CHUNK_FREE(p,v) go_mem_chunk_free ((p), (v))
#else
static int rv_allocations;
#ifdef HAVE_G_SLICE_ALLOC
#define CHUNK_ALLOC(T,c) (rv_allocations++, g_slice_new (T))
#define CHUNK_FREE(p,v) (rv_allocations--, g_slice_free1 (sizeof(*v),(v)))
#else
#define CHUNK_ALLOC(T,c) (rv_allocations++, g_new (T,1))
#define CHUNK_FREE(p,v) (rv_allocations--, g_free ((v)))
#endif
#endif


static guint16
calc_indent (PangoContext *context, const GnmStyle *mstyle, double zoom)
{
	int indent = 0;
	if (gnm_style_is_element_set (mstyle, MSTYLE_INDENT)) {
		int n = gnm_style_get_indent (mstyle);
		if (n) {
			GnmFont *style_font = gnm_style_get_font (mstyle, context, zoom);
			indent = PANGO_PIXELS (n * style_font->go.metrics->avg_digit_width);
		}
	}
	return MIN (indent, 65535);
}


void
gnm_rendered_value_remeasure (GnmRenderedValue *rv)
{
	if (rv->rotation) {
		GnmRenderedRotatedValue *rrv = (GnmRenderedRotatedValue *)rv;
		PangoContext *context = pango_layout_get_context (rv->layout);
		double sin_a, abs_sin_a, cos_a;
		int sdx = 0;
		int x0 = 0, x1 = 0;
		PangoLayoutIter *iter;
		int l = 0;
		int lwidth;

		sin_a = rrv->rotmat.xy;
		abs_sin_a = fabs (sin_a);
		cos_a = rrv->rotmat.xx;
		pango_context_set_matrix (context, &rrv->rotmat);
		pango_layout_context_changed (rv->layout);

		rrv->linecount = pango_layout_get_line_count (rv->layout);
		rrv->lines = g_new (struct GnmRenderedRotatedValueInfo, rrv->linecount);
		pango_layout_get_size (rv->layout, &lwidth, NULL);

		rv->layout_natural_height = 0;

		iter = pango_layout_get_iter (rv->layout);
		do {
			PangoRectangle logical;
			int x, dx, dy, indent;
			int h, ytop, ybot, baseline;

			pango_layout_iter_get_line_extents (iter, NULL, &logical);
			pango_layout_iter_get_line_yrange (iter, &ytop, &ybot);
			baseline = pango_layout_iter_get_baseline (iter);
			indent = logical.x;
			if (sin_a < 0)
				indent -= lwidth;

			if (l == 0 && rv->noborders)
				sdx = (int)(baseline * sin_a - ybot / sin_a);
			dx = sdx + (int)(ybot / sin_a + indent * cos_a);
			dy = (int)((baseline - ybot) * cos_a - indent * sin_a);

			rrv->lines[l].dx = dx;
			rrv->lines[l].dy = dy;

			/* Left edge.  */
			x = dx - (int)((baseline - ytop) * sin_a);
			x0 = MIN (x0, x);

			/* Right edge.  */
			x = dx + (int)(logical.width * cos_a + (ybot - baseline) * sin_a);
			x1 = MAX (x1, x);

			h = logical.width * abs_sin_a + logical.height * cos_a;
			if (h > rv->layout_natural_height)
				rv->layout_natural_height = h;

			l++;
		} while (pango_layout_iter_next_line (iter));
		pango_layout_iter_free (iter);

		rv->layout_natural_width = x1 - x0;
		if (sin_a < 0) {
			int dx = rv->layout_natural_width;
			for (l = 0; l < rrv->linecount; l++)
				rrv->lines[l].dx += dx;
		}
		for (l = 0; l < rrv->linecount; l++)
			rrv->lines[l].dy += rv->layout_natural_height;

#if 0
		g_print ("Natural size: %d x %d\n", rv->layout_natural_width, rv->layout_natural_height);
#endif

		pango_context_set_matrix (context, NULL);
		pango_layout_context_changed (rv->layout);
	} else
		pango_layout_get_size (rv->layout,
				       &rv->layout_natural_width,
				       &rv->layout_natural_height);
}


/**
 * gnm_rendered_value_new:
 * @cell:   The cell
 * @mstyle: The mstyle associated with the cell
 * @allow_variable_width : Allow format to depend on column width.
 * @context: A pango context for layout.
 *
 * Formats the value of the cell according to the format style given in @mstyle
 *
 * Return value: a new GnmRenderedValue
 **/
GnmRenderedValue *
gnm_rendered_value_new (GnmCell *cell, GnmStyle const *mstyle,
			gboolean allow_variable_width,
			PangoContext *context,
			double zoom)
{
	GnmRenderedValue	*res;
	GOColor		 fore;
	PangoLayout     *layout;
	PangoAttrList   *attrs;
	int              rotation;
	Sheet const     *sheet;
	gboolean         displayed_formula;

	g_return_val_if_fail (cell != NULL, NULL);
	g_return_val_if_fail (context != NULL, NULL);

	/* sheet->workbook can be NULL when called from preview-grid.c  */
	sheet = cell->base.sheet;

	displayed_formula =
		gnm_cell_has_expr (cell) && sheet->display_formulas;

	/* Special handling for manual recalc.
	 * If a cell has a new expression and something tries to display it we
	 * need to recalc the value */
	if (cell->base.flags & GNM_CELL_HAS_NEW_EXPR) {
		gnm_cell_eval (cell);
	}

	/* Must come after above gnm_cell_eval.  */
	g_return_val_if_fail (cell->value != NULL, NULL);

	rotation = gnm_style_get_rotation (mstyle);
	if (rotation) {
		static PangoMatrix const id = PANGO_MATRIX_INIT;
		GnmRenderedRotatedValue *rrv;
		GnmStyleElement e;

		rrv = CHUNK_ALLOC (GnmRenderedRotatedValue, rendered_rotated_value_pool);
		res = &rrv->rv;

		rrv->rotmat = id;
		pango_matrix_rotate (&rrv->rotmat, rotation);
		rrv->linecount = 0;
		rrv->lines = NULL;

		res->noborders = TRUE;
		/* Deliberately exclude diagonals.  */
		for (e = MSTYLE_BORDER_TOP; e <= MSTYLE_BORDER_RIGHT; e++) {
			GnmBorder *b = gnm_style_get_border (mstyle, e);
			if (!gnm_style_border_is_blank (b)) {
				res->noborders = FALSE;
				break;
			}
		}
	} else {
		res = CHUNK_ALLOC (GnmRenderedValue, rendered_value_pool);
		res->noborders = FALSE;
	}
	res->rotation = rotation;

	res->layout = layout = pango_layout_new (context);
	res->hfilled = FALSE;
	res->vfilled = FALSE;
	res->variable_width = FALSE;
	res->drawn = FALSE;

	/* ---------------------------------------- */

	attrs = gnm_style_get_pango_attrs (mstyle, context, zoom);
#ifdef DEBUG_BOUNDING_BOX
	/* Make the whole layout end up with a red background.  */
	{
		PangoAttrList *new_attrs = pango_attr_list_copy (attrs);
		PangoAttribute *attr;

		pango_attr_list_unref (attrs);
		attrs = new_attrs;
		attr = pango_attr_background_new (0xffff, 0, 0);
		attr->start_index = 0;
		attr->end_index = -1;
		pango_attr_list_insert (attrs, attr);
	}
#endif

	/* Add markup.  */
	{
		GOFormat const *fmt = VALUE_FMT (cell->value);
		if (fmt != NULL && go_format_is_markup (fmt)) {
			PangoAttrList *orig = attrs;
			const PangoAttrList *markup = go_format_get_markup (fmt);
			attrs = pango_attr_list_copy (attrs);
			pango_attr_list_splice (attrs,
						(PangoAttrList *)markup,
						0, 0);
			pango_attr_list_unref (orig);
		}
	}
	pango_layout_set_attributes (res->layout, attrs);
	pango_attr_list_unref (attrs);

	/* ---------------------------------------- */

	/*
	 * Excel actually does something rather weird.  Just like
	 * everywhere else we just see displayed formulas as
	 * strings.
	 */
	res->wrap_text =
		(VALUE_IS_STRING (cell->value) || displayed_formula) &&
		gnm_style_get_effective_wrap_text (mstyle);

	res->effective_valign = gnm_style_get_align_v (mstyle);
	res->effective_halign = gnm_style_default_halign (mstyle, cell);
	res->indent_left = res->indent_right = 0;
	switch (res->effective_halign) {
	case HALIGN_LEFT:
		res->indent_left = calc_indent (context, mstyle, zoom);
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
		res->variable_width = TRUE;
		break;

	case HALIGN_RIGHT:
		res->indent_right = calc_indent (context, mstyle, zoom);
		pango_layout_set_alignment (layout, PANGO_ALIGN_RIGHT);
		break;

	case HALIGN_DISTRIBUTED:
	case HALIGN_CENTER:
	case HALIGN_CENTER_ACROSS_SELECTION:
		pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
		break;

	default:
		g_warning ("Line justification style not supported.");
	}

	/* ---------------------------------------- */

	res->numeric_overflow = FALSE;

	if (displayed_formula) {
		GnmParsePos pp;
		GnmConventionsOut out;

		out.accum = g_string_new ("=");
		out.convs = sheet->convs;
		out.pp    = &pp;

		parse_pos_init_cell (&pp, cell),
		gnm_expr_top_as_gstring (cell->base.texpr, &out);
		pango_layout_set_text (layout, out.accum->str, out.accum->len);
		g_string_free (out.accum, TRUE);
		fore = 0;
		res->might_overflow = FALSE;
	} else if (sheet->hide_zero && gnm_cell_is_zero (cell)) {
		pango_layout_set_text (layout, "", 0);
		fore = 0;
		res->might_overflow = FALSE;
	} else {
		int col_width = -1;
		GOFormat *format = gnm_style_get_format (mstyle);
		GODateConventions const *date_conv = sheet->workbook
			? workbook_date_conv (sheet->workbook)
			: NULL;
		GnmFont *font = gnm_style_get_font (mstyle, context, zoom);
		gboolean is_rotated = (rotation != 0);
		gboolean variable;
		GOFormatNumberError err;

		if (go_format_is_general (format) && VALUE_FMT (cell->value))
			format = VALUE_FMT (cell->value);

		res->might_overflow = !is_rotated &&
			VALUE_IS_FLOAT (cell->value);

		if (go_format_is_general (format))
			variable = !is_rotated && VALUE_IS_FLOAT (cell->value);
		else
			variable = !is_rotated && go_format_is_var_width (format);

		if (variable)
			res->variable_width = TRUE;

		if (variable && allow_variable_width) {
			int col_width_pixels;

			if (gnm_cell_is_merged (cell)) {
				GnmRange const *merged =
					gnm_sheet_merge_is_corner (sheet, &cell->pos);

				col_width_pixels = sheet_col_get_distance_pixels
					(sheet,
					 merged->start.col, merged->end.col + 1);
			} else {
				ColRowInfo const *ci = sheet_col_get_info (sheet, cell->pos.col);
				col_width_pixels = ci->size_pixels;
			}
			col_width_pixels -= (GNM_COL_MARGIN + GNM_COL_MARGIN + 1);
			col_width = col_width_pixels * PANGO_SCALE;
		}

		err = gnm_format_layout (layout, font->go.metrics, format,
					 cell->value,
					 &fore, col_width, date_conv, TRUE);

		switch (err) {
		case GO_FORMAT_NUMBER_DATE_ERROR:
			pango_layout_set_text (layout, "", -1);
			pango_layout_set_alignment (layout, PANGO_ALIGN_LEFT);
			res->numeric_overflow = TRUE;
			res->effective_halign = HALIGN_LEFT;
			break;
		default:
			break;
		}
	}

	/* ---------------------------------------- */

	/*
	 * We store the foreground color separately because
	 * 1. It is [used to be?] slow to store it as an attribute, see
	 *    http://bugzilla.gnome.org/show_bug.cgi?id=105322
	 * 2. This way we get to share the attribute list.
	 */
	if (0 == fore) {
		GnmColor const *c = gnm_style_get_font_color (mstyle);
		res->go_fore_color = c->go_color;
	} else
		res->go_fore_color = fore;

	gnm_rendered_value_remeasure (res);

	return res;
}

void
gnm_rendered_value_destroy (GnmRenderedValue *rv)
{
	if (rv->layout) {
		g_object_unref (G_OBJECT (rv->layout));
		rv->layout = NULL;
	}

	if (rv->rotation) {
		GnmRenderedRotatedValue *rrv = (GnmRenderedRotatedValue *)rv;
		g_free (rrv->lines);
		CHUNK_FREE (rendered_rotated_value_pool, rrv);
	} else
		CHUNK_FREE (rendered_value_pool, rv);
}

GnmRenderedValue *
gnm_rendered_value_recontext (GnmRenderedValue *rv, PangoContext *context)
{
	GnmRenderedValue *res;
	PangoLayout *layout, *olayout;

	if (rv->rotation) {
		GnmRenderedRotatedValue *rres =
			CHUNK_ALLOC (GnmRenderedRotatedValue, rendered_rotated_value_pool);
		res = (GnmRenderedValue *)rres;

		*rres = *(GnmRenderedRotatedValue *)rv;
		rres->lines = g_memdup (rres->lines,
					rres->linecount * sizeof (struct GnmRenderedRotatedValueInfo));
	} else {
		res = CHUNK_ALLOC (GnmRenderedValue, rendered_value_pool);
		*res = *rv;
	}

	res->layout = layout = pango_layout_new (context);
	olayout = rv->layout;

	pango_layout_set_text (layout, pango_layout_get_text (olayout), -1);
	pango_layout_set_alignment (layout, pango_layout_get_alignment (olayout));
	pango_layout_set_attributes (layout, pango_layout_get_attributes (olayout));
	pango_layout_set_single_paragraph_mode (layout, pango_layout_get_single_paragraph_mode (olayout));
	pango_layout_set_justify (layout, pango_layout_get_justify (olayout));
	pango_layout_set_width (layout, pango_layout_get_width (olayout));
	pango_layout_set_spacing (layout, pango_layout_get_spacing (olayout));
	pango_layout_set_wrap (layout, pango_layout_get_wrap (olayout));
	pango_layout_set_indent (layout, pango_layout_get_indent (olayout));
	pango_layout_set_auto_dir (layout, pango_layout_get_auto_dir (olayout));
	pango_layout_set_ellipsize (layout, pango_layout_get_ellipsize (olayout));
	pango_layout_set_font_description (layout, pango_layout_get_font_description (olayout));
	// ignore tabs

	/*
	 * We really want to keep the line breaks, but currently pango
	 * does not support that.
	 */
	if (pango_layout_get_line_count (olayout) == 1) {
		if (pango_layout_get_line_count (layout) > 1) {
			res->wrap_text = FALSE;
			pango_layout_set_width (layout, -1);
		}
	}

	gnm_rendered_value_remeasure (res);
	return res;
}


/* Return the value as a single string without format infomation.
 */
char const *
gnm_rendered_value_get_text (GnmRenderedValue const *rv)
{
	g_return_val_if_fail (rv != NULL, "ERROR");
	return pango_layout_get_text (rv->layout);
}

void
gnm_rendered_value_init (void)
{
#if USE_RV_POOLS
	rendered_value_pool =
		go_mem_chunk_new ("rendered value pool",
				  sizeof (GnmRenderedValue),
				  16 * 1024 - 128);
	rendered_rotated_value_pool =
		go_mem_chunk_new ("rendered rotated value pool",
				  sizeof (GnmRenderedRotatedValue),
				  16 * 1024 - 128);
#endif
}

#if USE_RV_POOLS
static void
cb_rendered_value_pool_leak (gpointer data, G_GNUC_UNUSED gpointer user)
{
	GnmRenderedValue *rendered_value = data;
	g_printerr ("Leaking rendered value at %p [%s].\n",
		    rendered_value, pango_layout_get_text (rendered_value->layout));
}
#endif

void
gnm_rendered_value_shutdown (void)
{
#if USE_RV_POOLS
	go_mem_chunk_foreach_leak (rendered_value_pool, cb_rendered_value_pool_leak, NULL);
	go_mem_chunk_destroy (rendered_value_pool, FALSE);
	rendered_value_pool = NULL;

	go_mem_chunk_foreach_leak (rendered_rotated_value_pool, cb_rendered_value_pool_leak, NULL);
	go_mem_chunk_destroy (rendered_rotated_value_pool, FALSE);
	rendered_rotated_value_pool = NULL;
#else
	if (rv_allocations)
		g_printerr ("Leaking %d rendered values.\n", rv_allocations);
#endif
}
