/* vim: set sw=8: */
/*
 * workbook-view.c: View functions for the workbook
 *
 * Copyright (C) 2000-2006 Jody Goldberg (jody@gnome.org)
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
#include "gnumeric.h"
#include "workbook-view.h"

#include "workbook-control-priv.h"
#include "workbook-priv.h"
#include "application.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-merge.h"
#include "sheet-style.h"
#include "str.h"
#include "gnm-format.h"
#include "func.h"
#include "expr.h"
#include "expr-name.h"
#include "expr-impl.h"
#include "value.h"
#include "ranges.h"
#include "selection.h"
#include "mstyle.h"
#include "validation.h"
#include "validation-combo.h"
#include "position.h"
#include "cell.h"
#include "gutils.h"
#include "command-context.h"
#include "auto-format.h"
#include "sheet-object.h"

#include <goffice/app/file.h>
#include <goffice/app/go-doc.h>
#include <goffice/app/io-context.h>
#include <goffice/utils/go-file.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-locale.h>
#include <gsf/gsf.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-output-stdio.h>
#include <gsf/gsf-input.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include "mathfunc.h"

enum {
	PROP_0,
	PROP_AUTO_EXPR_FUNC,
	PROP_AUTO_EXPR_DESCR,
	PROP_AUTO_EXPR_MAX_PRECISION,
	PROP_AUTO_EXPR_TEXT
};

/* WorkbookView signals */
enum {
	LAST_SIGNAL
};

/**
 * wb_view_get_workbook :
 * @wbv : #WorkbookView
 *
 * Return the #Workbook assciated with @wbv
 **/
Workbook *
wb_view_get_workbook (WorkbookView const *wbv)
{
	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), NULL);
	return wbv->wb;
}

/**
 * wb_view_get_doc :
 * @wbv : #WorkbookView
 *
 * Return the #Workbook assciated with @wbv cast to a #GODoc
 **/
GODoc *
wb_view_get_doc (WorkbookView const *wbv)
{
	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), NULL);
	return GO_DOC (wbv->wb);
}

/**
 * wb_view_get_index_in_wb :
 * @wbv : #WorkbookView
 *
 * Returns 0 based index of wbv within workbook, or -1 if there is no workbook.
 **/
int
wb_view_get_index_in_wb (WorkbookView const *wbv)
{
	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), -1);
	if (NULL != wbv->wb) {
		unsigned i = wbv->wb->wb_views->len;
		while (i-- > 0)
			if (g_ptr_array_index (wbv->wb->wb_views, i) == wbv)
				return i;
	}
	return -1;
}

Sheet *
wb_view_cur_sheet (WorkbookView const *wbv)
{
	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), NULL);
	return wbv->current_sheet;
}

SheetView *
wb_view_cur_sheet_view (WorkbookView const *wbv)
{
	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), NULL);
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

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	new_view = sheet_view_new (new_sheet, wbv);

	WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control,
		wb_control_sheet_add (control, new_view););

	g_object_unref (new_view);

	if (wbv->current_sheet == NULL)
		wb_view_sheet_focus (wbv, new_sheet);
}

gboolean
wb_view_is_protected (WorkbookView *wbv, gboolean check_sheet)
{
	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), FALSE);

	return wbv->is_protected || (check_sheet &&
		wbv->current_sheet != NULL && wbv->current_sheet->is_protected);
}

void
wb_view_set_attribute (WorkbookView *wbv, char const *name,
		       char const *value)
{
	gboolean res;

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));
	g_return_if_fail (name != NULL);
	g_return_if_fail (value != NULL);

	res = !g_ascii_strcasecmp (value, "TRUE");
	if (!strcmp (name , "WorkbookView::show_horizontal_scrollbar"))
		wbv->show_horizontal_scrollbar = res;
	else if (!strcmp (name , "WorkbookView::show_vertical_scrollbar"))
		wbv->show_vertical_scrollbar = res;
	else if (!strcmp (name , "WorkbookView::show_notebook_tabs"))
		wbv->show_notebook_tabs = res;
	else if (!strcmp (name , "WorkbookView::do_auto_completion"))
		wbv->do_auto_completion = res;
	else if (!strcmp (name , "WorkbookView::is_protected"))
		wbv->is_protected = res;
	else
		g_warning ("WorkbookView unknown arg '%s'", name);
}

