#ifndef GNUMERIC_SELECTION_H
#define GNUMERIC_SELECTION_H

#include "gnumeric.h"

/* Selection management */
gboolean    sheet_is_all_selected        (Sheet const * const sheet);
gboolean    sheet_is_cell_selected       (Sheet const * const sheet, int col, int row);
gboolean    sheet_is_range_selected      (Sheet const * const sheet, Range const *r);
void        sheet_selection_extend_to    (Sheet *sheet, int col, int row);
void        sheet_selection_extend       (Sheet *sheet, int count, gboolean jump_to_boundaries,
					  gboolean const horizontal);
void	    sheet_selection_set		 (Sheet *sheet,
					  int edit_col, int edit_row,
					  int base_col, int base_row,
					  int move_col, int move_row);
void        sheet_selection_add          (Sheet *sheet, int col, int row);
void        sheet_selection_add_range    (Sheet *sheet,
					  int edit_col, int edit_row,
					  int base_col, int base_row,
					  int move_col, int move_row);

void        sheet_selection_reset_only   (Sheet *sheet);
void        sheet_selection_free         (Sheet *sheet);
void        sheet_cell_list_free         (CellList *cell_list);
char       *sheet_selection_to_string    (Sheet *sheet, gboolean include_sheet_name_prefix);

/* Cut/Copy/Paste on the workbook selection */
gboolean    sheet_selection_copy              (CommandContext *context, Sheet *sheet);
gboolean    sheet_selection_cut               (CommandContext *context, Sheet *sheet);

void        sheet_selection_walk_step         (Sheet *sheet,
					       gboolean const forward,
					       gboolean const horizontal);

gboolean    selection_contains_colrow         (Sheet *sheet, int colrow, gboolean is_col);

/* User visible actions */
void        sheet_selection_ant               (Sheet *sheet);
void        sheet_selection_unant             (Sheet *sheet);

/* Utilities for operating on a selection */
typedef void (*SelectionApplyFunc) (Sheet *sheet, 
				    int start_col, int start_row,
				    int end_col,   int end_row,
				    void *closure);

void selection_apply (Sheet *sheet, SelectionApplyFunc const func,
		      gboolean allow_intersection,
		      void *closure);
GSList * selection_get_ranges (Sheet * sheet, gboolean const allow_intersection);
gboolean selection_check_for_array (Sheet const * sheet, GSList const *selection);

/* export the selection */
CellList   *selection_to_list      (Sheet *sheet, gboolean allow_intersection);
char       *selection_to_string    (Sheet *sheet,
				    gboolean include_sheet_name_prefix);

/* Information about the selection */
gboolean      selection_is_simple   (CommandContext *context, Sheet const *sheet,
				     char const *command_name);
Range const * selection_first_range (Sheet const *sheet, gboolean const permit_complex);
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
gboolean	    sheet_selection_full_cols (Sheet const *sheet);
gboolean	    sheet_selection_full_rows (Sheet const *sheet);

#endif /* GNUMERIC_SELECTION_H */
