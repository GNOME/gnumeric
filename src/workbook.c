/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * workbook.c: workbook model and manipulation utilities
 *
 * Authors:
 *    Miguel de Icaza (miguel@gnu.org).
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1998, 1999, 2000 Miguel de Icaza
 * (C) 2000-2001 Helix Code, Inc.
 */
#include <config.h>

#include <stdlib.h>
#include "workbook.h"
#include "workbook-view.h"
#include "workbook-control.h"
#include "workbook-private.h"
#include "command-context.h"
#include "application.h"
#include "sheet.h"
#include "sheet-control.h"
#include "expr.h"
#include "expr-name.h"
#include "eval.h"
#include "value.h"
#include "ranges.h"
#include "history.h"
#include "commands.h"
#include "main.h"
#include "file.h"
#include "io-context.h"
#include "gutils.h"

#ifdef ENABLE_BONOBO
#include <bonobo/bonobo-persist-file.h>
#include "sheet-object-container.h"
#include "sheet-object-bonobo.h"
#endif

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <gal/util/e-util.h>
#include <ctype.h>

static GtkObjectClass *workbook_parent_class;

/* Workbook signals */
enum {
	CELL_CHANGED,
	LAST_SIGNAL
};

static gint workbook_signals [LAST_SIGNAL] = {
	0, /* CELL_CHANGED */
};

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
	GList *hl;
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
cb_saver_destroy_event (GtkObject *obj, gpointer *data)
{
	g_return_if_fail (IS_GNUM_FILE_SAVER (obj));
	g_return_if_fail (IS_WORKBOOK (data));

	WORKBOOK (data)->file_saver = NULL;
	WORKBOOK (data)->file_saver_sig_id = 0;
}

static void
workbook_destroy (GtkObject *wb_object)
{
	Workbook *wb = WORKBOOK (wb_object);
	GList *sheets, *ptr;

	wb->priv->during_destruction = TRUE;

	if (wb->file_saver_sig_id != 0) {
		gtk_signal_disconnect (GTK_OBJECT (wb->file_saver), wb->file_saver_sig_id);
		wb->file_saver_sig_id = 0;
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
	expr_name_invalidate_refs_wb (wb);

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
			sheet_dump_dependencies (sheet);
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
		WORKBOOK_FOREACH_VIEW (wb, view,
		{
			workbook_detach_view (view);
			gtk_object_unref (GTK_OBJECT (view));
		});
		if (wb->wb_views != NULL)
			g_warning ("Unexpected left over views");
	}

	/* Remove ourselves from the list of workbooks.  */
	application_workbook_list_remove (wb);

	/* Now do deletions that will put this workbook into a weird
	   state.  Careful here.  */
	g_hash_table_destroy (wb->sheet_hash_private);

	wb->names = expr_name_list_destroy (wb->names);

	workbook_private_delete (wb->priv);

	if (wb->file_format_level >= FILE_FL_MANUAL_REMEMBER)
		workbook_history_update (application_workbook_list (), wb->filename);

	if (wb->filename)
	       g_free (wb->filename);

	if (initial_workbook_open_complete && application_workbook_list () == NULL) {
		application_history_write_config ();
		gtk_main_quit ();
	}
	GTK_OBJECT_CLASS (workbook_parent_class)->destroy (wb_object);
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

	if (wb->names || wb->external_dependents ||
#ifdef ENABLE_BONOBO
	    wb->priv->workbook_views ||
#endif
	    (wb->file_format_level > FILE_FL_NEW))
		return FALSE;

	/* Check if we seem to contain anything */
	g_hash_table_foreach (wb->sheet_hash_private, cb_sheet_check_pristine,
			      &pristine);

	return pristine;
}

#ifdef ENABLE_BONOBO

static int
workbook_persist_file_load (BonoboPersistFile *ps, const CORBA_char *filename,
			    CORBA_Environment *ev, void *closure)
{
	WorkbookView *wbv = closure;

	return wb_view_open (wbv, /* FIXME */ NULL, filename, FALSE) ? 0 : -1;
}

static int
workbook_persist_file_save (BonoboPersistFile *ps, const CORBA_char *filename,
			    CORBA_Environment *ev, void *closure)
{
	WorkbookView *wbv = closure;
	GnumFileSaver *fs;

	fs = get_file_saver_by_id ("Gnumeric_XmlIO:gnum_xml");
	return wb_view_save_as (wbv, /* FIXME */ NULL, fs, filename) ? 0 : -1;
}

