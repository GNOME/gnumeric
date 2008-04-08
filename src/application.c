/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * application.c: Manage the data common to all workbooks
 *
 * Author:
 *     Jody Goldberg <jody@gnome.org>
 */
#include <gnumeric-config.h>
#include <string.h>
#include "gnumeric.h"
#include "application.h"

#include "clipboard.h"
#include "selection.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-private.h"
#include "sheet-object.h"
#include "auto-correct.h"
#include "gutils.h"
#include "ranges.h"
#include "sheet-object.h"
#include "pixmaps/gnumeric-stock-pixbufs.h"
#include "commands.h"

#include <gnumeric-gconf.h>
#include <goffice/app/go-doc.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-file.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtkfilefilter.h>
#include <gtk/gtkrecentmanager.h>

#define GNM_APP(o)		(G_TYPE_CHECK_INSTANCE_CAST((o), GNM_APP_TYPE, GnmApp))
#define GNM_APP_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k),	 GNM_APP_TYPE, GnmAppClass))
#define IS_GNM_APP(o)		(G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_APP_TYPE))
#define IS_GNM_APP_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE((k),	 GNM_APP_TYPE))

enum {
	APPLICATION_PROP_0,
	APPLICATION_PROP_FILE_HISTORY_LIST
};
/* Signals */
enum {
	WORKBOOK_ADDED,
	WORKBOOK_REMOVED,
	WINDOW_LIST_CHANGED,
	CUSTOM_UI_ADDED,
	CUSTOM_UI_REMOVED,
	CLIPBOARD_MODIFIED,
	RECALC_FINISHED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

struct _GnmApp {
	GObject  base;

	/* Clipboard */
	SheetView	*clipboard_sheet_view;
	GnmCellRegion	*clipboard_copied_contents;
	GnmRange	*clipboard_cut_range;

