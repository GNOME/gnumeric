/* vim: set sw=8: */
/*
 * workbook-view.c: View functions for the workbook
 *
 * Copyright (C) 2000 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "workbook-view.h"

#include "workbook-control-priv.h"
#include "workbook.h"
#include "history.h"
#include "application.h"
#include "sheet.h"
#include "sheet-merge.h"
#include "sheet-style.h"
#include "str.h"
#include "format.h"
#include "func.h"
#include "expr.h"
#include "expr-name.h"
#include "expr-impl.h"
#include "value.h"
#include "ranges.h"
#include "selection.h"
#include "mstyle.h"
#include "position.h"
#include "cell.h"
#include "gutils.h"
#include "io-context.h"

#include <gsf/gsf-input-memory.h>
#include <gsf/gsf-input-stdio.h>
#include <gsf/gsf-utils.h>

#include <gsf/gsf-impl-utils.h>
#include <locale.h>
#include <string.h>

/* WorkbookView signals */
enum {
	LAST_SIGNAL
};

Workbook *
wb_view_workbook (WorkbookView const *wbv)
{
	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), NULL);
	return wbv->wb;
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

		WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control,
			wb_control_sheet_focus (control, sheet););

		wbv->current_sheet = sheet;
		wbv->current_sheet_view = sheet_get_view (sheet, wbv);
		wb_view_selection_desc (wbv, TRUE, NULL);
		wb_view_edit_line_set (wbv, NULL);
		wb_view_format_feedback (wbv, TRUE);
		wb_view_menus_update (wbv);
		wb_view_auto_expr_recalc (wbv, TRUE);
	}
}

void
wb_view_sheet_add (WorkbookView *wbv, Sheet *new_sheet)
{
	SheetView *new_view;

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	/* create the new view before potentially looking for it
	 * if this is the 1st sheet
	 */
	new_view = sheet_view_new (new_sheet, wbv);
	if (wbv->current_sheet == NULL) {
		wbv->current_sheet = new_sheet;
		wbv->current_sheet_view = sheet_get_view (new_sheet, wbv);
		wb_view_format_feedback (wbv, FALSE);
		wb_view_menus_update (wbv);
		wb_view_auto_expr_recalc (wbv, FALSE);
	}

	WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control,
		wb_control_sheet_add (control, new_view););
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
wb_view_prefs_update (WorkbookView *view)
{
	WORKBOOK_VIEW_FOREACH_CONTROL(view, control,
		wb_control_prefs_update	(control););
}

void
wb_view_format_feedback (WorkbookView *wbv, gboolean display)
{
	SheetView *sv;

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	sv = wbv->current_sheet_view;
	if (sv != NULL) {
		MStyle *mstyle = sheet_style_get (sv->sheet,
			sv->edit_pos.col,
			sv->edit_pos.row);

		mstyle_ref (mstyle);
		if (wbv->current_format != NULL) {
			mstyle_unref (wbv->current_format);
			/* Cheap anti-flicker.
			 * Compare pointers (content may be invalid)
			 * No need for an expensive compare.
			 */
			if (mstyle == wbv->current_format)
				return;
		}
		wbv->current_format = mstyle;
		if (display) {
			WORKBOOK_VIEW_FOREACH_CONTROL(wbv, control,
				wb_control_format_feedback (control););
		}
	}
}

