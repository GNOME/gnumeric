/*
 * sylk-write.c : export sylk
 *
 * Copyright (C) 2007 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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

#include <workbook-priv.h>
#include <workbook-view.h>
#include <sheet.h>
#include <sheet-style.h>
#include <expr.h>
#include <value.h>
#include <cell.h>
#include <mstyle.h>
#include <gutils.h>
#include <parse-util.h>
#include <style-border.h>
#include <ranges.h>

#include <goffice/goffice.h>

#include <gsf/gsf-output.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>

static int
font_equal (gconstpointer a_, gconstpointer b_)
{
	GnmStyle const *a = a_;
	GnmStyle const *b = b_;

	return g_str_equal (gnm_style_get_font_name (a), gnm_style_get_font_name (b)) &&
		gnm_style_get_font_size (a) == gnm_style_get_font_size (b);
}

static guint
font_hash (gconstpointer s_)
{
	GnmStyle const *s = s_;

	return g_str_hash (gnm_style_get_font_name (s)) ^
		(guint)(gnm_style_get_font_size (s));
}


typedef struct {
	GsfOutput *output;

	GnmConventions *convs;

	Workbook *wb;
	Sheet	 *sheet;

	int cur_row;

	GPtrArray *formats;
	GHashTable *format_hash;

	GPtrArray *fonts;
	GHashTable *font_hash;
} SylkWriter;

static void
sylk_write (SylkWriter *state, char const *str)
{
	char const *p, *next;
	gunichar c;

	/* export the valid chunks */
	for (p = str ; *p ; p = next) {
		next = g_utf8_next_char (p);
		c = g_utf8_get_char (p);

		if (c == ';') {
			gsf_output_write (state->output, p - str, str);
			gsf_output_write (state->output, 2, ";;");
			str = next;
		} else if ((next - p) > 1) {
			gsf_output_write (state->output, p - str, str);
			gsf_output_write (state->output, 1, "?");
			str = next;
		}
#warning "handle the magic ascii escaping"
	}
	gsf_output_write (state->output, p - str, str);
}

static void
sylk_output_string (GnmConventionsOut *out, GOString const *string)
{
	g_string_append_c (out->accum, '\"');
	g_string_append (out->accum, string->str);
	g_string_append_c (out->accum, '\"');
}

static GnmValue *
cb_sylk_write_cell (GnmCellIter const *iter, SylkWriter *state)
{
	GnmValue const *v;
	GnmExprTop const *texpr;

	if (iter->pp.eval.row != state->cur_row)
		gsf_output_printf (state->output, "C;Y%d;X%d",
			(state->cur_row = iter->pp.eval.row) + 1,
			iter->pp.eval.col + 1);
	else
		gsf_output_printf (state->output, "C;X%d",
			iter->pp.eval.col + 1);

	v = iter->cell->value;
	if (v) {
		if (VALUE_IS_STRING (v)) {
			gsf_output_write (state->output, 3, ";K\"");
			sylk_write (state, v->v_str.val->str);
			gsf_output_write (state->output, 1, "\"");
		} else if (VALUE_IS_NUMBER (v) || VALUE_IS_ERROR (v)) {
			GString *res = g_string_sized_new (10);
			value_get_as_gstring (v, res, state->convs);
			gsf_output_write (state->output, 2, ";K");
			gsf_output_write (state->output, res->len, res->str);
			g_string_free (res, TRUE);
		} /* ignore the rest */
	}

	texpr = iter->cell->base.texpr;
	if (texpr) {
		if (gnm_expr_top_is_array_corner (texpr)) {
			int cols, rows;
			gnm_expr_top_get_array_size (texpr, &cols, &rows);

			gsf_output_printf (state->output, ";R%d;C%d;M",
					   iter->pp.eval.row + rows,
					   iter->pp.eval.col + cols);
		} else if (gnm_expr_top_is_array_elem (texpr, NULL, NULL)) {
			gsf_output_write (state->output, 2, ";I");
			texpr = NULL;
		} else
			gsf_output_write (state->output, 2, ";E");

		if (texpr != NULL) {
			GnmConventionsOut out;
			out.accum = g_string_new (NULL);
			out.pp    = &iter->pp;
			out.convs = state->convs;
			gnm_expr_top_as_gstring (texpr, &out);
			sylk_write (state, out.accum->str);
			g_string_free (out.accum, TRUE);
		}
	}
	gsf_output_write (state->output, 2, "\r\n");
	return NULL;
}

static gboolean
sylk_get_border (GnmStyle const *style, GnmStyleElement border)
{
	GnmBorder *b = gnm_style_get_border (style, border);
	return b && b->line_type > GNM_STYLE_BORDER_NONE;
}

