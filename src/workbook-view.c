/*
 * workbook-view.c: View functions for the workbook
 *
 * Copyright (C) 2000-2006 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2012 Morten Welinder (terra@gnome.org)
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <workbook-view.h>

#include <workbook-control-priv.h>
#include <workbook-priv.h>
#include <application.h>
#include <sheet.h>
#include <sheet-view.h>
#include <sheet-merge.h>
#include <sheet-style.h>
#include <func.h>
#include <expr.h>
#include <expr-name.h>
#include <value.h>
#include <ranges.h>
#include <selection.h>
#include <mstyle.h>
#include <validation.h>
#include <validation-combo.h>
#include <gnm-sheet-slicer.h>
#include <gnm-sheet-slicer-combo.h>
#include <position.h>
#include <cell.h>
#include <gutils.h>
#include <command-context.h>
#include <auto-format.h>
#include <sheet-object.h>
#include <gnumeric-conf.h>

#include <goffice/goffice.h>
#include <gsf/gsf-meta-names.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-output-stdio.h>
#include <gsf/gsf-input.h>
#include <gnm-i18n.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <mathfunc.h>

#ifdef G_OS_WIN32
#include <windows.h>
#endif

/**
 * WorkbookView:
 * @wb_controls: (element-type WorkbookControl):
 **/
enum {
	PROP_0,
	PROP_AUTO_EXPR_FUNC,
	PROP_AUTO_EXPR_DESCR,
	PROP_AUTO_EXPR_MAX_PRECISION,
	PROP_AUTO_EXPR_VALUE,
	PROP_AUTO_EXPR_EVAL_POS,
	PROP_SHOW_HORIZONTAL_SCROLLBAR,
	PROP_SHOW_VERTICAL_SCROLLBAR,
	PROP_SHOW_NOTEBOOK_TABS,
	PROP_SHOW_FUNCTION_CELL_MARKERS,
	PROP_SHOW_EXTENSION_MARKERS,
	PROP_DO_AUTO_COMPLETION,
	PROP_PROTECTED,
	PROP_PREFERRED_WIDTH,
	PROP_PREFERRED_HEIGHT,
	PROP_WORKBOOK
};

/* WorkbookView signals */
enum {
	LAST_SIGNAL
};

/**
 * wb_view_get_workbook:
 * @wbv: #WorkbookView
 *
 * Returns: (transfer none): the #Workbook associated with @wbv
 **/
Workbook *
wb_view_get_workbook (WorkbookView const *wbv)
{
	g_return_val_if_fail (GNM_IS_WORKBOOK_VIEW (wbv), NULL);
	return wbv->wb;
}

/**
 * wb_view_get_doc:
 * @wbv: #WorkbookView
 *
 * Returns: (transfer none): the #Workbook associated with @wbv cast to a #GODoc
 **/
GODoc *
wb_view_get_doc (WorkbookView const *wbv)
{
	g_return_val_if_fail (GNM_IS_WORKBOOK_VIEW (wbv), NULL);
	return GO_DOC (wbv->wb);
}

/**
 * wb_view_get_index_in_wb:
 * @wbv: #WorkbookView
 *
 * Returns 0 based index of wbv within workbook, or -1 if there is no workbook.
 **/
int
wb_view_get_index_in_wb (WorkbookView const *wbv)
{
	g_return_val_if_fail (GNM_IS_WORKBOOK_VIEW (wbv), -1);
	if (NULL != wbv->wb) {
		unsigned i = wbv->wb->wb_views->len;
		while (i-- > 0)
			if (g_ptr_array_index (wbv->wb->wb_views, i) == wbv)
				return i;
	}
	return -1;
}

/**
 * wb_view_cur_sheet:
 * @wbv: #WorkbookView
 *
 * Returns: (transfer none): the current sheet.
 **/
Sheet *
wb_view_cur_sheet (WorkbookView const *wbv)
{
	g_return_val_if_fail (GNM_IS_WORKBOOK_VIEW (wbv), NULL);
	return wbv->current_sheet;
}

/**
 * wb_view_cur_sheet_view:
 * @wbv: #WorkbookView
 *
 * Returns: (transfer none): the current sheet view.
 **/
SheetView *
wb_view_cur_sheet_view (WorkbookView const *wbv)
{
	g_return_val_if_fail (GNM_IS_WORKBOOK_VIEW (wbv), NULL);
	return wbv->current_sheet_view;
}

void
wb_view_sheet_focus (WorkbookView *wbv, Sheet *sheet)
{
	if (wbv->current_sheet != sheet) {
		/* Make sure the sheet has been attached */
		g_return_if_fail (sheet == NULL || sheet->index_in_wb >= 0);

#if 0
		g_print ("Focus %s\n", sheet ? sheet->name_quoted : "-");
#endif

		wbv->current_sheet = sheet;
		wbv->current_sheet_view = sheet_get_view (sheet, wbv);

		WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control,
			wb_control_sheet_focus (control, sheet););

		wb_view_selection_desc (wbv, TRUE, NULL);
		wb_view_edit_line_set (wbv, NULL);
		wb_view_style_feedback (wbv);
		wb_view_menus_update (wbv);
		wb_view_auto_expr_recalc (wbv);
	}
}

void
wb_view_sheet_add (WorkbookView *wbv, Sheet *new_sheet)
{
	SheetView *new_view;

	g_return_if_fail (GNM_IS_WORKBOOK_VIEW (wbv));

	new_view = gnm_sheet_view_new (new_sheet, wbv);

	WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control,
		wb_control_sheet_add (control, new_view););

	g_object_unref (new_view);

	if (wbv->current_sheet == NULL)
		wb_view_sheet_focus (wbv, new_sheet);
}

gboolean
wb_view_is_protected (WorkbookView *wbv, gboolean check_sheet)
{
	g_return_val_if_fail (GNM_IS_WORKBOOK_VIEW (wbv), FALSE);

	return wbv->is_protected || (check_sheet &&
		wbv->current_sheet != NULL && wbv->current_sheet->is_protected);
}

