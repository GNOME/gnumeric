/*
 * eval.c:  Cell recomputation routines.
 *
 * Authors:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Michael Meeks   (mmeeks@gnu.org)
 */

#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "ranges.h"
#include "eval.h"
#include "main.h"

typedef enum {
	REMOVE_DEPS = 0,
	ADD_DEPS = 1
} DepOperation;

/*
 * A DependencyRange defines a range of cells whose values
 * are used by another Cell in the spreadsheet.
 *
 * A change in those cells will trigger a recomputation on the
 * cells listed in cell_list.
 */
typedef struct {
	/*
	 *  This range specifies uniquely the position of the
	 * cells that are depended on by the cell_list.
	 */
	Range  range;

	/* The list of cells that depend on this range */
	GList *cell_list;
} DependencyRange;

/*
 *  A DependencySingle stores a list of cells that depend
 * on the cell at @pos in @cell_list. NB. the EvalPosition
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
	GList  *cell_list;
} DependencySingle;

struct _DependencyData {
	/*
	 *   Large ranges hashed on 'range' to accelerate duplicate
	 * culling. This is tranversed by g_hash_table_foreach mostly.
	 */
	GHashTable *range_hash;
	/*
	 *   Single ranges, this maps an EvalPosition * to a GList of its
	 * dependencies.
	 */
	GHashTable *single_hash;
};

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

void
dependency_data_destroy (DependencyData *deps)
{
	if (deps) {
		if (deps->range_hash) {
			if (g_hash_table_size (deps->range_hash) != 0) {
				g_warning ("Dangling range dependencies");
			}
			g_hash_table_destroy (deps->range_hash);
		}
		deps->range_hash = NULL;

		if (deps->single_hash) {
			if (g_hash_table_size (deps->single_hash) != 0)
				g_warning ("Dangling single dependencies");
			g_hash_table_destroy (deps->single_hash);
		}
		deps->single_hash = NULL;

		g_free (deps);
	}
}

/**
 * cell_eval_content:
 * @cell: the cell to evaluate.
 * 
 * This function evaluates the contents of the cell.
 * 
 **/
void
cell_eval_content (Cell *cell)
{
	Value           *v;
	FunctionEvalInfo ei;

#ifdef DEBUG_EVALUATION
	if (dependency_debugging > 1) {
		ParsePosition pp;
		
		char *exprtxt = expr_decode_tree
			(cell->parsed_node, parse_pos_cell (&pp, cell));
		printf ("Evaluating %s: %s ->\n",
			cell_name (cell->col->pos, cell->row->pos),
			exprtxt);
		g_free (exprtxt);
	}
#endif

	v = eval_expr (func_eval_info_cell (&ei, cell),
		       cell->parsed_node);

#ifdef DEBUG_EVALUATION
	if (dependency_debugging > 1) {
		char *valtxt = v
			? value_get_as_string (v)
			: g_strdup ("NULL");
		printf ("Evaluating %s: -> %s\n",
			cell_name (cell->col->pos, cell->row->pos),
			valtxt);
		g_free (valtxt);
	}
#endif

	if (v == NULL)
		v = value_new_error (&ei.pos, "Internal error");

	if (cell->value)
		value_release (cell->value);
	cell->value = v;
	cell_render_value (cell);

	cell_calc_dimensions (cell);

	sheet_redraw_cell_region (cell->sheet,
				  cell->col->pos, cell->row->pos,
				  cell->col->pos, cell->row->pos);

}

void
cell_eval (Cell *cell)
{
	g_return_if_fail (cell != NULL);

	if (cell->generation == cell->sheet->workbook->generation)
		return;

	cell->generation = cell->sheet->workbook->generation;

	if (cell->parsed_node) {
		GList *deps, *l;
		
		cell_eval_content (cell);

		deps = cell_get_dependencies (cell);

		for (l = deps; l; l = l->next) {
			Cell *one_cell = l->data;
			
			if (one_cell->generation != cell->sheet->workbook->generation)
				cell_queue_recalc (one_cell);
		}
		g_list_free (deps);
	}
}

