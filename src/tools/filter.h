#ifndef __FILTER_H__
#define __FILTER_H__

#define OK                 0
#define N_COLUMNS_ERROR    1
#define ERR_INVALID_FIELD  2
#define NO_RECORDS_FOUND   3


gint advanced_filter (WorkbookControl        *wbc,
		      data_analysis_output_t *dao,
		      Value                  *database, Value *criteria,
		      gboolean               unique_only_flag);

void filter_show_all (Sheet *sheet);

#endif
