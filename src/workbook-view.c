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
#include "gnumeric.h"
#include "workbook-view.h"

#include "workbook-control-priv.h"
#include "workbook.h"
#include "history.h"
#include "workbook-private.h"
#include "application.h"
#include "sheet.h"
#include "sheet-merge.h"
#include "sheet-style.h"
#include "str.h"
#include "format.h"
#include "expr.h"
#include "value.h"
#include "ranges.h"
#include "mstyle.h"
#include "position.h"
#include "cell.h"
#include "parse-util.h"
#include "io-context.h"

#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>
#include <gal/util/e-util.h>
#include <locale.h>

/* Persistent attribute ids, do not change them */
enum {
	ARG_VIEW_HSCROLLBAR = 1,
	ARG_VIEW_VSCROLLBAR,
	ARG_VIEW_TABS,
	ARG_VIEW_DO_AUTO_COMPLETION,
};

/* WorkbookView signals */
enum {
	SHEET_ENTERED,
	LAST_SIGNAL
};

static gint workbook_view_signals [LAST_SIGNAL] = {
	0, /* SHEET_ENTERED */
};

Workbook *
wb_view_workbook (WorkbookView *wbv)
{
	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), NULL);
	return wbv->wb;
}

Sheet *
wb_view_cur_sheet (WorkbookView *wbv)
{
	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), NULL);

	return wbv->current_sheet;
}

void
wb_view_sheet_focus (WorkbookView *wbv, Sheet *sheet)
{
	if (wbv->current_sheet != sheet) {
		Workbook *wb = wb_view_workbook (wbv);

		/* Make sure the sheet has been attached */
		g_return_if_fail (workbook_sheet_index_get (wb, sheet) >= 0);

		WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control,
			wb_control_sheet_focus (control, sheet););

		wbv->current_sheet = sheet;
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
	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	if (wbv->current_sheet == NULL) {
		wbv->current_sheet = new_sheet;
		wb_view_auto_expr_recalc (wbv, FALSE);
		wb_view_format_feedback (wbv, FALSE);
		wb_view_menus_update (wbv);
	}

	WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control,
		wb_control_sheet_add (control, new_sheet););
}

void
wb_view_set_attribute_list (WorkbookView *wbv, GList *list)
{
	GList *l;

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	for (l = list; l != NULL; l = l->next) {
		gtk_object_arg_set (GTK_OBJECT (wbv), l->data, NULL);
	}
}

GtkArg *
wb_view_get_attributev (WorkbookView *wbv, guint *n_args)
{
	GtkArg *args;
	guint num;

	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), NULL);

	args = gtk_object_query_args (WORKBOOK_VIEW_TYPE, NULL, &num);
	gtk_object_getv (GTK_OBJECT (wbv), num, args);

	*n_args = num;

	return args;
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
	Sheet *sheet;

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	sheet = wbv->current_sheet;
	if (sheet != NULL) {
		MStyle *mstyle = sheet_style_get (sheet,
			sheet->edit_pos.col,
			sheet->edit_pos.row);

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
			wb_control_menu_state_update (control, sheet, MS_ALL);
			wb_control_menu_state_sheet_prefs (control, sheet);
		});
	}
}

