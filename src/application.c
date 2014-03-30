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
#include "commands.h"
#include "gui-clipboard.h"
#include "expr-name.h"
#include "workbook-priv.h"

#include <gnumeric-conf.h>
#include <goffice/goffice.h>
#include <gsf/gsf-impl-utils.h>
#include "gnm-i18n.h"
#include <gtk/gtk.h>

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
	RECALC_CLEAR_CACHES,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

struct _GnmApp {
	GObject  base;

	/* Clipboard */
	SheetView	*clipboard_sheet_view;
	GnmCellRegion	*clipboard_copied_contents;
	GnmRange	*clipboard_cut_range;

	GList		*workbook_list;

	/* Recalculation manager.  */
	int             recalc_count;

	GtkRecentManager *recent;
	gulong           recent_sig;
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
	void (*recalc_clear_caches) (GnmApp *gnm_app);
} GnmAppClass;

static GObjectClass *parent_klass;
static GnmApp *app;

static Workbook *gnm_app_workbook_get_by_uri (char const *uri);

/**
 * gnm_app_get_app:
 *
 * Returns: (transfer none): the #GnmApp instance.
 **/
GObject *
gnm_app_get_app (void)
{
	return G_OBJECT (app);
}

/**
 * gnm_app_workbook_list_add:
 * @wb:
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
	g_signal_emit (G_OBJECT (app), signals[WORKBOOK_ADDED], 0, wb);
}

/**
 * gnm_app_workbook_list_remove:
 * @wb:
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
	g_signal_emit (G_OBJECT (app), signals[WORKBOOK_REMOVED], 0, wb);
}

/**
 * gnm_app_workbook_list:
 *
 * Returns: (element-type Workbook) (transfer none): the workbook list.
 **/
GList *
gnm_app_workbook_list (void)
{
	g_return_val_if_fail (app != NULL, NULL);

	return app->workbook_list;
}

void
gnm_app_sanity_check (void)
{
	GList *l;
	gboolean err = FALSE;
	for (l = gnm_app_workbook_list (); l; l = l->next) {
		Workbook *wb = l->data;
		if (gnm_named_expr_collection_sanity_check (wb->names, "workbook"))
			err = TRUE;
	}
	if (err)
		g_error ("Sanity check failed\n");
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

		g_signal_emit (G_OBJECT (app), signals[CLIPBOARD_MODIFIED], 0);

		sv_weak_unref (&(app->clipboard_sheet_view));

		/* Release the selection */
		if (drop_selection)
			gnm_x_disown_clipboard ();
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
 * @wbc: the workbook control that requested the operation.
 * @is_cut: is this a cut or a copy.
 * @sv: The source sheet for the copy.
 * @area: A single rectangular range to be copied.
 * @animate_range: Do we want ot add an animated cursor around things.
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
	app->clipboard_cut_range = gnm_range_dup (area);
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
		g_signal_emit (G_OBJECT (app), signals[CLIPBOARD_MODIFIED], 0);
	} else {
		gnm_app_clipboard_clear (FALSE);
		g_warning ("Unable to set selection ?");
	}
}

/**
 * gnm_app_clipboard_cut_copy_obj:
 * @wbc: #WorkbookControl
 * @is_cut:
 * @sv: #SheetView
 * @objects: (element-type SheetObject): a list of #SheetObject which is freed
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
		g_signal_emit (G_OBJECT (app), signals[CLIPBOARD_MODIFIED], 0);
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

/**
 * gnm_app_clipboard_sheet_get:
 *
 * Returns: (transfer none): the current clipboard #Sheet.
 **/
Sheet *
gnm_app_clipboard_sheet_get (void)
{
	g_return_val_if_fail (app != NULL, NULL);

	if (app->clipboard_sheet_view == NULL)
		return NULL;
	return sv_sheet (app->clipboard_sheet_view);
}

/**
 * gnm_app_clipboard_sheet_view_get:
 *
 * Returns: (transfer none): the current clipboard #SheetView.
 **/
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

