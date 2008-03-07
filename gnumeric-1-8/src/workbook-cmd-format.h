/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_WORKBOOK_CMD_FORMAT_H_
# define _GNM_WORKBOOK_CMD_FORMAT_H_

#include "gnumeric.h"

G_BEGIN_DECLS

void workbook_cmd_resize_selected_colrow   (WorkbookControl *wbc, Sheet *sheet,
					    gboolean is_cols, int new_size_pixels);
void workbook_cmd_inc_indent		   (WorkbookControl *wbc);
void workbook_cmd_dec_indent		   (WorkbookControl *wbc);

G_END_DECLS

#endif /* _GNM_WORKBOOK_CMD_FORMAT_H_ */
