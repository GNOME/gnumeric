/*
 * application.c: Manage the data common to all workbooks
 *
 * Author:
 *     Jody Goldberg <jgoldberg@home.com>
 *
 */
#include <config.h>
#include <gtk/gtk.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-config.h>
#include <libgnomeui/gnome-stock.h>
#include "application.h"
#include "clipboard.h"
#include "selection.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook.h"
#include "sheet.h"
#include "sheet-private.h"

#include "pixmaps/menu-print-preview.xpm"
#include "pixmaps/print-preview.xpm"
#include "pixmaps/sort-descending.xpm"
#include "pixmaps/auto-sum.xpm"
#include "pixmaps/equal-sign.xpm"
#include "pixmaps/function_selector.xpm"
#include "pixmaps/graphic.xpm"
#include "pixmaps/insert-bonobo-component.xpm"

#include "pixmaps/label.xpm"
#include "pixmaps/frame.xpm"
#include "pixmaps/button.xpm"
#include "pixmaps/checkbutton.xpm"
#include "pixmaps/radiobutton.xpm"
#include "pixmaps/list.xpm"
#include "pixmaps/combo.xpm"

#include "pixmaps/rect.xpm"
#include "pixmaps/line.xpm"
#include "pixmaps/arrow.xpm"
#include "pixmaps/oval.xpm"

#include "pixmaps/money.xpm"
#include "pixmaps/percent.xpm"
#include "pixmaps/thousands.xpm"
#include "pixmaps/add_decimals.xpm"
#include "pixmaps/remove_decimals.xpm"

#include "pixmaps/16_save.xpm"
#include "pixmaps/16_save_as.xpm"
#include "pixmaps/16_search.xpm"
#include "pixmaps/16_search_and_replace.xpm"
#include "pixmaps/16_cut.xpm"
#include "pixmaps/16_copy.xpm"
#include "pixmaps/16_paste.xpm"
#include "pixmaps/16_undo.xpm"
#include "pixmaps/16_redo.xpm"
#include "pixmaps/16_add_column.xpm"
#include "pixmaps/16_add_comment.xpm"
#include "pixmaps/16_add_row.xpm"
#include "pixmaps/16_cell_height.xpm"
#include "pixmaps/16_cell_width.xpm"
#include "pixmaps/16_center_across_selection.xpm"
#include "pixmaps/16_delete_column.xpm"
#include "pixmaps/16_delete_comment.xpm"
#include "pixmaps/16_delete_row.xpm"
#include "pixmaps/16_edit_comment.xpm"
#include "pixmaps/16_hide_column.xpm"
#include "pixmaps/16_hide_row.xpm"
#include "pixmaps/16_insert_shaped_object.xpm"
#include "pixmaps/16_merge_cells.xpm"
#include "pixmaps/16_split_cells.xpm"
#include "pixmaps/16_sort_ascending.xpm"
#include "pixmaps/16_unhide_column.xpm"
#include "pixmaps/16_unhide_row.xpm"
#include "pixmaps/16_group.xpm"
#include "pixmaps/16_ungroup.xpm"
#include "pixmaps/16_show_detail.xpm"
#include "pixmaps/16_hide_detail.xpm"