/**
 * gnm_app_workbook_get_by_name:
 * @name: the workbook name.
 * @ref_uri:
 *
 * Returns: (transfer none): the #Workbook or %NULL.
 **/
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

static gboolean
gnm_app_workbook_foreach (GnmWbIterFunc cback, gpointer data);

static Workbook *
gnm_app_workbook_get_by_uri (char const *uri)
{
	struct wb_uri_closure closure;

	g_return_val_if_fail (uri != NULL, NULL);

	closure.wb = NULL;
	closure.uri = uri;
	gnm_app_workbook_foreach (&cb_workbook_uri, &closure);

	return closure.wb;
}

static gboolean
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

/**
 * gnm_app_workbook_get_by_index:
 * @i: index
 *
 * Get nth workbook.  Index is zero-based.
 * Return value: (transfer none): the nth workbook if any.
 */
Workbook *
gnm_app_workbook_get_by_index (int i)
{
	return g_list_nth_data (app->workbook_list, i);
}

double
gnm_app_display_dpi_get (gboolean horizontal)
{
	return horizontal
		? gnm_conf_get_core_gui_screen_horizontaldpi ()
		: gnm_conf_get_core_gui_screen_verticaldpi ();
}

double
gnm_app_dpi_to_pixels (void)
{
	return MIN (gnm_app_display_dpi_get (TRUE),
		    gnm_app_display_dpi_get (FALSE)) / 72.;
}

/* GtkFileFilter */
/**
 * gnm_app_create_opener_filter:
 * @openers: (element-type GOFileOpener): a list of file openers.
 *
 * Creates a #GtkFileFilter from the list of file types supported by the
 * openers in the list.
 * Returns: (transfer full): the newly allocated #GtkFileFilter.
 **/
void *
gnm_app_create_opener_filter (GList *openers)
{
	/* See below.  */
	static const char *const bad_suffixes[] = {
		"txt",
		"html", "htm",
		"xml",
		NULL
	};

	GtkFileFilter *filter = gtk_file_filter_new ();
	gboolean for_history = (openers == NULL);

	if (openers == NULL)
		openers = go_get_file_openers ();

	for (; openers; openers = openers->next) {
		GOFileOpener *opener = openers->data;
		if (opener != NULL) {
			const GSList *mimes = go_file_opener_get_mimes (opener);
			const GSList *suffixes = go_file_opener_get_suffixes (opener);

			if (!for_history)
				while (mimes) {
					const char *mime = mimes->data;
					/*
					 * See 438918.  Too many things
					 * like *.xml and *.txt get added
					 * to be useful for the file history
					 */
					gtk_file_filter_add_mime_type (filter, mime);
					if (0)
						g_print ("%s: Adding mime %s\n", go_file_opener_get_description (opener), mime);
					mimes = mimes->next;
				}

			while (suffixes) {
				const char *suffix = suffixes->data;
				GString *pattern;
				int i;

				if (for_history)
					for (i = 0; bad_suffixes[i]; i++)
						if (strcmp (suffix, bad_suffixes[i]) == 0)
							goto bad_suffix;

				/* Create "*.[xX][lL][sS]" */
				pattern = g_string_new ("*.");
				while (*suffix) {
					gunichar uc = g_utf8_get_char (suffix);
					suffix = g_utf8_next_char (suffix);
					if (g_unichar_islower (uc)) {
						g_string_append_c (pattern, '[');
						g_string_append_unichar (pattern, uc);
						uc = g_unichar_toupper (uc);
						g_string_append_unichar (pattern, uc);
						g_string_append_c (pattern, ']');
					} else
						g_string_append_unichar (pattern, uc);
				}

				gtk_file_filter_add_pattern (filter, pattern->str);
				if (0)
					g_print ("%s: Adding %s\n", go_file_opener_get_description (opener), pattern->str);
				g_string_free (pattern, TRUE);

			bad_suffix:
				suffixes = suffixes->next;
			}
		}
	}
	return filter;
}