void
wb_view_menus_update (WorkbookView *wbv)
{
	Sheet *sheet;

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	sheet = wbv->current_sheet;
	if (sheet != NULL) {
		WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control, {
			wb_control_menu_state_update (control, MS_ALL);
			wb_control_menu_state_sheet_prefs (control, sheet);
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
		Range const *r, *m;

		g_return_if_fail (IS_SHEET_VIEW (sv));
		g_return_if_fail (sv->selections);

		r = sv->selections->data;

		if (use_pos || range_is_singleton (r) ||
		    (NULL != (m = sheet_merge_is_corner (sv->sheet, &r->start)) &&
		     range_equal (r, m))) {
			sel_descr = sheet_names_check (sv->sheet, r);
			if (sel_descr == NULL)
				sel_descr = cellpos_as_string (&r->start);
		} else
			snprintf (buffer, sizeof (buffer), _("%dR x %dC"),
				  r->end.row - r->start.row + 1,
				  r->end.col - r->start.col + 1);

		if (optional_wbc == NULL) {
		WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control,
			wb_control_selection_descr_set (control, sel_descr););
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
		Cell     *cell;
		char     *text;
		GnmExprArray const* ar;

		cell = sheet_cell_get (sv->sheet,
				       sv->edit_pos.col,
				       sv->edit_pos.row);

		if (cell) {
			text = cell_get_entered_text (cell);
			/* If this is part of an array we add '{' '}' and size
			 * information to the display.  That is not actually
			 * part of the parsable expression, but it is a useful
			 * extension to the simple '{' '}' that MS excel(tm)
			 * uses.
			 */
			if (NULL != (ar = cell_is_array(cell))) {
				/* No need to worry about locale for the comma
				 * this syntax is not parsed
				 */
				char *tmp = g_strdup_printf (
					"{%s}(%d,%d)[%d][%d]", text,
					ar->rows, ar->cols, ar->y, ar->x);
				g_free (text);
				text = tmp;
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

void
wb_view_auto_expr (WorkbookView *wbv, char const *descr, char const *func_name)
{
	if (wbv->auto_expr_desc)
		g_free (wbv->auto_expr_desc);
	if (wbv->auto_expr)
		gnm_expr_unref (wbv->auto_expr);

	wbv->auto_expr_desc = g_strdup (descr);
	wbv->auto_expr = gnm_expr_new_funcall (
		gnm_func_lookup (func_name, NULL), NULL);

	if (wbv->current_sheet != NULL)
		wb_view_auto_expr_recalc (wbv, TRUE);
}

static void
wb_view_auto_expr_value_display (WorkbookView *wbv)
{
	WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control,
		wb_control_auto_expr_value (control););
}

static void
accumulate_regions (SheetView *sv,  Range const *r, gpointer closure)
{
	GnmExprList	**selection = closure;
	CellRef a, b;

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
wb_view_auto_expr_recalc (WorkbookView *wbv, gboolean display)
{
	FunctionEvalInfo ei;
	EvalPos		 ep;
	GnmExprList	*selection = NULL;
	Value		*v;
	SheetView	*sv;

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));
	g_return_if_fail (wbv->auto_expr != NULL);

	sv = wb_view_cur_sheet_view (wbv);
	if (sv == NULL)
		return;

	selection_apply (sv, &accumulate_regions, FALSE, &selection);

	ei.pos = eval_pos_init_sheet (&ep, wbv->current_sheet);
	ei.func_call = (GnmExprFunction const *)wbv->auto_expr;

	v = function_call_with_list (&ei, selection, 0);
	gnm_expr_list_unref (selection);

	if (wbv->auto_expr_value_as_string)
		g_free (wbv->auto_expr_value_as_string);

	if (v) {
		char const *val_str = value_peek_string (v);
		wbv->auto_expr_value_as_string =
			g_strconcat (wbv->auto_expr_desc, "=", val_str, NULL);
		value_release (v);
	} else
		wbv->auto_expr_value_as_string = g_strdup (_("Internal ERROR"));

	wb_view_auto_expr_value_display (wbv);
}

void
wb_view_attach_control (WorkbookView *wbv, WorkbookControl *wbc)
{
	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));
	g_return_if_fail (IS_WORKBOOK_CONTROL (wbc));
	g_return_if_fail (wbc->wb_view == NULL);

	if (wbv->wb_controls == NULL)
		wbv->wb_controls = g_ptr_array_new ();
	g_ptr_array_add (wbv->wb_controls, wbc);
	wbc->wb_view = wbv;

	if (wbv->wb != NULL) {
		/* Set the title of the newly connected control */
		char *base_name = g_path_get_basename (workbook_get_filename (wbv->wb));
		wb_control_title_set (wbc, base_name);
		g_free (base_name);
	}
}

void
wb_view_detach_control (WorkbookControl *wbc)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL (wbc));
	g_return_if_fail (IS_WORKBOOK_VIEW (wbc->wb_view));

	g_ptr_array_remove (wbc->wb_view->wb_controls, wbc);
	if (wbc->wb_view->wb_controls->len == 0) {
		g_ptr_array_free (wbc->wb_view->wb_controls, TRUE);
		wbc->wb_view->wb_controls = NULL;
	}
	wbc->wb_view = NULL;
}

