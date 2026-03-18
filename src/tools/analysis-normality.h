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


#ifndef GNM_TOOLS_ANALYSIS_NORMALITY_H_
#define GNM_TOOLS_ANALYSIS_NORMALITY_H_

#include <gnumeric.h>
#include <numbers.h>
#include <tools/analysis-tools.h>

/****************  Normality  Tests ***************/

typedef enum {
	GNM_NORMALITY_TEST_TYPE_ANDERSONDARLING = 0,
	GNM_NORMALITY_TEST_TYPE_CRAMERVONMISES,
	GNM_NORMALITY_TEST_TYPE_LILLIEFORS,
	GNM_NORMALITY_TEST_TYPE_SHAPIROFRANCIA
} gnm_normality_test_type_t;

GType gnm_normality_test_type_get_type (void);
#define GNM_NORMALITY_TEST_TYPE (gnm_normality_test_type_get_type ())

#define GNM_TYPE_NORMALITY_TOOL (gnm_normality_tool_get_type ())
GType gnm_normality_tool_get_type (void);
typedef struct _GnmNormalityTool GnmNormalityTool;
typedef struct _GnmNormalityToolClass GnmNormalityToolClass;

struct _GnmNormalityTool {
	GnmGenericAnalysisTool parent;
	gnm_float alpha;
	gnm_normality_test_type_t type;
	gboolean graph;
};

struct _GnmNormalityToolClass {
	GnmGenericAnalysisToolClass parent_class;
};

#define GNM_NORMALITY_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_NORMALITY_TOOL, GnmNormalityTool))
#define GNM_IS_NORMALITY_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_NORMALITY_TOOL))

GnmAnalysisTool *gnm_normality_tool_new (void);


#endif