void
wb_view_preferred_size (WorkbookView *wbv, int w, int h)
{
	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	/* FIXME : should we notify the controls ? */
	wbv->preferred_width = w;
	wbv->preferred_height = h;
}

void
wb_view_style_feedback (WorkbookView *wbv)
{
	SheetView *sv;
	GnmStyle const *style;
	GnmValidation const *val;
	GOFormat *fmt_style, *fmt_cell;
	GnmCell *cell;
	gboolean update_controls = TRUE;

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

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

	if (wbv->validation_combo != NULL) {
		sheet_object_clear_sheet (wbv->validation_combo);
		g_object_unref (wbv->validation_combo);
		wbv->validation_combo = NULL;
	}

	if (gnm_style_is_element_set (style, MSTYLE_VALIDATION) &&
	    NULL != (val = gnm_style_get_validation (style)) &&
	    val->type == VALIDATION_TYPE_IN_LIST &&
	    val->use_dropdown) {
		float const a_offsets [4] = { 0., 0., 1., 1. };
		SheetObjectAnchor  anchor;
		GnmRange corner;
		GnmRange const *r;

		if (NULL == (r = gnm_sheet_merge_contains_pos (sv->sheet, &sv->edit_pos)))
			r = range_init_cellpos_size (&corner, &sv->edit_pos, 1, 1);
		wbv->validation_combo = gnm_validation_combo_new (val, sv);
		sheet_object_anchor_init (&anchor, r, a_offsets,
			GOD_ANCHOR_DIR_DOWN_RIGHT);
		sheet_object_set_anchor (wbv->validation_combo, &anchor);
		sheet_object_set_sheet (wbv->validation_combo, sv_sheet (sv));
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

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	sheet = wbv->current_sheet;
	if (sheet != NULL) {
		WORKBOOK_VIEW_FOREACH_CONTROL (wbv, wbc, {
			wb_control_menu_state_update (wbc, MS_ALL);
			wb_control_update_action_sensitivity (wbc);
		});
	}
}

void
wb_view_selection_desc (WorkbookView *wbv, gboolean use_pos,
			WorkbookControl *optional_wbc)
{
	SheetView *sv;

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	sv = wbv->current_sheet_view;
	if (sv != NULL) {
		char buffer [10 + 2 * 4 * sizeof (int)];
		char const *sel_descr = buffer;
		GnmRange const *r, *m;

		g_return_if_fail (IS_SHEET_VIEW (sv));
		g_return_if_fail (sv->selections);

		r = sv->selections->data;

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
				snprintf (buffer, sizeof (buffer), _("%dC"), cols);
			else if (cols == gnm_sheet_get_max_cols (sv->sheet))
				snprintf (buffer, sizeof (buffer), _("%dR"), rows);
			else
				snprintf (buffer, sizeof (buffer), _("%dR x %dC"),
					  rows, cols);
		}

		if (optional_wbc == NULL) {
			WORKBOOK_VIEW_FOREACH_CONTROL (wbv, wbc,
				wb_control_selection_descr_set (wbc, sel_descr););
		} else
			wb_control_selection_descr_set (optional_wbc, sel_descr);
	}
}

/**
 * Load the edit line with the value of the cell in @sheet's edit_pos.
 *
 * @wbv : The view
 * @wbc : An Optional control
 *
 * Calculate what to display on the edit line then display it either in the
 * control @wbc,  or if that is NULL, in all controls.
 */
