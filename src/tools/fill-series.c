/*
 * fill-series.c: Fill according to a linear or exponential serie.
 *
 * Authors:
 *        Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *        Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2003 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 * (C) Copyright 2003 by Andreas J. Guelzow  <aguelzow@taliesin.ca>
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>

#include <sheet.h>
#include <sheet-filter.h>
#include <cell.h>
#include <ranges.h>
#include <value.h>
#include <gnm-format.h>
#include <workbook.h>
#include <tools/tools.h>
#include <numbers.h>
#include <gnm-datetime.h>

#include <mathfunc.h>
#include <tools/fill-series.h>
#include <goffice/goffice.h>

static void
do_row_filling_wday (data_analysis_output_t *dao, fill_series_t *info)
{
	int i;
	gnm_float start = info->start_value;
	GDate        date;
	GODateConventions const *conv =
		sheet_date_conv (dao->sheet);


	for (i = 0; i < info->n; i++) {
		int steps = gnm_round (i * info->step_value);
		int days = (steps / 5) * 7 + steps % 5;
		GDateWeekday wd;

		go_date_serial_to_g (&date, start, conv);
		wd = g_date_get_weekday (&date);
		if (wd + (steps % 5) > G_DATE_FRIDAY)
				days += 2;
		gnm_date_add_days (&date, days);

		dao_set_cell_float (dao, i, 0,
				    go_date_g_to_serial (&date, conv));
	}

}

static void
do_column_filling_wday (data_analysis_output_t *dao, fill_series_t *info)
{
	int i;
	gnm_float start = info->start_value;
	GDate        date;
	GODateConventions const *conv =
		sheet_date_conv (dao->sheet);


	for (i = 0; i < info->n; i++) {
		int steps = gnm_round (i * info->step_value);
		int days = (steps / 5) * 7 + steps % 5;
		GDateWeekday wd;

		go_date_serial_to_g (&date, start, conv);
		wd = g_date_get_weekday (&date);
		if (wd + (steps % 5) > G_DATE_FRIDAY)
				days += 2;
		gnm_date_add_days (&date, days);

		dao_set_cell_float (dao, 0,i,
				    go_date_g_to_serial (&date, conv));
	}


}

static void
do_row_filling_month (data_analysis_output_t *dao, fill_series_t *info)
{
	int i;
	gnm_float start = info->start_value;
	GDate        date;
	GODateConventions const *conv =
		sheet_date_conv (dao->sheet);


	for (i = 0; i < info->n; i++) {
		go_date_serial_to_g (&date, start, conv);
		gnm_date_add_months (&date, i * info->step_value);

		dao_set_cell_float (dao, i, 0,
				    go_date_g_to_serial (&date, conv));
	}
}

static void
do_column_filling_month (data_analysis_output_t *dao, fill_series_t *info)
{
	int i;
	gnm_float start = info->start_value;
	GDate        date;
	GODateConventions const *conv =
		sheet_date_conv (dao->sheet);


	for (i = 0; i < info->n; i++) {
		go_date_serial_to_g (&date, start, conv);
		gnm_date_add_months (&date, i * info->step_value);

		dao_set_cell_float (dao, 0, i,
				    go_date_g_to_serial (&date, conv));
	}
}

static void
do_row_filling_year (data_analysis_output_t *dao, fill_series_t *info)
{
	int i;
	gnm_float start = info->start_value;
	GDate        date;
	GODateConventions const *conv =
		sheet_date_conv (dao->sheet);


	for (i = 0; i < info->n; i++) {
		go_date_serial_to_g (&date, start, conv);
		gnm_date_add_years (&date, i * info->step_value);

		dao_set_cell_float (dao, i, 0,
				    go_date_g_to_serial (&date, conv));
	}
}

static void
do_column_filling_year (data_analysis_output_t *dao, fill_series_t *info)
{
	int i;
	gnm_float start = info->start_value;
	GDate        date;
	GODateConventions const *conv =
		sheet_date_conv (dao->sheet);


	for (i = 0; i < info->n; i++) {
		go_date_serial_to_g (&date, start, conv);
		gnm_date_add_years (&date, i * info->step_value);

		dao_set_cell_float (dao, 0, i,
				    go_date_g_to_serial (&date, conv));
	}
}

static void
do_row_filling_linear (data_analysis_output_t *dao, fill_series_t *info)
{
	int i;
	gnm_float start = info->start_value;
	gnm_float step = info->step_value;

	for (i = 0; i < info->n; i++) {
		dao_set_cell_float (dao, i, 0, start);
		start += step;
	}
}

static void
do_column_filling_linear (data_analysis_output_t *dao, fill_series_t *info)
{
	int i;
	gnm_float start = info->start_value;
	gnm_float step = info->step_value;

	for (i = 0; i < info->n; i++) {
		dao_set_cell_float (dao, 0, i, start);
		start += step;
	}
}

static void
do_row_filling_growth (data_analysis_output_t *dao, fill_series_t *info)
{
	int i;
	gnm_float start = info->start_value;
	gnm_float step = info->step_value;

	for (i = 0; i < info->n; i++) {
		dao_set_cell_float (dao, i, 0, start);
		start *= step;
	}
}

static void
do_column_filling_growth (data_analysis_output_t *dao, fill_series_t *info)
{
	int i;
	gnm_float start = info->start_value;
	gnm_float step = info->step_value;

	for (i = 0; i < info->n; i++) {
		dao_set_cell_float (dao, 0, i, start);
		start *= step;
	}
}

static void
fill_series_adjust_variables (data_analysis_output_t *dao, fill_series_t *info)
{
	int length_of_series = -1;
	int length_of_space = info->series_in_rows
		? dao->cols : dao->rows;

	if (info->type == FillSeriesTypeDate &&
	    info->date_unit != FillSeriesUnitDay) {
		if (info->is_step_set)
			info->step_value = gnm_round (info->step_value);
		else    /* FIXME */
			info->step_value = 1;
		if (info->is_stop_set) {
			GDate        from_date, to_date;
			GODateConventions const *conv =
				sheet_date_conv (dao->sheet);

			if (info->step_value < 0) {
				go_date_serial_to_g (&from_date,
						      info->stop_value, conv);
				go_date_serial_to_g (&to_date,
						      info->start_value, conv);
			} else {
				go_date_serial_to_g (&from_date,
						      info->start_value, conv);
				go_date_serial_to_g (&to_date,
						      info->stop_value, conv);
			}
			switch (info->date_unit) {
			case FillSeriesUnitDay:
				/* This should not happen*/
				break;
			case FillSeriesUnitWeekday:
			{
				int days;
				days = g_date_days_between
					(&from_date, &to_date);
				length_of_series = (days / 7) * 5 + 1
					+ (days % 7);
				if (length_of_series < 1)
					length_of_series = 1;
			}
			break;
			case FillSeriesUnitMonth:
			{
				GDateYear    from_year, to_year;
				GDateMonth    from_month, to_month;
				gint months;

				from_year = g_date_get_year(&from_date);
				to_year = g_date_get_year(&to_date);
				from_month = g_date_get_month(&from_date);
				to_month = g_date_get_month(&to_date);
				g_date_set_year (&to_date, from_year);

				if (g_date_compare (&from_date, &to_date) > 0)
					months = (to_year - from_year) * 12 +
						(to_month - from_month);
				else
					months = (to_year - from_year) * 12 +
						(to_month - from_month) + 1;
				length_of_series = months
					/ (int)gnm_round(info->step_value);
				if (length_of_series < 1)
					length_of_series = 1;
			}
			break;
			case FillSeriesUnitYear:
			{
				GDateYear    from_year, to_year;
				gint years;

				from_year = g_date_get_year(&from_date);
				to_year = g_date_get_year(&to_date);
				g_date_set_year (&to_date, from_year);
				if (g_date_compare (&from_date, &to_date) > 0)
					years = to_year - from_year;
				else
					years = to_year - from_year + 1;
				length_of_series = years
					/ (int)gnm_round(info->step_value);
				if (length_of_series < 1)
					length_of_series = 1;
			}
			break;
			}

		}
	} else {
		if (!info->is_step_set) {
			switch (info->type) {
			case FillSeriesTypeDate:
			case FillSeriesTypeLinear:
				info->step_value =
					(info->stop_value - info->start_value)/
					(length_of_space - 1);
				break;
			case FillSeriesTypeGrowth:
				info->step_value =
					gnm_exp((gnm_log(info->stop_value
							 /info->start_value))/
						(length_of_space - 1));
				break;
			}
			info->is_step_set = TRUE;
		} else if (info->is_stop_set) {
			switch (info->type) {
			case FillSeriesTypeDate:
			case FillSeriesTypeLinear:
				length_of_series
					= gnm_floor(GNM_EPSILON + 1 +
						    (info->stop_value
						     - info->start_value)/
						    info->step_value);
				if (length_of_series < 1)
					length_of_series = 1;
				break;
			case FillSeriesTypeGrowth:
				length_of_series
					= gnm_floor(GNM_EPSILON + 1 +
						    (gnm_log(info->stop_value
							     /info->start_value))/
						    gnm_log(info->step_value));
				if (length_of_series < 1)
					length_of_series = 1;
				break;
			}
		}
	}
	if (info->series_in_rows) {
		dao_adjust (dao, length_of_series, 1);
		info->n = dao->cols;
	} else {
		dao_adjust (dao, 1, length_of_series);
		info->n = dao->rows;
	}
	if (length_of_series > 0)
		info->n = length_of_series;
}

