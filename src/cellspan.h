#ifndef GNUMERIC_CELLSPAN_H
#define GNUMERIC_CELLSPAN_H

#include "gnumeric.h"

typedef struct {
	Cell const *cell;
	int   left, right;
} CellSpanInfo;

void cell_calc_span (Cell const *cell, int *col1, int *col2);

/* Management routines for spans */
void cell_register_span    (Cell const *cell, int left, int right);
void cell_unregister_span  (Cell const *cell);

CellSpanInfo const *row_span_get     (ColRowInfo const *ri, int col);
void		    row_destroy_span (ColRowInfo *ri);
void	 	    row_calc_spans   (ColRowInfo *ri, Sheet const *sheet);


#endif /* GNUMERIC_CELLSPAN_H */
