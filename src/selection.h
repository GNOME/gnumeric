#ifndef GNUMERIC_SELECTION_H
#define GNUMERIC_SELECTION_H

#include "gnumeric.h"

/* Selection management */
gboolean    sheet_is_all_selected        (Sheet const *sheet);
gboolean    sheet_is_cell_selected       (Sheet const *sheet, int col, int row);
gboolean    sheet_is_range_selected      (Sheet const *sheet, Range const *r);
gboolean    sheet_is_full_range_selected (Sheet const *sheet, Range const *r);
void        sheet_selection_extend_to    (Sheet *sheet, int col, int row);
void	    sheet_selection_set		 (Sheet *sheet,
					  int edit_col, int edit_row,
					  int base_col, int base_row,
					  int move_col, int move_row);
void        sheet_selection_add          (Sheet *sheet, int col, int row);
void        sheet_selection_add_range    (Sheet *sheet,
					  int edit_col, int edit_row,
					  int base_col, int base_row,
					  int move_col, int move_row);

void        sheet_selection_reset        (Sheet *sheet);
void        sheet_selection_free         (Sheet *sheet);

/* Cut/Copy/Paste on the workbook selection */
gboolean    sheet_selection_copy              (WorkbookControl *context, Sheet *sheet);
gboolean    sheet_selection_cut               (WorkbookControl *context, Sheet *sheet);

void        sheet_selection_walk_step         (Sheet *sheet,
					       gboolean forward,
					       gboolean horizontal);

gboolean    selection_contains_colrow         (Sheet const *sheet,
					       int colrow, gboolean is_col);

/* User visible actions */
void        sheet_selection_redraw            (Sheet const *sheet);

/* Utilities for operating on a selection */
typedef void (*SelectionApplyFunc) (Sheet *sheet, Range const *, gpointer closure);

void selection_apply (Sheet *sheet, SelectionApplyFunc const func,
		      gboolean allow_intersection,
		      void *closure);
GSList  *selection_get_ranges      (Sheet const *sheet,
				    gboolean allow_intersection);

/* export the selection */
char       *selection_to_string    (Sheet *sheet,
				    gboolean include_sheet_name_prefix);

/* Information about the selection */
Range const *selection_first_range (Sheet const *sheet,
				    WorkbookControl *wbc, char const *cmd_name);
gboolean    selection_foreach_range (Sheet *sheet, gboolean from_start,
				     gboolean (*range_cb) (Sheet *sheet,
							   Range const *range,
							   gpointer user_data),
				     gpointer user_data);

typedef enum {
	COL_ROW_NO_SELECTION,
	COL_ROW_PARTIAL_SELECTION,
	COL_ROW_FULL_SELECTION
} ColRowSelectionType;

ColRowSelectionType sheet_col_selection_type  (Sheet const *sheet, int col);
ColRowSelectionType sheet_row_selection_type  (Sheet const *sheet, int row);
gboolean	    sheet_selection_full_cols_rows (Sheet const *sheet,
						    gboolean is_cols, int col);

#endif /* GNUMERIC_SELECTION_H */
