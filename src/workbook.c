/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * workbook.c: workbook model and manipulation utilities
 *
 * Authors:
 *    Miguel de Icaza (miguel@gnu.org).
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1998, 1999, 2000 Miguel de Icaza
 * (C) 2000-2001 Ximian, Inc.
 * (C) 2002 Jody Goldberg
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "workbook-priv.h"

#include "workbook-view.h"
#include "workbook-control.h"
#include "command-context.h"
#include "application.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-control.h"
#include "cell.h"
#include "expr.h"
#include "expr-name.h"
#include "dependent.h"
#include "value.h"
#include "ranges.h"
#include "history.h"
#include "commands.h"
#include "libgnumeric.h"
#include "file.h"
#include "io-context.h"
#include "gutils.h"
#include "gnm-marshalers.h"
#include "style-color.h"
#include <goffice/utils/go-file.h>

#ifdef WITH_GTK
#ifdef WITH_GNOME
#include <bonobo/bonobo-main.h>
#else
#include <gtk/gtkmain.h> /* for gtk_main_quit */
#endif /* WITH_GTK */
#endif
#include <gsf/gsf-impl-utils.h>
#include <string.h>

static GObjectClass *workbook_parent_class;

/* Signals */
enum {
	SUMMARY_CHANGED,
	FILENAME_CHANGED,
	SHEET_ORDER_CHANGED,
	SHEET_ADDED,
	SHEET_DELETED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

static void
cb_saver_finalize (Workbook *wb, GnmFileSaver *saver)
{
	g_return_if_fail (IS_GNM_FILE_SAVER (saver));
	g_return_if_fail (IS_WORKBOOK (wb));
	g_return_if_fail (wb->file_saver == saver);
	wb->file_saver = NULL;
}

static void
workbook_finalize (GObject *wb_object)
{
	Workbook *wb = WORKBOOK (wb_object);
	GList *sheets, *ptr;

	wb->during_destruction = TRUE;

	if (wb->file_saver != NULL) {
		g_object_weak_unref (G_OBJECT (wb->file_saver),
			(GWeakNotify) cb_saver_finalize, wb);
		wb->file_saver = NULL;
	}

	/* Remove all the sheet controls to avoid displaying while we exit */
	WORKBOOK_FOREACH_CONTROL (wb, view, control,
		wb_control_sheet_remove_all (control););

	summary_info_free (wb->summary_info);
	wb->summary_info = NULL;

	command_list_release (wb->undo_commands);
	command_list_release (wb->redo_commands);
	wb->undo_commands = NULL;
	wb->redo_commands = NULL;

	workbook_deps_destroy (wb);

	/* Copy the set of sheets, the list changes under us. */
	sheets = workbook_sheets (wb);

	/* Remove all contents while all sheets still exist */
	for (ptr = sheets; ptr != NULL ; ptr = ptr->next) {
		Sheet *sheet = ptr->data;

		sheet_destroy_contents (sheet);
		/*
		 * We need to put this test BEFORE we detach
		 * the sheet from the workbook.  It is ugly, but should
		 * be ok for debug code.
		 */
		if (gnumeric_debugging > 0)
			gnm_dep_container_dump (sheet->deps);
	}

#ifdef BIT_ROT
	if (wb->dependents != NULL) {
		/* Nobody expects the Spanish Inquisition!  */
		g_warning ("Trouble at the Mill.  Please report.");
	}
#endif

	/* Now remove the sheets themselves */
	for (ptr = sheets; ptr != NULL ; ptr = ptr->next) {
		Sheet *sheet = ptr->data;

		workbook_sheet_detach (wb, sheet, FALSE);
	}
	g_list_free (sheets);

	/* TODO : This should be earlier when we figure out how to deal with
	 * the issue raised by 'pristine'.
	 */
	/* Get rid of all the views */
	if (wb->wb_views != NULL) {
		WORKBOOK_FOREACH_VIEW (wb, view, {
			workbook_detach_view (view);
			g_object_unref (G_OBJECT (view));
		});
		if (wb->wb_views != NULL)
			g_warning ("Unexpected left over views");
	}

	if (wb->sheet_local_functions != NULL) {
		g_hash_table_destroy (wb->sheet_local_functions);
		wb->sheet_local_functions = NULL;
	}

	/* Remove ourselves from the list of workbooks.  */
	gnm_app_workbook_list_remove (wb);

	/* Now do deletions that will put this workbook into a weird
	   state.  Careful here.  */
	g_hash_table_destroy (wb->sheet_hash_private);
	wb->sheet_hash_private = NULL;

	g_ptr_array_free (wb->sheets, TRUE);
	wb->sheets = NULL;

	if (wb->uri) {
		if (wb->file_format_level >= FILE_FL_MANUAL_REMEMBER)
			gnm_app_history_add (wb->uri);
	       g_free (wb->uri);
	       wb->uri = NULL;
	}
	g_free (wb->basename);
	wb->basename = NULL;

	/* this has no business being here */
#ifdef WITH_GTK
	if (initial_workbook_open_complete && gnm_app_workbook_list () == NULL)
#ifdef WITH_GNOME
		bonobo_main_quit ();
#else
		gtk_main_quit ();
#endif
#endif
	G_OBJECT_CLASS (workbook_parent_class)->finalize (wb_object);
}

static void
cb_sheet_mark_dirty (gpointer key, gpointer value, gpointer user_data)
{
	Sheet *sheet = value;
	int dirty = GPOINTER_TO_INT (user_data);

	sheet_set_dirty (sheet, dirty);
}

void
workbook_set_dirty (Workbook *wb, gboolean is_dirty)
{
	gboolean changed;

	g_return_if_fail (wb != NULL);

	changed = (!workbook_is_dirty (wb) != !is_dirty);
	wb->modified = is_dirty;
	if (wb->summary_info != NULL)
		wb->summary_info->modified = is_dirty;
	g_hash_table_foreach (wb->sheet_hash_private,
		cb_sheet_mark_dirty, GINT_TO_POINTER (is_dirty));
	if (changed) {
		WORKBOOK_FOREACH_CONTROL (wb, view, control,
			wb_control_update_title (control););
	}
}

static void
cb_sheet_check_dirty (gpointer key, gpointer value, gpointer user_data)
{
	Sheet    *sheet = value;
	gboolean *dirty = user_data;

	if (*dirty)
		return;

	if (!sheet->modified)
		return;

	*dirty = TRUE;
}

gboolean
workbook_is_dirty (Workbook const *wb)
{
	gboolean dirty = FALSE;

	g_return_val_if_fail (wb != NULL, FALSE);
	if (wb->summary_info != NULL && wb->summary_info->modified)
		return TRUE;

	g_hash_table_foreach (wb->sheet_hash_private,
		cb_sheet_check_dirty, &dirty);

	return dirty;
}

/**
 * workbook_set_placeholder :
 * @wb :
 * @is_placeholder :
 **/
void
workbook_set_placeholder (Workbook *wb, gboolean is_placeholder)
{
	g_return_if_fail (IS_WORKBOOK (wb));
	wb->is_placeholder = is_placeholder;
}

/**
 * workbook_is_placeholder :
 * @wb :
 *
 * Returns TRUE if @wb has no views and was created as a placeholder for data
 * in an external reference.
 **/
gboolean
workbook_is_placeholder	(Workbook const *wb)
{
	g_return_val_if_fail (IS_WORKBOOK (wb), FALSE);
	return wb->is_placeholder;
}

static void
cb_sheet_check_pristine (gpointer key, gpointer value, gpointer user_data)
{
	Sheet    *sheet = value;
	gboolean *pristine = user_data;

	if (!sheet_is_pristine (sheet))
		*pristine = FALSE;
}

/**
 * workbook_is_pristine:
 * @wb:
 *
 *   This checks to see if the workbook has ever been
 * used ( approximately )
 *
 * Return value: TRUE if we can discard this workbook.
 **/
gboolean
workbook_is_pristine (Workbook const *wb)
{
	gboolean pristine = TRUE;

	g_return_val_if_fail (wb != NULL, FALSE);

	if (workbook_is_dirty (wb))
		return FALSE;

	if (wb->names ||
	    (wb->file_format_level > FILE_FL_NEW))
		return FALSE;

	/* Check if we seem to contain anything */
	g_hash_table_foreach (wb->sheet_hash_private,
		cb_sheet_check_pristine, &pristine);

	return pristine;
}

static void
workbook_init (GObject *object)
{
	Workbook *wb = WORKBOOK (object);

	wb->is_placeholder = FALSE;
	wb->modified	   = FALSE;

	wb->wb_views = NULL;
	wb->sheets = g_ptr_array_new ();
	wb->sheet_hash_private = g_hash_table_new (g_str_hash, g_str_equal);
	wb->sheet_order_dependents = NULL;
	wb->sheet_local_functions = NULL;
	wb->names        = NULL;
	wb->summary_info = summary_info_new ();
	summary_info_default (wb->summary_info);
	wb->summary_info->modified = FALSE;
	wb->uri = wb->basename = NULL;

	/* Nothing to undo or redo */
	wb->undo_commands = wb->redo_commands = NULL;

	/* default to no iteration */
	wb->iteration.enabled = TRUE;
	wb->iteration.max_number = 100;
	wb->iteration.tolerance = .001;
	wb->recalc_auto = TRUE;

	wb->date_conv.use_1904 = FALSE;

	wb->file_format_level = FILE_FL_NEW;
	wb->file_saver        = NULL;

	wb->during_destruction = FALSE;
	wb->being_reordered    = FALSE;
	wb->recursive_dirty_enabled = TRUE;

	gnm_app_workbook_list_add (wb);
}

static void
workbook_class_init (GObjectClass *object_class)
{
	workbook_parent_class = g_type_class_peek_parent (object_class);

	object_class->finalize = workbook_finalize;

	signals [SUMMARY_CHANGED] = g_signal_new ("summary_changed",
		WORKBOOK_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WorkbookClass, summary_changed),
		(GSignalAccumulator) NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,
		0, G_TYPE_NONE);

