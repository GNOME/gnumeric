/* vim: set sw=8: */
/*
 * eval.c:  Manage calculation dependencies between objects
 *
 * Copyright (C) 2000,2001
 *  Michael Meeks   (mmeeks@gnu.org)
 *  Miguel de Icaza (miguel@gnu.org)
 *  Jody Goldberg   (jgoldberg@home.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <config.h>
#include "eval.h"
#include "parse-util.h"
#include "ranges.h"
#include "value.h"
#include "main.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook-private.h"
#include "expr.h"
#include "cell.h"
#include "sheet.h"

#define UNLINK_DEP(dep)							\
  do {									\
	if (dep->sheet->deps->dependent_list == dep)			\
		dep->sheet->deps->dependent_list = dep->next_dep;	\
	if (dep->next_dep)						\
		dep->next_dep->prev_dep = dep->prev_dep;		\
	if (dep->prev_dep)						\
		dep->prev_dep->next_dep = dep->next_dep;		\
	/* Note, that ->prev_dep and ->next_dep are still valid.  */	\
  } while (0)


static GPtrArray *dep_classes = NULL;

void
dependent_types_init (void)
{
	g_return_if_fail (dep_classes == NULL);

	/* Init with a pair of NULL classes so we can access directly */
	dep_classes = g_ptr_array_new ();
	g_ptr_array_add	(dep_classes, NULL);
	g_ptr_array_add	(dep_classes, NULL);
}

void
dependent_types_shutdown (void)
{
	g_return_if_fail (dep_classes != NULL);
	g_ptr_array_free (dep_classes, TRUE);
}

/**
 * dependent_register_type :
 * @klass : A vtable
 *
 * Store the vtable and allocate an ID for a new class
 * of dependents.
 */
guint32
dependent_type_register (DependentClass const *klass)
{
	guint32 res;

	g_return_val_if_fail (dep_classes != NULL, 0);

	g_ptr_array_add	(dep_classes, (gpointer)klass);
	res = dep_classes->len-1;

	g_return_val_if_fail (res <= DEPENDENT_TYPE_MASK, res);

	return res;
}


/**
 * dependent_set_expr :
 * @dep : The dependent we are interested in.
 * @expr : new expression.
 *
 * When the expression associated with a dependent needs to change
 * this routine dispatches to the virtual handler.
 */
void
dependent_set_expr (Dependent *dep, ExprTree *expr)
{
	int const t = DEPENDENT_TYPE (dep);

	if (t == DEPENDENT_CELL) {
		/*
		 * Explicitly do not check for array subdivision, we may be
		 * replacing the corner of an array.
		 */
		cell_set_expr_unsafe (DEP_TO_CELL (dep), expr, NULL);
	} else {
		DependentClass *klass = g_ptr_array_index (dep_classes, t);

		g_return_if_fail (klass);
		(*klass->set_expr) (dep, expr);
	}
}

/*
 * dependent_queue_recalc:
 * @dep: the dependent that contains the expression needing recomputation.
 *
 * Marks @dep as needing recalculation
 * NOTE : it does NOT recursively dirty dependencies.
 */
#define dependent_queue_recalc(dep) \
  do { (dep)->flags |= DEPENDENT_NEEDS_RECALC; } while (0)

static void
cb_cell_list_deps (Dependent *dep, gpointer user)
{
	GSList **list = (GSList **)user;
	*list = g_slist_prepend (*list, dep);
}

static GSList *
cell_list_deps (const Cell *cell)
{
	GSList *deps = NULL;
	cell_foreach_dep (cell, cb_cell_list_deps, &deps);
	return deps;
}


/**
 * dependent_queue_recalc_list :
 * @list :
 * @recurse : optionally recursively dirty things
 *
 * Queues any elements of @list for recalc that are not already queued,
 * and marks all elements as needing a recalc.
 */