void
wb_view_set_attribute (WorkbookView *wbv, char const *name, char const *value)
{
	gboolean res;
	GObject *obj;
	const char *tname;

	g_return_if_fail (GNM_IS_WORKBOOK_VIEW (wbv));
	g_return_if_fail (name != NULL);
	g_return_if_fail (value != NULL);

	obj = G_OBJECT (wbv);
	res = !g_ascii_strcasecmp (value, "TRUE");

	if (strncmp (name, "WorkbookView::", 14) == 0)
		tname = name + 14;
	else if (strncmp (name, "Workbook::", 10) == 0)
		/* Some old files have this.  */
		tname = name + 10;
	else
		tname = "nope";

	if (!strcmp (tname , "show_horizontal_scrollbar"))
		g_object_set (obj, "show_horizontal_scrollbar", res, NULL);
	else if (!strcmp (tname , "show_vertical_scrollbar"))
		g_object_set (obj, "show_vertical_scrollbar", res, NULL);
	else if (!strcmp (tname , "show_notebook_tabs"))
		g_object_set (obj, "show_notebook_tabs", res, NULL);
	else if (!strcmp (tname , "show_function_cell_markers"))
		g_object_set (obj, "show_function_cell_markers", res, NULL);
	else if (!strcmp (tname , "show_extension_markers"))
		g_object_set (obj, "show_extension_markers", res, NULL);
	else if (!strcmp (tname , "do_auto_completion"))
		g_object_set (obj, "do_auto_completion", res, NULL);
	else if (!strcmp (tname , "is_protected"))
		g_object_set (obj, "protected", res, NULL);
	else
		g_warning ("WorkbookView unknown arg '%s'", name);
}

void
wb_view_preferred_size (WorkbookView *wbv, int w, int h)
{
	g_return_if_fail (GNM_IS_WORKBOOK_VIEW (wbv));

	if (w <= 0)
		w = 768; /* use default */
	if (h <= 0)
		h = 768; /* use default */

	g_object_set (G_OBJECT (wbv),
		      "preferred-width", w,
		      "preferred-height", h,
		      NULL);
}

void
wb_view_style_feedback (WorkbookView *wbv)
{
	SheetView *sv;
	GnmStyle const *style;
	GnmSheetSlicer const *dslicer;
	GODataSlicerField *dsfield;
	GnmValidation const *val;
	GOFormat const *fmt_style, *fmt_cell;
	GnmCell *cell;
	gboolean update_controls = TRUE;

	g_return_if_fail (GNM_IS_WORKBOOK_VIEW (wbv));

	sv = wbv->current_sheet_view;
	if (sv == NULL)
		return;

	style = sheet_style_get (sv->sheet,
		sv->edit_pos.col, sv->edit_pos.row);
	fmt_style = gnm_style_get_format (style);
	if (go_format_is_general (fmt_style) &&
	    (cell = sheet_cell_get (sv->sheet, sv->edit_pos.col, sv->edit_pos.row)) &&
	    cell->value && VALUE_FMT (cell->value))
		fmt_cell = VALUE_FMT (cell->value);
	else
		fmt_cell = fmt_style;

	if (go_format_eq (fmt_cell, fmt_style)) {
		if (style == wbv->current_style)
			update_controls = FALSE;
		gnm_style_ref (style);
	} else {
		GnmStyle *tmp = gnm_style_dup (style);
		gnm_style_set_format (tmp, fmt_cell);
		style = tmp;
	}

	if (wbv->current_style != NULL)
		gnm_style_unref (wbv->current_style);
	wbv->current_style = style;

	if (wbv->in_cell_combo != NULL) {
		sheet_object_clear_sheet (wbv->in_cell_combo);
		g_object_unref (wbv->in_cell_combo);
		wbv->in_cell_combo = NULL;
	}

	if (gnm_style_is_element_set (style, MSTYLE_VALIDATION) &&
	    NULL != (val = gnm_style_get_validation (style)) &&
	    val->type == GNM_VALIDATION_TYPE_IN_LIST &&
	    val->use_dropdown)
		wbv->in_cell_combo = gnm_validation_combo_new (val, sv);
	else if (NULL != (dslicer = gnm_sheet_slicers_at_pos (sv->sheet, &sv->edit_pos)) &&
		   NULL != (dsfield = gnm_sheet_slicer_field_header_at_pos (dslicer, &sv->edit_pos)))
		wbv->in_cell_combo = g_object_new (gnm_sheet_slicer_combo_get_type (),
						   "sheet-view", sv,
						   "field",	 dsfield,
						   NULL);

	if (NULL != wbv->in_cell_combo)
	{
		const double a_offsets [4] = { 0., 0., 1., 1. };
		SheetObjectAnchor  anchor;
		GnmRange corner;
		GnmRange const *r;
		if (NULL == (r = gnm_sheet_merge_contains_pos (sv->sheet, &sv->edit_pos)))
			r = range_init_cellpos (&corner, &sv->edit_pos);
		sheet_object_anchor_init (&anchor, r, a_offsets, GOD_ANCHOR_DIR_DOWN_RIGHT, GNM_SO_ANCHOR_TWO_CELLS);
		sheet_object_set_anchor (wbv->in_cell_combo, &anchor);
		sheet_object_set_sheet (wbv->in_cell_combo, sv->sheet);
	}

	if (update_controls) {
		WORKBOOK_VIEW_FOREACH_CONTROL(wbv, control,
			wb_control_style_feedback (control, NULL););
	}
}

void
wb_view_menus_update (WorkbookView *wbv)
{
	Sheet *sheet;

	g_return_if_fail (GNM_IS_WORKBOOK_VIEW (wbv));

	sheet = wbv->current_sheet;
	if (sheet != NULL) {
		WORKBOOK_VIEW_FOREACH_CONTROL (wbv, wbc, {
			wb_control_menu_state_update (wbc, MS_ALL);
			wb_control_update_action_sensitivity (wbc);
		});
	}
}

/**
 * wb_view_selection_desc:
 * @wbv: The view
 * @use_pos:
 * @wbc: (allow-none): A #WorkbookControl
 *
 * Load the edit line with the value of the cell in sheet's edit_pos.
 *
 * Calculate what to display on the edit line then display it either in the
 * control @wbc, or if that is %NULL, in all controls.
 */
