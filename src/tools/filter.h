#ifndef __FILTER_H__
#define __FILTER_H__

#include <gnumeric.h>
#include <tools/dao.h>

gint advanced_filter (WorkbookControl        *wbc,
		      data_analysis_output_t *dao,
		      GnmValue               *database, GnmValue *criteria,
		      gboolean                unique_only_flag);

void filter_show_all (WorkbookControl *wbc);

#endif
