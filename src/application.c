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
#include "sheet-private.h"
#include "auto-correct.h"
#include "pixmaps/gnumeric-stock-pixbufs.h"

#include <gtk/gtk.h>
#include <libgnome/gnome-config.h>

typedef struct
{
	/* Clipboard */
	Sheet		*clipboard_sheet;
	CellRegion	*clipboard_copied_contents;
	Range		 clipboard_cut_range;

	/* Display resolution */
	double           horizontal_dpi, vertical_dpi;

	/* History for file menu */
	GList           *history_list;

	gboolean         edit_auto_complete;
	gboolean         live_scrolling;
	int         	 auto_expr_recalc_lag;
} GnumericApplication;

static GnumericApplication app;

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
 * application_init:
 *
 * Initialize the application specific data structures.
 * and register some extra stock icons.
 */
void
application_init (void)
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
		{ gnm_function_selector,		NULL,				"Gnumeric_FormulaGuru" },
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
	};
	unsigned i = 0;
	GtkIconFactory *factory = gtk_icon_factory_new ();

	for (i = 0; i < G_N_ELEMENTS (entry) ; i++)
		add_icon (factory, entry[i].scalable_data,
			  entry[i].sized_data, entry[i].stock_id);
	gtk_icon_factory_add_default (factory);
	g_object_unref (G_OBJECT (factory));

	app.clipboard_copied_contents = NULL;
	app.clipboard_sheet = NULL;

	/* FIXME : 96 as the default will scale Helvetica-9 to Helvetica-12
	 * which is not too ugly.  Ideally we could get the correct values here but,
	 *
	 * XFree86-3) lies.  It defaults to 75x75 dpi and only allows the user to
	 *            specify one resolution, which it uses for both axis.
	 * XFree86-4) Makes a better guess, but still seems to use the same
	 *            resolution for both directions.
	 *
	 * XL seems to assume that everything is 96 dpi.
	 *
	 * I'll leave it as is for now, and revisit the solution when we shake
	 * out the flaws in the display code.
	 */
	gnome_config_push_prefix ("Gnumeric/Screen_Resolution/");
	app.horizontal_dpi = gnome_config_get_float ("Horizontal_dpi=96");
	app.vertical_dpi = gnome_config_get_float ("Vertical_dpi=96");
	gnome_config_pop_prefix ();

	gnome_config_push_prefix ("Gnumeric/Editing/");
	app.edit_auto_complete	= gnome_config_get_bool ("AutoComplete=true");
	app.live_scrolling	= gnome_config_get_bool ("LiveScrolling=true");

	/* If positive auto expressions are recalculated within <lag>
	 * millesecond after a change.
	 * if negative they are recalculated with <lag> milleseconds after the
	 * last change where 'last' is defined as a periond > <lag> after a
	 * change with no changes.
	 */
	app.auto_expr_recalc_lag = gnome_config_get_int ("AutoExprRecalcLag=200");
	gnome_config_pop_prefix ();

	autocorrect_init ();
}

static GList *workbook_list = NULL;
void
application_workbook_list_add (Workbook *wb)
{
	workbook_list = g_list_prepend (workbook_list, wb);
}
void
application_workbook_list_remove (Workbook *wb)
{
	workbook_list = g_list_remove (workbook_list, wb);
}
GList *
application_workbook_list (void)
{
	return workbook_list;
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
	if (app.clipboard_copied_contents) {
		cellregion_free (app.clipboard_copied_contents);
		app.clipboard_copied_contents = NULL;
	}
	if (app.clipboard_sheet != NULL) {
		Sheet *sheet = app.clipboard_sheet;

		sheet_unant (sheet);

		sheet->priv->enable_paste_special = FALSE;
		WORKBOOK_FOREACH_CONTROL (sheet->workbook, view, control,
			wb_control_menu_state_update (control, sheet, MS_PASTE_SPECIAL););

		app.clipboard_sheet = NULL;

		/* Release the selection */
		if (drop_selection)
			gtk_selection_owner_set (NULL,
						 GDK_SELECTION_PRIMARY,
						 GDK_CURRENT_TIME);
	}
}

void
application_clipboard_unant (void)
{
	if (app.clipboard_sheet != NULL)
		sheet_unant (app.clipboard_sheet);
}

