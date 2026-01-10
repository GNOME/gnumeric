#ifndef GNM_TABULATE_H_
#define GNM_TABULATE_H_

#include <gnumeric.h>
#include <numbers.h>
#include <tools/dao.h>
#include <tools/tools.h>

typedef struct {
	GnmCell   *target;
	int dims;
	GnmCell  **cells;
	gnm_float *minima;
	gnm_float *maxima;
	gnm_float *steps;
	gboolean with_coordinates;
} GnmTabulateInfo;

GSList *
do_tabulation (WorkbookControl *wbc,
	       GnmTabulateInfo *data);

#endif
