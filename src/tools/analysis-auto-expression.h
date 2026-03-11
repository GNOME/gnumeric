/*
 * analysis-auto-expression.h:
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


#ifndef GNM_TOOLS_ANALYSIS_AUTO_EXPRESSION_H_
#define GNM_TOOLS_ANALYSIS_AUTO_EXPRESSION_H_

#include <gnumeric.h>
#include <numbers.h>
#include <tools/analysis-tools.h>
#include <sheet.h>

#define GNM_TYPE_AUTO_EXPRESSION_TOOL (gnm_auto_expression_tool_get_type ())
GType gnm_auto_expression_tool_get_type (void);
typedef struct _GnmAutoExpressionTool GnmAutoExpressionTool;
typedef struct _GnmAutoExpressionToolClass GnmAutoExpressionToolClass;

struct _GnmAutoExpressionTool {
	GnmGenericAnalysisTool parent;
	gboolean multiple;
	gboolean below;
	GnmFunc *func;
};

struct _GnmAutoExpressionToolClass {
	GnmGenericAnalysisToolClass parent_class;
};

#define GNM_AUTO_EXPRESSION_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_AUTO_EXPRESSION_TOOL, GnmAutoExpressionTool))
#define GNM_IS_AUTO_EXPRESSION_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_AUTO_EXPRESSION_TOOL))

GnmAnalysisTool *gnm_auto_expression_tool_new (void);

#endif
