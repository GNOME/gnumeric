/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * fill-series.c: Fill according to a linear or exponential serie.
 *
 * Author:
 *        Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 2003 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <sheet.h>
#include <sheet-filter.h>
#include <cell.h>
#include <ranges.h>
#include <gui-util.h>
#include <tool-dialogs.h>
#include <dao-gui-utils.h>
#include <value.h>
#include <workbook-edit.h>
#include <format.h>
#include <workbook.h>

#include <glade/glade.h>
#include <widgets/gnumeric-expr-entry.h>
#include "mathfunc.h"
#include "fill-series.h"
#include "dao.h"


static void
iterate (fill_series_t *fs, Cell *cell, gnm_float *v, Sheet *sheet,
	 StyleFormat *style_format)
{
	Value *value;

	value = value_new_float (*v);
	sheet_cell_set_value (cell, value);

	if (fs->type == FillSeriesTypeLinear)
		*v += fs->step_value;
	else if (fs->type == FillSeriesTypeGrowth)
		*v *= fs->step_value;
	else {
		GDate        date;
		GDateWeekday wd;
		int          step = fs->step_value;

		GnmDateConventions const *conv =
			workbook_date_conv (sheet->workbook);


		value_set_fmt (value, style_format);
		
		datetime_serial_to_g (&date, value_get_as_int (value), conv);

		switch (fs->date_unit) {
		case FillSeriesUnitDay :
			*v += fs->step_value; break;
		case FillSeriesUnitWeekday :
			wd = g_date_get_weekday (&date);
			v += (step / 5) * 7;
			*v += step % 5;
			if (wd + (step % 5) > G_DATE_FRIDAY)
				*v += 2;
			break;
		case FillSeriesUnitMonth :
			g_date_add_months (&date, fs->step_value);
			*v = datetime_g_to_serial (&date, conv);
			break;
		case FillSeriesUnitYear :
			g_date_add_years (&date, fs->step_value);
			*v = datetime_g_to_serial (&date, conv);
			break;
		default:
			/* We should not get here. */
			break;
		}
	}
}

static void
do_row_filling (Sheet *sheet, fill_series_t *fs)
{
	int         col;
	gnm_float   v;
	gboolean    is_range;
	Cell        *cell;

	static StyleFormat *style_format = NULL;

	if (style_format == NULL)
		style_format = style_format_default_date ();

	is_range = fs->sel->start.col != fs->sel->end.col;
	col = fs->sel->start.col;
 
	for (v = fs->v0; ; col++ ) {
		if (fs->is_stop_set && v > fs->stop_value)
			break;
		if (is_range && col > fs->sel->end.col)
			break;

		cell = sheet_cell_fetch (sheet, col, fs->sel->start.row);
		iterate (fs, cell, &v, sheet, style_format);
	}
}

static void
do_column_filling (Sheet *sheet, fill_series_t *fs)
{
	int       row;
	gnm_float v;
	gboolean  is_range;
	Cell      *cell;

	static StyleFormat *style_format = NULL;

	if (style_format == NULL)
		style_format = style_format_default_date ();

	is_range = fs->sel->start.row != fs->sel->end.row;
	row = fs->sel->start.row;
 
	for (v = fs->v0; ; row++ ) {
		if (fs->is_stop_set && v > fs->stop_value)
			break;
		if (is_range && row > fs->sel->end.row)
			break;

		cell = sheet_cell_fetch (sheet, fs->sel->start.col, row);
		iterate (fs, cell, &v, sheet, style_format);
	}
}

void
fill_series (WorkbookControl        *wbc,
	     data_analysis_output_t *dao,
	     Sheet                  *sheet,
	     fill_series_t          *fs)
{
	Cell *cell;

	cell = sheet_cell_get (sheet, fs->sel->start.col, fs->sel->start.row);
	if (cell == NULL || ! VALUE_IS_NUMBER (cell->value))
		return;
	fs->v0 = value_get_as_float (cell->value);

	if (fs->series_in_rows)
		do_row_filling (sheet, fs);
	else
		do_column_filling (sheet, fs);

	sheet_redraw_all (sheet, TRUE);
}
