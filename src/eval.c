/*
 * eval.c:  Recomputation routines for things that depend on cells.
 *
 * Please do not commit to this module, send a patch to Michael.
 *
 * Authors:
 *  Michael Meeks   (mmeeks@gnu.org)
 *  Miguel de Icaza (miguel@gnu.org)
 *  Jody Goldberg   (jgoldberg@home.com)
 */

#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "parse-util.h"
#include "ranges.h"
#include "eval.h"
#include "value.h"
#include "main.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook.h"
#include "expr.h"
#include "cell.h"
#include "sheet.h"

typedef enum {
	REMOVE_DEPS = 0,
	ADD_DEPS = 1
} DepOperation;

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
	GList *dependent_list;
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
	GList  *dependent_list;
} DependencySingle;

struct _DependencyData {
	/*
	 *   Large ranges hashed on 'range' to accelerate duplicate
	 * culling. This is tranversed by g_hash_table_foreach mostly.
	 */
	GHashTable *range_hash;
	/*
	 *   Single ranges, this maps an EvalPos * to a GList of its
	 * dependencies.
	 */
	GHashTable *single_hash;
};

/*******************************************************************/

static void
dump_dependent_list (GList *l)
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
	const DependencyRange *deprange = key;
	const Range *range = &(deprange->range);

	/* 2 calls to col_name.  It uses a static buffer */
	printf ("\t%s%d:",
		col_name (range->start.col), range->start.row + 1);
	printf ("%s%d -> ",
		col_name (range->end.col), range->end.row + 1);

	dump_dependent_list (deprange->dependent_list);
}

static void
dump_single_dep (gpointer key, gpointer value, gpointer closure)
{
	DependencySingle *dep = key;

	/* 2 calls to col_name.  It uses a static buffer */
	printf ("\t%s -> ", cell_pos_name (&dep->pos));

	dump_dependent_list (dep->dependent_list);
}

void
sheet_dump_dependencies (const Sheet *sheet)
{
	DependencyData *deps;

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

	if (sheet->workbook->eval_queue) {
		printf ("Unevaluated cells on queue:\n");
		dump_dependent_list (sheet->workbook->eval_queue);
	}
}

/*******************************************************************/

/*
 * Comparission function for the dependency hash table
 */
