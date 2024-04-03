/*
 * workbook.c: workbook model and manipulation utilities
 *
 * Authors:
 *    Miguel de Icaza (miguel@gnu.org).
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1998, 1999, 2000 Miguel de Icaza
 * (C) 2000-2001 Ximian, Inc.
 * (C) 2002-2007 Jody Goldberg
 * Copyright (C) 1999-2009 Morten Welinder (terra@gnome.org)
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <workbook-priv.h>
#include <compilation.h>

#include <workbook-view.h>
#include <workbook-control.h>
#include <command-context.h>
#include <application.h>
#include <gnumeric-conf.h>
#include <sheet.h>
#include <sheet-view.h>
#include <sheet-control.h>
#include <cell.h>
#include <expr.h>
#include <expr-name.h>
#include <dependent.h>
#include <value.h>
#include <ranges.h>
#include <history.h>
#include <commands.h>
#include <libgnumeric.h>
#include <gutils.h>
#include <gnm-marshalers.h>
#include <style-color.h>
#include <sheet-style.h>
#include <sheet-object-graph.h>

#include <goffice/goffice.h>

#include <gsf/gsf-doc-meta-data.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-meta-names.h>
#include <gnm-i18n.h>
#include <string.h>
#include <errno.h>

/**
 * Workbook:
 * @wb_views: (element-type WorkbookView):
 **/

enum {
	PROP_0,
	PROP_RECALC_MODE,
	PROP_BEING_LOADED
};
enum {
	SHEET_ORDER_CHANGED,
	SHEET_ADDED,
	SHEET_DELETED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };
static GObjectClass *workbook_parent_class;

static void
cb_saver_finalize (Workbook *wb, GOFileSaver *saver)
{
	g_return_if_fail (GO_IS_FILE_SAVER (saver));
	g_return_if_fail (GNM_IS_WORKBOOK (wb));
	g_return_if_fail (wb->file_saver == saver);
	wb->file_saver = NULL;
}
static void
cb_exporter_finalize (Workbook *wb, GOFileSaver *saver)
{
	g_return_if_fail (GO_IS_FILE_SAVER (saver));
	g_return_if_fail (GNM_IS_WORKBOOK (wb));
	g_return_if_fail (wb->file_exporter == saver);
	workbook_set_file_exporter (wb, NULL);
}

void
workbook_update_history (Workbook *wb, GnmFileSaveAsStyle type)
{
	g_return_if_fail (GNM_IS_WORKBOOK (wb));

	switch (type) {
	case GNM_FILE_SAVE_AS_STYLE_SAVE:
		if (wb->doc.uri && wb->file_format_level >= GO_FILE_FL_MANUAL_REMEMBER) {
			const char *mimetype = wb->file_saver
				? go_file_saver_get_mime_type (wb->file_saver)
				: NULL;
			gnm_app_history_add (wb->doc.uri, mimetype);
		}
		break;
	case GNM_FILE_SAVE_AS_STYLE_EXPORT:
	default:
		if (wb->last_export_uri &&
		    wb->file_export_format_level >= GO_FILE_FL_MANUAL_REMEMBER) {
			const char *mimetype = wb->file_exporter
				? go_file_saver_get_mime_type (wb->file_exporter)
				: NULL;
			gnm_app_history_add (wb->last_export_uri, mimetype);
		}
		break;
	}
}

void
workbook_update_graphs (Workbook *wb)
{
	WORKBOOK_FOREACH_SHEET (wb, sheet, ({
		GSList *l, *graphs = sheet_objects_get (sheet, NULL, GNM_SO_GRAPH_TYPE);
		for (l = graphs; l; l = l->next) {
			SheetObject *sog = l->data;
			gog_graph_force_update (sheet_object_graph_get_gog (sog));
		}
		g_slist_free (graphs);
	}));
}


static void
workbook_dispose (GObject *wb_object)
{
	Workbook *wb = WORKBOOK (wb_object);
	GSList *controls = NULL;
	GPtrArray *sheets;
	unsigned ui;

	wb->during_destruction = TRUE;

	if (wb->file_saver)
		workbook_set_saveinfo (wb, GO_FILE_FL_AUTO, NULL);
	if (wb->file_exporter)
		workbook_set_saveinfo (wb, GO_FILE_FL_WRITE_ONLY, NULL);
	workbook_set_last_export_uri (wb, NULL);

	// Remove all the sheet controls to avoid displaying while we exit
	// However, hold on to a ref for each -- dialogs like to refer
	// to ->wbcg during destruction
	WORKBOOK_FOREACH_CONTROL (wb, view, control,
		controls = g_slist_prepend (controls, g_object_ref (control));
		wb_control_sheet_remove_all (control););

	/* Get rid of all the views */
	WORKBOOK_FOREACH_VIEW (wb, wbv, {
		wb_view_detach_from_workbook (wbv);
		g_object_unref (wbv);
	});
	if (wb->wb_views != NULL)
		g_warning ("Unexpected left over views");

	command_list_release (wb->undo_commands);
	wb->undo_commands = NULL;
	command_list_release (wb->redo_commands);
	wb->redo_commands = NULL;

	dependents_workbook_destroy (wb);

	/* Copy the set of sheets, the list changes under us. */
	sheets = g_ptr_array_sized_new (wb->sheets->len);
	for (ui = 0; ui < wb->sheets->len; ui++)
		g_ptr_array_add (sheets, g_ptr_array_index (wb->sheets, ui));

	/* Remove all contents while all sheets still exist */
	for (ui = 0; ui < sheets->len; ui++) {
		Sheet *sheet = g_ptr_array_index (sheets, ui);
		GnmRange r;

		sheet->being_destructed = TRUE;

		sheet_destroy_contents (sheet);
		range_init_full_sheet (&r, sheet);

		sheet_style_set_range (sheet, &r, sheet_style_default (sheet));

		sheet->being_destructed = FALSE;
	}

	/* Now remove the sheets themselves */
	for (ui = 0; ui < sheets->len; ui++) {
		Sheet *sheet = g_ptr_array_index (sheets, ui);
		workbook_sheet_delete (sheet);
	}
	g_ptr_array_unref (sheets);

	// Now get rid of the control refs
	g_slist_free_full (controls, g_object_unref);

	workbook_parent_class->dispose (wb_object);
}