static gint
compare_mru (GtkRecentInfo *a, GtkRecentInfo *b)
{
	time_t ta = MAX (gtk_recent_info_get_visited (a), gtk_recent_info_get_modified (a));
	time_t tb = MAX (gtk_recent_info_get_visited (b), gtk_recent_info_get_modified (b));

	return ta < tb;
}

/**
 * gnm_app_history_get_list:
 *
 * creating it if necessary.
 *
 * Return value: (element-type char) (transfer full): the list, which must be
 * freed along with the strings in it.
 **/
GSList *
gnm_app_history_get_list (int max_elements)
{
	GSList *res = NULL;
	GList *items, *l;
	GtkFileFilter *filter;
	int n_elements = 0;

	if (app->recent == NULL)
		return NULL;

	items = gtk_recent_manager_get_items (app->recent);
	items = g_list_sort (items, (GCompareFunc)compare_mru);

	filter = gnm_app_create_opener_filter (NULL);

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

	g_list_free_full (items, (GDestroyNotify)gtk_recent_info_unref);
	g_object_ref_sink (filter);
	g_object_unref (filter);

	return g_slist_reverse (res);
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
	GtkRecentData rd;

	if (app->recent == NULL)
		return;

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

	if (!gtk_recent_manager_add_full (app->recent, uri, &rd)) {
		/* Now what?  */
		g_printerr ("Warning: failed to update recent document.\n");
	}

	g_free (rd.mime_type);
	g_free (rd.app_name);
	g_free (rd.app_exec);

	g_object_notify (G_OBJECT (app), "file-history-list");
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

	application->recent = NULL;

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
add_icon (GtkIconFactory *factory,
	  const char *scalable_filename,
	  const char *sized_filename,
	  const char *stock_id)
{
	GtkIconSet *set = gtk_icon_set_new ();
	GtkIconSource *src = gtk_icon_source_new ();

	if (scalable_filename) {
		char *res = g_strconcat ("res:gnm:pixmaps/",
					 scalable_filename,
					 NULL);
		GdkPixbuf *pix = go_gdk_pixbuf_load_from_file (res);
		if (pix) {
			gtk_icon_source_set_size_wildcarded (src, TRUE);
			gtk_icon_source_set_pixbuf (src, pix);
			gtk_icon_set_add_source (set, src);
			g_object_unref (pix);
		} else {
			g_warning ("Missing resource %s\n", res);
		}
		g_free (res);
	}

	/*
	 * For now, don't register a fixed-sized icon as doing so without
	 * catching style changes kills things like bug 302902.
	 */
	if (sized_filename && !scalable_filename) {
		char *res = g_strconcat ("res:gnm:pixmaps/",
					 sized_filename,
					 NULL);
		GdkPixbuf *pix = go_gdk_pixbuf_load_from_file (res);
		gtk_icon_source_set_size (src, GTK_ICON_SIZE_MENU);
		gtk_icon_source_set_size_wildcarded (src, FALSE);
		gtk_icon_source_set_pixbuf (src, pix);
		gtk_icon_set_add_source (set, src);
		g_object_unref (pix);
		g_free (res);
	}

	gtk_icon_factory_add (factory, stock_id, set);
	gtk_icon_set_unref (set);
	gtk_icon_source_free (src);
}


static void
install_icons (GnmApp *app)
{
	static const char *icons[] = {
		/* Cursors */
		"cursor_cross.xpm",
		"bucket.xpm",
		"font.xpm",
		"sheet_move_marker.xpm",

		/* Patterns */
		"gp_125grey.xpm",
		"gp_25grey.xpm",
		"gp_50grey.xpm",
		"gp_625grey.xpm",
		"gp_75grey.xpm",
		"gp_bricks.xpm",
		"gp_diag.xpm",
		"gp_diag_cross.xpm",
		"gp_foreground_solid.xpm",
		"gp_horiz.xpm",
		"gp_large_circles.xpm",
		"gp_rev_diag.xpm",
		"gp_semi_circle.xpm",
		"gp_small_circle.xpm",
		"gp_solid.xpm",
		"gp_thatch.xpm",
		"gp_thick_diag_cross.xpm",
		"gp_thin_diag.xpm",
		"gp_thin_diag_cross.xpm",
		"gp_thin_horiz.xpm",
		"gp_thin_horiz_cross.xpm",
		"gp_thin_rev_diag.xpm",
		"gp_thin_vert.xpm",
		"gp_vert.xpm",
		"line_pattern_dash_dot.xpm",
		"line_pattern_dash_dot_dot.xpm",
		"line_pattern_dashed.xpm",
		"line_pattern_dotted.xpm",
		"line_pattern_double.xpm",
		"line_pattern_hair.xpm",
		"line_pattern_medium.xpm",
		"line_pattern_medium_dash.xpm",
		"line_pattern_medium_dash_dot.xpm",
		"line_pattern_medium_dash_dot_dot.xpm",
		"line_pattern_slant.xpm",
		"line_pattern_thick.xpm",
		"line_pattern_thin.xpm",

		/* Borders */
		"bottom_border.xpm",
		"diag_border.xpm",
		"inside_border.xpm",
		"inside_horiz_border.xpm",
		"inside_vert_border.xpm",
		"left_border.xpm",
		"no_border.xpm",
		"outline_border.xpm",
		"rev_diag_border.xpm",
		"right_border.xpm",
		"top_border.xpm"
	};
	static struct {
		const char *scalable_filename;
		const char *sized_filename;
		const char *stock_id;
	} const icons2[] = {
		{
			"column_add_24.xpm",
			"column_add_16.xpm",
			"Gnumeric_ColumnAdd"
		},
		{
			"column_delete_24.xpm",
			"column_delete_16.xpm",
			"Gnumeric_ColumnDelete"
		},
		{
			"column_size_24.xpm",
			"column_size_16.xpm",
			"Gnumeric_ColumnSize"
		},
		{
			"column_hide_24.xpm",
			"column_hide_16.xpm",
			"Gnumeric_ColumnHide"
		},
		{
			"column_unhide_24.xpm",
			"column_unhide_16.xpm",
			"Gnumeric_ColumnUnhide"
		},
		{
			"row_add_24.xpm",
			"row_add_16.xpm",
			"Gnumeric_RowAdd"
		},
		{
			"row_delete_24.xpm",
			"row_delete_16.xpm",
			"Gnumeric_RowDelete"
		},
		{
			"row_size_24.xpm",
			"row_size_16.xpm",
			"Gnumeric_RowSize"
		},
		{
			"row_hide_24.xpm",
			"row_hide_16.xpm",
			"Gnumeric_RowHide"
		},
		{
			"row_unhide_24.xpm",
			"row_unhide_16.xpm",
			"Gnumeric_RowUnhide"
		},

		{
			"group_24.xpm",
			"group_16.xpm",
			"Gnumeric_Group"
		},
		{
			"ungroup_24.xpm",
			"ungroup_16.xpm",
			"Gnumeric_Ungroup"
		},
		{
			"show_detail_24.xpm",
			"show_detail_16.xpm",
			"Gnumeric_ShowDetail"
		},
		{
			"hide_detail_24.xpm",
			"hide_detail_16.xpm",
			"Gnumeric_HideDetail"
		},

		{
			"graph_guru_24.xpm",
			"graph_guru_16.xpm",
			"Gnumeric_GraphGuru"
		},
		{
			"insert_component_24.xpm",
			"insert_component_16.xpm",
			"Gnumeric_InsertComponent"
		},
		{
			"insert_shaped_component_24.xpm",
			"insert_shaped_component_16.xpm",
			"Gnumeric_InsertShapedComponent"
		},

		{
			"center_across_selection_24.xpm",
			"center_across_selection_16.xpm",
			"Gnumeric_CenterAcrossSelection"
		},
		{
			"merge_cells_24.xpm",
			"merge_cells_16.xpm",
			"Gnumeric_MergeCells"
		},
		{
			"split_cells_24.xpm",
			"split_cells_16.xpm",
			"Gnumeric_SplitCells"
		},

		{
			"halign-fill_24.png",
			NULL,
			"Gnumeric_HAlignFill"
		},
		{
			"halign-general_24.png",
			NULL,
			"Gnumeric_HAlignGeneral"
		},

		{
			NULL,
			"comment_add_16.xpm",
			"Gnumeric_CommentAdd"
		},
		{
			NULL,
			"comment_delete_16.xpm",
			"Gnumeric_CommentDelete"
		},
		{
			NULL,
			"comment_edit_16.xpm",
			"Gnumeric_CommentEdit"
		},

		{
			"add_decimals.png",
			NULL,
			"Gnumeric_FormatAddPrecision"
		},
		{
			"remove_decimals.png",
			NULL,
			"Gnumeric_FormatRemovePrecision"
		},
		{
			"format_money_24.png",
			NULL,
			"Gnumeric_FormatAsAccounting"
		},
		{
			"format_percent_24.png",
			NULL,
			"Gnumeric_FormatAsPercentage"
		},
		{
			"thousands.xpm",
			NULL,
			"Gnumeric_FormatThousandSeparator"
		},
		{
			"gnm_subscript_24.png",
			"gnm_subscript_16.png",
			"Gnumeric_Subscript"
		},
		{
			"gnm_superscript_24.png",
			"gnm_superscript_16.png",
			"Gnumeric_Superscript"
		},

		{
			"auto-sum.xpm",
			NULL,
			"Gnumeric_AutoSum"
		},
		{
			"equal-sign.xpm",
			NULL,
			"Gnumeric_Equal"
		},
		{
			"formula_guru_24.png",
			"formula_guru_16.png",
			"Gnumeric_FormulaGuru"
		},
		{
			"insert_image_24.png",
			"insert_image_16.png",
			"Gnumeric_InsertImage"
		},
		{
			"bucket.xpm",
			NULL,
			"Gnumeric_Bucket"
		},
		{
			"font.xpm",
			NULL,
			"Gnumeric_Font"
		},
		{
			"expr_entry.png",
			NULL,
			"Gnumeric_ExprEntry"
		},
		{
			"brush-22.png",
			"brush-16.png",
			"Gnumeric_Brush"
		},

		{
			"object_arrow_24.png",
			NULL,
			"Gnumeric_ObjectArrow"
		},
		{
			"object_ellipse_24.png",
			NULL,
			"Gnumeric_ObjectEllipse"
		},
		{
			"object_line_24.png",
			NULL,
			"Gnumeric_ObjectLine"
		},

		{
			"object_rectangle_24.png",
			NULL,
			"Gnumeric_ObjectRectangle"
		},

		{
			"object_frame_24.png",
			NULL,
			"Gnumeric_ObjectFrame"
		},
		{
			"object_label_24.png",
			NULL,
			"Gnumeric_ObjectLabel"
		},
		{
			"object_button_24.png",
			NULL,
			"Gnumeric_ObjectButton"
		},
		{
			"object_checkbox_24.png",
			NULL,
			"Gnumeric_ObjectCheckbox"
		},
		{
			"object_radiobutton_24.png",
			NULL,
			"Gnumeric_ObjectRadioButton"
		},
		{
			"object_scrollbar_24.png",
			NULL,
			"Gnumeric_ObjectScrollbar"
		},
		{
			"object_spinbutton_24.png",
			NULL,
			"Gnumeric_ObjectSpinButton"
		},
		{
			"object_slider_24.png",
			NULL,
			"Gnumeric_ObjectSlider"
		},
		{
			"object_combo_24.png",
			NULL,
			"Gnumeric_ObjectCombo"
		},
		{
			"object_list_24.png",
			NULL,
			"Gnumeric_ObjectList"
		},

		{
			"pivottable_24.png",
			"pivottable_16.png",
			"Gnumeric_PivotTable"
		},
		{
			"protection_yes_24.png",
			NULL,
			"Gnumeric_Protection_Yes"
		},
		{
			"protection_no_24.png",
			NULL,
			"Gnumeric_Protection_No"
		},
		{
			"protection_yes_48.png",
			NULL,
			"Gnumeric_Protection_Yes_Dialog"
		},
		{
			"visible.png",
			NULL,
			"Gnumeric_Visible"
		},

		{
			"link_add_24.png",
			"link_add_16.png",
			"Gnumeric_Link_Add"
		},
		{
			NULL,
			"link_delete_16.png",
			"Gnumeric_Link_Delete"
		},
		{
			NULL,
			"link_edit_16.png",
			"Gnumeric_Link_Edit"
		},
		{
			"link_external_24.png",
			"link_external_16.png",
			"Gnumeric_Link_External"
		},
		{
			"link_internal_24.png",
			"link_internal_16.png",
			"Gnumeric_Link_Internal"
		},
		{
			"link_email_24.png",
			"link_email_16.png",
			"Gnumeric_Link_EMail"
		},
		{
			"link_url_24.png",
			"link_url_16.png",
			"Gnumeric_Link_URL"
		},

		{
			"autofilter_24.png",
			"autofilter_16.png",
			"Gnumeric_AutoFilter"
		},
		{
			"autofilter_delete_24.png",
			"autofilter_delete_16.png",
			"Gnumeric_AutoFilterDelete"
		},

		{
			"border_left.xpm",
			NULL,
			"Gnumeric_BorderLeft"
		},
		{
			"border_none.xpm",
			NULL,
			"Gnumeric_BorderNone"
		},
		{
			"border_right.xpm",
			NULL,
			"Gnumeric_BorderRight"
		},
		{
			"border_all.xpm",
			NULL,
			"Gnumeric_BorderAll"
		},
		{
			"border_outside.xpm",
			NULL,
			"Gnumeric_BorderOutside"
		},
		{
			"border_thick_outside.xpm",
			NULL,
			"Gnumeric_BorderThickOutside"
		},
		{
			"border_bottom.xpm",
			NULL,
			"Gnumeric_BorderBottom"
		},
		{
			"border_double_bottom.xpm",
			NULL,
			"Gnumeric_BorderDoubleBottom"
		},
		{
			"border_thick_bottom.xpm",
			NULL,
			"Gnumeric_BorderThickBottom"
		},
		{
			"border_top_n_bottom.xpm",
			NULL,
			"Gnumeric_BorderTop_n_Bottom"
		},
		{
			"border_top_n_double_bottom.xpm",
			NULL,
			"Gnumeric_BorderTop_n_DoubleBottom"
		},
		{
			"border_top_n_thick_bottom.xpm",
			NULL,
			"Gnumeric_BorderTop_n_ThickBottom"
		},

		{
			"hf_page.png",
			NULL,
			"Gnumeric_Pagesetup_HF_Page"
		},
		{
			"hf_pages.png",
			NULL,
			"Gnumeric_Pagesetup_HF_Pages"
		},
		{
			"hf_time.png",
			NULL,
			"Gnumeric_Pagesetup_HF_Time"
		},
		{
			"hf_date.png",
			NULL,
			"Gnumeric_Pagesetup_HF_Date"
		},
		{
			"hf_sheet.png",
			NULL,
			"Gnumeric_Pagesetup_HF_Sheet"
		},
		{
			"hf_cell.png",
			NULL,
			"Gnumeric_Pagesetup_HF_Cell"
		},
	};

	GtkIconFactory *factory = gtk_icon_factory_new ();
	unsigned int ui;

	for (ui = 0; ui < G_N_ELEMENTS (icons); ui++) {
		const char *filename = icons[ui];
		char *res = g_strconcat ("res:gnm:pixmaps/", filename, NULL);
		char *iconname;
		GdkPixbuf *pixbuf = go_gdk_pixbuf_load_from_file (res);
		int size = gdk_pixbuf_get_width (pixbuf);

		iconname = g_strdup (filename);
		strchr(iconname, '.')[0] = 0;
		gtk_icon_theme_add_builtin_icon (iconname,
						 size,
						 pixbuf);

		g_object_unref (pixbuf);
		g_free (iconname);
		g_free (res);
	}

	for (ui = 0; ui < G_N_ELEMENTS (icons2) ; ui++)
		add_icon (factory,
			  icons2[ui].scalable_filename,
			  icons2[ui].sized_filename,
			  icons2[ui].stock_id);
	gtk_icon_factory_add_default (factory);
	g_object_set_data_full (G_OBJECT (app),
				"icon-factory", factory,
				(GDestroyNotify)gtk_icon_factory_remove_default);
	g_object_unref (factory);
}


static void
gnm_app_class_init (GObjectClass *gobject_klass)
{
	parent_klass = g_type_class_peek_parent (gobject_klass);

	/* Object class method overrides */
	gobject_klass->finalize = gnumeric_application_finalize;
	gobject_klass->get_property = gnumeric_application_get_property;
	g_object_class_install_property (gobject_klass, APPLICATION_PROP_FILE_HISTORY_LIST,
		g_param_spec_pointer ("file-history-list",
				      P_("File History List"),
				      P_("A list of filenames that have been read recently"),
				      GSF_PARAM_STATIC | G_PARAM_READABLE));

	signals[WORKBOOK_ADDED] = g_signal_new ("workbook_added",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, workbook_added),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1, WORKBOOK_TYPE);
	signals[WORKBOOK_REMOVED] = g_signal_new ("workbook_removed",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, workbook_removed),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[WINDOW_LIST_CHANGED] = g_signal_new ("window-list-changed",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, window_list_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
	signals[CUSTOM_UI_ADDED] = g_signal_new ("custom-ui-added",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, custom_ui_added),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[CUSTOM_UI_REMOVED] = g_signal_new ("custom-ui-removed",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, custom_ui_removed),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[CLIPBOARD_MODIFIED] = g_signal_new ("clipboard_modified",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, clipboard_modified),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
	signals[RECALC_FINISHED] = g_signal_new ("recalc_finished",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, recalc_finished),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
	signals[RECALC_FINISHED] = g_signal_new ("recalc_clear_caches",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, recalc_clear_caches),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
}

static void
gnm_app_init (GObject *obj)
{
	GnmApp *gnm_app = GNM_APP (obj);
	static gboolean icons_installed = FALSE;

	if (!icons_installed) {
		icons_installed = TRUE;
		install_icons (gnm_app);
	}

	gnm_app->clipboard_copied_contents = NULL;
	gnm_app->clipboard_sheet_view = NULL;

	gnm_app->workbook_list = NULL;

	if (gdk_display_get_default ()) {
		/*
		 * Only allocate a GtkRecentManager if we have a gui.
		 * This is, in part, because it currently throws an error.
		 * deep inside gtk+.
		 */
		gnm_app->recent = gtk_recent_manager_get_default ();
		g_signal_connect_object (G_OBJECT (gnm_app->recent),
					 "changed",
					 G_CALLBACK (cb_recent_changed),
					 gnm_app, 0);
	}

	app = gnm_app;
}

GSF_CLASS (GnmApp, gnm_app,
	   gnm_app_class_init, gnm_app_init,
	   G_TYPE_OBJECT)

/**********************************************************************/
static GSList *extra_uis = NULL;

/**
 * gnm_action_new:
 * @name: action ID.
 * @label: label.
 * @icon: icon name.
 * @always_available: whether the action should always be available.
 * @handler: (scope async): the handler.
 *
 * Returns: (transfer full): the newly allocated #GnmAction.
 **/
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

static GnmAction *
gnm_action_copy (GnmAction const *action)
{
	return gnm_action_new (action->id, action->label, action->icon_name,
	                       action->always_available, action->handler);
}

GType
gnm_action_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmAction",
			 (GBoxedCopyFunc)gnm_action_copy,
			 (GBoxedFreeFunc)gnm_action_free);
	}
	return t;
}