static void
dependent_queue_recalc_list (GSList *list, gboolean recurse)
{
	GSList *work = NULL;

	for (; list != NULL ; list = list->next) {
		Dependent *dep = list->data;
		if (!(dep->flags & DEPENDENT_NEEDS_RECALC)) {
			dependent_queue_recalc (dep);
			if (recurse)
				work = g_slist_prepend (work, dep);
		}
	}

	/*
	 * Work is now a list of marked cells whose dependencies need
	 * to be marked.  Marking early guarentees that we will not
	 * get duplicates.  (And it thus limits the length of the list.)
	 * We treat work as a stack.
	 */

	while (work) {
		Dependent *dep = work->data;

		/* Pop the top element.  */
		list = work;
		work = work->next;
		g_slist_free_1 (list);

		if (DEPENDENT_IS_CELL (dep)) {
			GSList *deps = cell_list_deps (DEP_TO_CELL (dep));
			GSList *waste = NULL;
			GSList *next;
			for (list = deps; list != NULL ; list = next) {
				Dependent *dep = list->data;
				next = list->next;
				if (dep->flags & DEPENDENT_NEEDS_RECALC) {
					list->next = waste;
					waste = list;
				} else {
					dependent_queue_recalc (dep);
					list->next = work;
					work = list;
				}
			}
			g_slist_free (waste);
		}
	}
}


void
cb_dependent_queue_recalc (Dependent *dep, gpointer ignore)
{
	g_return_if_fail (dep != NULL);

	if (!(dep->flags & DEPENDENT_NEEDS_RECALC)) {
		GSList listrec;
		listrec.next = NULL;
		listrec.data =dep;
		dependent_queue_recalc_list (&listrec, TRUE);
	}
}


/**************************************************************************
 * Data structures for managing dependencies between objects.
 *
 * The DependencyRange hash needs to be improved.  It is a huge
 * performance hit when there are large numbers of range depends.
 */

/*
 * A DependencyRange defines a range of cells whose values
 * are used by another objects in the spreadsheet.
 *
 * A change in those cells will trigger a recomputation on the
 * cells listed in dependent_list.
 */
typedef struct {
	/*
	 *  This range specifies uniquely the position of the
	 * cells that are depended on by the dependent_list.
	 */
	Range  range;

	/* The list of cells that depend on this range */
	GSList *dependent_list;
} DependencyRange;

/*
 *  A DependencySingle stores a list of cells that depend
 * on the cell at @pos in @dependent_list. NB. the EvalPos
 * is quite vital since there may not be a cell there yet.
 */
typedef struct {
	/*
	 * The position of a cell
	 */
	CellPos pos;
	/*
	 * The list of cells that depend on this cell
	 */
	GSList  *dependent_list;
} DependencySingle;

typedef enum {
	REMOVE_DEPS = 0,
	ADD_DEPS = 1
} DepOperation;

static void
handle_cell_single_dep (Dependent *dep, CellPos const *pos,
			CellRef const *a, DepOperation operation)
{
	DependencyContainer *deps;
	DependencySingle *single;
	DependencySingle lookup;

	if (a->sheet == NULL)
		deps = dep->sheet->deps;
	else
		deps = a->sheet->deps;

	if (!deps)
		return;

	/* Convert to absolute cordinates */
	cellref_get_abs_pos (a, pos, &lookup.pos);

	single = g_hash_table_lookup (deps->single_hash, &lookup);

	if (operation == ADD_DEPS) {
		if (single) {
			if (!g_slist_find (single->dependent_list, dep))
				single->dependent_list =
					g_slist_prepend (single->dependent_list, dep);
			else
				/* Referenced twice in the same formula */;
		} else {
			single  = g_new (DependencySingle, 1);
			*single = lookup;
			single->dependent_list = g_slist_prepend (NULL, dep);
			g_hash_table_insert (deps->single_hash, single, single);
		}
	} else { /* Remove */
		if (single) {
			single->dependent_list = g_slist_remove (single->dependent_list, dep);
			if (single->dependent_list == NULL) {
				g_hash_table_remove (deps->single_hash, single);
				g_free (single);
			}
		} else
			;/* Referenced twice and list killed already */
	}
}

static void
add_range_dep (DependencyContainer *deps, Dependent *dependent,
	       DependencyRange const *range)
{
	/* Look it up */
	DependencyRange *result = g_hash_table_lookup (deps->range_hash, range);

	if (result) {
		/* Is the dependent already listed? */
		GSList const *cl = g_slist_find (result->dependent_list, dependent);
		if (cl)
			return;

		/* It was not: add it */
		result->dependent_list = g_slist_prepend (result->dependent_list, dependent);

		return;
	}

	/* Create a new DependencyRange structure */
	result = g_new (DependencyRange, 1);
	*result = *range;
	result->dependent_list = g_slist_prepend (NULL, dependent);

	g_hash_table_insert (deps->range_hash, result, result);
}

