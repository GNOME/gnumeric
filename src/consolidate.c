/*
 * consolidate.c : Implementation of the data consolidation feature.
 *
 * Copyright (C) Almer S. Tigelaar <almer@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
 
#include <config.h>
#include "cell.h"
#include "eval.h"
#include "func.h"
#include "position.h"
#include "ranges.h"
#include "selection.h"
#include "sheet.h"
#include "str.h"
#include "workbook.h"

#include "consolidate.h"

/**********************************************************************************
 * UTILITY ROUTINES
 **********************************************************************************/

/**
 * get_bounding_box:
 * @granges: A list with globalranges
 * @box: A reference to a range, this parameter
 *       must be set and will be modified to reflect
 *       the bounding box
 *
 * calculate the bounding box of all the global ranges
 * combined and put this into the extent parameter.
 * The origin is always (0,0). The result specifies the
 * size of the region not the exact placement.
 **/
static void
get_bounding_box (GSList const *granges, Range *box)
{
	GSList const *l;
	int max_x, max_y;

	g_return_if_fail (granges != NULL);
	g_return_if_fail (box != NULL);
	
	max_x = max_y = 0;
	for (l = granges; l != NULL; l = l->next) {
		GlobalRange *gr = l->data;
		int ext_x = gr->range.end.col - gr->range.start.col;
		int ext_y = gr->range.end.row - gr->range.start.row;

		g_return_if_fail (range_is_sane (&gr->range));

		if (ext_x > max_x)
			max_x = ext_x;
		if (ext_y > max_y)
			max_y = ext_y;
	}

	box->start.row = box->start.col = 0;
	box->end.col = max_x;
	box->end.row = max_y;
}

/**
 * set_cell_value:
 * @sheet: Sheet on which the cell is located
 * @col: Column index of the cell
 * @row: Row index of the cell
 * @value: The expression to put in the cell
 *
 * Set the expression of a cell optionally converting
 * it to a value.
 **/
static void
set_cell_expr (Sheet *sheet, int const col, int const row, ExprTree *expr)
{
	Cell *cell;

	g_return_if_fail (expr != NULL);
	
	cell = sheet_cell_fetch (sheet, col, row);
	cell_set_expr (cell, expr, NULL);
}

/**
 * set_cell_value:
 * @sheet: Sheet on which the cell is located
 * @col: Column index of the cell
 * @row: Row index of the cell
 * @value: The value to put in the cell
 *
 * Set the value of a cell.
 **/
static void
set_cell_value (Sheet *sheet, int const col, int const row, Value const *value)
{
	/* There are cases in which value can be NULL */
	if (value) {
		Cell *cell;
		
		cell = sheet_cell_fetch (sheet, col, row);
		cell_set_value (cell, value_duplicate (value), NULL);
	}
}

static void
redraw_respan_and_select (Sheet *sheet, int start_col, int start_row, int end_col, int end_row,
			  gboolean values)
{
	Range r;

	range_init (&r, start_col, start_row, end_col, end_row);
	sheet_range_calc_spans (sheet, &r, SPANCALC_RESIZE | SPANCALC_RENDER);
	sheet_region_queue_recalc (sheet, &r);
	
	if (values) {
		int row, col;
		
		workbook_recalc (sheet->workbook);
		for (row = start_row; row <= end_row; row++) {
			for (col = start_col; col <= end_col; col++) {
				Cell *cell = sheet_cell_fetch (sheet, col, row);

				if (cell_has_expr (cell))
					cell_convert_expr_to_value (cell);
			}
		}
	}
	
	sheet_redraw_range (sheet, &r);	
	sheet_selection_set (sheet, end_col, end_row,
			     start_col, start_row,
			     end_col, end_row);	
}

static int
cb_value_compare (Value const *a, Value const *b)
{
	ValueCompare vc = value_compare (a, b, TRUE);

	switch (vc) {
	case IS_EQUAL: return 0;
	case IS_LESS: return -1;
	case IS_GREATER: return 1;
	case TYPE_MISMATCH: return 1; /* Push time mismatches to the end */
	default :
		g_warning ("Unknown value comparison result");
	}

	return 0;
}

/**********************************************************************************/

Consolidate *
consolidate_new (void)
{
	Consolidate *cs;
	
	cs = g_new0 (Consolidate, 1);
	cs->fd = NULL;
	cs->dst = NULL;
	cs->src = NULL;
	cs->mode = CONSOLIDATE_PUT_VALUES;
	
	return cs;
}