	GList		*workbook_list;

#ifdef HAVE_GTK_RECENT_MANAGER_GET_DEFAULT
	GtkRecentManager *recent;
	gulong           recent_sig;
#endif	
};

typedef struct {
	GObjectClass     parent;

	void (*workbook_added)      (GnmApp *gnm_app, Workbook *wb);
	void (*workbook_removed)    (GnmApp *gnm_app, Workbook *wb);
	void (*window_list_changed) (GnmApp *gnm_app);
	void (*custom_ui_added)	    (GnmApp *gnm_app, GnmAppExtraUI *ui);
	void (*custom_ui_removed)   (GnmApp *gnm_app, GnmAppExtraUI *ui);
	void (*clipboard_modified)  (GnmApp *gnm_app);
	void (*recalc_finished)     (GnmApp *gnm_app);
} GnmAppClass;

static GObjectClass *parent_klass;
static GnmApp *app;

GObject *
gnm_app_get_app (void)
{
	return G_OBJECT (app);
}

/**
 * gnm_app_workbook_list_add :
 * @wb :
 *
 * Add @wb to the application's list of workbooks.
 **/
void
gnm_app_workbook_list_add (Workbook *wb)
{
	g_return_if_fail (IS_WORKBOOK (wb));
	g_return_if_fail (app != NULL);

	app->workbook_list = g_list_prepend (app->workbook_list, wb);
	g_signal_connect (G_OBJECT (wb),
		"notify::uri",
		G_CALLBACK (_gnm_app_flag_windows_changed), NULL);
	_gnm_app_flag_windows_changed ();
	g_signal_emit (G_OBJECT (app), signals [WORKBOOK_ADDED], 0, wb);
}

/**
 * gnm_app_workbook_list_remove :
 * @wb :
 *
 * Remove @wb from the application's list of workbooks.
 **/
void
gnm_app_workbook_list_remove (Workbook *wb)
{
	g_return_if_fail (wb != NULL);
	g_return_if_fail (app != NULL);

	app->workbook_list = g_list_remove (app->workbook_list, wb);
	g_signal_handlers_disconnect_by_func (G_OBJECT (wb),
		G_CALLBACK (_gnm_app_flag_windows_changed), NULL);
	_gnm_app_flag_windows_changed ();
	g_signal_emit (G_OBJECT (app), signals [WORKBOOK_REMOVED], 0, wb);
}

GList *
gnm_app_workbook_list (void)
{
	g_return_val_if_fail (app != NULL, NULL);

	return app->workbook_list;
}

/**
 * gnm_app_clipboard_clear:
 *
 * Clear and free the contents of the clipboard if it is
 * not empty.
 */
void
gnm_app_clipboard_clear (gboolean drop_selection)
{
	g_return_if_fail (app != NULL);

	if (app->clipboard_copied_contents) {
		cellregion_unref (app->clipboard_copied_contents);
		app->clipboard_copied_contents = NULL;
	}
	if (app->clipboard_sheet_view != NULL) {
		sv_unant (app->clipboard_sheet_view);

		g_signal_emit (G_OBJECT (app), signals [CLIPBOARD_MODIFIED], 0);

		sv_weak_unref (&(app->clipboard_sheet_view));

		/* Release the selection */
		if (drop_selection) {
#warning "FIXME: 1: this doesn't belong here.  2: it is not multihead safe."
			gtk_selection_owner_set (NULL,
						 GDK_SELECTION_PRIMARY,
						 GDK_CURRENT_TIME);
			gtk_selection_owner_set (NULL,
						 GDK_SELECTION_CLIPBOARD,
						 GDK_CURRENT_TIME);
		}
	}
}

void
gnm_app_clipboard_invalidate_sheet (Sheet *sheet)
{
	/* Clear the cliboard to avoid dangling references to the deleted sheet */
	if (sheet == gnm_app_clipboard_sheet_get ())
		gnm_app_clipboard_clear (TRUE);
	else if (app->clipboard_copied_contents)
		cellregion_invalidate_sheet (app->clipboard_copied_contents, sheet);
}

void
gnm_app_clipboard_unant (void)
{
	g_return_if_fail (app != NULL);

	if (app->clipboard_sheet_view != NULL)
		sv_unant (app->clipboard_sheet_view);
}

/**
 * gnm_app_clipboard_cut_copy:
 *
 * @wbc   : the workbook control that requested the operation.
 * @is_cut: is this a cut or a copy.
 * @sheet : The source sheet for the copy.
 * @area  : A single rectangular range to be copied.
 * @animate_cursor : Do we want ot add an animated cursor around things.
 *
 * When Cutting we
 *   Clear and free the contents of the clipboard and save the sheet and area
 *   to be cut.  DO NOT ACTUALLY CUT!  Paste will move the region if this was a
 *   cut operation.
 *
 * When Copying we
 *   Clear and free the contents of the clipboard and COPY the designated region
 *   into the clipboard.
 *
 * we need to pass @wbc as a control rather than a simple command-context so
 * that the control can claim the selection.
 **/
void
gnm_app_clipboard_cut_copy (WorkbookControl *wbc, gboolean is_cut,
			    SheetView *sv, GnmRange const *area,
			    gboolean animate_cursor)
{
	Sheet *sheet;

	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (area != NULL);
	g_return_if_fail (app != NULL);

	gnm_app_clipboard_clear (FALSE);
	sheet = sv_sheet (sv);
	g_free (app->clipboard_cut_range);
	app->clipboard_cut_range = range_dup (area);
	sv_weak_ref (sv, &(app->clipboard_sheet_view));

	if (!is_cut)
		app->clipboard_copied_contents =
			clipboard_copy_range (sheet, area);
	if (animate_cursor) {
		GList *l = g_list_append (NULL, (gpointer)area);
		sv_ant (sv, l);
		g_list_free (l);
	}

	if (wb_control_claim_selection (wbc)) {
		g_signal_emit (G_OBJECT (app), signals [CLIPBOARD_MODIFIED], 0);
	} else {
		gnm_app_clipboard_clear (FALSE);
		g_warning ("Unable to set selection ?");
	}
}

/** gnm_app_clipboard_cut_copy_obj :
 * @wbc : #WorkbookControl
 * @is_cut :
 * @sv : #SheetView
 * @objects : a list of #SheetObject which is freed
 *
 * Different than copying/cutting a region, this can actually cuts an object
 **/
void
gnm_app_clipboard_cut_copy_obj (WorkbookControl *wbc, gboolean is_cut,
				SheetView *sv, GSList *objects)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (objects != NULL);
	g_return_if_fail (app != NULL);

