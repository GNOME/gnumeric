#ifndef _GNM_CELLSPAN_H_
# define _GNM_CELLSPAN_H_

#include <gnumeric.h>

G_BEGIN_DECLS

typedef struct {
	GnmCell const *cell;
	int   left, right;
} CellSpanInfo;

void cell_calc_span (GnmCell const *cell, int *col1, int *col2);

/* Management routines for spans */
void cell_register_span    (GnmCell const *cell, int left, int right);
void cell_unregister_span  (GnmCell const *cell);

GType               cell_span_info_get_type (void);
CellSpanInfo const *row_span_get     (ColRowInfo const *ri, int col);
void		    row_destroy_span (ColRowInfo *ri);
void		    row_calc_spans   (ColRowInfo *ri, int row, Sheet const *sheet);

G_END_DECLS

#endif /* _GNM_CELLSPAN_H_ */
