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
#include <tools/analysis-tools.h>
#include <numbers.h>
#include <gnm-datetime.h>

#include <mathfunc.h>
#include <tools/fill-series.h>
#include <goffice/goffice.h>

static void
do_row_filling_wday (GnmFillSeriesTool *ftool, data_analysis_output_t *dao)
{
	int i;
	gnm_float start = ftool->start_value;
	GDate        date;
	GODateConventions const *conv =
		sheet_date_conv (dao->sheet);


	for (i = 0; i < ftool->n; i++) {
		int steps = gnm_round (i * ftool->step_value);
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
do_column_filling_wday (GnmFillSeriesTool *ftool, data_analysis_output_t *dao)
{
	int i;
	gnm_float start = ftool->start_value;
	GDate        date;
	GODateConventions const *conv =
		sheet_date_conv (dao->sheet);


	for (i = 0; i < ftool->n; i++) {
		int steps = gnm_round (i * ftool->step_value);
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
do_row_filling_month (GnmFillSeriesTool *ftool, data_analysis_output_t *dao)
{
	int i;
	gnm_float start = ftool->start_value;
	GDate        date;
	GODateConventions const *conv =
		sheet_date_conv (dao->sheet);


	for (i = 0; i < ftool->n; i++) {
		go_date_serial_to_g (&date, start, conv);
		gnm_date_add_months (&date, i * ftool->step_value);

		dao_set_cell_float (dao, i, 0,
				    go_date_g_to_serial (&date, conv));
	}
}

static void
do_column_filling_month (GnmFillSeriesTool *ftool, data_analysis_output_t *dao)
{
	int i;
	gnm_float start = ftool->start_value;
	GDate        date;
	GODateConventions const *conv =
		sheet_date_conv (dao->sheet);


	for (i = 0; i < ftool->n; i++) {
		go_date_serial_to_g (&date, start, conv);
		gnm_date_add_months (&date, i * ftool->step_value);

		dao_set_cell_float (dao, 0, i,
				    go_date_g_to_serial (&date, conv));
	}
}

static void
do_row_filling_year (GnmFillSeriesTool *ftool, data_analysis_output_t *dao)
{
	int i;
	gnm_float start = ftool->start_value;
	GDate        date;
	GODateConventions const *conv =
		sheet_date_conv (dao->sheet);


	for (i = 0; i < ftool->n; i++) {
		go_date_serial_to_g (&date, start, conv);
		gnm_date_add_years (&date, i * ftool->step_value);

		dao_set_cell_float (dao, i, 0,
				    go_date_g_to_serial (&date, conv));
	}
}

static void
do_column_filling_year (GnmFillSeriesTool *ftool, data_analysis_output_t *dao)
{
	int i;
	gnm_float start = ftool->start_value;
	GDate        date;
	GODateConventions const *conv =
		sheet_date_conv (dao->sheet);


	for (i = 0; i < ftool->n; i++) {
		go_date_serial_to_g (&date, start, conv);
		gnm_date_add_years (&date, i * ftool->step_value);

		dao_set_cell_float (dao, 0, i,
				    go_date_g_to_serial (&date, conv));
	}
}

static void
do_row_filling_linear (GnmFillSeriesTool *ftool, data_analysis_output_t *dao)
{
	int i;
	gnm_float start = ftool->start_value;
	gnm_float step = ftool->step_value;

	for (i = 0; i < ftool->n; i++) {
		dao_set_cell_float (dao, i, 0, start);
		start += step;
	}
}

static void
do_column_filling_linear (GnmFillSeriesTool *ftool, data_analysis_output_t *dao)
{
	int i;
	gnm_float start = ftool->start_value;
	gnm_float step = ftool->step_value;

	for (i = 0; i < ftool->n; i++) {
		dao_set_cell_float (dao, 0, i, start);
		start += step;
	}
}

static void
do_row_filling_growth (GnmFillSeriesTool *ftool, data_analysis_output_t *dao)
{
	int i;
	gnm_float start = ftool->start_value;
	gnm_float step = ftool->step_value;

	for (i = 0; i < ftool->n; i++) {
		dao_set_cell_float (dao, i, 0, start);
		start *= step;
	}
}

static void
do_column_filling_growth (GnmFillSeriesTool *ftool, data_analysis_output_t *dao)
{
	int i;
	gnm_float start = ftool->start_value;
	gnm_float step = ftool->step_value;

	for (i = 0; i < ftool->n; i++) {
		dao_set_cell_float (dao, 0, i, start);
		start *= step;
	}
}

static void
fill_series_adjust_variables (GnmFillSeriesTool *ftool, data_analysis_output_t *dao)
{
	int length_of_series = -1;
	int length_of_space = ftool->series_in_rows
		? dao->cols : dao->rows;

	if (ftool->type == FillSeriesTypeDate &&
	    ftool->date_unit != FillSeriesUnitDay) {
		if (ftool->is_step_set)
			ftool->step_value = gnm_round (ftool->step_value);
		else    /* FIXME */
			ftool->step_value = 1;
		if (ftool->is_stop_set) {
			GDate        from_date, to_date;
			GODateConventions const *conv =
				sheet_date_conv (dao->sheet);

			if (ftool->step_value < 0) {
				go_date_serial_to_g (&from_date,
						      ftool->stop_value, conv);
				go_date_serial_to_g (&to_date,
						      ftool->start_value, conv);
			} else {
				go_date_serial_to_g (&from_date,
						      ftool->start_value, conv);
				go_date_serial_to_g (&to_date,
						      ftool->stop_value, conv);
			}
			switch (ftool->date_unit) {
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

				from_year = g_date_get_year (&from_date);
				to_year = g_date_get_year (&to_date);
				from_month = g_date_get_month (&from_date);
				to_month = g_date_get_month (&to_date);
				g_date_set_year (&to_date, from_year);

				if (g_date_compare (&from_date, &to_date) > 0)
					months = (to_year - from_year) * 12 +
						(to_month - from_month);
				else
					months = (to_year - from_year) * 12 +
						(to_month - from_month) + 1;
				length_of_series = months
					/ (int)gnm_round (ftool->step_value);
				if (length_of_series < 1)
					length_of_series = 1;
			}
			break;
			case FillSeriesUnitYear:
			{
				GDateYear    from_year, to_year;
				gint years;

				from_year = g_date_get_year (&from_date);
				to_year = g_date_get_year (&to_date);
				g_date_set_year (&to_date, from_year);
				if (g_date_compare (&from_date, &to_date) > 0)
					years = to_year - from_year;
				else
					years = to_year - from_year + 1;
				length_of_series = years
					/ (int)gnm_round (ftool->step_value);
				if (length_of_series < 1)
					length_of_series = 1;
			}
			break;
			}

		}
	} else {
		if (!ftool->is_step_set) {
			switch (ftool->type) {
			case FillSeriesTypeDate:
			case FillSeriesTypeLinear:
				ftool->step_value =
					(ftool->stop_value - ftool->start_value)/
					(length_of_space - 1);
				break;
			case FillSeriesTypeGrowth:
				ftool->step_value =
					gnm_exp ((gnm_log(ftool->stop_value
							  /ftool->start_value))/
						 (length_of_space - 1));
				break;
			}
			ftool->is_step_set = TRUE;
		} else if (ftool->is_stop_set) {
			switch (ftool->type) {
			case FillSeriesTypeDate:
			case FillSeriesTypeLinear:
				length_of_series
					= gnm_floor(GNM_EPSILON + 1 +
						    (ftool->stop_value
						     - ftool->start_value)/
						    ftool->step_value);
				if (length_of_series < 1)
					length_of_series = 1;
				break;
			case FillSeriesTypeGrowth:
				length_of_series
					= gnm_floor(GNM_EPSILON + 1 +
						    (gnm_log(ftool->stop_value
							     /ftool->start_value))/
						    gnm_log(ftool->step_value));
				if (length_of_series < 1)
					length_of_series = 1;
				break;
			}
		}
	}
	if (ftool->series_in_rows) {
		dao_adjust (dao, length_of_series, 1);
		ftool->n = dao->cols;
	} else {
		dao_adjust (dao, 1, length_of_series);
		ftool->n = dao->rows;
	}
	if (length_of_series > 0)
		ftool->n = length_of_series;
}

G_DEFINE_TYPE (GnmFillSeriesTool, gnm_fill_series_tool, GNM_TYPE_ANALYSIS_TOOL)

static void
gnm_fill_series_tool_init (G_GNUC_UNUSED GnmFillSeriesTool *tool)
{
}

static gboolean
gnm_fill_series_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmFillSeriesTool *ftool = GNM_FILL_SERIES_TOOL (tool);
	fill_series_adjust_variables (ftool, dao);
	return FALSE;
}

static char *
gnm_fill_series_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Fill Series (%s)"));
}

static gboolean
gnm_fill_series_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("Fill Series"));
	return FALSE;
}

