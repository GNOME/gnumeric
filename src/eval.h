#ifndef GNUMERIC_EVAL_H
#define GNUMERIC_EVAL_H

#include "gnumeric.h"

DependencyData *dependency_data_new          (void);

void            sheet_deps_destroy           (Sheet    *sheet);
void            workbook_deps_destroy        (Workbook *wb);

void            cell_eval                    (Cell *cell);

void            cell_add_dependencies        (Cell *cell);
void            cell_drop_dependencies       (Cell *cell);

void            eval_queue_cell		     (Cell *cell);
void            eval_queue_list		     (GList *list, gboolean freelist);
void            eval_unqueue_cell	     (Cell *cell);
void		eval_unqueue_sheet	     (Sheet *sheet);

/*
 * Return a newly allocated list with Cells inside that
 * depend on the value at Sheet, col, row
 */
GList          *cell_get_dependencies        (const Cell *cell);
GList          *sheet_region_get_deps        (Sheet *sheet,
					      int start_col, int start_row,
					      int end_col, int end_row);
void           sheet_recalc_dependencies     (Sheet *sheet);

/* Very AEsoteric */
void            cell_add_explicit_dependency (Cell *cell, const CellRef *a);
void            cell_eval_content            (Cell *cell);

/* Debug */
void            sheet_dump_dependencies      (const Sheet *sheet);

#endif /* GNUMERIC_EVAL_H */

