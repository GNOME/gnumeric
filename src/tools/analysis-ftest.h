/*
 * analysis-ftest.h:
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


#ifndef GNM_TOOLS_ANALYSIS_FTEST_H_
#define GNM_TOOLS_ANALYSIS_FTEST_H_

#include <gnumeric.h>
#include <numbers.h>
#include <tools/analysis-tools.h>

#define GNM_TYPE_FTEST_TOOL (gnm_ftest_tool_get_type ())
GType gnm_ftest_tool_get_type (void);
typedef struct _GnmFTestTool GnmFTestTool;
typedef struct _GnmFTestToolClass GnmFTestToolClass;

struct _GnmFTestTool {
	GnmGenericBAnalysisTool parent;
};

struct _GnmFTestToolClass {
	GnmGenericBAnalysisToolClass parent_class;
};

#define GNM_FTEST_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_FTEST_TOOL, GnmFTestTool))
#define GNM_IS_FTEST_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_FTEST_TOOL))

GnmAnalysisTool *gnm_ftest_tool_new (void);

#endif