gboolean fill_series_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
			     analysis_tool_engine_t selector, gpointer result)
{
	fill_series_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Fill Series (%s)"),
						result) == NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		fill_series_adjust_variables (dao, info);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return FALSE;
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Fill Series"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Fill Series"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		switch (info->type) {
		case FillSeriesTypeLinear:
			if (info->series_in_rows)
				do_row_filling_linear (dao, info);
			else
				do_column_filling_linear (dao, info);
			break;
		case FillSeriesTypeGrowth:
			if (info->series_in_rows)
				do_row_filling_growth (dao, info);
			else
				do_column_filling_growth (dao, info);
			break;
		case FillSeriesTypeDate:
			switch (info->date_unit) {
			case FillSeriesUnitDay:
				if (info->series_in_rows)
					do_row_filling_linear (dao, info);
				else
					do_column_filling_linear (dao, info);
				break;
			case FillSeriesUnitWeekday:
				if (info->series_in_rows)
					do_row_filling_wday (dao, info);
				else
					do_column_filling_wday (dao, info);
				break;
			case FillSeriesUnitMonth:
				if (info->series_in_rows)
					do_row_filling_month (dao, info);
				else
					do_column_filling_month (dao, info);
				break;
			case FillSeriesUnitYear:
				if (info->series_in_rows)
					do_row_filling_year (dao, info);
				else
					do_column_filling_year (dao, info);
				break;
			}
			dao_set_date (dao, 0, 0,
				      dao->cols - 1, dao->rows -1);
			break;
		}
		return FALSE;
	}
	return TRUE;  /* We shouldn't get here */
}
