#ifndef GNUMERIC_DIALOGS_H
#define GNUMERIC_DIALOGS_H

void  dialog_goto_cell       (Workbook *wb);
void  dialog_cell_format     (Workbook *wb, Sheet *sheet);
int   dialog_paste_special   (Workbook *wb);
void  dialog_insert_cells    (Workbook *wb, Sheet *sheet);
void  dialog_delete_cells    (Workbook *wb, Sheet *sheet);
void  dialog_zoom            (Workbook *wb, Sheet *sheet);
char *dialog_query_load_file (Workbook *wb);
void  dialog_about           (Workbook *wb);
void  dialog_define_names    (Workbook *wb);

#endif /* GNUMERIC_DIALOGS_H */