static gboolean
application_set_selected_sheet (WorkbookControl *wbc, Sheet *sheet)
{
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	application_clipboard_clear (FALSE);

	/* FIXME : how to do this on a per display basis ? */
	if (wb_control_claim_selection (wbc)) {
		app.clipboard_sheet = sheet;
		return TRUE;
	}

	g_warning ("Unable to set selection ?");

	return FALSE;
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
 */
void
application_clipboard_cut_copy (WorkbookControl *wbc, gboolean is_cut,
				Sheet *sheet, Range const *area,
				gboolean animate_cursor)
{
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (area != NULL);

	if (application_set_selected_sheet (wbc, sheet) ) {
		app.clipboard_cut_range = *area;

		sheet->priv->enable_paste_special = !is_cut;
		if (!is_cut)
			app.clipboard_copied_contents =
				clipboard_copy_range (sheet, area);

		WORKBOOK_FOREACH_CONTROL (sheet->workbook, view, control,
			wb_control_menu_state_update (control, sheet, MS_PASTE_SPECIAL););

		if (animate_cursor) {
			/* * The 'area' and the list itself will be copied
			 * entirely. We ant the copied range on the sheet.
			 */
			GList *l = g_list_append (NULL, (Range *) area);
			sheet_ant (sheet, l);
			g_list_free (l);
		}
	}
}

gboolean
application_clipboard_is_empty (void)
{
	return app.clipboard_sheet == NULL;
}

gboolean
application_clipboard_is_cut (void)
{
	if (app.clipboard_sheet != NULL)
		return app.clipboard_copied_contents ? FALSE : TRUE;
	return FALSE;
}

Sheet *
application_clipboard_sheet_get (void)
{
	return app.clipboard_sheet;
}

CellRegion *
application_clipboard_contents_get (void)
{
	return app.clipboard_copied_contents;
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
	if (app.clipboard_sheet != NULL)
		return &app.clipboard_cut_range;
	return NULL;
}

struct wb_name_closure
{
	Workbook *wb;
	char const * name;
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

	for (l = workbook_list; l; l = l->next){
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
cb_workbook_index (Workbook * wb, gpointer closure)
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
	return horizontal ? app.horizontal_dpi : app.vertical_dpi;
}

void
application_display_dpi_set (gboolean horizontal, double val)
{
	if (horizontal)
		app.horizontal_dpi  = val;
	else
		app.vertical_dpi = val;
}

double
application_dpi_to_pixels (void)
{
	return MIN (application_display_dpi_get (FALSE),
		    application_display_dpi_get (TRUE)) / 72.;
}

/**
 * application_history_get_list:
 *
 *  This function returns a pointer to the history list,
 * creating it if neccessary.
 *
 * Return value: the list./
 **/
GList*
application_history_get_list (void)
{
        gchar *filename, *key;
        gint max_entries, i;
	gboolean do_set = FALSE;

	/* If the list is already populated, return it. */
	if (app.history_list)
		return app.history_list;

        gnome_config_push_prefix ("/Gnumeric/History/");

        /* Get maximum number of history entries.  Write default value to
	 * config file if no entry exists. */
        max_entries = gnome_config_get_int_with_default ("MaxFiles=4", &do_set);
	if (do_set)
		gnome_config_set_int ("MaxFiles", 4);

	/* Read the history filenames from the config file */
        for (i = 0; i < max_entries; i++) {
		key = g_strdup_printf ("File%d", i);
 	        filename = gnome_config_get_string (key);
 	        if (filename == NULL) {
                       /* Ran out of filenames. */
                       g_free (key);
                       break;
		}
		app.history_list = g_list_append (app.history_list, filename);
               	g_free (key);
       	}
       	gnome_config_pop_prefix ();

	return app.history_list;
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
	GList *l = NULL;
	GList *new_list = NULL;
	gint max_entries, count = 0;
	gboolean do_set = FALSE;
	gboolean found = FALSE;

	g_return_val_if_fail (filename != NULL, NULL);

	/* Get maximum list length from config */
	gnome_config_push_prefix ("Gnumeric/History/");
	max_entries = gnome_config_get_int_with_default ("MaxFiles=4", &do_set);
	if (do_set)
		gnome_config_set_int ("MaxFiles", max_entries);
	gnome_config_pop_prefix ();

	/* Check if this filename already exists in the list */
	for (l = app.history_list; l && (count < max_entries); l = l->next) {

		if (!found && (!strcmp ((gchar *)l->data, filename) ||
			       (count == max_entries - 1))) {
			/* This is either the last item in the list, or a
			 * duplicate of the requested entry. */
			old_name = (gchar *)l->data;
			found = TRUE;
		} else  /* Add this item to the new list */
			new_list = g_list_append (new_list, l->data);

		count++;
	}

	/* Insert the new filename to the new list and free up the old list */
	name = g_strdup (filename);
	new_list = g_list_prepend (new_list, name);
	g_list_free (app.history_list);
	app.history_list = new_list;

	return old_name;
}

/* Remove the last item from the history list and return it. */
gchar *
application_history_list_shrink (void)
{
	gchar *name;
	GList *l;

	if (app.history_list == NULL)
		return NULL;

	l = g_list_last (app.history_list);
	name = (gchar *)l->data;
	app.history_list = g_list_remove (app.history_list, name);

	return name;
}

/* Write contents of the history list to the configuration file. */
void
application_history_write_config (void)
{
	gchar *key;
	GList *l;
	gint max_entries, i = 0;

	if (app.history_list == NULL) return;

	max_entries = gnome_config_get_int ("Gnumeric/History/MaxFiles=4");
	gnome_config_clean_section ("Gnumeric/History");
	gnome_config_push_prefix("Gnumeric/History/");
	gnome_config_set_int ("MaxFiles", max_entries);

	for (l = app.history_list; l; l = l->next) {
		key = g_strdup_printf ("File%d", i++);
		gnome_config_set_string (key, (gchar *)l->data);
		g_free (l->data);
		g_free (key);
	}

	gnome_config_sync ();
	gnome_config_pop_prefix ();
	g_list_free (app.history_list);
	app.history_list = NULL;
}

gboolean application_use_auto_complete	  (void) { return app.edit_auto_complete; }
gboolean application_live_scrolling	  (void) { return app.live_scrolling; }
int	 application_auto_expr_recalc_lag (void) { return app.auto_expr_recalc_lag; }