/***************
 * making GnmAppExtraUI a boxed type for introspection. copy and free don't do
 anything which might be critical, crossing fingers.*/

static GnmAppExtraUI *
gnm_app_extra_ui_ref (GnmAppExtraUI *ui)
{
	return ui;
}

GType
gnm_app_extra_ui_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmAppExtraUI",
			 (GBoxedCopyFunc)gnm_app_extra_ui_ref,
			 (GBoxedFreeFunc)gnm_app_extra_ui_ref);
	}
	return t;
}

/**
 * gnm_app_add_extra_ui:
 * @group_name: action group name.
 * @actions: (element-type GnmAction): list of actions.
 * @layout: the xml string describing the menus and toolbars.
 * @domain: localization domain.
 * @user_data: user data
 *
 * Returns: (transfer full): the newly allocated #GnmAppExtraUI.
 **/
GnmAppExtraUI *
gnm_app_add_extra_ui (char const *group_name,
		      GSList *actions,
		      const char *layout,
		      char const *domain,
		      gpointer user_data)
{
	GnmAppExtraUI *extra_ui = g_new0 (GnmAppExtraUI, 1);
	extra_uis = g_slist_prepend (extra_uis, extra_ui);
	extra_ui->group_name = g_strdup (group_name);
	extra_ui->actions = actions;
	extra_ui->layout = g_strdup (layout);
	extra_ui->user_data = user_data;
	g_signal_emit (G_OBJECT (app), signals[CUSTOM_UI_ADDED], 0, extra_ui);
	if (gnm_debug_flag ("extra-ui"))
		g_printerr ("Adding extra ui [%s] %p\n", group_name, extra_ui);
	return extra_ui;
}

