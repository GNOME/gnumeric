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
#include "auto-correct.h"
#include "pixmaps/gnumeric-stock-pixbufs.h"
#include "gnm-marshalers.h"

#include <gnumeric-gconf.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtk.h>

/* Signals */
enum {
	WORKBOOK_ADDED,
	WORKBOOK_REMOVED,
	CLIPBOARD_MODIFIED,
	LAST_SIGNAL
};

static GQuark signals [LAST_SIGNAL] = { 0 };

struct _GnumericApplication {
	GObject  base;

	/* Clipboard */
	SheetView	*clipboard_sheet_view;
	CellRegion	*clipboard_copied_contents;
	Range		 clipboard_cut_range;

	/* History for file menu */
	GSList           *history_list;

	/* Others */
	GConfClient     *gconf_client;
	GtkWidget       *pref_dialog;

	GList		*workbook_list;
};

typedef struct 
{
	GObjectClass     parent;

	void (*workbook_added)     (GnumericApplication *gnm_app, Workbook *wb);
	void (*workbook_removed)   (GnumericApplication *gnm_app, Workbook *wb);
	void (*clipboard_modified) (GnumericApplication *gnm_app);
	
} GnumericApplicationClass;

static GObjectClass *gnumeric_application_parent_class;


static GnumericApplication *app;

GObject *
gnumeric_application_get_app (void)
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
 * application_workbook_list_remove :
 * @wb : 
 *
 * Remove @wb from the application's list of workbooks.
 **/
void
application_workbook_list_add (Workbook *wb)
{
	g_return_if_fail (IS_WORKBOOK (wb));

	app->workbook_list = g_list_prepend (app->workbook_list, wb);
	g_signal_emit (G_OBJECT (app), signals [WORKBOOK_ADDED], 0, wb);
}

/**
 * application_workbook_list_remove :
 * @wb : 
 *
 * Remove @wb from the application's list of workbooks.
 **/
void
application_workbook_list_remove (Workbook *wb)
{
	g_return_if_fail (wb != NULL);

	app->workbook_list = g_list_remove (app->workbook_list, wb);
	g_signal_emit (G_OBJECT (app), signals [WORKBOOK_REMOVED], 0, wb);
}

GList *
application_workbook_list (void)
{
	return app->workbook_list;
}

/**
 * application_clipboard_clear:
 *
 * Clear and free the contents of the clipboard if it is
 * not empty.
 */
