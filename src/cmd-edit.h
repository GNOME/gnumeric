#ifndef GNUMERIC_CMD_EDIT_H
#define GNUMERIC_CMD_EDIT_H

#include "gnumeric.h"

/* TODO : move these to selection */
void sv_select_cur_row	   (SheetView *sv);
void sv_select_cur_col	   (SheetView *sv);
void sv_select_cur_array   (SheetView *sv);
void sv_select_cur_depends (SheetView *sv);
void sv_select_cur_inputs  (SheetView *sv);

void cmd_paste_to_selection (WorkbookControl *wbc, SheetView *sv, int flags);
void cmd_paste 		    (WorkbookControl *wbc, PasteTarget const *pt);

void cmd_shift_cols  (WorkbookControl *wbc, Sheet *sheet,
		      int start_col, int end_col,
		      int row,       int count);
void cmd_shift_rows  (WorkbookControl *wbc, Sheet *sheet,
		      int col,
		      int start_row, int end_row, int count);

#endif /* GNUMERIC_CMD_EDIT_H */
