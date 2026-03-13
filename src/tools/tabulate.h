#ifndef GNM_TABULATE_H_
#define GNM_TABULATE_H_

#include <gnumeric.h>
#include <numbers.h>

struct _GnmTabulateInfo {
	GnmCell   *target;
	int dims;
	GnmCell  **cells;
	gnm_float *minima;
	gnm_float *maxima;
	gnm_float *steps;
	gboolean with_coordinates;
};

GSList *
gnm_tabulate (WorkbookControl *wbc,
	       GnmTabulateInfo *data);

GnmTabulateInfo *gnm_tabulate_info_new (int dims);
void             gnm_tabulate_info_free (GnmTabulateInfo *info);

#endif
