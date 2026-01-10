#ifndef GNM_TOOLS_FILTER_H_
#define GNM_TOOLS_FILTER_H_

#include <gnumeric.h>
#include <tools/dao.h>

gint advanced_filter (WorkbookControl        *wbc,
		      data_analysis_output_t *dao,
		      GnmValue               *database, GnmValue *criteria,
		      gboolean                unique_only_flag);

void filter_show_all (WorkbookControl *wbc);

#endif