static void
add_cell_range_dep (DependencyData *deps, Cell *cell,
		    const DependencyRange * const range)
{
	/* Look it up */
	DependencyRange *result = g_hash_table_lookup (deps->range_hash, range);

	if (result) {
		/* Is the cell already listed? */
		GList const *cl = g_list_find (result->cell_list, cell);
		if (cl)
			return;

		/* It was not: add it */
		result->cell_list = g_list_prepend (result->cell_list, cell);

		return;
	}

	/* Create a new DependencyRange structure */
	result = g_new (DependencyRange, 1);
	*result = *range;
	result->cell_list = g_list_prepend (NULL, cell);

	g_hash_table_insert (deps->range_hash, result, result);
}

static void
drop_cell_range_dep (DependencyData *deps, Cell *cell,
		     const DependencyRange *const range)
{
	/* Look it up */
	DependencyRange *result = g_hash_table_lookup (deps->range_hash, range);

#ifdef DEBUG_EVALUATION
	if (dependency_debugging > 0) {
		Range const * const r = &(range->range);
		printf ("Dropping range deps of %s ",
			cell_name (cell->col->pos, cell->row->pos));
		printf ("on range %s%d:",
			col_name (r->start.col),
			r->start.row + 1);
		printf ("%s%d\n",
			col_name (r->end.col),
			r->end.row + 1);
	}
#endif

	if (result) {
		GList *cl = g_list_find (result->cell_list, cell);

		if (!cl) {
/*			g_warning ("Range referenced twice + by some other cells"); */
			return;
		}

		result->cell_list = g_list_remove_link (result->cell_list, cl);
		g_list_free_1 (cl);

		if (!result->cell_list) {
			g_hash_table_remove (deps->range_hash, result);
			g_free (result);
		}
	} else
/*		g_warning ("Unusual; range referenced twice in same formula")*/;
}

static void
dependency_range_ctor (DependencyRange * const range, Cell const * const cell,
		       CellRef const * const a, CellRef const * const b)
{
	CellPos pos;

	pos.col = cell->col->pos;
	pos.row = cell->row->pos;

	/* Convert to absolute cordinates */
	cell_get_abs_col_row (a, &pos, &range->range.start.col, &range->range.start.row);
	cell_get_abs_col_row (b, &pos, &range->range.end.col,   &range->range.end.row);

	range->cell_list = NULL;
	if (b->sheet && a->sheet != b->sheet)
		g_warning ("FIXME: 3D references need work");
}

