#ifndef GNM_TOOLS_FILL_SERIES_H_
#define GNM_TOOLS_FILL_SERIES_H_

#include <gnumeric.h>


typedef enum {
        GNM_FILL_SERIES_LINEAR, GNM_FILL_SERIES_GROWTH, GNM_FILL_SERIES_DATE
} gnm_fill_series_type_t;

GType fill_series_type_get_type (void);
#define GNM_FILL_SERIES_TYPE (fill_series_type_get_type ())

typedef enum {
        GNM_FILL_SERIES_UNIT_DAY, GNM_FILL_SERIES_UNIT_WEEKDAY, GNM_FILL_SERIES_UNIT_MONTH,
	GNM_FILL_SERIES_UNIT_YEAR
} gnm_fill_series_date_unit_t;

GType fill_series_date_unit_get_type (void);
#define GNM_FILL_SERIES_DATE_UNIT (fill_series_date_unit_get_type ())

#define GNM_TYPE_FILL_SERIES_TOOL (gnm_fill_series_tool_get_type ())
GType gnm_fill_series_tool_get_type (void);
typedef struct _GnmFillSeriesTool GnmFillSeriesTool;
typedef struct _GnmFillSeriesToolClass GnmFillSeriesToolClass;

struct _GnmFillSeriesTool {
	GnmAnalysisTool parent;

        gnm_fill_series_type_t      type;
        gnm_fill_series_date_unit_t date_unit;
        gboolean                series_in_rows;
        gnm_float               step_value;
        gnm_float               stop_value;
        gnm_float               start_value;
        gboolean                is_step_set;
        gboolean                is_stop_set;

	// Derived
	gint                    n;
};

struct _GnmFillSeriesToolClass {
	GnmAnalysisToolClass parent_class;
};

#define GNM_FILL_SERIES_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_FILL_SERIES_TOOL, GnmFillSeriesTool))
#define GNM_IS_FILL_SERIES_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_FILL_SERIES_TOOL))

GnmAnalysisTool *gnm_fill_series_tool_new (void);

#endif
