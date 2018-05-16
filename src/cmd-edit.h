#ifndef _GNM_CMD_EDIT_H_
# define _GNM_CMD_EDIT_H_

#include <gnumeric.h>

G_BEGIN_DECLS

/* TODO : move these to selection */
void sv_select_cur_row	   (SheetView *sv);
void sv_select_cur_col	   (SheetView *sv);
void sv_select_cur_array   (SheetView *sv);
void sv_select_cur_depends (SheetView *sv);
void sv_select_cur_inputs  (SheetView *sv);

void cmd_paste_to_selection (WorkbookControl *wbc, SheetView *dest_sv, int paste_flags);
void cmd_paste		    (WorkbookControl *wbc, GnmPasteTarget const *pt);

void cmd_shift_cols  (WorkbookControl *wbc, Sheet *sheet,
		      int start_col, int end_col,
		      int row,       int count);
void cmd_shift_rows  (WorkbookControl *wbc, Sheet *sheet,
		      int col,
		      int start_row, int end_row, int count);

G_END_DECLS

#endif /* _GNM_CMD_EDIT_H_ */