void
wb_view_edit_line_set (WorkbookView *wbv, WorkbookControl *optional_wbc)
{
	SheetView *sv;

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	sv = wbv->current_sheet_view;
	if (sv != NULL) {
		char *text;
		Sheet *sheet = sv->sheet;
		GnmCell const *cell = sheet_cell_get (sheet,
			sv->edit_pos.col, sv->edit_pos.row);

		if (NULL != cell) {
			text = gnm_cell_get_entered_text (cell);

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
					GnmExprArrayCorner const *ac = gnm_cell_is_array_corner (corner);

					char *tmp = g_strdup_printf
						("{%s}(%d%c%d)[%d][%d]",
						 text,
						 ac->cols,
						 go_locale_get_arg_sep (),
						 ac->rows,
						 x, y);
					g_free (text);
					text = tmp;
				}
			}
		} else
			text = g_strdup ("");

		if (optional_wbc == NULL) {
			WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control,
				wb_control_edit_line_set (control, text););
		} else
			wb_control_edit_line_set (optional_wbc, text);

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
	GnmFuncEvalInfo ei;
	GnmEvalPos      ep;
	GnmExprList	*selection = NULL;
	GnmValue	*v;
	SheetView	*sv;
	GnmExpr const   *expr;

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	sv = wb_view_cur_sheet_view (wbv);
	if (wbv->current_sheet == NULL ||
	    wbv->auto_expr_func == NULL ||
	    sv == NULL)
		return;

	sv_selection_apply (sv, &accumulate_regions, FALSE, &selection);

	expr = gnm_expr_new_funcall (wbv->auto_expr_func, selection);

	ei.pos = eval_pos_init_sheet (&ep, wbv->current_sheet);
	ei.func_call = &expr->func;
	v = function_call_with_exprs (&ei, 0);

	if (v) {
		GString *str = g_string_new (wbv->auto_expr_descr);
		GOFormat const *format = NULL;
		GOFormat *tmp_format = NULL;

		g_string_append_c (str, '=');
		if (!wbv->auto_expr_use_max_precision) {
			format = VALUE_FMT (v);
			if (!format) {
				const GnmExprTop *fcall =
					gnm_expr_top_new (expr);
				expr = NULL;
				format = tmp_format =
					auto_style_format_suggest (fcall, ei.pos);
				gnm_expr_top_unref (fcall);
			}
		}

		if (format) {
			format_value_gstring (str, format, v, NULL,
					      -1, workbook_date_conv (wb_view_get_workbook (wbv)));
			if (tmp_format)
				go_format_unref (tmp_format);
		} else {
			g_string_append (str, value_peek_string (v));
		}

		g_object_set (wbv, "auto-expr-text", str->str, NULL);

		g_string_free (str, TRUE);
		value_release (v);
	} else {
		g_object_set (wbv, "auto-expr-text", "Internal ERROR", NULL);
	}

	if (expr)
		gnm_expr_free (expr);
}

/* perform whatever initialization of a control that is necessary when it
 * finally gets assigned to a view with a workbook */
static void
wb_view_init_control (WorkbookControl *wbc)
{
}

void
wb_view_attach_control (WorkbookView *wbv, WorkbookControl *wbc)
{
	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));
	g_return_if_fail (IS_WORKBOOK_CONTROL (wbc));
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
	g_return_if_fail (IS_WORKBOOK_CONTROL (wbc));
	g_return_if_fail (IS_WORKBOOK_VIEW (wb_control_view (wbc)));

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
	if (wbv->auto_expr_func == func)
		return;

	if (wbv->auto_expr_func)
		gnm_func_unref (wbv->auto_expr_func);

	if (func)
		gnm_func_ref (func);
	wbv->auto_expr_func = func;

	wb_view_auto_expr_recalc (wbv);
}

static void
wb_view_auto_expr_descr (WorkbookView *wbv, const char *descr)
{
	char *s;

	if (go_str_compare (descr, wbv->auto_expr_descr) == 0)
		return;

	s = g_strdup (descr);
	g_free (wbv->auto_expr_descr);
	wbv->auto_expr_descr = s;

	wb_view_auto_expr_recalc (wbv);
}

static void
wb_view_auto_expr_precision (WorkbookView *wbv, gboolean use_max_precision)
{
	use_max_precision = !!use_max_precision;

	if (wbv->auto_expr_use_max_precision == use_max_precision)
		return;

	wbv->auto_expr_use_max_precision = use_max_precision;

	wb_view_auto_expr_recalc (wbv);
}

