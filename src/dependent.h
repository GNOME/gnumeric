#ifndef GNUMERIC_EVAL_H
#define GNUMERIC_EVAL_H

#include "gnumeric.h"
#include <stdio.h>

struct _Dependent
{
	guint	  flags;
	Sheet	 *sheet;
	ExprTree *expression;
};

typedef struct {
	void (*eval) (Dependent *dep);
	void (*set_expr) (Dependent *dep, ExprTree *expr);
	void (*debug_name) (Dependent const *dep, FILE *out);
} DependentClass;

typedef enum {
	/* Linked into the workbook wide expression list */
	DEPENDENT_IN_EXPR_LIST     = 0x00001000,
	DEPENDENT_NEEDS_RECALC	   = 0x00002000,
	DEPENDENT_IN_RECALC_QUEUE  = 0x00004000,
	DEPENDENT_BEING_CALCULATED = 0x00008000,

	/* Types */
	DEPENDENT_CELL 		  = 0x00000001,
	DEPENDENT_TYPE_MASK	  = 0x00000fff,
} DependentFlags;

typedef void (*DepFunc) (Dependent *dep, gpointer user);

guint32 dependent_type_register  (DependentClass const *klass);
void dependent_types_init	 (void);
void dependent_types_shutdown	 (void);

void dependent_set_expr		 (Dependent *dependent, ExprTree *expr);
void dependent_unqueue		 (Dependent *dep);
void dependent_unqueue_sheet	 (Sheet *sheet);
void dependent_link		 (Dependent *dep, CellPos const *pos);
void dependent_unlink		 (Dependent *dep, CellPos const *pos);
void dependent_unlink_sheet	 (Sheet *sheet);
void dependent_changed		 (Dependent *dep, CellPos const *pos,
				  gboolean queue_recalc);
void cb_dependent_queue_recalc	 (Dependent *dep, gpointer ignore);

void cell_add_dependencies	 (Cell *cell);
void cell_drop_dependencies	 (Cell *cell);
void cell_queue_recalc		 (Cell const *cell);
void cell_foreach_dep		 (Cell const *cell, DepFunc func, gpointer user);
gboolean cell_eval		 (Cell *cell);

void sheet_region_queue_recalc	 (Sheet const *sheet, Range const *range);
/* Do we need this ?
void sheet_foreach_dep		 (Sheet *sheet, DepFunc func, gpointer user);
 */
void sheet_deps_destroy		 (Sheet *sheet);
void workbook_deps_destroy	 (Workbook *wb);
void workbook_queue_all_recalc	 (Workbook *wb);

DependencyData *dependency_data_new (void);

void sheet_dump_dependencies	 (Sheet const *sheet);
void dependent_debug_name	 (Dependent const *dep, FILE *out);

#endif /* GNUMERIC_EVAL_H */