static void
drop_range_dep (DependencyContainer *deps, Dependent *dependent,
		DependencyRange const *range)
{
	DependencyRange *result;

	if (!deps)
		return;

	result = g_hash_table_lookup (deps->range_hash, range);
	if (result) {
		result->dependent_list =
			g_slist_remove (result->dependent_list, dependent);
		if (result->dependent_list == NULL) {
			g_hash_table_remove (deps->range_hash, result);
			g_free (result);
		}
	}
}

static void
depsingle_dtor (DependencySingle *single)
{
	g_return_if_fail (single->dependent_list == NULL);
	g_free (single);
}

static void
deprange_init (DependencyRange *range, CellPos const *pos,
	       CellRef const *a,  CellRef const *b)
{
	cellref_get_abs_pos (a, pos, &range->range.start);
	cellref_get_abs_pos (b, pos, &range->range.end);
	range_normalize (&range->range);

	range->dependent_list = NULL;
	if (b->sheet && a->sheet != b->sheet)
		g_warning ("FIXME: 3D references need work");
}

static void
deprange_dtor (DependencyRange *deprange)
{
	deprange->dependent_list = NULL;	/* poison it */
	g_free (deprange);
}

/*
 * Add the dependency of Dependent dep on the range
 * enclose by CellRef a and CellRef b
 *
 * We compute the location from @pos
 */
static void
handle_cell_range_deps (Dependent *dep, CellPos const *pos,
			CellRef const *a, CellRef const *b, DepOperation operation)
{
	DependencyRange range;
	DependencyContainer *depsa, *depsb;

	deprange_init (&range, pos, a, b);

	depsa = eval_sheet (a->sheet, dep->sheet)->deps;
	if (operation)
		add_range_dep  (depsa, dep, &range);
	else
		drop_range_dep (depsa, dep, &range);

	depsb = eval_sheet (b->sheet, dep->sheet)->deps;
	if (depsa != depsb) {
		/* FIXME: we need to iterate sheets between to be correct */
		if (operation)
			add_range_dep  (depsb, dep, &range);
		else
			drop_range_dep (depsb, dep, &range);
	}
}

/*
 * Adds the dependencies for a Value
 */
static void
handle_value_deps (Dependent *dep, CellPos const *pos,
		   Value const *value, DepOperation operation)
{
	switch (value->type) {
	case VALUE_EMPTY:
	case VALUE_STRING:
	case VALUE_INTEGER:
	case VALUE_FLOAT:
	case VALUE_BOOLEAN:
	case VALUE_ERROR:
		/* Constants have no dependencies */
		break;

		/* Check every element of the array */
	case VALUE_ARRAY:
	{
		int x, y;

		for (x = 0; x < value->v_array.x; x++)
			for (y = 0; y < value->v_array.y; y++)
				handle_value_deps (dep, pos,
						   value->v_array.vals [x] [y],
						   operation);
		break;
	}
	case VALUE_CELLRANGE:
		handle_cell_range_deps (dep, pos,
			&value->v_range.cell.a,
			&value->v_range.cell.b,
			operation);
		break;
	default:
		g_warning ("Unknown Value type, dependencies lost");
		break;
	}
}

/*
 * This routine walks the expression tree looking for cell references
 * and cell range references.
 */
static void
handle_tree_deps (Dependent *dep, CellPos const *pos,
		  ExprTree *tree, DepOperation operation)
{
	switch (tree->any.oper) {
	case OPER_ANY_BINARY:
		handle_tree_deps (dep, pos, tree->binary.value_a, operation);
		handle_tree_deps (dep, pos, tree->binary.value_b, operation);
		return;

	case OPER_ANY_UNARY:
		handle_tree_deps (dep, pos, tree->unary.value, operation);
		return;

	case OPER_VAR:
		handle_cell_single_dep (dep, pos, &tree->var.ref, operation);
		return;

	case OPER_CONSTANT:
		handle_value_deps (dep, pos, tree->constant.value, operation);
		return;

	/*
	 * FIXME: needs to be taught implicit intersection +
	 * more cunning handling of argument type matching.
	 */
	case OPER_FUNCALL: {
		GList *l;

		for (l = tree->func.arg_list; l; l = l->next)
			handle_tree_deps (dep, pos, l->data, operation);
		return;
	}

	case OPER_NAME:
		if (tree->name.name->builtin) {
			/* FIXME: insufficiently flexible dependancy code (?) */
		} else
			handle_tree_deps (dep, pos, tree->name.name->t.expr_tree, operation);
		return;

	case OPER_ARRAY:
		if (tree->array.x != 0 || tree->array.y != 0) {
			/* Non-corner cells depend on the corner */
			CellRef a;

			/* We cannot support array expressions unless
			 * we have a position.
			 */
			g_return_if_fail (pos != NULL);

			a.col_relative = a.row_relative = 0;
			a.sheet = dep->sheet;
			a.col   = pos->col - tree->array.x;
			a.row   = pos->row - tree->array.y;

			handle_cell_single_dep (dep, pos, &a, operation);
		} else
			/* Corner cell depends on the contents of the expr */
			handle_tree_deps (dep, pos, tree->array.corner.expr, operation);
		return;
	default:
		g_warning ("Unknown Operation type, dependencies lost");
		break;
	}
}