static void
sylk_write_style (SylkWriter *state, GnmStyle const *style)
{
	GOFormat const *fmt;
	unsigned n;
	GnmHAlign halign;

	gsf_output_printf (state->output, "F");

	halign = gnm_style_get_align_h (style);
	switch (halign) {
	case GNM_HALIGN_LEFT: gsf_output_printf (state->output, ";FD0L"); break;
	case GNM_HALIGN_RIGHT: gsf_output_printf (state->output, ";FD0R"); break;
	case GNM_HALIGN_CENTER: gsf_output_printf (state->output, ";FD0C"); break;
	case GNM_HALIGN_FILL: gsf_output_printf (state->output, ";FD0X"); break;
	default:
		; // Nothing
	}

	fmt = gnm_style_get_format (style);
	n = GPOINTER_TO_UINT (g_hash_table_lookup (state->format_hash, (gpointer)fmt));
	gsf_output_printf (state->output, ";P%d", n);

	n = GPOINTER_TO_UINT (g_hash_table_lookup (state->font_hash, style));
	gsf_output_printf (state->output, ";SM%d", n + 1);

	if (gnm_style_get_font_bold (style))
		gsf_output_printf (state->output, ";SD");
	if (gnm_style_get_font_italic (style))
		gsf_output_printf (state->output, ";SI");
	if (gnm_style_get_pattern (style) == 5)
		gsf_output_printf (state->output, ";SS");
	if (sylk_get_border (style, MSTYLE_BORDER_TOP))
		gsf_output_printf (state->output, ";ST");
	if (sylk_get_border (style, MSTYLE_BORDER_BOTTOM))
		gsf_output_printf (state->output, ";SB");
	if (sylk_get_border (style, MSTYLE_BORDER_LEFT))
		gsf_output_printf (state->output, ";SL");
	if (sylk_get_border (style, MSTYLE_BORDER_RIGHT))
		gsf_output_printf (state->output, ";SR");

	// Line not terminated
}

static void
sylk_write_pos (SylkWriter *state, int col, int row)
{
	if (row != state->cur_row) {
		state->cur_row = row;
		gsf_output_printf (state->output, ";Y%d", row + 1);
	}
	gsf_output_printf (state->output, ";X%d\r\n", col + 1);
}

static GnmValue *
cb_sylk_write_cell_style (GnmCellIter const *iter, SylkWriter *state)
{
	GnmStyle const *style = sheet_style_get (state->sheet, iter->pp.eval.col, iter->pp.eval.row);

	sylk_write_style (state, style);
	sylk_write_pos (state, iter->pp.eval.col, iter->pp.eval.row);

	return NULL;
}


static void
cb_sylk_collect_styles (GnmStyle const *st, SylkWriter *state)
{
	GOFormat const *fmt;

	fmt = gnm_style_get_format (st);
	if (!g_hash_table_lookup_extended (state->format_hash, fmt, NULL, NULL)) {
		unsigned n = state->formats->len;
		g_hash_table_insert (state->format_hash, (gpointer)fmt, GUINT_TO_POINTER (n));
		g_ptr_array_add (state->formats, (gpointer)fmt);
	}

	if (!g_hash_table_lookup_extended (state->font_hash, st, NULL, NULL)) {
		unsigned n = state->fonts->len;
		g_hash_table_insert (state->font_hash, (gpointer)st, GUINT_TO_POINTER (n));
		g_ptr_array_add (state->fonts, (gpointer)st);
	}

}

static void
cb_sylk_collect_cell_styles (G_GNUC_UNUSED gpointer unused,
			     GnmCell *cell, SylkWriter *state)
{
}

