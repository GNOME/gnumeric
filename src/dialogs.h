#ifndef DIALOGS_H
#define DIALOGS_H

void dialog_goto_cell     (Workbook *wb);
void dialog_cell_format   (Sheet *sheet);
int  dialog_paste_special (void);
void dialog_insert_cells  (Sheet *sheet);

#endif
