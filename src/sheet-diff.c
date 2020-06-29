/*
 * sheet-diff.c: Code for comparing sheets.
 *
 * Copyright (C) 2018 Morten Welinder (terra@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <sheet-diff.h>
#include <sheet.h>
#include <cell.h>
#include <expr.h>
#include <value.h>
#include <sheet-style.h>
#include <mstyle.h>
#include <ranges.h>
#include <expr-name.h>
#include <workbook.h>
#include <workbook-priv.h>
#include <string.h>

/* ------------------------------------------------------------------------- */

#define DISPATCH(method) if (istate->actions->method == NULL) { } else (istate->actions->method)
#define DISPATCH_VAL(method,def) (istate->actions->method == NULL) ? (def) : (istate->actions->method)

/* ------------------------------------------------------------------------- */

typedef struct {
	gpointer user;

	const GnmDiffActions *actions;
	gboolean diff_found;
	gboolean error;

	Sheet *old_sheet, *new_sheet;
	GnmRange common_range;

	Workbook *old_wb, *new_wb;
} GnmDiffIState;

/* ------------------------------------------------------------------------- */

static gboolean
compare_texpr_equal (GnmExprTop const *oe, GnmParsePos const *opp,
		     GnmExprTop const *ne, GnmParsePos const *npp,
		     GnmConventions const *convs)
{
	char *so, *sn;
	gboolean eq;

	if (gnm_expr_top_equal (oe, ne))
		return TRUE;

	// Not equal, but with references to sheets, that is not
	// necessary.  Compare as strings.

	so = gnm_expr_top_as_string (oe, opp, convs);
	sn = gnm_expr_top_as_string (ne, npp, convs);

	eq = g_strcmp0 (so, sn) == 0;

	g_free (so);
	g_free (sn);

	return eq;
}

static gboolean
compare_corresponding_cells (GnmCell const *co, GnmCell const *cn)
{
	gboolean has_expr = gnm_cell_has_expr (co);
	gboolean has_value = co->value != NULL;

	if (has_expr != gnm_cell_has_expr (cn))
		return TRUE;
	if (has_expr) {
		GnmParsePos opp, npp;
		parse_pos_init_cell (&opp, co);
		parse_pos_init_cell (&npp, cn);
		return !compare_texpr_equal (co->base.texpr, &opp,
					     cn->base.texpr, &npp,
					     sheet_get_conventions (cn->base.sheet));
	}

	if (has_value != (cn->value != NULL))
		return TRUE;
	if (has_value)
		return !(value_equal (co->value, cn->value) &&
			 go_format_eq (VALUE_FMT (co->value),
				       VALUE_FMT (cn->value)));


	return FALSE;
}

static gboolean
ignore_cell (GnmCell const *cell)
{
	if (cell) {
		if (gnm_cell_has_expr (cell)) {
			return gnm_expr_top_is_array_elem (cell->base.texpr,
							   NULL, NULL);
		} else {
			return VALUE_IS_EMPTY (cell->value);
		}
	}
	return FALSE;
}

static void
diff_sheets_cells (GnmDiffIState *istate)
{
	GPtrArray *old_cells = sheet_cells (istate->old_sheet, NULL);
	GPtrArray *new_cells = sheet_cells (istate->new_sheet, NULL);
	size_t io = 0, in = 0;

	// Make code below simpler.
	g_ptr_array_add (old_cells, NULL);
	g_ptr_array_add (new_cells, NULL);

	while (TRUE) {
		GnmCell const *co, *cn;

		while (ignore_cell ((co = g_ptr_array_index (old_cells, io))))
			io++;

		while (ignore_cell ((cn = g_ptr_array_index (new_cells, in))))
			in++;

		if (co && cn) {
			int order = co->pos.row == cn->pos.row
				? co->pos.col - cn->pos.col
				: co->pos.row - cn->pos.row;
			if (order < 0)
				cn = NULL;
			else if (order > 0)
				co = NULL;
			else {
				if (compare_corresponding_cells (co, cn)) {
					istate->diff_found = TRUE;
					DISPATCH(cell_changed) (istate->user, co, cn);
				}
				io++, in++;
				continue;
			}
		}

		if (co) {
			istate->diff_found = TRUE;
			DISPATCH(cell_changed) (istate->user, co, NULL);
			io++;
		} else if (cn) {
			istate->diff_found = TRUE;
			DISPATCH(cell_changed) (istate->user, NULL, cn);
			in++;
		} else
			break;
	}

	g_ptr_array_free (old_cells, TRUE);
	g_ptr_array_free (new_cells, TRUE);
}

