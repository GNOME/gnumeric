/*
 * analysis-frequency.h:
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


#ifndef GNM_TOOLS_ANALYSIS_FREQUENCY_H_
#define GNM_TOOLS_ANALYSIS_FREQUENCY_H_

#include <gnumeric.h>
#include <numbers.h>
#include <tools/analysis-tools.h>

typedef enum {
	GNM_FREQ_TOOL_NO_CHART = 0,
	GNM_FREQ_TOOL_BAR_CHART,
	GNM_FREQ_TOOL_COLUMN_CHART
} gnm_freq_tool_chart_t;

GType gnm_freq_tool_chart_get_type (void);
#define GNM_FREQ_TOOL_CHART_TYPE (gnm_freq_tool_chart_get_type ())


#define GNM_TYPE_FREQUENCY_TOOL (gnm_frequency_tool_get_type ())
GType gnm_frequency_tool_get_type (void);
typedef struct _GnmFrequencyTool GnmFrequencyTool;
typedef struct _GnmFrequencyToolClass GnmFrequencyToolClass;

struct _GnmFrequencyTool {
	GnmGenericAnalysisTool parent;
	gboolean   predetermined;
	GnmValue   *bin;
	gnm_float max;
	gnm_float min;
	gint       n;
	gboolean   percentage;
	gboolean   exact;
	gnm_freq_tool_chart_t chart;
};

struct _GnmFrequencyToolClass {
	GnmGenericAnalysisToolClass parent_class;
};

#define GNM_FREQUENCY_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_FREQUENCY_TOOL, GnmFrequencyTool))
#define GNM_IS_FREQUENCY_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_FREQUENCY_TOOL))

GnmAnalysisTool *gnm_frequency_tool_new (void);

#endif
