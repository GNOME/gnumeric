/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * sylk-write.c : export sylk
 *
 * Copyright (C) 2007 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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

#include "workbook-priv.h"
#include "workbook-view.h"
#include "sheet.h"
#include "sheet-style.h"
#include "expr.h"
#include "expr-impl.h"
#include "value.h"
#include "cell.h"
#include "gutils.h"
#include "str.h"
#include "parse-util.h"

#include <goffice/app/io-context.h>

#include <gsf/gsf-output.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>

typedef struct {
	GsfOutput *output;

	GnmConventions *convs;

	Workbook *wb;
	Sheet	 *sheet;

	int cur_row;
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
#warning handle the magic ascii escaping
	}
	gsf_output_write (state->output, p - str, str);
}

static void
sylk_output_string (GnmConventionsOut *out, GnmString const *string)
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
	GnmExprArrayCorner const *array;

	if (iter->pp.eval.row != state->cur_row)
		gsf_output_printf (state->output, "C;Y%d;X%d",
			(state->cur_row = iter->pp.eval.row) + 1,
			iter->pp.eval.col + 1);
	else
		gsf_output_printf (state->output, "C;X%d",
			iter->pp.eval.col + 1);

	if (NULL != (v = iter->cell->value)) {
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

	if (NULL != (texpr = iter->cell->base.texpr)) {
		if (NULL != (array = gnm_expr_top_get_array_corner (texpr))) {
			gsf_output_printf (state->output, ";R%d;C%d;M",
				iter->pp.eval.row + array->rows,
				iter->pp.eval.col + array->cols);
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

static void
cb_sylk_collect_styles (GnmStyle const *st,
			G_GNUC_UNUSED gconstpointer dummy,
			SylkWriter *state)
{
}

static void
cb_sylk_collect_cell_styles (G_GNUC_UNUSED gpointer unused,
			     GnmCell *cell, SylkWriter *state)
{
}

static void
sylk_write_sheet (SylkWriter *state)
{
	GnmRange extent;

/* collect style and font info */
	extent = sheet_get_extent (state->sheet, FALSE);
	sheet_style_foreach (state->sheet,
		(GHFunc) cb_sylk_collect_styles, state);
	sheet_cell_foreach (state->sheet,
		(GHFunc) cb_sylk_collect_cell_styles, state);

	/*
	 * 1) formats P;P.....
	 * 2.1) ??	fonts   P;F....
	 * 2.2) indexed fonts   P;E....
	 * 3) global formats F;
	 */

/* Global Formating */
	/* F;P0;DG0G10;SM0;Z;M280;N3 10 */

/* Bounds */
	gsf_output_printf (state->output, "B;Y%d;X%d;D0 0 %d %d\r\n",
		extent.end.row + 1,	extent.end.col + 1,
		extent.end.row,		extent.end.col);

/* Global options */
	gsf_output_printf (state->output, "O;%c%d %f",
		(state->wb->iteration.enabled ? 'A' : 'G'),
		state->wb->iteration.max_number,
		state->wb->iteration.tolerance);
	if (!state->sheet->convs->r1c1_addresses)
		gsf_output_puts (state->output, ";L");
	if (!state->wb->recalc_auto)
		gsf_output_puts (state->output, ";M");
	gsf_output_printf (state->output, ";V%d",
		workbook_date_conv (state->wb)->use_1904 ? 4 : 0);
	if (state->sheet->hide_zero)
		gsf_output_puts (state->output, ";Z");
	gsf_output_write (state->output, 2, "\r\n");

/* dump content */
	state->cur_row = -1;
	sheet_foreach_cell_in_range (state->sheet, CELL_ITER_IGNORE_BLANK,
		extent.start.col, extent.start.row,
		extent.end.col,   extent.end.row,
		(CellIterFunc) cb_sylk_write_cell, state);
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
sylk_file_save (GOFileSaver const *fs, IOContext *io_context,
		gconstpointer wb_view, GsfOutput *output);
void
sylk_file_save (GOFileSaver const *fs, IOContext *io_context,
		gconstpointer wb_view, GsfOutput *output)
{
	GnmLocale *locale;
	SylkWriter state;

	state.wb     = wb_view_get_workbook (wb_view);
	state.sheet  = wb_view_cur_sheet (wb_view);
	state.output = output;
	state.convs  = sylk_conventions_new ();

	if (NULL == state.sheet) {
		gnumeric_io_error_string (io_context, _("Cannot get default sheet."));
		return;
	}

	locale = gnm_push_C_locale ();
	gsf_output_puts (output, "ID;PGnumeric;N;E\r\n");
	sylk_write_sheet (&state);
	gsf_output_puts (output, "E\r\n");
	gnm_pop_C_locale (locale);
	gnm_conventions_free (state.convs);
}