static void
diff_sheets_colrow (GnmDiffIState *istate, gboolean is_cols)
{
	ColRowInfo const *old_def =
		sheet_colrow_get_default (istate->old_sheet, is_cols);
	ColRowInfo const *new_def =
		sheet_colrow_get_default (istate->new_sheet, is_cols);
	int i, U;

	if (!col_row_info_equal (old_def, new_def)) {
		istate->diff_found = TRUE;
		DISPATCH(colrow_changed) (istate->user, old_def, new_def, is_cols, -1);
	}

	U = is_cols
		? istate->common_range.end.col
		: istate->common_range.end.row;
	for (i = 0; i <= U; i++) {
		ColRowInfo const *ocr =
			sheet_colrow_get (istate->old_sheet, i, is_cols);
		ColRowInfo const *ncr =
			sheet_colrow_get (istate->new_sheet, i, is_cols);

		if (ocr == ncr)
			continue; // Considered equal, even if defaults are different
		if (!ocr) ocr = old_def;
		if (!ncr) ncr = new_def;
		if (!col_row_info_equal (ocr, ncr)) {
			istate->diff_found = TRUE;
			DISPATCH(colrow_changed) (istate->user, ocr, ncr, is_cols, i);
		}
	}
}

#define DO_INT(field,attr)						\
  do {									\
	  if (istate->old_sheet->field != istate->new_sheet->field) {	\
		  istate->diff_found = TRUE;				\
		  DISPATCH(sheet_attr_int_changed)			\
			  (istate->user, attr, istate->old_sheet->field, istate->new_sheet->field); \
	  }								\
  } while (0)

static void
diff_sheets_attrs (GnmDiffIState *istate)
{
	GnmSheetSize const *os = gnm_sheet_get_size (istate->old_sheet);
	GnmSheetSize const *ns = gnm_sheet_get_size (istate->new_sheet);

	if (os->max_cols != ns->max_cols) {
		istate->diff_found = TRUE;
		DISPATCH(sheet_attr_int_changed)
			(istate->user, "Cols", os->max_cols, ns->max_cols);
	}
	if (os->max_rows != ns->max_rows) {
		istate->diff_found = TRUE;
		DISPATCH(sheet_attr_int_changed)
			(istate->user, "Rows", os->max_rows, ns->max_rows);
	}

	DO_INT (display_formulas, "DisplayFormulas");
	DO_INT (hide_zero, "HideZero");
	DO_INT (hide_grid, "HideGrid");
	DO_INT (hide_col_header, "HideColHeader");
	DO_INT (hide_row_header, "HideRowHeader");
	DO_INT (display_outlines, "DisplayOutlines");
	DO_INT (outline_symbols_below, "OutlineSymbolsBelow");
	DO_INT (outline_symbols_right, "OutlineSymbolsRight");
	DO_INT (text_is_rtl, "RTL_Layout");
	DO_INT (is_protected, "Protected");
	DO_INT (visibility, "Visibility");
}
#undef DO_INT

struct cb_diff_sheets_styles {
	GnmDiffIState *istate;
	GnmStyle *old_style;
};

static void
cb_diff_sheets_styles_2 (G_GNUC_UNUSED gpointer key,
			 gpointer sr_, gpointer user_data)
{
	GnmStyleRegion *sr = sr_;
	struct cb_diff_sheets_styles *data = user_data;
	GnmDiffIState *istate = data->istate;
	GnmRange r = sr->range;

	if (gnm_style_find_differences (data->old_style, sr->style, TRUE) == 0)
		return;

	istate->diff_found = TRUE;

	DISPATCH(style_changed) (istate->user, &r, data->old_style, sr->style);
}

static void
cb_diff_sheets_styles_1 (G_GNUC_UNUSED gpointer key,
			 gpointer sr_, gpointer user_data)
{
	GnmStyleRegion *sr = sr_;
	struct cb_diff_sheets_styles *data = user_data;
	GnmDiffIState *istate = data->istate;

	data->old_style = sr->style;
	sheet_style_range_foreach (istate->new_sheet, &sr->range,
				   cb_diff_sheets_styles_2,
				   data);
}

static void
diff_sheets_styles (GnmDiffIState *istate)
{
	struct cb_diff_sheets_styles data;

	data.istate = istate;
	sheet_style_range_foreach (istate->old_sheet, &istate->common_range,
				   cb_diff_sheets_styles_1,
				   &data);
}

static int
cb_expr_name_by_name (GnmNamedExpr const *a, GnmNamedExpr const *b)
{
	return g_strcmp0 (expr_name_name (a), expr_name_name (b));
}

