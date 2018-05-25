/*
 * consolidate.c : Implementation of the data consolidation feature.
 *
 * Copyright (C) Almer S. Tigelaar <almer@gnome.org>
 * Copyright (C) Andreas J Guelzow <aguelzow@taliesin.ca>
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <glib/gi18n-lib.h>
#include <consolidate.h>

#include <cell.h>
#include <dependent.h>
#include <expr.h>
#include <func.h>
#include <position.h>
#include <ranges.h>
#include <selection.h>
#include <sheet.h>
#include <value.h>
#include <workbook.h>
#include <workbook-control.h>


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
get_bounding_box (GSList const *granges, GnmRange *box)
{
	GSList const *l;
	GnmSheetRange const *gr;
	int ext_x, ext_y, max_x, max_y;

	g_return_if_fail (granges != NULL);
	g_return_if_fail (box != NULL);

	max_x = max_y = 0;
	for (l = granges; l != NULL; l = l->next) {
		gr = l->data;

		g_return_if_fail (range_is_sane (&gr->range));

		if ((ext_x = gr->range.end.col - gr->range.start.col) > max_x)
			max_x = ext_x;
		if ((ext_y = gr->range.end.row - gr->range.start.row) > max_y)
			max_y = ext_y;
	}

	box->start.row = box->start.col = 0;
	box->end.col = max_x;
	box->end.row = max_y;
}


static int
cb_value_compare (GnmValue const *a, GnmValue const *b)
{
	GnmValDiff vc = value_compare (a, b, TRUE);

	switch (vc) {
	case IS_EQUAL: return 0;
	case IS_LESS: return -1;
	case IS_GREATER: return 1;
	case TYPE_MISMATCH: return 1; /* Push time mismatches to the end */
	default:
		g_warning ("Unknown value comparison result");
	}

	return 0;
}

/**********************************************************************************/

GnmConsolidate *
gnm_consolidate_new (void)
{
	GnmConsolidate *cs;

	cs = g_new0 (GnmConsolidate, 1);
	cs->fd = NULL;
	cs->src = NULL;
	cs->mode = CONSOLIDATE_PUT_VALUES;
	cs->ref_count = 1;

	return cs;
}

void
gnm_consolidate_free (GnmConsolidate *cs, gboolean content_only)
{
	GSList *l;

	g_return_if_fail (cs != NULL);

	if (cs->ref_count-- > 1)
		return;
	if (cs->fd) {
		gnm_func_dec_usage (cs->fd);
		cs->fd = NULL;
	}

	for (l = cs->src; l != NULL; l = l->next)
		gnm_sheet_range_free ((GnmSheetRange *) l->data);
	g_slist_free (cs->src);
	cs->src = NULL;

	if (!content_only)
		g_free (cs);
}

static GnmConsolidate *
gnm_consolidate_ref (GnmConsolidate *cs)
{
	cs->ref_count++;
	return cs;
}

static void
gnm_consolidate_unref (GnmConsolidate *cs)
{
	cs->ref_count--;
	if (cs->ref_count == 0)
		gnm_consolidate_free (cs, TRUE);
}

GType
gnm_consolidate_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmConsolidate",
			 (GBoxedCopyFunc)gnm_consolidate_ref,
			 (GBoxedFreeFunc)gnm_consolidate_unref);
	}
	return t;
}

void
gnm_consolidate_set_function (GnmConsolidate *cs, GnmFunc *fd)
{
	g_return_if_fail (cs != NULL);
	g_return_if_fail (fd != NULL);

	if (cs->fd)
		gnm_func_dec_usage (cs->fd);

	cs->fd = fd;
	gnm_func_inc_usage (fd);
}

void
gnm_consolidate_set_mode (GnmConsolidate *cs, GnmConsolidateMode mode)
{
	g_return_if_fail (cs != NULL);

	cs->mode = mode;
}