/**
 * dependent_link:
 * @dep : the dependent that changed
 * @pos: The optionally NULL position of the dependent.
 *
 * Adds the dependent to the workbook wide list of dependents.
 */
void
dependent_link (Dependent *dep, CellPos const *pos)
{
	Sheet *sheet;

	g_return_if_fail (dep != NULL);
	g_return_if_fail (dep->expression != NULL);
	g_return_if_fail (!(dep->flags & DEPENDENT_IN_EXPR_LIST));
	g_return_if_fail (IS_SHEET (dep->sheet));
	g_return_if_fail (dep->sheet->deps != NULL);

	sheet = dep->sheet;

	/* Make this the new head of the dependent list.  */
	dep->prev_dep = NULL;
	dep->next_dep = sheet->deps->dependent_list;
	if (dep->next_dep)
		dep->next_dep->prev_dep = dep;
	sheet->deps->dependent_list = dep;
	dep->flags |= DEPENDENT_IN_EXPR_LIST;

	handle_tree_deps (dep, pos, dep->expression, ADD_DEPS);
}

/**
 * dependent_unlink:
 * @dep : the dependent that changed
 * @pos: The optionally NULL position of the dependent.
 *
 * Removes the dependent from the workbook wide list of dependents.
 */
void
dependent_unlink (Dependent *dep, CellPos const *pos)
{
	g_return_if_fail (dep != NULL);

	if (dep->sheet != NULL) {
		g_return_if_fail (dep->expression != NULL);
		g_return_if_fail (dep->flags & DEPENDENT_IN_EXPR_LIST);
		g_return_if_fail (IS_SHEET (dep->sheet));

		/* see note in do_deps_destroy */
		if (dep->sheet->deps != NULL)
			handle_tree_deps (dep, pos, dep->expression, REMOVE_DEPS);

		dep->flags &= ~(DEPENDENT_IN_EXPR_LIST | DEPENDENT_NEEDS_RECALC);
		UNLINK_DEP (dep);
	}
}

/**
 * dependent_unlink_sheet :
 * @sheet :
 *
 * An internal routine to remove all expressions associated with a given sheet
 * from the workbook wide expression list.  WARNING : This is a dangerous
 * internal function.  it leaves the dependents in an invalid state.  It is
 * intended for use by sheet_destroy_contents.
 */
void
dependent_unlink_sheet (Sheet *sheet)
{
	Workbook *wb;

	g_return_if_fail (IS_SHEET (sheet));

	wb = sheet->workbook;
	WORKBOOK_FOREACH_DEPENDENT (wb, dep,
		 if (dep->sheet == sheet) {
			 dep->flags &= ~DEPENDENT_IN_EXPR_LIST;
			 UNLINK_DEP (dep);
		 });
}

/**
 * dependent_changed:
 * @cell : the dependent that changed
 * @queue_recalc: also queue a recalc for the dependent.
 *
 * Registers the expression with the sheet and optionally queues a recalc
 * of the dependent.
 */
void
dependent_changed (Dependent *dep, CellPos const *pos, gboolean queue_recalc)
{
	g_return_if_fail (dep != NULL);

	dependent_link (dep, pos);

	if (queue_recalc) {
		if (dep->sheet->workbook->priv->recursive_dirty_enabled)
			cb_dependent_queue_recalc (dep, NULL);
		else
			dependent_queue_recalc (dep);
	}
}

/**
 * cell_add_dependencies:
 * @cell:
 *
 * This registers the dependencies for this cell
 * by scanning all of the references made in the
 * parsed expression.
 **/
