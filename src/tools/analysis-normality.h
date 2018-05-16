/*
 * analysis-normality.h:
 *
 * Author:
 *   Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2009 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
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


#ifndef ANALYSIS_NORMALITY_H
#define ANALYSIS_NORMALITY_H

#include <gnumeric.h>
#include <numbers.h>
#include <tools/dao.h>
#include <tools/tools.h>
#include <tools/analysis-tools.h>

/****************  Normality  Tests ***************/

typedef enum {
	normality_test_type_andersondarling = 0,
	normality_test_type_cramervonmises,
	normality_test_type_lilliefors,
	normality_test_type_shapirofrancia
} normality_test_type_t;

typedef struct {
	analysis_tools_data_generic_t base;
	gnm_float alpha;
	normality_test_type_t type;
	gboolean graph;
} analysis_tools_data_normality_t;

gboolean analysis_tool_normality_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
					   analysis_tool_engine_t selector, gpointer result);


#endif