static GObjectClass *parent_class;
static void
wb_view_finalize (GObject *object)
{
	WorkbookView *wbv = WORKBOOK_VIEW (object);

	if (wbv->wb_controls != NULL) {
		WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control, {
			wb_control_sheet_remove_all (control);
			wb_view_detach_control (control);
			g_object_unref (G_OBJECT (control));
		});
		if (wbv->wb_controls != NULL)
			g_warning ("Unexpected left over controls");
	}

	if (wbv->wb != NULL)
		workbook_detach_view (wbv);

	if (wbv->auto_expr) {
		gnm_expr_unref (wbv->auto_expr);
		wbv->auto_expr = NULL;
	}
	if (wbv->auto_expr_desc) {
		g_free (wbv->auto_expr_desc);
		wbv->auto_expr_desc = NULL;
	}
	if (wbv->auto_expr_value_as_string) {
		g_free (wbv->auto_expr_value_as_string);
		wbv->auto_expr_value_as_string = NULL;
	}
	if (wbv->current_format != NULL) {
		mstyle_unref (wbv->current_format);
		wbv->current_format = NULL;
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
workbook_view_class_init (GObjectClass *klass)
{
	WorkbookViewClass *wbc_class = WORKBOOK_VIEW_CLASS (klass);

	g_return_if_fail (wbc_class != NULL);

	parent_class = g_type_class_peek (G_TYPE_OBJECT);

	klass->finalize = wb_view_finalize;
}

GSF_CLASS (WorkbookView, workbook_view,
	   workbook_view_class_init, NULL, G_TYPE_OBJECT);

WorkbookView *
workbook_view_new (Workbook *wb)
{
	WorkbookView *wbv = g_object_new (WORKBOOK_VIEW_TYPE, NULL);
	char *base_name;
	int i;

	if (wb == NULL)
		wb = workbook_new ();

	g_return_val_if_fail (wb != NULL, NULL);

	workbook_attach_view (wb, wbv);

	wbv->show_horizontal_scrollbar = TRUE;
	wbv->show_vertical_scrollbar = TRUE;
	wbv->show_notebook_tabs = TRUE;
	wbv->do_auto_completion = application_use_auto_complete ();
	wbv->is_protected = FALSE;

	/* Set the default operation to be performed over selections */
	wbv->auto_expr      = NULL;
	wbv->auto_expr_desc = NULL;
	wbv->auto_expr_value_as_string = NULL;
	wb_view_auto_expr (wbv, _("Sum"), "sum");

	wbv->current_format = NULL;

	/* Guess at the current sheet */
	wbv->current_sheet = NULL;
	wbv->current_sheet_view = NULL;

	for (i = 0 ; i < workbook_sheet_count (wb); i++)
		wb_view_sheet_add (wbv, workbook_sheet_by_index (wb, i));

	/* Set the titles of the newly connected view's controls */
	base_name = g_path_get_basename (workbook_get_filename (wbv->wb));
	WORKBOOK_VIEW_FOREACH_CONTROL (wbv, wbc,
		wb_control_title_set (wbc, base_name););
	g_free (base_name);

	return wbv;
}

/**
 * wb_view_save_as:
 * @wbv         : Workbook View
 * @fs          : GnumFileSaver object
 * @file_name   : File name
 * @context     :
 *
 * Saves @wbv and workbook it's attached to into @file_name file using
 * @fs file saver.
 *
 * Return value: TRUE if file was successfully saved and FALSE otherwise.
 */
gboolean
wb_view_save_as (WorkbookView *wbv, GnumFileSaver *fs, gchar const *file_name,
		 CommandContext *context)
{
	IOContext *io_context;
	Workbook  *wb;
	gboolean error;

	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), FALSE);
	g_return_val_if_fail (IS_GNUM_FILE_SAVER (fs), FALSE);
	g_return_val_if_fail (file_name != NULL, FALSE);
	g_return_val_if_fail (IS_COMMAND_CONTEXT (context), FALSE);

	wb = wb_view_workbook (wbv);
	io_context = gnumeric_io_context_new (context);

	cmd_context_set_sensitive (context, FALSE);
	gnum_file_saver_save (fs, io_context, wbv, file_name);
	cmd_context_set_sensitive (context, TRUE);

	error = gnumeric_io_error_occurred (io_context);
	if (!error) {
		workbook_set_saveinfo (wb, file_name, gnum_file_saver_get_format_level (fs), fs);
		workbook_set_dirty (wb, FALSE);
	} else
		gnumeric_io_error_display (io_context);
	g_object_unref (G_OBJECT (io_context));

	return !error;
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
wb_view_save (WorkbookView *wbv, CommandContext *context)
{
	IOContext	*io_context;
	Workbook	*wb;
	GnumFileSaver	*fs;
	gboolean error;

	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), FALSE);
	g_return_val_if_fail (IS_COMMAND_CONTEXT (context), FALSE);

	wb = wb_view_workbook (wbv);
	fs = workbook_get_file_saver (wb);
	if (fs == NULL)
		fs = get_default_file_saver ();

	io_context = gnumeric_io_context_new (context);
	if (fs == NULL)
		gnumeric_error_save (COMMAND_CONTEXT (io_context),
			_("Default file saver is not available."));
	else
		gnum_file_saver_save (fs, io_context, wbv, workbook_get_filename (wb));

	error = gnumeric_io_error_occurred (io_context);
	if (error)
		gnumeric_io_error_display (io_context);
	else
		workbook_set_dirty (wb, FALSE);

	g_object_unref (G_OBJECT (io_context));

	return !error;
}