	signals [FILENAME_CHANGED] = g_signal_new ("filename_changed",
		WORKBOOK_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WorkbookClass, filename_changed),
		(GSignalAccumulator) NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,
		0, G_TYPE_NONE);

	signals [SHEET_ORDER_CHANGED] = g_signal_new ("sheet_order_changed",
		WORKBOOK_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WorkbookClass, sheet_order_changed),
		(GSignalAccumulator) NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,
		0, G_TYPE_NONE);

	signals [SHEET_ADDED] = g_signal_new ("sheet_added",
		WORKBOOK_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WorkbookClass, sheet_added),
		(GSignalAccumulator) NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,
		0, G_TYPE_NONE);

	signals [SHEET_DELETED] = g_signal_new ("sheet_deleted",
		WORKBOOK_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WorkbookClass, sheet_deleted),
		(GSignalAccumulator) NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,
		0, G_TYPE_NONE);
}

/**
 * workbook_new:
 *
 * Creates a new empty Workbook
 * and assigns a unique name.
 */
Workbook *
workbook_new (void)
{
	static int count = 0;
	gboolean is_unique;
	Workbook  *wb;
	GnmFileSaver *def_save = gnm_file_saver_get_default ();
	char const *extension = NULL;

	if (def_save != NULL)
		extension = gnm_file_saver_get_extension (def_save);
	if (extension == NULL)
		extension = "gnumeric";

	wb = g_object_new (WORKBOOK_TYPE, NULL);

	/* Assign a default name */
	do {
		char *name, *nameutf8, *uri;

		count++;
		nameutf8 = g_strdup_printf (_("Book%d.%s"), count, extension);
		name = g_filename_from_utf8 (nameutf8, -1, NULL, NULL, NULL);
		if (!name) {
			name = g_strdup_printf ("Book%d.%s", count, extension);
		}
		uri = go_filename_to_uri (name);

		is_unique = workbook_set_uri (wb, uri);

		g_free (uri);
		g_free (name);
		g_free (nameutf8);
	} while (!is_unique);
	return wb;
}

/**
 * workbook_sheet_name_strip_number:
 * @name: name to strip number from
 * @number: returns the number stripped off in *number
 *
 * Gets a name in the form of "Sheet (10)", "Stuff" or "Dummy ((((,"
 * and returns the real name of the sheet "Sheet","Stuff","Dummy ((((,"
 * without the copy number.
 **/