	gnm_app_clipboard_clear (FALSE);
	g_free (app->clipboard_cut_range);
	app->clipboard_cut_range = NULL;
	sv_weak_ref (sv, &(app->clipboard_sheet_view));
	app->clipboard_copied_contents
		= clipboard_copy_obj (sv_sheet (sv), objects);
	if (is_cut) {
		cmd_objects_delete (wbc, objects, _("Cut Object"));
		objects = NULL;
	}
	if (wb_control_claim_selection (wbc)) {
		g_signal_emit (G_OBJECT (app), signals [CLIPBOARD_MODIFIED], 0);
	} else {
		gnm_app_clipboard_clear (FALSE);
		g_warning ("Unable to set selection ?");
	}
	g_slist_free (objects);
}

gboolean
gnm_app_clipboard_is_empty (void)
{
	g_return_val_if_fail (app != NULL, TRUE);

	return app->clipboard_sheet_view == NULL;
}

gboolean
gnm_app_clipboard_is_cut (void)
{
	g_return_val_if_fail (app != NULL, FALSE);

	if (app->clipboard_sheet_view != NULL)
		return app->clipboard_copied_contents ? FALSE : TRUE;
	return FALSE;
}

Sheet *
gnm_app_clipboard_sheet_get (void)
{
	g_return_val_if_fail (app != NULL, NULL);

	if (app->clipboard_sheet_view == NULL)
		return NULL;
	return sv_sheet (app->clipboard_sheet_view);
}

SheetView *
gnm_app_clipboard_sheet_view_get (void)
{
	g_return_val_if_fail (app != NULL, NULL);
	return app->clipboard_sheet_view;
}

GnmCellRegion *
gnm_app_clipboard_contents_get (void)
{
	g_return_val_if_fail (app != NULL, NULL);
	return app->clipboard_copied_contents;
}

GnmRange const *
gnm_app_clipboard_area_get (void)
{
	g_return_val_if_fail (app != NULL, NULL);
	/*
	 * Only return the range if the sheet has been set.
	 * The range will still contain data even after
	 * the clipboard has been cleared so we need to be extra
	 * safe and only return a range if there is a valid selection
	 */
	if (app->clipboard_sheet_view != NULL)
		return app->clipboard_cut_range;
	return NULL;
}

Workbook *
gnm_app_workbook_get_by_name (char const *name,
			      char const *ref_uri)
{
	Workbook *wb;
	char *filename = NULL;

	if (name == NULL || *name == 0)
		return NULL;

	/* Try as URI.  */
	wb = gnm_app_workbook_get_by_uri (name);
	if (wb)
		goto out;

	filename = g_filename_from_utf8 (name, -1, NULL, NULL, NULL);

	/* Try as absolute filename.  */
	if (filename && g_path_is_absolute (filename)) {
		char *uri = go_filename_to_uri (filename);
		if (uri) {
			wb = gnm_app_workbook_get_by_uri (uri);
			g_free (uri);
		}
		if (wb)
			goto out;
	}

	if (filename && ref_uri) {
		char *rel_uri = go_url_encode (filename, 1);
		char *uri = go_url_resolve_relative (ref_uri, rel_uri);
		g_free (rel_uri);
		if (uri) {
			wb = gnm_app_workbook_get_by_uri (uri);
			g_free (uri);
		}
		if (wb)
			goto out;
	}

 out:
	g_free (filename);
	return wb;
}

struct wb_uri_closure {
	Workbook *wb;
	char const *uri;
};

static gboolean
cb_workbook_uri (Workbook * wb, gpointer closure)
{
	struct wb_uri_closure *dat = closure;
	char const *wb_uri = go_doc_get_uri (GO_DOC (wb));

	if (wb_uri && strcmp (wb_uri, dat->uri) == 0) {
		dat->wb = wb;
		return FALSE;
	}
	return TRUE;
}

Workbook *
gnm_app_workbook_get_by_uri (char const *uri)
{
	struct wb_uri_closure closure;

	g_return_val_if_fail (uri != NULL, NULL);

	closure.wb = NULL;
	closure.uri = uri;
	gnm_app_workbook_foreach (&cb_workbook_uri, &closure);

	return closure.wb;
}