void
application_clipboard_clear (gboolean drop_selection)
{
	if (app->clipboard_copied_contents) {
		cellregion_free (app->clipboard_copied_contents);
		app->clipboard_copied_contents = NULL;
	}
	if (app->clipboard_sheet_view != NULL) {
		sv_unant (app->clipboard_sheet_view);

		g_signal_emit (G_OBJECT (app), signals [CLIPBOARD_MODIFIED], 0);

		sv_weak_unref (&(app->clipboard_sheet_view));

		/* Release the selection */
		if (drop_selection) {
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
application_clipboard_unant (void)
{
	if (app->clipboard_sheet_view != NULL)
		sv_unant (app->clipboard_sheet_view);
}

/**
 * application_clipboard_cut_copy:
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
 */
void
application_clipboard_cut_copy (WorkbookControl *wbc, gboolean is_cut,
				SheetView *sv, Range const *area,
				gboolean animate_cursor)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (area != NULL);

	application_clipboard_clear (FALSE);

	if (wb_control_claim_selection (wbc)) {
		Sheet *sheet = sv_sheet (sv);
		app->clipboard_cut_range = *area;
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

gboolean
application_clipboard_is_empty (void)
{
	return app->clipboard_sheet_view == NULL;
}

gboolean
application_clipboard_is_cut (void)
{
	if (app->clipboard_sheet_view != NULL)
		return app->clipboard_copied_contents ? FALSE : TRUE;
	return FALSE;
}

Sheet *
application_clipboard_sheet_get (void)
{
	if (app->clipboard_sheet_view == NULL)
		return NULL;
	return sv_sheet (app->clipboard_sheet_view);
}

SheetView *
application_clipboard_sheet_view_get (void)
{
	return app->clipboard_sheet_view;
}

CellRegion *
application_clipboard_contents_get (void)
{
	return app->clipboard_copied_contents;
}

Range const *
application_clipboard_area_get (void)
{
	/*
	 * Only return the range if the sheet has been set.
	 * The range will still contain data even after
	 * the clipboard has been cleared so we need to be extra
	 * safe and only return a range if there is a valid selection
	 */
	if (app->clipboard_sheet_view != NULL)
		return &app->clipboard_cut_range;
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
	if (0 == strcmp (wb->filename, dat->name)) {
		dat->wb = wb;
		return FALSE;
	}
	return TRUE;
}

Workbook *
application_workbook_get_by_name (char const * const name)
{
	struct wb_name_closure close;
	close.wb = NULL;
	close.name = name;
	application_workbook_foreach (&cb_workbook_name, &close);

	return close.wb;
}

gboolean
application_workbook_foreach (WorkbookCallback cback, gpointer data)
{
	GList *l;

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
application_workbook_get_by_index (int i)
{
	struct wb_index_closure close;
	close.wb = NULL;
	close.index = i;
	application_workbook_foreach (&cb_workbook_index, &close);

	return close.wb;
}

double
application_display_dpi_get (gboolean horizontal)
{
	return horizontal ? gnm_app_prefs->horizontal_dpi : gnm_app_prefs->vertical_dpi;
}

double
application_dpi_to_pixels (void)
{
	return MIN (gnm_app_prefs->horizontal_dpi,
		    gnm_app_prefs->vertical_dpi) / 72.;
}

/**
 * application_history_get_list:
 *
 *  This function returns a pointer to the history list,
 * creating it if necessary.
 *
 * Return value: the list./
 **/
GSList*
application_history_get_list (void)
{
        gint max_entries;

	/* If the list is already populated, return it. */
	if (app->history_list)
		return app->history_list;

	max_entries = gnm_app_prefs->file_history_max;
	app->history_list = gnm_app_prefs->file_history_files;

	while (g_slist_length (app->history_list) > (guint)max_entries) {
		GSList *last = g_slist_last (app->history_list);
		g_free (last->data);
		app->history_list = g_slist_delete_link (app->history_list,
							last);
	}

	return app->history_list;
}

/**
 * application_history_update_list:
 * @filename:
 *
 * This function updates the history list.  The return value is a
 * pointer to the filename that was removed, if the list was already full
 * or NULL if no items were removed.
 *
 * Return value:
 **/
gchar *
application_history_update_list (const gchar *filename)
{
	gchar *name, *old_name = NULL;
	GSList *l = NULL;
	gint max_entries;

	g_return_val_if_fail (filename != NULL, NULL);

	/* Get maximum list length from config */
	max_entries = gnm_app_prefs->file_history_max;

	/* Shorten the list in case max_entries has changed. */
	while (g_slist_length (app->history_list) > (guint) max_entries) {
		GSList *last = g_slist_last (app->history_list);
		g_free (last->data);
		app->history_list = g_slist_delete_link (app->history_list,
							last);
	}

	/* Check if this filename already exists in the list */
	for (l = app->history_list; l ; l = l->next) {
		if (!strcmp ((gchar *)l->data, filename)) {
			old_name = (gchar *)l->data;
			break;
		} 
	}

	/* Insert the new filename to the new list and free up the old list */
	name = g_strdup (filename);
	app->history_list  = g_slist_prepend (app->history_list, name);
	if (l)
		app->history_list = g_slist_delete_link (app->history_list, l);
	if (g_slist_length (app->history_list) > (guint)max_entries) {
		GSList *last = g_slist_last (app->history_list);
		old_name = (gchar *)last->data;
		app->history_list = g_slist_delete_link (app->history_list,
							last);
	}

	gnm_gconf_set_file_history_files (app->history_list);
	gnm_conf_sync ();
	return old_name;
}

/* Remove the last item from the history list and return it. */
gchar *
application_history_list_shrink (void)
{
	gchar *name;
	GSList *l;

	if (app->history_list == NULL)
		return NULL;

	l = g_slist_last (app->history_list);
	name = (gchar *)l->data;
	app->history_list = g_slist_delete_link (app->history_list, l);

	return name;
}

/* Write contents of the history list to the configuration file. */
void
application_history_write_config (void)
{
	gint max_entries;

	if (app->history_list == NULL) return;

	max_entries = gnm_app_prefs->file_history_max;

	/* Shorten the list in case max_entries has changed. */
	while (g_slist_length (app->history_list) > (guint) max_entries) {
		GSList *last = g_slist_last (app->history_list);
		g_free (last->data);
		app->history_list = g_slist_delete_link (app->history_list,
							last);
	}

	gnm_gconf_set_file_history_files (app->history_list);
	gnm_conf_sync ();

	g_slist_foreach (app->history_list, (GFunc)g_free, NULL);
	g_slist_free (app->history_list);
	app->history_list = NULL;
}

gboolean application_use_auto_complete	  (void) { return gnm_app_prefs->auto_complete; }
gboolean application_live_scrolling	  (void) { return gnm_app_prefs->live_scrolling; }
int	 application_auto_expr_recalc_lag (void) { return gnm_app_prefs->recalc_lag; }

GConfClient *
application_get_gconf_client (void) 
{
	if (!app->gconf_client) {
		app->gconf_client = gconf_client_get_default ();
		gconf_client_add_dir (app->gconf_client, "/apps/gnumeric",
				      GCONF_CLIENT_PRELOAD_RECURSIVE,
				      NULL);
	}
	return app->gconf_client; 
}

void         
application_release_gconf_client (void) 
{ 
	if (app->gconf_client) {
		gconf_client_remove_dir (app->gconf_client,
					 "/apps/gnumeric", NULL);
		g_object_unref (G_OBJECT (app->gconf_client));
	}
	app->gconf_client = NULL;
}

gpointer       
application_get_pref_dialog (void)
{
	return app->pref_dialog; 
}

void         
application_set_pref_dialog (gpointer dialog)
{
	app->pref_dialog = dialog;
}

void     
application_release_pref_dialog (void)
{ 
	if (app->pref_dialog)
		gtk_widget_destroy (app->pref_dialog);
}

static void
gnumeric_application_setup_icons (void)
{
	static struct GnumericStockPixmap{
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

		{ NULL,					gnm_comment_add_16,		"Gnumeric_CommentAdd" },
		{ NULL,					gnm_comment_delete_16,		"Gnumeric_CommentDelete" },
		{ NULL,					gnm_comment_edit_16,		"Gnumeric_CommentEdit" },

		{ gnm_add_decimals,			NULL,				"Gnumeric_FormatAddPrecision" },
		{ gnm_remove_decimals,			NULL,				"Gnumeric_FormatRemovePrecision" },
		{ gnm_money,				NULL,				"Gnumeric_FormatAsMoney" },
		{ gnm_percent,				NULL,				"Gnumeric_FormatAsPercent" },
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

		{ gnm_object_button_24,			NULL,				"Gnumeric_ObjectButton" },
		{ gnm_object_checkbox_24,		NULL,				"Gnumeric_ObjectCheckbox" },
		{ gnm_object_combo_24,			NULL,				"Gnumeric_ObjectCombo" },
		{ gnm_object_frame_24,			NULL,				"Gnumeric_ObjectFrame" },
		{ gnm_object_label_24,			NULL,				"Gnumeric_ObjectLabel" },
		{ gnm_object_list_24,			NULL,				"Gnumeric_ObjectList" },
		{ gnm_object_radiobutton_24,		NULL,				"Gnumeric_ObjectRadiobutton" },
		{ gnm_object_scrollbar_24,		NULL,				"Gnumeric_ObjectScrollbar" },

		{ gnm_pivottable_24,	                gnm_pivottable_16,		"Gnumeric_PivotTable" },
		{ gnm_protection_yes,	                NULL,				"Gnumeric_Protection_Yes" },
		{ gnm_protection_no,       		NULL,				"Gnumeric_Protection_No" },

		{ gnm_link_add_24,			gnm_link_add_16,		"Gnumeric_Link_Add" },
		{ NULL,					gnm_link_delete_16,		"Gnumeric_Link_Delete" },
		{ NULL,					gnm_link_edit_16,		"Gnumeric_Link_Edit" },
		{ gnm_link_external_24,			gnm_link_external_16,		"Gnumeric_Link_External" },
		{ gnm_link_internal_24,			gnm_link_internal_16,		"Gnumeric_Link_Internal" },
		{ gnm_link_email_24,			gnm_link_email_16,		"Gnumeric_Link_EMail" },
		{ gnm_link_url_24,			gnm_link_url_16,		"Gnumeric_Link_URL" },
	};
	unsigned i = 0;
	GtkIconFactory *factory = gtk_icon_factory_new ();

	for (i = 0; i < G_N_ELEMENTS (entry) ; i++)
		add_icon (factory, entry[i].scalable_data,
			  entry[i].sized_data, entry[i].stock_id);
	gtk_icon_factory_add_default (factory);
	g_object_unref (G_OBJECT (factory));	
}

static void
gnumeric_application_finalize (GObject *object)
{
	app = NULL;
	G_OBJECT_CLASS (gnumeric_application_parent_class)->finalize (object);
}

static void
gnumeric_application_class_init (GObjectClass *object_class)
{
	gnumeric_application_parent_class = g_type_class_peek (G_TYPE_OBJECT);

	/* Object class method overrides */
	object_class->finalize = gnumeric_application_finalize;

	signals [WORKBOOK_ADDED] = g_signal_new ("workbook_added",
		GNUMERIC_APPLICATION_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnumericApplicationClass, workbook_added),
		(GSignalAccumulator) NULL, NULL,
		gnm__VOID__OBJECT,
		G_TYPE_NONE,
		1, WORKBOOK_TYPE);
	signals [WORKBOOK_REMOVED] = g_signal_new ("workbook_removed",
		GNUMERIC_APPLICATION_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnumericApplicationClass, workbook_removed),
		(GSignalAccumulator) NULL, NULL,
		gnm__VOID__POINTER,
		G_TYPE_NONE,
		1, G_TYPE_POINTER);
	signals [CLIPBOARD_MODIFIED] = g_signal_new ("clipboard_modified",
		GNUMERIC_APPLICATION_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnumericApplicationClass, clipboard_modified),
		(GSignalAccumulator) NULL, NULL,
		gnm__VOID__VOID,
		G_TYPE_NONE,
		0);
}

static void
gnumeric_application_init (GObject *obj)
{
	GnumericApplication *gnm_app = GNUMERIC_APPLICATION (obj);

	gnumeric_application_setup_icons ();

	gnm_app->clipboard_copied_contents = NULL;
	gnm_app->clipboard_sheet_view = NULL;

	gnm_app->gconf_client = NULL;

	gnm_app->workbook_list = NULL;

	app = gnm_app;
}

GSF_CLASS (GnumericApplication, gnumeric_application,
	   gnumeric_application_class_init, gnumeric_application_init,
	   G_TYPE_OBJECT);
