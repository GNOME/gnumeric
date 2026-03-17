/*
 * analysis-ztest.h:
 *
 * Author:
 *   Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2008 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 * (C) Copyright 2026 by Morten Welinder <terra@gnome.org>
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

#ifndef GNM_TOOLS_ANALYSIS_ZTEST_H_
#define GNM_TOOLS_ANALYSIS_ZTEST_H_

#include <gnumeric.h>
#include <numbers.h>
#include <tools/analysis-tools.h>

/*********************** ZTest ************************/

#define GNM_TYPE_ZTEST_TOOL (gnm_ztest_tool_get_type ())
GType gnm_ztest_tool_get_type (void);
typedef struct _GnmZTestTool GnmZTestTool;
typedef struct _GnmZTestToolClass GnmZTestToolClass;

struct _GnmZTestTool {
	GnmGenericBAnalysisTool parent;
	gnm_float mean_diff;
	gnm_float var1;
	gnm_float var2;
};

struct _GnmZTestToolClass {
	GnmGenericBAnalysisToolClass parent_class;
};

#define GNM_ZTEST_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_ZTEST_TOOL, GnmZTestTool))
#define GNM_IS_ZTEST_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_ZTEST_TOOL))

GnmAnalysisTool *gnm_ztest_tool_new (void);

#endif