void
cell_add_dependencies (Cell *cell)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->base.sheet != NULL);
	g_return_if_fail (cell->base.sheet->deps != NULL);

	if (cell_has_expr (cell))
		handle_tree_deps (CELL_TO_DEP (cell), &cell->pos,
				  cell->base.expression, ADD_DEPS);
}

/**
 * cell_drop_dependencies:
 * @cell:
 *
 * Remove the Cell from the DependencyRange hash tables
 **/
void
cell_drop_dependencies (Cell *cell)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->base.sheet != NULL);

	if (cell_has_expr (cell))
		handle_tree_deps (CELL_TO_DEP (cell), &cell->pos,
				  cell->base.expression, REMOVE_DEPS);
}

gboolean
cell_eval (Cell *cell)
{
	g_return_val_if_fail (cell != NULL, TRUE);
	if (cell->base.flags & DEPENDENT_NEEDS_RECALC) {
		if (cell_eval_content (cell))
			cell->base.flags &= ~DEPENDENT_NEEDS_RECALC;
		else
			return FALSE;
	}
	return TRUE;
}

/**
 * cell_queue_recalc :
 * @cell :
 *
 * Queue the cell and everything that depends on it for recalculation.
 * If a dependency is already queued ignore it.
 */
void
cell_queue_recalc (Cell const *cell)
{
	g_return_if_fail (cell != NULL);

	if (!cell_needs_recalc (cell)) {
		GSList *deps;

		if (cell_has_expr (cell))
			dependent_queue_recalc (CELL_TO_DEP (cell));

		deps = cell_list_deps (cell);
		dependent_queue_recalc_list (deps, TRUE);
		g_slist_free (deps);
	}
}

typedef struct {
	int      col, row;
	DepFunc	 func;
	gpointer user;
} get_cell_dep_closure_t;

static void
search_cell_deps (gpointer key, gpointer value, gpointer closure)
{
	get_cell_dep_closure_t const *c = closure;
	DependencyRange const *deprange = key;
	Range const *range = &(deprange->range);

#if 0
	/* When things get slow this is a good test to enable */
	static int counter = 0;
	if ((++counter % 100000) == 0)
	    printf ("%d\n", counter / 100000);
#endif

	/* No intersection is the common case */
	if (range_contains (range, c->col, c->row)) {
		GSList *l;
		for (l = deprange->dependent_list; l; l = l->next) {
			Dependent *dep = l->data;
			(*c->func) (dep, c->user);
		}
	}
}

static void
cell_foreach_range_dep (Cell const *cell, DepFunc func, gpointer user)
{
	get_cell_dep_closure_t closure;

	closure.col   = cell->pos.col;
	closure.row   = cell->pos.row;
	closure.func  = func;
	closure.user  = user;

	/* FIXME FIXME FIXME :
	 * This call decimates performance
	 * If this list contains lots of ranges we are toast.  Consider
	 * subdividing the master list.  A simple fixed bucket scheme is
	 * probably sufficient (say 64x64) but we could go to something
	 * adaptive or a simple quad tree.
	 */
	g_hash_table_foreach (cell->base.sheet->deps->range_hash,
			      &search_cell_deps, &closure);
}

static void
cell_foreach_single_dep (Sheet const *sheet, int col, int row,
			 DepFunc func, gpointer user)
{
	DependencySingle lookup, *single;
	DependencyContainer *deps = sheet->deps;
	GSList *ptr;

	lookup.pos.col = col;
	lookup.pos.row = row;

	single = g_hash_table_lookup (deps->single_hash, &lookup);
	if (single == NULL)
		return;

	for (ptr = single->dependent_list ; ptr != NULL ; ptr = ptr->next) {
		Dependent *dep = ptr->data;
		(*func) (dep, user);
	}
}

void
cell_foreach_dep (Cell const *cell, DepFunc func, gpointer user)
{
	g_return_if_fail (cell != NULL);

	/* accelerate exit */
	if (!cell->base.sheet->deps)
		return;

	cell_foreach_range_dep (cell, func, user);
	cell_foreach_single_dep (cell->base.sheet, cell->pos.col, cell->pos.row,
				 func, user);
}

static void
cb_region_contained_depend (gpointer key, gpointer value, gpointer user)
{
	DependencyRange const *deprange  = key;
	Range const *range = &deprange->range;
	Range const *target = user;

	if (range_overlap (target, range))
		dependent_queue_recalc_list (deprange->dependent_list, TRUE);
}