gboolean
gnm_app_workbook_foreach (GnmWbIterFunc cback, gpointer data)
{
	GList *l;

	g_return_val_if_fail (app != NULL, FALSE);

	for (l = app->workbook_list; l; l = l->next){
		Workbook *wb = l->data;

		if (!(*cback)(wb, data))
			return FALSE;
	}
	return TRUE;
}

struct wb_index_closure
{
	Workbook *wb;
	int index;	/* 1 based */
};

static gboolean
cb_workbook_index (Workbook *wb, gpointer closure)
{
	struct wb_index_closure *dat = closure;
	if (--(dat->index) == 0) {
		dat->wb = wb;
		return FALSE;
	}
	return TRUE;
}

Workbook *
gnm_app_workbook_get_by_index (int i)
{
	struct wb_index_closure closure;
	closure.wb = NULL;
	closure.index = i;
	gnm_app_workbook_foreach (&cb_workbook_index, &closure);

	return closure.wb;
}

double
gnm_app_display_dpi_get (gboolean horizontal)
{
	return horizontal ? gnm_app_prefs->horizontal_dpi : gnm_app_prefs->vertical_dpi;
}

double
gnm_app_dpi_to_pixels (void)
{
	return MIN (gnm_app_prefs->horizontal_dpi,
		    gnm_app_prefs->vertical_dpi) / 72.;
}

/* GtkFileFilter */
void *
gnm_app_create_opener_filter (void)
{
	/* See below.  */
	static const char *const bad_suffixes[] = {
		"txt",
		"html", "htm",
		"xml",
		NULL
	};

	GtkFileFilter *filter = gtk_file_filter_new ();

	GList *openers;

	for (openers = go_get_file_openers ();
	     openers;
	     openers = openers->next) {
		GOFileOpener *opener = openers->data;
		const GSList *mimes = go_file_opener_get_mimes (opener);
		const GSList *suffixes = go_file_opener_get_suffixes (opener);

		while (mimes) {
#if 0
			const char *mime = mimes->data;
			/*
			 * This needs rethink, see 438918.  Too many things
			 * like *.xml and *.txt get added.
			 */
			gtk_file_filter_add_mime_type (filter, mime);
			if (0)
				g_print ("%s: Adding mime %s\n", go_file_opener_get_description (opener), mime);
#endif
			mimes = mimes->next;
		}

		while (suffixes) {
			const char *suffix = suffixes->data;
			char *pattern;
			int i;

			for (i = 0; bad_suffixes[i]; i++)
				if (strcmp (suffix, bad_suffixes[i]) == 0)
					goto bad_suffix;

			pattern = g_strconcat ("*.", suffix, NULL);
			gtk_file_filter_add_pattern (filter, pattern);
			if (0)
				g_print ("%s: Adding %s\n", go_file_opener_get_description (opener), pattern);
			g_free (pattern);

		bad_suffix:
			suffixes = suffixes->next;
		}
	}
	return filter;
}

#ifdef HAVE_GTK_RECENT_MANAGER_GET_DEFAULT
static gint
compare_mru (GtkRecentInfo *a, GtkRecentInfo *b)
{
	time_t ta = MAX (gtk_recent_info_get_visited (a), gtk_recent_info_get_modified (a));
	time_t tb = MAX (gtk_recent_info_get_visited (b), gtk_recent_info_get_modified (b));

	return ta < tb;
}
#endif

/**
 * gnm_app_history_get_list:
 *
 * creating it if necessary.
 *
 * Return value: the list, which must be freed along with the strings in it.
 **/
