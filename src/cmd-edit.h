#ifndef GNUMERIC_CMD_EDIT_H
#define GNUMERIC_CMD_EDIT_H

#include "gnumeric.h"

void cmd_select_cur_row	    (Sheet *sheet);
void cmd_select_cur_col	    (Sheet *sheet);
void cmd_select_cur_array   (Sheet *sheet);
void cmd_select_cur_depends (Sheet *sheet);
void cmd_select_cur_inputs  (Sheet *sheet);

void cmd_paste_to_selection (WorkbookControl *context, Sheet *sheet, int flags);
void cmd_paste 		    (WorkbookControl *context, PasteTarget const *pt);

void cmd_shift_cols  (WorkbookControl *context, Sheet *sheet,
		      int start_col, int end_col,
		      int row,       int count);
void cmd_shift_rows  (WorkbookControl *context, Sheet *sheet,
		      int col,
		      int start_row, int end_row, int count);

#endif /* GNUMERIC_CMD_EDIT_H */