static void
cb_range_recalc_all_depends (gpointer key, gpointer value, gpointer ignore)
{
	DependencyRange const *deprange = key;
	dependent_queue_recalc_list (deprange->dependent_list, FALSE);
}

static void
cb_single_recalc_all_depends (gpointer key, gpointer value, gpointer ignore)
{
	DependencySingle const *single = value;
	dependent_queue_recalc_list (single->dependent_list, FALSE);
}

/**
 * sheet_region_queue_recalc :
 *
 * @sheet : The sheet.
 * @range : Optionally NULL range.
 *
 * Queues things that depend on @sheet!@range for recalc.
 *
 * If @range is NULL the entire sheet is used.
 */
void
sheet_region_queue_recalc (Sheet const *sheet, Range const *r)
{
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->deps != NULL);

	if (r == NULL) {
		SHEET_FOREACH_DEPENDENT (sheet, dep, {
			dependent_queue_recalc (dep);
		});

		/* Find anything that depends on a range in this sheet */
		g_hash_table_foreach (sheet->deps->range_hash,
				      &cb_range_recalc_all_depends, NULL);

		/* Find anything that depends on a single reference */
		g_hash_table_foreach (sheet->deps->single_hash,
				      &cb_single_recalc_all_depends, NULL);
	} else {
		int ix, iy, end_col, end_row;

		SHEET_FOREACH_DEPENDENT (sheet, dep, {
			Cell *cell = DEP_TO_CELL (dep);
			if (DEPENDENT_IS_CELL (dep) &&
			    range_contains (r, cell->pos.col, cell->pos.row))
				dependent_queue_recalc (dep);
		});

		g_hash_table_foreach (sheet->deps->range_hash,
				      &cb_region_contained_depend,
				      (gpointer)r);

		/* TODO : Why not use sheet_foreach_cell ?
		 * We would need to be more careful about queueing
		 * things that depend on blanks, but that is not too hard.
		 */
		end_col = MIN (r->end.col, sheet->cols.max_used);
		end_row = MIN (r->end.row, sheet->rows.max_used);
		for (ix = r->start.col; ix <= end_col; ix++)
			for (iy = r->start.row; iy <= end_row; iy++)
				cell_foreach_single_dep (sheet, ix, iy,
					cb_dependent_queue_recalc, NULL);
	}
}

/*******************************************************************/

static void
invalidate_refs (Dependent *dep, ExprRewriteInfo const *rwinfo)
{
	ExprTree *newtree;

	newtree = expr_rewrite (dep->expression, rwinfo);

	/*
	 * We are told this dependent depends on this region, hence if newtree
	 * is null then either we did not depend on it ( ie. serious breakage )
	 * or we had a duplicate reference and we have already removed it.
	 */
	g_return_if_fail (newtree != NULL);

	dependent_set_expr (dep, newtree);
}

/*
 * WARNING : Hash is pointing to freed memory once this is complete
 * This is tightly coupled with do_deps_destroy.
 */
static void
cb_range_hash_invalidate (gpointer key, gpointer value, gpointer closure)
{
	ExprRewriteInfo const *rwinfo = closure;
	DependencyRange   *deprange = value;
	GSList *deps = deprange->dependent_list;
	GSList *ptr = deps;
	Dependent *dependent;

	deprange->dependent_list = NULL;
	if (rwinfo->type == EXPR_REWRITE_SHEET) {
		Sheet const *target = rwinfo->u.sheet;
		for (; ptr != NULL; ptr = ptr->next) {
			dependent = ptr->data;
			if (dependent->sheet != target)
				invalidate_refs (dependent, rwinfo);
		}
	} else if (rwinfo->type == EXPR_REWRITE_WORKBOOK) {
		Workbook const *target = rwinfo->u.workbook;
		for (; ptr != NULL; ptr = ptr->next) {
			dependent = ptr->data;
			if (dependent->sheet->workbook != target)
				invalidate_refs (dependent, rwinfo);
		}
	} else {
		g_assert_not_reached ();
	}

	g_slist_free (deps);
	deprange_dtor (deprange);
}

/*
 * WARNING : Hash is pointing to freed memory once this is complete
 * This is tightly coupled with do_deps_destroy.
 */