void
wb_view_selection_desc (WorkbookView *wbv, gboolean use_pos,
			WorkbookControl *optional_wbc)
{
	Sheet *sheet;

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	sheet = wbv->current_sheet;
	if (sheet != NULL) {
		char buffer [10 + 2 * 4 * sizeof (int)];
		char const *sel_descr = buffer;
		Range const *r, *m;
		
		g_return_if_fail (IS_SHEET (sheet));
		g_return_if_fail (sheet->selections);

		r = sheet->selections->data;

		if (use_pos || range_is_singleton (r) ||
		    (NULL != (m = sheet_merge_is_corner (sheet, &r->start)) &&
		     range_equal (r, m)))
			sel_descr = cell_pos_name (&r->start);
		else
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
	Sheet *sheet;

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	sheet = wbv->current_sheet;
	if (sheet != NULL) {
		Cell     *cell;
		char     *text;
		ExprArray const* ar;

		cell = sheet_cell_get (sheet,
				       sheet->edit_pos.col,
				       sheet->edit_pos.row);

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

		/* FIXME : This does not belong here.  */
		/* This is intended for screen reading software etc. */
		gtk_signal_emit_by_name (GTK_OBJECT (sheet->workbook), "cell_changed",
					 sheet, text,
					 sheet->edit_pos.col,
					 sheet->edit_pos.row);

		if (optional_wbc == NULL) {
			WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control,
				wb_control_edit_line_set (control, text););
		} else
			wb_control_edit_line_set (optional_wbc, text);

		g_free (text);
	}
}

void
wb_view_auto_expr (WorkbookView *wbv, char const *descr, char const *expression)
{
	char *old_num_locale, *old_monetary_locale, *old_msg_locale;
	ExprTree *new_auto_expr;
	ParsePos pp;

	old_num_locale = g_strdup (gnumeric_setlocale (LC_NUMERIC, NULL));
	gnumeric_setlocale (LC_NUMERIC, "C");
	old_monetary_locale = g_strdup (gnumeric_setlocale (LC_MONETARY, NULL));
	gnumeric_setlocale (LC_MONETARY, "C");
	/* FIXME : Get rid of this */
	old_msg_locale = g_strdup (textdomain (NULL));
	textdomain ("C");

	parse_pos_init (&pp, wb_view_workbook (wbv), NULL, 0, 0);
	new_auto_expr = expr_parse_str_simple (expression, &pp);

	g_return_if_fail (new_auto_expr != NULL);

	textdomain (old_msg_locale);
	g_free (old_msg_locale);
	gnumeric_setlocale (LC_MONETARY, old_monetary_locale);
	g_free (old_monetary_locale);
	gnumeric_setlocale (LC_NUMERIC, old_num_locale);
	g_free (old_num_locale);

	if (wbv->auto_expr_desc)
		g_free (wbv->auto_expr_desc);
	if (wbv->auto_expr)
		expr_tree_unref (wbv->auto_expr);

	wbv->auto_expr_desc = g_strdup (descr);
	wbv->auto_expr = new_auto_expr;

	if (wbv->current_sheet != NULL)
		wb_view_auto_expr_recalc (wbv, TRUE);
}

static void
wb_view_auto_expr_value_display (WorkbookView *wbv)
{
	WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control,
		wb_control_auto_expr_value (control););
}

void
wb_view_auto_expr_recalc (WorkbookView *wbv, gboolean display)
{
	static CellPos const cp = {0, 0};
	EvalPos	 ep;
	Value	*v;

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));
	g_return_if_fail (wbv->auto_expr != NULL);

	v = expr_eval (wbv->auto_expr,
		       eval_pos_init (&ep, wbv->current_sheet, &cp),
		       EVAL_STRICT);

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

static void
wb_view_set_arg (GtkObject *object, GtkArg *arg, guint arg_id)
{
	WorkbookView *wbv = WORKBOOK_VIEW (object);

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	switch (arg_id) {
	case ARG_VIEW_HSCROLLBAR:
		wbv->show_horizontal_scrollbar = GTK_VALUE_BOOL (*arg);
		break;
	case ARG_VIEW_VSCROLLBAR:
		wbv->show_vertical_scrollbar = GTK_VALUE_BOOL (*arg);
		break;
	case ARG_VIEW_TABS:
		wbv->show_notebook_tabs = GTK_VALUE_BOOL (*arg);
		break;
	case ARG_VIEW_DO_AUTO_COMPLETION:
		wbv->do_auto_completion = GTK_VALUE_BOOL (*arg);
		break;
	}
	wb_view_prefs_update (wbv);
}