extern Bonobo_Unknown
workbook_container_get_object (BonoboObject *container, CORBA_char *item_name,
			       CORBA_boolean only_if_exists, CORBA_Environment *ev,
			       Workbook *wb);
static void
workbook_bonobo_setup (Workbook *wb)
{
	wb->priv->bonobo_container = BONOBO_ITEM_CONTAINER (bonobo_item_container_new ());

	/* FIXME : This is totaly broken.
	 * 1) it does not belong here at the workbook level
	 * 2) which bonobo object to use ?
	 * 3) it should not be in this file.
	 */
	wb->priv->persist_file = bonobo_persist_file_new (
		workbook_persist_file_load,
		workbook_persist_file_save,
		wb);

	bonobo_object_add_interface (
		BONOBO_OBJECT (wb->priv),
		BONOBO_OBJECT (wb->priv->bonobo_container));
	bonobo_object_add_interface (
		BONOBO_OBJECT (wb->priv),
		BONOBO_OBJECT (wb->priv->persist_file));

	gtk_signal_connect (
		GTK_OBJECT (wb->priv->bonobo_container), "get_object",
		GTK_SIGNAL_FUNC (workbook_container_get_object), wb);
}
#endif

static void
workbook_init (GtkObject *object)
{
	Workbook *wb = WORKBOOK (object);

	wb->priv = workbook_private_new ();
	wb->wb_views = NULL;
	wb->sheets = g_ptr_array_new ();
	wb->sheet_hash_private = g_hash_table_new (gnumeric_strcase_hash,
						   gnumeric_strcase_equal);
	wb->names        = NULL;
	wb->summary_info   = summary_info_new ();
	summary_info_default (wb->summary_info);

	/* Nothing to undo or redo */
	wb->undo_commands = wb->redo_commands = NULL;

	/* default to no iteration */
	wb->iteration.enabled = FALSE;
	wb->iteration.max_number = 100;
	wb->iteration.tolerance = .001;

	application_workbook_list_add (wb);

#if 0
	workbook_corba_setup (wb);
#endif
#ifdef ENABLE_BONOBO
	workbook_bonobo_setup (wb);
#endif
}

static void
workbook_class_init (GtkObjectClass *object_class)
{
	workbook_parent_class = gtk_type_class (gtk_object_get_type ());

	/*
	 * WARNING :
	 * This is a preliminary hook used by screen reading software,
	 * etc.  The signal does NOT trigger for all cell changes and
	 * should be used with care.
	 */
	workbook_signals [CELL_CHANGED] =
		gtk_signal_new (
			"cell_changed",
			GTK_RUN_LAST,
			object_class->type,
			GTK_SIGNAL_OFFSET (WorkbookClass,
					   cell_changed),
			gtk_marshal_NONE__POINTER_POINTER_INT_INT,
			GTK_TYPE_NONE,
			1,
			GTK_TYPE_POINTER);
	gtk_object_class_add_signals (object_class, workbook_signals, LAST_SIGNAL);

	object_class->destroy = workbook_destroy;
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

	wb = gtk_type_new (workbook_get_type ());

	/* Assign a default name */
	do {
		char *name = g_strdup_printf (_("Book%d.%s"), ++count, extension);
		is_unique = workbook_set_filename (wb, name);
		g_free (name);
	} while (!is_unique);
	wb->file_format_level = FILE_FL_NEW;
	wb->file_saver        = NULL;
	wb->file_saver_sig_id = 0;

	wb->priv->during_destruction = FALSE;

#ifdef ENABLE_BONOBO
	wb->priv->workbook_views  = NULL;
	wb->priv->persist_file    = NULL;
#endif
	return wb;
}

/**
 * workbook_sheet_name_strip_number:
 * @name: name to strip number from
 * @number: returns the number stripped off in *number
 *
 * Gets a name in the form of "Sheet (10)", "Stuff" or "Dummy ((((,"
 * and returns the real name of the sheet "Sheet","Stuff","Dymmy ((((,"
 * without the copy number.
 **/