void
gnm_app_remove_extra_ui (GnmAppExtraUI *extra_ui)
{
	if (gnm_debug_flag ("extra-ui"))
		g_printerr ("Removing extra ui %p\n", extra_ui);
	extra_uis = g_slist_remove (extra_uis, extra_ui);
	g_signal_emit (G_OBJECT (app), signals[CUSTOM_UI_REMOVED], 0, extra_ui);
	g_free (extra_ui->group_name);
	g_free (extra_ui->layout);
	g_free (extra_ui);
}

/**
 * gnm_app_foreach_extra_ui:
 * @func: (scope call): #GFunc
 * @data: user data.
 *
 * Applies @func to each #GnmAppExtraUI.
 **/
void
gnm_app_foreach_extra_ui (GFunc func, gpointer data)
{
	g_slist_foreach (extra_uis, func, data);
}

/**********************************************************************/

static gint windows_update_timer = 0;
static gboolean
cb_flag_windows_changed (void)
{
	if (app)
		g_signal_emit (G_OBJECT (app), signals[WINDOW_LIST_CHANGED], 0);
	windows_update_timer = 0;
	return FALSE;
}

/**
 * _gnm_app_flag_windows_changed:
 *
 * An internal utility routine to flag a regeneration of the window lists
 **/
void
_gnm_app_flag_windows_changed (void)
{
	if (windows_update_timer == 0)
		windows_update_timer = g_timeout_add (100,
			(GSourceFunc)cb_flag_windows_changed, NULL);
}

/**********************************************************************/

/**
 * gnm_app_recalc:
 *
 * Recalculate everything dirty in all workbooks that have automatic
 * recalc turned on.
 **/
void
gnm_app_recalc (void)
{
	GList *l;

	g_return_if_fail (app != NULL);

	gnm_app_recalc_start ();

	for (l = app->workbook_list; l; l = l->next) {
		Workbook *wb = l->data;

		if (workbook_get_recalcmode (wb))
			workbook_recalc (wb);
	}

	gnm_app_recalc_finish ();
}

void
gnm_app_recalc_start (void)
{
	g_return_if_fail (app->recalc_count >= 0);
	app->recalc_count++;
}

void
gnm_app_recalc_finish (void)
{
	g_return_if_fail (app->recalc_count > 0);
	app->recalc_count--;
	if (app->recalc_count == 0) {
		gnm_app_recalc_clear_caches ();
		g_signal_emit_by_name (gnm_app_get_app (), "recalc-finished");
	}
}

void
gnm_app_recalc_clear_caches (void)
{
	g_signal_emit_by_name (gnm_app_get_app (), "recalc-clear-caches");
}
