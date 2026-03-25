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
#include <workbook-control.h>

#include <mathfunc.h>
#include <tools/fill-series.h>
#include <goffice/goffice.h>

static void
do_filling_wday (GnmFillSeriesTool *ftool, data_analysis_output_t *dao, int dc, int dr)
{
	int i;
	gnm_float start = ftool->start_value;
	GDate date;
	GODateConventions const *conv = dao_get_date_conv (dao);
	int c = 0, r = 0;

	for (i = 0; i < ftool->n; i++) {
		int steps = gnm_round (i * ftool->step_value);
		int days = (steps / 5) * 7 + steps % 5;
		GDateWeekday wd;

		go_date_serial_to_g (&date, start, conv);
		wd = g_date_get_weekday (&date);
		if (wd + (steps % 5) > G_DATE_FRIDAY)
				days += 2;
		gnm_date_add_days (&date, days);

		dao_set_cell_float (dao, c, r,
				    go_date_g_to_serial (&date, conv));
		c += dc;
		r += dr;
	}
}

static void
do_filling_month (GnmFillSeriesTool *ftool, data_analysis_output_t *dao, int dc, int dr)
{
	int i;
	gnm_float start = ftool->start_value;
	GDate date;
	GODateConventions const *conv = dao_get_date_conv (dao);
	int c = 0, r = 0;

	for (i = 0; i < ftool->n; i++) {
		go_date_serial_to_g (&date, start, conv);
		gnm_date_add_months (&date, i * ftool->step_value);

		dao_set_cell_float (dao, c, r,
				    go_date_g_to_serial (&date, conv));
		c += dc;
		r += dr;
	}
}

static void
do_filling_year (GnmFillSeriesTool *ftool, data_analysis_output_t *dao, int dc, int dr)
{
	int i;
	gnm_float start = ftool->start_value;
	GDate date;
	GODateConventions const *conv = dao_get_date_conv (dao);
	int c = 0, r = 0;

	for (i = 0; i < ftool->n; i++) {
		go_date_serial_to_g (&date, start, conv);
		gnm_date_add_years (&date, i * ftool->step_value);

		dao_set_cell_float (dao, c, r,
				    go_date_g_to_serial (&date, conv));
		c += dc;
		r += dr;
	}
}

static void
do_filling_linear (GnmFillSeriesTool *ftool, data_analysis_output_t *dao, int dc, int dr)
{
	int i;
	gnm_float start = ftool->start_value;
	gnm_float step = ftool->step_value;
	int c = 0, r = 0;

	for (i = 0; i < ftool->n; i++) {
		dao_set_cell_float (dao, c, r, start);
		c += dc;
		r += dr;
		start += step;
	}
}

static void
do_filling_growth (GnmFillSeriesTool *ftool, data_analysis_output_t *dao, int dc, int dr)
{
	int i;
	gnm_float start = ftool->start_value;
	gnm_float step = ftool->step_value;
	int c = 0, r = 0;

	for (i = 0; i < ftool->n; i++) {
		dao_set_cell_float (dao, c, r, start);
		c += dc;
		r += dr;
		start *= step;
	}
}

