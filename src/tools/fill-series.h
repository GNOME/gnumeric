#ifndef GNM_TOOLS_FILL_SERIES_H_
#define GNM_TOOLS_FILL_SERIES_H_

#include <gnumeric.h>


typedef enum {
        FillSeriesTypeLinear, FillSeriesTypeGrowth, FillSeriesTypeDate
} fill_series_type_t;

typedef enum {
        FillSeriesUnitDay, FillSeriesUnitWeekday, FillSeriesUnitMonth,
	FillSeriesUnitYear
} fill_series_date_unit_t;

typedef struct {
        fill_series_type_t      type;
        fill_series_date_unit_t date_unit;
        gboolean                series_in_rows;
        gnm_float               step_value;
        gnm_float               stop_value;
        gnm_float               start_value;
        gboolean                is_step_set;
        gboolean                is_stop_set;

	gint                    n;
} fill_series_t;

#define GNM_TYPE_FILL_SERIES_TOOL (gnm_fill_series_tool_get_type ())
GType gnm_fill_series_tool_get_type (void);
typedef struct _GnmFillSeriesTool GnmFillSeriesTool;
typedef struct _GnmFillSeriesToolClass GnmFillSeriesToolClass;

struct _GnmFillSeriesTool {
	GnmAnalysisTool parent;
	fill_series_t data;
};

struct _GnmFillSeriesToolClass {
	GnmAnalysisToolClass parent_class;
};

#define GNM_FILL_SERIES_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_FILL_SERIES_TOOL, GnmFillSeriesTool))
#define GNM_IS_FILL_SERIES_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_FILL_SERIES_TOOL))

GnmAnalysisTool *gnm_fill_series_tool_new (void);

#endif
