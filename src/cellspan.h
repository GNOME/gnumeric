#ifndef GNUMERIC_CELLSPAN_H
#define GNUMERIC_CELLSPAN_H

#include "gnumeric.h"

typedef struct {
	Cell const *cell;
	int   left, right;
} CellSpanInfo;

/* Information about cells whose contents span columns */
CellSpanInfo const * row_span_get (ColRowInfo const * const ri, int const col);

void cell_calc_span (Cell const * const cell,
		     int * const col1, int * const col2);

/* Management routines for spans */
void cell_register_span    (Cell const * const cell, int left, int right);
void cell_unregister_span  (Cell const * const cell);
void row_init_span         (ColRowInfo *ri);
void row_destroy_span      (ColRowInfo *ri);

#endif /* GNUMERIC_CELLSPAN_H */