static void
workbook_finalize (GObject *obj)
{
	Workbook *wb = WORKBOOK (obj);

	/* Remove ourselves from the list of workbooks.  */
	gnm_app_workbook_list_remove (wb);

	if (wb->sheet_local_functions) {
		g_hash_table_destroy (wb->sheet_local_functions);
		wb->sheet_local_functions = NULL;
	}

	/* Now do deletions that will put this workbook into a weird
	   state.  Careful here.  */
	g_hash_table_destroy (wb->sheet_hash_private);
	wb->sheet_hash_private = NULL;

	g_ptr_array_free (wb->sheets, TRUE);
	wb->sheets = NULL;

	workbook_parent_class->finalize (obj);
}

static void
workbook_init (GObject *object)
{
	Workbook *wb = WORKBOOK (object);

	wb->is_placeholder = FALSE;
	wb->wb_views = NULL;
	wb->sheets = g_ptr_array_new ();
	wb->sheet_size_cached = FALSE;
	wb->sheet_hash_private = g_hash_table_new (g_str_hash, g_str_equal);
	wb->sheet_order_dependents = NULL;
	wb->sheet_local_functions = NULL;
	wb->names = gnm_named_expr_collection_new ();

	/* Nothing to undo or redo */
	wb->undo_commands = wb->redo_commands = NULL;

	/* default to no iteration */
	wb->iteration.enabled = TRUE;
	wb->iteration.max_number = 100;
	wb->iteration.tolerance = GNM_const(.001);
	wb->recalc_auto = TRUE;

	workbook_set_1904 (wb, FALSE);

	wb->file_format_level = GO_FILE_FL_NEW;
	wb->file_export_format_level = GO_FILE_FL_NEW;
	wb->file_saver        = NULL;
	wb->file_exporter     = NULL;
	wb->last_export_uri   = NULL;

	wb->during_destruction = FALSE;
	wb->being_reordered    = FALSE;
	wb->recursive_dirty_enabled = TRUE;
	wb->being_loaded = FALSE;

	gnm_app_workbook_list_add (wb);
}

