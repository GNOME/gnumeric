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
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "workbook.h"

#include "workbook-view.h"
#include "workbook-control.h"
#include "command-context.h"
#include "application.h"
#include "sheet.h"
#include "cell.h"
#include "sheet-control.h"
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

#include <gtk/gtkmain.h> /* for gtk_main_quit */
#include <gsf/gsf-impl-utils.h>
#include <gal/util/e-util.h>
#include <ctype.h>
#include <string.h>

static GObjectClass *workbook_parent_class;

/* Signals */
enum {
	SUMMARY_CHANGED,
	FILENAME_CHANGED,
	LAST_SIGNAL
};

static GQuark signals [LAST_SIGNAL] = { 0 };


/*
 * We introduced numbers in front of the the history file names for two
 * reasons:
 * 1. Bonobo won't let you make 2 entries with the same label in the same
 *    menu. But that's what happens if you e.g. access worksheets with the
 *    same name from 2 different directories.
 * 2. The numbers are useful accelerators.
 * 3. Excel does it this way.
 *
 * Because numbers are reassigned with each insertion, we have to remove all
 * the old entries and insert new ones.
 */
static void
workbook_history_update (GList *wl, gchar *filename)
{
	gchar *del_name;
	gchar *canonical_name;
	GSList *hl;
	gchar *cwd;
	gboolean add_sep;

	/* Rudimentary filename canonicalization. */
	if (!g_path_is_absolute (filename)) {
		cwd = g_get_current_dir ();
		canonical_name = g_strconcat (cwd, "/", filename, NULL);
		g_free (cwd);
	} else
		canonical_name = g_strdup (filename);

	/* Get the history list */
	hl = application_history_get_list ();

	/* If List is empty, a separator will be needed too. */
	add_sep = (hl == NULL);

	/* Do nothing if filename already at head of list */
	if (!(hl && strcmp ((gchar *)hl->data, canonical_name) == 0)) {
		history_menu_flush (wl, hl); /* Remove the old entries */

		/* Update the history list */
		del_name = application_history_update_list (canonical_name);
		g_free (del_name);

		/* Fill the menus */
		hl = application_history_get_list ();
		history_menu_fill (wl, hl, add_sep);
	}
	g_free (canonical_name);
}

static void
cb_saver_finalize (Workbook *wb, GnumFileSaver *saver)
{
	g_return_if_fail (IS_GNUM_FILE_SAVER (saver));
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
		 * the sheet from the workbook.  Its ugly, but should
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

		workbook_sheet_detach (wb, sheet);
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

	/* Remove ourselves from the list of workbooks.  */
	application_workbook_list_remove (wb);

	/* Now do deletions that will put this workbook into a weird
	   state.  Careful here.  */
	g_hash_table_destroy (wb->sheet_hash_private);
	wb->sheet_hash_private = NULL;

	g_ptr_array_free (wb->sheets, TRUE);
	wb->sheets = NULL;

	if (wb->file_format_level >= FILE_FL_MANUAL_REMEMBER)
		workbook_history_update (application_workbook_list (), wb->filename);

	if (wb->filename) {
	       g_free (wb->filename);
	       wb->filename = NULL;
	}

#warning this has no business being here
	if (initial_workbook_open_complete && application_workbook_list () == NULL) {
		application_history_write_config ();
		gtk_main_quit ();
	}
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
	g_return_if_fail (wb != NULL);

	wb->modified = is_dirty;
	if (wb->summary_info != NULL)
		wb->summary_info->modified = is_dirty;
	g_hash_table_foreach (wb->sheet_hash_private,
			      cb_sheet_mark_dirty, GINT_TO_POINTER (is_dirty));
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

	g_hash_table_foreach (wb->sheet_hash_private, cb_sheet_check_dirty,
			      &dirty);

	return dirty;
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
	g_hash_table_foreach (wb->sheet_hash_private, cb_sheet_check_pristine,
			      &pristine);

	return pristine;
}

