#ifndef __FILL_SERIES_H__
#define __FILL_SERIES_H__

#include <gnumeric.h>
#include <tools/dao.h>


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

gboolean fill_series_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
			     analysis_tool_engine_t selector, gpointer result);

#endif