void
wb_view_selection_desc (WorkbookView *wbv, gboolean use_pos,
			WorkbookControl *wbc)
{
	SheetView *sv;

	g_return_if_fail (GNM_IS_WORKBOOK_VIEW (wbv));

	sv = wbv->current_sheet_view;
	if (sv != NULL) {
		char buffer [10 + 2 * 4 * sizeof (int)];
		char const *sel_descr = buffer;
		GnmRange const *r, *m;

		g_return_if_fail (GNM_IS_SHEET_VIEW (sv));
		g_return_if_fail (sv->selections);

		r = selection_first_range (sv, NULL, NULL);

		if (use_pos || range_is_singleton (r) ||
		    (NULL != (m = gnm_sheet_merge_is_corner (sv->sheet, &r->start)) &&
		     range_equal (r, m))) {
			sel_descr = sheet_names_check (sv->sheet, r);
			if (sel_descr == NULL) {
				GnmParsePos pp;
				parse_pos_init_editpos (&pp, sv);
				sel_descr = parsepos_as_string (&pp);
			}
		} else {
			int rows = r->end.row - r->start.row + 1;
			int cols = r->end.col - r->start.col + 1;

			if (rows == gnm_sheet_get_max_rows (sv->sheet))
        /* Translators: "%dC" is a very short format to indicate the number of full columns */
				snprintf (buffer, sizeof (buffer), _("%dC"), cols);
			else if (cols == gnm_sheet_get_max_cols (sv->sheet))
        /* Translators: "%dR" is a very short format to indicate the number of full rows */
				snprintf (buffer, sizeof (buffer), _("%dR"), rows);
			else
        /* Translators: "%dR x %dC" is a very short format to indicate the number of rows and columns */
				snprintf (buffer, sizeof (buffer), _("%dR x %dC"),
					  rows, cols);
		}

		if (wbc == NULL) {
			WORKBOOK_VIEW_FOREACH_CONTROL (wbv, wbc,
				wb_control_selection_descr_set (wbc, sel_descr););
		} else
			wb_control_selection_descr_set (wbc, sel_descr);
	}
}

/**
 * wb_view_edit_line_set:
 * @wbv: The view
 * @wbc: (allow-none): A #WorkbookControl
 *
 * Load the edit line with the value of the cell in @sheet's edit_pos.
 *
 * Calculate what to display on the edit line then display it either in the
 * control @wbc, or if that is %NULL, in all controls.
 */
void
wb_view_edit_line_set (WorkbookView *wbv, WorkbookControl *wbc)
{
	SheetView *sv;

	g_return_if_fail (GNM_IS_WORKBOOK_VIEW (wbv));

	sv = wbv->current_sheet_view;
	if (sv != NULL) {
		char *text;
		Sheet *sheet = sv->sheet;
		GnmCell const *cell = sheet_cell_get (sheet,
			sv->edit_pos.col, sv->edit_pos.row);

		if (NULL != cell) {
			text = gnm_cell_get_text_for_editing (cell, NULL, NULL);

			if (gnm_cell_has_expr (cell)) {
				GnmExprTop const *texpr = cell->base.texpr;
				GnmCell const *corner = NULL;
				int x = 0, y = 0;

				/*
				 * If this is part of an array we add '{' '}'
				 * and size information to the display.  That
				 * is not actually part of the parsable
				 * expression, but it is a useful extension to
				 * the simple '{' '}' that MS excel(tm) uses.
				 */
				if (gnm_expr_top_is_array_corner (texpr))
					corner = cell;
				else if (gnm_expr_top_is_array_elem (texpr, &x, &y)) {
					corner = sheet_cell_get
						(sheet,
						 cell->pos.col - x,
						 cell->pos.row - y);
				}

				if (corner) {
					int cols, rows;
					char *tmp;

					gnm_expr_top_get_array_size (corner->base.texpr, &cols, &rows);

					tmp = g_strdup_printf
						("{%s}(%d%c%d)[%d][%d]",
						 text,
						 cols, go_locale_get_arg_sep (), rows,
						 x, y);
					g_free (text);
					text = tmp;
				}
			}
		} else
			text = g_strdup ("");

		if (wbc == NULL) {
			WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control,
				wb_control_edit_line_set (control, text););
		} else
			wb_control_edit_line_set (wbc, text);

		g_free (text);
	}
}

static void
accumulate_regions (SheetView *sv,  GnmRange const *r, gpointer closure)
{
	GnmExprList	**selection = closure;
	GnmCellRef a, b;

	a.sheet = b.sheet = sv_sheet (sv);
	a.col_relative = a.row_relative = b.col_relative = b.row_relative = FALSE;
	a.col = r->start.col;
	a.row = r->start.row;
	b.col = r->end.col;
	b.row = r->end.row;

	*selection = gnm_expr_list_prepend (*selection,
		gnm_expr_new_constant (value_new_cellrange_unsafe (&a, &b)));
}

