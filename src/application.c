/*
 * application.c: Manage the data common to all workbooks
 *
 * Author:
 *     Jody Goldberg <jgoldberg@home.com>
 *
 */
#include <config.h>
#include "application.h"
#include "clipboard.h"
#include "selection.h"
#include "workbook.h"
#include "workbook-view.h"

typedef struct
{
	/* Clipboard */
	Sheet		* clipboard_sheet;
	CellRegion	* clipboard_copied_contents;
	Range		  clipboard_cut_range;

	/* Display resolution */
	float horizontal_dpi, vertical_dpi;
} GnumericApplication;

static GnumericApplication app;

/**
 * application_init:
 *
 * Initialize the application specific data structures.
 */
void
application_init (void)
{
	gboolean h_was_default = TRUE;
	gboolean v_was_default = TRUE;

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
	app.horizontal_dpi = 96.;
	app.vertical_dpi = 96.;

	gnome_config_push_prefix ("Gnumeric/Screen_Resolution/"); 
	app.horizontal_dpi =
	    gnome_config_get_float_with_default ("Horizontal_dpi=96", 
						 &h_was_default);
	app.vertical_dpi = 
	    gnome_config_get_float_with_default ("Vertical_dpi=96", 
						 &v_was_default);

	if (h_was_default)
		gnome_config_set_float ("Horizontal_dpi", app.horizontal_dpi);
	if (v_was_default)
		gnome_config_set_float ("Vertical_dpi", app.vertical_dpi);
	if (h_was_default || v_was_default)
		gnome_config_sync ();

	gnome_config_pop_prefix ();
}

/**
 * application_clipboard_clear:
 *
 * Clear and free the contents of the clipboard if it is
 * not empty.
 */
void
application_clipboard_clear (void)
{
	if (app.clipboard_copied_contents) {
		clipboard_release (app.clipboard_copied_contents);
		app.clipboard_copied_contents = NULL;
	}
	if (app.clipboard_sheet != NULL) {
		Sheet *sheet = app.clipboard_sheet;

		sheet_selection_unant (sheet);
		workbook_view_set_paste_special_state (sheet->workbook, FALSE);
		app.clipboard_sheet = NULL;

		/* Release the selection */
		(void) gtk_selection_owner_set (NULL,
						GDK_SELECTION_PRIMARY,
						GDK_CURRENT_TIME);
	}
}

void
application_clipboard_unant (void)
{
	if (app.clipboard_sheet != NULL)
		sheet_selection_unant (app.clipboard_sheet);
}

static gboolean
application_set_selected_sheet (Sheet *sheet)
{
	g_return_val_if_fail (sheet != NULL, FALSE);

	application_clipboard_clear ();

	if (gtk_selection_owner_set (sheet->workbook->toplevel,
				     GDK_SELECTION_PRIMARY,
				     GDK_CURRENT_TIME)) {
		app.clipboard_sheet = sheet;
		return TRUE;
	}

	g_warning ("Unable to set selection ?");

	return FALSE;
}

/**
 * application_clipboard_copy:
 *
 * @sheet : The source sheet for the copy.
 * @area  : A single rectangular range to be copied.
 *
 * Clear and free the contents of the clipboard and COPY the designated region
 * into the clipboard.
 */
void
application_clipboard_copy (Sheet *sheet, Range const *area)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (area != NULL);

	if (application_set_selected_sheet (sheet) ) {
		app.clipboard_cut_range = *area;
		app.clipboard_copied_contents = 
		    clipboard_copy_cell_range (sheet,
					       area->start.col, area->start.row,
					       area->end.col,   area->end.row);

		workbook_view_set_paste_special_state (sheet->workbook, TRUE);

		sheet_selection_ant (sheet);
	}
}

/**
 * application_clipboard_cut:
 *
 * @sheet : The source sheet for the copy.
 * @area  : A single rectangular range to be cut.
 *
 * Clear and free the contents of the clipboard and save the sheet and area
 * to be cut.  DO NOT ACTUALLY CUT!  Paste will move the region if this was a
 * cut operation.
 */
void
application_clipboard_cut (Sheet *sheet, Range const *area)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (area != NULL);

	if (application_set_selected_sheet (sheet) ) {
		app.clipboard_cut_range = *area;

		/* No paste special for copies */
		workbook_view_set_paste_special_state (sheet->workbook, FALSE);

		sheet_selection_ant (sheet);
	}
}

gboolean
application_clipboard_is_empty (void)
{
	return app.clipboard_sheet == NULL;
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
	workbook_foreach (&cb_workbook_name, &close);

	return close.wb;
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
	workbook_foreach (&cb_workbook_index, &close);

	return close.wb;
}

float
application_display_dpi_get (gboolean const horizontal)
{
    return horizontal ? app.horizontal_dpi : app.vertical_dpi;
}

void
application_display_dpi_set (gboolean const horizontal, float const val)
{
    if (horizontal)
	    app.horizontal_dpi  = val;
    else
	    app.vertical_dpi = val;
}

