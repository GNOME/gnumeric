#ifndef GNUMERIC_SELECTION_H
#define GNUMERIC_SELECTION_H

#include "sheet.h"


/* Selection management */
void        sheet_select_all             (Sheet *sheet);
int         sheet_is_all_selected        (Sheet *sheet);
void        sheet_selection_append       (Sheet *sheet, int col, int row);
void        sheet_selection_extend_to    (Sheet *sheet, int col, int row);
void	    sheet_selection_set		 (Sheet *sheet,
					  int start_col, int start_row,
					  int end_col, int end_row);
void        sheet_selection_reset        (Sheet *sheet);
void        sheet_selection_reset_only   (Sheet *sheet);
int         sheet_selection_equal        (SheetSelection *a, SheetSelection *b);
void        sheet_selection_append_range (Sheet *sheet,
					  int base_col,  int base_row,
					  int start_col, int start_row,
					  int end_col,   int end_row);
int         sheet_selection_first_range  (Sheet *sheet,
					  int *base_col,  int *base_row,
					  int *start_col, int *start_row,
					  int *end_col,   int *end_row);
void        sheet_selection_free         (Sheet *sheet);
CellList   *sheet_selection_to_list      (Sheet *sheet);
char       *sheet_selection_to_string    (Sheet *sheet, gboolean include_sheet_name_prefix);

/* Operations on the selection */
void        sheet_selection_clear             (Sheet *sheet);
void        sheet_selection_clear_content     (Sheet *sheet);
void        sheet_selection_clear_comments    (Sheet *sheet);
void        sheet_selection_clear_formats     (Sheet *sheet);

/* Cut/Copy/Paste on the workbook selection */
gboolean    sheet_selection_copy              (Sheet *sheet);
gboolean    sheet_selection_cut               (Sheet *sheet);
void        sheet_selection_paste             (Sheet *sheet,
					       int dest_col,    int dest_row,
					       int paste_flags, guint32 time32);
int         sheet_selection_walk_step         (Sheet *sheet,
					       int   forward,     int horizontal,
					       int   current_col, int current_row,
					       int   *new_col,    int *new_row);
void        sheet_selection_extend_horizontal (Sheet *sheet, int count, gboolean jump_to_boundaries);
void        sheet_selection_extend_vertical   (Sheet *sheet, int count, gboolean jump_to_boundaries);
int         sheet_selection_is_cell_selected  (Sheet *sheet, int col, int row);
gboolean    sheet_verify_selection_simple     (Sheet *sheet, const char *command_name);

#endif /* GNUMERIC_SELECTION_H */