GSList *
gnm_app_history_get_list (int max_elements)
{
#ifdef HAVE_GTK_RECENT_MANAGER_GET_DEFAULT
	GSList *res = NULL;
	GList *items, *l;
	GtkFileFilter *filter = gnm_app_create_opener_filter ();
	int n_elements = 0;

	items = gtk_recent_manager_get_items (app->recent);
	items = g_list_sort (items, (GCompareFunc)compare_mru);

	for (l = items; l && n_elements < max_elements; l = l->next) {
		GtkRecentInfo *ri = l->data;
		const char *uri = gtk_recent_info_get_uri (ri);
		gboolean want_it;

		if (gtk_recent_info_has_application (ri, g_get_application_name ())) {
			want_it = TRUE;
		} else {
			GtkFileFilterInfo fi;
			char *display_name = g_filename_display_basename (uri);

			memset (&fi, 0, sizeof (fi));
			fi.contains = (GTK_FILE_FILTER_MIME_TYPE |
				       GTK_FILE_FILTER_URI |
				       GTK_FILE_FILTER_DISPLAY_NAME);
			fi.uri = uri;
			fi.mime_type = gtk_recent_info_get_mime_type (ri);
			fi.display_name = display_name;
			want_it = gtk_file_filter_filter (filter, &fi);
			g_free (display_name);
		}

		if (want_it) {
			char *filename = go_filename_from_uri (uri);
			if (filename && !g_file_test (filename, G_FILE_TEST_EXISTS))
				want_it = FALSE;
			g_free (filename);
		}

		if (want_it) {
			res = g_slist_prepend (res, g_strdup (uri));
			n_elements++;
		}
	}

	go_list_free_custom (items, (GFreeFunc)gtk_recent_info_unref);
	g_object_ref_sink (filter);
	g_object_unref (filter);

	return g_slist_reverse (res);
#else
	return NULL;
#endif
}

/**
 * application_history_update_list:
 * @uri:
 *
 * Adds @uri to the application's history of files.
 **/
void
gnm_app_history_add (char const *uri, const char *mimetype)
{
#ifdef HAVE_GTK_RECENT_MANAGER_GET_DEFAULT
	GtkRecentData rd;
	gboolean retval;

	memset (&rd, 0, sizeof (rd));

#if 0
	g_print ("uri: %s\nmime: %s\n\n", uri, mimetype ? mimetype : "-");
#endif

        rd.mime_type =
		g_strdup (mimetype ? mimetype : "application/octet-stream");

	rd.app_name = g_strdup (g_get_application_name ());
	rd.app_exec = g_strjoin (" ", g_get_prgname (), "%u", NULL);
	rd.groups = NULL;
	rd.is_private = FALSE;

	retval = gtk_recent_manager_add_full (app->recent, uri, &rd);

	g_free (rd.mime_type);
	g_free (rd.app_name);
	g_free (rd.app_exec);
#endif

	g_object_notify (G_OBJECT (app), "file-history-list");
}

int	 gnm_app_enter_moves_dir	(void) { return gnm_app_prefs->enter_moves_dir; }
gboolean gnm_app_use_auto_complete	(void) { return gnm_app_prefs->auto_complete; }
gboolean gnm_app_live_scrolling		(void) { return gnm_app_prefs->live_scrolling; }
int	 gnm_app_auto_expr_recalc_lag	(void) { return gnm_app_prefs->recalc_lag; }
gboolean gnm_app_use_transition_keys	(void) { return gnm_app_prefs->transition_keys; }
void     gnm_app_set_transition_keys	(gboolean state)
{
	((GnmAppPrefs *)gnm_app_prefs)->transition_keys = state;
}

static void
cb_recent_changed (G_GNUC_UNUSED GtkRecentManager *recent, GnmApp *app)
{
	g_object_notify (G_OBJECT (app), "file-history-list");
}

static void
gnumeric_application_finalize (GObject *obj)
{
	GnmApp *application = GNM_APP (obj);

	g_free (application->clipboard_cut_range);
	application->clipboard_cut_range = NULL;

#ifdef HAVE_GTK_RECENT_MANAGER_GET_DEFAULT
	application->recent = NULL;
#endif

	if (app == application)
		app = NULL;

	G_OBJECT_CLASS (parent_klass)->finalize (obj);
}

