#ifndef GNUMERIC_WORKBOOK_EDIT_H
#define GNUMERIC_WORKBOOK_EDIT_H

void        workbook_start_editing_at_cursor (Workbook *wb,
					      gboolean blankp, gboolean cursorp);
void        workbook_finish_editing          (Workbook *wb, gboolean const accept);

gboolean    workbook_editing_expr            (Workbook const *wb);
GtkEntry   *workbook_get_entry               (Workbook const *wb);
GtkEntry   *workbook_get_entry_logical       (Workbook const *wb);
void	    workbook_set_entry               (Workbook       *wb, GtkEntry *new_entry);
void	    workbook_edit_attach_guru	     (Workbook       *wb, GtkWidget *guru);
void	    workbook_edit_detach_guru	     (Workbook       *wb);
gboolean    workbook_edit_has_guru	     (Workbook const *wb);
void	    workbook_edit_select_absolute    (Workbook       *wb);

void        workbook_auto_complete_destroy   (Workbook *wb);

const char *workbook_edit_get_display_text   (Workbook *wb);
gboolean    workbook_auto_completing         (Workbook *wb);

void        workbook_edit_load_value	     (Sheet const *sheet);
void        workbook_edit_init               (Workbook *wb);

#endif /* GNUMERIC_WORKBOOK_EDIT_H */
