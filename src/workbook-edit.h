#ifndef GNUMERIC_WORKBOOK_EDIT_H
#define GNUMERIC_WORKBOOK_EDIT_H

#include "gnumeric.h"
#include "workbook-control-gui.h"
#include "widgets/gnumeric-expr-entry.h"

void	 wbcg_edit_ctor	  (WorkbookControlGUI *wbcg);
void	 wbcg_edit_dtor	  (WorkbookControlGUI *wbcg);
gboolean wbcg_edit_finish (WorkbookControlGUI *wbcg, gboolean accept);
void     wbcg_edit_start  (WorkbookControlGUI *wbcg,
			   gboolean blankp, gboolean cursorp);

void	    wbcg_edit_attach_guru	(WorkbookControlGUI *wbcg, GtkWidget *guru);
void	    wbcg_edit_detach_guru	(WorkbookControlGUI *wbcg);
gboolean    wbcg_edit_has_guru		(WorkbookControlGUI const *wbcg);
gboolean    wbcg_edit_entry_redirect_p	(WorkbookControlGUI const *wbcg);
gboolean    wbcg_auto_completing        (WorkbookControlGUI const *wbcg);
void	    wbcg_auto_complete_destroy  (WorkbookControlGUI *wbcg);
char const *wbcg_edit_get_display_text	(WorkbookControlGUI *wbcg);

GnumericExprEntry *wbcg_get_entry	  (WorkbookControlGUI const  *wbcg);
GnumericExprEntry *wbcg_get_entry_logical (WorkbookControlGUI const  *wbcg);
void		   wbcg_set_entry	  (WorkbookControlGUI *wbc,
					   GnumericExprEntry *new_entry);

#endif /* GNUMERIC_WORKBOOK_EDIT_H */