void
consolidate_free (Consolidate *cs)
{
	GSList *l;

	g_return_if_fail (cs != NULL);
		
	if (cs->fd) {
		func_unref (cs->fd);
		cs->fd = NULL;
	}
	if (cs->dst) {
		global_range_free (cs->dst);
		cs->dst = NULL;
	}

	for (l = cs->src; l != NULL; l = l->next)
		global_range_free ((GlobalRange *) l->data);
	g_slist_free (cs->src);
	cs->src = NULL;

	g_free (cs);
}

void
consolidate_set_function (Consolidate *cs, FunctionDefinition *fd)
{
	g_return_if_fail (cs != NULL);
	g_return_if_fail (fd != NULL);
	
	if (cs->fd)
		func_unref (cs->fd);

	cs->fd = fd;
	func_ref (fd);
}

void
consolidate_set_mode (Consolidate *cs, ConsolidateMode mode)
{
	g_return_if_fail (cs != NULL);

	cs->mode = mode;
}

gboolean
consolidate_set_destination (Consolidate *cs, Sheet *sheet, Range const *r)
{
	GlobalRange *new;
	GSList const *l;
	
	g_return_val_if_fail (cs != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (r != NULL, FALSE);

	new = global_range_new (sheet, r);
	
	/*
	 * Don't allow the destination to overlap
	 * with any of the source ranges
	 */
	for (l = cs->src; l != NULL; l = l->next) {
		GlobalRange const *gr = l->data;

		if (global_range_overlap (new, gr)) {
			global_range_free (new);
			return FALSE;
		}
	}
	
	if (cs->dst)
		global_range_free (cs->dst);
	cs->dst = new;
	
	return TRUE;
}

gboolean
consolidate_add_source (Consolidate *cs, Sheet *sheet, Range const *r)
{
	GlobalRange *tmp, *new;

	g_return_val_if_fail (cs != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (r != NULL, FALSE);

	/*
	 * Make sure the range that is added doesn't overlap
	 * with the destination range
	 *
	 * In reality we only use the start col/row of
	 * the destination range. Given that the size of result
	 * of the consolidation can never exceed the size of the
	 * any of the source ranges (it may be smaller, but never
	 * larger) we do a strict sanity check here.
	 */
	tmp = global_range_dup (cs->dst);
	tmp->range.end.col = tmp->range.start.col + (r->end.col - r->start.col);
	tmp->range.end.row = tmp->range.start.row + (r->end.row - r->start.row);

	new = global_range_new (sheet, r);
	
	if (global_range_overlap (tmp, new)) {
		global_range_free (new);
		global_range_free (tmp);
		return FALSE;
	}
	global_range_free (tmp);
	
	cs->src = g_slist_append (cs->src, new);

	return TRUE;
}

/**********************************************************************************
 * TREE MANAGEMENT/RETRIEVAL
 **********************************************************************************/

typedef struct {
	Value const *key;
	GSList      *val;
} TreeItem;

static int
cb_tree_free (Value const *key, TreeItem *ti, gpointer user_data)
{
	g_return_val_if_fail (key != NULL, FALSE);

	/*
	 * No need to release the ti->key!, it's const.
	 */

	if (ti->val) {
		GSList *l;
		
		for (l = ti->val; l != NULL; l = l->next)
			global_range_free ((GlobalRange *) l->data);

		g_slist_free (ti->val);
	}
	g_free (ti);

	return FALSE;
}

static void
tree_free (GTree *tree)
{
	g_tree_traverse (tree, (GTraverseFunc) cb_tree_free,
			 G_IN_ORDER, NULL);
	g_tree_destroy (tree);
}

/**
 * retrieve_row_tree:
 * 
 * This routine traverses the whole source list
 * of regions and puts all rows in regions which
 * have a similar key (the key is the first column
 * in a row) together.
 *
 * For example, you have (from a single source region, or multiple):
 * A  1
 * B  1
 * A  1
 *
 * This will be put in the tree like :
 * A  2
 * B  1
 *
 * This routine can also be called with "with_ranges" set to false,
 * in that case the ranges will not be stored, but only the unique
 * row keys.
 *
 * The tree will be sorted automatically.
 **/
static GTree *
retrieve_row_tree (Consolidate *cs)
{
	GTree *tree;
	GSList *l;
	
	g_return_val_if_fail (cs != NULL, NULL);

	tree = g_tree_new ((GCompareFunc) cb_value_compare);

	for (l = cs->src; l != NULL; l = l->next) {
		GlobalRange const *sgr = l->data;
		int row;

		for (row = sgr->range.start.row; row <= sgr->range.end.row; row++) {
			Value const *v = sheet_cell_get_value (sgr->sheet, sgr->range.start.col, row);

			if (v && v->type != VALUE_EMPTY) {
				GlobalRange *gr;
				GSList *granges;
				TreeItem *ti;
				Range s;
			
				ti = g_tree_lookup (tree, (Value *) v);
				
				if (ti)
					granges = ti->val;
				else
					granges = NULL;
					
				s.start.row = s.end.row = row;
				s.start.col = sgr->range.start.col + 1;
				s.end.col   = sgr->range.end.col;

				gr = global_range_new (sgr->sheet, &s);
				granges = g_slist_append (granges, gr);
				
				/*
				 * NOTE: There is no need to duplicate the value
				 * as it will not change during the consolidation
				 * operation. We simply store it as const
				 */
				if (!ti) {
					ti = g_new0 (TreeItem, 1);
				        ti->key = v;
				}
				ti->val = granges;
				
				g_tree_insert (tree, (Value *) ti->key, ti);
			}
		}
	}

	return tree;
}

/**
 * Same as retrieve_row_tree, but for cols
 **/
static GTree *
retrieve_col_tree (Consolidate *cs)
{
	GTree *tree;
	GSList *l;
	
	g_return_val_if_fail (cs != NULL, NULL);

	tree = g_tree_new ((GCompareFunc) cb_value_compare);
	
	for (l = cs->src; l != NULL; l = l->next) {
		GlobalRange const *sgr = l->data;
		int col;

		for (col = sgr->range.start.col; col <= sgr->range.end.col; col++) {
			Value const *v = sheet_cell_get_value (sgr->sheet, col, sgr->range.start.row);
			
			if (v && v->type != VALUE_EMPTY) {
				GlobalRange *gr;
				GSList *granges;
				TreeItem *ti;
				Range s;

				ti = g_tree_lookup (tree, (Value *) v);
				
				if (ti)
					granges = ti->val;
				else
					granges = NULL;
					
				s.start.col = s.end.col = col;
				s.start.row = sgr->range.start.row + 1;
				s.end.row   = sgr->range.end.row;

				gr = global_range_new (sgr->sheet, &s);
				granges = g_slist_append (granges, gr);

				/*
				 * NOTE: There is no need to duplicate the value
				 * as it will not change during the consolidation
				 * operation. We simply store it as const
				 */
				if (!ti) {
					ti = g_new0 (TreeItem, 1);
				        ti->key = v; 
				}
				ti->val = granges;
				
				g_tree_insert (tree, (Value *) ti->key, ti);
			}
		}
	}
	
	return tree;
}

static gboolean
cb_key_find (Value const *current, Value const *wanted)
{
	return !(value_compare (current, wanted, TRUE) == IS_EQUAL);
}

static GSList *
key_list_get (Consolidate *cs, gboolean is_cols)
{
	GSList *keys = NULL;
	GSList *l;
	
	for (l = cs->src; l != NULL; l = l->next) {
		GlobalRange *sgr = l->data;
		int i = is_cols
			? sgr->range.start.col
			: sgr->range.start.row;
		int max = is_cols
			? sgr->range.end.col
			: sgr->range.end.row;

		/*
		 * NOTE: We always need to skip the first col/row
		 * because it's situated in the corner and it is not
		 * a label!
		 * Keep into account that this only the case
		 * for col/row consolidations.
		 */
		i++;
		for (; i <= max; i++) {
			Value const *v = sheet_cell_get_value (sgr->sheet,
							       is_cols ? i : sgr->range.start.col,
							       is_cols ? sgr->range.start.row : i);
			/*
			 * We avoid adding duplicates, this list needs to contain unique keys,
			 * also we treat the value as a constant, we don't duplicate it. It will
			 * not change during the consolidation.
			 */
			if (v && v->type != VALUE_EMPTY
			    && g_slist_find_custom (keys, (Value *) v, (GCompareFunc) cb_key_find) == NULL)
				keys = g_slist_insert_sorted (keys, (Value *) v, (GCompareFunc) cb_value_compare);
		}
	}
	
	return keys;
}

/**********************************************************************************
 * CONSOLIDATION ROUTINES
 **********************************************************************************/

/**
 * This routine consolidates all the ranges in source into
 * the region dst using the function "fd" on all the overlapping
 * src regions.
 *
 * Example :
 * function is  : SUM
 * src contains : A1:B2 (4 cells containing all 1's) and A3:B4 (4 cells containing all 2's).
 *
 * The consolidated result will be :
 * 3  3
 * 3  3
 *
 * Note that the topleft of the result will be put at the topleft of the
 * destination FullRange. Currently no clipping is done on the destination
 * range so the dst->range->end.* are ignored. (clipping isn't terribly
 * useful either)
 **/ 
static void
simple_consolidate (FunctionDefinition *fd, GlobalRange const *dst, GSList const *src,
		    gboolean is_col_or_row, int *end_col, int *end_row)
{
	GSList const *l;
	Range box;
	Sheet *prev_sheet = NULL;
	RangeRef *prev_r = NULL;
	int x, y;
	
	g_return_if_fail (fd != NULL);
	g_return_if_fail (dst != NULL);
	g_return_if_fail (src != NULL);

	/*
	 * We first deduct the full bounding box of all ranges combined
	 * this is needed so we know how far to traverse in total
	 */
	get_bounding_box (src, &box);
	if (dst->range.start.row + box.end.row >= SHEET_MAX_ROWS)
		box.end.row = SHEET_MAX_ROWS - 1;
	if (dst->range.start.col + box.end.col >= SHEET_MAX_COLS)
		box.end.col = SHEET_MAX_COLS - 1;
		
	for (y = box.start.row; y <= box.end.row; y++) {
		for (x = box.start.col; x <= box.end.col; x++) {
			ExprList *args = NULL;
			
			for (l = src; l != NULL; l = l->next) {
				GlobalRange const *gr = l->data;
				Value *val;
				Range r;
				
				/*
				 * We don't want to include this range
				 * this time if the current traversal
				 * offset falls out of it's bounds
				 */
				if (gr->range.start.row + y > gr->range.end.row ||
				    gr->range.start.col + x > gr->range.end.col)
					continue;

				r.start.col = r.end.col = gr->range.start.col + x;
				r.start.row = r.end.row = gr->range.start.row + y;

				/*
				 * If possible add it to the previous range
				 * this looks nicer for the formula's and can
				 * save us much allocations which in turn
				 * improves performance. Don't remove!
				 * NOTE: Only for col/row consolidation!
				 * This won't work for simple consolidations.
				 */
				if (is_col_or_row && prev_sheet == gr->sheet) {
					if (prev_r->a.row == r.start.row
					    && prev_r->b.row == r.start.row
					    && prev_r->b.col + 1 == r.start.col) {
						prev_r->b.col++;
						continue;
					} else if (prev_r->a.col == r.start.col
						   && prev_r->b.col == r.start.col
						   && prev_r->b.row + 1 == r.start.row) {
						prev_r->b.row++;
						continue;
					}
				}

				val = value_new_cellrange_r (gr->sheet, &r);
				prev_r = &val->v_range.cell;
				prev_sheet = gr->sheet;
				
				args = expr_list_append (args, expr_tree_new_constant (val));
			}
			
			/* There is no need to free 'args', it will be absorbed
			 * into the ExprTree
			 */
			if (args) {
				ExprTree *expr;

				expr = expr_tree_new_funcall (fd, args);
				set_cell_expr (dst->sheet, dst->range.start.col + x,
					       dst->range.start.row + y, expr);
				expr_tree_unref (expr);
			}
		}
	}

	if (end_row)
		*end_row = box.end.row + dst->range.start.row;
	if (end_col)
		*end_col = box.end.col + dst->range.start.col;
}

typedef struct {
	Consolidate *cs;
	GlobalRange *colrow;
} ConsolidateContext;

/**
 * row_consolidate_row:
 *
 * Consolidates a list of regions which all specify a single
 * row and share the same key into a single target range.
 **/
static int
cb_row_tree (Value const *key, TreeItem *ti, ConsolidateContext *cc)
{
	Consolidate *cs;
	int end_col = 0;
	
	cs = cc->cs;
	if (cs->mode & CONSOLIDATE_COPY_LABELS)
		set_cell_value (cs->dst->sheet, cc->colrow->range.start.col - 1,
				cc->colrow->range.start.row, key);
	simple_consolidate (cs->fd, cc->colrow, ti->val, FALSE, &end_col, NULL);
	
	if (end_col > cc->colrow->range.end.col)
		cc->colrow->range.end.col = end_col;
		
	cc->colrow->range.start.row++;
	cc->colrow->range.end.row++;

	return FALSE;
}

/**
 * row_consolidate:
 *
 * High level routine for row consolidation, retrieves
 * the row (name) hash and uses a callback routine to do a
 * simple consolidation for each row.
 **/
static void
row_consolidate (Consolidate *cs)
{
	ConsolidateContext cc;
	GTree *tree;
	
	g_return_if_fail (cs != NULL);

	tree = retrieve_row_tree (cs);
	cc.cs = cs;
	cc.colrow = global_range_dup (cs->dst);
	
	if (cs->mode & CONSOLIDATE_COPY_LABELS) {
		cc.colrow->range.start.col++;
		cc.colrow->range.end.col++;
	}

	g_tree_traverse (tree, (GTraverseFunc) cb_row_tree,
			 G_IN_ORDER, &cc);
	
	redraw_respan_and_select (cs->dst->sheet,
				  cs->dst->range.start.col,
				  cs->dst->range.start.row,
				  cc.colrow->range.end.col,
				  cc.colrow->range.end.row - 1,
				  cs->mode & CONSOLIDATE_PUT_VALUES);
				 
	global_range_free (cc.colrow);
	cc.colrow = NULL;

	tree_free (tree);
}

/**
 * cb_col_tree
 *
 * Consolidates a list of regions which all specify a single
 * column and share the same key into a single target range.
 **/
static int
cb_col_tree (Value const *key, TreeItem *ti, ConsolidateContext *cc)
{
	Consolidate *cs;
	int end_row = 0;
	
	cs = cc->cs;
	
	if (cs->mode & CONSOLIDATE_COPY_LABELS)
		set_cell_value (cs->dst->sheet,
				cc->colrow->range.start.col,
				cc->colrow->range.start.row - 1,
				key);
			       
	simple_consolidate (cs->fd, cc->colrow, ti->val, FALSE, NULL, &end_row);

	if (end_row > cc->colrow->range.end.row)
		cc->colrow->range.end.row = end_row;

	cc->colrow->range.start.col++;
	cc->colrow->range.end.col++;

	return FALSE;
}

/**
 * col_consolidate:
 *
 * High level routine for column consolidation, retrieves
 * the column (name) hash and uses a callback routine to do a
 * simple consolidation for each column.
 **/
static void
col_consolidate (Consolidate *cs)
{
	ConsolidateContext cc;
	GTree *tree;
	
	g_return_if_fail (cs != NULL);
	tree = retrieve_col_tree (cs);

	cc.cs = cs;
	cc.colrow = global_range_dup (cs->dst);

	if (cs->mode & CONSOLIDATE_COPY_LABELS) {
		cc.colrow->range.start.row++;
		cc.colrow->range.end.row++;
	}

	g_tree_traverse (tree, (GTraverseFunc) cb_col_tree,
			 G_IN_ORDER, &cc);
	
	redraw_respan_and_select (cs->dst->sheet,
				  cs->dst->range.start.col,
				  cs->dst->range.start.row,
				  cc.colrow->range.end.col - 1,
				  cc.colrow->range.end.row,
				  cs->mode & CONSOLIDATE_PUT_VALUES);

	global_range_free (cc.colrow);
	cc.colrow = NULL;

	tree_free (tree);
}

static ExprList *
colrow_formula_args_build (Value const *row_name, Value const *col_name, GSList *granges)
{
	GSList const *l;
	ExprList *args = NULL;
	
	for (l = granges; l != NULL; l = l->next) {
		GlobalRange *gr = l->data;
		int rx, ry;

		/*
		 * Now walk trough the entire area the range
		 * covers and attempt to find cells that have
		 * the right row_name and col_name keys. If such
		 * a cell is found we append it to the formula
		 */
		for (ry = gr->range.start.row + 1; ry <= gr->range.end.row; ry++) {
			Value const *rowtxt = sheet_cell_get_value (gr->sheet, gr->range.start.col, ry);

			if (rowtxt == NULL || value_compare (rowtxt, row_name, TRUE) != IS_EQUAL)
				continue;

			for (rx = gr->range.start.col + 1; rx <= gr->range.end.col; rx++) {
				Value const *coltxt = sheet_cell_get_value (gr->sheet, rx, gr->range.start.row);
				CellRef ref;
				
				if (coltxt == NULL || value_compare (coltxt, col_name, TRUE) != IS_EQUAL)
					continue;

				ref.sheet = gr->sheet;
				ref.col = rx;
				ref.row = ry;
				ref.col_relative = ref.row_relative = FALSE;

				args = expr_list_append (args, expr_tree_new_var (&ref));
			}

		}
	}

	return args;
}

static void
colrow_consolidate (Consolidate *cs)
{
	GSList *rows;
	GSList *cols;
	GSList const *l;
	GSList const *m;
	int x = 0;
	int y = 0;

	g_return_if_fail (cs != NULL);
	
	rows = key_list_get (cs, FALSE);
	cols = key_list_get (cs, TRUE);

	if (cs->mode & CONSOLIDATE_COPY_LABELS)
		y = 1;
	else
		y = 0;

	for (l = rows; l != NULL && cs->dst->range.start.row + y < SHEET_MAX_ROWS; l = l->next, y++) {
		Value const *row_name = l->data;
		
		if (cs->mode & CONSOLIDATE_COPY_LABELS) {
			set_cell_value (cs->dst->sheet,
					cs->dst->range.start.col,
					cs->dst->range.start.row + y,
					row_name);
			x = 1;
		} else
			x = 0;

		for (m = cols; m != NULL && cs->dst->range.start.col + x < SHEET_MAX_COLS; m = m->next, x++) {
			Value const *col_name = m->data;
			ExprList *args;

			if (cs->mode & CONSOLIDATE_COPY_LABELS) {
				set_cell_value (cs->dst->sheet,
						cs->dst->range.start.col + x,
						cs->dst->range.start.row,
						col_name);
			}

			args = colrow_formula_args_build (row_name, col_name, cs->src);

			if (args) {
				ExprTree *expr = expr_tree_new_funcall (cs->fd, args);
				
				set_cell_expr (cs->dst->sheet, cs->dst->range.start.col + x,
					       cs->dst->range.start.row + y, expr);
				expr_tree_unref (expr);
			}
		}
	}

	redraw_respan_and_select (cs->dst->sheet,
				  cs->dst->range.start.col,
				  cs->dst->range.start.row,
				  cs->dst->range.end.col + x - 1,
				  cs->dst->range.end.row + y - 1,
				  cs->mode & CONSOLIDATE_PUT_VALUES);

	/*
	 * No need to free the values in these
	 * lists, they are constpointers.
	 */
	g_slist_free (rows);
	g_slist_free (cols);
}

/**
 * consolidate_get_dest_bounding_box:
 *
 * Retrieve the exact maximum area that the result of consolidation
 * can cover
 **/
Range
consolidate_get_dest_bounding_box (Consolidate *cs)
{
	Range r;
	
	range_init (&r, 0, 0, 0, 0);
	
	g_return_val_if_fail (cs != NULL, r);
	
	if (cs->src)
		get_bounding_box (cs->src, &r);

	if (range_translate (&r, cs->dst->range.start.col, cs->dst->range.start.row))
		range_ensure_sanity (&r);

	return r;
}

void
consolidate_apply (Consolidate *cs)
{
	g_return_if_fail (cs != NULL);

	/*
	 * All parameters must be set, it's not
	 * a critical error if one of them is not set,
	 * we just don't do anything in that case
	 */
	if (!cs->fd || !cs->dst || !cs->src)
		return;

	if ((cs->mode & CONSOLIDATE_ROW_LABELS) && (cs->mode & CONSOLIDATE_COL_LABELS))
		colrow_consolidate (cs);
	else if (cs->mode & CONSOLIDATE_ROW_LABELS)
		row_consolidate (cs);
	else if (cs->mode & CONSOLIDATE_COL_LABELS)
		col_consolidate (cs);
	else {
		int end_row, end_col;
		
		simple_consolidate (cs->fd, cs->dst, cs->src, FALSE, &end_col, &end_row);
		redraw_respan_and_select (cs->dst->sheet,
					  cs->dst->range.start.col,
					  cs->dst->range.start.row,
					  end_col, end_row,
					  cs->mode & CONSOLIDATE_PUT_VALUES);
	}
}
