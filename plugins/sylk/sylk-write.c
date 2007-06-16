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
#include "value.h"
#include "cell.h"
#include "gutils.h"

#include <goffice/app/io-context.h>

#include <gsf/gsf-output.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>

typedef struct {
	GsfOutput *output;

	GnmConventions const *convs;

	Workbook *wb;
	Sheet	 *sheet;

	int cur_row;
} SylkWriter;
 
static void
sylk_write_str (SylkWriter *state, char const *str)
{
}

static GnmValue *
cb_sylk_write_cell (GnmCellIter const *iter, SylkWriter *state)
{
	if (iter->pp.eval.row != state->cur_row)
		gsf_output_printf (state->output, "C;X%d;Y%d",
			iter->pp.eval.col + 1,
			(state->cur_row = iter->pp.eval.row) + 1);
	else
		gsf_output_printf (state->output, "C;X%d",
			iter->pp.eval.col + 1);

	gsf_output_puts (state->output, ";K\"foo\"");
	gsf_output_write (state->output, 1, "\n");
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
	 * 2.1) ?? 	fonts   P;F....
	 * 2.2) indexed fonts   P;E....
	 * 3) global formats F;
	 */

/* Global Formating */
	/* F;P0;DG0G10;SM0;Z;M280;N3 10 */

/* Bounds */
	gsf_output_printf (state->output, "B;Y%d;X%d;D0 0 %d %d\n",
		extent.end.row + 1,	extent.end.col + 1,
		extent.end.row,		extent.end.col);

/* Global options */
	gsf_output_printf (state->output, "O;%c%d %f",
		(state->wb->iteration.enabled ? 'A' : 'G'),
		state->wb->iteration.max_number,
		state->wb->iteration.tolerance);
	if (!state->sheet->r1c1_addresses)
		gsf_output_puts (state->output, ";L");
	if (!state->wb->recalc_auto)
		gsf_output_puts (state->output, ";M");
	gsf_output_printf (state->output, ";V%d",
		workbook_date_conv (state->wb)->use_1904 ? 4 : 0);
	if (state->sheet->hide_zero)
		gsf_output_puts (state->output, ";Z");
	gsf_output_write (state->output, 1, "\n");

/* dump content */
	state->cur_row = -1;
	sheet_foreach_cell_in_range (state->sheet, CELL_ITER_IGNORE_BLANK,
		extent.start.col, extent.start.row,
		extent.end.col,   extent.end.row,
		(CellIterFunc) cb_sylk_write_cell, state);
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
	if (NULL == state.sheet) {
		gnumeric_io_error_string (io_context, _("Cannot get default sheet."));
		return;
	}

	locale = gnm_push_C_locale ();
	gsf_output_puts (output, "ID;PGnumeric;N;E\n");
	sylk_write_sheet (&state);
	gsf_output_puts (output, "E\n");
	gnm_pop_C_locale (locale);
}