static void
workbook_get_property (GObject *object, guint property_id,
		       GValue *value, GParamSpec *pspec)
{
	Workbook *wb = (Workbook *)object;

	switch (property_id) {
	case PROP_RECALC_MODE:
		g_value_set_boolean (value, wb->recalc_auto);
		break;
	case PROP_BEING_LOADED:
		g_value_set_boolean (value, wb->being_loaded);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
workbook_set_property (GObject *object, guint property_id,
		       const GValue *value, GParamSpec *pspec)
{
	Workbook *wb = (Workbook *)object;

	switch (property_id) {
	case PROP_RECALC_MODE:
		workbook_set_recalcmode (wb, g_value_get_boolean (value));
		break;
	case PROP_BEING_LOADED:
		wb->being_loaded = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static GObject *
workbook_constructor (GType type,
		      guint n_construct_properties,
		      GObjectConstructParam *construct_params)
{
	GObject *obj;
	Workbook *wb;
	static int count = 0;
	gboolean is_unique;
	GOFileSaver *def_save = go_file_saver_get_default ();
	char const *extension = NULL;

	obj = workbook_parent_class->constructor
		(type, n_construct_properties, construct_params);
	wb = WORKBOOK (obj);

	if (def_save != NULL)
		extension = go_file_saver_get_extension (def_save);
	if (extension == NULL)
		extension = "gnumeric";

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

		is_unique = go_doc_set_uri (GO_DOC (wb), uri);

		g_free (uri);
		g_free (name);
		g_free (nameutf8);
	} while (!is_unique);

	gnm_insert_meta_date (GO_DOC (wb), GSF_META_NAME_DATE_CREATED);

	return obj;
}

static void
workbook_class_init (GObjectClass *gobject_class)
{
	workbook_parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->constructor  = workbook_constructor;
	gobject_class->set_property = workbook_set_property;
	gobject_class->get_property = workbook_get_property;
	gobject_class->finalize	    = workbook_finalize;
	gobject_class->dispose	    = workbook_dispose;

        g_object_class_install_property (gobject_class, PROP_RECALC_MODE,
		 g_param_spec_boolean ("recalc-mode",
				       P_("Recalc mode"),
				       P_("Enable automatic recalculation."),
				       TRUE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));

        g_object_class_install_property (gobject_class, PROP_BEING_LOADED,
		 g_param_spec_boolean ("being-loaded",
				       P_("Being loaded"),
				       P_("Workbook is currently being loaded."),
				       TRUE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));

	signals[SHEET_ORDER_CHANGED] = g_signal_new ("sheet_order_changed",
		GNM_WORKBOOK_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WorkbookClass, sheet_order_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,
		0, G_TYPE_NONE);

	signals[SHEET_ADDED] = g_signal_new ("sheet_added",
		GNM_WORKBOOK_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WorkbookClass, sheet_added),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,
		0, G_TYPE_NONE);

	signals[SHEET_DELETED] = g_signal_new ("sheet_deleted",
		GNM_WORKBOOK_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WorkbookClass, sheet_deleted),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,
		0, G_TYPE_NONE);
}

/**
 * workbook_new:
 *
 * Returns: A new empty #Workbook with a unique name.
 **/
Workbook *
workbook_new (void)
{
	return g_object_new (GNM_WORKBOOK_TYPE, NULL);
}

/**
 * workbook_sheet_name_strip_number:
 * @name: name to strip number from
 * @number: returns the number stripped off, or 1 if no number.
 *
 * Gets a name in the form of "Sheet (10)", "Stuff" or "Dummy ((((,"
 * and returns the real name of the sheet "Sheet ", "Stuff", "Dummy ((((,"
 * without the copy number.
 **/
static void
workbook_sheet_name_strip_number (char *name, unsigned int *number)
{
	char *end, *p, *pend;
	unsigned long ul;

	*number = 1;
	g_return_if_fail (*name != 0);

	end = name + strlen (name) - 1;
	if (*end != ')')
		return;

	for (p = end; p > name; p--)
		if (!g_ascii_isdigit (p[-1]))
			break;

	if (p == name || p[-1] != '(')
		return;

	errno = 0;
	ul = strtoul (p, &pend, 10);
	if (pend != end || ul != (unsigned int)ul || errno == ERANGE)
		return;

	*number = (unsigned)ul;
	p[-1] = 0;
}

/**
 * workbook_new_with_sheets:
 * @sheet_count: initial number of sheets to create.
 *
 * Returns: a #Workbook with @sheet_count allocated
 * sheets on it
 */
Workbook *
workbook_new_with_sheets (int sheet_count)
{
	Workbook *wb = workbook_new ();
	int cols = gnm_conf_get_core_workbook_n_cols ();
	int rows = gnm_conf_get_core_workbook_n_rows ();
	if (!gnm_sheet_valid_size (cols, rows))
		gnm_sheet_suggest_size (&cols, &rows);
	while (sheet_count-- > 0)
		workbook_sheet_add (wb, -1, cols, rows);
	// Restore to pristine state
	go_doc_set_state (GO_DOC (wb), go_doc_get_saved_state (GO_DOC (wb)));
	go_doc_set_pristine (GO_DOC (wb), TRUE);
	return wb;
}

void
workbook_mark_dirty (Workbook *wb)
{
	go_doc_bump_state (GO_DOC (wb));
}


/**
 * workbook_set_saveinfo:
 * @wb: the workbook to modify
 * @lev: the file format level
 * @saver: (nullable): the file saver.
 *
 * If level is sufficiently advanced, assign the info.
 *
 * Returns: %TRUE if save info was set and history may require updating
 *
 * FIXME : Add a check to ensure the name is unique.
 */
gboolean
workbook_set_saveinfo (Workbook *wb, GOFileFormatLevel level, GOFileSaver *fs)
{
	g_return_val_if_fail (wb != NULL, FALSE);
	g_return_val_if_fail (level > GO_FILE_FL_NONE && level < GO_FILE_FL_LAST,
			      FALSE);

	if (level != GO_FILE_FL_AUTO) {
		if (wb->file_exporter != NULL)
			g_object_weak_unref (G_OBJECT (wb->file_exporter),
					     (GWeakNotify) cb_exporter_finalize, wb);
		workbook_set_file_exporter (wb, fs);
		if (fs != NULL)
			g_object_weak_ref (G_OBJECT (fs),
					   (GWeakNotify) cb_exporter_finalize, wb);
	} else {
		if (wb->file_saver != NULL)
			g_object_weak_unref (G_OBJECT (wb->file_saver),
					     (GWeakNotify) cb_saver_finalize, wb);

		wb->file_saver = fs;
		if (fs != NULL)
			g_object_weak_ref (G_OBJECT (fs),
					   (GWeakNotify) cb_saver_finalize, wb);
	}

	if (level != GO_FILE_FL_AUTO) {
		wb->file_export_format_level = level;
		return FALSE;
	}
	wb->file_format_level = level;
	return TRUE;
}

/**
 * workbook_get_file_saver:
 * @wb: #Workbook
 *
 * Returns: (transfer none): the saver for the Workbook.
 **/
GOFileSaver *
workbook_get_file_saver (Workbook *wb)
{
	g_return_val_if_fail (GNM_IS_WORKBOOK (wb), NULL);

	return wb->file_saver;
}

/**
 * workbook_get_file_exporter:
 * @wb: #Workbook
 *
 * Returns: (transfer none): the exporter for the Workbook.
 **/
GOFileSaver *
workbook_get_file_exporter (Workbook *wb)
{
	g_return_val_if_fail (GNM_IS_WORKBOOK (wb), NULL);

	return wb->file_exporter;
}

/**
 * workbook_get_last_export_uri:
 * @wb: #Workbook
 *
 * Returns: (transfer none): the URI for export.
 **/
gchar const *
workbook_get_last_export_uri (Workbook *wb)
{
	g_return_val_if_fail (GNM_IS_WORKBOOK (wb), NULL);

	return wb->last_export_uri;
}

void
workbook_set_file_exporter (Workbook *wb, GOFileSaver *fs)
{
	wb->file_exporter = fs;
	WORKBOOK_FOREACH_CONTROL (wb, wbv, wbc,
				  wb_control_menu_state_update (wbc, MS_FILE_EXPORT_IMPORT););
}

void
workbook_set_last_export_uri (Workbook *wb, const gchar *uri)
{
	char *s = g_strdup (uri);
	g_free (wb->last_export_uri);
	wb->last_export_uri = s;
	WORKBOOK_FOREACH_CONTROL (wb, wbv, wbc,
				  wb_control_menu_state_update (wbc, MS_FILE_EXPORT_IMPORT););
}


/**
 * workbook_foreach_cell_in_range:
 * @pos: The position the range is relative to.
 * @cell_range: A value containing a range;
 * @flags: flags determining which cells to consider
 * @handler: (scope call): The operator to apply to each cell.
 * @closure: User data.
 *
 * The supplied value must be a cellrange.
 * The range bounds are calculated relative to the eval position
 * and normalized.
 * For each existing cell in the range specified, invoke the
 * callback routine.  If the only_existing flag is %TRUE, then
 * callbacks are only invoked for existing cells.
 *
 * Note: this function does not honour the CELL_ITER_IGNORE_SUBTOTAL flag.
 *
 * Returns:
 *    non-%NULL on error, or VALUE_TERMINATE if some the handler requested
 *    to stop (by returning non-%NULL).
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
	g_return_val_if_fail (VALUE_IS_CELLRANGE (cell_range), NULL);

	gnm_rangeref_normalize (&cell_range->v_range.cell, pos,
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
				g_ptr_array_index (wb->sheets, i), flags, &r,
				handler, closure);
			if (res != NULL)
				return res;
		}
		return NULL;
	}

	return sheet_foreach_cell_in_range (start_sheet, flags, &r,
		handler, closure);
}

/**
 * workbook_cells:
 * @wb: The workbook to find cells in.
 * @comments: If true, include cells with only comments also.
 * @vis: How visible a sheet needs to be in order to be considered.
 *
 * Collects a GPtrArray of GnmEvalPos pointers for all cells in a workbook.
 * No particular order should be assumed.
 *
 * Returns: (element-type GnmEvalPos) (transfer container): the cells array
 */
GPtrArray *
workbook_cells (Workbook *wb, gboolean comments, GnmSheetVisibility vis)
{
	GPtrArray *cells = g_ptr_array_new ();

	g_return_val_if_fail (wb != NULL, cells);

	WORKBOOK_FOREACH_SHEET (wb, sheet, {
		size_t oldlen = cells->len;
		GPtrArray *scells;

		if (sheet->visibility > vis)
			continue;

		scells = sheet_cell_positions (sheet, comments);
		if (scells->len) {
			g_ptr_array_set_size (cells, oldlen + scells->len);
			memcpy (&g_ptr_array_index (cells, oldlen),
				&g_ptr_array_index (scells, 0),
				scells->len * sizeof (GnmEvalPos *));
		}
		g_ptr_array_free (scells, TRUE);
	});

	return cells;
}

GnmExprSharer *
workbook_share_expressions (Workbook *wb, gboolean freeit)
{
	GnmExprSharer *es = gnm_expr_sharer_new ();

	WORKBOOK_FOREACH_DEPENDENT (wb, dep, {
		if (dependent_is_cell (dep)) {
			/* Hopefully safe, even when linked.  */
			dep->texpr = gnm_expr_sharer_share (es, dep->texpr);
		} else {
			/* Not sure we want to touch this here.  */
		}
	});

	if (gnm_debug_flag ("expr-sharer")) {
		g_printerr ("Sharing report for %s\n", go_doc_get_uri (GO_DOC (wb)));
		gnm_expr_sharer_report (es);
	}

	if (freeit) {
		gnm_expr_sharer_unref (es);
		es = NULL;
	}

	return es;
}

void
workbook_optimize_style (Workbook *wb)
{
	WORKBOOK_FOREACH_SHEET (wb, sheet, {
		sheet_style_optimize (sheet);
	});
}

/**
 * workbook_foreach_name:
 * @wb: #Workbook
 * @globals_only: whether to apply only to global names.
 * @func: (scope call): The operator to apply to each cell.
 * @data: User data.
 *
 **/
void
workbook_foreach_name (Workbook const *wb, gboolean globals_only,
		       GHFunc func, gpointer data)
{
	g_return_if_fail (GNM_IS_WORKBOOK (wb));

	if (wb->names)
		gnm_named_expr_collection_foreach (wb->names, func, data);

	if (!globals_only) {
		WORKBOOK_FOREACH_SHEET (wb, sheet, {
				gnm_sheet_foreach_name (sheet, func, data);
		});
	}
}


gboolean
workbook_enable_recursive_dirty (Workbook *wb, gboolean enable)
{
	gboolean old;

	g_return_val_if_fail (GNM_IS_WORKBOOK (wb), FALSE);

	old = wb->recursive_dirty_enabled;
	wb->recursive_dirty_enabled = enable;
	return old;
}

void
workbook_set_recalcmode (Workbook *wb, gboolean is_auto)
{
	g_return_if_fail (GNM_IS_WORKBOOK (wb));

	is_auto = !!is_auto;
	if (is_auto == wb->recalc_auto)
		return;

	wb->recalc_auto = is_auto;
	g_object_notify (G_OBJECT (wb), "recalc-mode");
}

gboolean
workbook_get_recalcmode (Workbook const *wb)
{
	g_return_val_if_fail (GNM_IS_WORKBOOK (wb), FALSE);
	return wb->recalc_auto;
}

void
workbook_iteration_enabled (Workbook *wb, gboolean enable)
{
	g_return_if_fail (GNM_IS_WORKBOOK (wb));
	wb->iteration.enabled = enable;
}

void
workbook_iteration_max_number (Workbook *wb, int max_number)
{
	g_return_if_fail (GNM_IS_WORKBOOK (wb));
	g_return_if_fail (max_number >= 0);
	wb->iteration.max_number = max_number;
}

void
workbook_iteration_tolerance (Workbook *wb, gnm_float tolerance)
{
	g_return_if_fail (GNM_IS_WORKBOOK (wb));
	g_return_if_fail (tolerance >= 0);

	wb->iteration.tolerance = tolerance;
}

void
workbook_attach_view (WorkbookView *wbv)
{
	Workbook *wb;

	g_return_if_fail (GNM_IS_WORKBOOK_VIEW (wbv));

	wb = wb_view_get_workbook (wbv);
	g_return_if_fail (GNM_IS_WORKBOOK (wb));

	if (wb->wb_views == NULL)
		wb->wb_views = g_ptr_array_new ();
	g_ptr_array_add (wb->wb_views, wbv);
}

void
workbook_detach_view (WorkbookView *wbv)
{
	g_return_if_fail (GNM_IS_WORKBOOK_VIEW (wbv));
	g_return_if_fail (GNM_IS_WORKBOOK (wbv->wb));

	WORKBOOK_FOREACH_SHEET (wbv->wb, sheet, {
		SheetView *sv = sheet_get_view (sheet, wbv);
		gnm_sheet_view_dispose (sv);
	});

	g_ptr_array_remove (wbv->wb->wb_views, wbv);
	if (wbv->wb->wb_views->len == 0) {
		g_ptr_array_free (wbv->wb->wb_views, TRUE);
		wbv->wb->wb_views = NULL;
	}
}

/*****************************************************************************/

/**
 * workbook_sheets: (skip)
 * @wb: #Workbook
 *
 * Get an ordered list of the sheets in the workbook
 *
 * Returns: (element-type Sheet) (transfer container): the sheets list.
 */
GPtrArray *
workbook_sheets (Workbook const *wb)
{
	g_return_val_if_fail (GNM_IS_WORKBOOK (wb), NULL);
	return g_ptr_array_ref (wb->sheets);
}

// Alternate version for the sake of introspection which is unhappy with
// the GPtrArray api.

/**
 * gnm_workbook_sheets0: (rename-to workbook_sheets)
 * @wb: #Workbook
 *
 * Get an ordered list of the sheets in the workbook
 *
 * Returns: (element-type Sheet) (transfer full): the sheets list.
 */
GSList *
gnm_workbook_sheets0 (Workbook const *wb)
{
	GSList *res = NULL;
	unsigned ui;

	g_return_val_if_fail (GNM_IS_WORKBOOK (wb), NULL);

	for (ui = wb->sheets->len; ui--; ) {
		Sheet *sheet = g_ptr_array_index (wb->sheets, ui);
		res = g_slist_prepend (res, g_object_ref (sheet));
	}

	return g_slist_reverse (res);
}

int
workbook_sheet_count (Workbook const *wb)
{
	g_return_val_if_fail (GNM_IS_WORKBOOK (wb), 0);

	return wb->sheets ? wb->sheets->len : 0;
}

static void
pre_sheet_index_change (Workbook *wb)
{
	g_return_if_fail (!wb->being_reordered);

	wb->being_reordered = TRUE;

	if (wb->sheet_order_dependents != NULL)
		g_hash_table_foreach (wb->sheet_order_dependents,
				      (GHFunc)dependent_unlink,
				      NULL);
}

static void
post_sheet_index_change (Workbook *wb)
{
	g_return_if_fail (wb->being_reordered);

	if (wb->sheet_order_dependents != NULL)
		g_hash_table_foreach (wb->sheet_order_dependents,
				      (GHFunc)dependent_link,
				      NULL);

	wb->being_reordered = FALSE;

	if (wb->during_destruction)
		return;

	g_signal_emit (G_OBJECT (wb), signals[SHEET_ORDER_CHANGED], 0);
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

/**
 * workbook_sheet_by_index:
 * @wb: workbook to lookup the sheet on
 * @i: the sheet index we are looking for.
 *
 * Return value: (transfer none) (nullable): A #Sheet
 */
Sheet *
workbook_sheet_by_index (Workbook const *wb, int i)
{
	g_return_val_if_fail (GNM_IS_WORKBOOK (wb), NULL);
	g_return_val_if_fail (i >= -1, NULL);

	// i = -1 is special, return NULL
	if (i == -1 || i >= (int)wb->sheets->len)
		return NULL;

	return g_ptr_array_index (wb->sheets, i);
}

/**
 * workbook_sheet_by_name:
 * @wb: workbook to lookup the sheet on
 * @sheet_name: the sheet name we are looking for.  This is case insensitive.
 *
 * Return value: (transfer none) (nullable): A #Sheet
 */
Sheet *
workbook_sheet_by_name (Workbook const *wb, char const *name)
{
	Sheet *sheet;
	char *tmp;

	g_return_val_if_fail (GNM_IS_WORKBOOK (wb), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	tmp = g_utf8_casefold (name, -1);
	sheet = g_hash_table_lookup (wb->sheet_hash_private, tmp);
	g_free (tmp);

	return sheet;
}

/*
 * Find a sheet to focus on, left or right of sheet_index.
 */
static Sheet *
workbook_focus_other_sheet (Workbook *wb, Sheet *sheet)
{
	int i;
	Sheet *focus = NULL;
	int sheet_index = sheet->index_in_wb;

	for (i = sheet_index; !focus && --i >= 0; ) {
		Sheet *this_sheet = g_ptr_array_index (wb->sheets, i);
		if (this_sheet->visibility == GNM_SHEET_VISIBILITY_VISIBLE)
			focus = this_sheet;
	}

	for (i = sheet_index; !focus && ++i < (int)wb->sheets->len; ) {
		Sheet *this_sheet = g_ptr_array_index (wb->sheets, i);
		if (this_sheet->visibility == GNM_SHEET_VISIBILITY_VISIBLE)
			focus = this_sheet;
	}

	WORKBOOK_FOREACH_VIEW (wb, wbv, {
		if (sheet == wb_view_cur_sheet (wbv))
			wb_view_sheet_focus (wbv, focus);
	});

	return focus;
}

/**
 * workbook_sheet_remove_controls:
 * @wb: #Workbook
 * @sheet: #Sheet
 *
 * Remove the visible #SheetControls of a sheet and shut them down politely.
 *
 * Returns %TRUE if there are any remaining sheets visible
 **/
static gboolean
workbook_sheet_remove_controls (Workbook *wb, Sheet *sheet)
{
	Sheet *focus = NULL;

	g_return_val_if_fail (GNM_IS_WORKBOOK (wb), TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (sheet->workbook == wb, TRUE);
	g_return_val_if_fail (workbook_sheet_by_name (wb, sheet->name_unquoted) == sheet, TRUE);

	/* Finish any object editing */
	SHEET_FOREACH_CONTROL (sheet, view, control,
		sc_mode_edit (control););

	/* If not exiting, adjust the focus for any views whose focus sheet
	 * was the one being deleted, and prepare to recalc */
	if (!wb->during_destruction)
		focus = workbook_focus_other_sheet (wb, sheet);

	WORKBOOK_FOREACH_CONTROL (wb, wbv, wbc,
		wb_control_sheet_remove (wbc, sheet););

	return focus != NULL;
}

/**
 * workbook_sheet_attach_at_pos:
 * @wb: A #Workbook
 * @new_sheet: A #Sheet
 * @pos: position to attach @new_sheet at, -1 meaning at the end
 *
 * Add @new_sheet to @wb, placing it at @pos.
 */
void
workbook_sheet_attach_at_pos (Workbook *wb, Sheet *new_sheet, int pos)
{
	g_return_if_fail (GNM_IS_WORKBOOK (wb));
	g_return_if_fail (IS_SHEET (new_sheet));
	g_return_if_fail (new_sheet->workbook == wb);
	g_return_if_fail (pos >= -1 && pos <= (int)wb->sheets->len);

	if (pos == -1)
		pos = wb->sheets->len;

	pre_sheet_index_change (wb);

	g_object_ref (new_sheet);
	g_ptr_array_insert (wb->sheets, pos, (gpointer)new_sheet);
	workbook_sheet_index_update (wb, pos);
	g_hash_table_insert (wb->sheet_hash_private,
			     new_sheet->name_case_insensitive,
			     new_sheet);
	wb->sheet_size_cached = FALSE;

	WORKBOOK_FOREACH_VIEW (wb, view,
		wb_view_sheet_add (view, new_sheet););

	/* Do not signal until after adding the views [#314208] */
	post_sheet_index_change (wb);

	workbook_mark_dirty (wb);
}

/**
 * workbook_sheet_attach:
 * @wb: A #Workbook
 * @new_sheet: (transfer full): A #Sheet to attach
 *
 * Add @new_sheet to @wb, placing it at the end.  SURPRISE: This assumes
 * a ref to the sheet.
 */
void
workbook_sheet_attach (Workbook *wb, Sheet *new_sheet)
{
	workbook_sheet_attach_at_pos (wb, new_sheet, -1);
	/* Balance the ref added by the above call.  */
	g_object_unref (new_sheet);
}

/**
 * workbook_sheet_add:
 * @wb: a #Workbook.
 * @pos: position to add, -1 meaning at end.
 * @columns: the sheet columns number.
 * @rows: the sheet rows number.
 *
 * Create and name a new sheet, putting it at position @pos.  The sheet
 * returned is not ref'd.  (The ref belongs to the workbook.)
 *
 * Return value: (transfer none): the new sheet.
 */
Sheet *
workbook_sheet_add (Workbook *wb, int pos, int columns, int rows)
{
	char *name = workbook_sheet_get_free_name (wb, _("Sheet"), TRUE, FALSE);
	Sheet *new_sheet = sheet_new (wb, name, columns, rows);
	g_free (name);

	workbook_sheet_attach_at_pos (wb, new_sheet, pos);

	/* FIXME: Why here?  */
	g_signal_emit (G_OBJECT (wb), signals[SHEET_ADDED], 0);

	g_object_unref (new_sheet);

	return new_sheet;
}

/**
 * workbook_sheet_add_with_type:
 * @wb: a workbook.
 * @sheet_type: the sheet type.
 * @pos: position to add, -1 meaning append.
 * @columns: the sheet columns number.
 * @rows: the sheet rows number.
 *
 * Create and name a new sheet, putting it at position @pos.  The sheet
 * returned is not ref'd.  (The ref belongs to the workbook.)
 *
 * Return value: (transfer none): the new sheet.
 */
Sheet *
workbook_sheet_add_with_type (Workbook *wb, GnmSheetType sheet_type, int pos, int columns, int rows)
{
	char *name = workbook_sheet_get_free_name (wb, (sheet_type == GNM_SHEET_OBJECT)? _("Graph"): _("Sheet"), TRUE, FALSE);
	Sheet *new_sheet = sheet_new_with_type (wb, name, sheet_type, columns, rows);
	g_free (name);

	workbook_sheet_attach_at_pos (wb, new_sheet, pos);

	/* FIXME: Why here?  */
	g_signal_emit (G_OBJECT (wb), signals[SHEET_ADDED], 0);

	g_object_unref (new_sheet);

	return new_sheet;
}

/**
 * workbook_sheet_delete:
 * @sheet: the #Sheet that we want to delete from its workbook
 *
 * This function detaches the given sheet from its parent workbook and
 * invalidates all references to the deleted sheet from other sheets and
 * clears all references in the clipboard to this sheet.
 */
void
workbook_sheet_delete (Sheet *sheet)
{
	Workbook *wb;
	int sheet_index;

        g_return_if_fail (IS_SHEET (sheet));
        g_return_if_fail (GNM_IS_WORKBOOK (sheet->workbook));

	wb = sheet->workbook;
	sheet_index = sheet->index_in_wb;

	if (gnm_debug_flag ("sheets"))
		g_printerr ("Removing sheet %s from %s\n",
			    sheet->name_unquoted,
			    go_doc_get_uri (GO_DOC (wb)));

	gnm_app_clipboard_invalidate_sheet (sheet);

	if (!wb->during_destruction) {
		workbook_focus_other_sheet (wb, sheet);
		/* During destruction this was already done.  */
		dependents_invalidate_sheet (sheet, FALSE);
		workbook_sheet_remove_controls (wb, sheet);
	}

	/* All is fine, remove the sheet */
	pre_sheet_index_change (wb);
	g_ptr_array_remove_index (wb->sheets, sheet_index);
	wb->sheet_size_cached = FALSE;
	workbook_sheet_index_update (wb, sheet_index);
	sheet->index_in_wb = -1;
	g_hash_table_remove (wb->sheet_hash_private, sheet->name_case_insensitive);
	post_sheet_index_change (wb);

	/* Clear the controls first, before we potentially update */
	SHEET_FOREACH_VIEW (sheet, view, gnm_sheet_view_dispose (view););

	g_signal_emit_by_name (G_OBJECT (sheet), "detached_from_workbook", wb);
	g_object_unref (sheet);

	if (!wb->during_destruction)
		workbook_mark_dirty (wb);
	g_signal_emit (G_OBJECT (wb), signals[SHEET_DELETED], 0);

	if (!wb->during_destruction)
		workbook_queue_all_recalc (wb);
}

/**
 * workbook_sheet_move:
 * @sheet: #Sheet to move
 * @direction: number of spots to move, positive for right and negative
 * for left.
 *
 * Moves the sheet up or down @direction spots in the sheet list
 * If @direction is negative, move left. If positive, move right.
 */
void
workbook_sheet_move (Sheet *sheet, int direction)
{
	Workbook *wb;
	gint old_pos, new_pos;

	g_return_if_fail (IS_SHEET (sheet));

	wb = sheet->workbook;

	pre_sheet_index_change (wb);
        old_pos = sheet->index_in_wb;
	new_pos = old_pos + direction;

	if (0 <= new_pos && new_pos < workbook_sheet_count (wb)) {
		int min_pos = MIN (old_pos, new_pos);
		int max_pos = MAX (old_pos, new_pos);

		g_ptr_array_remove_index (wb->sheets, old_pos);
		g_ptr_array_insert (wb->sheets, new_pos, sheet);

		for (; max_pos >= min_pos ; max_pos--) {
			Sheet *sheet = g_ptr_array_index (wb->sheets, max_pos);
			sheet->index_in_wb = max_pos;
		}
	}

	post_sheet_index_change (wb);

	workbook_mark_dirty (wb);
}

/**
 * workbook_sheet_get_free_name:
 * @wb: #Workbook for which the new name can be used
 * @base: base for the name, e. g. "Sheet"
 * @always_suffix: if true, add suffix even if the name "base" is not in use.
 * @handle_counter: strip counter if necessary
 *
 * Gets a new unquoted name for a sheets such that it does not exist on the
 * workbook.
 *
 * Returns: (transfer full): a unique sheet name
 **/
char *
workbook_sheet_get_free_name (Workbook *wb,
			      char const *base,
			      gboolean always_suffix,
			      gboolean handle_counter)
{
	char const *name_format;
	char *name, *base_name;
	unsigned int i = 0;
	int limit;

	g_return_val_if_fail (wb != NULL, NULL);

	if (!always_suffix && (workbook_sheet_by_name (wb, base) == NULL))
		return g_strdup (base); /* Name not in use */

	base_name = g_strdup (base);
	if (handle_counter) {
		workbook_sheet_name_strip_number (base_name, &i);
		name_format = "%s(%u)";
	} else
		name_format = "%s%u";

	limit = workbook_sheet_count (wb) + 2;
	name = g_malloc (strlen (base_name) + strlen (name_format) + 10);
	while (limit-- > 0) {
		i++;
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
 * @wb: #Workbook in which to rename sheets
 * @sheet_indices: (element-type int): list of sheet indices (ignore -1)
 * @new_names: (element-type utf8): list of new names
 *
 * Adjusts the names of the sheets. We assume that everything is
 * valid. If in doubt call workbook_sheet_reorder_check first.
 *
 * Returns: %FALSE when it was successful
 **/
gboolean
workbook_sheet_rename (Workbook *wb,
		       GSList *sheet_indices,
		       GSList *new_names,
		       G_GNUC_UNUSED GOCmdContext *cc)
{
	GSList *sheet_index = sheet_indices;
	GSList *new_name = new_names;

	while (new_name && sheet_index) {
		int ix = GPOINTER_TO_INT (sheet_index->data);
		const char *name = new_name->data;
		if (ix != -1)
			g_hash_table_remove (wb->sheet_hash_private, name);
		sheet_index = sheet_index->next;
		new_name = new_name->next;
	}

	sheet_index = sheet_indices;
	new_name = new_names;
	while (new_name && sheet_index) {
		int ix = GPOINTER_TO_INT (sheet_index->data);
		const char *name = new_name->data;
		if (ix != -1) {
			Sheet *sheet = workbook_sheet_by_index (wb, ix);
			g_object_set (sheet, "name", name, NULL);
		}
		sheet_index = sheet_index->next;
		new_name = new_name->next;
	}

	return FALSE;
}

/**
 * workbook_find_command:
 * @wb: #Workbook
 * @is_undo: undo vs redo
 * @cmd: command
 *
 * Returns: the 1 based index of the @cmd command, or 0 if it is not found
 * (which would be a programmer error).
 **/
unsigned
workbook_find_command (Workbook *wb, gboolean is_undo, gpointer cmd)
{
	GSList *ptr;
	unsigned n = 1;

	g_return_val_if_fail (GNM_IS_WORKBOOK (wb), 0);

	ptr = is_undo ? wb->undo_commands : wb->redo_commands;
	for ( ; ptr != NULL ; ptr = ptr->next, n++)
		if (ptr->data == cmd)
			return n;
	g_warning ("%s command : %p not found", is_undo ? "undo" : "redo", cmd);
	return 0;
}

/**
 * workbook_sheet_reorder:
 * @wb: workbook to reorder
 * @new_order: (element-type Sheet): list of #Sheet
 *
 * Adjusts the order of the sheets.
 *
 * Returns %FALSE when it was successful
 **/
gboolean
workbook_sheet_reorder (Workbook *wb, GSList *new_order)
{
	GSList   *ptr;
	Sheet    *sheet;
	unsigned  pos = 0;

	g_return_val_if_fail (GNM_IS_WORKBOOK (wb), FALSE);
	g_return_val_if_fail (g_slist_length (new_order) == wb->sheets->len, FALSE);

	pre_sheet_index_change (wb);

	for (ptr = new_order; NULL != ptr ; ptr = ptr->next, pos++) {
		g_ptr_array_index (wb->sheets, pos) = sheet = ptr->data;
		sheet->index_in_wb = pos;
	}

	post_sheet_index_change (wb);

	return FALSE;
}

/**
 * workbook_date_conv:
 * @wb: Workbook
 *
 * Returns: (transfer none): the date conventions in effect for the workbook.
 **/
GODateConventions const *
workbook_date_conv (Workbook const *wb)
{
	g_return_val_if_fail (wb != NULL, NULL);
	return wb->date_conv;
}

/**
 * workbook_set_date_conv:
 * @wb: workbook
 * @date_conv: new date convention
 *
 * Sets the date convention @date_conv.
 * NOTE : THIS IS NOT A SMART ROUTINE.  If you want to actually change this
 * We'll need to recalc and rerender everything.  That will need to be done
 * externally.
 **/
void
workbook_set_date_conv (Workbook *wb, GODateConventions const *date_conv)
{
	g_return_if_fail (GNM_IS_WORKBOOK (wb));
	g_return_if_fail (date_conv != NULL);

	wb->date_conv = date_conv;
}

void
workbook_set_1904 (Workbook *wb, gboolean base1904)
{
	GODateConventions const *date_conv =
		go_date_conv_from_str (base1904 ? "Apple:1904" : "Lotus:1900");
	workbook_set_date_conv (wb, date_conv);
}

/**
 * workbook_get_sheet_size:
 * @wb: (nullable): #Workbook
 *
 * Returns: (transfer none): the current sheet size for @wb.  If sheets are
 * not of uniform size, this will be some size that is big enough in both
 * directions for all sheets.  That size isn't necessarily one that could
 * be used to create a new sheet.
 **/
GnmSheetSize const *
workbook_get_sheet_size (Workbook const *wb)
{
	static const GnmSheetSize max_size = {
		GNM_MAX_COLS, GNM_MAX_ROWS
	};
	int n = wb ? workbook_sheet_count (wb) : 0;

	if (n == 0)
		return &max_size;

	if (!wb->sheet_size_cached) {
		Workbook *wb1 = (Workbook *)wb;
		int i;

		wb1->sheet_size = *gnm_sheet_get_size (workbook_sheet_by_index (wb, 0));
		for (i = 1; i < n; i++) {
			Sheet *sheet = workbook_sheet_by_index (wb, i);
			GnmSheetSize const *ss = gnm_sheet_get_size (sheet);
			wb1->sheet_size.max_cols = MAX (wb->sheet_size.max_cols, ss->max_cols);
			wb1->sheet_size.max_rows = MAX (wb->sheet_size.max_rows, ss->max_rows);
		}

		wb1->sheet_size_cached = TRUE;
	}

	return &wb->sheet_size;
}

/* ------------------------------------------------------------------------- */

typedef struct {
	Sheet *sheet;
	GSList *properties;
} WorkbookSheetStateSheet;

struct _WorkbookSheetState {
	GSList *properties;
	int n_sheets;
	WorkbookSheetStateSheet *sheets;
	unsigned ref_count;
};


WorkbookSheetState *
workbook_sheet_state_new (const Workbook *wb)
{
	int i;
	WorkbookSheetState *wss = g_new (WorkbookSheetState, 1);

	wss->properties = go_object_properties_collect (G_OBJECT (wb));
	wss->n_sheets = workbook_sheet_count (wb);
	wss->sheets = g_new (WorkbookSheetStateSheet, wss->n_sheets);
	for (i = 0; i < wss->n_sheets; i++) {
		WorkbookSheetStateSheet *wsss = wss->sheets + i;
		wsss->sheet = g_object_ref (workbook_sheet_by_index (wb, i));
		wsss->properties = go_object_properties_collect (G_OBJECT (wsss->sheet));
	}
	wss->ref_count = 1;
	return wss;
}

void
workbook_sheet_state_unref (WorkbookSheetState *wss)
{
	int i;

	if (!wss || wss->ref_count-- > 1)
		return;

	go_object_properties_free (wss->properties);

	for (i = 0; i < wss->n_sheets; i++) {
		WorkbookSheetStateSheet *wsss = wss->sheets + i;
		g_object_unref (wsss->sheet);
		go_object_properties_free (wsss->properties);
	}
	g_free (wss->sheets);
	g_free (wss);
}

static WorkbookSheetState *
workbook_sheet_state_ref (WorkbookSheetState *wss)
{
	wss->ref_count++;
	return wss;
}

GType
workbook_sheet_state_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("WorkbookSheetState",
			 (GBoxedCopyFunc)workbook_sheet_state_ref,
			 (GBoxedFreeFunc)workbook_sheet_state_unref);
	}
	return t;
}

void
workbook_sheet_state_restore (Workbook *wb, const WorkbookSheetState *wss)
{
	int i;

	/* Get rid of sheets that shouldn't be there.  */
	for (i = workbook_sheet_count (wb) ; i-- > 0; ) {
		Sheet *sheet = workbook_sheet_by_index (wb, i);
		int j;
		for (j = 0; j < wss->n_sheets; j++)
			if (sheet == wss->sheets[j].sheet)
				break;
		if (j == wss->n_sheets)
			workbook_sheet_delete (sheet);
	}

	/* Attach new sheets and handle order.  */
	for (i = 0; i < wss->n_sheets; i++) {
		Sheet *sheet = wss->sheets[i].sheet;
		if (sheet->index_in_wb != i) {
			if (sheet->index_in_wb == -1) {
				workbook_sheet_attach_at_pos (wb, sheet, i);
				dependents_revive_sheet (sheet);
			} else {
				/*
				 * There might be a smarter way of getting more
				 * sheets into place faster.  This will at
				 * least work.
				 */
				workbook_sheet_move (sheet, i - sheet->index_in_wb);
			}
		}
		go_object_properties_apply (G_OBJECT (sheet),
					    wss->sheets[i].properties,
					    TRUE);
	}

	go_object_properties_apply (G_OBJECT (wb), wss->properties, TRUE);
}

int
workbook_sheet_state_size (const WorkbookSheetState *wss)
{
	int size = 1 + g_slist_length (wss->properties);
	int i;
	for (i = 0; i < wss->n_sheets; i++) {
		WorkbookSheetStateSheet *wsss = wss->sheets + i;
		size += 5;  /* For ->sheet.  */
		size += g_slist_length (wsss->properties);
	}
	return size;
}

GNM_BEGIN_KILL_SWITCH_WARNING
char *
workbook_sheet_state_diff (const WorkbookSheetState *wss_a, const WorkbookSheetState *wss_b)
{
	enum {
		WSS_SHEET_RENAMED = 1,
		WSS_SHEET_ADDED = 2,
		WSS_SHEET_TAB_COLOR = 4,
		WSS_SHEET_PROPERTIES = 8,
		WSS_SHEET_DELETED = 16,
		WSS_SHEET_ORDER = 32,
		WSS_FUNNY = 0x40000000
	} what = 0;
	int ia;
	int n = 0;
	int n_added, n_deleted = 0;

	for (ia = 0; ia < wss_a->n_sheets; ia++) {
		Sheet *sheet = wss_a->sheets[ia].sheet;
		int ib;
		GSList *pa, *pb;
		int diff = 0;

		for (ib = 0; ib < wss_b->n_sheets; ib++)
			if (sheet == wss_b->sheets[ib].sheet)
				break;
		if (ib == wss_b->n_sheets) {
			what |= WSS_SHEET_DELETED;
			n++;
			n_deleted++;
			continue;
		}

		if (ia != ib) {
			what |= WSS_SHEET_ORDER;
			/* We do not count reordered sheet.  */
		}

		pa = wss_a->sheets[ia].properties;
		pb = wss_b->sheets[ib].properties;
		for (; pa && pb; pa = pa->next->next, pb = pb->next->next) {
			GParamSpec *pspec = pa->data;
			const GValue *va = pa->next->data;
			const GValue *vb = pb->next->data;
			if (pspec != pb->data)
				break;

			if (g_param_values_cmp (pspec, va, vb) == 0)
				continue;

			diff = 1;
			if (strcmp (pspec->name, "name") == 0)
				what |= WSS_SHEET_RENAMED;
			else if (strcmp (pspec->name, "tab-foreground") == 0)
				what |= WSS_SHEET_TAB_COLOR;
			else if (strcmp (pspec->name, "tab-background") == 0)
				what |= WSS_SHEET_TAB_COLOR;
			else
				what |= WSS_SHEET_PROPERTIES;
		}

		if (pa || pb)
			what |= WSS_FUNNY;
		n += diff;
	}

	n_added = wss_b->n_sheets - (wss_a->n_sheets - n_deleted);
	if (n_added) {
		what |= WSS_SHEET_ADDED;
		n += n_added;
	}

	switch (what) {
	case WSS_SHEET_RENAMED:
		return g_strdup_printf (ngettext ("Renaming sheet", "Renaming %d sheets", n), n);
	case WSS_SHEET_ADDED:
		return g_strdup_printf (ngettext ("Adding sheet", "Adding %d sheets", n), n);
	case WSS_SHEET_ADDED | WSS_SHEET_ORDER:
		/*
		 * This is most likely just a sheet inserted, but it just
		 * might be a compound operation.  Lie.
		 */
		return g_strdup_printf (ngettext ("Inserting sheet", "Inserting %d sheets", n), n);
	case WSS_SHEET_TAB_COLOR:
		return g_strdup (_("Changing sheet tab colors"));
	case WSS_SHEET_PROPERTIES:
		return g_strdup (_("Changing sheet properties"));
	case WSS_SHEET_DELETED:
	case WSS_SHEET_DELETED | WSS_SHEET_ORDER:
		/*
		 * This is most likely just a sheet delete, but it just
		 * might be a compound operation.  Lie.
		 */
		return g_strdup_printf (ngettext ("Deleting sheet", "Deleting %d sheets", n), n);
	case WSS_SHEET_ORDER:
		return g_strdup (_("Changing sheet order"));
	default:
		return g_strdup (_("Reorganizing Sheets"));
	}
}
GNM_END_KILL_SWITCH_WARNING

/* ------------------------------------------------------------------------- */

GSF_CLASS (Workbook, workbook,
	   workbook_class_init, workbook_init,
	   GO_TYPE_DOC)
