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
#include "workbook-priv.h" /* For Workbook::name */
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-private.h"
#include "sheet-object.h"
#include "auto-correct.h"
#include "gutils.h"
#include "ranges.h"
#include "pixmaps/gnumeric-stock-pixbufs.h"

#include <gnumeric-gconf.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkiconfactory.h>
#include <gtk/gtkselection.h>

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
	CLIPBOARD_MODIFIED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

struct _GnmApp {
	GObject  base;

	/* Clipboard */
	SheetView	*clipboard_sheet_view;
	GnmCellRegion	*clipboard_copied_contents;
	GnmRange	*clipboard_cut_range;

	/* History for file menu */
	GSList           *history_list;

	/* Others */
	GtkWidget       *pref_dialog;

	GList		*workbook_list;

	GHashTable      *named_pixbufs;
};

typedef struct {
	GObjectClass     parent;

	void (*workbook_added)      (GnmApp *gnm_app, Workbook *wb);
	void (*workbook_removed)    (GnmApp *gnm_app, Workbook *wb);
	void (*window_list_changed) (GnmApp *gnm_app);
	void (*clipboard_modified)  (GnmApp *gnm_app);

} GnmAppClass;

static GObjectClass *parent_klass;
static GnmApp *app;

GObject *
gnm_app_get_app (void)
{
	return G_OBJECT (app);
}

static void
add_icon (GtkIconFactory *factory,
	  guchar const   *scalable_data,
	  guchar const   *sized_data,
	  gchar const    *stock_id)
{
	GtkIconSet *set = gtk_icon_set_new ();
	GtkIconSource *src = gtk_icon_source_new ();

	if (scalable_data != NULL) {
		gtk_icon_source_set_size_wildcarded (src, TRUE);
		gtk_icon_source_set_pixbuf (src,
			gdk_pixbuf_new_from_inline (-1, scalable_data, FALSE, NULL));
		gtk_icon_set_add_source (set, src);	/* copies the src */
	}

	if (sized_data != NULL) {
		gtk_icon_source_set_size (src, GTK_ICON_SIZE_MENU);
		gtk_icon_source_set_size_wildcarded (src, FALSE);
		gtk_icon_source_set_pixbuf (src,
			gdk_pixbuf_new_from_inline (-1, sized_data, FALSE, NULL));
		gtk_icon_set_add_source (set, src);	/* copies the src */
	}

	gtk_icon_factory_add (factory, stock_id, set);	/* keeps reference to set */
	gtk_icon_set_unref (set);
	gtk_icon_source_free (src);
}

/**
 * gnm_app_workbook_list_remove :
 * @wb :
 *
 * Remove @wb from the application's list of workbooks.
 **/
void
gnm_app_workbook_list_add (Workbook *wb)
{
	g_return_if_fail (IS_WORKBOOK (wb));
	g_return_if_fail (app != NULL);

	app->workbook_list = g_list_prepend (app->workbook_list, wb);
	g_signal_connect (G_OBJECT (wb),
		"filename_changed",
		G_CALLBACK (gnm_app_flag_windows_changed), NULL);
	gnm_app_flag_windows_changed ();
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
		G_CALLBACK (gnm_app_flag_windows_changed), NULL);
	gnm_app_flag_windows_changed ();
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
	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (area != NULL);
	g_return_if_fail (app != NULL);

	gnm_app_clipboard_clear (FALSE);

	if (wb_control_claim_selection (wbc)) {
		Sheet *sheet = sv_sheet (sv);
		g_free (app->clipboard_cut_range);
		app->clipboard_cut_range = range_dup (area);
		sv_weak_ref (sv, &(app->clipboard_sheet_view));

		if (!is_cut)
			app->clipboard_copied_contents =
				clipboard_copy_range (sheet, area);

		g_signal_emit (G_OBJECT (app), signals [CLIPBOARD_MODIFIED], 0);

		if (animate_cursor) {
			GList *l = g_list_append (NULL, (gpointer)area);
			sv_ant (sv, l);
			g_list_free (l);
		}
	} else {
		g_warning ("Unable to set selection ?");
	}
}
/** gnm_app_clipboard_cut_copy_obj :
 * @wbc : #WorkbookControl
 * @is_cut : 
 * @sv : #SheetView
 * @so : #SheetObject
 *
 * Different than copying/cutting a region, this can actually cut an object
 **/