static void
cb_single_hash_invalidate (gpointer key, gpointer value, gpointer closure)
{
	ExprRewriteInfo const *rwinfo = closure;
	DependencySingle *depsingle = value;
	GSList *deps = depsingle->dependent_list;
	GSList *ptr = deps;
	Dependent *dependent;

	depsingle->dependent_list = NULL;
	if (rwinfo->type == EXPR_REWRITE_SHEET) {
		Sheet const *target = rwinfo->u.sheet;
		for (; ptr != NULL; ptr = ptr->next) {
			dependent = ptr->data;
			if (dependent->sheet != target)
				invalidate_refs (dependent, rwinfo);
		}
	} else if (rwinfo->type == EXPR_REWRITE_WORKBOOK) {
		Workbook const *target = rwinfo->u.workbook;
		for (; ptr != NULL; ptr = ptr->next) {
			dependent = ptr->data;
			if (dependent->sheet->workbook != target)
				invalidate_refs (dependent, rwinfo);
		}
	} else {
		g_assert_not_reached ();
	}

	g_slist_free (deps);
	depsingle_dtor (depsingle);
}

/*
 * do_deps_destroy :
 * Invalidate references of all kinds to the target region described by
 * @rwinfo.
 */
static void
do_deps_destroy (Sheet *sheet, ExprRewriteInfo const *rwinfo)
{
	DependencyContainer *deps;

	g_return_if_fail (IS_SHEET (sheet));

	deps = sheet->deps;
	if (deps == NULL)
		return;

	/* We are destroying all the dependencies, there is no need to
	 * delicately remove individual items from the lists.  The only purpose
	 * that serves is to validate the state of our data structures.  If
	 * required this optimization can be disabled for debugging.
	 */
	sheet->deps = NULL;

	if (deps->range_hash) {
		g_hash_table_foreach (deps->range_hash,
				      &cb_range_hash_invalidate, (gpointer)rwinfo);
		g_hash_table_destroy (deps->range_hash);
		deps->range_hash = NULL;
	}

	if (deps->single_hash) {
		g_hash_table_foreach (deps->single_hash,
				      &cb_single_hash_invalidate, (gpointer)rwinfo);
		g_hash_table_destroy (deps->single_hash);
		deps->single_hash = NULL;
	}

	g_free (deps);
}

void
sheet_deps_destroy (Sheet *sheet)
{
	ExprRewriteInfo rwinfo;

	g_return_if_fail (sheet != NULL);

	rwinfo.type = EXPR_REWRITE_SHEET;
	rwinfo.u.sheet = sheet;

	do_deps_destroy (sheet, &rwinfo);
}

void
workbook_deps_destroy (Workbook *wb)
{
	ExprRewriteInfo rwinfo;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (wb->sheets != NULL);

	rwinfo.type = EXPR_REWRITE_WORKBOOK;
	rwinfo.u.workbook = wb;

	WORKBOOK_FOREACH_SHEET (wb, sheet, {
		do_deps_destroy (sheet, &rwinfo);
	});
}

void
workbook_queue_all_recalc (Workbook *wb)
{
	/* FIXME : warning what about dependents in other workbooks */

	WORKBOOK_FOREACH_DEPENDENT
		(wb, dep, { dependent_queue_recalc (dep); });
}

/*
 * Computes all of the cells pending computation and
 * any dependency.
 */
void
workbook_recalc (Workbook *wb)
{
	g_return_if_fail (IS_WORKBOOK (wb));

	WORKBOOK_FOREACH_DEPENDENT (wb, dep,
		if (dep->flags & DEPENDENT_NEEDS_RECALC) {
			int const t = DEPENDENT_TYPE (dep);

			if (t != DEPENDENT_CELL) {
				DependentClass *klass = g_ptr_array_index (dep_classes, t);

				g_return_if_fail (klass);
				(*klass->eval) (dep);
			} else {
				gboolean finished = cell_eval_content (DEP_TO_CELL (dep));

				/* This should always be the top of the stack */
				g_return_if_fail (finished);
			}

			/* Don't clear flag until after in case we iterate */
			dep->flags &= ~DEPENDENT_NEEDS_RECALC;
		});
}

/**
 * workbook_recalc_all :
 * @wb :
 *
 * Queues all dependents for recalc and marks them all as dirty.
 */
void
workbook_recalc_all (Workbook *wb)
{
	workbook_queue_all_recalc (wb);
	workbook_recalc (wb);
	WORKBOOK_FOREACH_VIEW (wb, view,
		sheet_update (wb_view_cur_sheet (view)););
}

