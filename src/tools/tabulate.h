#ifndef GNUMERIC_TABULATE_H
#define GNUMERIC_TABULATE_H

#include "gnumeric.h"
#include "numbers.h"
#include "dao.h"
#include "tools.h"

typedef struct {
	Cell *target;
	int dims;
	Cell **cells;
	gnm_float *minima;
	gnm_float *maxima;
	gnm_float *steps;
	gboolean with_coordinates;	
} tabulate_t;

GSList *
do_tabulation (WorkbookControl *wbc,
	       tabulate_t *data);


#endif
