#ifndef GNUMERIC_WORKBOOK_CMD_FORMAT_H
#define GNUMERIC_WORKBOOK_CMD_FORMAT_H

void workbook_cmd_format_column_auto_fit   (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_column_width      (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_row_auto_fit      (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_row_height        (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_sheet_change_name (GtkWidget *widget, Workbook *wb);

void workbook_cmd_format_column_hide      (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_column_unhide    (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_column_std_width (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_row_hide         (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_row_unhide       (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_row_std_height   (GtkWidget *widget, Workbook *wb);

#endif /* GNUMERIC_WORKBOOK_CMD_FORMAT_H */