static void
sylk_write_sheet (SylkWriter *state)
{
	Sheet *sheet = state->sheet;
	GnmRange extent;
	unsigned ui;
	GnmRange whole_sheet;
	GPtrArray *col_defs;
	ColRowInfo const *cr_def;
	int col, row;

	/* collect style and font info */
	range_init_full_sheet (&whole_sheet, sheet);
	extent = sheet_get_extent (sheet, FALSE, TRUE);
	col_defs = sheet_style_most_common (sheet, TRUE);
	sheet_style_get_nondefault_extent (sheet, &extent, &whole_sheet, col_defs);

	sheet_style_foreach (sheet,
			     (GFunc)cb_sylk_collect_styles, state);
	sheet_cell_foreach (sheet,
			    (GHFunc)cb_sylk_collect_cell_styles, state);

	for (ui = 0; ui < state->formats->len; ui++) {
		GOFormat const *fmt = g_ptr_array_index (state->formats, ui);
		gsf_output_printf (state->output, "P;P%s\r\n",
				   go_format_as_XL (fmt));
	}
	for (ui = 0; ui < state->fonts->len; ui++) {
		GnmStyle const *s = g_ptr_array_index (state->fonts, ui);
		gsf_output_printf (state->output, "P;E%s;M%d\r\n",
				   gnm_style_get_font_name (s),
				   (int)(gnm_style_get_font_size (s) * 20 + 0.5));
	}

	// Column styles.
	for (col = extent.start.col; col <= extent.end.col; col++) {
		sylk_write_style (state, g_ptr_array_index (col_defs, col));
		gsf_output_printf (state->output, ";C%d\r\n", col + 1);
	}

	// Cell styles
	state->cur_row = -1;
	sheet_foreach_cell_in_range (sheet, 0, &extent,
		(CellIterFunc) cb_sylk_write_cell_style, state);

	// Column widths
	cr_def = sheet_colrow_get_default (sheet, TRUE);
	for (col = extent.start.col; col <= extent.end.col; col++) {
		ColRowInfo *cr = sheet_col_get (sheet, col);
		if (!cr || cr->size_pts == cr_def->size_pts)
			continue;
		gsf_output_printf (state->output, "F;W%d %d %d\r\n",
				   col + 1, col + 1, (int)(cr->size_pts / 7.45 + 0.5));
	}

	// Row heights
	cr_def = sheet_colrow_get_default (sheet, FALSE);
	for (row = extent.start.row; row <= extent.end.row; row++) {
		ColRowInfo *cr = sheet_row_get (sheet, row);
		if (!cr || cr->size_pts == cr_def->size_pts)
			continue;
		gsf_output_printf (state->output, "F;M%d;R%d\r\n",
				   (int)(cr->size_pts * 20 + 0.5),
				   row + 1);
	}

/* Global Formatting */
	/* F;P0;DG0G10;SM0;Z;M280;N3 10 */

/* Bounds */
	gsf_output_printf (state->output, "B;Y%d;X%d;D0 0 %d %d\r\n",
		extent.end.row + 1,	extent.end.col + 1,
		extent.end.row,		extent.end.col);

/* Global options */
	gsf_output_printf (state->output, "O;%c%d %" GNM_FORMAT_f,
		(state->wb->iteration.enabled ? 'A' : 'G'),
		state->wb->iteration.max_number,
		state->wb->iteration.tolerance);
	if (!sheet->convs->r1c1_addresses)
		gsf_output_puts (state->output, ";L");
	if (!state->wb->recalc_auto)
		gsf_output_puts (state->output, ";M");
	gsf_output_printf (state->output, ";V%d",
		workbook_date_conv (state->wb)->use_1904 ? 4 : 0);
	if (sheet->hide_zero)
		gsf_output_puts (state->output, ";Z");
	gsf_output_write (state->output, 2, "\r\n");

/* dump content */
	state->cur_row = -1;
	sheet_foreach_cell_in_range (sheet, CELL_ITER_IGNORE_BLANK, &extent,
				     (CellIterFunc) cb_sylk_write_cell, state);

	g_ptr_array_free (col_defs, TRUE);
}

static GnmConventions *
sylk_conventions_new (void)
{
	GnmConventions *res = gnm_conventions_new ();

	res->range_sep_colon		= TRUE;
	res->r1c1_addresses		= TRUE;
	res->input.range_ref		= rangeref_parse;
	res->output.translated		= FALSE;
	res->output.string		= sylk_output_string;

	return res;
}

G_MODULE_EXPORT void
sylk_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		gconstpointer wb_view, GsfOutput *output);
void
sylk_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		gconstpointer wb_view, GsfOutput *output)
{
	GnmLocale *locale;
	SylkWriter state;

	state.wb     = wb_view_get_workbook (wb_view);
	state.sheet  = wb_view_cur_sheet (wb_view);
	state.output = output;
	state.convs  = sylk_conventions_new ();

	state.formats = g_ptr_array_new ();
	state.format_hash = g_hash_table_new (g_direct_hash, g_direct_equal);

	state.fonts = g_ptr_array_new ();
	state.font_hash = g_hash_table_new (font_hash, font_equal);

	locale = gnm_push_C_locale ();
	gsf_output_puts (output, "ID;PGnumeric;N;E\r\n");

	sylk_write_sheet (&state);

	gsf_output_puts (output, "E\r\n");
	gnm_pop_C_locale (locale);
	gnm_conventions_unref (state.convs);

	g_hash_table_destroy (state.font_hash);
	g_ptr_array_free (state.fonts, TRUE);

	g_hash_table_destroy (state.format_hash);
	g_ptr_array_free (state.formats, TRUE);
}
