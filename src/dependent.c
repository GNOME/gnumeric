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

static GList *dependency_hash;

void
sheet_compute_cell (Sheet *sheet, Cell *cell)
{
	char *error_msg;
	Value *v;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (cell != NULL);
	
	if (cell->text)
		string_unref_ptr (&cell->text);
	
	v = eval_expr (sheet, cell->parsed_node,
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
		char *str = value_string (v);
		
		cell->value = v;
		cell->text  = string_get (str);
		g_free (str);
	}
}

static void
sheet_recompute_one_cell (Sheet *sheet, int col, int row, Cell *cell)
{
	if (cell->parsed_node == NULL)
		return;

	printf ("recomputing %d %d\n", col, row);
	sheet_compute_cell (sheet, cell);
	sheet_redraw_cell_region (sheet,
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
dependency_hash (gconstpointer v)
{
	DependencyRange *r = v;

	return (((r->start_row << 8) + (r->end_row) << 8) +
		(r->start_col << 8) + (r->end_col));
}

/*
 * Initializes the hash table for the dependency ranges
 */
static void
dependency_hash_init (void)
{
	dependency_hash = g_hash_table_new (dependency_hash, dependency_equal);
}

/*
 * We add the dependency of Cell a in the ranges
 * enclose by CellRef a and CellRef b
 *
 * We compute the location from cell->row->pos and cell->col->pos
 */
static void
add_cell_range_deps (Sheet *sheet, Cell *cell, CellRef *a, CellRef *b)
{
	DependencyRange range, *result;
	int col = cell->col->pos;
	int row = cell->row->pos;

	/* Convert to absolute cordinates */
	cell_get_abs_col_row (a, col, row, &range.start_col, &range.start_row);
	cell_get_abs_col_row (b, col, row, &range.end_col,   &range.end_row);
	
	range.sheet = sheet;

	/* Look it up */
	result = g_hash_table_lookup (dependency_hash, &range);
	if (result){
		Cell *cell;

		result->ref_count++;

		/* Is the cell already listed? */
		cell = g_list_find (result->cell_list, cell);
		if (cell)
			return;

		/* It was not: add it */
		result->cell_list = g_list_prepend (result->cell_list, cell);
		return;
	}

	/* Create a new DependencyRange structure */ 
	result = g_new (DependencyRange, 1);
	*result = range;
	range.ref_count = 1;
	range.cell_list = g_list_insert (NULL, cell);
	
	g_hash_table_insert (dependency_hash, result, result);
}

/*
 * Adds the dependencies for a Value
 */
static void
add_value_deps (Sheet *sheet, Cell *cell, Value *value)
{
	GList *l;
	
	switch (v->type){
	case VALUE_STRING:
	case VALUE_INTEGER:
	case VALUE_FLOAT:
		/* Constants are no dependencies */
		break;
		
		/* Check every element of the array */
	case VALUE_ARRAY:
		for (l = value->v.array; l; l = l->next)
			add_value_deps (sheet, cell, l->data);
		break;
		
	case VALUE_CELLRANGE:
		add_cell_range_deps (
			sheet, cell,
			v->cell_range.cell_a,
			v->cell_range.cell_b);
		break;
	}
}

/*
 * This routine walks the expression tree looking for cell references
 * and cell range references.
 */
static void
add_tree_deps (Sheet *sheet, Cell *cell, ExprTree *tree)
{
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
		add_tree_deps (sheet, cell, tree->u.binary.value_a);
		add_tree_deps (sheet, cell, tree->u.binary.value_b);
		return;

	case OP_CONSTANT:
		add_value_deps (sheet, cell, tree->u.constant);
		return;

	case OP_FUNCALL: 
		for (l = tree->u.function.arg_list; l; l = l->next)
			add_tree_deps (sheet, cell, l->data);
		return;

	case OP_VAR: 
		add_cell_range_deps (
			sheet, cell,
			cell->u.constant->v.cell,
			cell->u.constant->v.cell);
		return;

	case OP_NEG:
		add_value_deps (sheet, cell, tree->u.value);
		return;
	
	} /* switch */
}

/*
 * This registers the dependencies for this cell
 * by scanning all of the references made in the
 * parsed expression.
 */
void
cell_add_dependencies (Cell *cell, Sheet *sheet)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (cell->parsed_node != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	
	if (!dependency_hash)
		dependency_hash_init ();

	add_tree_deps (sheet, cell, cell->parsed_node);
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

	/* Drop any unused DependencyRanges (because their ref_count reached zero */
	if (remove_list){
		GList *l = remove_list;
		
		for (; l ; l = l->next)
			g_free (l->data);
		
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
	DependencyRanges *range = key;
	get_dep_closure_t *c = closure;

	if (!((c->col >= range->start_col) &&
	      (c->col <= range->end_col)   &&
	      (c->row >= range->start_row) &&
	      (c->col <= range->end_row)   &&
	      (c->sheet = range->sheet)))
		return;

	for (l = range->cell_list; l; l = l->next)
		c->list = g_list_prepend (c->list, l->data);
}

GList *
cell_get_dependencies (Sheet *sheet, int col, int row)
{
	get_dep_closure_t closure;

	closure.col = col;
	closure.row = row;
	closure.sheet = sheet;
	closure.list = NULL;
	
	g_hash_table_foreach (dependency_hash, &search_cell_deps, closure);

	return closure.list;
}

sheet_queue_cell_compute (Sheet *sheet, Cell *cell)
{
}
