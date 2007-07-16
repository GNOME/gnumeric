#ifndef GNUMERIC_WORKBOOK_CMD_FORMAT_H
#define GNUMERIC_WORKBOOK_CMD_FORMAT_H

#include "gnumeric.h"
#include "workbook-control-gui.h"

void workbook_cmd_resize_selected_colrow   (WorkbookControl *wbc, Sheet *sheet,
					    gboolean is_cols, int new_size_pixels);
void workbook_cmd_inc_indent		   (WorkbookControl *wbc);
void workbook_cmd_dec_indent		   (WorkbookControl *wbc);

#endif /* GNUMERIC_WORKBOOK_CMD_FORMAT_H */
