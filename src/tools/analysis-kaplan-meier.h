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


#ifndef GNM_TOOLS_ANALYSIS_KAPLAN_MEIER_H_
#define GNM_TOOLS_ANALYSIS_KAPLAN_MEIER_H_

#include <gnumeric.h>
#include <numbers.h>
#include <tools/analysis-tools.h>

#define GNM_TYPE_KAPLAN_MEIER_TOOL (gnm_kaplan_meier_tool_get_type ())
GType gnm_kaplan_meier_tool_get_type (void);
typedef struct _GnmKaplanMeierTool GnmKaplanMeierTool;
typedef struct _GnmKaplanMeierToolClass GnmKaplanMeierToolClass;

struct _GnmKaplanMeierTool {
	GnmGenericBAnalysisTool parent;
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
};

struct _GnmKaplanMeierToolClass {
	GnmGenericBAnalysisToolClass parent_class;
};

#define GNM_KAPLAN_MEIER_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_KAPLAN_MEIER_TOOL, GnmKaplanMeierTool))
#define GNM_IS_KAPLAN_MEIER_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_KAPLAN_MEIER_TOOL))

GnmAnalysisTool *gnm_kaplan_meier_tool_new (void);

typedef struct {
	char *name;
	guint group_from;
	guint group_to;
} analysis_tools_kaplan_meier_group_t;

#endif