static void
workbook_sheet_name_strip_number (char *name, int* number)
{
	char *end;

	*number = 1;

	end = strrchr (name, ')');
	if (end == NULL || end[1] != '\0')
		return;

	while (--end >= name) {
		if (*end == '(') {
			*number = atoi (end + 1);
			*end = '\0';
			return;
		}
		if (!isdigit ((unsigned char)*end))
			return;
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
workbook_set_filename (Workbook *wb, const char *name)
{
	char const *base_name;
	g_return_val_if_fail (wb != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	if (wb->filename)
		g_free (wb->filename);

	wb->filename = g_strdup (name);
	base_name = g_basename (name);

	WORKBOOK_FOREACH_CONTROL (wb, view, control,
		wb_control_title_set (control, base_name););

	return TRUE;
}

const gchar *
workbook_get_filename (Workbook *wb)
{
	g_return_val_if_fail (IS_WORKBOOK (wb), NULL);

	return wb->filename;
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
	    !workbook_set_filename (wb, file_name)) {
		return FALSE;
	}

	wb->file_format_level = level;
	if (wb->file_saver_sig_id != 0) {
		gtk_signal_disconnect (GTK_OBJECT (wb->file_saver), wb->file_saver_sig_id);
	}
	wb->file_saver = fs;
	if (wb->file_saver != NULL) {
		wb->file_saver_sig_id = gtk_signal_connect (GTK_OBJECT (wb->file_saver),
		                        "destroy",
		                        GTK_SIGNAL_FUNC (cb_saver_destroy_event),
		                        wb);
	} else {
		wb->file_saver_sig_id = 0;
	}

	return TRUE;
}

GnumFileSaver *
workbook_get_file_saver (Workbook *wb)
{
	g_return_val_if_fail (IS_WORKBOOK (wb), NULL);

	return wb->file_saver;
}


typedef struct
{
    	int dep_type;
	union {
		EvalPos    pos;
		Dependent *dep;
	} u;
	ExprTree *oldtree;
} ExprRelocateStorage;

/**
 * workbook_expr_unrelocate_free : Release the storage associated with
 *    the list.
 */
void
workbook_expr_unrelocate_free (GSList *info)
{
	while (info != NULL) {
		GSList *cur = info;
		ExprRelocateStorage *tmp = (ExprRelocateStorage *)(info->data);

		info = info->next;
		expr_tree_unref (tmp->oldtree);
		g_free (tmp);
		g_slist_free_1 (cur);
	}
}

void
workbook_expr_unrelocate (Workbook *wb, GSList *info)
{
	while (info != NULL) {
		GSList *cur = info;
		ExprRelocateStorage *tmp = (ExprRelocateStorage *)info->data;

		if (tmp->dep_type == DEPENDENT_CELL) {
			Cell *cell = sheet_cell_get (tmp->u.pos.sheet,
						     tmp->u.pos.eval.col,
						     tmp->u.pos.eval.row);

			/* It is possible to have a NULL if the relocation info
			 * is not really relevant.  eg when undoing a pasted
			 * cut that was relocated but also saved to a buffer.
			 */
			if (cell != NULL)
				sheet_cell_set_expr (cell, tmp->oldtree);
		} else
			dependent_set_expr (tmp->u.dep, tmp->oldtree);
#warning Check to see what recalc assumptions the callers make
		expr_tree_unref (tmp->oldtree);

		info = info->next;
		g_free (tmp);
		g_slist_free_1 (cur);
	}
}

/**
 * workbook_expr_relocate:
 * Fixes references to or from a region that is going to be moved.
 *
 * @wb: the workbook to modify
 * @info : the descriptor record for what is being moved where.
 *
 * Returns a list of the locations and expressions that were changed outside of
 * the region.
 * NOTE : Does not queue the changed elemenents or their recursive dependents
 * for recalc
 */
GSList *
workbook_expr_relocate (Workbook *wb, ExprRelocateInfo const *info)
{
	GSList *dependents, *l;
	GSList *undo_info = NULL;
	int i;

	if (info->col_offset == 0 && info->row_offset == 0 &&
	    info->origin_sheet == info->target_sheet)
		return NULL;

	g_return_val_if_fail (wb != NULL, NULL);

	/* Copy the list since it will change underneath us.  */
	dependents = NULL;
	WORKBOOK_FOREACH_DEPENDENT
		(wb, dep, dependents = g_slist_prepend (dependents, dep));

	for (l = dependents; l; l = l->next)	{
		Dependent *dep = l->data;
		ExprRewriteInfo rwinfo;
		ExprTree *newtree;

		rwinfo.type = EXPR_REWRITE_RELOCATE;
		memcpy (&rwinfo.u.relocate, info, sizeof (ExprRelocateInfo));
		eval_pos_init_dep (&rwinfo.u.relocate.pos, dep);

		newtree = expr_rewrite (dep->expression, &rwinfo);

		if (newtree) {
			int const t = dependent_type (dep);

			/* Don't store relocations if they were inside the region
			 * being moved.  That is handled elsewhere */
			if (t != DEPENDENT_CELL ||
			    info->origin_sheet != rwinfo.u.relocate.pos.sheet ||
			    !range_contains (&info->origin,
					     rwinfo.u.relocate.pos.eval.col,
					     rwinfo.u.relocate.pos.eval.row)) {
				ExprRelocateStorage *tmp =
					g_new (ExprRelocateStorage, 1);

				tmp->dep_type = t;
				if (t != DEPENDENT_CELL)
					tmp->u.dep = dep;
				else
					tmp->u.pos = rwinfo.u.relocate.pos;
				tmp->oldtree = dep->expression;
				expr_tree_ref (tmp->oldtree);
				undo_info = g_slist_prepend (undo_info, tmp);
			}

			dependent_set_expr (dep, newtree);
			expr_tree_unref (newtree);
		}
	}

	g_slist_free (dependents);

	/* flag all sheets to update status line given than we may have moved things */
	for (i = wb->sheets->len; i-- > 0 ;)
		sheet_flag_status_update_range (
			g_ptr_array_index (wb->sheets, i), NULL);

	return undo_info;
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
	gtk_object_unref (GTK_OBJECT (wb));
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
 * NOTE : Does not yet handle 3D references.
 *
 * Return value:
 *    non-NULL on error, or value_terminate() if some invoked routine requested
 *    to stop (by returning non-NULL).
 */
Value *
workbook_foreach_cell_in_range (EvalPos const *pos,
				Value const	*cell_range,
				gboolean	 only_existing,
				ForeachCellCB	 handler,
				void		*closure)
{
	Range  r;
	Sheet *start_sheet, *end_sheet;

	g_return_val_if_fail (pos != NULL, NULL);
	g_return_val_if_fail (cell_range != NULL, NULL);

	g_return_val_if_fail (cell_range->type == VALUE_CELLRANGE, NULL);

	value_cellrange_normalize (pos, cell_range, &start_sheet, &end_sheet, &r);

	/* We cannot support this until the Sheet management is tidied up.  */
	if (start_sheet != end_sheet)
		g_warning ("3D references are not supported yet, using 1st sheet");

	return sheet_foreach_cell_in_range (start_sheet, only_existing,
					    r.start.col, r.start.row,
					    r.end.col, r.end.row,
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
	gboolean old = wb->priv->recursive_dirty_enabled;
	wb->priv->recursive_dirty_enabled = enable;
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

	wbv->wb = wb;
	if (wbv->wb->wb_views == NULL)
		wbv->wb->wb_views = g_ptr_array_new ();
	g_ptr_array_add (wbv->wb->wb_views, wbv);

	/* Set the titles of the newly connected view's controls */
	if (wbv->wb != NULL) {
		char const *base_name = g_basename (wb->filename);
		WORKBOOK_VIEW_FOREACH_CONTROL (wbv, wbc,
			wb_control_title_set (wbc, base_name););
	}
}

void
workbook_detach_view (WorkbookView *wbv)
{
	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));
	g_return_if_fail (IS_WORKBOOK (wbv->wb));

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

int
workbook_sheet_index_get (Workbook const *wb, Sheet const * sheet)
{
	int i;

	g_return_val_if_fail (IS_WORKBOOK (wb), 0);

	for (i = wb->sheets->len ; i-- > 0 ; )
		if (sheet == g_ptr_array_index (wb->sheets, i))
			return i;
	return -1;
}

Sheet *
workbook_sheet_by_index (Workbook const *wb, int i)
{
	g_return_val_if_fail (IS_WORKBOOK (wb), 0);
	g_return_val_if_fail ((int)wb->sheets->len > i, 0);

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

void
workbook_sheet_attach (Workbook *wb, Sheet *new_sheet,
		       Sheet const *insert_after)
{
	g_return_if_fail (IS_WORKBOOK (wb));
	g_return_if_fail (IS_SHEET (new_sheet));
	g_return_if_fail (new_sheet->workbook == wb);

	if (insert_after != NULL) {
		int pos = workbook_sheet_index_get (wb, insert_after);
		g_ptr_array_insert (wb->sheets, (gpointer)new_sheet, pos+1);
	} else
		g_ptr_array_add (wb->sheets, new_sheet);

	g_hash_table_insert (wb->sheet_hash_private,
			     new_sheet->name_unquoted, new_sheet);

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
	SHEET_FOREACH_CONTROL (sheet, control,
		sc_mode_edit (control););

	sheet_index = workbook_sheet_index_get (wb, sheet);

	/* If not exiting, adjust the focus for any views whose focus sheet
	 * was the one being deleted, and prepare to recalc */
	if (!wb->priv->during_destruction) {
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

	/* Remove our reference to this sheet */
	g_ptr_array_remove_index (wb->sheets, sheet_index);
	g_hash_table_remove (wb->sheet_hash_private, sheet->name_unquoted);
	sheet_destroy (sheet);

	if (focus != NULL)
		workbook_recalc_all (wb);

	return TRUE;
}

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

	/* Important to do these BEFORE detaching the sheet */
	sheet_deps_destroy (sheet);
	expr_name_invalidate_refs_sheet (sheet);

	/* All is fine, remove the sheet */
	workbook_sheet_detach (wb, sheet);
}

/*
 * Moves the sheet up or down @direction spots in the sheet list
 * If @direction is positive, move left. If positive, move right.
 */
void
workbook_sheet_move (Sheet *sheet, int direction)
{
	Workbook *wb;
	gint old_pos, new_pos;

	g_return_if_fail (IS_SHEET (sheet));

	wb = sheet->workbook;
        old_pos = workbook_sheet_index_get (wb, sheet);
	new_pos = old_pos + direction;

	if (0 <= new_pos && new_pos < workbook_sheet_count (wb)) {
		g_ptr_array_remove_index (wb->sheets, old_pos);
		g_ptr_array_insert (wb->sheets, sheet, new_pos);
		WORKBOOK_FOREACH_CONTROL (wb, view, control,
			wb_control_sheet_move (control, sheet, new_pos););
		sheet_set_dirty (sheet, TRUE);
	}
}

/**
 * workbook_sheet_get_free_name:
 * @wb:   workbook to look for
 * @base: base for the name, e. g. "Sheet"
 * @name_format : optionally null format for handling dupilicates.
 * @always_suffix: if true, add suffix even if the name "base" is not in use.
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
 * workbook_sheet_rename:
 * @wb:       the workbook where the sheet is
 * @old_name: the name of the sheet we want to rename
 * @new_name: new name we want to assing to the sheet.
 *
 * Returns TRUE if there was a problem changing the name
 * return FALSE otherwise.
 */
gboolean
workbook_sheet_rename (WorkbookControl *wbc,
		       Workbook *wb,
		       const char *old_name,
		       const char *new_name)
{
	Sheet *tmp, *sheet;

	g_return_val_if_fail (wb != NULL, TRUE);
	g_return_val_if_fail (old_name != NULL, TRUE);
	g_return_val_if_fail (new_name != NULL, TRUE);

	/* Did the name change? */
	if (strcmp (old_name, new_name) == 0)
		return TRUE;

	if (strlen (new_name) < 1) {
		gnumeric_error_invalid (COMMAND_CONTEXT (wbc), _("Sheet name"),
					_("must have at least 1 letter"));
		return TRUE;
	}

	sheet = (Sheet *) g_hash_table_lookup (wb->sheet_hash_private,
					       old_name);

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	/* Do not let two sheets in the workbook have the same name */
	tmp = (Sheet *) g_hash_table_lookup (wb->sheet_hash_private, new_name);
	if (tmp != NULL && tmp != sheet) {
		gnumeric_error_invalid (COMMAND_CONTEXT (wbc),
					_("There is already a sheet named"),
					new_name);
		return TRUE;
	}

	g_hash_table_remove (wb->sheet_hash_private, old_name);
	sheet_rename (sheet, new_name);
	g_hash_table_insert (wb->sheet_hash_private,
			     sheet->name_unquoted, sheet);

	sheet_set_dirty (sheet, TRUE);

	WORKBOOK_FOREACH_CONTROL (wb, view, control,
		wb_control_sheet_rename	(control, sheet););

	return FALSE;
}

E_MAKE_TYPE (workbook, "Workbook", Workbook,
	     workbook_class_init, workbook_init,
	     GTK_TYPE_OBJECT);