void
gnm_app_clipboard_cut_copy_obj (WorkbookControl *wbc, gboolean is_cut,
				SheetView *sv, SheetObject *so)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (IS_SHEET_OBJECT (so));
	g_return_if_fail (app != NULL);

	gnm_app_clipboard_clear (FALSE);

	if (wb_control_claim_selection (wbc)) {
		Sheet *sheet = sv_sheet (sv);
		app->clipboard_cut_range = NULL;
		sv_weak_ref (sv, &(app->clipboard_sheet_view));

		app->clipboard_copied_contents = cellregion_new	(sheet);
		if (is_cut) {
			g_object_ref (so);
			sheet_object_clear_sheet (so);
		} else
			so = sheet_object_dup (so);
		app->clipboard_copied_contents->objects = g_slist_prepend (
			app->clipboard_copied_contents->objects, so);

		g_signal_emit (G_OBJECT (app), signals [CLIPBOARD_MODIFIED], 0);
	} else {
		g_warning ("Unable to set selection ?");
	}
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

struct wb_name_closure {
	Workbook *wb;
	char const *name;
};
static gboolean
cb_workbook_name (Workbook * wb, gpointer closure)
{
	struct wb_name_closure *dat = closure;
	const char *wb_uri = workbook_get_uri (wb);

	if (wb_uri && strcmp (wb_uri, dat->name) == 0) {
		dat->wb = wb;
		return FALSE;
	}
	return TRUE;
}