gboolean
gnm_consolidate_check_destination (GnmConsolidate *cs, data_analysis_output_t *dao)
{
	GnmSheetRange *new;
	GnmRange r;
	GSList const *l;

	g_return_val_if_fail (cs != NULL, FALSE);
	g_return_val_if_fail (dao != NULL, FALSE);

	if (dao->type == NewSheetOutput || dao->type == NewWorkbookOutput)
		return TRUE;

	range_init (&r, dao->start_col, dao->start_row,
		    dao->start_col + dao->cols - 1,
		    dao->start_row + dao->rows - 1);
	new = gnm_sheet_range_new (dao->sheet, &r);

	for (l = cs->src; l != NULL; l = l->next) {
		GnmSheetRange const *gr = l->data;

		if (gnm_sheet_range_overlap (new, gr)) {
			gnm_sheet_range_free (new);
			return FALSE;
		}
	}

	gnm_sheet_range_free (new);
	return TRUE;
}

gboolean
gnm_consolidate_add_source (GnmConsolidate *cs, GnmValue *range)
{
	GnmSheetRange *new;

	g_return_val_if_fail (cs != NULL, FALSE);
	g_return_val_if_fail (range != NULL, FALSE);

	new = g_new (GnmSheetRange, 1);

	new->sheet = range->v_range.cell.a.sheet;
	range_init_value (&new->range, range);
	value_release (range);

	cs->src = g_slist_append (cs->src, new);

	return TRUE;
}

/**********************************************************************************
 * TREE MANAGEMENT/RETRIEVAL
 **********************************************************************************/

typedef struct {
	GnmValue const *key;
	GSList      *val;
} TreeItem;

static int
cb_tree_free (GnmValue const *key, TreeItem *ti,
	      G_GNUC_UNUSED gpointer user_data)
{
	g_return_val_if_fail (key != NULL, FALSE);

	/*
	 * No need to release the ti->key!, it's const.
	 */

	if (ti->val) {
		GSList *l;

		for (l = ti->val; l != NULL; l = l->next)
			gnm_sheet_range_free ((GnmSheetRange *) l->data);

		g_slist_free (ti->val);
	}
	g_free (ti);

	return FALSE;
}

