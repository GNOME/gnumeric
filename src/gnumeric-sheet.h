#ifndef GNUMERIC_GNUMERIC_SHEET_H
#define GNUMERIC_GNUMERIC_SHEET_H

#include "gui-gnumeric.h"

#define GNUMERIC_TYPE_SHEET     (gnumeric_sheet_get_type ())
#define GNUMERIC_SHEET(obj)     (GTK_CHECK_CAST((obj), GNUMERIC_TYPE_SHEET, GnumericSheet))
#define GNUMERIC_SHEET_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), GNUMERIC_TYPE_SHEET))
#define GNUMERIC_IS_SHEET(o)    (GTK_CHECK_TYPE((o), GNUMERIC_TYPE_SHEET))

#define GNUMERIC_SHEET_FACTOR_X 1000000
#define GNUMERIC_SHEET_FACTOR_Y 2000000

/* FIXME : standardize names (gnm_canvas_ ?) */
struct _GnumericSheet {
	GnomeCanvas   canvas;

	SheetControlGUI *scg;
	GnumericPane *pane;

	struct {
		int first, last_full, last_visible;
	} row, col, row_offset, col_offset;

	ItemGrid      *item_grid;
	ItemEdit      *item_editor;
	ItemCursor    *item_cursor;
	ItemCursor    *sel_cursor;

	GnomeCanvasGroup *anted_group;
	GnomeCanvasGroup *object_group;

	/* Input context for dead key support */
	GdkIC     *ic;
	GdkICAttr *ic_attr;
};

GtkType        gnumeric_sheet_get_type (void);
GnumericSheet *gnumeric_sheet_new      (SheetControlGUI *scg, GnumericPane *pane);

int  gnumeric_sheet_find_col	 (GnumericSheet *gsheet, int x, int *col_origin);
int  gnumeric_sheet_find_row	 (GnumericSheet *gsheet, int y, int *row_origin);

void gnumeric_sheet_create_editor  (GnumericSheet *gsheet);
void gnumeric_sheet_stop_editing   (GnumericSheet *gsheet);
void gnumeric_sheet_cursor_bound   (GnumericSheet *gsheet, Range const *r);
void gnumeric_sheet_rangesel_bound (GnumericSheet *gsheet, Range const *r);
void gnumeric_sheet_rangesel_start (GnumericSheet *gsheet, int col, int row);
void gnumeric_sheet_rangesel_stop  (GnumericSheet *gsheet);

void gsheet_compute_visible_region    (GnumericSheet *gsheet,
				       gboolean const full_recompute);

void gnumeric_sheet_redraw_region  (GnumericSheet *gsheet,
				    int start_col, int start_row,
				    int end_col, int end_row);

#endif /* GNUMERIC_GNUMERIC_SHEET_H */
