#ifndef GNUMERIC_WORKBOOK_CMD_FORMAT_H
#define GNUMERIC_WORKBOOK_CMD_FORMAT_H

#include "gnumeric.h"
#include "workbook-control-gui.h"

void workbook_cmd_format_column_width	   (WorkbookControl *wbc,
					    Sheet *sheet, int new_size_pixels);
void workbook_cmd_format_column_auto_fit   (GtkWidget *widget, WorkbookControl *wbc);
void sheet_dialog_set_column_width         (GtkWidget *widget, WorkbookControlGUI *wbcg);
void workbook_cmd_format_column_std_width  (GtkWidget *widget, WorkbookControl *wbc);
void workbook_cmd_format_column_hide       (GtkWidget *widget, WorkbookControl *wbc);
void workbook_cmd_format_column_unhide     (GtkWidget *widget, WorkbookControl *wbc);

void workbook_cmd_format_row_height        (WorkbookControl *wbc,
					    Sheet *sheet, int new_size_pixels);
void workbook_cmd_format_row_auto_fit      (GtkWidget *widget, WorkbookControl *wbc);
void sheet_dialog_set_row_height	   (GtkWidget *widget, WorkbookControlGUI *wbcg);
void workbook_cmd_format_row_std_height    (GtkWidget *widget, WorkbookControl *wbc);
void workbook_cmd_format_row_hide          (GtkWidget *widget, WorkbookControl *wbc);
void workbook_cmd_format_row_unhide        (GtkWidget *widget, WorkbookControl *wbc);

#endif /* GNUMERIC_WORKBOOK_CMD_FORMAT_H */
