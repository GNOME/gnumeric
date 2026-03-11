/*
 * analysis-one-mean-test.h:
 *
 * Author:
 *   Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2012 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
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


#ifndef GNM_TOOLS_ANALYSIS_ONE_MEAN_TEST_H_
#define GNM_TOOLS_ANALYSIS_ONE_MEAN_TEST_H_

#include <gnumeric.h>
#include <numbers.h>
#include <tools/analysis-tools.h>
#include <sheet.h>

#define GNM_TYPE_ONE_MEAN_TEST_TOOL (gnm_one_mean_test_tool_get_type ())
GType gnm_one_mean_test_tool_get_type (void);
typedef struct _GnmOneMeanTestTool GnmOneMeanTestTool;
typedef struct _GnmOneMeanTestToolClass GnmOneMeanTestToolClass;

struct _GnmOneMeanTestTool {
	GnmGenericAnalysisTool parent;
	gnm_float mean;
	gnm_float alpha;
};

struct _GnmOneMeanTestToolClass {
	GnmGenericAnalysisToolClass parent_class;
};

#define GNM_ONE_MEAN_TEST_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_ONE_MEAN_TEST_TOOL, GnmOneMeanTestTool))
#define GNM_IS_ONE_MEAN_TEST_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_ONE_MEAN_TEST_TOOL))

GnmAnalysisTool *gnm_one_mean_test_tool_new (void);

#endif
