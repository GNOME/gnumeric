#ifndef GNUMERIC_DIALOGS_H
#define GNUMERIC_DIALOGS_H

void  dialog_goto_cell       (Workbook *wb);
void  dialog_cell_format     (Sheet *sheet);
int   dialog_paste_special   (void);
void  dialog_insert_cells    (Sheet *sheet);
void  dialog_delete_cells    (Sheet *sheet);
void  dialog_zoom            (Sheet *sheet);
char *dialog_query_load_file (void);
void  dialog_about           (void);

#endif /* GNUMERIC_DIALOGS_H */