Workbook *
gnm_app_workbook_get_by_name (char const * const name)
{
	struct wb_name_closure close;
	close.wb = NULL;
	close.name = name;
	gnm_app_workbook_foreach (&cb_workbook_name, &close);

	return close.wb;
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
cb_workbook_index (G_GNUC_UNUSED Workbook * wb, gpointer closure)
{
	struct wb_index_closure *dat = closure;
	return (--(dat->index) != 0);
}

Workbook *
gnm_app_workbook_get_by_index (int i)
{
	struct wb_index_closure close;
	close.wb = NULL;
	close.index = i;
	gnm_app_workbook_foreach (&cb_workbook_index, &close);

	return close.wb;
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

/**
 * gnm_app_history_get_list:
 *
 * creating it if necessary.
 *
 * Return value: the list./
 **/
GSList const *
gnm_app_history_get_list (gboolean force_reload)
{
        gint max_entries;
	GSList const *ptr;
	GSList *res = NULL;

	g_return_val_if_fail (app != NULL, NULL);

	if (app->history_list != NULL) {
		if (force_reload) {
			GSList *tmp = app->history_list;
			app->history_list = NULL;
			gnm_slist_free_custom (tmp, g_free);
		} else
			return app->history_list;
	}

	max_entries = gnm_app_prefs->file_history_max;
	for (ptr = gnm_app_prefs->file_history_files;
	     ptr != NULL && max_entries-- > 0 ; ptr = ptr->next)
		res = g_slist_prepend (res, g_strdup (ptr->data));
	return app->history_list = g_slist_reverse (res);;
}

/**
 * application_history_update_list:
 * @uri:
 *
 * Adds @uri to the application's history of files.
 **/
void
gnm_app_history_add (char const *uri)
{
        gint max_entries;
	GSList *exists;
	GSList **ptr;

	g_return_if_fail (uri != NULL);
	g_return_if_fail (app != NULL);

	/* force a reload in case max_entries has changed */
	gnm_app_history_get_list (TRUE);
	exists = g_slist_find_custom (app->history_list,
				      uri, gnm_str_compare);

	if (exists != NULL) {
		/* its already the top of the stack no need to do anything */
		if (exists == app->history_list)
			return;

		/* remove the other instance */
		g_free (exists->data);
		app->history_list = g_slist_delete_link (app->history_list, exists);
	}

	app->history_list = g_slist_prepend (app->history_list, g_strdup (uri));

	/* clip the list if it is too long */
	max_entries = gnm_app_prefs->file_history_max;
	ptr = &(app->history_list);
	while (*ptr != NULL && max_entries-- > 0)
		ptr = &((*ptr)->next);
	if (*ptr != NULL) {
		gnm_slist_free_custom (*ptr, g_free);
		*ptr = NULL;
	}

	g_object_notify (G_OBJECT (app), "file-history-list");
	gnm_gconf_set_file_history_files (
		gnm_string_slist_copy (app->history_list));
	go_conf_sync ();
}

gboolean gnm_app_use_auto_complete	  (void) { return gnm_app_prefs->auto_complete; }
gboolean gnm_app_live_scrolling	  (void) { return gnm_app_prefs->live_scrolling; }
int	 gnm_app_auto_expr_recalc_lag (void) { return gnm_app_prefs->recalc_lag; }
gboolean gnm_app_use_transition_keys  (void) { return gnm_app_prefs->transition_keys; }
void     gnm_app_set_transition_keys  (gboolean state)
{
	((GnmAppPrefs *)gnm_app_prefs)->transition_keys = state;
}

/*
 * Get a named pixbuf.
 */
GdkPixbuf *
gnm_app_get_pixbuf (const char *name)
{
	g_return_val_if_fail (app != NULL, NULL);
	return g_hash_table_lookup (app->named_pixbufs, name);
}


gpointer
gnm_app_get_pref_dialog (void)
{
	g_return_val_if_fail (app != NULL, NULL);
	return app->pref_dialog;
}

void
gnm_app_set_pref_dialog (gpointer dialog)
{
	g_return_if_fail (app != NULL);
	app->pref_dialog = dialog;
}

void
gnm_app_release_pref_dialog (void)
{
	g_return_if_fail (app != NULL);
	if (app->pref_dialog)
		gtk_widget_destroy (app->pref_dialog);
}

static void
gnumeric_application_setup_pixbufs (GnmApp *app)
{
	static struct {
		guchar const   *scalable_data;
		gchar const    *name;
	} const entry [] = {
		/* Cursors */
		{ gnm_cursor_cross, "cursor_cross" },
		{ gnm_bucket, "bucket" },
		{ gnm_font, "font" },
		{ sheet_move_marker, "sheet_move_marker" },
		/* Patterns */
		{ gp_125grey, "gp_125grey" },
		{ gp_25grey, "gp_25grey" },
		{ gp_50grey, "gp_50grey" },
		{ gp_625grey, "gp_625grey" },
		{ gp_75grey, "gp_75grey" },
		{ gp_bricks, "gp_bricks" },
		{ gp_diag, "gp_diag" },
		{ gp_diag_cross, "gp_diag_cross" },
		{ gp_foreground_solid, "gp_foreground_solid" },
		{ gp_horiz, "gp_horiz" },
		{ gp_large_circles, "gp_large_circles" },
		{ gp_rev_diag, "gp_rev_diag" },
		{ gp_semi_circle, "gp_semi_circle" },
		{ gp_small_circle, "gp_small_circle" },
		{ gp_solid, "gp_solid" },
		{ gp_thatch, "gp_thatch" },
		{ gp_thick_diag_cross, "gp_thick_diag_cross" },
		{ gp_thin_diag, "gp_thin_diag" },
		{ gp_thin_diag_cross, "gp_thin_diag_cross" },
		{ gp_thin_horiz, "gp_thin_horiz" },
		{ gp_thin_horiz_cross, "gp_thin_horiz_cross" },
		{ gp_thin_rev_diag, "gp_thin_rev_diag" },
		{ gp_thin_vert, "gp_thin_vert" },
		{ gp_vert, "gp_vert" },
		{ line_pattern_dash_dot, "line_pattern_dash_dot" },
		{ line_pattern_dash_dot_dot, "line_pattern_dash_dot_dot" },
		{ line_pattern_dashed, "line_pattern_dashed" },
		{ line_pattern_dotted, "line_pattern_dotted" },
		{ line_pattern_double, "line_pattern_double" },
		{ line_pattern_hair, "line_pattern_hair" },
		{ line_pattern_medium, "line_pattern_medium" },
		{ line_pattern_medium_dash, "line_pattern_medium_dash" },
		{ line_pattern_medium_dash_dot, "line_pattern_medium_dash_dot" },
		{ line_pattern_medium_dash_dot_dot, "line_pattern_medium_dash_dot_dot" },
		{ line_pattern_slant, "line_pattern_slant" },
		{ line_pattern_thick, "line_pattern_thick" },
		{ line_pattern_thin, "line_pattern_thin" },
		/* Borders */
		{ bottom_border, "bottom_border" },
		{ diag_border, "diag_border" },
		{ inside_border, "inside_border" },
		{ inside_horiz_border, "inside_horiz_border" },
		{ inside_vert_border, "inside_vert_border" },
		{ left_border, "left_border" },
		{ no_border, "no_border" },
		{ outline_border, "outline_border" },
		{ rev_diag_border, "rev_diag_border" },
		{ right_border, "right_border" },
		{ top_border, "top_border" },
		/* Stuff */
		{ unknown_image, "unknown_image" },
		{ gnumeric_splash, "gnumeric_splash" }
	};
	unsigned int ui;

	g_return_if_fail (app != NULL);

	for (ui = 0; ui < G_N_ELEMENTS (entry); ui++) {
		GdkPixbuf *pixbuf = gdk_pixbuf_new_from_inline
			(-1, entry[ui].scalable_data, FALSE, NULL);
		g_hash_table_insert (app->named_pixbufs,
				     (char *)entry[ui].name,
				     pixbuf);
	}
}

static void
gnumeric_application_setup_icons (void)
{
	static struct {
		guchar const   *scalable_data;
		guchar const   *sized_data;
		gchar const    *stock_id;
	} const entry [] = {
		{ gnm_column_add_24,			gnm_column_add_16,		"Gnumeric_ColumnAdd" },
		{ gnm_column_delete_24,			gnm_column_delete_16,		"Gnumeric_ColumnDelete" },
		{ gnm_column_size_24,			gnm_column_size_16,		"Gnumeric_ColumnSize" },
		{ gnm_column_hide_24,			gnm_column_hide_16,		"Gnumeric_ColumnHide" },
		{ gnm_column_unhide_24,			gnm_column_unhide_16,		"Gnumeric_ColumnUnhide" },
		{ gnm_row_add_24,			gnm_row_add_16,			"Gnumeric_RowAdd" },
		{ gnm_row_delete_24,			gnm_row_delete_16,		"Gnumeric_RowDelete" },
		{ gnm_row_size_24,			gnm_row_size_16,		"Gnumeric_RowSize" },
		{ gnm_row_hide_24,			gnm_row_hide_16,		"Gnumeric_RowHide" },
		{ gnm_row_unhide_24,			gnm_row_unhide_16,		"Gnumeric_RowUnhide" },

		{ gnm_group_24,				gnm_group_16,			"Gnumeric_Group" },
		{ gnm_ungroup_24,			gnm_ungroup_16,			"Gnumeric_Ungroup" },
		{ gnm_show_detail_24,			gnm_show_detail_16,		"Gnumeric_ShowDetail" },
		{ gnm_hide_detail_24,			gnm_hide_detail_16,		"Gnumeric_HideDetail" },

		{ gnm_graph_guru_24,			gnm_graph_guru_16,		"Gnumeric_GraphGuru" },
		{ gnm_insert_component_24,		gnm_insert_component_16,	"Gnumeric_InsertComponent" },
		{ gnm_insert_shaped_component_24,	gnm_insert_shaped_component_16,	"Gnumeric_InsertShapedComponent" },

		{ gnm_center_across_selection_24,	gnm_center_across_selection_16,	"Gnumeric_CenterAcrossSelection" },
		{ gnm_merge_cells_24,			gnm_merge_cells_16,		"Gnumeric_MergeCells" },
		{ gnm_split_cells_24,			gnm_split_cells_16,		"Gnumeric_SplitCells" },

		{ gnm_halign_fill_24,			NULL,				"Gnumeric_HAlignFill" },
		{ gnm_halign_general_24,		NULL,				"Gnumeric_HAlignGeneral" },

		{ NULL,					gnm_comment_add_16,		"Gnumeric_CommentAdd" },
		{ NULL,					gnm_comment_delete_16,		"Gnumeric_CommentDelete" },
		{ NULL,					gnm_comment_edit_16,		"Gnumeric_CommentEdit" },

		{ gnm_add_decimals,			NULL,				"Gnumeric_FormatAddPrecision" },
		{ gnm_remove_decimals,			NULL,				"Gnumeric_FormatRemovePrecision" },
		{ gnm_money,				NULL,				"Gnumeric_FormatAsAccounting" },
		{ gnm_percent,				NULL,				"Gnumeric_FormatAsPercentage" },
		{ gnm_thousand,				NULL,				"Gnumeric_FormatThousandSeparator" },

		{ gnm_auto,				NULL,				"Gnumeric_AutoSum" },
		{ gnm_equal,				NULL,				"Gnumeric_Equal" },
		{ gnm_formula_guru_24,			gnm_formula_guru_16,		"Gnumeric_FormulaGuru" },
		{ gnm_insert_image_24,			gnm_insert_image_16,		"Gnumeric_InsertImage" },
		{ gnm_bucket,				NULL,				"Gnumeric_Bucket" },
		{ gnm_font,				NULL,				"Gnumeric_Font" },
		{ gnm_expr_entry,			NULL,				"Gnumeric_ExprEntry" },

		{ gnm_object_arrow_24,			NULL,				"Gnumeric_ObjectArrow" },
		{ gnm_object_ellipse_24,		NULL,				"Gnumeric_ObjectEllipse" },
		{ gnm_object_line_24,			NULL,				"Gnumeric_ObjectLine" },
		{ gnm_object_rectangle_24,		NULL,				"Gnumeric_ObjectRectangle" },

		{ gnm_object_frame_24,			NULL,				"Gnumeric_ObjectFrame" },
		{ gnm_object_label_24,			NULL,				"Gnumeric_ObjectLabel" },
		{ gnm_object_button_24,			NULL,				"Gnumeric_ObjectButton" },
		{ gnm_object_checkbox_24,		NULL,				"Gnumeric_ObjectCheckbox" },
		{ gnm_object_radiobutton_24,		NULL,				"Gnumeric_ObjectRadioButton" },
		{ gnm_object_scrollbar_24,		NULL,				"Gnumeric_ObjectScrollbar" },
		{ gnm_object_spinbutton_24,		NULL,				"Gnumeric_ObjectSpinButton" },
		{ gnm_object_slider_24,			NULL,				"Gnumeric_ObjectSlider" },
		{ gnm_object_combo_24,			NULL,				"Gnumeric_ObjectCombo" },
		{ gnm_object_list_24,			NULL,				"Gnumeric_ObjectList" },

		{ gnm_pivottable_24,	                gnm_pivottable_16,		"Gnumeric_PivotTable" },
		{ gnm_protection_yes,	                NULL,				"Gnumeric_Protection_Yes" },
		{ gnm_protection_no,       		NULL,				"Gnumeric_Protection_No" },
		{ gnm_protection_yes_48,	        NULL,				"Gnumeric_Protection_Yes_Dialog" },
		{ gnm_visible,	                        NULL,				"Gnumeric_Visible" },

		{ gnm_link_add_24,			gnm_link_add_16,		"Gnumeric_Link_Add" },
		{ NULL,					gnm_link_delete_16,		"Gnumeric_Link_Delete" },
		{ NULL,					gnm_link_edit_16,		"Gnumeric_Link_Edit" },
		{ gnm_link_external_24,			gnm_link_external_16,		"Gnumeric_Link_External" },
		{ gnm_link_internal_24,			gnm_link_internal_16,		"Gnumeric_Link_Internal" },
		{ gnm_link_email_24,			gnm_link_email_16,		"Gnumeric_Link_EMail" },
		{ gnm_link_url_24,			gnm_link_url_16,		"Gnumeric_Link_URL" },

		{ gnm_autofilter_24,			gnm_autofilter_16,		"Gnumeric_AutoFilter" },
		{ gnm_autofilter_delete_24,		gnm_autofilter_delete_16,	"Gnumeric_AutoFilterDelete" },

		{ gnm_border_left,			NULL,				"Gnumeric_BorderLeft" },
		{ gnm_border_none,			NULL,				"Gnumeric_BorderNone" },
		{ gnm_border_right,			NULL,				"Gnumeric_BorderRight" },

		{ gnm_border_all,			NULL,				"Gnumeric_BorderAll" },
		{ gnm_border_outside,			NULL,				"Gnumeric_BorderOutside" },
		{ gnm_border_thick_outside,		NULL,				"Gnumeric_BorderThickOutside" },

		{ gnm_border_bottom,			NULL,				"Gnumeric_BorderBottom" },
		{ gnm_border_double_bottom,		NULL,				"Gnumeric_BorderDoubleBottom" },
		{ gnm_border_thick_bottom,		NULL,				"Gnumeric_BorderThickBottom" },

		{ gnm_border_top_n_bottom,		NULL,				"Gnumeric_BorderTop_n_Bottom" },
		{ gnm_border_top_n_double_bottom,	NULL,				"Gnumeric_BorderTop_n_DoubleBottom" },
		{ gnm_border_top_n_thick_bottom,	NULL,				"Gnumeric_BorderTop_n_ThickBottom" }
	};
	static gboolean done = FALSE;

	if (!done) {
		unsigned int ui = 0;
		GtkIconFactory *factory = gtk_icon_factory_new ();
		for (ui = 0; ui < G_N_ELEMENTS (entry) ; ui++)
			add_icon (factory, entry[ui].scalable_data,
				  entry[ui].sized_data, entry[ui].stock_id);
		gtk_icon_factory_add_default (factory);
		g_object_unref (G_OBJECT (factory));
		done = TRUE;
	}
}

static void
gnumeric_application_finalize (GObject *obj)
{
	GnmApp *application = GNM_APP (obj);

	g_slist_foreach (application->history_list, (GFunc)g_free, NULL);
	g_slist_free (application->history_list);
	application->history_list = NULL;
	g_hash_table_destroy (application->named_pixbufs);
	app = NULL;
	G_OBJECT_CLASS (parent_klass)->finalize (obj);
}

static void
gnumeric_application_get_property (GObject *obj, guint param_id,
				   GValue *value, GParamSpec *pspec)
{
	GnmApp *application = GNM_APP (obj);
	switch (param_id) {
	case APPLICATION_PROP_FILE_HISTORY_LIST:
		g_value_set_pointer (value, application->history_list);
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
		g_param_spec_pointer ("file-history-list", "File History List",
			"A GSlist of filenames that have been read recently",
			G_PARAM_READABLE));

	signals [WORKBOOK_ADDED] = g_signal_new ("workbook_added",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, workbook_added),
		(GSignalAccumulator) NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE,
		1, WORKBOOK_TYPE);
	signals [WORKBOOK_REMOVED] = g_signal_new ("workbook_removed",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, workbook_removed),
		(GSignalAccumulator) NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE,
		1, G_TYPE_POINTER);
	signals [WINDOW_LIST_CHANGED] = g_signal_new ("window-list-changed",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, window_list_changed),
		(GSignalAccumulator) NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,
		0);
	signals [CLIPBOARD_MODIFIED] = g_signal_new ("clipboard_modified",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, clipboard_modified),
		(GSignalAccumulator) NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,
		0);
}