WorkbookView *
wb_view_new_from_input  (GsfInput *input,
			 GnumFileOpener const *optional_fmt,
			 IOContext *io_context)
{
	WorkbookView *new_wbv = NULL;

	g_return_val_if_fail (GSF_IS_INPUT(input), NULL);
	g_return_val_if_fail (optional_fmt == NULL ||
			      IS_GNUM_FILE_OPENER (optional_fmt), NULL);

	/* NOTE : we could support gzipped anything here if we wanted to
	 * by adding a wrapper, but there is no framework for remembering that
	 * the file was gzipped so lets not just yet.
	 */

	/* Search for an applicable opener */
	if (optional_fmt == NULL) {
		FileProbeLevel pl;
		GList *l;

		for (pl = FILE_PROBE_FILE_NAME; pl < FILE_PROBE_LAST && optional_fmt == NULL; pl++) {
			for (l = get_file_openers (); l != NULL; l = l->next) {
				GnumFileOpener const *tmp_fo = GNUM_FILE_OPENER (l->data);
				if (gnum_file_opener_probe (tmp_fo, input, pl)) {
					optional_fmt = tmp_fo;
					break;
				}
			}
		}
	}

	if (optional_fmt != NULL) {
		Workbook *new_wb;
		gboolean old;

		new_wbv = workbook_view_new (NULL);
		new_wb = wb_view_workbook (new_wbv);

		/* disable recursive dirtying while loading */
		old = workbook_enable_recursive_dirty (new_wb, FALSE);
		gnum_file_opener_open (optional_fmt, io_context, new_wbv, input);
		workbook_enable_recursive_dirty (new_wb, old);

		if (gnumeric_io_error_occurred (io_context)) {
			g_object_unref (G_OBJECT (new_wb));
			new_wbv = NULL;
		} else
			workbook_set_dirty (new_wb, FALSE);
	} else
		gnumeric_error_read (COMMAND_CONTEXT (io_context),
			_("Unsupported file format."));

	return new_wbv;
}

/**
 * wb_view_new_from_file :
 * @file_name    : File name
 * @optional_fmt : Optional GnumFileOpener
 * @io_context   : Optional context to display errors.
 *
 * Reads @file_name file using given file opener @optional_fmt, or probes for a valid
 * possibility if @optional_fmt is NULL.  Reports problems to @io_context.
 *
 * Return value: TRUE if file was successfully read and FALSE otherwise.
 */
WorkbookView *
wb_view_new_from_file (char const *file_name,
		       GnumFileOpener const *optional_fmt,
		       IOContext *io_context)
{
	char *msg = NULL;

	if (gnumeric_valid_filename (file_name)) {
		GError *err = NULL;
		GsfInput *input;

		/* Only report error if stdio fails too */
		input = gsf_input_mmap_new (file_name, NULL);
		if (input == NULL)
			input = gsf_input_stdio_new (file_name, &err);

		puts (file_name);
		if (input != NULL) {
			WorkbookView *res = wb_view_new_from_input (input,
								    optional_fmt, io_context);
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
					       file_name);
	} else {
		/*
		 * This should be quite rare.  To provoke, use
		 * gnumeric `echo -e '\377\376'`
		 */
		msg = g_strdup (_("The filename given is not valid."));
	}
	gnumeric_error_read (COMMAND_CONTEXT (io_context), msg);
	g_free (msg);

	return NULL;
}