#include "pixmaps/24_save.xpm"
#include "pixmaps/24_cut.xpm"
#include "pixmaps/24_copy.xpm"
#include "pixmaps/24_paste.xpm"
#include "pixmaps/24_undo.xpm"
#include "pixmaps/24_redo.xpm"
#include "pixmaps/24_add_column.xpm"
#include "pixmaps/24_add_row.xpm"
#include "pixmaps/24_cell_height.xpm"
#include "pixmaps/24_cell_width.xpm"
#include "pixmaps/24_center_across_selection.xpm"
#include "pixmaps/24_delete_column.xpm"
#include "pixmaps/24_delete_row.xpm"
#include "pixmaps/24_hide_column.xpm"
#include "pixmaps/24_hide_row.xpm"
#include "pixmaps/24_insert_shaped_object.xpm"
#include "pixmaps/24_merge_cells.xpm"
#include "pixmaps/24_split_cells.xpm"
#include "pixmaps/24_sort_ascending.xpm"
#include "pixmaps/24_unhide_column.xpm"
#include "pixmaps/24_unhide_row.xpm"
#include "pixmaps/24_group.xpm"
#include "pixmaps/24_ungroup.xpm"
#include "pixmaps/24_show_detail.xpm"
#include "pixmaps/24_hide_detail.xpm"

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
		int width, height;
		char const * const name;
		gchar **xpm_data;
	} const entry_names [] = {
		{ 16, 16, "Menu_Gnumeric_Save", i16_save_xpm },
		{ 16, 16, "Menu_Gnumeric_SaveAs", i16_save_as_xpm },
		{ 16, 16, "Menu_Gnumeric_Search", i16_search },
		{ 16, 16, "Menu_Gnumeric_SearchAndReplace", i16_search_and_replace },
		{ 16, 16, "Menu_Gnumeric_PrintPreview", menu_print_preview_xpm },
		{ 16, 16, "Menu_Gnumeric_InsertShapedComponent", i16_insert_shaped_object_xpm },

		{ 16, 16, "Menu_Gnumeric_Cut", i16_cut_xpm },
		{ 16, 16, "Menu_Gnumeric_Copy", i16_copy_xpm },
		{ 16, 16, "Menu_Gnumeric_Paste", i16_paste_xpm },
		{ 16, 16, "Menu_Gnumeric_Undo", i16_undo_xpm },
		{ 16, 16, "Menu_Gnumeric_Redo", i16_redo_xpm },
		{ 16, 16, "Menu_Gnumeric_CommentAdd", i16_add_comment_xpm },
		{ 16, 16, "Menu_Gnumeric_CommentDelete", i16_delete_comment_xpm },
		{ 16, 16, "Menu_Gnumeric_CommentEdit", i16_edit_comment_xpm },
		{ 16, 16, "Menu_Gnumeric_ColumnAdd", i16_add_column_xpm },
		{ 16, 16, "Menu_Gnumeric_ColumnDelete", i16_delete_column_xpm },
		{ 16, 16, "Menu_Gnumeric_ColumnSize", i16_cell_width_xpm },
		{ 16, 16, "Menu_Gnumeric_ColumnHide", i16_hide_column_xpm },
		{ 16, 16, "Menu_Gnumeric_ColumnUnhide", i16_unhide_column_xpm },
		{ 16, 16, "Menu_Gnumeric_RowAdd", i16_add_row_xpm },
		{ 16, 16, "Menu_Gnumeric_RowDelete", i16_delete_row_xpm },
		{ 16, 16, "Menu_Gnumeric_RowSize", i16_cell_height_xpm },
		{ 16, 16, "Menu_Gnumeric_RowHide", i16_hide_row_xpm },
		{ 16, 16, "Menu_Gnumeric_RowUnhide", i16_unhide_row_xpm },
		{ 16, 16, "Menu_Gnumeric_MergeCells", i16_merge_cells_xpm },
		{ 16, 16, "Menu_Gnumeric_SplitCells", i16_split_cells_xpm },
		{ 16, 16, "Menu_Gnumeric_SortAscending", i16_sort_ascending_xpm },
		{ 16, 16, "Menu_Gnumeric_CenterAcrossSelection", i16_center_across_selection_xpm },
		{ 16, 16, "Menu_Gnumeric_ShowDetail", i16_show_detail_xpm },
		{ 16, 16, "Menu_Gnumeric_HideDetail", i16_hide_detail_xpm },
		{ 16, 16, "Menu_Gnumeric_Group", i16_group_xpm },
		{ 16, 16, "Menu_Gnumeric_Ungroup", i16_ungroup_xpm },

		{ 24, 24, "Gnumeric_PrintPreview", print_preview_xpm },
		{ 24, 24, "Gnumeric_InsertShapedComponent", i24_insert_shaped_object_xpm },
#if 0
		{ 24, 24, "Gnumeric_CommentAdd", i24_add_comment_xpm },
		{ 24, 24, "Gnumeric_CommentDelete", i24_delete_comment_xpm },
		{ 24, 24, "Gnumeric_CommentEdit", i24_edit_comment_xpm },
#endif
		{ 24, 24, "Gnumeric_Save", i24_save_xpm },
		{ 24, 24, "Gnumeric_Cut", i24_cut_xpm },
		{ 24, 24, "Gnumeric_Copy", i24_copy_xpm },
		{ 24, 24, "Gnumeric_Paste", i24_paste_xpm },
		{ 24, 24, "Gnumeric_Undo", i24_undo_xpm },
		{ 24, 24, "Gnumeric_Redo", i24_redo_xpm },
		{ 24, 24, "Gnumeric_ColumnAdd", i24_add_column_xpm },
		{ 24, 24, "Gnumeric_ColumnDelete", i24_delete_column_xpm },
		{ 24, 24, "Gnumeric_ColumnSize", i24_cell_width_xpm },
		{ 24, 24, "Gnumeric_ColumnHide", i24_hide_column_xpm },
		{ 24, 24, "Gnumeric_ColumnUnhide", i24_unhide_column_xpm },
		{ 24, 24, "Gnumeric_RowAdd", i24_add_row_xpm },
		{ 24, 24, "Gnumeric_RowDelete", i24_delete_row_xpm },
		{ 24, 24, "Gnumeric_RowSize", i24_cell_height_xpm },
		{ 24, 24, "Gnumeric_RowHide", i24_hide_row_xpm },
		{ 24, 24, "Gnumeric_RowUnhide", i24_unhide_row_xpm },
		{ 24, 24, "Gnumeric_MergeCells", i24_merge_cells_xpm },
		{ 24, 24, "Gnumeric_SplitCells", i24_split_cells_xpm },
		{ 24, 24, "Gnumeric_SortAscending", i24_sort_ascending_xpm },
		{ 24, 24, "Gnumeric_CenterAcrossSelection", i24_center_across_selection_xpm },
		{ 24, 24, "Gnumeric_ShowDetail", i24_show_detail_xpm },
		{ 24, 24, "Gnumeric_HideDetail", i24_hide_detail_xpm },
		{ 24, 24, "Gnumeric_Group", i24_group_xpm },
		{ 24, 24, "Gnumeric_Ungroup", i24_ungroup_xpm },

		{ 24, 24, "Gnumeric_SortDescending", sort_descending_xpm },
		{ 24, 24, "Gnumeric_AutoSum", auto_sum_xpm },
		{ 24, 24, "Gnumeric_EqualSign", equal_sign_xpm },
		{ 24, 24, "Gnumeric_FormulaGuru", formula_guru_xpm },
		{ 24, 24, "Gnumeric_GraphGuru", graph_guru_xpm },
		{ 24, 24, "Gnumeric_InsertComponent", insert_component_xpm },

		{ 24, 21, "Gnumeric_FormatAsMoney", money_xpm },
		{ 24, 21, "Gnumeric_FormatAsPercent", percent_xpm },
		{ 24, 21, "Gnumeric_FormatThousandSeperator", thousands_xpm },
		{ 24, 21, "Gnumeric_FormatAddPrecision", add_decimals_xpm },
		{ 24, 21, "Gnumeric_FormatRemovePrecision", remove_decimals_xpm },

		{ 21, 21, "Gnumeric_Label", label_xpm },
		{ 21, 21, "Gnumeric_Frame", frame_xpm },
		{ 21, 21, "Gnumeric_Button", button_xpm },
		{ 21, 21, "Gnumeric_Checkbutton", checkbutton_xpm },
		{ 21, 21, "Gnumeric_Radiobutton", radiobutton_xpm },
		{ 21, 21, "Gnumeric_List", list_xpm },
		{ 21, 21, "Gnumeric_Combo", combo_xpm },
		{ 21, 21, "Gnumeric_Line", line_xpm },
		{ 21, 21, "Gnumeric_Arrow", arrow_xpm },
		{ 21, 21, "Gnumeric_Rectangle", rect_xpm },
		{ 21, 21, "Gnumeric_Oval", oval_xpm },

		{ 0, 0, NULL, NULL}
	};
	static GnomeStockPixmapEntry entry[sizeof(entry_names)/sizeof(struct GnumericStockPixmap)-1];

	int i = 0;

	for (i = 0; entry_names[i].name != NULL ; i++) {
		entry[i].data.type = GNOME_STOCK_PIXMAP_TYPE_DATA;
		entry[i].data.width = entry_names[i].width;
		entry[i].data.height = entry_names[i].height;
		entry[i].data.xpm_data = entry_names[i].xpm_data;
		gnome_stock_pixmap_register (entry_names[i].name,
					     GNOME_STOCK_PIXMAP_REGULAR, entry + i);
	}

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
