#ifndef GNUMERIC_EVAL_H
#define GNUMERIC_EVAL_H

#include "gnumeric.h"

DependencyData *dependency_data_new      (void);

/* Workbook */
void            workbook_deps_destroy    (Workbook *wb);

/* Sheets */
void            sheet_deps_destroy        (Sheet *sheet);
void            sheet_recalc_dependencies (Sheet *sheet);
#if 0
/* Write this it will be useful */
void		sheet_region_recalc_deps  (const Sheet *sheet, Range r);
#endif
GList          *sheet_region_get_deps     (const Sheet *sheet, Range r);

/* Convenience routines for Cells */
void            cell_eval                 (Cell *cell);
GList          *cell_get_dependencies     (const Cell *cell);
#if 0
/* Write this it will be useful */
void            cell_recalc_dependencies  (const Cell *cell);
#endif
void            cell_add_dependencies     (Cell *cell);
void            cell_drop_dependencies    (Cell *cell);

/* Convenience routines for Dependents */
void            dependent_add_dependencies     (Dependent *dep, const CellPos *pos);
void            dependent_drop_dependencies    (Dependent *dep, const CellPos *pos);

/* Debug */
void            sheet_dump_dependencies   (const Sheet *sheet);

#endif /* GNUMERIC_EVAL_H */
