/*
 * analysis-kaplan-meier.h:
 *
 * Author:
 *   Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2008 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
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


#ifndef ANALYSIS_KAPLAN_MEIER_H
#define ANALYSIS_KAPLAN_MEIER_H

#include <gnumeric.h>
#include <numbers.h>
#include <tools/dao.h>
#include <tools/tools.h>
#include <tools/analysis-tools.h>

/* typedef struct { */
/*	analysis_tools_error_code_t err; */
/*	WorkbookControl *wbc; */
/*	GnmValue *range_1; */
/*	GnmValue *range_2; */
/*	gboolean   labels; */
/*	gnm_float alpha; */
/* } analysis_tools_data_generic_b_t; */

typedef struct {
	analysis_tools_data_generic_b_t base;
	GnmValue *range_3;
	gboolean censored;
	int censor_mark;
	int censor_mark_to;
	gboolean chart;
	gboolean ticks;
	gboolean std_err;
	gboolean median;
	gboolean logrank_test;
	GSList *group_list;
} analysis_tools_data_kaplan_meier_t;

typedef struct {
	char *name;
	guint group_from;
	guint group_to;
} analysis_tools_kaplan_meier_group_t;


gboolean analysis_tool_kaplan_meier_engine (GOCmdContext *gcc, data_analysis_output_t *dao,
					    gpointer specs,
					   analysis_tool_engine_t selector,
					    gpointer result);

#endif