static void
wb_view_auto_expr_text (WorkbookView *wbv, const char *text)
{
	char *s;

	if (go_str_compare (text, wbv->auto_expr_text) == 0)
		return;

	s = g_strdup (text);
	g_free (wbv->auto_expr_text);
	wbv->auto_expr_text = s;
}

static void
wb_view_set_property (GObject *object, guint property_id,
		      const GValue *value, GParamSpec *pspec)
{
	WorkbookView *wbv = (WorkbookView *)object;

	switch (property_id) {
	case PROP_AUTO_EXPR_FUNC:
		wb_view_auto_expr_func (wbv, g_value_get_pointer (value));
		break;
	case PROP_AUTO_EXPR_DESCR:
		wb_view_auto_expr_descr (wbv, g_value_get_string (value));
		break;
	case PROP_AUTO_EXPR_MAX_PRECISION:
		wb_view_auto_expr_precision (wbv, g_value_get_boolean (value));
		break;
	case PROP_AUTO_EXPR_TEXT:
		wb_view_auto_expr_text (wbv, g_value_get_string (value));
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
		g_value_set_pointer (value, wbv->auto_expr_func);
		break;
	case PROP_AUTO_EXPR_DESCR:
		g_value_set_string (value, wbv->auto_expr_descr);
		break;
	case PROP_AUTO_EXPR_MAX_PRECISION:
		g_value_set_boolean (value, wbv->auto_expr_use_max_precision);
		break;
	case PROP_AUTO_EXPR_TEXT:
		g_value_set_string (value, wbv->auto_expr_text);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

void
wb_view_detach_from_workbook (WorkbookView *wbv)
{
	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	if (wbv->wb) {
		workbook_detach_view (wbv);
		wbv->wb = NULL;
	}
}

static void
wb_view_dispose (GObject *object)
{
	WorkbookView *wbv = WORKBOOK_VIEW (object);

	if (wbv->wb_controls != NULL) {
		WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control, {
			wb_control_sheet_remove_all (control);
			wb_view_detach_control (control);
			g_object_unref (G_OBJECT (control));
		});
		if (wbv->wb_controls != NULL)
			g_warning ("Unexpected left-over controls");
	}

	wb_view_detach_from_workbook (wbv);

	parent_class->dispose (object);
}


static void
wb_view_finalize (GObject *object)
{
	WorkbookView *wbv = WORKBOOK_VIEW (object);

	if (wbv->auto_expr_func) {
		gnm_func_unref (wbv->auto_expr_func);
		wbv->auto_expr_func = NULL;
	}

	g_free (wbv->auto_expr_descr);
	wbv->auto_expr_descr = NULL;

	g_free (wbv->auto_expr_text);
	wbv->auto_expr_text = NULL;

	if (wbv->current_style != NULL) {
		gnm_style_unref (wbv->current_style);
		wbv->current_style = NULL;
	}
	if (wbv->validation_combo != NULL) {
		sheet_object_clear_sheet (wbv->validation_combo);
		g_object_unref (wbv->validation_combo);
		wbv->validation_combo = NULL;
	}

	parent_class->finalize (object);
}

static void
workbook_view_class_init (GObjectClass *gobject_class)
{
	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->set_property = wb_view_set_property;
	gobject_class->get_property = wb_view_get_property;
	gobject_class->finalize = wb_view_finalize;
	gobject_class->dispose = wb_view_dispose;

	/* FIXME?  Make a boxed type.  */
        g_object_class_install_property
		(gobject_class,
		 PROP_AUTO_EXPR_FUNC,
		 g_param_spec_pointer ("auto-expr-func",
				       _("Auto-expression function"),
				       _("The automatically computed sheet function."),
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
        g_object_class_install_property
		(gobject_class,
		 PROP_AUTO_EXPR_DESCR,
		 g_param_spec_string ("auto-expr-descr",
				      _("Auto-expression description"),
				      _("Description of the automatically computed sheet function."),
				      NULL,
				      GSF_PARAM_STATIC |
				      G_PARAM_READWRITE));
        g_object_class_install_property
		(gobject_class,
		 PROP_AUTO_EXPR_MAX_PRECISION,
		 g_param_spec_boolean ("auto-expr-max-precision",
				       _("Auto-expression maximum precision"),
				       _("Use maximum available precision for auto-expressions"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
        g_object_class_install_property
		(gobject_class,
		 PROP_AUTO_EXPR_TEXT,
		 g_param_spec_string ("auto-expr-text",
				      _("Auto-expression text"),
				      _("Displayed text for the automatically computed sheet function."),
				      NULL,
				      GSF_PARAM_STATIC |
				      G_PARAM_READWRITE));

	parent_class = g_type_class_peek_parent (gobject_class);
}

GSF_CLASS (WorkbookView, workbook_view,
	   workbook_view_class_init, NULL, G_TYPE_OBJECT);

WorkbookView *
workbook_view_new (Workbook *wb)
{
	WorkbookView *wbv = g_object_new (WORKBOOK_VIEW_TYPE, NULL);
	int i;

	if (wb == NULL)
		wb = workbook_new ();

	g_return_val_if_fail (wb != NULL, NULL);

	wbv->wb = wb;
	workbook_attach_view (wbv);

	wbv->show_horizontal_scrollbar = TRUE;
	wbv->show_vertical_scrollbar = TRUE;
	wbv->show_notebook_tabs = TRUE;
	wbv->do_auto_completion = gnm_app_use_auto_complete ();
	wbv->is_protected = FALSE;

	wbv->current_style      = NULL;
	wbv->validation_combo   = NULL;

	wbv->current_sheet      = NULL;
	wbv->current_sheet_view = NULL;

	/* Set the default operation to be performed over selections */
	wbv->auto_expr_func = gnm_func_lookup ("sum", NULL);
	if (wbv->auto_expr_func)
		gnm_func_ref (wbv->auto_expr_func);
	wbv->auto_expr_descr = g_strdup (_("Sum"));
	wbv->auto_expr_text = NULL;
	wbv->auto_expr_use_max_precision = FALSE;

	for (i = 0 ; i < workbook_sheet_count (wb); i++)
		wb_view_sheet_add (wbv, workbook_sheet_by_index (wb, i));

	WORKBOOK_VIEW_FOREACH_CONTROL (wbv, wbc,
		wb_view_init_control (wbc););

	return wbv;
}

/**
 * wbv_save_to_output :
 * @wbv : #WorkbookView
 * @fs  : #GOFileSaver
 * @output : #GsfOutput
 * @io_context : #IOContext
 *
 * NOTE : Temporary api until we get the new output framework.
 **/
void
wbv_save_to_output (WorkbookView *wbv, GOFileSaver const *fs,
		    GsfOutput *output, IOContext *io_context)
{
	GError const *err;
	char const   *msg;

	go_file_saver_save (fs, io_context, wbv, output);

	/* The plugin convention is unclear */
	if (!gsf_output_is_closed (output))
		gsf_output_close (output);

	if (NULL == (err = gsf_output_error (output)))
		return;
	if (NULL == (msg = err->message))
		msg = _("An unexplained error happened while saving.");
	g_printerr ("  ==> %s\n", msg);
	if (!gnumeric_io_error_occurred (io_context))
		go_cmd_context_error_export (GO_CMD_CONTEXT (io_context), msg);
}

static void
wbv_save_to_uri (WorkbookView *wbv, GOFileSaver const *fs,
		 char const *uri, IOContext *io_context)
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
		g_printerr ("Writing %s\n", uri);
		wbv_save_to_output (wbv, fs, output, io_context);
		g_object_unref (output);
	}
}

/**
 * wb_view_save_as:
 * @wbv         : Workbook View
 * @fs          : GOFileSaver object
 * @uri         : URI to save as.
 * @context     :
 *
 * Saves @wbv and workbook it's attached to into @uri file using
 * @fs file saver.  If the format sufficiently advanced make it the saver
 * and update the uri.
 *
 * Return value: TRUE if file was successfully saved and FALSE otherwise.
 */
gboolean
wb_view_save_as (WorkbookView *wbv, GOFileSaver *fs, char const *uri,
		 GOCmdContext *context)
{
	IOContext *io_context;
	Workbook  *wb;
	gboolean has_error, has_warning;

	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), FALSE);
	g_return_val_if_fail (IS_GO_FILE_SAVER (fs), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (IS_GO_CMD_CONTEXT (context), FALSE);

	wb = wb_view_get_workbook (wbv);
	g_object_ref (wb);
	io_context = gnumeric_io_context_new (context);

	go_cmd_context_set_sensitive (context, FALSE);
	wbv_save_to_uri (wbv, fs, uri, io_context);
	go_cmd_context_set_sensitive (context, TRUE);

	has_error   = gnumeric_io_error_occurred (io_context);
	has_warning = gnumeric_io_warning_occurred (io_context);
	if (!has_error) {
		if (workbook_set_saveinfo (wb,
			go_file_saver_get_format_level (fs), fs) &&
		    go_doc_set_uri (GO_DOC (wb), uri))
			go_doc_set_dirty (GO_DOC (wb), FALSE);
	}
	if (has_error || has_warning)
		gnumeric_io_error_display (io_context);
	g_object_unref (G_OBJECT (io_context));
	g_object_unref (wb);

	return !has_error;
}

/**
 * wb_view_save:
 * @wbv         : The view to save.
 * @context     : The context that invoked the operation
 *
 * Saves @wbv and workbook it's attached to into file assigned to the
 * workbook using workbook's file saver. If the workbook has no file
 * saver assigned to it, default file saver is used instead.
 *
 * Return value: TRUE if file was successfully saved and FALSE otherwise.
 */
gboolean
wb_view_save (WorkbookView *wbv, GOCmdContext *context)
{
	IOContext	*io_context;
	Workbook	*wb;
	GOFileSaver	*fs;
	gboolean has_error, has_warning;

	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), FALSE);
	g_return_val_if_fail (IS_GO_CMD_CONTEXT (context), FALSE);

	wb = wb_view_get_workbook (wbv);
	g_object_ref (wb);

	fs = workbook_get_file_saver (wb);
	if (fs == NULL)
		fs = go_file_saver_get_default ();

	io_context = gnumeric_io_context_new (context);
	if (fs == NULL)
		go_cmd_context_error_export (GO_CMD_CONTEXT (io_context),
			_("Default file saver is not available."));
	else {
		char const *uri = go_doc_get_uri (GO_DOC (wb));
		wbv_save_to_uri (wbv, fs, uri, io_context);
	}

	has_error   = gnumeric_io_error_occurred (io_context);
	has_warning = gnumeric_io_warning_occurred (io_context);
	if (!has_error)
		go_doc_set_dirty (GO_DOC (wb), FALSE);
	if (has_error || has_warning)
		gnumeric_io_error_display (io_context);

	g_object_unref (G_OBJECT (io_context));
	g_object_unref (wb);

	return !has_error;
}

#ifndef GNM_WITH_GNOME
static void
gnm_mailto_url_show (char const *url, char const *working_dir, GError **err)
{
	static struct {
		char const *app;
		char const *arg;
	} const fallback_mailers[] = {
		{ "evolution",			NULL },
		{ "evolution-1.6",		NULL },
		{ "evolution-1.5",		NULL },
		{ "evolution-1.4",		NULL },
		{ "balsa",			"-m" },
		{ "kmail",			NULL },
		{ "mozilla",			"-mail" }
	};
	unsigned i;

	for (i = 0 ; i < G_N_ELEMENTS (fallback_mailers); i++) {
		char const *app = fallback_mailers[i].app;
		if (g_find_program_in_path (app)) {
			char *argv[4];
			argv[0] = (char *)app;
			if (fallback_mailers [i].arg == NULL) {
				argv[1] = (char *)url;
				argv[2] = NULL;
			} else {
				argv[1] = (char *)fallback_mailers[i].arg;
				argv[2] = (char *)url;
				argv[3] = NULL;
			}
			g_spawn_async (working_dir,
				       argv, NULL, G_SPAWN_SEARCH_PATH,
				       NULL, NULL, NULL, err);
			return;
		}
	}

	if (err)
		*err = g_error_new (go_error_invalid (), 0,
				    "Missing handler for mailto URLs.");
}
#endif

static gboolean
cb_cleanup_sendto (gpointer path)
{
	char *dir = g_path_get_dirname (path);
	g_unlink (path); g_free (path);	/* the attachment */
	g_rmdir (dir); g_free (dir);	/* the tempdir */
	return FALSE;
}

gboolean
wb_view_sendto (WorkbookView *wbv, GOCmdContext *context)
{
	gboolean problem = FALSE;
	IOContext	*io_context;
	Workbook	*wb;
	GOFileSaver	*fs;

	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), FALSE);
	g_return_val_if_fail (IS_GO_CMD_CONTEXT (context), FALSE);

	wb = wb_view_get_workbook (wbv);
	g_object_ref (wb);
	fs = workbook_get_file_saver (wb);
	if (fs == NULL)
		fs = go_file_saver_get_default ();

	io_context = gnumeric_io_context_new (context);
	if (fs != NULL) {
		char *template, *full_name, *uri;
		char *basename = g_path_get_basename (go_doc_get_uri (GO_DOC (wb)));

#define GNM_SEND_DIR	".gnm-sendto-"
#ifdef HAVE_MKDTEMP
		template = g_build_filename (g_get_tmp_dir (),
			GNM_SEND_DIR "XXXXXX", NULL);
		problem = (mkdtemp (template) == NULL);
#else
		while (1) {
			char *dirname = g_strdup_printf
				("%s%ld-%08d",
				 GNM_SEND_DIR,
				 (long)getpid (),
				 (int)(1e8 * random_01 ()));
			template = g_build_filename (g_get_tmp_dir (), dirname, NULL);
			g_free (dirname);

			if (g_mkdir (template, 0700) == 0) {
				problem = FALSE;
				break;
			}

			if (errno != EEXIST) {
				go_cmd_context_error_export (GO_CMD_CONTEXT (io_context),
					_("Failed to create temporary file for sending."));
				gnumeric_io_error_display (io_context);
				problem = TRUE;
				break;
			}
		}
#endif

		if (problem) {
			g_free (template);
			goto out;
		}

		full_name = g_build_filename (template, basename, NULL);
		g_free (basename);
		uri = go_filename_to_uri (full_name);

		wbv_save_to_uri (wbv, fs, uri, io_context);

		if (gnumeric_io_error_occurred (io_context) ||
		    gnumeric_io_warning_occurred (io_context))
			gnumeric_io_error_display (io_context);

		if (gnumeric_io_error_occurred (io_context)) {
			problem = TRUE;
		} else {
/****************************************************************
 * This code does not belong here
 * move to goffice
 **/
			/* mutt does not handle urls with no destination
			 * so pick something to arbitrary */
			GError *err = NULL;
			char *url, *tmp = go_url_encode (full_name, 0);
			url = g_strdup_printf ("mailto:someone?attach=%s", tmp);
			g_free (tmp);
#ifdef GNM_WITH_GNOME
			go_url_show (url);
#else
			gnm_mailto_url_show (url, template, &err);
#endif
			if (err != NULL) {
				go_cmd_context_error (GO_CMD_CONTEXT (io_context), err);
				g_error_free (err);
				gnumeric_io_error_display (io_context);
				problem = TRUE;
			}
			g_free (url);
		}
		g_free (template);

		/*
		 * We wait a while before we clean up to ensure the file is
		 * loaded by the mailer.
		 */
		g_timeout_add (1000 * 10, cb_cleanup_sendto, full_name);
		g_free (uri);
	} else {
		go_cmd_context_error_export (GO_CMD_CONTEXT (io_context),
			_("Default file saver is not available."));
		gnumeric_io_error_display (io_context);
		problem = TRUE;
	}

 out:
	g_object_unref (G_OBJECT (io_context));
	g_object_unref (wb);

	return !problem;
}

