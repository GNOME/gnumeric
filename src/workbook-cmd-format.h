#ifndef GNUMERIC_WORKBOOK_CMD_FORMAT_H
#define GNUMERIC_WORKBOOK_CMD_FORMAT_H

#include "gnumeric.h"
#include "workbook-control-gui.h"

void workbook_cmd_resize_selected_colrow   (WorkbookControl *wbc, Sheet *sheet,
					    gboolean is_cols, int new_size_pixels);
void workbook_cmd_inc_indent		   (WorkbookControl *wbc);
void workbook_cmd_dec_indent		   (WorkbookControl *wbc);

void workbook_cmd_format_column_auto_fit   (GtkWidget *widget, WorkbookControl *wbc);
void sheet_dialog_set_column_width         (GtkWidget *widget, WorkbookControlGUI *wbcg);
void workbook_cmd_format_column_std_width  (GtkWidget *widget, WorkbookControl *wbc);

void workbook_cmd_format_row_auto_fit      (GtkWidget *widget, WorkbookControl *wbc);
void sheet_dialog_set_row_height	   (GtkWidget *widget, WorkbookControlGUI *wbcg);
void workbook_cmd_format_row_std_height    (GtkWidget *widget, WorkbookControl *wbc);

#endif /* GNUMERIC_WORKBOOK_CMD_FORMAT_H */