static void
gnm_app_init (GObject *obj)
{
	GnmApp *gnm_app = GNM_APP (obj);

	gnm_app->clipboard_copied_contents = NULL;
	gnm_app->clipboard_sheet_view = NULL;

	gnm_app->workbook_list = NULL;

	gnm_app->named_pixbufs = g_hash_table_new_full (g_str_hash, g_str_equal,
							NULL,
							(GDestroyNotify)g_object_unref);

	gnumeric_application_setup_pixbufs (gnm_app);
	gnumeric_application_setup_icons ();

	app = gnm_app;
}

GSF_CLASS (GnmApp, gnm_app,
	   gnm_app_class_init, gnm_app_init,
	   G_TYPE_OBJECT);
 
static gint windows_update_timer = -1;
static gboolean
cb_flag_windows_changed (void)
{
	g_signal_emit (G_OBJECT (app), signals [WINDOW_LIST_CHANGED], 0);
	windows_update_timer = -1;
	return FALSE;
}

/**
 * gnm_app_flag_windows_changed :
 *
 * An internal utility routine to flag a regeneration of the window lists
 **/
void
gnm_app_flag_windows_changed (void)
{
	if (windows_update_timer < 0)
		windows_update_timer = g_timeout_add (100,
			(GSourceFunc)cb_flag_windows_changed, NULL);
}
