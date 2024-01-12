/*
 * rendered-value.c: Management & utility routines for formated
 *     colored text.
 *
 * Copyright (C) 2000, 2001 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2001-2009 Morten Welinder (terra@gnome.org)
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
#include <gnumeric.h>
#include <rendered-value.h>

#include <expr.h>
#include <cell.h>
#include <style.h>
#include <style-color.h>
#include <style-font.h>
#include <style-border.h>
#include <sheet.h>
#include <sheet-merge.h>
#include <gnm-format.h>
#include <value.h>
#include <parse-util.h>
#include <workbook.h>
#include <gutils.h>

#include <string.h>
#include <goffice/goffice.h>

#undef DEBUG_BOUNDING_BOX

#ifndef USE_RV_POOLS
#define USE_RV_POOLS 0
#endif

#if USE_RV_POOLS
/* Memory pool for GnmRenderedValue.  */
static GOMemChunk *rendered_value_pool;
static GOMemChunk *rendered_rotated_value_pool;
#define CHUNK_ALLOC(T,p) ((T*)go_mem_chunk_alloc (p))
#define CHUNK_FREE(p,v) go_mem_chunk_free ((p), (v))
#else
static int rv_allocations;
#define CHUNK_ALLOC(T,c) (rv_allocations++, g_slice_new (T))
#define CHUNK_FREE(p,v) (rv_allocations--, g_slice_free1 (sizeof(*v),(v)))
#endif

// Number of decimal digits in a gnm_float rounded down to an integer.
static int sane_digits;

static gboolean
debug_rv (void)
{
	static int res = -1;
	if (res == -1) {
		res = gnm_debug_flag ("rendered-value");
	}
	return res > 0;
}

/*
 * Some valgrind versions have a hard time with signed bitfields,
 * such as GnmRenderedValue::rotation.
 */
static gboolean
valgrind_bitfield_workarounds (void)
{
	static int res = -1;
	if (res == -1) {
		res = gnm_debug_flag ("valgrind-bitfield-workarounds");
	}
	return res > 0;
}


