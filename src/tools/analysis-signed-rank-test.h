/*
 * analysis-signed-rank-test.h:
 *
 * Author:
 *   Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2010 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
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


#ifndef GNM_TOOLS_ANALYSIS_SIGNED_RANK_TEST_H_
#define GNM_TOOLS_ANALYSIS_SIGNED_RANK_TEST_H_

#include <gnumeric.h>
#include <numbers.h>
#include <tools/analysis-tools.h>
#include <tools/analysis-sign-test.h>

#define GNM_TYPE_SIGNED_RANK_TEST_TOOL (gnm_signed_rank_test_tool_get_type ())
GType gnm_signed_rank_test_tool_get_type (void);
typedef struct _GnmSignedRankTestTool GnmSignedRankTestTool;
typedef struct _GnmSignedRankTestToolClass GnmSignedRankTestToolClass;

struct _GnmSignedRankTestTool {
	GnmGenericAnalysisTool parent;
	gnm_float median;
	gnm_float alpha;
};

struct _GnmSignedRankTestToolClass {
	GnmGenericAnalysisToolClass parent_class;
};

#define GNM_SIGNED_RANK_TEST_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_SIGNED_RANK_TEST_TOOL, GnmSignedRankTestTool))
#define GNM_IS_SIGNED_RANK_TEST_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_SIGNED_RANK_TEST_TOOL))

GnmAnalysisTool *gnm_signed_rank_test_tool_new (void);



#define GNM_TYPE_SIGNED_RANK_TEST_TWO_TOOL (gnm_signed_rank_test_two_tool_get_type ())
GType gnm_signed_rank_test_two_tool_get_type (void);
typedef struct _GnmSignedRankTestTwoTool GnmSignedRankTestTwoTool;
typedef struct _GnmSignedRankTestTwoToolClass GnmSignedRankTestTwoToolClass;

struct _GnmSignedRankTestTwoTool {
	GnmGenericBAnalysisTool parent;
	gnm_float median;
};

struct _GnmSignedRankTestTwoToolClass {
	GnmGenericBAnalysisToolClass parent_class;
};

#define GNM_SIGNED_RANK_TEST_TWO_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_SIGNED_RANK_TEST_TWO_TOOL, GnmSignedRankTestTwoTool))
#define GNM_IS_SIGNED_RANK_TEST_TWO_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_SIGNED_RANK_TEST_TWO_TOOL))

GnmAnalysisTool *gnm_signed_rank_test_two_tool_new (void);

#endif
