/*
 * eval.c:  Cell recomputation routines.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 */

#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "eval.h"

#undef DEBUG_EVALUATION

static GHashTable *dependency_hash;

void
cell_eval (Cell *cell)
{
	Value *v;
	FunctionEvalInfo s;
	EvalPosition fp;

	g_return_if_fail (cell != NULL);

#ifdef DEBUG_EVALUATION
	{
		char *exprtxt = expr_decode_tree
			(cell->parsed_node, eval_pos_cell (&fp, cell));
		printf ("Evaluating %s: %s ->\n",
			cell_name (cell->col->pos, cell->row->pos),
			exprtxt);
		g_free (exprtxt);
	}
#endif

	v = (Value *)eval_expr (func_eval_info_cell (&s, cell), cell->parsed_node);

#ifdef DEBUG_EVALUATION
	{
		char *valtxt = v
			? value_get_as_string (v)
			: g_strdup ("NULL");
		printf ("Evaluating %s: -> %s\n",
			cell_name (cell->col->pos, cell->row->pos),
			valtxt);
		g_free (valtxt);
	}
#endif

	if (cell->value){
		value_release (cell->value);
		cell->value = NULL;
	}

	if (v == NULL){
		cell_set_rendered_text (cell, error_message_txt (s.error));
		cell->value = NULL;
		cell->flags |= CELL_ERROR;
	} else {
		cell->value = v;
		cell_render_value (cell);
		cell->flags &= ~CELL_ERROR;
	}

	cell_calc_dimensions (cell);

	sheet_redraw_cell_region (cell->sheet,
				  cell->col->pos, cell->row->pos,
				  cell->col->pos, cell->row->pos);
}

/*
 * Comparission function for the dependency hash table
 */
static gint
dependency_equal (gconstpointer v, gconstpointer v2)
{
	const DependencyRange *r1 = (const DependencyRange *) v;
	const DependencyRange *r2 = (const DependencyRange *) v2;

	if (r1->sheet != r2->sheet)
		return 0;
	if (r1->range.start_col != r2->range.start_col)
		return 0;
	if (r1->range.start_row != r2->range.start_row)
		return 0;
	if (r1->range.end_col != r2->range.end_col)
		return 0;
	if (r1->range.end_row != r2->range.end_row)
		return 0;

	return 1;
}

/*
 * Hash function for DependencyRange structures
 */
static guint
dependency_hash_func (gconstpointer v)
{
	const DependencyRange *r = v;

	return ((((r->range.start_row << 8) + r->range.end_row) << 8) +
		(r->range.start_col << 8) + (r->range.end_col));
}

/*
 * Initializes the hash table for the dependency ranges
 */
static void
dependency_hash_init (void)
{
	dependency_hash = g_hash_table_new (dependency_hash_func, dependency_equal);
}

/*
 * We add the dependency of Cell a in the ranges
 * enclose by CellRef a and CellRef b
 *
 * We compute the location from cell->row->pos and cell->col->pos
 */
static void
add_cell_range_deps (Cell *cell, const CellRef *a, const CellRef *b)
{
	DependencyRange range, *result;
	int col = cell->col->pos;
	int row = cell->row->pos;

	/* Convert to absolute cordinates */
	cell_get_abs_col_row (a, col, row, &range.range.start_col, &range.range.start_row);
	cell_get_abs_col_row (b, col, row, &range.range.end_col,   &range.range.end_row);

	range.ref_count = 0;
	range.sheet = cell->sheet;

	/* Look it up */
	result = g_hash_table_lookup (dependency_hash, &range);
	if (result){
		GList *cl;

		result->ref_count++;

		/* Is the cell already listed? */
		cl = g_list_find (result->cell_list, cell);
		if (cl)
			return;

		/* It was not: add it */
		result->cell_list = g_list_prepend (result->cell_list, cell);
		return;
	}

	/* Create a new DependencyRange structure */
	result = g_new (DependencyRange, 1);
	*result = range;
	result->ref_count = 1;
	result->cell_list = g_list_prepend (NULL, cell);

	g_hash_table_insert (dependency_hash, result, result);
}

/*
 * Adds the dependencies for a Value
 */
static void
add_value_deps (Cell *cell, const Value *value)
{
	switch (value->type){
	case VALUE_STRING:
	case VALUE_INTEGER:
	case VALUE_FLOAT:
		/* Constants are no dependencies */
		break;

		/* Check every element of the array */
	case VALUE_ARRAY:
	{
		int x, y;

		for (x = 0; x < value->v.array.x; x++)
			for (y = 0; y < value->v.array.y; y++)
				add_value_deps (cell,
						value->v.array.vals [x][y]);
		break;
	}
	case VALUE_CELLRANGE:
		add_cell_range_deps (
			cell,
			&value->v.cell_range.cell_a,
			&value->v.cell_range.cell_b);
		break;
	}
}

/*
 * This routine walks the expression tree looking for cell references
 * and cell range references.
 */