static guint16
calc_indent (PangoContext *context, const GnmStyle *mstyle)
{
	int indent = 0;
	if (gnm_style_is_element_set (mstyle, MSTYLE_INDENT)) {
		int n = gnm_style_get_indent (mstyle);
		if (n) {
			GnmFont *style_font = gnm_style_get_font (mstyle, context);
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
		PangoMatrix rotmat = PANGO_MATRIX_INIT;

		pango_matrix_rotate (&rotmat, rv->rotation);

		sin_a = rotmat.xy;
		rrv->sin_a_neg = (sin_a < 0);
		abs_sin_a = fabs (sin_a);
		cos_a = rotmat.xx;
		pango_context_set_matrix (context, &rotmat);
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

typedef struct {
	double zoom;
	double scale;
	int rise;
} rv_adjust_attributes_t;

static gboolean
rv_adjust_filter (PangoAttribute *attribute, rv_adjust_attributes_t *raat)
{
	if (attribute->klass->type == PANGO_ATTR_RISE) {
		PangoAttrInt *pa_rise = (PangoAttrInt *)attribute;
		pa_rise->value = raat->scale * pa_rise->value + raat->rise;
	}
	if (attribute->klass->type == PANGO_ATTR_SCALE && raat->scale != 1.) {
		PangoAttrFloat *pa_scale = (PangoAttrFloat *)attribute;
		pa_scale->value = pa_scale->value * raat->zoom;
	}
	return FALSE;
}

static void
rv_adjust_attributes (PangoAttrList *markup, double zoom, double scale, int rise)
{
	rv_adjust_attributes_t raat = {zoom, scale, rise};

	pango_attr_list_filter (markup, (PangoAttrFilterFunc) rv_adjust_filter,
				&raat);
}

static gboolean
too_many_digits (const char *s)
{
	int count = 0;
	const char *p;

	// Count significant digits
	for (p = s; *p; p = g_utf8_next_char (p)) {
		gunichar uc = g_utf8_get_char (p);
		if (uc == '0' && count == 0)
			continue;
		if (g_unichar_isdigit (uc))
			count++;
		if (uc == 'e' || uc == 'E')
			break;
	}

	return count > sane_digits;
}

/**
 * gnm_rendered_value_new: (skip)
 * @cell: The cell
 * @context: A #PangoContext for text measurement.
 * @allow_variable_width: Allow format to depend on column width.
 * @zoom: zoom factor.
 *
 * Formats the value of the cell according to its style.
 *
 * Returns: a new GnmRenderedValue
 **/
GnmRenderedValue *
gnm_rendered_value_new (GnmCell const *cell,
			PangoContext *context,
			gboolean allow_variable_width,
			double zoom)
{
	GnmRenderedValue	*res;
	PangoLayout     *layout;
	PangoAttrList   *attrs;
	int              rotation;
	Sheet const     *sheet;
	gboolean         displayed_formula;
	GnmStyle const *mstyle;
	PangoDirection dir;
	char const *text;
	gboolean debug = debug_rv ();

	g_return_val_if_fail (cell != NULL, NULL);

	/* sheet->workbook can be NULL when called from preview-grid.c  */
	sheet = cell->base.sheet;

	if (debug)
		g_printerr ("Rendering %s value [%s]\n",
			    cell_name (cell),
			    value_peek_string (cell->value));

	displayed_formula =
		gnm_cell_has_expr (cell) && sheet->display_formulas;

	/* Special handling for manual recalc.
	 * If a cell has a new expression and something tries to display it we
	 * need to recalc the value */
	if (cell->base.flags & GNM_CELL_HAS_NEW_EXPR) {
		gnm_cell_eval ((GnmCell *)cell);
	}

	/* Must come after above gnm_cell_eval.  */
	g_return_val_if_fail (cell->value != NULL, NULL);

	mstyle = gnm_cell_get_effective_style (cell);

	rotation = gnm_style_get_rotation (mstyle);
	if (rotation) {
		GnmRenderedRotatedValue *rrv;
		GnmStyleElement e;

		rrv = CHUNK_ALLOC (GnmRenderedRotatedValue, rendered_rotated_value_pool);
		res = &rrv->rv;
		if (valgrind_bitfield_workarounds ()) {
			memset (&res->go_fore_color + 1,
				0,
				(char *)(res + 1) - (char *)(&res->go_fore_color + 1));
		}

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
			PangoAttrList *c_markup = NULL;
			PangoAttrIterator *iter;
			GSList *extra_attrs = NULL, *l;
			PangoFontDescription *desc = pango_font_description_new ();
			double font_size, scale = 1., tscale;
			int rise = 0;

			if (debug)
				g_printerr ("  Markup on value: %s\n", go_format_as_XL (fmt));

			attrs = pango_attr_list_copy (attrs);

			iter = pango_attr_list_get_iterator (attrs);
			pango_attr_iterator_get_font (iter,
						      desc,
						      NULL,
						      &extra_attrs);
			font_size = pango_font_description_get_size (desc)/
				(double)PANGO_SCALE;

			for (l = extra_attrs; l != NULL; l = l->next) {
				PangoAttribute *pa = l->data;
				if (pa->klass->type == PANGO_ATTR_RISE) {
					PangoAttrInt *pa_rise = l->data;
					rise = pa_rise->value;

				}
				if (pa->klass->type == PANGO_ATTR_SCALE) {
					PangoAttrFloat *pa_scale = l->data;
					scale = pa_scale->value;
				}
			}
			g_slist_free_full (extra_attrs,
					      (GFreeFunc) pango_attribute_destroy);
			pango_font_description_free (desc);
			pango_attr_iterator_destroy (iter);

			tscale = font_size/10. * scale;
			if (tscale != 1|| rise != 0) {
				markup = c_markup = pango_attr_list_copy
					((PangoAttrList *)markup);
				rv_adjust_attributes (c_markup, zoom, tscale, rise);
			}

			// See #607
			pango_attr_list_splice (attrs, (PangoAttrList *)markup, 0, INT_MAX / 2);
			pango_attr_list_unref (orig);
			pango_attr_list_unref (c_markup);
		}
	}

	pango_layout_set_attributes (res->layout, attrs);
	pango_attr_list_unref (attrs);

	/* Store foreground color.  */
	/* Wrapping this colour around the attribute list drops performance! */
	res->go_fore_color = (gnm_style_get_font_color (mstyle))->go_color;

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

	if (res->effective_halign == GNM_HALIGN_FILL) {
		pango_layout_set_single_paragraph_mode (layout, TRUE);
		res->variable_width = TRUE;
	}

	/* ---------------------------------------- */

	res->numeric_overflow = FALSE;

	if (displayed_formula) {
		GnmParsePos pp;
		GnmConventionsOut out;
		gboolean is_array = gnm_expr_top_is_array (cell->base.texpr);

		out.accum = g_string_new (is_array ? "{=" : "=");
		out.convs = sheet->convs;
		out.pp    = &pp;

		parse_pos_init_cell (&pp, cell),
		gnm_expr_top_as_gstring (cell->base.texpr, &out);
		if (is_array)
			g_string_append_c (out.accum, '}');
		pango_layout_set_text (layout, out.accum->str, out.accum->len);
		g_string_free (out.accum, TRUE);
		res->might_overflow = FALSE;
	} else if (sheet->hide_zero && gnm_cell_is_zero (cell)) {
		pango_layout_set_text (layout, "", 0);
		res->might_overflow = FALSE;
	} else {
		int col_width = -1;
		GOFormat const *format = gnm_style_get_format (mstyle);
		GODateConventions const *date_conv = sheet->workbook
			? sheet_date_conv (sheet)
			: NULL;
		GnmFont *font = gnm_style_get_font (mstyle, context);
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
			if (col_width_pixels < 0)
				col_width_pixels = 0;
			col_width = col_width_pixels * PANGO_SCALE;
		}

		err = gnm_format_layout (layout, font->go.metrics, format,
					 cell->value,
					 col_width, date_conv, TRUE);

		// If we are formatting a number as General without a limit
		// on size [i.e., we are autofitting a column] then avoid excess
		// precision.  This is somewhat hacky.
		if (col_width == -1 &&
		    go_format_is_general (format) &&
		    VALUE_IS_FLOAT (cell->value) &&
		    font->go.metrics->min_digit_width > 0 &&
		    too_many_digits (pango_layout_get_text (layout))) {
			int width;
			int delta = (font->go.metrics->min_digit_width + 1) / 2;
			pango_layout_get_size (layout, &width, NULL);
			col_width = width - delta;
			err = gnm_format_layout (layout,
						 font->go.metrics, format,
						 cell->value,
						 col_width, date_conv, TRUE);
		}

		switch (err) {
		case GO_FORMAT_NUMBER_DATE_ERROR:
			pango_layout_set_text (layout, "", -1);
			pango_layout_set_alignment (layout, PANGO_ALIGN_LEFT);
			res->numeric_overflow = TRUE;
			res->effective_halign = GNM_HALIGN_LEFT;
			break;
		default:
			break;
		}
	}

	/* ---------------------------------------- */

	text = pango_layout_get_text (layout);
	dir = (text && *text)? pango_find_base_dir (text, -1): PANGO_DIRECTION_LTR;
	if (gnm_style_get_align_h (mstyle) == GNM_HALIGN_GENERAL && dir == PANGO_DIRECTION_RTL) {
		switch (res->effective_halign) {
		case GNM_HALIGN_LEFT:
			res->effective_halign = GNM_HALIGN_RIGHT;
			break;
		case GNM_HALIGN_RIGHT:
			res->effective_halign = GNM_HALIGN_LEFT;
			break;
		}
	}
	switch (res->effective_halign) {
	case GNM_HALIGN_LEFT:
		res->indent_left = calc_indent (context, mstyle);
		pango_layout_set_alignment (layout, (dir == PANGO_DIRECTION_RTL)? PANGO_ALIGN_RIGHT: PANGO_ALIGN_LEFT);
		break;

	case GNM_HALIGN_JUSTIFY:
		pango_layout_set_justify (layout, TRUE);
		pango_layout_set_alignment (layout, PANGO_ALIGN_LEFT);
		break;

	case GNM_HALIGN_FILL:
		break;

	case GNM_HALIGN_RIGHT:
		res->indent_right = calc_indent (context, mstyle);
		pango_layout_set_alignment (layout, (dir == PANGO_DIRECTION_RTL)? PANGO_ALIGN_LEFT: PANGO_ALIGN_RIGHT);
		break;

	case GNM_HALIGN_DISTRIBUTED:
		pango_layout_set_justify (layout, TRUE);
		pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
		break;

	case GNM_HALIGN_CENTER:
	case GNM_HALIGN_CENTER_ACROSS_SELECTION:
		pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
		break;

	default:
		g_warning ("Line justification style not supported.");
	}
			 /* ---------------------------------------- */

	go_pango_translate_layout (layout);
	gnm_rendered_value_remeasure (res);

	return res;
}

void
gnm_rendered_value_destroy (GnmRenderedValue *rv)
{
	if (rv->layout) {
		g_object_unref (rv->layout);
		rv->layout = NULL;
	}

	if (rv->rotation) {
		GnmRenderedRotatedValue *rrv = (GnmRenderedRotatedValue *)rv;
		g_free (rrv->lines);
		CHUNK_FREE (rendered_rotated_value_pool, rrv);
	} else
		CHUNK_FREE (rendered_value_pool, rv);
}

/* Return the value as a single string without format infomation.
 */
char const *
gnm_rendered_value_get_text (GnmRenderedValue const *rv)
{
	g_return_val_if_fail (rv != NULL, "ERROR");
	return pango_layout_get_text (rv->layout);
}

GOColor
gnm_rendered_value_get_color (GnmRenderedValue const * rv)
{
	return rv->go_fore_color;
}

/* ------------------------------------------------------------------------- */

static gboolean
debug_rvc (void)
{
	static int res = -1;
	if (res == -1) {
		res = gnm_debug_flag ("rvc");
	}
	return res > 0;
}

/**
 * gnm_rvc_new: (skip)
 * @context:   The context
 * @size:
 *
 * Returns: a new GnmRenderedValueCollection
 **/
GnmRenderedValueCollection *
gnm_rvc_new (PangoContext *context, gsize size)
{
	GnmRenderedValueCollection *res = g_new0 (GnmRenderedValueCollection, 1);

	res->context = g_object_ref (context);

	res->size = size;
	res->values = g_hash_table_new_full
		(g_direct_hash, g_direct_equal,
		 NULL,
		 (GDestroyNotify)gnm_rendered_value_destroy);

	if (debug_rvc ())
		g_printerr ("Created rendered value cache %p of size %u\n",
			    res, (unsigned)size);

	return res;
}

void
gnm_rvc_free (GnmRenderedValueCollection *rvc)
{
	g_return_if_fail (rvc != NULL);

	if (debug_rvc ())
		g_printerr ("Destroying rendered value cache %p\n", rvc);

	g_object_unref (rvc->context);
	g_hash_table_destroy (rvc->values);
	g_free (rvc);
}

/**
 * gnm_rvc_query: (skip)
 * @rvc:   The rendered value collection
 * @cell: #GnmCell
 *
 * Returns: the rendered value for @cell.
 **/
GnmRenderedValue *
gnm_rvc_query (GnmRenderedValueCollection *rvc, GnmCell const *cell)
{
	g_return_val_if_fail (rvc != NULL, NULL);

	return g_hash_table_lookup (rvc->values, cell);
}

void
gnm_rvc_store (GnmRenderedValueCollection *rvc,
	       GnmCell const *cell,
	       GnmRenderedValue *rv)
{
	g_return_if_fail (rvc != NULL);

	/* Crude cache management:  */
	if (g_hash_table_size (rvc->values) >= rvc->size) {
		if (debug_rvc ())
			g_printerr ("Clearing rendered value cache %p\n", rvc);
		g_hash_table_remove_all (rvc->values);
	}

	g_hash_table_insert (rvc->values, (gpointer)cell, rv);
}

void
gnm_rvc_remove (GnmRenderedValueCollection *rvc, GnmCell const *cell)
{
	g_return_if_fail (rvc != NULL);
	g_hash_table_remove (rvc->values, (gpointer)cell);
}

/* ------------------------------------------------------------------------- */

/**
 * gnm_rendered_value_init: (skip)
 */
void
gnm_rendered_value_init (void)
{
	sane_digits = (int)gnm_floor (GNM_MANT_DIG * gnm_log10 (GNM_RADIX));

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

/**
 * gnm_rendered_value_shutdown: (skip)
 */
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