void
wb_view_auto_expr_recalc (WorkbookView *wbv)
{
	GnmExprList	*selection = NULL;
	GnmValue	*v;
	SheetView	*sv;
	GnmExprTop const *texpr;
	GnmEvalPos      ep;

	g_return_if_fail (GNM_IS_WORKBOOK_VIEW (wbv));

	sv = wb_view_cur_sheet_view (wbv);
	if (wbv->current_sheet == NULL ||
	    sv == NULL)
		return;

	if (wbv->auto_expr.dep.base.sheet != NULL &&
	    wbv->auto_expr.dep.base.texpr != NULL) {
		texpr = wbv->auto_expr.dep.base.texpr;
		gnm_expr_top_ref (texpr);
	} else if (wbv->auto_expr.func != NULL) {
		sv_selection_apply (sv, &accumulate_regions, FALSE, &selection);
		texpr = gnm_expr_top_new
			(gnm_expr_new_funcall (wbv->auto_expr.func, selection));
	} else {
		texpr = gnm_expr_top_new_constant (value_new_string (""));
	}

	eval_pos_init_sheet (&ep, wbv->current_sheet);

	v = gnm_expr_top_eval (texpr, &ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	if (v) {
		if (wbv->auto_expr.use_max_precision)
			value_set_fmt (v, NULL);
		else if (!VALUE_FMT (v)) {
			GOFormat const *fmt = gnm_auto_style_format_suggest (texpr, &ep);
			value_set_fmt (v, fmt);
			go_format_unref (fmt);
		}
	}
	g_object_set (wbv, "auto-expr-value", v, NULL);
	value_release (v);
	gnm_expr_top_unref (texpr);
}

/* perform whatever initialization of a control that is necessary when it
 * finally gets assigned to a view with a workbook */
static void
wb_view_init_control (G_GNUC_UNUSED WorkbookControl *wbc)
{
}

void
wb_view_attach_control (WorkbookView *wbv, WorkbookControl *wbc)
{
	g_return_if_fail (GNM_IS_WORKBOOK_VIEW (wbv));
	g_return_if_fail (GNM_IS_WBC (wbc));
	g_return_if_fail (wb_control_view (wbc) == NULL);

	if (wbv->wb_controls == NULL)
		wbv->wb_controls = g_ptr_array_new ();
	g_ptr_array_add (wbv->wb_controls, wbc);
	g_object_set (G_OBJECT (wbc), "view", wbv, NULL);

	if (wbv->wb != NULL)
		wb_view_init_control (wbc);
}

void
wb_view_detach_control (WorkbookControl *wbc)
{
	g_return_if_fail (GNM_IS_WBC (wbc));
	g_return_if_fail (GNM_IS_WORKBOOK_VIEW (wb_control_view (wbc)));

	g_ptr_array_remove (wbc->wb_view->wb_controls, wbc);
	if (wbc->wb_view->wb_controls->len == 0) {
		g_ptr_array_free (wbc->wb_view->wb_controls, TRUE);
		wbc->wb_view->wb_controls = NULL;
	}
	g_object_set (G_OBJECT (wbc), "view", NULL, NULL);
}

static GObjectClass *parent_class;

static void
wb_view_auto_expr_func (WorkbookView *wbv, GnmFunc *func)
{
	if (wbv->auto_expr.func == func)
		return;

	if (wbv->auto_expr.func)
		gnm_func_dec_usage (wbv->auto_expr.func);

	if (func)
		gnm_func_inc_usage (func);
	wbv->auto_expr.func = func;

	wb_view_auto_expr_recalc (wbv);
}

static void
wb_view_auto_expr_descr (WorkbookView *wbv, const char *descr)
{
	char *s;

	if (go_str_compare (descr, wbv->auto_expr.descr) == 0)
		return;

	s = g_strdup (descr);
	g_free (wbv->auto_expr.descr);
	wbv->auto_expr.descr = s;

	wb_view_auto_expr_recalc (wbv);
}

static void
wb_view_auto_expr_precision (WorkbookView *wbv, gboolean use_max_precision)
{
	use_max_precision = !!use_max_precision;

	if (wbv->auto_expr.use_max_precision == use_max_precision)
		return;

	wbv->auto_expr.use_max_precision = use_max_precision;

	wb_view_auto_expr_recalc (wbv);
}

static void
wb_view_auto_expr_value (WorkbookView *wbv, const GnmValue *value)
{
	value_release (wbv->auto_expr.value);
	wbv->auto_expr.value = value_dup (value);
}

static void
cb_clear_auto_expr_sheet (WorkbookView *wbv)
{
	g_object_set (G_OBJECT (wbv),
		      "auto-expr-eval-pos", NULL,
		      NULL);
}

static void
wb_view_auto_expr_eval_pos (WorkbookView *wbv, GnmEvalPos const *ep)
{
	Sheet *sheet = ep ? ep->sheet : NULL;

	if (wbv->auto_expr.sheet_detached_sig) {
		g_signal_handler_disconnect (wbv->auto_expr.dep.base.sheet,
					     wbv->auto_expr.sheet_detached_sig);
		wbv->auto_expr.sheet_detached_sig = 0;
	}

	dependent_managed_set_expr (&wbv->auto_expr.dep, NULL);
	dependent_managed_set_sheet (&wbv->auto_expr.dep, sheet);

	if (sheet) {
		GnmRange r;
		GnmValue *v;
		GnmExprTop const *texpr;

		wbv->auto_expr.sheet_detached_sig = g_signal_connect_swapped (
			G_OBJECT (sheet),
			"detached-from-workbook",
			G_CALLBACK (cb_clear_auto_expr_sheet), wbv);

		range_init_cellpos (&r, &ep->eval);
		v = value_new_cellrange_r (sheet, &r);
		texpr = gnm_expr_top_new_constant (v);
		dependent_managed_set_expr (&wbv->auto_expr.dep, texpr);
		gnm_expr_top_unref (texpr);
	}

	wb_view_auto_expr_recalc (wbv);
}

static void
wb_view_set_property (GObject *object, guint property_id,
		      const GValue *value, GParamSpec *pspec)
{
	WorkbookView *wbv = (WorkbookView *)object;

	switch (property_id) {
	case PROP_AUTO_EXPR_FUNC:
		wb_view_auto_expr_func (wbv, g_value_get_object (value));
		break;
	case PROP_AUTO_EXPR_DESCR:
		wb_view_auto_expr_descr (wbv, g_value_get_string (value));
		break;
	case PROP_AUTO_EXPR_MAX_PRECISION:
		wb_view_auto_expr_precision (wbv, g_value_get_boolean (value));
		break;
	case PROP_AUTO_EXPR_VALUE:
		wb_view_auto_expr_value (wbv, g_value_get_boxed (value));
		break;
	case PROP_AUTO_EXPR_EVAL_POS:
		wb_view_auto_expr_eval_pos (wbv, g_value_get_boxed (value));
		break;
	case PROP_SHOW_HORIZONTAL_SCROLLBAR:
		wbv->show_horizontal_scrollbar = !!g_value_get_boolean (value);
		break;
	case PROP_SHOW_VERTICAL_SCROLLBAR:
		wbv->show_vertical_scrollbar = !!g_value_get_boolean (value);
		break;
	case PROP_SHOW_NOTEBOOK_TABS:
		wbv->show_notebook_tabs = !!g_value_get_boolean (value);
		break;
	case PROP_SHOW_FUNCTION_CELL_MARKERS:
		wbv->show_function_cell_markers = !!g_value_get_boolean (value);
		if (wbv->current_sheet)
			sheet_redraw_all (wbv->current_sheet, FALSE);
		break;
	case PROP_SHOW_EXTENSION_MARKERS:
		wbv->show_extension_markers = !!g_value_get_boolean (value);
		if (wbv->current_sheet)
			sheet_redraw_all (wbv->current_sheet, FALSE);
		break;
	case PROP_DO_AUTO_COMPLETION:
		wbv->do_auto_completion = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED:
		wbv->is_protected = !!g_value_get_boolean (value);
		break;
	case PROP_PREFERRED_WIDTH:
		wbv->preferred_width = g_value_get_int (value);
		break;
	case PROP_PREFERRED_HEIGHT:
		wbv->preferred_height = g_value_get_int (value);
		break;
	case PROP_WORKBOOK:
		wbv->wb = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
wb_view_get_property (GObject *object, guint property_id,
		      GValue *value, GParamSpec *pspec)
{
	WorkbookView *wbv = (WorkbookView *)object;

	switch (property_id) {
	case PROP_AUTO_EXPR_FUNC:
		g_value_set_object (value, wbv->auto_expr.func);
		break;
	case PROP_AUTO_EXPR_DESCR:
		g_value_set_string (value, wbv->auto_expr.descr);
		break;
	case PROP_AUTO_EXPR_MAX_PRECISION:
		g_value_set_boolean (value, wbv->auto_expr.use_max_precision);
		break;
	case PROP_AUTO_EXPR_VALUE:
		g_value_set_boxed (value, wbv->auto_expr.value);
		break;
	case PROP_SHOW_HORIZONTAL_SCROLLBAR:
		g_value_set_boolean (value, wbv->show_horizontal_scrollbar);
		break;
	case PROP_SHOW_VERTICAL_SCROLLBAR:
		g_value_set_boolean (value, wbv->show_vertical_scrollbar);
		break;
	case PROP_SHOW_NOTEBOOK_TABS:
		g_value_set_boolean (value, wbv->show_notebook_tabs);
		break;
	case PROP_SHOW_FUNCTION_CELL_MARKERS:
		g_value_set_boolean (value, wbv->show_function_cell_markers);
		break;
	case PROP_SHOW_EXTENSION_MARKERS:
		g_value_set_boolean (value, wbv->show_extension_markers);
		break;
	case PROP_DO_AUTO_COMPLETION:
		g_value_set_boolean (value, wbv->do_auto_completion);
		break;
	case PROP_PROTECTED:
		g_value_set_boolean (value, wbv->is_protected);
		break;
	case PROP_PREFERRED_WIDTH:
		g_value_set_int (value, wbv->preferred_width);
		break;
	case PROP_PREFERRED_HEIGHT:
		g_value_set_int (value, wbv->preferred_height);
		break;
	case PROP_WORKBOOK:
		g_value_set_object (value, wbv->wb);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

void
wb_view_detach_from_workbook (WorkbookView *wbv)
{
	g_return_if_fail (GNM_IS_WORKBOOK_VIEW (wbv));

	if (wbv->wb) {
		workbook_detach_view (wbv);
		wbv->wb = NULL;
		wbv->current_sheet = NULL;
	}
}

static GObject *
wb_view_constructor (GType type,
		     guint n_construct_properties,
		     GObjectConstructParam *construct_params)
{
	GObject *obj;
	WorkbookView *wbv;
	int i;

	obj = parent_class->constructor
		(type, n_construct_properties, construct_params);
	wbv = GNM_WORKBOOK_VIEW (obj);

	if (wbv->wb == NULL)
		wbv->wb = workbook_new ();

	workbook_attach_view (wbv);

	for (i = 0 ; i < workbook_sheet_count (wbv->wb); i++)
		wb_view_sheet_add (wbv, workbook_sheet_by_index (wbv->wb, i));

	if (wbv->auto_expr.func == NULL) {
		wb_view_auto_expr_func (wbv, gnm_func_lookup ("sum", NULL));
		wb_view_auto_expr_descr (wbv, _("Sum"));
	}

	return obj;
}

static void
wb_view_dispose (GObject *object)
{
	WorkbookView *wbv = GNM_WORKBOOK_VIEW (object);

	if (wbv->wb_controls != NULL) {
		WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control, {
			wb_control_sheet_remove_all (control);
			wb_view_detach_control (control);
			g_object_unref (control);
		});
		if (wbv->wb_controls != NULL)
			g_warning ("Unexpected left-over controls");
	}

	/* The order of these are important.  Make sure not to leak the value.  */
	wb_view_auto_expr_descr (wbv, NULL);
	wb_view_auto_expr_eval_pos (wbv, NULL);
	wb_view_auto_expr_func (wbv, NULL);
	wb_view_auto_expr_value (wbv, NULL);

	wb_view_detach_from_workbook (wbv);

	if (wbv->current_style != NULL) {
		gnm_style_unref (wbv->current_style);
		wbv->current_style = NULL;
	}
	if (wbv->in_cell_combo != NULL) {
		sheet_object_clear_sheet (wbv->in_cell_combo);
		g_object_unref (wbv->in_cell_combo);
		wbv->in_cell_combo = NULL;
	}

	parent_class->dispose (object);
}


static void
workbook_view_class_init (GObjectClass *gobject_class)
{
	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->constructor = wb_view_constructor;
	gobject_class->set_property = wb_view_set_property;
	gobject_class->get_property = wb_view_get_property;
	gobject_class->dispose = wb_view_dispose;

        g_object_class_install_property
		(gobject_class,
		 PROP_AUTO_EXPR_FUNC,
		 g_param_spec_object ("auto-expr-func",
				      P_("Auto-expression function"),
				      P_("The automatically computed sheet function."),
				      GNM_FUNC_TYPE,
				      GSF_PARAM_STATIC |
				      G_PARAM_READWRITE));
        g_object_class_install_property
		(gobject_class,
		 PROP_AUTO_EXPR_DESCR,
		 g_param_spec_string ("auto-expr-descr",
				      P_("Auto-expression description"),
				      P_("Description of the automatically computed sheet function."),
				      NULL,
				      GSF_PARAM_STATIC |
				      G_PARAM_READWRITE));
        g_object_class_install_property
		(gobject_class,
		 PROP_AUTO_EXPR_MAX_PRECISION,
		 g_param_spec_boolean ("auto-expr-max-precision",
				       P_("Auto-expression maximum precision"),
				       P_("Use maximum available precision for auto-expressions"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
        g_object_class_install_property
		(gobject_class,
		 PROP_AUTO_EXPR_VALUE,
		 g_param_spec_boxed ("auto-expr-value",
				     P_("Auto-expression value"),
				     P_("The current value of the auto-expression."),
				     gnm_value_get_type (),
				     GSF_PARAM_STATIC |
				     G_PARAM_READWRITE));
        g_object_class_install_property
		(gobject_class,
		 PROP_AUTO_EXPR_EVAL_POS,
		 g_param_spec_boxed ("auto-expr-eval-pos",
				     P_("Auto-expression position"),
				     P_("The cell position to track."),
				     gnm_eval_pos_get_type (),
				     GSF_PARAM_STATIC |
				     G_PARAM_WRITABLE));
        g_object_class_install_property
		(gobject_class,
		 PROP_SHOW_HORIZONTAL_SCROLLBAR,
		 g_param_spec_boolean ("show-horizontal-scrollbar",
				       P_("Show horizontal scrollbar"),
				       P_("Show the horizontal scrollbar"),
				       TRUE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
        g_object_class_install_property
		(gobject_class,
		 PROP_SHOW_VERTICAL_SCROLLBAR,
		 g_param_spec_boolean ("show-vertical-scrollbar",
				       P_("Show vertical scrollbar"),
				       P_("Show the vertical scrollbar"),
				       TRUE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
        g_object_class_install_property
		(gobject_class,
		 PROP_SHOW_NOTEBOOK_TABS,
		 g_param_spec_boolean ("show-notebook-tabs",
				       P_("Show notebook tabs"),
				       P_("Show the notebook tabs for sheets"),
				       TRUE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
        g_object_class_install_property
		(gobject_class,
		 PROP_SHOW_FUNCTION_CELL_MARKERS,
		 g_param_spec_boolean ("show-function-cell-markers",
				       P_("Show formula cell markers"),
				       P_("Mark each cell containing a formula"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
        g_object_class_install_property
		(gobject_class,
		 PROP_SHOW_EXTENSION_MARKERS,
		 g_param_spec_boolean ("show-extension-markers",
				       P_("Show extension markers"),
				       P_("Mark each cell that fails to show the complete content"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_DO_AUTO_COMPLETION,
		 g_param_spec_boolean ("do-auto-completion",
				       P_("Do auto completion"),
				       P_("Auto-complete text"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
        g_object_class_install_property
		(gobject_class,
		 PROP_PROTECTED,
		 g_param_spec_boolean ("protected",
				       P_("Protected"),
				       P_("Is view protected?"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
        g_object_class_install_property
		(gobject_class,
		 PROP_PREFERRED_WIDTH,
		 g_param_spec_int ("preferred-width",
				   P_("Preferred width"),
				   P_("Preferred width"),
				   1, G_MAXINT, 1024,
				   GSF_PARAM_STATIC |
				   G_PARAM_READWRITE));
        g_object_class_install_property
		(gobject_class,
		 PROP_PREFERRED_HEIGHT,
		 g_param_spec_int ("preferred-height",
				   P_("Preferred height"),
				   P_("Preferred height"),
				   1, G_MAXINT, 768,
				   GSF_PARAM_STATIC |
				   G_PARAM_READWRITE));
        g_object_class_install_property
		(gobject_class,
		 PROP_WORKBOOK,
		 g_param_spec_object ("workbook",
				      P_("Workbook"),
				      P_("Workbook"),
				      GNM_WORKBOOK_TYPE,
				      GSF_PARAM_STATIC |
				      G_PARAM_CONSTRUCT_ONLY |
				      G_PARAM_READWRITE));

	parent_class = g_type_class_peek_parent (gobject_class);
}

static void
workbook_view_init (WorkbookView *wbv)
{
	wbv->show_horizontal_scrollbar = TRUE;
	wbv->show_vertical_scrollbar = TRUE;
	wbv->show_notebook_tabs = TRUE;
	wbv->show_function_cell_markers =
		gnm_conf_get_core_gui_cells_function_markers ();
	wbv->show_extension_markers =
		gnm_conf_get_core_gui_cells_extension_markers ();
	wbv->do_auto_completion =
		gnm_conf_get_core_gui_editing_autocomplete ();
	wbv->is_protected = FALSE;

	dependent_managed_init (&wbv->auto_expr.dep, NULL);
}

GSF_CLASS (WorkbookView, workbook_view,
	   workbook_view_class_init, workbook_view_init, GO_TYPE_VIEW)

/**
 * workbook_view_new:
 * @wb: (allow-none) (transfer full): #Workbook
 *
 * Returns: A new #WorkbookView for @wb (or a fresh one if that is %NULL).
 **/
WorkbookView *
workbook_view_new (Workbook *wb)
{
	WorkbookView *wbv =
		g_object_new (GNM_WORKBOOK_VIEW_TYPE, "workbook", wb, NULL);
	if (wb) g_object_unref (wb);

	return wbv;
}

/**
 * workbook_view_save_to_output:
 * @wbv: #WorkbookView
 * @fs: #GOFileSaver
 * @output: #GsfOutput
 * @io_context: #GOIOContext
 *
 * NOTE : Temporary api until we get the new output framework.
 **/
void
workbook_view_save_to_output (WorkbookView *wbv, GOFileSaver const *fs,
			      GsfOutput *output, GOIOContext *io_context)
{
	GError const *err;
	char const   *msg;
	GODoc *godoc = wb_view_get_doc (wbv);

	if (go_doc_is_dirty (godoc))
	  /* FIXME: we should be using the true modification time */
	  gnm_insert_meta_date (godoc, GSF_META_NAME_DATE_MODIFIED);
	go_file_saver_save (fs, io_context, GO_VIEW (wbv), output);

	/* The plugin convention is unclear */
	if (!gsf_output_is_closed (output))
		gsf_output_close (output);

	if (NULL == (err = gsf_output_error (output)))
		return;
	if (NULL == (msg = err->message))
		msg = _("An unexplained error happened while saving.");
	g_printerr ("  ==> %s\n", msg);
	if (!go_io_error_occurred (io_context))
		go_cmd_context_error_export (GO_CMD_CONTEXT (io_context), msg);
}

/**
 * workbook_view_save_to_uri:
 * @wbv: #WorkbookView
 * @fs: #GOFileSaver
 * @uri: destination URI
 * @io_context: #GOIOContext
 *
 **/
void
workbook_view_save_to_uri (WorkbookView *wbv, GOFileSaver const *fs,
			   char const *uri, GOIOContext *io_context)
{
	char   *msg = NULL;
	GError *err = NULL;
	GsfOutput *output = go_file_create (uri, &err);

	if (output == NULL) {
		if (NULL != err) {
			msg = g_strdup_printf (_("Can't open '%s' for writing: %s"),
						     uri, err->message);
			g_error_free (err);
		} else
			msg = g_strdup_printf (_("Can't open '%s' for writing"), uri);

		go_cmd_context_error_export (GO_CMD_CONTEXT (io_context), msg);
		g_free (msg);
	} else {
		workbook_view_save_to_output (wbv, fs, output, io_context);
		g_object_unref (output);
	}
}

static GDateTime *
get_uri_modtime (GsfInput *input, const char *uri)
{
	GDateTime *modtime = NULL;

	if (input) {
		modtime = gsf_input_get_modtime (input);
		if (modtime)
			g_date_time_ref (modtime);
	}

	if (!modtime && uri)
		modtime = go_file_get_modtime (uri);

	if (gnm_debug_flag ("modtime")) {
		char *s = modtime
			? g_date_time_format (modtime, "%F %T")
			: g_strdup ("?");
		g_printerr ("Modtime of %s is %s\n", uri, s);
		g_free (s);
	}

	return modtime;
}

/**
 * workbook_view_save_as:
 * @wbv: Workbook View
 * @fs: GOFileSaver object
 * @uri: URI to save as.
 * @cc: The #GOCmdContext that invoked the operation
 *
 * Saves @wbv and workbook it's attached to into @uri file using
 * @fs file saver.  If the format sufficiently advanced make it the saver
 * and update the uri.
 *
 * Return value: %TRUE if file was successfully saved and %FALSE otherwise.
 */
gboolean
workbook_view_save_as (WorkbookView *wbv, GOFileSaver *fs, char const *uri,
		       GOCmdContext *cc)
{
	GOIOContext *io_context;
	Workbook  *wb;
	gboolean has_error, has_warning;

	g_return_val_if_fail (GNM_IS_WORKBOOK_VIEW (wbv), FALSE);
	g_return_val_if_fail (GO_IS_FILE_SAVER (fs), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (GO_IS_CMD_CONTEXT (cc), FALSE);

	wb = wb_view_get_workbook (wbv);
	g_object_ref (wb);
	io_context = go_io_context_new (cc);

	go_cmd_context_set_sensitive (cc, FALSE);
	workbook_view_save_to_uri (wbv, fs, uri, io_context);
	go_cmd_context_set_sensitive (cc, TRUE);

	has_error   = go_io_error_occurred (io_context);
	has_warning = go_io_warning_occurred (io_context);
	if (!has_error) {
		GOFileFormatLevel fl = go_file_saver_get_format_level (fs);
		if (workbook_set_saveinfo (wb, fl, fs)) {
			if (go_doc_set_uri (GO_DOC (wb), uri)) {
				GDateTime *modtime;

				go_doc_set_saved_state (GO_DOC (wb),
							go_doc_get_state (GO_DOC (wb)));
				go_doc_set_dirty (GO_DOC (wb), FALSE);
				/* See 634792.  */
				go_doc_set_pristine (GO_DOC (wb), FALSE);

				modtime = get_uri_modtime (NULL, uri);
				if (modtime) {
					go_doc_set_modtime (GO_DOC (wb), modtime);
					if (gnm_debug_flag ("modtime"))
						g_printerr ("Modtime set\n");
					g_date_time_unref (modtime);
				}
			}
		} else
			workbook_set_last_export_uri (wb, uri);
	}
	if (has_error || has_warning)
		go_io_error_display (io_context);
	g_object_unref (io_context);
	g_object_unref (wb);

	return !has_error;
}

/**
 * workbook_view_save:
 * @wbv: The view to save.
 * @cc: The #GOCmdContext that invoked the operation
 *
 * Saves @wbv and workbook it's attached to into file assigned to the
 * workbook using workbook's file saver. If the workbook has no file
 * saver assigned to it, default file saver is used instead.
 *
 * Return value: %TRUE if file was successfully saved and %FALSE otherwise.
 */
gboolean
workbook_view_save (WorkbookView *wbv, GOCmdContext *context)
{
	GOIOContext	*io_context;
	Workbook	*wb;
	GOFileSaver	*fs;
	gboolean has_error, has_warning;
	char const *uri;

	g_return_val_if_fail (GNM_IS_WORKBOOK_VIEW (wbv), FALSE);
	g_return_val_if_fail (GO_IS_CMD_CONTEXT (context), FALSE);

	wb = wb_view_get_workbook (wbv);
	g_object_ref (wb);
	uri = go_doc_get_uri (GO_DOC (wb));

	fs = workbook_get_file_saver (wb);
	if (fs == NULL)
		fs = go_file_saver_get_default ();

	io_context = go_io_context_new (context);
	if (fs == NULL)
		go_cmd_context_error_export (GO_CMD_CONTEXT (io_context),
			_("Default file saver is not available."));
	else {
		char const *uri = go_doc_get_uri (GO_DOC (wb));
		workbook_view_save_to_uri (wbv, fs, uri, io_context);
	}

	has_error   = go_io_error_occurred (io_context);
	has_warning = go_io_warning_occurred (io_context);
	if (!has_error) {
		GDateTime *modtime = get_uri_modtime (NULL, uri);
		go_doc_set_modtime (GO_DOC (wb), modtime);
		if (gnm_debug_flag ("modtime"))
			g_printerr ("Modtime set\n");
		g_date_time_unref (modtime);
		go_doc_set_saved_state (GO_DOC (wb),
					go_doc_get_state (GO_DOC (wb)));
		go_doc_set_dirty (GO_DOC (wb), FALSE);
	}
	if (has_error || has_warning)
		go_io_error_display (io_context);

	g_object_unref (io_context);
	g_object_unref (wb);

	return !has_error;
}

/**
 * workbook_view_new_from_input:
 * @input: #GsfInput to read data from.
 * @uri: (allow-none): URI
 * @file_opener: (allow-none): #GOFileOpener
 * @io_context: (allow-none): Context to display errors.
 * @encoding: (allow-none): Encoding for @file_opener that understand it
 *
 * Reads @uri file using given file opener @file_opener, or probes for a valid
 * possibility if @file_opener is %NULL.  Reports problems to @io_context.
 *
 * Return value: (transfer full) (nullable): the newly allocated WorkbookView
 * or %NULL on error.
 **/
WorkbookView *
workbook_view_new_from_input (GsfInput *input,
                              const char *uri,
                              GOFileOpener const *file_opener,
                              GOIOContext *io_context,
                              char const *encoding)
{
	WorkbookView *new_wbv = NULL;

	g_return_val_if_fail (GSF_IS_INPUT(input), NULL);
	g_return_val_if_fail (file_opener == NULL ||
			      GO_IS_FILE_OPENER (file_opener), NULL);

	/* NOTE : we could support gzipped anything here if we wanted to
	 * by adding a wrapper, but there is no framework for remembering that
	 * the file was gzipped so let's not just yet.
	 */

	/* Search for an applicable opener */
	if (file_opener == NULL) {
		GOFileProbeLevel pl;
		GList *l;
		int input_refs = G_OBJECT (input)->ref_count;

		for (pl = GO_FILE_PROBE_FILE_NAME; pl < GO_FILE_PROBE_LAST && file_opener == NULL; pl++) {
			for (l = go_get_file_openers (); l != NULL; l = l->next) {
				GOFileOpener const *tmp_fo = GO_FILE_OPENER (l->data);
				int new_input_refs;
				/* A name match needs to be a content match too */
				if (go_file_opener_probe (tmp_fo, input, pl) &&
				    (pl == GO_FILE_PROBE_CONTENT ||
				     !go_file_opener_can_probe	(tmp_fo, GO_FILE_PROBE_CONTENT) ||
				     go_file_opener_probe (tmp_fo, input, GO_FILE_PROBE_CONTENT)))
					file_opener = tmp_fo;

				new_input_refs = G_OBJECT (input)->ref_count;
				if (new_input_refs != input_refs) {
					g_warning ("Format %s's probe changed input ref_count from %d to %d.",
						   go_file_opener_get_id (tmp_fo),
						   input_refs,
						   new_input_refs);
					input_refs = new_input_refs;
				}

				if (file_opener)
					break;
			}
		}
	}

	if (file_opener != NULL) {
		Workbook *new_wb;
		gboolean old;
		GDateTime *modtime;

		new_wbv = workbook_view_new (NULL);
		new_wb = wb_view_get_workbook (new_wbv);
		if (uri)
			go_doc_set_uri (GO_DOC (new_wb), uri);

		// Grab the modtime before we actually do the reading
		modtime = get_uri_modtime (input, uri);
		go_doc_set_modtime (GO_DOC (new_wb), modtime);
		if (modtime)
			g_date_time_unref (modtime);

		/* disable recursive dirtying while loading */
		old = workbook_enable_recursive_dirty (new_wb, FALSE);
		g_object_set (new_wb, "being-loaded", TRUE, NULL);
		go_file_opener_open (file_opener, encoding, io_context,
		                     GO_VIEW (new_wbv), input);
		g_object_set (new_wb, "being-loaded", FALSE, NULL);
		workbook_enable_recursive_dirty (new_wb, old);

		if (go_io_error_occurred (io_context)) {
			g_object_unref (new_wb);
			new_wbv = NULL;
		} else if (workbook_sheet_count (new_wb) == 0) {
			/* we didn't get a sheet nor an error, */
			/* the user must have canceled        */
			g_object_unref (new_wb);
			new_wbv = NULL;
		} else {
			workbook_share_expressions (new_wb, TRUE);
			workbook_optimize_style (new_wb);
			workbook_queue_volatile_recalc (new_wb);
			workbook_recalc (new_wb);
			workbook_update_graphs (new_wb);
			go_doc_set_saved_state
				(GO_DOC (new_wb),
				 go_doc_get_state (GO_DOC (new_wb)));
			if (uri && workbook_get_file_exporter (new_wb))
				workbook_set_last_export_uri
					(new_wb, uri);
		}
	} else {
		if (io_context) {
			char *bn = go_basename_from_uri (uri);
			char *errtxt = g_strdup_printf
				(_("Unsupported file format for file \"%s\""),
				 bn);
			go_cmd_context_error_import
				(GO_CMD_CONTEXT (io_context),
				 errtxt);
			g_free (errtxt);
			g_free (bn);
		}
	}

	return new_wbv;
}

/**
 * workbook_view_new_from_uri:
 * @uri: URI for file
 * @file_opener: (allow-none): #GOFileOpener
 * @io_context: Context to display errors.
 * @encoding: (allow-none): Encoding for @file_opener that understands it
 *
 * Reads @uri file using given file opener @file_opener, or probes for a valid
 * possibility if @file_opener is %NULL.  Reports problems to @io_context.
 *
 * Return value: (transfer full) (nullable): the newly allocated WorkbookView
 * or %NULL on error.
 **/
WorkbookView *
workbook_view_new_from_uri (char const *uri,
			    GOFileOpener const *file_opener,
			    GOIOContext *io_context,
			    char const *encoding)
{
	char *msg = NULL;
	GError *err = NULL;
	GsfInput *input;

	g_return_val_if_fail (uri != NULL, NULL);

	input = go_file_open (uri, &err);
	if (input != NULL) {
		WorkbookView *res;

		res = workbook_view_new_from_input (input, uri,
						    file_opener, io_context,
						    encoding);
		g_object_unref (input);
		return res;
	}

	if (err != NULL) {
		if (err->message != NULL)
			msg = g_strdup (err->message);
		g_error_free (err);
	}

	if (msg == NULL)
		msg = g_strdup_printf (_("An unexplained error happened while opening %s"),
				       uri);

	if (io_context)
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context), msg);
	g_free (msg);

	return NULL;
}