static void
gnumeric_application_get_property (GObject *obj, guint param_id,
				   GValue *value, GParamSpec *pspec)
{
#if 0
	GnmApp *application = GNM_APP (obj);
#endif
	switch (param_id) {
	case APPLICATION_PROP_FILE_HISTORY_LIST:
		g_value_set_pointer (value, gnm_app_history_get_list (G_MAXINT));
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static void
gnm_app_class_init (GObjectClass *gobject_klass)
{
	parent_klass = g_type_class_peek_parent (gobject_klass);

	/* Object class method overrides */
	gobject_klass->finalize = gnumeric_application_finalize;
	gobject_klass->get_property = gnumeric_application_get_property;
	g_object_class_install_property (gobject_klass, APPLICATION_PROP_FILE_HISTORY_LIST,
		g_param_spec_pointer ("file-history-list", _("File History List"),
				      _("A list of filenames that have been read recently"),
				      GSF_PARAM_STATIC | G_PARAM_READABLE));

	signals [WORKBOOK_ADDED] = g_signal_new ("workbook_added",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, workbook_added),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1, WORKBOOK_TYPE);
	signals [WORKBOOK_REMOVED] = g_signal_new ("workbook_removed",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, workbook_removed),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [WINDOW_LIST_CHANGED] = g_signal_new ("window-list-changed",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, window_list_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
	signals [CUSTOM_UI_ADDED] = g_signal_new ("custom-ui-added",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, custom_ui_added),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [CUSTOM_UI_REMOVED] = g_signal_new ("custom-ui-removed",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, custom_ui_removed),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [CLIPBOARD_MODIFIED] = g_signal_new ("clipboard_modified",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, clipboard_modified),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
	signals [RECALC_FINISHED] = g_signal_new ("recalc_finished",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, recalc_finished),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
}

static void
gnm_app_init (GObject *obj)
{
	GnmApp *gnm_app = GNM_APP (obj);

	gnm_app->clipboard_copied_contents = NULL;
	gnm_app->clipboard_sheet_view = NULL;

	gnm_app->workbook_list = NULL;

#ifdef HAVE_GTK_RECENT_MANAGER_GET_DEFAULT
	gnm_app->recent = gtk_recent_manager_get_default ();
	g_signal_connect_object (G_OBJECT (gnm_app->recent),
				 "changed", G_CALLBACK (cb_recent_changed),
				 gnm_app, 0);
#endif

	app = gnm_app;
}

GSF_CLASS (GnmApp, gnm_app,
	   gnm_app_class_init, gnm_app_init,
	   G_TYPE_OBJECT);

/**********************************************************************/
static GSList *extra_uis = NULL;

GnmAction *
gnm_action_new (char const *id, char const *label,
		char const *icon_name, gboolean always_available,
		GnmActionHandler handler)
{
	GnmAction *res = g_new0 (GnmAction, 1);
	res->id		= g_strdup (id);
	res->label	= g_strdup (label);
	res->icon_name	= g_strdup (icon_name);
	res->always_available = always_available;
	res->handler	= handler;
	return res;
}

void
gnm_action_free (GnmAction *action)
{
	if (NULL != action) {
		g_free (action->id);
		g_free (action->label);
		g_free (action->icon_name);
		g_free (action);
	}
}

GnmAppExtraUI *
gnm_app_add_extra_ui (GSList *actions, char *layout,
		      char const *domain,
		      gpointer user_data)
{
	GnmAppExtraUI *extra_ui = g_new0 (GnmAppExtraUI, 1);
	extra_uis = g_slist_prepend (extra_uis, extra_ui);
	extra_ui->actions = actions;
	extra_ui->layout = layout;
	extra_ui->user_data = user_data;
	g_signal_emit (G_OBJECT (app), signals [CUSTOM_UI_ADDED], 0, extra_ui);
	return extra_ui;
}

void
gnm_app_remove_extra_ui (GnmAppExtraUI *extra_ui)
{
	g_signal_emit (G_OBJECT (app), signals [CUSTOM_UI_REMOVED], 0, extra_ui);
}

void
gnm_app_foreach_extra_ui (GFunc func, gpointer data)
{
	g_slist_foreach (extra_uis, func, data);
}

/**********************************************************************/

static gint windows_update_timer = -1;
static gboolean
cb_flag_windows_changed (void)
{
	g_signal_emit (G_OBJECT (app), signals [WINDOW_LIST_CHANGED], 0);
	windows_update_timer = -1;
	return FALSE;
}

/**
 * _gnm_app_flag_windows_changed :
 *
 * An internal utility routine to flag a regeneration of the window lists
 **/
void
_gnm_app_flag_windows_changed (void)
{
	if (windows_update_timer < 0)
		windows_update_timer = g_timeout_add (100,
			(GSourceFunc)cb_flag_windows_changed, NULL);
}
