#ifndef GNUMERIC_CMD_EDIT_H
#define GNUMERIC_CMD_EDIT_H

#include "gnumeric.h"

void cmd_select_all	    (Sheet *sheet);
void cmd_select_cur_row	    (Sheet *sheet);
void cmd_select_cur_col	    (Sheet *sheet);
void cmd_select_cur_array   (Sheet *sheet);
void cmd_select_cur_depends (Sheet *sheet);
void cmd_select_cur_inputs  (Sheet *sheet);

#endif /* GNUMERIC_CMD_EDIT_H */