static void
diff_names (GnmDiffIState *istate,
	    GnmNamedExprCollection const *onames, GnmNamedExprCollection const *nnames)
{
	GSList *old_names = gnm_named_expr_collection_list (onames);
	GSList *new_names = gnm_named_expr_collection_list (nnames);
	GSList *lo, *ln;
	GnmConventions const *convs;

	if (istate->new_sheet)
		convs = sheet_get_conventions (istate->new_sheet);
	else
		// Hmm...  It's not terribly important where we get them
		convs = sheet_get_conventions (workbook_sheet_by_index (istate->new_wb, 0));

	old_names = g_slist_sort (old_names, (GCompareFunc)cb_expr_name_by_name);
	new_names = g_slist_sort (new_names, (GCompareFunc)cb_expr_name_by_name);

	lo = old_names;
	ln = new_names;
	while (lo || ln) {
		GnmNamedExpr const *on = lo ? lo->data : NULL;
		GnmNamedExpr const *nn = ln ? ln->data : NULL;

		if (!nn || (on && cb_expr_name_by_name (on, nn) < 0)) {
			// Old name got removed
			istate->diff_found = TRUE;
			DISPATCH(name_changed) (istate->user, on, NULL);
			lo = lo->next;
			continue;
		}

		if (!on || (nn && cb_expr_name_by_name (on, nn) > 0)) {
			// New name got added
			istate->diff_found = TRUE;
			DISPATCH(name_changed) (istate->user, NULL, nn);
			ln = ln->next;
			continue;
		}

		if (!compare_texpr_equal (on->texpr, &on->pos,
					  nn->texpr, &nn->pos,
					  convs)) {
			istate->diff_found = TRUE;
			DISPATCH(name_changed) (istate->user, on, nn);
		}

		lo = lo->next;
		ln = ln->next;
	}

	g_slist_free (old_names);
	g_slist_free (new_names);
}

static void
real_diff_sheets (GnmDiffIState *istate, Sheet *old_sheet, Sheet *new_sheet)
{
	GnmRange or, nr;

	istate->old_sheet = old_sheet;
	istate->new_sheet = new_sheet;

	DISPATCH(sheet_start) (istate->user, old_sheet, new_sheet);

	range_init_full_sheet (&or, old_sheet);
	range_init_full_sheet (&nr, new_sheet);
	range_intersection (&istate->common_range, &or, &nr);

	diff_sheets_attrs (istate);
	diff_names (istate, istate->old_sheet->names, istate->new_sheet->names);
	diff_sheets_colrow (istate, TRUE);
	diff_sheets_colrow (istate, FALSE);
	diff_sheets_cells (istate);
	diff_sheets_styles (istate);

	DISPATCH(sheet_end) (istate->user);

	istate->old_sheet = istate->new_sheet = NULL;
}

gboolean
gnm_diff_sheets (const GnmDiffActions *actions, gpointer user,
		 Sheet *old_sheet, Sheet *new_sheet)
{
	GnmDiffIState istate;

	memset (&istate, 0, sizeof (istate));
	istate.user = user;
	istate.actions = actions;
	istate.diff_found = FALSE;
	istate.error = FALSE;

	real_diff_sheets (&istate, old_sheet, new_sheet);

	return istate.diff_found;
}

static void
real_diff_workbooks (GnmDiffIState *istate,
		     Workbook *old_wb, Workbook *new_wb)
{
	int last_index = -1;
	int i, count;
	gboolean sheet_order_changed = FALSE;

	istate->old_wb = old_wb;
	istate->new_wb = new_wb;

	if (DISPATCH_VAL(diff_start,FALSE) (istate->user)) {
		istate->error = TRUE;
		return;
	}

	diff_names (istate, old_wb->names, new_wb->names);

	// This doesn't handle sheet renames very well, but simply considers
	// that a sheet deletion and a sheet insert.
	count = workbook_sheet_count (old_wb);
	for (i = 0; i < count; i++) {
		Sheet *old_sheet = workbook_sheet_by_index (old_wb, i);
		Sheet *new_sheet = workbook_sheet_by_name (new_wb,
							   old_sheet->name_unquoted);
		if (new_sheet) {
			if (new_sheet->index_in_wb < last_index)
				sheet_order_changed = TRUE;
			last_index = new_sheet->index_in_wb;

			real_diff_sheets (istate, old_sheet, new_sheet);
		} else {
			istate->diff_found = TRUE;
			DISPATCH(sheet_start) (istate->user, old_sheet, new_sheet);
			DISPATCH(sheet_end) (istate->user);
		}

	}

	count = workbook_sheet_count (new_wb);
	for (i = 0; i < count; i++) {
		Sheet *new_sheet = workbook_sheet_by_index (new_wb, i);
		Sheet *old_sheet = workbook_sheet_by_name (old_wb,
							   new_sheet->name_unquoted);
		if (old_sheet)
			; // Nothing -- already done above.
		else {
			istate->diff_found = TRUE;
			DISPATCH(sheet_start) (istate->user, old_sheet, new_sheet);
			DISPATCH(sheet_end) (istate->user);
		}
	}

	if (sheet_order_changed) {
		istate->diff_found = TRUE;
		DISPATCH(sheet_order_changed) (istate->user);
	}

	DISPATCH(diff_end) (istate->user);
}

int
gnm_diff_workbooks (const GnmDiffActions *actions, gpointer user,
		    Workbook *old_wb, Workbook *new_wb)
{
	GnmDiffIState istate;

	memset (&istate, 0, sizeof (istate));
	istate.user = user;
	istate.actions = actions;
	istate.diff_found = FALSE;
	istate.error = FALSE;

	real_diff_workbooks (&istate, old_wb, new_wb);

	return istate.error
		? 2
		: (istate.diff_found
		   ? 1
		   : 0);
}
