/*
 * workbook-cmd-format.c: Workbook format commands hooked to the menus
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Jody Goldberg (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <workbook-cmd-format.h>

#include <cell.h>
#include <dependent.h>
#include <expr.h>
#include <func.h>
#include <ranges.h>
#include <gui-util.h>
#include <selection.h>
#include <sheet-merge.h>
#include <sheet-view.h>
#include <value.h>
#include <workbook-control.h>
#include <workbook-view.h>
#include <workbook.h>
#include <application.h>
#include <dialogs/dialogs.h>
#include <sheet.h>
#include <commands.h>
#include <style-border.h>
#include <style-color.h>

struct closure_colrow_resize {
	gboolean	 is_cols;
	ColRowIndexList *selection;
};

static gboolean
cb_colrow_collect (G_GNUC_UNUSED SheetView *sv, GnmRange const *r, gpointer user_data)
{
	struct closure_colrow_resize *info = user_data;
	int first, last;

	if (info->is_cols) {
		first = r->start.col;
		last = r->end.col;
	} else {
		first = r->start.row;
		last = r->end.row;
	}

	info->selection = colrow_get_index_list (first, last, info->selection);
	return TRUE;
}

void
workbook_cmd_resize_selected_colrow (WorkbookControl *wbc, Sheet *sheet,
				     gboolean is_cols, int new_size_pixels)
{
	struct closure_colrow_resize closure;
	closure.is_cols = is_cols;
	closure.selection = NULL;
	sv_selection_foreach (sheet_get_view (sheet, wb_control_view (wbc)),
		&cb_colrow_collect, &closure);
	cmd_resize_colrow (wbc, sheet, is_cols, closure.selection, new_size_pixels);
}

void
workbook_cmd_autofit_selection  (WorkbookControl *wbc, Sheet *sheet,
			gboolean is_cols)
{
	SheetView *sv = sheet_get_view (sheet, wb_control_view (wbc));
	struct closure_colrow_resize closure;
	closure.is_cols = is_cols;
	closure.selection = NULL;
	sv_selection_foreach (sv, &cb_colrow_collect, &closure);
	cmd_autofit_selection (wbc, sv, sheet, is_cols, closure.selection);
}

void
workbook_cmd_inc_indent (WorkbookControl *wbc)
{
	WorkbookView const *wbv = wb_control_view (wbc);
	int i;

	g_return_if_fail (wbv != NULL);
	g_return_if_fail (wbv->current_style != NULL);

	i = gnm_style_get_indent (wbv->current_style);
	if (i < 20) {
		GnmStyle *style = gnm_style_new ();

		if (GNM_HALIGN_LEFT != gnm_style_get_align_h (wbv->current_style))
			gnm_style_set_align_h (style, GNM_HALIGN_LEFT);
		gnm_style_set_indent (style, i+1);
		cmd_selection_format (wbc, style, NULL, _("Increase Indent"));
	}
}

void
workbook_cmd_dec_indent (WorkbookControl *wbc)
{
	WorkbookView const *wbv = wb_control_view (wbc);
	int i;

	g_return_if_fail (wbv != NULL);
	g_return_if_fail (wbv->current_style != NULL);

	i = gnm_style_get_indent (wbv->current_style);
	if (i > 0) {
		GnmStyle *style = gnm_style_new ();
		gnm_style_set_indent (style, i-1);
		cmd_selection_format (wbc, style, NULL, _("Decrease Indent"));
	}
}

struct workbook_cmd_wrap_sort_t {
	GnmExprList    *args;
	GnmRange const *r;
	Workbook *wb;
};

static GnmValue *
cb_get_cell_content (GnmCellIter const *iter, struct workbook_cmd_wrap_sort_t *cl)
{
	GnmExpr const *expr;

	if (iter->cell == NULL)
		expr = gnm_expr_new_constant (value_new_empty ());
	else if (gnm_cell_has_expr (iter->cell)) {
		char	 *text;
		GnmParsePos pp;
		GnmExprTop const *texpr;

		parse_pos_init (&pp, cl->wb, iter->pp.sheet,
				cl->r->start.col, cl->r->start.row);
		text = gnm_expr_as_string   ((iter->cell)->base.texpr->expr,
					     &iter->pp, NULL);
		texpr = gnm_expr_parse_str (text, &pp, GNM_EXPR_PARSE_DEFAULT,
					    NULL, NULL);
		g_free (text);
		expr = gnm_expr_copy (texpr->expr);
		gnm_expr_top_unref (texpr);

	} else if (iter->cell->value != NULL)
		expr = gnm_expr_new_constant (value_dup (iter->cell->value));
	else
		expr = gnm_expr_new_constant (value_new_empty ());

	cl->args = gnm_expr_list_prepend (cl->args, expr);
	return NULL;
}

void
workbook_cmd_wrap_sort (WorkbookControl *wbc, int type)
{
	WorkbookView const *wbv = wb_control_view (wbc);
	SheetView *sv = wb_view_cur_sheet_view (wbv);
	GSList *l = sv->selections, *merges;
	GnmExpr const *expr;
	GnmFunc	   *fd_sort;
	GnmFunc	   *fd_array;
	GnmExprTop const *texpr;
	struct workbook_cmd_wrap_sort_t cl = {NULL, NULL, NULL};

	cl.r = selection_first_range
		(sv, GO_CMD_CONTEXT (wbc), _("Wrap SORT"));
	cl.wb = wb_control_get_workbook (wbc);

	if (g_slist_length (l) > 1) {
		go_cmd_context_error_invalid (GO_CMD_CONTEXT (wbc), _("Wrap SORT"),
			_("A single selection is required."));

		return;
	}
	if (range_height (cl.r) > 1 && range_width (cl.r) > 1) {
		go_cmd_context_error_invalid
			(GO_CMD_CONTEXT (wbc), _("Wrap SORT"),
			 _("An n\342\250\2571 or 1\342\250\257n "
			   "selection is required."));
		return;
	}
	if (range_height (cl.r) == 1 && range_width (cl.r) == 1) {
		go_cmd_context_error_invalid
			(GO_CMD_CONTEXT (wbc), _("Wrap SORT"),
			 _("There is no point in sorting a single cell."));
		return;
	}
	merges = gnm_sheet_merge_get_overlap (sv->sheet, cl.r);
	if (merges != NULL) {
		g_slist_free (merges);
		go_cmd_context_error_invalid
			(GO_CMD_CONTEXT (wbc), _("Wrap SORT"),
			 _("The range to be sorted may not contain any merged cells."));
		return;
	}
	fd_sort = gnm_func_lookup_or_add_placeholder ("sort");
	fd_array = gnm_func_lookup_or_add_placeholder ("array");

	sheet_foreach_cell_in_range
		(sv->sheet, CELL_ITER_ALL, cl.r,
		 (CellIterFunc)&cb_get_cell_content, &cl);

	cl.args = g_slist_reverse (cl.args);
	expr = gnm_expr_new_funcall (fd_array, cl.args);
	expr = gnm_expr_new_funcall2
		(fd_sort, expr, gnm_expr_new_constant (value_new_int (type)));
	texpr = gnm_expr_top_new (expr);
	cmd_area_set_array_expr (wbc, sv, texpr);
	gnm_expr_top_unref (texpr);
}