static void
workbook_sheet_name_strip_number (char *name, int* number)
{
	char *end;
	int p10 = 1;
	int n = 0;
	*number = 1;

	g_return_if_fail (*name != 0);

	end = name + strlen (name) - 1;
	if (*end != ')')
		return;

	while (end > name) {
		int dig;
		end = g_utf8_prev_char (end);

		if (*end == '(') {
			*number = n;
			*end = 0;
			return;
		}

		dig = g_unichar_digit_value (g_utf8_get_char (end));
		if (dig == -1)
			return;

		/* FIXME: check for overflow.  */
		n += p10 * dig;
		p10 *= 10;
	}
}

/**
 * workbook_new_with_sheets:
 * @sheet_count: initial number of sheets to create.
 *
 * Returns a Workbook with @sheet_count allocated
 * sheets on it
 */
Workbook *
workbook_new_with_sheets (int sheet_count)
{
	Workbook *wb = workbook_new ();
	while (sheet_count-- > 0)
		workbook_sheet_add (wb, NULL, FALSE);
	return wb;
}

/**
 * workbook_set_uri:
 * @wb: the workbook to modify
 * @uri: the uri for this worksheet.
 *
 * Sets the internal filename to @name and changes
 * the title bar for the toplevel window to be the name
 * of this file.
 *
 * Returns : TRUE if the name was set succesfully.
 *
 * FIXME : Add a check to ensure the name is unique.
 */
gboolean
workbook_set_uri (Workbook *wb, char const *uri)
{
	g_return_val_if_fail (wb != NULL, FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);

	if (uri == wb->uri)
		return TRUE;

	g_free (wb->uri);
	wb->uri = g_strdup (uri);
	g_free (wb->basename);
	wb->basename = go_basename_from_uri (uri);
	WORKBOOK_FOREACH_CONTROL (wb, view, control,
		wb_control_update_title (control););

	g_signal_emit (G_OBJECT (wb), signals [FILENAME_CHANGED], 0);
	_gnm_app_flag_windows_changed ();
	return TRUE;
}

const gchar *
workbook_get_uri (Workbook const *wb)
{
	g_return_val_if_fail (IS_WORKBOOK (wb), NULL);
	return wb->uri;
}
const gchar *
workbook_get_basename (Workbook const *wb)
{
	g_return_val_if_fail (IS_WORKBOOK (wb), NULL);
	return wb->basename;
}

void
workbook_add_summary_info (Workbook *wb, SummaryItem *sit)
{
	if (summary_info_add (wb->summary_info, sit))
		g_signal_emit (G_OBJECT (wb), signals [SUMMARY_CHANGED], 0);
}

SummaryInfo *
workbook_metadata (Workbook const *wb)
{
	g_return_val_if_fail (IS_WORKBOOK (wb), NULL);
	return wb->summary_info;
}

/**
 * workbook_set_saveinfo:
 * @wb: the workbook to modify
 * @name: the file name for this worksheet.
 * @level: the file format level
 *
 * If level is sufficiently advanced assign the info.
 *
 * Returns : TRUE if save info was set succesfully.
 *
 * FIXME : Add a check to ensure the name is unique.
 */
gboolean
workbook_set_saveinfo (Workbook *wb, FileFormatLevel level, GnmFileSaver *fs)
{
	g_return_val_if_fail (wb != NULL, FALSE);
	g_return_val_if_fail (level > FILE_FL_NONE && level <= FILE_FL_AUTO,
			      FALSE);

	if (level <= FILE_FL_WRITE_ONLY)
		return FALSE;

	wb->file_format_level = level;
	if (wb->file_saver != NULL)
		g_object_weak_unref (G_OBJECT (wb->file_saver),
			(GWeakNotify) cb_saver_finalize, wb);

	wb->file_saver = fs;
	if (fs != NULL)
		g_object_weak_ref (G_OBJECT (fs),
			(GWeakNotify) cb_saver_finalize, wb);

	return TRUE;
}

GnmFileSaver *
workbook_get_file_saver (Workbook *wb)
{
	g_return_val_if_fail (IS_WORKBOOK (wb), NULL);

	return wb->file_saver;
}

void
workbook_unref (Workbook *wb)
{
	g_object_unref (G_OBJECT (wb));
}

/**
 * workbook_foreach_cell_in_range :
 *
 * @pos : The position the range is relative to.
 * @cell_range : A value containing a range;
 * @only_existing : if TRUE only existing cells are sent to the handler.
 * @handler : The operator to apply to each cell.
 * @closure : User data.
 *
 * The supplied value must be a cellrange.
 * The range bounds are calculated relative to the eval position
 * and normalized.
 * For each existing cell in the range specified, invoke the
 * callback routine.  If the only_existing flag is TRUE, then
 * callbacks are only invoked for existing cells.
 *
 * Return value:
 *    non-NULL on error, or VALUE_TERMINATE if some invoked routine requested
 *    to stop (by returning non-NULL).
 */
GnmValue *
workbook_foreach_cell_in_range (GnmEvalPos const *pos,
				GnmValue const	*cell_range,
				CellIterFlags	 flags,
				CellIterFunc	 handler,
				gpointer	 closure)
{
	GnmRange  r;
	Sheet *start_sheet, *end_sheet;

	g_return_val_if_fail (pos != NULL, NULL);
	g_return_val_if_fail (cell_range != NULL, NULL);
	g_return_val_if_fail (cell_range->type == VALUE_CELLRANGE, NULL);

	rangeref_normalize (&cell_range->v_range.cell, pos,
			    &start_sheet, &end_sheet, &r);

	if (start_sheet != end_sheet) {
		GnmValue *res;
		Workbook const *wb = start_sheet->workbook;
		int i = start_sheet->index_in_wb;
		int stop = end_sheet->index_in_wb;
		if (i > stop) { int tmp = i; i = stop ; stop = tmp; }

		g_return_val_if_fail (end_sheet->workbook == wb, VALUE_TERMINATE);

		for (; i <= stop ; i++) {
			res = sheet_foreach_cell_in_range (
				g_ptr_array_index (wb->sheets, i), flags,
				r.start.col, r.start.row, r.end.col, r.end.row,
				handler, closure);
			if (res != NULL)
				return res;
		}
		return NULL;
	}

	return sheet_foreach_cell_in_range (start_sheet, flags,
		r.start.col, r.start.row, r.end.col, r.end.row,
		handler, closure);
}

