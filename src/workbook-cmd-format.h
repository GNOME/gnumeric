#ifndef GNUMERIC_WORKBOOK_CMD_FORMAT_H
#define GNUMERIC_WORKBOOK_CMD_FORMAT_H

void workbook_cmd_format_as_money          (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_as_percent        (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_add_thousands     (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_add_decimals      (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_remove_decimals   (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_column_auto_fit   (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_column_width      (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_row_auto_fit      (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_row_height        (GtkWidget *widget, Workbook *wb);
void workbook_cmd_format_sheet_change_name (GtkWidget *widget, Workbook *wb);

#endif /* GNUMERIC_WORKBOOK_CMD_FORMAT_H */
