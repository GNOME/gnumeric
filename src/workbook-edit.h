#ifndef GNUMERIC_WORKBOOK_EDIT_H
#define GNUMERIC_WORKBOOK_EDIT_H

#include "gnumeric.h"
#include "workbook-control-gui.h"

void        workbook_start_editing_at_cursor (WorkbookControlGUI *wbcg,
					      gboolean blankp, gboolean cursorp);
gboolean    workbook_finish_editing          (WorkbookControlGUI *wbcg, gboolean accept);

gboolean    workbook_editing_expr            (WorkbookControlGUI const  *wbcg);
GtkEntry   *workbook_get_entry               (WorkbookControlGUI const  *wbcg);
GtkEntry   *workbook_get_entry_logical       (WorkbookControlGUI const  *wbcg);
void	    workbook_set_entry               (WorkbookControlGUI *wbc, GtkEntry *new_entry);
void	    workbook_edit_attach_guru	     (WorkbookControlGUI *wbcg, GtkWidget *guru);
void	    workbook_edit_detach_guru	     (WorkbookControlGUI *wbcg);
gboolean    workbook_edit_has_guru	     (WorkbookControlGUI const  *wbcg);
gboolean    workbook_edit_entry_redirect_p   (WorkbookControlGUI const  *wbcg);
void	    workbook_edit_select_absolute    (WorkbookControlGUI        *wbcg);

void        workbook_auto_complete_destroy   (WorkbookControlGUI *wbcg);

char const *workbook_edit_get_display_text   (WorkbookControlGUI *wbcg);
gboolean    workbook_auto_completing         (WorkbookControlGUI *wbcg);

void        workbook_edit_init               (WorkbookControlGUI *wbcg);

#endif /* GNUMERIC_WORKBOOK_EDIT_H */