/**
 * workbook_cells:
 *
 * @wb : The workbook to find cells in.
 * @comments: If true, include cells with only comments also.
 *
 * Collects a GPtrArray of GnmEvalPos pointers for all cells in a workbook.
 * No particular order should be assumed.
 */
GPtrArray *
workbook_cells (Workbook *wb, gboolean comments)
{
	GList *tmp, *sheets;
	GPtrArray *cells = g_ptr_array_new ();

	g_return_val_if_fail (wb != NULL, cells);

	sheets = workbook_sheets (wb);
	for (tmp = sheets; tmp; tmp = tmp->next) {
		Sheet *sheet = tmp->data;
		int oldlen = cells->len;
		GPtrArray *scells =
			sheet_cells (sheet,
				     0, 0, SHEET_MAX_COLS, SHEET_MAX_ROWS,
				     comments);

		g_ptr_array_set_size (cells, oldlen + scells->len);
		memcpy (&g_ptr_array_index (cells, oldlen),
			&g_ptr_array_index (scells, 0),
			scells->len * sizeof (GnmEvalPos *));

		g_ptr_array_free (scells, TRUE);
	}
	g_list_free (sheets);

	return cells;
}

GSList *
workbook_local_functions (Workbook const *wb)
{
	return NULL;
}

gboolean
workbook_enable_recursive_dirty (Workbook *wb, gboolean enable)
{
	gboolean old;

	g_return_val_if_fail (IS_WORKBOOK (wb), FALSE);

	old = wb->recursive_dirty_enabled;
	wb->recursive_dirty_enabled = enable;
	return old;
}

void
workbook_autorecalc_enable (Workbook *wb, gboolean enable)
{
	g_return_if_fail (IS_WORKBOOK (wb));
	wb->recalc_auto = enable;
}

gboolean
workbook_autorecalc (Workbook *wb)
{
	g_return_val_if_fail (IS_WORKBOOK (wb), FALSE);
	return wb->recalc_auto;
}

void
workbook_iteration_enabled (Workbook *wb, gboolean enable)
{
	g_return_if_fail (IS_WORKBOOK (wb));
	wb->iteration.enabled = enable;
}

void
workbook_iteration_max_number (Workbook *wb, int max_number)
{
	g_return_if_fail (IS_WORKBOOK (wb));
	g_return_if_fail (max_number >= 0);
	wb->iteration.max_number = max_number;
}

void
workbook_iteration_tolerance (Workbook *wb, double tolerance)
{
	g_return_if_fail (IS_WORKBOOK (wb));
	g_return_if_fail (tolerance >= 0);

	wb->iteration.tolerance = tolerance;
}

void
workbook_attach_view (Workbook *wb, WorkbookView *wbv)
{
	g_return_if_fail (IS_WORKBOOK (wb));
	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));
	g_return_if_fail (wb_view_workbook (wbv) == NULL);

	if (wb->wb_views == NULL)
		wb->wb_views = g_ptr_array_new ();
	g_ptr_array_add (wb->wb_views, wbv);
	wbv->wb = wb;
}

void
workbook_detach_view (WorkbookView *wbv)
{
	SheetView *sv;

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));
	g_return_if_fail (IS_WORKBOOK (wbv->wb));

	WORKBOOK_FOREACH_SHEET (wbv->wb, sheet, {
		sv = sheet_get_view (sheet, wbv);
		sheet_detach_view (sv);
		g_object_unref (G_OBJECT (sv));
	});

	g_ptr_array_remove (wbv->wb->wb_views, wbv);
	if (wbv->wb->wb_views->len == 0) {
		g_ptr_array_free (wbv->wb->wb_views, TRUE);
		wbv->wb->wb_views = NULL;
	}
	wbv->wb = NULL;
}

/*****************************************************************************/

/**
 * workbook_sheets : Get an ordered list of the sheets in the workbook
 *                   The caller is required to free the list.
 */
GList *
workbook_sheets (Workbook const *wb)
{
	GList *list = NULL;

	g_return_val_if_fail (IS_WORKBOOK (wb), NULL);

	if (wb->sheets) {
		int i = wb->sheets->len;
		while (i-- > 0)
			list = g_list_prepend (list,
				g_ptr_array_index (wb->sheets, i));
	}

	return list;
}

int
workbook_sheet_count (Workbook const *wb)
{
	g_return_val_if_fail (IS_WORKBOOK (wb), 0);

	return wb->sheets ? wb->sheets->len : 0;
}

static void
cb_dep_unlink (GnmDependent *dep, gpointer value, gpointer user_data)
{
	GnmCellPos *pos = NULL;
	if (dependent_is_cell (dep))
		pos = &DEP_TO_CELL (dep)->pos;
	dependent_unlink (dep, pos);
}

static void
pre_sheet_index_change (Workbook *wb)
{
	g_return_if_fail (!wb->being_reordered);

	wb->being_reordered = TRUE;

	if (wb->sheet_order_dependents != NULL)
		g_hash_table_foreach (wb->sheet_order_dependents,
			(GHFunc) cb_dep_unlink, NULL);
}

static void
cb_dep_link (GnmDependent *dep, gpointer value, gpointer user_data)
{
	GnmCellPos *pos = NULL;
	if (dependent_is_cell (dep))
		pos = &DEP_TO_CELL (dep)->pos;
	dependent_link (dep, pos);
}

static void
post_sheet_index_change (Workbook *wb)
{
	g_return_if_fail (wb->being_reordered);

	if (wb->sheet_order_dependents != NULL)
		g_hash_table_foreach (wb->sheet_order_dependents,
			(GHFunc) cb_dep_link, NULL);

	wb->being_reordered = FALSE;

	if (wb->during_destruction)
		return;

	g_signal_emit (G_OBJECT (wb), signals [SHEET_ORDER_CHANGED], 0);
}

