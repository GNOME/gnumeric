#ifndef GNUMERIC_SELECTION_H
#define GNUMERIC_SELECTION_H

#include "gnumeric.h"

typedef enum {
	COL_ROW_NO_SELECTION,
	COL_ROW_PARTIAL_SELECTION,
	COL_ROW_FULL_SELECTION
} ColRowSelectionType;

/* Selection information */
gboolean sv_is_pos_selected         (SheetView const *sv, int col, int row);
gboolean sv_is_range_selected       (SheetView const *sv, Range const *r);
gboolean sv_is_full_range_selected  (SheetView const *sv, Range const *r);
gboolean sv_is_colrow_selected	    (SheetView const *sv,
				    int colrow, gboolean is_col);
gboolean sv_is_full_colrow_selected (SheetView const *sv,
				     gboolean is_cols, int col);
ColRowSelectionType sheet_col_selection_type (SheetView const *sv, int col);
ColRowSelectionType sheet_row_selection_type (SheetView const *sv, int row);

char    *selection_to_string	   (SheetView *sv,
				    gboolean include_sheet_name_prefix);
Range const *selection_first_range (SheetView const *sv,
				    WorkbookControl *wbc, char const *cmd_name);
GSList  *selection_get_ranges	   (SheetView const *sv,
				    gboolean allow_intersection);


/* Selection management */
void	 sv_selection_reset	   (SheetView *sv);
void	 sv_selection_add_pos	   (SheetView *sv, int col, int row);
void	 sv_selection_add_range	   (SheetView *sv,
				    int edit_col, int edit_row,
				    int base_col, int base_row,
				    int move_col, int move_row);
void	sv_selection_set	   (SheetView *sv, CellPos const *edit,
				    int base_col, int base_row,
				    int move_col, int move_row);
void	sv_selection_extend_to	   (SheetView *sv, int col, int row);
void	sv_selection_free	   (SheetView *sv);

void	sv_selection_walk_step	   (SheetView *sv,
				    gboolean forward,
				    gboolean horizontal);

/* Utilities for operating on a selection */
typedef void (*SelectionApplyFunc) (SheetView *sv, Range const *, gpointer closure);

void	 selection_apply	 (SheetView *sv, SelectionApplyFunc const func,
				 gboolean allow_intersection,
				 void *closure);
gboolean selection_foreach_range (SheetView *sv, gboolean from_start,
				  gboolean (*range_cb) (SheetView *sv,
							Range const *range,
							gpointer user_data),
				     gpointer user_data);

#endif /* GNUMERIC_SELECTION_H */