static void
tree_free (GTree *tree)
{
	g_tree_foreach (tree, (GTraverseFunc) cb_tree_free, NULL);
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
 * This will be put in the tree like:
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
retrieve_row_tree (GnmConsolidate *cs)
{
	GTree *tree;
	GSList *l;
	GnmSheetRange *gr;
	TreeItem *ti;
	GnmRange s;

	g_return_val_if_fail (cs != NULL, NULL);

	tree = g_tree_new ((GCompareFunc) cb_value_compare);

	for (l = cs->src; l != NULL; l = l->next) {
		GnmSheetRange const *sgr = l->data;
		int row;

		for (row = sgr->range.start.row; row <= sgr->range.end.row; row++) {
			GnmValue const *v = sheet_cell_get_value (sgr->sheet, sgr->range.start.col, row);

			if (!VALUE_IS_EMPTY (v)) {
				if (NULL == (ti = g_tree_lookup (tree, (gpointer) v))) {
					/* NOTE: There is no need to duplicate
					 * the value as it will not change
					 * during the consolidation operation.
					 * We simply store it as const */
					ti = g_new0 (TreeItem, 1);
					ti->key = v;
					ti->val = NULL;
				}

				s.start.col = sgr->range.start.col + 1;
				s.end.col   = sgr->range.end.col;
				if (s.end.col >= s.start.col) {
					s.start.row = s.end.row = row;
					gr = gnm_sheet_range_new (sgr->sheet, &s);
					ti->val = g_slist_append (ti->val, gr);
				}
				g_tree_insert (tree, (GnmValue *) ti->key, ti);
			}
		}
	}

	return tree;
}

/**
 * retrieve_col_tree:
 *
 * Same as retrieve_row_tree, but for cols
 **/
static GTree *
retrieve_col_tree (GnmConsolidate *cs)
{
	GTree *tree;
	GSList *l;

	g_return_val_if_fail (cs != NULL, NULL);

	tree = g_tree_new ((GCompareFunc) cb_value_compare);

	for (l = cs->src; l != NULL; l = l->next) {
		GnmSheetRange const *sgr = l->data;
		int col;

		for (col = sgr->range.start.col; col <= sgr->range.end.col; col++) {
			GnmValue const *v = sheet_cell_get_value (sgr->sheet, col, sgr->range.start.row);

			if (!VALUE_IS_EMPTY (v)) {
				GnmSheetRange *gr;
				GSList *granges;
				TreeItem *ti;
				GnmRange s;

				ti = g_tree_lookup (tree, (GnmValue *) v);

				if (ti)
					granges = ti->val;
				else
					granges = NULL;

				s.start.col = s.end.col = col;
				s.start.row = sgr->range.start.row + 1;
				s.end.row   = sgr->range.end.row;

				gr = gnm_sheet_range_new (sgr->sheet, &s);
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

				g_tree_insert (tree, (GnmValue *) ti->key, ti);
			}
		}
	}

	return tree;
}

static gboolean
cb_key_find (GnmValue const *current, GnmValue const *wanted)
{
	return !(value_compare (current, wanted, TRUE) == IS_EQUAL);
}

static GSList *
key_list_get (GnmConsolidate *cs, gboolean is_cols)
{
	GSList *keys = NULL;
	GSList *l;

	for (l = cs->src; l != NULL; l = l->next) {
		GnmSheetRange *sgr = l->data;
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
			GnmValue const *v = sheet_cell_get_value (sgr->sheet,
							       is_cols ? i : sgr->range.start.col,
							       is_cols ? sgr->range.start.row : i);
			/*
			 * We avoid adding duplicates, this list needs to contain unique keys,
			 * also we treat the value as a constant, we don't duplicate it. It will
			 * not change during the consolidation.
			 */
			if (!VALUE_IS_EMPTY (v) &&
			    g_slist_find_custom (keys, (GnmValue *) v, (GCompareFunc) cb_key_find) == NULL)
				keys = g_slist_insert_sorted (keys, (GnmValue *) v, (GCompareFunc) cb_value_compare);
		}
	}

	return keys;
}

/**********************************************************************************
 * CONSOLIDATION ROUTINES
 **********************************************************************************/

/**
 * simple_consolidate:
 *
 * This routine consolidates all the ranges in source into
 * the region dst using the function "fd" on all the overlapping
 * src regions.
 *
 * Example:
 * function is  : SUM
 * src contains : A1:B2 (4 cells containing all 1's) and A3:B4 (4 cells containing all 2's).
 *
 * The consolidated result will be:
 * 3  3
 * 3  3
 *
 * Note that the topleft of the result will be put at the topleft of the
 * destination FullRange. Currently no clipping is done on the destination
 * range so the dst->range->end.* are ignored. (clipping isn't terribly
 * useful either)
 **/