static void
workbook_sheet_index_update (Workbook *wb, int start)
{
	int i;

	for (i = wb->sheets->len ; i-- > start ; ) {
		Sheet *sheet = g_ptr_array_index (wb->sheets, i);
		sheet->index_in_wb = i;
	}
}

Sheet *
workbook_sheet_by_index (Workbook const *wb, int i)
{
	g_return_val_if_fail (IS_WORKBOOK (wb), NULL);
	g_return_val_if_fail ((int)wb->sheets->len > i, NULL);

	/* i = -1 is special, return NULL */

	if (i == -1)
		return NULL;

	return g_ptr_array_index (wb->sheets, i);
}

/**
 * workbook_sheet_by_name:
 * @wb: workbook to lookup the sheet on
 * @name: the sheet name we are looking for.
 *
 * Returns a pointer to a Sheet or NULL if the sheet
 * was not found.
 */
Sheet *
workbook_sheet_by_name (Workbook const *wb, char const *name)
{
	Sheet *sheet;
	char *tmp;

	g_return_val_if_fail (IS_WORKBOOK (wb), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	tmp = g_utf8_casefold (name, -1);
	sheet = g_hash_table_lookup (wb->sheet_hash_private, tmp);
	g_free (tmp);

	return sheet;
}

/**
 * workbook_sheet_attach :
 * @wb :
 * @new_sheet :
 * @insert_after : optional position.
 *
 * Add @new_sheet to @wb, either placing it after @insert_after, or appending.
 */
void
workbook_sheet_attach (Workbook *wb, Sheet *new_sheet,
		       Sheet const *insert_after)
{
	g_return_if_fail (IS_WORKBOOK (wb));
	g_return_if_fail (IS_SHEET (new_sheet));
	g_return_if_fail (new_sheet->workbook == wb);

	pre_sheet_index_change (wb);
	if (insert_after != NULL) {
		int pos = insert_after->index_in_wb;
		gnm_ptr_array_insert (wb->sheets, (gpointer)new_sheet, ++pos);
		workbook_sheet_index_update (wb, pos);
	} else {
		g_ptr_array_add (wb->sheets, new_sheet);
		workbook_sheet_index_update (wb, workbook_sheet_count (wb) - 1);
	}

	g_hash_table_insert (wb->sheet_hash_private,
		new_sheet->name_case_insensitive, new_sheet);
	post_sheet_index_change (wb);

	WORKBOOK_FOREACH_VIEW (wb, view,
		wb_view_sheet_add (view, new_sheet););
}

static void
cb_tweak_3d (GnmDependent *dep, gpointer value, GnmExprRewriteInfo *rwinfo)
{
	GnmExpr const *newtree = gnm_expr_rewrite (dep->expression, rwinfo);
	if (newtree != NULL) {
		dependent_set_expr (dep, newtree);
		gnm_expr_unref (newtree);
	}
}

/**
 * workbook_sheet_hide_controls :
 * @wb : #Workbook
 * @sheet : #Sheet
 *
 * Remove the visible SheetControls of a sheet and shut them down politely.
 *
 * Returns TRUE if there are any remaining sheets visible
 **/
gboolean
workbook_sheet_hide_controls (Workbook *wb, Sheet *sheet)
{
	Sheet *focus = NULL;
	int sheet_index;

	g_return_val_if_fail (IS_WORKBOOK (wb), TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (sheet->workbook == wb, TRUE);
	g_return_val_if_fail (workbook_sheet_by_name (wb, sheet->name_unquoted) == sheet, TRUE);

	/* Finish any object editing */
	SHEET_FOREACH_CONTROL (sheet, view, control,
		sc_mode_edit (control););

	sheet_index = sheet->index_in_wb;

	/* If not exiting, adjust the focus for any views whose focus sheet
	 * was the one being deleted, and prepare to recalc */
	if (!wb->during_destruction) {
		if (sheet_index > 0)
			focus = g_ptr_array_index (wb->sheets, sheet_index-1);
		else if ((sheet_index+1) < (int)wb->sheets->len)
			focus = g_ptr_array_index (wb->sheets, sheet_index+1);

		if (focus != NULL)
			WORKBOOK_FOREACH_VIEW (wb, view, {
				if (view->current_sheet == sheet)
					wb_view_sheet_focus (view, focus);
			});
	}

	/* Remove all controls */
	WORKBOOK_FOREACH_CONTROL (wb, view, control,
		wb_control_sheet_remove (control, sheet););

	return focus != NULL;
}

void
workbook_sheet_unhide_controls (Workbook *wb, Sheet *sheet)
{
	g_return_if_fail (IS_WORKBOOK (wb));
	g_return_if_fail (IS_SHEET (sheet));

	WORKBOOK_FOREACH_VIEW (wb, wbv,
		wb_view_sheet_add (wbv, sheet););
}


/**
 * workbook_sheet_detach:
 * @wb: workbook.
 * @sheet: the sheet that we want to detach from the workbook
 * @recalc : force a recalc afterward
 *
 * Detaches @sheet from the workbook @wb.
 */
void
workbook_sheet_detach (Workbook *wb, Sheet *sheet, gboolean recalc)
{
	int sheet_index;
	gboolean still_visible_sheets;

	g_return_if_fail (IS_WORKBOOK (wb));
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->workbook == wb);
	g_return_if_fail (workbook_sheet_by_name (wb, sheet->name_unquoted) == sheet);

	sheet_index = sheet->index_in_wb;
	still_visible_sheets = workbook_sheet_hide_controls (wb, sheet);
	pre_sheet_index_change (wb);
	/* If we are not destroying things, Check for 3d refs that start or end
	 * on this sheet */
	if (wb->sheet_order_dependents != NULL) {
		GnmExprRewriteInfo rwinfo;

		rwinfo.type = GNM_EXPR_REWRITE_SHEET;
		rwinfo.u.sheet = sheet;
		g_hash_table_foreach (wb->sheet_order_dependents,
			(GHFunc) cb_tweak_3d, &rwinfo);
	}

	/* Remove our reference to this sheet */
	g_ptr_array_remove_index (wb->sheets, sheet_index);
	workbook_sheet_index_update (wb, sheet_index);
	sheet->index_in_wb = -1;
	g_hash_table_remove (wb->sheet_hash_private, sheet->name_case_insensitive);
	sheet_destroy (sheet);
	post_sheet_index_change (wb);

	if (recalc && still_visible_sheets)
		workbook_recalc_all (wb);
}

/**
 * workbook_sheet_add :
 * @wb :
 * @insert_after : optional position.
 *
 * Create and name a new sheet, either placing it after @insert_after, or
 * appending.
 */
Sheet *
workbook_sheet_add (Workbook *wb, Sheet const *insert_after, gboolean make_dirty)
{
	char *name = workbook_sheet_get_free_name (wb, _("Sheet"), TRUE, FALSE);
	Sheet *new_sheet = sheet_new (wb, name);

	g_free (name);
	workbook_sheet_attach (wb, new_sheet, insert_after);
	if (make_dirty)
		sheet_set_dirty (new_sheet, TRUE);

	g_signal_emit (G_OBJECT (wb), signals [SHEET_ADDED], 0);

	return new_sheet;
}

/**
 * Unlike workbook_sheet_detach, this function not only detaches the given
 * sheet from its parent workbook, But also invalidates all references to the
 * deleted sheet from other sheets and clears all references In the clipboard
 * to this sheet.  Finally, it also detaches the sheet from the workbook.
 */
void
workbook_sheet_delete (Sheet *sheet)
{
        Workbook *wb;

        g_return_if_fail (IS_SHEET (sheet));
        g_return_if_fail (IS_WORKBOOK (sheet->workbook));

	wb = sheet->workbook;

	if (!sheet->pristine) {
		Sheet *new_focus = NULL;
		int i;

		/*
		 * FIXME : Deleting a sheet plays havoc with our data structures.
		 * Be safe for now and empty the undo/redo queues
		 */
		command_list_release (wb->undo_commands);
		command_list_release (wb->redo_commands);
		wb->undo_commands = NULL;
		wb->redo_commands = NULL;
		WORKBOOK_FOREACH_CONTROL (wb, view, control,
			wb_control_undo_redo_truncate (control, 0, TRUE);
			wb_control_undo_redo_truncate (control, 0, FALSE);
			wb_control_undo_redo_labels (control, NULL, NULL);
		);

		i = sheet->index_in_wb - 1;
		if (i < 0)
			i = sheet->index_in_wb + 1;
		if (i < workbook_sheet_count (wb))
			new_focus = workbook_sheet_by_index (wb, i);

		WORKBOOK_FOREACH_VIEW (wb, wbv,
			if (sheet == wb_view_cur_sheet (wbv))
				wb_view_sheet_focus (wbv, new_focus);
		);
		WORKBOOK_FOREACH_CONTROL (wb, view, control,
			wb_control_undo_redo_truncate (control, 0, TRUE);
			wb_control_undo_redo_truncate (control, 0, FALSE);
			wb_control_undo_redo_labels (control, NULL, NULL);
		);
	}

	/* Important to do these BEFORE detaching the sheet */
	sheet_deps_destroy (sheet);

	/* All is fine, remove the sheet */
	workbook_sheet_detach (wb, sheet, TRUE);

	g_signal_emit (G_OBJECT (wb), signals [SHEET_DELETED], 0);
}

/**
 * Moves the sheet up or down @direction spots in the sheet list
 * If @direction is positive, move left. If positive, move right.
 */
gboolean
workbook_sheet_move (Sheet *sheet, int direction)
{
	Workbook *wb;
	gint old_pos, new_pos;

	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	wb = sheet->workbook;
        old_pos = sheet->index_in_wb;
	new_pos = old_pos + direction;

	if (0 <= new_pos && new_pos < workbook_sheet_count (wb)) {
		int min_pos = MIN (old_pos, new_pos);
		int max_pos = MAX (old_pos, new_pos);

		g_ptr_array_remove_index (wb->sheets, old_pos);
		gnm_ptr_array_insert (wb->sheets, sheet, new_pos);

		for (; max_pos >= min_pos ; max_pos--) {
			Sheet *sheet = g_ptr_array_index (wb->sheets, max_pos);
			sheet->index_in_wb = max_pos;
		}

		WORKBOOK_FOREACH_CONTROL (wb, view, control,
			wb_control_sheet_move (control, sheet, new_pos););
		sheet_set_dirty (sheet, TRUE);
		return TRUE;
	}
	return FALSE;
}

/**
 * workbook_sheet_get_free_name:
 * @wb:   workbook to look for
 * @base: base for the name, e. g. "Sheet"
 * @always_suffix: if true, add suffix even if the name "base" is not in use.
 * @handle_counter : strip counter if necessary
 *
 * Gets a new unquoted name for a sheets such that it does not exist on the
 * workbook.
 *
 * Returns the name assigned to the sheet.
 **/
char *
workbook_sheet_get_free_name (Workbook *wb,
			      const char *base,
			      gboolean always_suffix,
			      gboolean handle_counter)
{
	const char *name_format;
	char *name, *base_name;
	int i = 0;
	int limit;

	g_return_val_if_fail (wb != NULL, NULL);

	if (!always_suffix && (workbook_sheet_by_name (wb, base) == NULL))
		return g_strdup (base); /* Name not in use */

	base_name = g_strdup (base);
	if (handle_counter) {
		workbook_sheet_name_strip_number (base_name, &i);
		name_format = "%s(%d)";
	} else
		name_format = "%s%d";

	limit = i + workbook_sheet_count (wb) + 2;
	name = g_malloc (strlen (base_name) + strlen (name_format) + 10);
	for ( ; ++i < limit ; ){
		sprintf (name, name_format, base_name, i);
		if (workbook_sheet_by_name (wb, name) == NULL) {
			g_free (base_name);
			return name;
		}
	}

	/* We should not get here.  */
	g_warning ("There is trouble at the mill.");

	g_free (name);
	g_free (base_name);
	name = g_strdup_printf ("%s (%i)", base, 2);
	return name;
}

/**
 * workbook_sheet_rename_check:
 * @wb:          workbook to look for
 * @sheet_indices:   list of sheet indices (ignore -1)
 * @new_names:   list of new names
 *
 * Check whether changing of the names of the sheets as indicated
 * would be possible.
 *
 * Returns TRUE when it is possible.
 **/
gboolean
workbook_sheet_rename_check (Workbook *wb,
			     GSList *sheet_indices,
			     GSList *new_names,
			     GSList *sheet_indices_deleted,
			     GnmCmdContext *cc)
{
	GSList *sheet_index = sheet_indices;
	GSList *new_name = new_names;
	gint max_sheet = workbook_sheet_count (wb);

	/* First we check whether the sheet_indices are valid */
	while (sheet_index) {
		gint n = GPOINTER_TO_INT (sheet_index->data);
		if (n < -1 || n >= max_sheet) {
			g_warning ("Invalid sheet index %i", n);
			return FALSE;
		}
		sheet_index = sheet_index->next;
	}


	/* Then we just check whether the names are valid.*/
	sheet_index = sheet_indices;
	while (new_name && sheet_index) {
		if (new_name != NULL) {
			char *the_new_name = new_name->data;
			Sheet *tmp;

			if (the_new_name == NULL && 
			    GPOINTER_TO_INT (sheet_index->data) != -1) {
				gnm_cmd_context_error_invalid 
					(cc, 
					 _("Sheet name is NULL"),
					 the_new_name);
				return FALSE;
			}

			if (the_new_name != NULL) {
				
				/* Is the sheet name valid utf-8 ?*/
				if (!g_utf8_validate (the_new_name, -1, NULL)) {
					gnm_cmd_context_error_invalid 
						(cc, 
						 _("Sheet name is not valid utf-8"),
						 the_new_name);
					return FALSE;
				}
				
				/* Is the sheet name to short ?*/
				if (1 > g_utf8_strlen (the_new_name, -1)) {
					gnm_cmd_context_error_invalid 
						(cc,
						 _("Sheet name must have at "
						   "least 1 letter"),
						 the_new_name);
					return FALSE;
				}
				
				/* Is the sheet name already in use ?*/
				tmp = workbook_sheet_by_name (wb, the_new_name);
				
				if (tmp != NULL) {
					/* Perhaps it is a sheet also to be renamed */
					GSList *tmp_sheets = g_slist_find 
						(sheet_indices, 
						 GINT_TO_POINTER(tmp->index_in_wb));
					if (NULL == tmp_sheets) {
					/* Perhaps it is a sheet to be deleted */					
						tmp_sheets = g_slist_find 
						    (sheet_indices_deleted, 
						     GINT_TO_POINTER(tmp->index_in_wb));
					}

					if (NULL == tmp_sheets) {
						gnm_cmd_context_error_invalid 
							(cc,
							 _("There is already a "
							   "sheet named"),
							 the_new_name);
						return FALSE;
					}
				}
				
				/* Will we try to use the same name a second time ?*/
				if (new_name->next != NULL &&
				    g_slist_find_custom (new_name->next, 
							 the_new_name, 
							 gnm_str_compare) != NULL) {
					gnm_cmd_context_error_invalid 
						(cc,
						 _("You may not use this name twice"),
						 the_new_name);
					return FALSE;
				}
			}
		}
		new_name = new_name->next;
		sheet_index = sheet_index->next;
	}
	
	return TRUE;
}

/**
 * workbook_sheet_rename:
 * @wb:          workbook to look for
 * @sheet_indices:   list of sheet indices (ignore -1)
 * @new_names:   list of new names
 *
 * Adjusts the names of the sheets. We assume that everything is
 * valid. If in doubt call workbook_sheet_reorder_check first.
 *
 * Returns FALSE when it was successful
 **/
gboolean
workbook_sheet_rename (Workbook *wb,
		       GSList *sheet_indices,
		       GSList *new_names,
		       GnmCmdContext *cc)
{
	GSList *sheet_index = sheet_indices;
	GSList *new_name = new_names;

	while (new_name && sheet_index) {
		if (-1 != GPOINTER_TO_INT (sheet_index->data)) {
			g_hash_table_remove (wb->sheet_hash_private, 
					     new_name->data);
		}
		sheet_index = sheet_index->next;
		new_name = new_name->next;
	}

	sheet_index = sheet_indices;
	new_name = new_names;
	while (new_name && sheet_index) {
		if (-1 != GPOINTER_TO_INT (sheet_index->data)) {
			Sheet *sheet = workbook_sheet_by_index 
				(wb, GPOINTER_TO_INT (sheet_index->data));
			sheet_rename (sheet, new_name->data);
			g_hash_table_insert (wb->sheet_hash_private,
					     sheet->name_case_insensitive, 
					     sheet);
			sheet_set_dirty (sheet, TRUE);

			WORKBOOK_FOREACH_CONTROL (wb, view, control,
						  wb_control_sheet_rename (control, sheet););
		}
		sheet_index = sheet_index->next;
		new_name = new_name->next;
	}

	return FALSE;
}

/**
 * workbook_sheet_change_protection:
 * @wb:          workbook to look for
 * @sheets   :   list of sheet indices (ignore -1)
 * @locks    :   list of new locks
 *
 * Adjusts the locks
 *
 * Returns FALSE when it was successful
 **/
gboolean    
workbook_sheet_change_protection  (Workbook *wb,
				   GSList *sheets,
				   GSList *locks)
{
	g_return_val_if_fail (g_slist_length (sheets) 
			      == g_slist_length (locks), 
			      TRUE);

	while (sheets) {
		Sheet *sheet = workbook_sheet_by_index 
			(wb, GPOINTER_TO_INT (sheets->data));
		if (sheet != NULL)
			sheet->is_protected = GPOINTER_TO_INT (locks->data);
		sheets = sheets->next;
		locks = locks->next;
	}
	return FALSE;
	
}

/**
 * workbook_sheet_change_visibility:
 * @wb:          workbook to look for
 * @sheets   :   list of sheet indices (ignore -1)
 * @visibility:  list of new visibility
 *
 * Adjusts the visibility
 *
 * Returns FALSE when it was successful
 **/
gboolean    
workbook_sheet_change_visibility  (Workbook *wb,
				   GSList *sheets,
				   GSList *visibility)
{
	g_return_val_if_fail (g_slist_length (sheets) 
			      == g_slist_length (visibility), 
			      TRUE);

	while (sheets) {
		Sheet *sheet = workbook_sheet_by_index 
			(wb, GPOINTER_TO_INT (sheets->data));
		if (sheet != NULL)
			sheet_set_visibility (sheet, 
					      GPOINTER_TO_INT (visibility->data));
		sheets = sheets->next;
		visibility = visibility->next;
	}
	return FALSE;
	
}

/**
 * workbook_find_command :
 * @wb : #Workbook
 * @is_undo : undo vs redo
 * @key : command
 *
 * returns the 1 based index of the @key command, or 0 if it is not found
 **/
unsigned
workbook_find_command (Workbook *wb, gboolean is_undo, gpointer cmd)
{
	GSList *ptr;
	unsigned n = 1;

	g_return_val_if_fail (IS_WORKBOOK (wb), 0);

	ptr = is_undo ? wb->undo_commands : wb->redo_commands;
	for ( ; ptr != NULL ; ptr = ptr->next, n++)
		if (ptr->data == cmd)
			return n;
	g_warning ("%s command : %p not found", is_undo ? "undo" : "redo", cmd);
	return 0;
}

/**
 * workbook_sheet_recolor:
 * @wb:          workbook to look for
 * @sheets   :   list of sheet indices (ignore -1)
 * @fore     :   list of new text colors
 * @back     :   list of new background colors
 *
 * Adjusts the foreground colors
 *
 * Returns FALSE when it was successful
 **/
gboolean    
workbook_sheet_recolor  (Workbook *wb,
			 GSList *sheets,
			 GSList *fore, 
			 GSList *back)
{
	g_return_val_if_fail (g_slist_length (sheets) 
			      == g_slist_length (fore), 
			      TRUE);
	g_return_val_if_fail (g_slist_length (sheets) 
			      == g_slist_length (back), 
			      TRUE);

	while (sheets) {
		Sheet *sheet = workbook_sheet_by_index 
			(wb, GPOINTER_TO_INT (sheets->data));
		if (sheet != NULL) {
			GdkColor *fc = (GdkColor *) fore->data;
			GdkColor *bc = (GdkColor *) back->data;
			GnmColor *fore_color = fc ?
				style_color_new (fc->red, fc->green,
						 fc->blue) : NULL;
			GnmColor *back_color = bc ?
				style_color_new (bc->red, bc->green,
						 bc->blue) : NULL;
			sheet_set_tab_color (sheet, back_color, fore_color);
		}
		sheets = sheets->next;
		fore = fore->next;
		back = back->next;
	}
	return FALSE;
}

/**
 * workbook_sheet_reorder:
 * @wb:          workbook to look for
 * @new_order:   list of sheets
 *
 * Adjusts the order of the sheets.
 *
 * Returns FALSE when it was successful
 **/
gboolean
workbook_sheet_reorder (Workbook *wb, GSList *new_order)
{
	GSList *this_sheet;
	gint old_pos, new_pos = 0;

	g_return_val_if_fail (IS_WORKBOOK (wb), FALSE);

	if (new_order == NULL)
		return TRUE;

	pre_sheet_index_change (wb);

	this_sheet = new_order;
	while (this_sheet) {
		Sheet *sheet = this_sheet->data;

		if (sheet != NULL) {
			old_pos = sheet->index_in_wb;
			if (new_pos != old_pos) {
				int max_pos = MAX (old_pos, new_pos);
				int min_pos = MIN (old_pos, new_pos);

				g_ptr_array_remove_index (wb->sheets, old_pos);
				gnm_ptr_array_insert (wb->sheets, sheet, new_pos);
				for (; max_pos >= min_pos ; max_pos--) {
					Sheet *sheet = g_ptr_array_index (wb->sheets, max_pos);
					sheet->index_in_wb = max_pos;
				}
				WORKBOOK_FOREACH_CONTROL (wb, view, control,
							  wb_control_sheet_move (control,
										 sheet, new_pos););
			}
			new_pos++;
		}
		this_sheet = this_sheet->next;
	}

	post_sheet_index_change (wb);

	return FALSE;
}


/**
 * workbook_sheet_reorder_by_idx:
 * @wb:          workbook to look for
 * @new_order:   list of sheet indices in order
 *
 * Adjusts the order of the sheets.
 *
 * Returns FALSE when it was successful
 **/
gboolean
workbook_sheet_reorder_by_idx (Workbook *wb, GSList *new_order)
{
	GSList *sheets = NULL;
	gboolean result;

	g_return_val_if_fail (IS_WORKBOOK (wb), FALSE);

	if (new_order == NULL)
		return TRUE;

	while (new_order) {
		sheets = g_slist_prepend (sheets, 
					  workbook_sheet_by_index 
					  (wb,
					   GPOINTER_TO_INT (new_order->data)));
		new_order = new_order->next;
	}
	sheets = g_slist_reverse (sheets);

	result = workbook_sheet_reorder (wb, sheets);

	g_slist_free (sheets);

	return result;
}

/**
 * workbook_uses_1904 :
 * @wb :
 *
 * Does @wb use the 1904 date convention ?  This could be expanded to return a
 * locale-ish type object to get passed around.  However, since we use libc for
 * some of the formatting and parsing we can not get around setting the actual
 * locale globally and there is not much that I can think of to put in here for
 * now.  Hence I'll leave it as a boolean.
 **/
GnmDateConventions const *
workbook_date_conv (Workbook const *wb)
{
	g_return_val_if_fail (wb != NULL, NULL);
	return &wb->date_conv;
}

/**
 * workbook_set_1904 :
 * @wb :
 * @flag : new value
 *
 * Sets the 1904 flag to @flag and returns the old value.
 * NOTE : THIS IS NOT A SMART ROUTINE.  If you want to actually change this
 * We'll need to recalc and rerender everything.  That will nee to be done
 * externally.
 **/
gboolean
workbook_set_1904 (Workbook *wb, gboolean flag)
{
	gboolean old_val;

	g_return_val_if_fail (IS_WORKBOOK (wb), FALSE);

	old_val = wb->date_conv.use_1904;
	wb->date_conv.use_1904 = flag;
	return old_val;
}

GSF_CLASS (Workbook, workbook,
	   workbook_class_init, workbook_init,
	   GO_DOC_TYPE);