static void
wb_view_get_arg (GtkObject *object, GtkArg *arg, guint arg_id)
{
	WorkbookView *wbv = WORKBOOK_VIEW (object);

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	switch (arg_id) {
	case ARG_VIEW_HSCROLLBAR:
		GTK_VALUE_BOOL (*arg) = wbv->show_horizontal_scrollbar;
		break;

	case ARG_VIEW_VSCROLLBAR:
		GTK_VALUE_BOOL (*arg) = wbv->show_vertical_scrollbar;
		break;

	case ARG_VIEW_TABS:
		GTK_VALUE_BOOL (*arg) = wbv->show_notebook_tabs;
		break;

	case ARG_VIEW_DO_AUTO_COMPLETION:
		GTK_VALUE_BOOL (*arg) = wbv->do_auto_completion;
		break;
	}
}

void
wb_view_attach_control (WorkbookView *wbv, WorkbookControl *wbc)
{
	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));
	g_return_if_fail (IS_WORKBOOK_CONTROL (wbc));
	g_return_if_fail (wbc->wb_view == NULL);

	wbc->wb_view = wbv;
	if (wbc->wb_view->wb_controls == NULL)
		wbc->wb_view->wb_controls = g_ptr_array_new ();
	g_ptr_array_add (wbc->wb_view->wb_controls, wbc);

	if (wbv->wb != NULL) {
		/* Set the title of the newly connected control */
		char const *base_name = g_basename (wbv->wb->filename);
		wb_control_title_set (wbc, base_name);
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

static GtkObjectClass *parent_class;
static void
wb_view_destroy (GtkObject *object)
{
	WorkbookView *wbv = WORKBOOK_VIEW (object);

	if (wbv->auto_expr) {
		expr_tree_unref (wbv->auto_expr);
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

	if (wbv->wb != NULL)
		workbook_detach_view (wbv);

	if (wbv->wb_controls != NULL) {
		WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control,
	        {
			wb_view_detach_control (control);
			gtk_object_unref (GTK_OBJECT (control));
		});
		if (wbv->wb_controls != NULL)
			g_warning ("Unexpected left over controls");
	}

	GTK_OBJECT_CLASS (parent_class)->destroy (object);
}

void
workbook_view_init (WorkbookView *wbv, Workbook *opt_wb)
{
	workbook_attach_view ((opt_wb != NULL) ? opt_wb : workbook_new (), wbv);

	wbv->show_horizontal_scrollbar = TRUE;
	wbv->show_vertical_scrollbar = TRUE;
	wbv->show_notebook_tabs = TRUE;
	wbv->do_auto_completion = application_use_auto_complete ();

	/* Set the default operation to be performed over selections */
	wbv->auto_expr      = NULL;
	wbv->auto_expr_desc = NULL;
	wbv->auto_expr_value_as_string = NULL;
	wb_view_auto_expr (wbv, _("Sum"), "sum(selection(0))");

	wbv->current_format = NULL;

	/* Guess at the current sheet */
	wbv->current_sheet = NULL;
	if (opt_wb != NULL) {
		GList *sheets = workbook_sheets (opt_wb);
		if (sheets != NULL) {
			wb_view_sheet_focus (wbv, sheets->data);
			g_list_free (sheets);
		}
	}
}

static void
workbook_view_class_init (GtkObjectClass *object_class)
{
	WorkbookViewClass *wbc_class = WORKBOOK_VIEW_CLASS (object_class);

	g_return_if_fail (wbc_class != NULL);

	parent_class = gtk_type_class (gtk_object_get_type ());

	object_class->set_arg = wb_view_set_arg;
	object_class->get_arg = wb_view_get_arg;
	object_class->destroy = wb_view_destroy;
	gtk_object_add_arg_type ("WorkbookView::show_horizontal_scrollbar",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE,
				 ARG_VIEW_HSCROLLBAR);
	gtk_object_add_arg_type ("WorkbookView::show_vertical_scrollbar",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE,
				 ARG_VIEW_VSCROLLBAR);
	gtk_object_add_arg_type ("WorkbookView::show_notebook_tabs",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE,
				 ARG_VIEW_TABS);
	gtk_object_add_arg_type ("WorkbookView::do_auto_completion",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE,
				 ARG_VIEW_DO_AUTO_COMPLETION);

	workbook_view_signals [SHEET_ENTERED] =
		gtk_signal_new (
			"sheet_entered",
			GTK_RUN_LAST,
			object_class->type,
			GTK_SIGNAL_OFFSET (WorkbookViewClass,
					   sheet_entered),
			gtk_marshal_NONE__POINTER,
			GTK_TYPE_NONE,
			1,
			GTK_TYPE_POINTER);
}

E_MAKE_TYPE (workbook_view, "WorkbookView", WorkbookView,
	     workbook_view_class_init, NULL, GTK_TYPE_OBJECT);

WorkbookView *
workbook_view_new (Workbook *optional_wb)
{
	WorkbookView *view;

	view = gtk_type_new (workbook_view_get_type ());
	workbook_view_init (view, optional_wb);
	return view;
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
	gboolean success = FALSE;
	IOContext *io_context;
	Workbook *wb;

	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), FALSE);
	g_return_val_if_fail (IS_GNUM_FILE_SAVER (fs), FALSE);
	g_return_val_if_fail (file_name != NULL, FALSE);
	g_return_val_if_fail (IS_COMMAND_CONTEXT (context), FALSE);

	wb = wb_view_workbook (wbv);
	io_context = gnumeric_io_context_new (context);
	gnum_file_saver_save (fs, io_context, wbv, file_name);
	if (!gnumeric_io_error_occurred (io_context)) {
		workbook_set_saveinfo (wb, file_name, gnum_file_saver_get_format_level (fs), fs);
		workbook_set_dirty (wb, FALSE);
		success = TRUE;
	} else {
		gnumeric_io_error_display (io_context);
		success = FALSE;
	}
	gtk_object_unref (GTK_OBJECT (io_context));

	return success;
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
 * NOTE : @display does NOT need to be from the 
 *
 * Return value: TRUE if file was successfully saved and FALSE otherwise.
 */
