#ifndef __FILL_SERIES_H__
#define __FILL_SERIES_H__

#include <gnumeric.h>
#include <dao.h>


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
        gboolean                is_step_set;
        gboolean                is_stop_set;

	Range const             *sel;
        gnm_float               v0;
} fill_series_t;


void fill_series (WorkbookControl        *wbc,
		  data_analysis_output_t *dao,
		  Sheet                  *sheet,
		  fill_series_t          *fs);

#endif