static void
workbook_init (GObject *object)
{
	Workbook *wb = WORKBOOK (object);

	wb->wb_views = NULL;
	wb->sheets = g_ptr_array_new ();
	wb->sheet_hash_private = g_hash_table_new (gnumeric_strcase_hash,
						   gnumeric_strcase_equal);
	wb->sheet_order_dependents = NULL;
	wb->names        = NULL;
	wb->summary_info = summary_info_new ();
	summary_info_default (wb->summary_info);
	wb->summary_info->modified = FALSE;

	/* Nothing to undo or redo */
	wb->undo_commands = wb->redo_commands = NULL;

	/* default to no iteration */
	wb->iteration.enabled = FALSE;
	wb->iteration.max_number = 100;
	wb->iteration.tolerance = .001;

	wb->file_format_level = FILE_FL_NEW;
	wb->file_saver        = NULL;

	wb->during_destruction = FALSE;
	wb->recursive_dirty_enabled = TRUE;

	application_workbook_list_add (wb);
}

static void
workbook_class_init (GObjectClass *object_class)
{
	workbook_parent_class = g_type_class_peek (G_TYPE_OBJECT);

	object_class->finalize = workbook_finalize;

	signals [SUMMARY_CHANGED] = g_signal_new ("summary_changed",
		WORKBOOK_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WorkbookClass, summary_changed),
		(GSignalAccumulator) NULL, NULL,
		gnm__VOID__VOID,
		G_TYPE_NONE,
		0, G_TYPE_NONE);
	
	signals [FILENAME_CHANGED] = g_signal_new ("filename_changed",
		WORKBOOK_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WorkbookClass, filename_changed),
		(GSignalAccumulator) NULL, NULL,
		gnm__VOID__VOID,
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
	GnumFileSaver *def_save = get_default_file_saver ();
	char const *extension = NULL;

	if (def_save != NULL)
		extension = gnum_file_saver_get_extension (def_save);
	if (extension == NULL)
		extension = "gnumeric";

	wb = g_object_new (WORKBOOK_TYPE, NULL);

	/* Assign a default name */
	do {
		char *name = g_strdup_printf (_("Book%d.%s"), ++count, extension);
		is_unique = workbook_set_filename (wb, name);
		g_free (name);
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
 * workbook_set_filename:
 * @wb: the workbook to modify
 * @name: the file name for this worksheet.
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
workbook_set_filename (Workbook *wb, char const *name)
{
	char *base_name;
	g_return_val_if_fail (wb != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	if (wb->filename)
		g_free (wb->filename);

	wb->filename = g_strdup (name);
	base_name = g_path_get_basename (name);

	WORKBOOK_FOREACH_CONTROL (wb, view, control,
		wb_control_title_set (control, base_name););
	g_free (base_name);

	g_signal_emit (G_OBJECT (wb), signals [FILENAME_CHANGED], 0);
	return TRUE;
}

const gchar *
workbook_get_filename (Workbook *wb)
{
	g_return_val_if_fail (IS_WORKBOOK (wb), NULL);

	return wb->filename;
}

void workbook_add_summary_info (Workbook *wb, SummaryItem *sit)
{
	if (summary_info_add (wb->summary_info, sit))
		g_signal_emit (G_OBJECT (wb), signals [SUMMARY_CHANGED], 0);
}

/**
 * workbook_set_saveinfo:
 * @wb: the workbook to modify
 * @name: the file name for this worksheet.
 * @level: the file format level
 *
 * Provided level is at least as high as current level,
 * calls workbook_set_filename, and sets level and saver.
 *
 * Returns : TRUE if save info was set succesfully.
 *
 * FIXME : Add a check to ensure the name is unique.
 */
gboolean
workbook_set_saveinfo (Workbook *wb, const gchar *file_name,
                       FileFormatLevel level, GnumFileSaver *fs)
{
	g_return_val_if_fail (wb != NULL, FALSE);
	g_return_val_if_fail (file_name != NULL, FALSE);
	g_return_val_if_fail (level > FILE_FL_NONE && level <= FILE_FL_AUTO,
			      FALSE);

	if (level < wb->file_format_level ||
	    !workbook_set_filename (wb, file_name))
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

GnumFileSaver *
workbook_get_file_saver (Workbook *wb)
{
	g_return_val_if_fail (IS_WORKBOOK (wb), NULL);

	return wb->file_saver;
}

static void
cb_sheet_calc_spans (gpointer key, gpointer value, gpointer flags)
{
	sheet_calc_spans (value, GPOINTER_TO_INT(flags));
}
void
workbook_calc_spans (Workbook *wb, SpanCalcFlags const flags)
{
	g_return_if_fail (wb != NULL);

	g_hash_table_foreach (wb->sheet_hash_private,
			      &cb_sheet_calc_spans, GINT_TO_POINTER (flags));
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
Value *
workbook_foreach_cell_in_range (EvalPos const *pos,
				Value const	*cell_range,
				CellIterFlags	 flags,
				CellIterFunc	 handler,
				gpointer	 closure)
{
	Range  r;
	Sheet *start_sheet, *end_sheet;

	g_return_val_if_fail (pos != NULL, NULL);
	g_return_val_if_fail (cell_range != NULL, NULL);

	g_return_val_if_fail (cell_range->type == VALUE_CELLRANGE, NULL);

	value_cellrange_normalize (pos, cell_range, &start_sheet, &end_sheet, &r);

	if (start_sheet != end_sheet) {
		Value *res;
		Workbook const *wb = start_sheet->workbook;
		int i = start_sheet->index_in_wb;
		int stop = end_sheet->index_in_wb;
		if (i < stop) { int tmp = i; i = stop ; stop = tmp; }

		g_return_val_if_fail (end_sheet->workbook == wb, VALUE_TERMINATE);

		while (i <= stop) {
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
 * Collects a GPtrArray of EvalPos pointers for all cells in a workbook.
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
			scells->len * sizeof (EvalPos *));

		g_ptr_array_free (scells, TRUE);
	}
	g_list_free (sheets);

	return cells;
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
cb_dep_unlink (Dependent *dep, gpointer value, gpointer user_data)
{
	CellPos *pos = NULL;
	if (dependent_is_cell (dep))
		pos = &DEP_TO_CELL (dep)->pos;
	dependent_unlink (dep, pos);
}
static void
pre_sheet_index_change (Workbook *wb)
{
	if (wb->sheet_order_dependents != NULL)
		g_hash_table_foreach (wb->sheet_order_dependents,
			(GHFunc) cb_dep_unlink, NULL);
}
static void
cb_dep_link (Dependent *dep, gpointer value, gpointer user_data)
{
	CellPos *pos = NULL;
	if (dependent_is_cell (dep))
		pos = &DEP_TO_CELL (dep)->pos;
	dependent_link (dep, pos);
}
static void
post_sheet_index_change (Workbook *wb)
{
	if (wb->sheet_order_dependents != NULL)
		g_hash_table_foreach (wb->sheet_order_dependents,
			(GHFunc) cb_dep_link, NULL);
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

	return g_ptr_array_index (wb->sheets, i);
}

/**
 * workbook_sheet_by_name:
 * @wb: workbook to lookup the sheet on
 * @sheet_name: the sheet name we are looking for.
 *
 * Returns a pointer to a Sheet or NULL if the sheet
 * was not found.
 */
Sheet *
workbook_sheet_by_name (Workbook const *wb, char const *sheet_name)
{
	g_return_val_if_fail (wb != NULL, NULL);
	g_return_val_if_fail (sheet_name != NULL, NULL);

	return g_hash_table_lookup (wb->sheet_hash_private, sheet_name);
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
		g_ptr_array_insert (wb->sheets, (gpointer)new_sheet, ++pos);
		workbook_sheet_index_update (wb, pos);
	} else {
		g_ptr_array_add (wb->sheets, new_sheet);
		workbook_sheet_index_update (wb, workbook_sheet_count (wb) - 1);
	}

	g_hash_table_insert (wb->sheet_hash_private,
			     new_sheet->name_unquoted, new_sheet);
	post_sheet_index_change (wb);

	WORKBOOK_FOREACH_VIEW (wb, view,
		wb_view_sheet_add (view, new_sheet););
}

/**
 * workbook_sheet_detach:
 * @wb: workbook.
 * @sheet: the sheet that we want to detach from the workbook
 *
 * Detaches @sheet from the workbook @wb.
 */
gboolean
workbook_sheet_detach (Workbook *wb, Sheet *sheet)
{
	Sheet *focus = NULL;
	int sheet_index;

	g_return_val_if_fail (IS_WORKBOOK (wb), FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (sheet->workbook == wb, FALSE);
	g_return_val_if_fail (workbook_sheet_by_name (wb, sheet->name_unquoted)
			      == sheet, FALSE);

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

		if (focus != NULL) {
			WORKBOOK_FOREACH_VIEW (wb, view,
			{
				if (view->current_sheet == sheet)
					wb_view_sheet_focus (view, focus);
			});
		}
	}

	/* Remove all controls */
	WORKBOOK_FOREACH_CONTROL (wb, view, control,
		wb_control_sheet_remove (control, sheet););

	/* If we are not destroying things,
	 * Check for 3d refs that start or end on this sheet
	 */
	if (wb->sheet_order_dependents != NULL) {
#warning TODO
		puts ("foo");
	}

	/* Remove our reference to this sheet */
	pre_sheet_index_change (wb);
	g_ptr_array_remove_index (wb->sheets, sheet_index);
	workbook_sheet_index_update (wb, sheet_index);
	sheet->index_in_wb = -1;
	g_hash_table_remove (wb->sheet_hash_private, sheet->name_unquoted);
	sheet_destroy (sheet);
	post_sheet_index_change (wb);

	if (focus != NULL)
		workbook_recalc_all (wb);

	return TRUE;
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
			wb_control_undo_redo_clear (control, TRUE);
			wb_control_undo_redo_clear (control, FALSE);
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
			wb_control_undo_redo_clear (control, TRUE);
			wb_control_undo_redo_clear (control, FALSE);
			wb_control_undo_redo_labels (control, NULL, NULL);
		);
	}

	/* Important to do these BEFORE detaching the sheet */
	sheet_deps_destroy (sheet);

	/* All is fine, remove the sheet */
	workbook_sheet_detach (wb, sheet);
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
		g_ptr_array_insert (wb->sheets, sheet, new_pos);

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

gboolean
workbook_sheet_reorganize (WorkbookControl *wbc, 
			   GSList *changed_names, GSList *new_order,  
			   GSList *new_names,  GSList *old_names,
			   GSList **new_sheets, GSList *color_changed,
			   GSList *colors_fore, GSList *colors_back,
			   GSList *protection_changed, GSList *new_locks)
{
	GSList *this_sheet;
	GSList *new_sheet = NULL;
	gint old_pos, new_pos = 0;
	GSList *the_names;
	GSList *the_sheets;
	GSList *the_fore;
	GSList *the_back;
	GSList *the_lock;
	Workbook *wb = wb_control_workbook (wbc);

/* We need to verify validity of the new names */
	the_names = new_names;
	the_sheets = changed_names;
	while (the_names) {
		Sheet *tmp;
		Sheet *sheet;
		char *new_name = the_names->data;

		g_return_val_if_fail (the_sheets != NULL, TRUE);

		if (new_name != NULL ) {
			sheet = the_sheets->data;
			
			/* Is the sheet name to short ?*/
			if (1 > strlen (new_name)) {
				gnumeric_error_invalid (COMMAND_CONTEXT (wbc), 
							_("Sheet name must have at least 1 letter"),
							new_name);
				return TRUE;
			}
			
			/* Is the sheet name already in use ?*/
			tmp = (Sheet *) g_hash_table_lookup (wb->sheet_hash_private, 
							     new_name);
			
			if (tmp != NULL) {
				/* Perhaps it is a sheet also to be renamed */
				GSList *tmp_sheets = g_slist_find (changed_names, tmp);
				if (NULL == tmp_sheets) {
					gnumeric_error_invalid (COMMAND_CONTEXT (wbc),
							 _("There is already a sheet named"),
								new_name);
					return TRUE;
				}
			}
			
			/* Will we try to use the same name a second time ?*/
			if (the_names->next != NULL && 
			    g_slist_find_custom (the_names->next, new_name, g_str_compare) != NULL) {
				gnumeric_error_invalid (COMMAND_CONTEXT (wbc),
							_("You may not use this name twice"),
							new_name);
				return TRUE;			
			}
		}
		the_names = the_names->next;
		the_sheets = the_sheets->next;
	}
/* Names are indeed valid */
/* Changing Names (except for new sheets)*/
	the_names = old_names;
	the_sheets = changed_names;
	while (the_names) {
		if (the_sheets->data != NULL)
			g_hash_table_remove (wb->sheet_hash_private, the_names->data);
		the_names = the_names->next;
		the_sheets = the_sheets->next;
	}

	the_names = new_names;
	the_sheets = changed_names;
	while (the_names) {
		Sheet *sheet = the_sheets->data;

		if (sheet != NULL) {
			sheet_rename (sheet, the_names->data);
			g_hash_table_insert (wb->sheet_hash_private,
					     sheet->name_unquoted, sheet);
			
			sheet_set_dirty (sheet, TRUE);
			
			WORKBOOK_FOREACH_CONTROL (wb, view, control,
						  wb_control_sheet_rename (control, sheet););
		}
		the_names = the_names->next;
		the_sheets = the_sheets->next;
	}

/* Names have been changed */
/* Create new sheets */

	if (new_sheets) {
		the_names = new_names;
		the_sheets = changed_names;
		while (the_names) {
			Sheet *sheet = the_sheets->data;
			if (sheet == NULL) {
				char *name = the_names->data;
				Sheet *a_new_sheet ;
				gboolean free_name = (name == NULL);
				
				if (free_name)
					name = workbook_sheet_get_free_name 
						(wb, _("Sheet"), TRUE, FALSE);
				a_new_sheet = sheet_new (wb, name);
				if (free_name)
					g_free (name);
				workbook_sheet_attach (wb, a_new_sheet, NULL);
				*new_sheets = g_slist_prepend (*new_sheets, a_new_sheet);
			}
			the_names = the_names->next;
			the_sheets = the_sheets->next;
		}
		new_sheet = *new_sheets = g_slist_reverse (*new_sheets);
	}

/* Changing Colors */
	the_fore = colors_fore;
	the_back = colors_back;
	the_sheets = color_changed;
	while (the_sheets) {
		Sheet *sheet = the_sheets->data;
		if (new_sheet && sheet == NULL) {
			sheet = new_sheet->data;
			new_sheet = new_sheet->next;
		}
		
		if (sheet != NULL) {
			GdkColor *back = (GdkColor *) the_back->data;
			GdkColor *fore = (GdkColor *) the_fore->data;
			StyleColor *tab_color = back ? 
				style_color_new (back->red, back->green, 
						 back->blue) : NULL;
			StyleColor *text_color = fore ?
				style_color_new (fore->red, fore->green, 
						 fore->blue) : NULL;
			sheet_set_tab_color (sheet, tab_color, text_color);
		}
		the_fore = the_fore->next;
		the_back = the_back->next;
		the_sheets = the_sheets->next;
	}
	
/* Changing Protection */
	the_lock = new_locks;
	the_sheets = protection_changed;
	while (the_sheets) {
		Sheet *sheet = the_sheets->data;
		if (new_sheet && sheet == NULL) {
			sheet = new_sheet->data;
			new_sheet = new_sheet->next;
		}
		if (sheet != NULL)
			sheet->is_protected = GPOINTER_TO_INT (the_lock->data);;
		the_lock = the_lock->next;
		the_sheets = the_sheets->next;
	}

/* reordering */
	new_sheet = new_sheets ? *new_sheets : NULL;
	this_sheet = new_order;
	while (this_sheet) {
		gboolean an_old_sheet = TRUE;
		Sheet *sheet = this_sheet->data;

		if (new_sheet && sheet == NULL) {
			sheet = new_sheet->data;
			new_sheet = new_sheet->next;
			an_old_sheet = FALSE;
		}
		if (sheet != NULL) {
			old_pos = sheet->index_in_wb;
			if (new_pos != old_pos) {
				int max_pos = MAX (old_pos, new_pos);
				int min_pos = MIN (old_pos, new_pos);

				g_ptr_array_remove_index (wb->sheets, old_pos);
				g_ptr_array_insert (wb->sheets, sheet, new_pos);
				for (; max_pos >= min_pos ; max_pos--) {
					Sheet *sheet = g_ptr_array_index (wb->sheets, max_pos);
					sheet->index_in_wb = max_pos;
				}
				WORKBOOK_FOREACH_CONTROL (wb, view, control,
							  wb_control_sheet_move (control, 
										 sheet, new_pos););
				if (an_old_sheet)
					sheet_set_dirty (sheet, TRUE);
			}
			new_pos++;
		}
		this_sheet = this_sheet->next;
	}
	return FALSE;
}

GSF_CLASS (Workbook, workbook,
	   workbook_class_init, workbook_init,
	   G_TYPE_OBJECT);
