/*
 * analysis-ttest.h:
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

#ifndef GNM_TOOLS_ANALYSIS_TTEST_H_
#define GNM_TOOLS_ANALYSIS_TTEST_H_

#include <gnumeric.h>
#include <numbers.h>
#include <tools/analysis-tools.h>

/*********************** TTest paired *****************/

#define GNM_TYPE_TTEST_PAIRED_TOOL (gnm_ttest_paired_tool_get_type ())
GType gnm_ttest_paired_tool_get_type (void);
typedef struct _GnmTTestPairedTool GnmTTestPairedTool;
typedef struct _GnmTTestPairedToolClass GnmTTestPairedToolClass;

struct _GnmTTestPairedTool {
	GnmGenericBAnalysisTool parent;
	gnm_float mean_diff;
	gnm_float var1;
	gnm_float var2;
};

struct _GnmTTestPairedToolClass {
	GnmGenericBAnalysisToolClass parent_class;
};

#define GNM_TTEST_PAIRED_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_TTEST_PAIRED_TOOL, GnmTTestPairedTool))
#define GNM_IS_TTEST_PAIRED_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_TTEST_PAIRED_TOOL))

GnmAnalysisTool *gnm_ttest_paired_tool_new (void);


/*********************** TTest equal variances *********/

#define GNM_TYPE_TTEST_EQVAR_TOOL (gnm_ttest_eqvar_tool_get_type ())
GType gnm_ttest_eqvar_tool_get_type (void);
typedef struct _GnmTTestEqVarTool GnmTTestEqVarTool;
typedef struct _GnmTTestEqVarToolClass GnmTTestEqVarToolClass;

struct _GnmTTestEqVarTool {
	GnmGenericBAnalysisTool parent;
	gnm_float mean_diff;
};

struct _GnmTTestEqVarToolClass {
	GnmGenericBAnalysisToolClass parent_class;
};

#define GNM_TTEST_EQVAR_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_TTEST_EQVAR_TOOL, GnmTTestEqVarTool))
#define GNM_IS_TTEST_EQVAR_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_TTEST_EQVAR_TOOL))

GnmAnalysisTool *gnm_ttest_eqvar_tool_new (void);


/*********************** TTest unequal variances *******/

#define GNM_TYPE_TTEST_NEQVAR_TOOL (gnm_ttest_neqvar_tool_get_type ())
GType gnm_ttest_neqvar_tool_get_type (void);
typedef struct _GnmTTestNeqVarTool GnmTTestNeqVarTool;
typedef struct _GnmTTestNeqVarToolClass GnmTTestNeqVarToolClass;

struct _GnmTTestNeqVarTool {
	GnmGenericBAnalysisTool parent;
	gnm_float mean_diff;
};

struct _GnmTTestNeqVarToolClass {
	GnmGenericBAnalysisToolClass parent_class;
};

#define GNM_TTEST_NEQVAR_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_TTEST_NEQVAR_TOOL, GnmTTestNeqVarTool))
#define GNM_IS_TTEST_NEQVAR_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_TTEST_NEQVAR_TOOL))

GnmAnalysisTool *gnm_ttest_neqvar_tool_new (void);


#endif