static gboolean
gnm_fill_series_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("Fill Series"));
}

static gboolean
gnm_fill_series_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmFillSeriesTool *ftool = GNM_FILL_SERIES_TOOL (tool);

	switch (ftool->type) {
	case FillSeriesTypeLinear:
		if (ftool->series_in_rows)
			do_row_filling_linear (ftool, dao);
		else
			do_column_filling_linear (ftool, dao);
		break;
	case FillSeriesTypeGrowth:
		if (ftool->series_in_rows)
			do_row_filling_growth (ftool, dao);
		else
			do_column_filling_growth (ftool, dao);
		break;
	case FillSeriesTypeDate:
		switch (ftool->date_unit) {
		case FillSeriesUnitDay:
			if (ftool->series_in_rows)
				do_row_filling_linear (ftool, dao);
			else
				do_column_filling_linear (ftool, dao);
			break;
		case FillSeriesUnitWeekday:
			if (ftool->series_in_rows)
				do_row_filling_wday (ftool, dao);
			else
				do_column_filling_wday (ftool, dao);
			break;
		case FillSeriesUnitMonth:
			if (ftool->series_in_rows)
				do_row_filling_month (ftool, dao);
			else
				do_column_filling_month (ftool, dao);
			break;
		case FillSeriesUnitYear:
			if (ftool->series_in_rows)
				do_row_filling_year (ftool, dao);
			else
				do_column_filling_year (ftool, dao);
			break;
		}
		dao_set_date (dao, 0, 0,
			      dao->cols - 1, dao->rows -1);
		break;
	}
	return FALSE;
}

static void
gnm_fill_series_tool_class_init (GnmFillSeriesToolClass *klass)
{
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	at_class->update_dao = gnm_fill_series_tool_update_dao;
	at_class->update_descriptor = gnm_fill_series_tool_update_descriptor;
	at_class->prepare_output_range = gnm_fill_series_tool_prepare_output_range;
	at_class->format_output_range = gnm_fill_series_tool_format_output_range;
	at_class->perform_calc = gnm_fill_series_tool_perform_calc;
}

GnmAnalysisTool *
gnm_fill_series_tool_new (void)
{
	return g_object_new (GNM_TYPE_FILL_SERIES_TOOL, NULL);
}
