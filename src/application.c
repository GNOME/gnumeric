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
#include "workbook-view.h"

typedef struct
{
	/* Clipboard */
	Sheet		* clipboard_sheet;
	CellRegion	* clipboard_copied_contents;
	Range		  clipboard_cut_range;
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
	app.clipboard_copied_contents = NULL;
	app.clipboard_sheet = NULL;
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
		workbook_view_set_paste_state (sheet->workbook, 0);
		app.clipboard_sheet = NULL;
	}
}

void
application_clipboard_unant (void)
{
	if (app.clipboard_sheet != NULL)
		sheet_selection_unant (app.clipboard_sheet);
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
	application_clipboard_clear ();

	app.clipboard_sheet = sheet;
	app.clipboard_cut_range = *area;
	app.clipboard_copied_contents = 
	    clipboard_copy_cell_range (sheet,
				       area->start.col, area->start.row,
				       area->end.col,   area->end.row);

	workbook_view_set_paste_state (sheet->workbook,
				       WORKBOOK_VIEW_PASTE_ITEM |
				       WORKBOOK_VIEW_PASTE_SPECIAL_ITEM);

	sheet_selection_ant (sheet);
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
	application_clipboard_clear ();

	app.clipboard_sheet = sheet;
	app.clipboard_cut_range = *area;

	/* No paste special for copies */
	workbook_view_set_paste_state (sheet->workbook, WORKBOOK_VIEW_PASTE_ITEM);

	sheet_selection_ant (sheet);
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
	if (app.clipboard_sheet)
		return &app.clipboard_cut_range;
	return NULL;
}
