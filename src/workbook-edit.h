#ifndef GNUMERIC_WORKBOOK_EDIT_H
#define GNUMERIC_WORKBOOK_EDIT_H

void        workbook_start_editing_at_cursor (Workbook *wb,
					      gboolean blankp, gboolean cursorp);
void        workbook_finish_editing          (Workbook *wb, gboolean const accept);

GtkEntry   *workbook_get_entry               (Workbook const *wb);
gboolean    workbook_editing_expr            (Workbook const *wb);

void        workbook_auto_complete_destroy   (Workbook *wb);

const char *workbook_edit_get_display_text   (Workbook *wb);
gboolean    workbook_auto_completing         (Workbook *wb);

void        workbook_edit_init               (Workbook *wb);

#endif /* GNUMERIC_WORKBOOK_EDIT_H */