gboolean
wb_view_save (WorkbookView *wbv, CommandContext *context)
{
	gboolean success = FALSE;
	IOContext *io_context;
	Workbook *wb;
	GnumFileSaver *fs;

	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), FALSE);
	g_return_val_if_fail (IS_COMMAND_CONTEXT (context), FALSE);

	wb = wb_view_workbook (wbv);
	fs = workbook_get_file_saver (wb);
	if (fs == NULL)
		fs = get_default_file_saver ();

	io_context = gnumeric_io_context_new (context);
	if (fs == NULL) {
		gnumeric_io_error_save (io_context,
			_("Default file saver is not available."));
	} else
		gnum_file_saver_save (fs, io_context, wbv, wb->filename);

	success = !gnumeric_io_error_occurred (io_context);
	if (success)
		workbook_set_dirty (wb, FALSE);
	else 
		gnumeric_io_error_display (io_context);
	gtk_object_unref (GTK_OBJECT (io_context));

	return success;
}

/**
 * wb_view_open:
 * @wbv         : Workbook View
 * @wbc         : Workbook Control
 * @file_name   : File name
 * @display_err : should errors messages be generated
 *
 * Reads @file_name file, automatically detecting file type and using
 * appropriate file opener.
 *
 * Return value: TRUE if file was successfully read and FALSE otherwise.
 */
gboolean
wb_view_open (WorkbookView *wbv, WorkbookControl *wbc,
              gchar const *file_name, gboolean display_errors)
{
	return wb_view_open_custom (wbv, wbc, NULL, file_name, display_errors);
}