static void
add_tree_deps (Cell *cell, ExprTree *tree)
{
	GList *l;

	switch (tree->oper){
	case OPER_ANY_BINARY:
		add_tree_deps (cell, tree->u.binary.value_a);
		add_tree_deps (cell, tree->u.binary.value_b);
		return;

	case OPER_ANY_UNARY:
		add_tree_deps (cell, tree->u.value);
		return;

	case OPER_VAR:
		add_cell_range_deps (
			cell,
			&tree->u.ref,
			&tree->u.ref);
		return;

	case OPER_CONSTANT:
		add_value_deps (cell, tree->u.constant);
		return;

	case OPER_FUNCALL:
		for (l = tree->u.function.arg_list; l; l = l->next)
			add_tree_deps (cell, l->data);
		return;

	} /* switch */
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
	g_return_if_fail (cell->parsed_node != NULL);

	if (!dependency_hash)
		dependency_hash_init ();

	add_tree_deps (cell, cell->parsed_node);
}

/*
 * List used by cell_drop_dependencies and dependency_remove_cell
 * to accumulate all of the "dead" DependencyRange structures.
 *
 * Once the table_foreach process has finished, these are released
 */
static GList *remove_list;

static void
dependency_remove_cell (gpointer key, gpointer value, gpointer the_cell)
{
	DependencyRange *range = value;
	GList *list;

	list = g_list_find (range->cell_list, the_cell);
	if (!list)
		return;

	range->cell_list = g_list_remove_link (range->cell_list, list);
	g_list_free_1 (list);

	range->ref_count--;

	if (range->ref_count == 0)
		remove_list = g_list_prepend (remove_list, range);
}

/*
 * Remove the Cell from the DependecyRange hash tables
 */
void
cell_drop_dependencies (Cell *cell)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->parsed_node != NULL);

	if (!dependency_hash)
		return;

	g_hash_table_foreach (dependency_hash, dependency_remove_cell, cell);

	/* Drop any unused DependencyRanges (because their ref_count reached zero) */
	if (remove_list){
		GList *l = remove_list;

		for (; l; l = l->next){
			g_hash_table_remove (dependency_hash, l->data);
			g_free (l->data);
		}

		g_list_free (remove_list);
		remove_list = NULL;
	}
}

typedef struct {
	int   start_col, start_row;
	int   end_col, end_row;
	Sheet *sheet;
	GList *list;
} get_dep_closure_t;

static gboolean
intersects (Sheet *sheet, int col, int row, DependencyRange *range)
{
	if ((sheet == range->sheet) &&
	    range_contains (&range->range, col, row))
		return TRUE;

	return FALSE;
}

static void
search_cell_deps (gpointer key, gpointer value, gpointer closure)
{
	DependencyRange *range = key;
	get_dep_closure_t *c = closure;
	Sheet *sheet = c->sheet;
	GList *l;

	/* No intersection is the common case */

	if (!(intersects (sheet, c->start_col, c->start_row, range) ||
	      intersects (sheet, c->end_col,   c->end_row,   range) ||
	      intersects (sheet, c->start_col, c->end_row,   range) ||
	      intersects (sheet, c->end_col,   c->start_row, range)))
		return;

	for (l = range->cell_list; l; l = l->next){
		Cell *cell = l->data;

		c->list = g_list_prepend (c->list, cell);
	}
}

GList *
region_get_dependencies (Sheet *sheet, int start_col, int start_row, int end_col, int end_row)
{
	get_dep_closure_t closure;

	if (!dependency_hash)
		dependency_hash_init ();

	closure.start_col = start_col;
	closure.start_row = start_row;
	closure.end_col = end_col;
	closure.end_row = end_row;
	closure.sheet = sheet;
	closure.list = NULL;

	g_hash_table_foreach (dependency_hash, &search_cell_deps, &closure);

	return closure.list;
}

GList *
cell_get_dependencies (Sheet *sheet, int col, int row)
{
	get_dep_closure_t closure;

	if (!dependency_hash)
		dependency_hash_init ();

	closure.start_col = col;
	closure.start_row = row;
	closure.end_col = col;
	closure.end_row = row;
	closure.sheet = sheet;
	closure.list = NULL;

	g_hash_table_foreach (dependency_hash, &search_cell_deps, &closure);

	return closure.list;
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
void
workbook_next_generation (Workbook *wb)
{
	if (wb->generation == 255){
		GList *cell_list = wb->formula_cell_list;

		for (; cell_list; cell_list = cell_list->next){
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

	while ((cell = pick_next_cell_from_queue (wb))){
		GList *deps, *l;

		if (cell->generation == generation)
			continue;

		cell->generation = generation;
		cell_eval (cell);
		deps = cell_get_dependencies (cell->sheet, cell->col->pos, cell->row->pos);

		for (l = deps; l; l = l->next){
			Cell *one_cell;

			one_cell = l->data;
			if (one_cell->generation != generation)
				cell_queue_recalc (one_cell);
		}
		g_list_free (deps);
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