WorkbookView *
wb_view_new_from_input  (GsfInput *input,
			 GOFileOpener const *optional_fmt,
			 IOContext *io_context,
			 char const *optional_enc)
{
	WorkbookView *new_wbv = NULL;

	g_return_val_if_fail (GSF_IS_INPUT(input), NULL);
	g_return_val_if_fail (optional_fmt == NULL ||
			      IS_GO_FILE_OPENER (optional_fmt), NULL);

	/* NOTE : we could support gzipped anything here if we wanted to
	 * by adding a wrapper, but there is no framework for remembering that
	 * the file was gzipped so lets not just yet.
	 */

	/* Search for an applicable opener */
	if (optional_fmt == NULL) {
		FileProbeLevel pl;
		GList *l;
		int input_refs = G_OBJECT (input)->ref_count;

		for (pl = FILE_PROBE_FILE_NAME; pl < FILE_PROBE_LAST && optional_fmt == NULL; pl++) {
			for (l = go_get_file_openers (); l != NULL; l = l->next) {
				GOFileOpener const *tmp_fo = GO_FILE_OPENER (l->data);
				int new_input_refs;
				/* A name match needs to be a content match too */
				if (go_file_opener_probe (tmp_fo, input, pl) &&
				    (pl == FILE_PROBE_CONTENT ||
				     !go_file_opener_can_probe	(tmp_fo, FILE_PROBE_CONTENT) ||
				     go_file_opener_probe (tmp_fo, input, FILE_PROBE_CONTENT)))
					optional_fmt = tmp_fo;

				new_input_refs = G_OBJECT (input)->ref_count;
				if (new_input_refs != input_refs) {
					g_warning ("Format %s's probe changed input ref_count from %d to %d.",
						   go_file_opener_get_id (tmp_fo),
						   input_refs,
						   new_input_refs);
					input_refs = new_input_refs;
				}

				if (optional_fmt)
					break;
			}
		}
	}

	if (optional_fmt != NULL) {
		char const *input_name;
		Workbook *new_wb;
		gboolean old;

		new_wbv = workbook_view_new (NULL);
		new_wb = wb_view_get_workbook (new_wbv);
		if (NULL != (input_name = gsf_input_name (input))) {
			char *uri = go_shell_arg_to_uri (input_name);
			go_doc_set_uri (GO_DOC (new_wb), uri);
			g_free (uri);
		}

		/* disable recursive dirtying while loading */
		old = workbook_enable_recursive_dirty (new_wb, FALSE);
		go_file_opener_open (optional_fmt, optional_enc, io_context, new_wbv, input);
		workbook_enable_recursive_dirty (new_wb, old);

		if (gnumeric_io_error_occurred (io_context)) {
			g_object_unref (G_OBJECT (new_wb));
			new_wbv = NULL;
		} else if (workbook_sheet_count (new_wb) == 0) {
			/* we didn't get a sheet nor an error, */
			/* the user must have cancelled        */
			g_object_unref (G_OBJECT (new_wb));
			new_wbv = NULL;
		} else {
			workbook_share_expressions (new_wb, TRUE);
			workbook_recalc (new_wb);
			go_doc_set_dirty (GO_DOC (new_wb), FALSE);
		}
	} else
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
			_("Unsupported file format."));

	return new_wbv;
}

/**
 * wb_view_new_from_uri :
 * @uri          : URI for file
 * @optional_fmt : Optional GOFileOpener
 * @io_context   : Optional context to display errors.
 * @optional_enc : Optional encoding for GOFileOpener that understand it
 *
 * Reads @uri file using given file opener @optional_fmt, or probes for a valid
 * possibility if @optional_fmt is NULL.  Reports problems to @io_context.
 *
 * Return value: TRUE if file was successfully read and FALSE otherwise.
 */
WorkbookView *
wb_view_new_from_uri (char const *uri,
		      GOFileOpener const *optional_fmt,
		      IOContext *io_context,
		      char const *optional_enc)
{
	char *msg = NULL;
	GError *err = NULL;
	GsfInput *input;

	g_return_val_if_fail (uri != NULL, NULL);

	input = go_file_open (uri, &err);
	if (input != NULL) {
		WorkbookView *res;

		g_printerr ("Reading %s\n", uri);
		res = wb_view_new_from_input (input,
					      optional_fmt, io_context,
					      optional_enc);
		g_object_unref (G_OBJECT (input));
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

	go_cmd_context_error_import (GO_CMD_CONTEXT (io_context), msg);
	g_free (msg);

	return NULL;
}