static void
fill_series_adjust_variables (GnmFillSeriesTool *ftool, data_analysis_output_t *dao)
{
	int length_of_series = -1;
	int length_of_space = ftool->series_in_rows
		? dao->cols : dao->rows;

	if (ftool->type == GNM_FILL_SERIES_DATE &&
	    ftool->date_unit != GNM_FILL_SERIES_UNIT_DAY) {
		GODateConventions const *conv = dao_get_date_conv (dao);
		if (ftool->is_step_set)
			ftool->step_value = gnm_round (ftool->step_value);
		else    /* FIXME */
			ftool->step_value = 1;
		if (ftool->is_stop_set) {
			GDate        from_date, to_date;

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
			case GNM_FILL_SERIES_UNIT_DAY:
				/* This should not happen*/
				break;
			case GNM_FILL_SERIES_UNIT_WEEKDAY:
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
			case GNM_FILL_SERIES_UNIT_MONTH:
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
			case GNM_FILL_SERIES_UNIT_YEAR:
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
			case GNM_FILL_SERIES_DATE:
			case GNM_FILL_SERIES_LINEAR:
				ftool->step_value =
					(ftool->stop_value - ftool->start_value)/
					(length_of_space - 1);
				break;
			case GNM_FILL_SERIES_GROWTH:
				ftool->step_value =
					gnm_exp ((gnm_log(ftool->stop_value
							  /ftool->start_value))/
						 (length_of_space - 1));
				break;
			}
			ftool->is_step_set = TRUE;
		} else if (ftool->is_stop_set) {
			switch (ftool->type) {
			case GNM_FILL_SERIES_DATE:
			case GNM_FILL_SERIES_LINEAR:
				length_of_series
					= gnm_floor(GNM_EPSILON + 1 +
						    (ftool->stop_value
						     - ftool->start_value)/
						    ftool->step_value);
				if (length_of_series < 1)
					length_of_series = 1;
				break;
			case GNM_FILL_SERIES_GROWTH:
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

GType
fill_series_type_get_type (void)
{
	static GType etype = 0;
	if (etype == 0) {
		static GEnumValue const values[] = {
			{ GNM_FILL_SERIES_LINEAR, "GNM_FILL_SERIES_LINEAR", "linear" },
			{ GNM_FILL_SERIES_GROWTH, "GNM_FILL_SERIES_GROWTH", "growth" },
			{ GNM_FILL_SERIES_DATE,   "GNM_FILL_SERIES_DATE",   "date" },
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("gnm_fill_series_type_t", values);
	}
	return etype;
}

GType
fill_series_date_unit_get_type (void)
{
	static GType etype = 0;
	if (etype == 0) {
		static GEnumValue const values[] = {
			{ GNM_FILL_SERIES_UNIT_DAY,     "GNM_FILL_SERIES_UNIT_DAY",     "day" },
			{ GNM_FILL_SERIES_UNIT_WEEKDAY, "GNM_FILL_SERIES_UNIT_WEEKDAY", "weekday" },
			{ GNM_FILL_SERIES_UNIT_MONTH,   "GNM_FILL_SERIES_UNIT_MONTH",   "month" },
			{ GNM_FILL_SERIES_UNIT_YEAR,    "GNM_FILL_SERIES_UNIT_YEAR",    "year" },
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("gnm_fill_series_date_unit_t", values);
	}
	return etype;
}

G_DEFINE_TYPE (GnmFillSeriesTool, gnm_fill_series_tool, GNM_ANALYSIS_TOOL_TYPE)

enum {
	FILL_SERIES_PROP_0,
	FILL_SERIES_PROP_TYPE,
	FILL_SERIES_PROP_DATE_UNIT,
	FILL_SERIES_PROP_SERIES_IN_ROWS,
	FILL_SERIES_PROP_STEP_VALUE,
	FILL_SERIES_PROP_STOP_VALUE,
	FILL_SERIES_PROP_START_VALUE,
	FILL_SERIES_PROP_IS_STEP_SET,
	FILL_SERIES_PROP_IS_STOP_SET
};

static void
gnm_fill_series_tool_set_property (GObject      *obj,
				   guint         property_id,
				   GValue const *value,
				   GParamSpec   *pspec)
{
	GnmFillSeriesTool *tool = GNM_FILL_SERIES_TOOL (obj);

	switch (property_id) {
	case FILL_SERIES_PROP_TYPE:
		tool->type = g_value_get_enum (value);
		break;
	case FILL_SERIES_PROP_DATE_UNIT:
		tool->date_unit = g_value_get_enum (value);
		break;
	case FILL_SERIES_PROP_SERIES_IN_ROWS:
		tool->series_in_rows = g_value_get_boolean (value);
		break;
	case FILL_SERIES_PROP_STEP_VALUE:
		tool->step_value = g_value_get_double (value);
		tool->is_step_set = TRUE;
		break;
	case FILL_SERIES_PROP_STOP_VALUE:
		tool->stop_value = g_value_get_double (value);
		tool->is_stop_set = TRUE;
		break;
	case FILL_SERIES_PROP_START_VALUE:
		tool->start_value = g_value_get_double (value);
		break;
	case FILL_SERIES_PROP_IS_STEP_SET:
		tool->is_step_set = g_value_get_boolean (value);
		break;
	case FILL_SERIES_PROP_IS_STOP_SET:
		tool->is_stop_set = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
		break;
	}
}

static void
gnm_fill_series_tool_get_property (GObject    *obj,
				   guint       property_id,
				   GValue     *value,
				   GParamSpec *pspec)
{
	GnmFillSeriesTool *tool = GNM_FILL_SERIES_TOOL (obj);

	switch (property_id) {
	case FILL_SERIES_PROP_TYPE:
		g_value_set_enum (value, tool->type);
		break;
	case FILL_SERIES_PROP_DATE_UNIT:
		g_value_set_enum (value, tool->date_unit);
		break;
	case FILL_SERIES_PROP_SERIES_IN_ROWS:
		g_value_set_boolean (value, tool->series_in_rows);
		break;
	case FILL_SERIES_PROP_STEP_VALUE:
		g_value_set_double (value, tool->step_value);
		break;
	case FILL_SERIES_PROP_STOP_VALUE:
		g_value_set_double (value, tool->stop_value);
		break;
	case FILL_SERIES_PROP_START_VALUE:
		g_value_set_double (value, tool->start_value);
		break;
	case FILL_SERIES_PROP_IS_STEP_SET:
		g_value_set_boolean (value, tool->is_step_set);
		break;
	case FILL_SERIES_PROP_IS_STOP_SET:
		g_value_set_boolean (value, tool->is_stop_set);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
		break;
	}
}

static void
gnm_fill_series_tool_init (GnmFillSeriesTool *tool)
{
	tool->type = GNM_FILL_SERIES_LINEAR;
	tool->date_unit = GNM_FILL_SERIES_UNIT_DAY;
	tool->series_in_rows = FALSE;
	tool->step_value = 1.0;
	tool->stop_value = 0.0;
	tool->start_value = 0.0;
	tool->is_step_set = FALSE;
	tool->is_stop_set = FALSE;
	tool->n = 0;
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
gnm_fill_series_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	dao_prepare_output (wbc, dao, _("Fill Series"));
	return FALSE;
}

static gboolean
gnm_fill_series_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	return dao_format_output (wbc, dao, _("Fill Series"));
}

static gboolean
gnm_fill_series_tool_perform_calc (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	GnmFillSeriesTool *ftool = GNM_FILL_SERIES_TOOL (tool);
	int dc = ftool->series_in_rows ? 1 : 0;
	int dr = 1 - dc;

	switch (ftool->type) {
	case GNM_FILL_SERIES_LINEAR:
		do_filling_linear (ftool, dao, dc, dr);
		break;
	case GNM_FILL_SERIES_GROWTH:
		do_filling_growth (ftool, dao, dc, dr);
		break;
	case GNM_FILL_SERIES_DATE:
		switch (ftool->date_unit) {
		case GNM_FILL_SERIES_UNIT_DAY:
			do_filling_linear (ftool, dao, dc, dr);
			break;
		case GNM_FILL_SERIES_UNIT_WEEKDAY:
			do_filling_wday (ftool, dao, dc, dr);
			break;
		case GNM_FILL_SERIES_UNIT_MONTH:
			do_filling_month (ftool, dao, dc, dr);
			break;
		case GNM_FILL_SERIES_UNIT_YEAR:
			do_filling_year (ftool, dao, dc, dr);
			break;
		}
		dao_set_format_date (dao, 0, 0,
			      dao->cols - 1, dao->rows -1);
		break;
	}
	return FALSE;
}

static void
gnm_fill_series_tool_class_init (GnmFillSeriesToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->set_property = gnm_fill_series_tool_set_property;
	gobject_class->get_property = gnm_fill_series_tool_get_property;

	at_class->update_dao = gnm_fill_series_tool_update_dao;
	at_class->update_descriptor = gnm_fill_series_tool_update_descriptor;
	at_class->prepare_output_range = gnm_fill_series_tool_prepare_output_range;
	at_class->format_output_range = gnm_fill_series_tool_format_output_range;
	at_class->perform_calc = gnm_fill_series_tool_perform_calc;

	g_object_class_install_property (gobject_class,
		FILL_SERIES_PROP_TYPE,
		g_param_spec_enum ("type", NULL, NULL,
				   GNM_FILL_SERIES_TYPE, GNM_FILL_SERIES_LINEAR,
				   G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		FILL_SERIES_PROP_DATE_UNIT,
		g_param_spec_enum ("date-unit", NULL, NULL,
				   GNM_FILL_SERIES_DATE_UNIT, GNM_FILL_SERIES_UNIT_DAY,
				   G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		FILL_SERIES_PROP_SERIES_IN_ROWS,
		g_param_spec_boolean ("series-in-rows", NULL, NULL,
				      FALSE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		FILL_SERIES_PROP_STEP_VALUE,
		g_param_spec_double ("step-value", NULL, NULL,
				     -G_MAXDOUBLE, G_MAXDOUBLE, 1.0, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		FILL_SERIES_PROP_STOP_VALUE,
		g_param_spec_double ("stop-value", NULL, NULL,
				     -G_MAXDOUBLE, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		FILL_SERIES_PROP_START_VALUE,
		g_param_spec_double ("start-value", NULL, NULL,
				     -G_MAXDOUBLE, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		FILL_SERIES_PROP_IS_STEP_SET,
		g_param_spec_boolean ("is-step-set", NULL, NULL,
				      FALSE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		FILL_SERIES_PROP_IS_STOP_SET,
		g_param_spec_boolean ("is-stop-set", NULL, NULL,
				      FALSE, G_PARAM_READWRITE));
}

GnmAnalysisTool *
gnm_fill_series_tool_new (void)
{
	return g_object_new (GNM_TYPE_FILL_SERIES_TOOL, NULL);
}
