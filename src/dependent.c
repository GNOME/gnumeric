/*
 * eval.c:  Cell recomputation routines.
 * (C) 1998 The Free Software Foundation
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

static GHashTable *dependency_hash;

void
cell_eval (Cell *cell)
{
	char *error_msg;
	Value *v;

	g_return_if_fail (cell != NULL);

	if (cell->text)
		string_unref (cell->text);
	
	v = eval_expr (cell->sheet, cell->parsed_node,
		       cell->col->pos,
		       cell->row->pos,
		       &error_msg);

	if (cell->value)
		value_release (cell->value);
	
	if (v == NULL){
		cell->text = string_get (error_msg);
		cell->value = NULL;
	} else {
		/* FIXME: Use the format stuff */
		char *str = value_format (v, cell->style->format, NULL);
		
		cell->value = v;
		cell->text  = string_get (str);
		g_free (str);
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
	DependencyRange *r1 = (DependencyRange *) v;
	DependencyRange *r2 = (DependencyRange *) v2;

	if (r1->sheet != r2->sheet)
		return 0;
	if (r1->start_col != r2->start_col)
		return 0;
	if (r1->start_row != r2->start_row)
		return 0;
	if (r1->end_col != r2->end_col)
		return 0;
	if (r1->end_row != r2->end_row)
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

	return ((((r->start_row << 8) + r->end_row) << 8) +
		(r->start_col << 8) + (r->end_col));
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
add_cell_range_deps (Cell *cell, CellRef *a, CellRef *b)
{
	DependencyRange range, *result;
	int col = cell->col->pos;
	int row = cell->row->pos;

	/* Convert to absolute cordinates */
	cell_get_abs_col_row (a, col, row, &range.start_col, &range.start_row);
	cell_get_abs_col_row (b, col, row, &range.end_col,   &range.end_row);

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
add_value_deps (Cell *cell, Value *value)
{
	GList *l;
	
	switch (value->type){
	case VALUE_STRING:
	case VALUE_INTEGER:
	case VALUE_FLOAT:
		/* Constants are no dependencies */
		break;
		
		/* Check every element of the array */
	case VALUE_ARRAY:
		for (l = value->v.array; l; l = l->next)
			add_value_deps (cell, l->data);
		break;
		
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
	case OP_EQUAL:
	case OP_NOT_EQUAL:
	case OP_GT:
	case OP_GTE:
	case OP_LT:
	case OP_LTE:
	case OP_ADD:
	case OP_SUB:
	case OP_MULT:
	case OP_DIV:
	case OP_EXP:
	case OP_CONCAT:
		add_tree_deps (cell, tree->u.binary.value_a);
		add_tree_deps (cell, tree->u.binary.value_b);
		return;

	case OP_CONSTANT:
		add_value_deps (cell, tree->u.constant);
		return;

	case OP_FUNCALL: 
		for (l = tree->u.function.arg_list; l; l = l->next)
			add_tree_deps (cell, l->data);
		return;

	case OP_VAR: 
		add_cell_range_deps (
			cell,
			&tree->u.constant->v.cell,
			&tree->u.constant->v.cell);
		return;

	case OP_NEG:
		add_tree_deps (cell, tree->u.value);
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
	GList *list = range->cell_list;

	list = g_list_find (range->cell_list, the_cell);
	if (!list)
		return;

	range->cell_list = g_list_remove_link (range->cell_list, list);
	
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

	if (!dependency_hash)
		return;
	
	g_hash_table_foreach (dependency_hash, dependency_remove_cell, cell);

	/* Drop any unused DependencyRanges (because their ref_count reached zero) */
	if (remove_list){
		GList *l = remove_list;
		
		for (; l ; l = l->next){
			g_hash_table_remove (dependency_hash, l->data);
			g_free (l->data);
		}
		
		g_list_free (remove_list);
		remove_list = NULL;
	}
}

typedef struct {
	int   col;
	int   row;
	Sheet *sheet;
	GList *list;
} get_dep_closure_t;

static void
search_cell_deps (gpointer key, gpointer value, gpointer closure)
{
	DependencyRange *range = key;
	get_dep_closure_t *c = closure;
	GList *l;
	
	if (!((c->col >= range->start_col) &&
	      (c->col <= range->end_col)   &&
	      (c->row >= range->start_row) &&
	      (c->row <= range->end_row)   &&
	      (c->sheet = range->sheet)))
		return;

	for (l = range->cell_list; l; l = l->next){
		Cell *cell = l->data;

		c->list = g_list_prepend (c->list, cell);
	}
}

GList *
cell_get_dependencies (Sheet *sheet, int col, int row)
{
	get_dep_closure_t closure;

	if (!dependency_hash)
		dependency_hash_init ();
	
	closure.col = col;
	closure.row = row;
	closure.sheet = sheet;
	closure.list = NULL;

	g_hash_table_foreach (dependency_hash, &search_cell_deps, &closure);

	return closure.list;
}

void
cell_queue_recalc (Cell *cell)
{
	Workbook *wb;

	g_return_if_fail (cell != NULL);
	
	wb = ((Sheet *)cell->sheet)->workbook;
	wb->eval_queue = g_list_prepend (wb->eval_queue, cell);
}

void
cell_queue_recalc_list (GList *list)
{
	Workbook *wb;
	Cell *first_cell;
	
	if (!list)
		return;

	first_cell = list->data;
	wb = ((Sheet *)(first_cell->sheet))->workbook;

	wb->eval_queue = g_list_concat (wb->eval_queue, list);
}

static Cell *
pick_next_cell_from_queue (Workbook *wb)
{
	Cell *cell;

	if (!wb->eval_queue)
		return NULL;
	
	cell = wb->eval_queue->data;
	wb->eval_queue = g_list_remove (wb->eval_queue, cell);
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
	GList *deps, *l;

	workbook_next_generation (wb);
	generation = wb->generation;
	
	while ((cell = pick_next_cell_from_queue (wb))){
		cell->generation = generation;
		cell_eval (cell);
		deps = cell_get_dependencies (cell->sheet, cell->col->pos, cell->row->pos);

		for (l = deps ; l; l = l->next){
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
	GList *l;
	
	for (l = workbook->formula_cell_list; l; l = l->next){
		Cell *cell = l->data;

		cell_queue_recalc (cell);
	}
	workbook_recalc (workbook);
}