static guint
deprange_hash_func (gconstpointer v)
{
	DependencyRange const *r = v;

	return ((((r->range.start.row << 8) + r->range.end.row) << 8) +
		(r->range.start.col << 8) + (r->range.end.col));
}
static gint
deprange_equal_func (gconstpointer v, gconstpointer v2)
{
	DependencyRange const *r1 = (DependencyRange const *) v;
	DependencyRange const *r2 = (DependencyRange const *) v2;

	if (r1->range.start.col != r2->range.start.col)
		return 0;
	if (r1->range.start.row != r2->range.start.row)
		return 0;
	if (r1->range.end.col != r2->range.end.col)
		return 0;
	if (r1->range.end.row != r2->range.end.row)
		return 0;

	return 1;
}


static guint
depsingle_hash (gconstpointer key)
{
	DependencySingle const *d = (DependencySingle const *) key;

	return (d->pos.row << 8) ^ d->pos.col;
}

static gint
depsingle_equal (gconstpointer ai, gconstpointer bi)
{
	DependencySingle const *a = (DependencySingle const *)ai;
	DependencySingle const *b = (DependencySingle const *)bi;

	return (a->pos.row == b->pos.row &&
		a->pos.col == b->pos.col);
}

DependencyContainer *
dependency_data_new (void)
{
	DependencyContainer *deps = g_new (DependencyContainer, 1);

	deps->dependent_list = NULL;

	deps->range_hash  = g_hash_table_new (deprange_hash_func,
					      deprange_equal_func);
	deps->single_hash = g_hash_table_new (depsingle_hash,
					      depsingle_equal);

	return deps;
}

/****************************************************************************
 * Debug utils
 */
static void
dump_dependent_list (GSList *l)
{
	printf ("(");
	for (; l; l = l->next) {
		Dependent *dep = l->data;
		dependent_debug_name (dep, stdout);
		printf (", ");
	}
	printf (")\n");
}

static void
dump_range_dep (gpointer key, gpointer value, gpointer closure)
{
	DependencyRange const *deprange = key;
	Range const *range = &(deprange->range);

	/* 2 calls to col_name and row_name.  It uses a static buffer */
	printf ("\t%s:", cell_pos_name (&range->start));
	printf ("%s -> ", cell_pos_name (&range->end));

	dump_dependent_list (deprange->dependent_list);
}

static void
dump_single_dep (gpointer key, gpointer value, gpointer closure)
{
	DependencySingle *dep = key;

	printf ("\t%s -> ", cell_pos_name (&dep->pos));

	dump_dependent_list (dep->dependent_list);
}

/**
 * sheet_dump_dependencies :
 * @sheet :
 *
 * A useful utility for checking the state of the dependency data structures.
 */
void
sheet_dump_dependencies (Sheet const *sheet)
{
	DependencyContainer *deps;

	g_return_if_fail (sheet != NULL);

	deps = sheet->deps;

	if (deps) {
		printf ("For %s:%s\n",
			sheet->workbook->filename
			?  sheet->workbook->filename
			: "(no name)",
			sheet->name_unquoted);

		if (g_hash_table_size (deps->range_hash) > 0) {
			printf ("Range hash size %d: range over which cells in list depend\n",
				g_hash_table_size (deps->range_hash));
			g_hash_table_foreach (deps->range_hash,
					      dump_range_dep, NULL);
		}

		if (g_hash_table_size (deps->single_hash) > 0) {
			printf ("Single hash size %d: cell on which list of cells depend\n",
				g_hash_table_size (deps->single_hash));
			g_hash_table_foreach (deps->single_hash,
					      dump_single_dep, NULL);
		}
	}
}

/**
 * dependent_debug_name :
 * @dep : The dependent we are interested in.
 * @file : FILE * to print to.
 *
 * A useful little debugging utility.
 */
void
dependent_debug_name (Dependent const *dep, FILE *out)
{
	int t;

	g_return_if_fail (dep != NULL);
	g_return_if_fail (out != NULL);
	g_return_if_fail (dep_classes);

	if (dep->sheet != NULL)
		fprintf (out, "%s!", dep->sheet->name_quoted);
	else
		g_warning ("Invalid dep, missing sheet");

	t = DEPENDENT_TYPE (dep);
	if (t != DEPENDENT_CELL) {
		DependentClass *klass = g_ptr_array_index (dep_classes, t);

		g_return_if_fail (klass);
		(*klass->debug_name) (dep, out);
	} else
		fprintf (out, "%s", cell_name (DEP_TO_CELL (dep)));
}
