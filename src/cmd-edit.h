#ifndef GNUMERIC_CMD_EDIT_H
#define GNUMERIC_CMD_EDIT_H

#include "gnumeric.h"

void cmd_select_all	    (Sheet *sheet);
void cmd_select_cur_row	    (Sheet *sheet);
void cmd_select_cur_col	    (Sheet *sheet);
void cmd_select_cur_array   (Sheet *sheet);
void cmd_select_cur_depends (Sheet *sheet);
void cmd_select_cur_inputs  (Sheet *sheet);

void cmd_paste_to_selection (CommandContext *context, Sheet *sheet, int flags);
void cmd_paste 		    (CommandContext *context, PasteTarget const *pt,
			     guint32 time);

#endif /* GNUMERIC_CMD_EDIT_H */