static void
handle_cell_single_dep (Cell *cell, const CellRef *a,
			DepOperation operation)
{
	DependencyData   *deps = cell->sheet->deps;
	DependencySingle *single;
	DependencySingle  lookup;
	CellPos           pos;

	g_return_if_fail (deps != NULL);
	g_return_if_fail (a->sheet == NULL || a->sheet == cell->sheet);
	
	pos.col = cell->col->pos;
	pos.row = cell->row->pos;
	/* Convert to absolute cordinates */
	cell_get_abs_col_row (a, &pos, &lookup.pos.col, &lookup.pos.row);

	single = g_hash_table_lookup (deps->single_hash, &lookup);

	if (operation == ADD_DEPS) {
		if (single) {
			if (!g_list_find (single->cell_list, cell))
				single->cell_list = g_list_prepend (single->cell_list,
								    cell);
			else
				/* Referenced twice in the same formula */;
		} else {
			single  = g_new (DependencySingle, 1);
			*single = lookup;
			single->cell_list = g_list_prepend (NULL, cell);
			g_hash_table_insert (deps->single_hash, single, single);
		}
	} else { /* Remove */
		if (single) {
			GList *l = g_list_find (single->cell_list, cell);

			if (l) {
				single->cell_list = g_list_remove_link (single->cell_list, l);
				g_list_free_1 (l);

				if (!single->cell_list) {
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
 * We add the dependency of Cell a in the ranges
 * enclose by CellRef a and CellRef b
 *
 * We compute the location from cell->row->pos and cell->col->pos
 */
static void
handle_cell_range_deps (Cell *cell, const CellRef *a, const CellRef *b,
			DepOperation operation)
{
	DependencyRange range;
	gboolean        same_sheet;
	gboolean        single;

	same_sheet = (a->sheet == NULL ||
		      a->sheet == cell->sheet);
	single = (a == b);

	if (single && same_sheet) /* Single, simple range */

		handle_cell_single_dep (cell, a, operation);

	else {                   /* Large / inter-sheet range */
		DependencyData *deps;
		
		dependency_range_ctor (&range, cell, a, b);
		deps = eval_sheet (a->sheet, cell->sheet)->deps;
		if (operation)
			add_cell_range_dep  (deps, cell, &range);
		else
			drop_cell_range_dep (deps, cell, &range);
	}
}

/*
 * Adds the dependencies for a Value
 */
static void
handle_value_deps (Cell *cell, const Value *value, DepOperation operation)
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

		for (x = 0; x < value->v.array.x; x++)
			for (y = 0; y < value->v.array.y; y++)
				handle_value_deps (cell,
						   value->v.array.vals [x] [y],
						   operation);
		break;
	}
	case VALUE_CELLRANGE:
		handle_cell_range_deps (
			cell,
			&value->v.cell_range.cell_a,
			&value->v.cell_range.cell_b,
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
handle_tree_deps (Cell *cell, ExprTree *tree, DepOperation operation)
{
	GList *l;

	switch (tree->oper) {
	case OPER_ANY_BINARY:
		handle_tree_deps (cell, tree->u.binary.value_a, operation);
		handle_tree_deps (cell, tree->u.binary.value_b, operation);
		return;

	case OPER_ANY_UNARY:
		handle_tree_deps (cell, tree->u.value, operation);
		return;

	case OPER_VAR:
		handle_cell_range_deps (
			cell,
			&tree->u.ref,
			&tree->u.ref,
			operation);
		return;

	case OPER_CONSTANT:
		handle_value_deps (cell, tree->u.constant, operation);
		return;

	/*
	 * FIXME: needs to be taught implicit intersection +
	 * more cunning handling of argument type matching.
	 */
	case OPER_FUNCALL:
		for (l = tree->u.function.arg_list; l; l = l->next)
			handle_tree_deps (cell, l->data, operation);
		return;

	case OPER_NAME:
		if (tree->u.name->builtin) {
			/* FIXME: insufficiently flexible dependancy code (?) */
		} else
			handle_tree_deps (cell, tree->u.name->t.expr_tree, operation);
		return;

	case OPER_ARRAY:
		if (tree->u.array.x != 0 || tree->u.array.y != 0) {
			/* Non-corner cells depend on the corner */
			CellRef a;

			a.col_relative = a.row_relative = 0;
			a.sheet = cell->sheet;
			a.col   = cell->col->pos - tree->u.array.x;
			a.row   = cell->row->pos - tree->u.array.y;

			handle_cell_range_deps (cell, &a, &a, operation);
		} else
			/* Corner cell depends on the contents of the expr */
			handle_tree_deps (cell, tree->u.array.corner.func.expr,
					  operation);
		return;
	default:
		g_warning ("Unknown Operation type, dependencies lost");
		break;
	}
}

/*
 * This registers the dependencies for this cell
 * by scanning all of the references made in the
 * parsed expression.
 */
void
cell_add_dependencies (Cell *cell)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->sheet != NULL);
	g_return_if_fail (cell->sheet->deps != NULL);

	if (cell->parsed_node)
		handle_tree_deps (cell, cell->parsed_node, ADD_DEPS);
}

/*
 * Add a dependency on a CellRef iff the cell is not already dependent on the
 * cellref.
 *
 * @cell : The cell which will depend on.
 * @ref  : The row/col of the cell in the same sheet as cell to depend on.
 */
void
cell_add_explicit_dependency (Cell *cell, CellRef const *ref)
{
	g_warning ("Redundant cell_add_explicit_dependency function hacked");
}

/*
 * Remove the Cell from the DependencyRange hash tables
 */
void
cell_drop_dependencies (Cell *cell)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->sheet != NULL);
	g_return_if_fail (cell->sheet->deps != NULL);

	if (cell->parsed_node)
		handle_tree_deps (cell, cell->parsed_node, REMOVE_DEPS);
}

typedef struct {
	int   col, row;
	Sheet *sheet;
	GList *list;
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

	for (l = deprange->cell_list; l; l = l->next) {
		Cell *cell = l->data;

		c->list = g_list_prepend (c->list, cell);
	}
#ifdef DEBUG_EVALUATION
	if (dependency_debugging > 1) {
		printf ("Adding list: [\n");
		for (l = deprange->cell_list; l; l = l->next) {
			Cell *cell = l->data;
			
			printf (" %s(%d), ", cell_name (cell->col->pos, cell->row->pos),
				cell->generation);
		}
		printf ("]\n");
	}
#endif
}

static GList *
cell_get_range_dependencies (Cell *cell)
{
	get_cell_dep_closure_t closure;

	g_return_val_if_fail (cell != NULL, NULL);
	g_return_val_if_fail (cell->sheet->deps != NULL, NULL);

	closure.col   = cell->col->pos;
	closure.row   = cell->row->pos;
	closure.sheet = cell->sheet;
	closure.list  = NULL;

	g_hash_table_foreach (cell->sheet->deps->range_hash,
			      &search_cell_deps, &closure);

	return closure.list;
}

static GList *
get_single_dependencies (Sheet *sheet, int col, int row)
{
	DependencySingle  lookup, *single;
	DependencyData  *deps = sheet->deps;

	g_return_val_if_fail (deps != NULL, NULL);

	lookup.pos.col = col;
	lookup.pos.row = row;

	single = g_hash_table_lookup (deps->single_hash, &lookup);

#ifdef DEBUG_EVALUATION
	if (dependency_debugging > 0 && single) {
		GList *l = single->cell_list;

		printf ("Single dependencies on %s %d\n",
			cell_name (col, row), g_list_length (l));

		while (l) {
			Cell *dep_cell = l->data;
			printf ("%s\n",
				cell_name (dep_cell->col->pos,
					   dep_cell->row->pos));
			l = g_list_next (l);
		}
	}
#endif

	if (single)
		return g_list_copy (single->cell_list);
	else
		return NULL;
}

GList *
cell_get_dependencies (Cell *cell)
{
	GList *deps;

	deps = g_list_concat (cell_get_range_dependencies (cell),
			      get_single_dependencies (cell->sheet,
						       cell->col->pos,
						       cell->row->pos));
#ifdef DEBUG_EVALUATION
	if (dependency_debugging > 1) {
		printf ("There are %d dependencies for %s!%s\n",
			g_list_length (deps), cell->sheet->name,
			cell_name (cell->col->pos, cell->row->pos));
	}
#endif

	return deps;
}

/*
 * cell_queue_recalc:
 * @cell: the cell that contains the formula that must be recomputed
 *
 * Queues the cell @cell for recalculation.
 */
void
cell_queue_recalc (Cell *cell)
{
	Workbook *wb;

	g_return_if_fail (cell != NULL);

	if (cell->flags & CELL_QUEUED_FOR_RECALC)
		return;

#ifdef DEBUG_EVALUATION
	if (dependency_debugging > 2)
		printf ("Queuing: %s\n", cell_name (cell->col->pos,
						    cell->row->pos));
#endif
	wb = cell->sheet->workbook;
	wb->eval_queue = g_list_prepend (wb->eval_queue, cell);
	cell->flags |= CELL_QUEUED_FOR_RECALC;
}

/*
 * cell_unqueue_from_recalc:
 * @cell: the cell to remove from the recomputation queue
 *
 * Removes a cell that has been previously added to the recomputation
 * queue.  Used internally when a cell that was queued no longer contains
 * a formula.
 */
void
cell_unqueue_from_recalc (Cell *cell)
{
	Workbook *wb;

	g_return_if_fail (cell != NULL);

	if (!(cell->flags & CELL_QUEUED_FOR_RECALC))
		return;

	wb = cell->sheet->workbook;
	wb->eval_queue = g_list_remove (wb->eval_queue, cell);
	cell->flags &= ~CELL_QUEUED_FOR_RECALC;
}

void
cell_queue_recalc_list (GList *list, gboolean freelist)
{
	Workbook *wb;
	Cell *first_cell;
	GList *list0 = list;

	if (!list)
		return;

	first_cell = list->data;
	wb = first_cell->sheet->workbook;

	while (list) {
		Cell *cell = list->data;
		list = list->next;

		if (cell->flags & CELL_QUEUED_FOR_RECALC)
			continue;

#ifdef DEBUG_EVALUATION
	if (dependency_debugging > 2)
		printf ("Queuing: %s\n", cell_name (cell->col->pos, cell->row->pos));
#endif
		wb->eval_queue = g_list_prepend (wb->eval_queue, cell);

		cell->flags |= CELL_QUEUED_FOR_RECALC;
	}

	if (freelist)
		g_list_free (list0);
}

static Cell *
pick_next_cell_from_queue (Workbook *wb)
{
	Cell *cell;

	if (!wb->eval_queue)
		return NULL;

	cell = wb->eval_queue->data;
	wb->eval_queue = g_list_remove (wb->eval_queue, cell);
	if (!(cell->flags & CELL_QUEUED_FOR_RECALC))
		printf ("De-queued cell here\n");
	cell->flags &= ~CELL_QUEUED_FOR_RECALC;
	return cell;
}

/*
 * Increments the generation.  Every time the generation is
 * about to wrap around, we reset all of the cell counters to zero
 */
static void
workbook_next_generation (Workbook *wb)
{
	if (wb->generation == 255) {
		GList *cell_list = wb->formula_cell_list;

		for (; cell_list; cell_list = cell_list->next) {
			Cell *cell = cell_list->data;

			cell->generation = 0;
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
	Cell *cell;

	workbook_next_generation (wb);
	generation = wb->generation;

	while ((cell = pick_next_cell_from_queue (wb))) {
		if (cell->generation == generation)
			continue;

		cell_eval (cell);
	}
}

/*
 * Recomputes all of the formulas.
 */
void
workbook_recalc_all (Workbook *workbook)
{
	cell_queue_recalc_list (workbook->formula_cell_list, FALSE);
	workbook_recalc (workbook);
}

static void
dump_cell_list (GList *l)
{
	printf ("(");
	for (; l; l = l->next) {
		Cell *cell = l->data;
		printf ("%s!%s, ", cell->sheet->name,
			cell_name (cell->col->pos, cell->row->pos));
	}
	printf (")\n");
}

static void
dump_range_dep (gpointer key, gpointer value, gpointer closure)
{
	DependencyRange const * const deprange = key;
	Range const * const range = &(deprange->range);

	/* 2 calls to col_name.  It uses a static buffer */
	printf ("\t%s%d:",
		col_name (range->start.col), range->start.row + 1);
	printf ("%s%d -> ",
		col_name (range->end.col), range->end.row + 1);

	dump_cell_list (deprange->cell_list);
}

static void
dump_single_dep (gpointer key, gpointer value, gpointer closure)
{
	DependencySingle *dep = key;

	/* 2 calls to col_name.  It uses a static buffer */
	printf ("\t%s -> ", cell_name (dep->pos.col, dep->pos.row));

	dump_cell_list (dep->cell_list);
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
			sheet->name);

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
		GList *l = sheet->workbook->eval_queue;

		printf ("Unevaluated cells on queue:\n");
		while (l) {
			Cell *cell = l->data;
			printf ("%s!%s\n", cell->sheet->name,
				cell_name (cell->col->pos, cell->row->pos));
			l = l->next;
		}
	}
}

typedef struct {
	Sheet *sheet;
	GList *list;
} get_intersheet_dep_closure_t;

static void
search_intersheet_deps (gpointer key, gpointer value, gpointer closure)
{
	DependencyRange *deprange = key;
	get_intersheet_dep_closure_t *c = closure;
	GList *l;

	for (l = deprange->cell_list; l; l = l->next) {
		Cell *cell = l->data;

		if (cell->sheet != c->sheet)
			c->list = g_list_prepend (c->list, cell);
	}
}

GList *
sheet_get_intersheet_deps (Sheet *sheet)
{
	get_intersheet_dep_closure_t closure;

	g_return_val_if_fail (sheet->deps != NULL, NULL);

	closure.sheet = sheet;
	closure.list = NULL;

	g_hash_table_foreach (sheet->deps->range_hash,
			      &search_intersheet_deps, &closure);

	return closure.list;	
}

typedef struct {
	Range r;
	GList *list;
} get_range_dep_closure_t;

static void
search_range_deps (gpointer key, gpointer value, gpointer closure)
{
	DependencyRange *deprange  =  key;
	Range           *range     = &(deprange->range);
	get_range_dep_closure_t *c =  closure;
	GList                   *l;

	if (!range_overlap (range, &c->r))
		return;

	/* concat a copy of depend list */
	for (l = deprange->cell_list; l; l = l->next)
		c->list = g_list_prepend (c->list, l->data);
}

GList *
sheet_region_get_deps (Sheet *sheet, int start_col, int start_row,
		       int end_col,  int end_row)
{
	int                      ix, iy;
	get_range_dep_closure_t  closure;

	g_return_val_if_fail (sheet != NULL, NULL);

	closure.r.start.col = start_col;
	closure.r.start.row = start_row;
	closure.r.end.col   = end_col;
	closure.r.end.row   = end_row;
	closure.list        = NULL;

	g_hash_table_foreach (sheet->deps->range_hash,
			      &search_range_deps, &closure);

	/*
	 * FIXME : Only an existing cell can depend on things.
	 * we should clip this.
	 */
	for (ix = start_col; ix <= end_col; ix++) {
		for (iy = start_row; iy <= end_row; iy++) {
			GList *l = get_single_dependencies (sheet, ix, iy);

			closure.list = g_list_concat (closure.list, l);
		}
	}

	return closure.list;
}

static void
cb_sheet_get_all_depends (gpointer key, gpointer value, gpointer closure)
{
	DependencyRange *deprange  =  key;
	GList 		*l, *res = *((GList **)closure);

	/* concat a copy of depend list */
	for (l = deprange->cell_list; l; l = l->next)
		res = g_list_prepend (res, l->data);
	*((GList **)closure) = res;
}

static void
cb_cell_get_all_depends (gpointer key, gpointer value, gpointer closure)
{
	Cell	*cell = (Cell *) value;
	GList *l = get_single_dependencies (cell->sheet,
					    cell->col->pos,
					    cell->row->pos);
	*((GList **)closure) = g_list_concat (l, *((GList **)closure));
}

/**
 * sheet_recalc_dependencies :
 * Force a recalc of anything that depends on the cells in this sheet.
 *
 * @sheet : The sheet.
 *
 * This seems like over kill we could probably use a finer grained test.
 */
void
sheet_recalc_dependencies (Sheet *sheet)
{
	GList *deps = NULL;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* Find anything that depends on something in this sheet */
	g_hash_table_foreach (sheet->deps->range_hash,
			      &cb_sheet_get_all_depends, &deps);

	/* Find anything that depends on existing cells. */
	g_hash_table_foreach (sheet->cell_hash,
			      &cb_cell_get_all_depends, &deps);

	if (deps) {
		cell_queue_recalc_list (deps, TRUE);
		workbook_recalc (sheet->workbook);
	}
}