static void
simple_consolidate (GnmFunc *fd, GSList const *src,
		    gboolean is_col_or_row,
		    data_analysis_output_t *dao)
{
	GSList const *l;
	GnmRange box;
	Sheet *prev_sheet = NULL;
	GnmRangeRef *prev_r = NULL;
	int x, y;

	g_return_if_fail (fd != NULL);
	g_return_if_fail (src != NULL);

	get_bounding_box (src, &box);
	for (y = box.start.row; y <= box.end.row; y++) {
		for (x = box.start.col; x <= box.end.col; x++) {
			GnmExprList *args = NULL;

			for (l = src; l != NULL; l = l->next) {
				GnmSheetRange const *gr = l->data;
				GnmValue *val;
				GnmRange r;

				/*
				 * We don't want to include this range
				 * this time if the current traversal
				 * offset falls out of its bounds
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

				args = gnm_expr_list_append (args, gnm_expr_new_constant (val));
			}

			if (args)
				dao_set_cell_expr (dao, x, y,
					gnm_expr_new_funcall (fd, args));
		}
	}
}

typedef struct {
	GnmConsolidate *cs;
	data_analysis_output_t *dao;
	WorkbookControl *wbc;
} ConsolidateContext;

/**
 * row_consolidate_row:
 *
 * Consolidates a list of regions which all specify a single
 * row and share the same key into a single target range.
 **/
static int
cb_row_tree (GnmValue const *key, TreeItem *ti, ConsolidateContext *cc)
{
	GnmConsolidate *cs = cc->cs;

	if (cs->mode & CONSOLIDATE_COPY_LABELS)
		dao_set_cell_value (cc->dao, -1, 0, value_dup (key));

	simple_consolidate (cs->fd, ti->val, FALSE, cc->dao);

	cc->dao->offset_col++;

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
row_consolidate (GnmConsolidate *cs, data_analysis_output_t *dao)
{
	ConsolidateContext cc;
	GTree *tree;

	g_return_if_fail (cs != NULL);

	tree = retrieve_row_tree (cs);
	cc.cs = cs;
	cc.dao = dao;

	if (cs->mode & CONSOLIDATE_COPY_LABELS)
		dao->offset_col++;

	g_tree_foreach (tree, (GTraverseFunc) cb_row_tree, &cc);

	tree_free (tree);
}

/**
 * cb_col_tree:
 *
 * Consolidates a list of regions which all specify a single
 * column and share the same key into a single target range.
 **/
static gboolean
cb_col_tree (GnmValue const *key, TreeItem *ti, ConsolidateContext *cc)
{
	GnmConsolidate *cs = cc->cs;

	if (cs->mode & CONSOLIDATE_COPY_LABELS)
		dao_set_cell_value (cc->dao, 0, -1, value_dup (key));

	simple_consolidate (cs->fd, ti->val, FALSE, cc->dao);

	cc->dao->offset_col++;

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
col_consolidate (GnmConsolidate *cs, data_analysis_output_t *dao)
{
	ConsolidateContext cc;
	GTree *tree;

	g_return_if_fail (cs != NULL);

	tree = retrieve_col_tree (cs);

	cc.cs = cs;
	cc.dao = dao;

	if (cs->mode & CONSOLIDATE_COPY_LABELS)
		dao->offset_row++;

	g_tree_foreach (tree, (GTraverseFunc) cb_col_tree, &cc);

	tree_free (tree);
}

static GnmExprList *
colrow_formula_args_build (GnmValue const *row_name, GnmValue const *col_name, GSList *granges)
{
	GSList const *l;
	GnmExprList *args = NULL;

	for (l = granges; l != NULL; l = l->next) {
		GnmSheetRange *gr = l->data;
		int rx, ry;

		/*
		 * Now walk trough the entire area the range
		 * covers and attempt to find cells that have
		 * the right row_name and col_name keys. If such
		 * a cell is found we append it to the formula
		 */
		for (ry = gr->range.start.row + 1; ry <= gr->range.end.row; ry++) {
			GnmValue const *rowtxt = sheet_cell_get_value (gr->sheet, gr->range.start.col, ry);

			if (rowtxt == NULL || value_compare (rowtxt, row_name, TRUE) != IS_EQUAL)
				continue;

			for (rx = gr->range.start.col + 1; rx <= gr->range.end.col; rx++) {
				GnmValue const *coltxt = sheet_cell_get_value (gr->sheet, rx, gr->range.start.row);
				GnmCellRef ref;

				if (coltxt == NULL || value_compare (coltxt, col_name, TRUE) != IS_EQUAL)
					continue;

				ref.sheet = gr->sheet;
				ref.col = rx;
				ref.row = ry;
				ref.col_relative = ref.row_relative = FALSE;

				args = gnm_expr_list_append (args, gnm_expr_new_cellref (&ref));
			}

		}
	}

	return args;
}

static void
colrow_consolidate (GnmConsolidate *cs, data_analysis_output_t *dao)
{
	GSList *rows;
	GSList *cols;
	GSList const *l;
	GSList const *m;
	int x;
	int y;

	g_return_if_fail (cs != NULL);

	rows = key_list_get (cs, FALSE);
	cols = key_list_get (cs, TRUE);

	if (cs->mode & CONSOLIDATE_COPY_LABELS) {
		for (l = rows, y = 1; l != NULL; l = l->next, y++) {
			GnmValue const *row_name = l->data;

			dao_set_cell_value (dao, 0, y, value_dup (row_name));
		}
		for (m = cols, x = 1; m != NULL; m = m->next, x++) {
			GnmValue const *col_name = m->data;

			dao_set_cell_value (dao, x, 0, value_dup (col_name));
		}
		dao->offset_col = 1;
		dao->offset_row = 1;
	}

	for (l = rows, y = 0; l != NULL; l = l->next, y++) {
		GnmValue const *row_name = l->data;

		for (m = cols, x = 0; m != NULL; m = m->next, x++) {
			GnmValue const *col_name = m->data;
			GnmExprList *args;

			args = colrow_formula_args_build (row_name, col_name, cs->src);

			if (args) {
				GnmExpr const *expr = gnm_expr_new_funcall (cs->fd, args);

				dao_set_cell_expr (dao, x, y, expr);
			}
		}
	}

	g_slist_free (rows);
	g_slist_free (cols);
}

static gboolean
consolidate_apply (GnmConsolidate *cs,
		   data_analysis_output_t *dao)
{
/*	WorkbookView *wbv = wb_control_view (wbc); */

	g_return_val_if_fail (cs != NULL, TRUE);

	/*
	 * All parameters must be set, it's not
	 * a critical error if one of them is not set,
	 * we just don't do anything in that case
	 */
	if (!cs->fd || !cs->src)
		return TRUE;

	if ((cs->mode & CONSOLIDATE_ROW_LABELS) && (cs->mode & CONSOLIDATE_COL_LABELS))
		colrow_consolidate (cs, dao);
	else if (cs->mode & CONSOLIDATE_ROW_LABELS)
		row_consolidate (cs, dao);
	else if (cs->mode & CONSOLIDATE_COL_LABELS)
		col_consolidate (cs, dao);
	else
		simple_consolidate (cs->fd, cs->src, FALSE, dao);
	dao_redraw_respan (dao);
	return FALSE;
}



gboolean
gnm_tool_consolidate_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
			 analysis_tool_engine_t selector, gpointer result)
{
	GnmConsolidate *cs = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Consolidating to (%s)"),
						result) == NULL);
	case TOOL_ENGINE_UPDATE_DAO:
	{
		GnmRange r;

		range_init (&r, 0, 0, 0, 0);
		get_bounding_box (cs->src, &r);

		if ((cs->mode & CONSOLIDATE_ROW_LABELS) &&
		    (cs->mode & CONSOLIDATE_COL_LABELS))
			dao_adjust (dao, r.end.col + 1 +
				    ((cs->mode & CONSOLIDATE_COPY_LABELS) ?
				     1 : 0),
				    r.end.row + 1 +
				    ((cs->mode & CONSOLIDATE_COPY_LABELS) ?
				     1 : 0));
		else if (cs->mode & CONSOLIDATE_ROW_LABELS)
			dao_adjust (dao, r.end.col + 1,
				    r.end.row + 1 +
				    ((cs->mode & CONSOLIDATE_COPY_LABELS) ?
				     1 : 0));

		else if (cs->mode & CONSOLIDATE_COL_LABELS)
			dao_adjust (dao, r.end.col + 1 +
				    ((cs->mode & CONSOLIDATE_COPY_LABELS) ?
				     1 : 0),
				    r.end.row + 1);
		else
			dao_adjust (dao, r.end.col + 1,
				    r.end.row + 1);
		return FALSE;
	}
	case TOOL_ENGINE_CLEAN_UP:
		gnm_consolidate_free (cs, TRUE);
		return FALSE;
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Data Consolidation"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Data Consolidation"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return consolidate_apply (cs, dao);
	}
	return TRUE;  /* We shouldn't get here */
}