static gint
range_equal_func (gconstpointer v, gconstpointer v2)
{
	const DependencyRange *r1 = (const DependencyRange *) v;
	const DependencyRange *r2 = (const DependencyRange *) v2;

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

/*
 * Hash function for DependencyRange structures
 */
static guint
range_hash_func (gconstpointer v)
{
	const DependencyRange *r = v;

	return ((((r->range.start.row << 8) + r->range.end.row) << 8) +
		(r->range.start.col << 8) + (r->range.end.col));
}

static guint
dependency_single_hash (gconstpointer key)
{
	const DependencySingle *d = (const DependencySingle *) key;

	return (d->pos.row << 8) ^ d->pos.col;
}

static gint
dependency_single_equal (gconstpointer ai, gconstpointer bi)
{
	const DependencySingle *a = (const DependencySingle *)ai;
	const DependencySingle *b = (const DependencySingle *)bi;

	return (a->pos.row == b->pos.row &&
		a->pos.col == b->pos.col);
}

DependencyData *
dependency_data_new (void)
{
	DependencyData *deps  = g_new (DependencyData, 1);

	deps->range_hash  = g_hash_table_new (range_hash_func,
					      range_equal_func);
	deps->single_hash = g_hash_table_new (dependency_single_hash,
					      dependency_single_equal);

	return deps;
}

static gboolean
dependency_range_destroy (gpointer key, gpointer value, gpointer closure)
{
	DependencyRange *deprange = value;

	g_list_free (deprange->dependent_list);
	deprange->dependent_list = NULL;

	g_free (value);

	return TRUE;
}

static gboolean
dependency_single_destroy (gpointer key, gpointer value, gpointer closure)
{
	DependencySingle *single = value;

	g_list_free (single->dependent_list);
	single->dependent_list = NULL;

	g_free (value);

	return TRUE;
}

typedef struct {
	ExprRewriteInfo const *rwinfo;

	GSList          *dependent_list;
} destroy_closure_t;

static void
cb_range_hash_to_list (gpointer key, gpointer value, gpointer closure)
{
	destroy_closure_t *c = closure;
	GList             *l;
	DependencyRange   *dep = value;

	for (l = dep->dependent_list; l; l = l->next) {
		Dependent *dependent = l->data;

		if      (c->rwinfo->type == EXPR_REWRITE_SHEET &&
			 dependent->sheet != c->rwinfo->u.sheet)

			c->dependent_list = g_slist_prepend (c->dependent_list, l->data);

		else if (c->rwinfo->type == EXPR_REWRITE_WORKBOOK &&
			 dependent->sheet->workbook != c->rwinfo->u.workbook)

			c->dependent_list = g_slist_prepend (c->dependent_list, l->data);
	}
}

static void
cb_single_hash_to_list (gpointer key, gpointer value, gpointer closure)
{
	destroy_closure_t *c = closure;
	GList             *l;
	DependencySingle  *dep = value;

	for (l = dep->dependent_list; l; l = l->next) {
		Dependent *dependent = l->data;

		if      (c->rwinfo->type == EXPR_REWRITE_SHEET &&
			 dependent->sheet != c->rwinfo->u.sheet)

			c->dependent_list = g_slist_prepend (c->dependent_list, l->data);

		else if (c->rwinfo->type == EXPR_REWRITE_WORKBOOK &&
			 dependent->sheet->workbook != c->rwinfo->u.workbook)

			c->dependent_list = g_slist_prepend (c->dependent_list, l->data);
	}
}

static void
invalidate_refs (Dependent *dep, const ExprRewriteInfo *rwinfo)
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
 * do_deps_destroy :
 * Invalidate references of all kinds to the target region described by
 * @rwinfo.
 */
static void
do_deps_destroy (Sheet *sheet, const ExprRewriteInfo *rwinfo)
{
	DependencyData   *deps;
	destroy_closure_t c;

	g_return_if_fail (sheet != NULL);

	deps = sheet->deps;
	if (deps == NULL)
		return;

	c.rwinfo    = rwinfo;
	c.dependent_list = NULL;

	if (deps->range_hash) {
		g_hash_table_foreach (deps->range_hash,
				      &cb_range_hash_to_list, &c);

		while (c.dependent_list) {
			invalidate_refs (c.dependent_list->data, rwinfo);
			c.dependent_list = g_slist_remove (c.dependent_list, c.dependent_list->data);
		}

		g_hash_table_foreach_remove (deps->range_hash,
					     dependency_range_destroy,
					     NULL);

		g_hash_table_destroy (deps->range_hash);
		deps->range_hash = NULL;
	}

	c.dependent_list = NULL;
	if (deps->single_hash) {
		g_hash_table_foreach (deps->single_hash,
				      &cb_single_hash_to_list, &c);

		while (c.dependent_list) {
			invalidate_refs (c.dependent_list->data, rwinfo);
			c.dependent_list = g_slist_remove (c.dependent_list, c.dependent_list->data);
		}

		g_hash_table_foreach_remove (deps->single_hash,
					     dependency_single_destroy,
					     NULL);

		g_hash_table_destroy (deps->single_hash);
		deps->single_hash = NULL;
	}

	g_free (deps);
	sheet->deps = NULL;
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
	GList          *sheets, *l;
	ExprRewriteInfo rwinfo;

	g_return_if_fail (wb != NULL);

	rwinfo.type = EXPR_REWRITE_WORKBOOK;
	rwinfo.u.workbook = wb;

	sheets = workbook_sheets (wb);
	for (l = sheets; l; l = l->next)
		do_deps_destroy (l->data, &rwinfo);

	g_list_free (sheets);
}

void
cell_eval (Cell *cell)
{
	g_return_if_fail (cell != NULL);

	cell->base.generation = cell->base.sheet->workbook->generation;

	if (cell_has_expr (cell)) {
		GList *deps, *l;

		cell_eval_content (cell);

		deps = cell_get_dependencies (cell);

		for (l = deps; l; l = l->next) {
			Dependent *dep = l->data;

			if (dep->generation != dep->sheet->workbook->generation)
				dependent_queue_recalc (dep);
		}
		g_list_free (deps);
	}
}

static void
add_range_dep (DependencyData *deps, Dependent *dependent,
	       const DependencyRange      *range)
{
	/* Look it up */
	DependencyRange *result = g_hash_table_lookup (deps->range_hash, range);

	if (result) {
		/* Is the dependent already listed? */
		const GList *cl = g_list_find (result->dependent_list, dependent);
		if (cl)
			return;

		/* It was not: add it */
		result->dependent_list = g_list_prepend (result->dependent_list, dependent);

		return;
	}

	/* Create a new DependencyRange structure */
	result = g_new (DependencyRange, 1);
	*result = *range;
	result->dependent_list = g_list_prepend (NULL, dependent);

	g_hash_table_insert (deps->range_hash, result, result);
}

static void
drop_range_dep (DependencyData *deps, Dependent *dependent,
		const DependencyRange *const range)
{
	/* Look it up */
	DependencyRange *result;

	if (!deps)
		return;

	result = g_hash_table_lookup (deps->range_hash, range);

#ifdef DEBUG_EVALUATION
	if (dependency_debugging > 0) {
		const Range *r = &(range->range);
		printf ("Dropping range deps of ");,
		dependent_debug_name (dep, stdout);
		printf (" on range %s%d:",
			col_name (r->start.col),
			r->start.row + 1);
		printf ("%s%d\n",
			col_name (r->end.col),
			r->end.row + 1);
	}
#endif

	if (result) {
		GList *cl = g_list_find (result->dependent_list, dependent);

		if (!cl) {
/*			g_warning ("Range referenced twice + by some other cells"); */
			return;
		}

		result->dependent_list = g_list_remove_link (result->dependent_list, cl);
		g_list_free_1 (cl);

		if (!result->dependent_list) {
			g_hash_table_remove (deps->range_hash, result);
			g_free (result);
		}
	} else
/*		g_warning ("Unusual; range referenced twice in same formula")*/;
}

static void
dependency_range_ctor (DependencyRange *range, const CellPos *pos,
		       const CellRef   *a,  const CellRef *b)
{
	cell_get_abs_col_row (a, pos,
			      &range->range.start.col,
			      &range->range.start.row);
	cell_get_abs_col_row (b, pos,
			      &range->range.end.col,
			      &range->range.end.row);
	range_normalize (&range->range);

	range->dependent_list = NULL;
	if (b->sheet && a->sheet != b->sheet)
		g_warning ("FIXME: 3D references need work");
}

static void
handle_cell_single_dep (Dependent *dep, const CellPos *pos,
			const CellRef *a, DepOperation operation)
{
	DependencyData   *deps;
	DependencySingle *single;
	DependencySingle  lookup;

	if (a->sheet == NULL)
		deps = dep->sheet->deps;
	else
		deps = a->sheet->deps;

	if (!deps)
		return;

	/* Convert to absolute cordinates */
	cell_get_abs_col_row (a, pos, &lookup.pos.col, &lookup.pos.row);

	single = g_hash_table_lookup (deps->single_hash, &lookup);

	if (operation == ADD_DEPS) {
		if (single) {
			if (!g_list_find (single->dependent_list, dep))
				single->dependent_list =
					g_list_prepend (single->dependent_list, dep);
			else
				/* Referenced twice in the same formula */;
		} else {
			single  = g_new (DependencySingle, 1);
			*single = lookup;
			single->dependent_list = g_list_prepend (NULL, dep);
			g_hash_table_insert (deps->single_hash, single, single);
		}
	} else { /* Remove */
		if (single) {
			GList *l = g_list_find (single->dependent_list, dep);

			if (l) {
				single->dependent_list = g_list_remove_link (single->dependent_list, l);
				g_list_free_1 (l);

				if (!single->dependent_list) {
					g_hash_table_remove (deps->single_hash, single);
					g_free (single);
				}
			} else
				/* Referenced twice in the same formula */;
		} else
			/* Referenced twice and list killed already */;
	}
}

/*
 * Add the dependency of Dependent dep on the range
 * enclose by CellRef a and CellRef b
 *
 * We compute the location from @pos
 */
static void
handle_cell_range_deps (Dependent *dep, const CellPos *pos,
			const CellRef *a, const CellRef *b, DepOperation operation)
{
	DependencyRange range;
	DependencyData *depsa, *depsb;

	dependency_range_ctor (&range, pos, a, b);

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
handle_value_deps (Dependent *dep, const CellPos *pos,
		   const Value *value, DepOperation operation)
{
	switch (value->type) {
	case VALUE_EMPTY:
	case VALUE_STRING:
	case VALUE_INTEGER:
	case VALUE_FLOAT:
	case VALUE_BOOLEAN:
	case VALUE_ERROR:
		/* Constants are no dependencies */
		break;

		/* Check every element of the array */
		/* FIXME: currently array's only hold alphanumerics */
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
handle_tree_deps (Dependent *dep, const CellPos *pos,
		  ExprTree *tree, DepOperation operation)
{
	GList *l;

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
	case OPER_FUNCALL:
		for (l = tree->func.arg_list; l; l = l->next)
			handle_tree_deps (dep, pos, l->data, operation);
		return;

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
 * cell_add_dependencies:
 * @cell:
 *
 * This registers the dependencies for this cell
 * by scanning all of the references made in the
 * parsed expression.
 *
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

void
dependent_add_dependencies  (Dependent *dep, const CellPos *pos)
{
	g_return_if_fail (dep != NULL);
	g_return_if_fail (dep->sheet != NULL);
	g_return_if_fail (dep->sheet->deps != NULL);

	if (dep->expression != NULL)
		handle_tree_deps (dep, pos, dep->expression, ADD_DEPS);
}

void
dependent_drop_dependencies (Dependent *dep, const CellPos *pos)
{
	g_return_if_fail (dep != NULL);
	g_return_if_fail (dep->sheet != NULL);

	if (dep->expression != NULL)
		handle_tree_deps (dep, pos, dep->expression, REMOVE_DEPS);
}

typedef struct {
	int   col, row;
	Sheet *sheet;
	GList *list;
	gboolean cells_only;
} get_cell_dep_closure_t;

static void
search_cell_deps (gpointer key, gpointer value, gpointer closure)
{
	DependencyRange *deprange = key;
	Range *range = &(deprange->range);
	get_cell_dep_closure_t *c = closure;
	GList *l;

	/* No intersection is the common case */
	if (!range_contains (range, c->col, c->row))
		return;

	for (l = deprange->dependent_list; l; l = l->next) {
		Dependent *dep = l->data;

		c->list = g_list_prepend (c->list, dep);
	}
#ifdef DEBUG_EVALUATION
	if (dependency_debugging > 1) {
		printf ("Adding list: [\n");
		for (l = deprange->dependent_list; l; l = l->next) {
			Dependent *dep = l->data;

			dependent_debug_name (dep, stdout);
			printf ("(%d), ", cell->base.generation);
		}
		printf ("]\n");
	}
#endif
}

static GList *
cell_get_range_dependencies (const Cell *cell)
{
	get_cell_dep_closure_t closure;

	g_return_val_if_fail (cell != NULL, NULL);

	if (!cell->base.sheet->deps)
		return NULL;

	closure.col   = cell->pos.col;
	closure.row   = cell->pos.row;
	closure.sheet = cell->base.sheet;
	closure.list  = NULL;

	g_hash_table_foreach (cell->base.sheet->deps->range_hash,
			      &search_cell_deps, &closure);

	return closure.list;
}

static GList *
get_single_dependencies (const Sheet *sheet, int col, int row)
{
	DependencySingle  lookup, *single;
	DependencyData  *deps = sheet->deps;

	g_return_val_if_fail (deps != NULL, NULL);

	lookup.pos.col = col;
	lookup.pos.row = row;

	single = g_hash_table_lookup (deps->single_hash, &lookup);

#ifdef DEBUG_EVALUATION
	if (dependency_debugging > 0 && single) {
		printf ("Single dependencies on %s %d\n",
			cell_coord_name (col, row), g_list_length (l));

		dump_cell_list (single->dependent_list);
	}
#endif

	if (single)
		return g_list_copy (single->dependent_list);
	else
		return NULL;
}

GList *
cell_get_dependencies (const Cell *cell)
{
	GList *deps;

	if (!cell->base.sheet->deps)
		return NULL;

	deps = g_list_concat (cell_get_range_dependencies (cell),
			      get_single_dependencies (cell->base.sheet,
						       cell->pos.col,
						       cell->pos.row));
#ifdef DEBUG_EVALUATION
	if (dependency_debugging > 1) {
		printf ("There are %d dependencies for %s!%s\n",
			g_list_length (deps), cell->base.sheet->name,
			cell_name (cell));
	}
#endif

	return deps;
}

#if 0
void
cell_recalc_dependencies  (const Cell *cell)
{
	/* FIXME is there a way to write this without replicating alot of code ? */
}
#endif

static Dependent *
pick_next_dependent_from_queue (Workbook *wb)
{
	Dependent *dep;

	if (!wb->eval_queue)
		return NULL;

	dep = wb->eval_queue->data;
	wb->eval_queue = g_list_remove (wb->eval_queue, dep);
	if (!(dep->flags & DEPENDENT_QUEUED_FOR_RECALC))
		printf ("De-queued cell here\n");
	dep->flags &= ~DEPENDENT_QUEUED_FOR_RECALC;
	return dep;
}

/*
 * Increments the generation.  Every time the generation is
 * about to wrap around, we reset all of the cell counters to zero
 */
static void
workbook_next_generation (Workbook *wb)
{
	if (wb->generation == 255) {
		GList *dependent_list = wb->dependents;

		for (; dependent_list; dependent_list = dependent_list->next) {
			Dependent *dep = dependent_list->data;

			dep->generation = 0;
		}
		wb->generation = 1;
	} else
		wb->generation++;
}

/*
 * Computes all of the cells pending computation and
 * any dependency.
 */
void
workbook_recalc (Workbook *wb)
{
	int generation;
	Dependent *dep;

	workbook_next_generation (wb);
	generation = wb->generation;

	while ((dep = pick_next_dependent_from_queue (wb))) {
		if (dep->generation == generation)
			continue;

		dependent_eval (dep);
	}
}

/*
 * Recomputes all of the formulas.
 */
void
workbook_recalc_all (Workbook *wb)
{
	dependent_queue_recalc_list (wb->dependents, FALSE);
	workbook_recalc (wb);
	WORKBOOK_FOREACH_VIEW (wb, view,
		sheet_update (wb_view_cur_sheet (view)););
}

typedef struct {
	const Range *r;
	GList *list;
} get_range_dep_closure_t;

static void
search_range_deps (gpointer key, gpointer value, gpointer closure)
{
	DependencyRange *deprange  =  key;
	Range           *range     = &(deprange->range);
	get_range_dep_closure_t *c =  closure;

	if (!range_overlap (range, c->r))
		return;

	c->list = g_list_concat (c->list, g_list_copy (deprange->dependent_list));
}

/**
 * sheet_region_get_deps :
 * Get a list of the elements that depend on the specified range.
 *
 * @sheet : The sheet.
 * @range : The target range.
 */
GList *
sheet_region_get_deps (const Sheet *sheet, const Range *range)
{
	int ix, iy, end_row, end_col;
	get_range_dep_closure_t  closure;

	g_return_val_if_fail (sheet != NULL, NULL);

	closure.r    = range;
	closure.list = NULL;

	g_hash_table_foreach (sheet->deps->range_hash,
			      &search_range_deps, &closure);

	end_col = MIN (range->end.col, sheet->cols.max_used);
	end_row = MIN (range->end.row, sheet->rows.max_used);

	for (ix = range->start.col; ix <= end_col; ix++) {
		for (iy = range->start.row; iy <= end_row; iy++) {
			GList *l = get_single_dependencies (sheet, ix, iy);

			closure.list = g_list_concat (closure.list, l);
		}
	}

	return closure.list;
}

static void
cb_sheet_get_all_depends (gpointer key, gpointer value, gpointer closure)
{
	DependencyRange *deprange = key;
	GList          **deps     = closure;

	*deps = g_list_concat (*deps, g_list_copy (deprange->dependent_list));
}

static void
cb_single_get_all_depends (gpointer key, gpointer value, gpointer closure)
{
	DependencySingle *single = value;
	GList           **deps = closure;

	*deps = g_list_concat (*deps, g_list_copy (single->dependent_list));
}

/**
 * sheet_recalc_dependencies :
 * Queue a recalc of anything that depends on the cells in this sheet.
 * Do not actually recalc, just queue them up.
 *
 * @sheet : The sheet.
 */
void
sheet_recalc_dependencies (Sheet *sheet)
{
	GList *deps = NULL;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->deps != NULL);

	/* Find anything that depends on a range in this sheet */
	g_hash_table_foreach (sheet->deps->range_hash,
			      &cb_sheet_get_all_depends, &deps);

	/* Find anything that depends on a single reference within this sheet */
	g_hash_table_foreach (sheet->deps->single_hash,
			      &cb_single_get_all_depends, &deps);

	if (deps)
		dependent_queue_recalc_list (deps, TRUE);
}

/*
 * Ok; so we will have some new semantics;
 */

/*
DEPENDENT_QUEUED_FOR_RECALC will signify that the dependent is in fact in the
recalc list. This means this cell will have to be re-calculated, it
also means that _All_ its dependencies are also in the re-calc list.

Hence; whenever a dependent is added to the recalc list; its dependency
tree, must be progressively added to the list. Clearly any entries
marked 'DEPENDENT_QUEUED_FOR_RECALC' are already in there ( as are their
children ) so we can quickly and efficiently prune the tree.

The advantage of this is that we can dispense with the generation
scheme, with its costly linear reset ( even though amortized over
255 calculations it is an expense. ) We also have the _Luxury_ of
leaving things uncalculated with no loss of efficiency in the
queue. This will allow us to do far less re-calculating.

*/
