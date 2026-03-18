#ifndef GNM_TOOLS_FILTER_H_
#define GNM_TOOLS_FILTER_H_

#include <gnumeric.h>
#include <tools/analysis-tools.h>

/****************  Advanced Filter  ********************/

#define GNM_TYPE_ADVANCED_FILTER_TOOL (gnm_advanced_filter_tool_get_type ())
GType gnm_advanced_filter_tool_get_type (void);
typedef struct _GnmAdvancedFilterTool GnmAdvancedFilterTool;
typedef struct _GnmAdvancedFilterToolClass GnmAdvancedFilterToolClass;

struct _GnmAdvancedFilterTool {
	GnmGenericBAnalysisTool parent;
	gboolean   unique_only_flag;
};

struct _GnmAdvancedFilterToolClass {
	GnmGenericBAnalysisToolClass parent_class;
};

#define GNM_ADVANCED_FILTER_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_ADVANCED_FILTER_TOOL, GnmAdvancedFilterTool))
#define GNM_IS_ADVANCED_FILTER_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_ADVANCED_FILTER_TOOL))

GnmAnalysisTool *gnm_advanced_filter_tool_new (void);


gint advanced_filter (WorkbookControl        *wbc,
		      data_analysis_output_t *dao,
		      GnmValue               *database, GnmValue *criteria,
		      gboolean                unique_only_flag);

void filter_show_all (WorkbookControl *wbc);

#endif
