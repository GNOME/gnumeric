#ifndef GNUMERIC_EVAL_H
#define GNUMERIC_EVAL_H

#include "gnumeric.h"
#include <stdio.h>

struct _Dependent
{
	guint	  flags;
	Sheet	 *sheet;
	ExprTree *expression;

	/* Double-linked list.  */
	struct _Dependent *next_dep, *prev_dep;
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
	DEPENDENT_BEING_CALCULATED = 0x00004000,

	/* Types */
	DEPENDENT_CELL 		  = 0x00000001,
	DEPENDENT_TYPE_MASK	  = 0x00000fff,
} DependentFlags;

#define dependent_type(dep)		((dep)->flags & DEPENDENT_TYPE_MASK)
#define dependent_is_cell(dep)		(dependent_type (dep) == DEPENDENT_CELL)
#define dependent_needs_recalc(dep)	(dep->flags & DEPENDENT_NEEDS_RECALC)

struct _DependencyContainer {
	Dependent *dependent_list;

	/*
	 *   Large ranges hashed on 'range' to accelerate duplicate
	 * culling. This is tranversed by g_hash_table_foreach mostly.
	 */
	GHashTable **range_hash;
	/*
	 *   Single ranges, this maps an EvalPos * to a GSList of its
	 * dependencies.
	 */
	GHashTable *single_hash;
};

typedef void (*DepFunc) (Dependent *dep, gpointer user);

guint32 dependent_type_register  (DependentClass const *klass);
void dependent_types_init	 (void);
void dependent_types_shutdown	 (void);

void dependent_set_expr		 (Dependent *dependent, ExprTree *expr);
void dependent_link		 (Dependent *dep, CellPos const *pos);
void dependent_unlink		 (Dependent *dep, CellPos const *pos);
void dependent_unlink_sheet	 (Sheet *sheet);
void dependent_eval		 (Dependent *dep);
void dependent_changed		 (Dependent *dep, CellPos const *pos,
				  gboolean queue_recalc);
void cb_dependent_queue_recalc	 (Dependent *dep, gpointer ignore);

void cell_add_dependencies	 (Cell *cell);
void cell_drop_dependencies	 (Cell *cell);
void cell_queue_recalc		 (Cell const *cell);
void cell_foreach_dep		 (Cell const *cell, DepFunc func, gpointer user);
void cell_eval		 	 (Cell *cell);

void sheet_region_queue_recalc	 (Sheet const *sheet, Range const *range);
/* Do we need this ?
void sheet_foreach_dep		 (Sheet *sheet, DepFunc func, gpointer user);
 */
void sheet_deps_destroy		 (Sheet *sheet);
void workbook_deps_destroy	 (Workbook *wb);
void workbook_queue_all_recalc	 (Workbook *wb);

DependencyContainer *dependency_data_new (void);

void sheet_dump_dependencies	 (Sheet const *sheet);
void dependent_debug_name	 (Dependent const *dep, FILE *out);

#define DEPENDENT_CONTAINER_FOREACH_DEPENDENT(dc, dep, code)	\
  do {								\
	Dependent *dep = (dc)->dependent_list;			\
	while (dep) {						\
		Dependent *_next = dep->next_dep;		\
		code;						\
		dep = _next;					\
	}							\
  } while (0)


#endif /* GNUMERIC_EVAL_H */