/**
 * wb_view_open_custom:
 * @wbv         : Workbook View
 * @wbc         : Workbook Control
 * @fo          : GnumFileOpener object
 * @file_name   : File name
 *
 * Reads @file_name file using given file opener (@fo).
 *
 * Return value: TRUE if file was successfully read and FALSE otherwise.
 */
gboolean
wb_view_open_custom (WorkbookView *wbv, WorkbookControl *wbc,
                     GnumFileOpener const *fo, gchar const *file_name,
		     gboolean display_errors)
{
	Workbook *new_wb = NULL;
	WorkbookView *new_wbv = NULL;

	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), FALSE);
	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), FALSE);
	g_return_val_if_fail (fo == NULL || IS_GNUM_FILE_OPENER (fo), FALSE);
	g_return_val_if_fail (file_name != NULL, FALSE);

	if (g_file_test (file_name, G_FILE_TEST_ISFILE)) {
		IOContext *io_context = gnumeric_io_context_new (COMMAND_CONTEXT (wbc));
		wb_control_menu_state_sensitivity (wbc, FALSE);

		/* Search for an applicable opener */
		if (fo == NULL) {
			FileProbeLevel pl;
			GList *l;

			for (pl = FILE_PROBE_FILE_NAME; pl < FILE_PROBE_LAST && fo == NULL; pl++) {
				for (l = get_file_openers (); l != NULL; l = l->next) {
					GnumFileOpener const *tmp_fo = GNUM_FILE_OPENER (l->data);
					if (gnum_file_opener_probe (tmp_fo, file_name, pl)) {
						fo = tmp_fo;
						break;
					}
				}
			}
		}

		if (fo != NULL) {
			gboolean old;

			new_wbv = workbook_view_new (NULL);
			new_wb = wb_view_workbook (new_wbv);

			/* disable recursive dirtying while loading */
			old = workbook_enable_recursive_dirty (new_wb, FALSE);
			gnum_file_opener_open (fo, io_context, new_wbv, file_name);
			workbook_enable_recursive_dirty (new_wb, old);

			if (!gnumeric_io_error_occurred (io_context) &&
			    workbook_sheet_count (new_wb) == 0)
				gnumeric_io_error_read (io_context, _("No sheets in workbook."));

			if (!gnumeric_io_error_occurred (io_context)) {
				workbook_set_dirty (new_wb, FALSE);
			} else {
				gtk_object_destroy (GTK_OBJECT (new_wb));
				new_wb = NULL;
				new_wbv = NULL;
			}
		} else
			gnumeric_io_error_read (io_context, _("Unsupported file format."));

		if (gnumeric_io_error_occurred (io_context)) {
			gnumeric_io_error_display (io_context);
		}

		wb_control_menu_state_sensitivity (wbc, TRUE);
		gtk_object_unref (GTK_OBJECT (io_context));
	} else {
		new_wb = workbook_new_with_sheets (1);
		new_wbv = workbook_view_new (new_wb);
		workbook_set_saveinfo (new_wb, file_name, FILE_FL_NEW, NULL);
	}

	if (new_wbv != NULL) {
		Workbook *old_wb;

		g_return_val_if_fail (new_wb != NULL, FALSE);

		old_wb = wb_control_workbook (wbc);
		if (workbook_is_pristine (old_wb)) {
			gtk_object_ref (GTK_OBJECT (wbc));
			workbook_unref (old_wb);
			workbook_control_set_view (wbc, new_wbv, NULL);
			workbook_control_init_state (wbc);
		} else
			(void) wb_control_wrapper_new (wbc, new_wbv, NULL);

		workbook_recalc (new_wb);

		g_return_val_if_fail (!workbook_is_dirty (new_wb), FALSE);

		sheet_update (wb_view_cur_sheet (new_wbv));
		return TRUE;
	} else {
		g_return_val_if_fail (new_wb == NULL, FALSE);
		return FALSE;
	}
}
