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
void        sheet_selection_append_range (Sheet *sheet,
					  int base_col,  int base_row,
					  int start_col, int start_row,
					  int end_col,   int end_row);
void        sheet_selection_free         (Sheet *sheet);
CellList   *sheet_selection_to_list      (Sheet *sheet);
void        sheet_cell_list_free         (CellList *cell_list);
char       *sheet_selection_to_string    (Sheet *sheet, gboolean include_sheet_name_prefix);

/* Operations on the selection */
void        sheet_selection_clear             (CmdContext *context, Sheet *sheet);
void        sheet_selection_clear_content     (CmdContext *context, Sheet *sheet);
void        sheet_selection_clear_comments    (CmdContext *context, Sheet *sheet);
void        sheet_selection_clear_formats     (CmdContext *context, Sheet *sheet);
void        sheet_selection_height_update     (Sheet *sheet);

/* Cut/Copy/Paste on the workbook selection */
gboolean    sheet_selection_copy              (CmdContext *context, Sheet *sheet);
gboolean    sheet_selection_cut               (CmdContext *context, Sheet *sheet);
void        sheet_selection_paste             (CmdContext *context, Sheet *sheet,
					       int dest_col,    int dest_row,
					       int paste_flags, guint32 time32);
int         sheet_selection_walk_step         (Sheet *sheet,
					       int   forward,     int horizontal,
					       int   current_col, int current_row,
					       int   *new_col,    int *new_row);
void        sheet_selection_extend_horizontal (Sheet *sheet, int count, gboolean jump_to_boundaries);
void        sheet_selection_extend_vertical   (Sheet *sheet, int count, gboolean jump_to_boundaries);
int         sheet_selection_is_cell_selected  (Sheet *sheet, int col, int row);

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

/* export the selection */
CellList   *selection_to_list      (Sheet *sheet, gboolean allow_intersection);
char       *selection_to_string    (Sheet *sheet,
				    gboolean include_sheet_name_prefix);

/* Information about the selection */
gboolean      selection_is_simple   (CmdContext *context, Sheet const *sheet,
				     char const *command_name);
Range const * selection_first_range (Sheet const *sheet, gboolean const permit_complex);
gboolean    selection_foreach_range (Sheet *sheet,
				     gboolean (*range_cb) (Sheet *sheet,
							   Range const *range,
							   gpointer user_data),
				     gpointer user_data);

#endif /* GNUMERIC_SELECTION_H */
