#ifndef GNUMERIC_SELECTION_H
#define GNUMERIC_SELECTION_H

#include "gnumeric.h"

/* Selection management */
gboolean sv_is_cell_selected       (SheetView const *sheet, int col, int row);
gboolean sv_is_range_selected      (SheetView const *sheet, Range const *r);
gboolean sv_is_full_range_selected (SheetView const *sheet, Range const *r);
gboolean sv_is_colrow_selected	   (SheetView const *sheet,
				    int colrow, gboolean is_col);

void sv_selection_reset		(SheetView *sv);
void sv_selection_add_pos	(SheetView *sv, int col, int row);
void sv_selection_add_range	(SheetView *sv,
				 int edit_col, int edit_row,
				 int base_col, int base_row,
				 int move_col, int move_row);
void sv_selection_set		(SheetView *sv, CellPos const *edit,
				 int base_col, int base_row,
				 int move_col, int move_row);
void sv_selection_extend_to	(SheetView *sv, int col, int row);
void sv_selection_free		(SheetView *sv);

void sv_selection_walk_step	(SheetView *sheet,
				 gboolean forward,
				 gboolean horizontal);


/* User visible actions */
void        sheet_selection_redraw            (SheetView const *sheet);

/* Utilities for operating on a selection */
typedef void (*SelectionApplyFunc) (SheetView *sheet, Range const *, gpointer closure);

void selection_apply (SheetView *sheet, SelectionApplyFunc const func,
		      gboolean allow_intersection,
		      void *closure);
GSList  *selection_get_ranges      (SheetView const *sheet,
				    gboolean allow_intersection);

/* export the selection */
char       *selection_to_string    (SheetView *sheet,
				    gboolean include_sheet_name_prefix);

/* Information about the selection */
Range const *selection_first_range (SheetView const *sv,
				    WorkbookControl *wbc, char const *cmd_name);
gboolean    selection_foreach_range (SheetView *sheet, gboolean from_start,
				     gboolean (*range_cb) (SheetView *sheet,
							   Range const *range,
							   gpointer user_data),
				     gpointer user_data);

typedef enum {
	COL_ROW_NO_SELECTION,
	COL_ROW_PARTIAL_SELECTION,
	COL_ROW_FULL_SELECTION
} ColRowSelectionType;

ColRowSelectionType sheet_col_selection_type  (SheetView const *sheet, int col);
ColRowSelectionType sheet_row_selection_type  (SheetView const *sheet, int row);
gboolean	    sheet_selection_full_cols_rows (SheetView const *sheet,
						    gboolean is_cols, int col);

#endif /* GNUMERIC_SELECTION_H */
