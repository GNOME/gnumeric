#ifndef GNUMERIC_WORKBOOK_EDIT_H
#define GNUMERIC_WORKBOOK_EDIT_H

#include "gnumeric.h"
#include "workbook-control-gui.h"
#include "widgets/gnumeric-expr-entry.h"

typedef enum {
    WBC_EDIT_REJECT = 0,
    WBC_EDIT_ACCEPT,		/* assign content to current edit pos */
    WBC_EDIT_ACCEPT_RANGE,	/* assign content to first range in selection */
    WBC_EDIT_ACCEPT_ARRAY	/* assign content as an array to the first range in selection */
} WBCEditResult;

void	 wbcg_edit_ctor	  (WorkbookControlGUI *wbcg);
gboolean wbcg_edit_finish (WorkbookControlGUI *wbcg, WBCEditResult result,
			   gboolean *showed_dialog);
gboolean wbcg_edit_start  (WorkbookControlGUI *wbcg,
			   gboolean blankp, gboolean cursorp);

void	    wbcg_edit_attach_guru	(WorkbookControlGUI *wbcg, GtkWidget *guru);
void	    wbcg_edit_attach_guru_with_unfocused_rs (WorkbookControlGUI *wbcg, GtkWidget *guru,
						     GnmExprEntry *gee);
void	    wbcg_edit_detach_guru	(WorkbookControlGUI *wbcg);
GtkWidget  *wbcg_edit_get_guru		(WorkbookControlGUI const *wbcg);
gboolean    wbcg_auto_completing        (WorkbookControlGUI const *wbcg);
void	    wbcg_auto_complete_destroy  (WorkbookControlGUI *wbcg);
char const *wbcg_edit_get_display_text	(WorkbookControlGUI *wbcg);
void	    wbcg_edit_add_markup	(WorkbookControlGUI *wbcg, PangoAttribute *attr);
PangoAttrList *wbcg_edit_get_markup	(WorkbookControlGUI *wbcg, gboolean full);

GtkEntry     *wbcg_get_entry		(WorkbookControlGUI const *wbcg);
GnmExprEntry *wbcg_get_entry_logical	(WorkbookControlGUI const *wbcg);
GtkWidget    *wbcg_get_entry_underlying	(WorkbookControlGUI const *wbcg);
void	      wbcg_set_entry	     	(WorkbookControlGUI *wbc,
					 GnmExprEntry *new_entry);
gboolean      wbcg_entry_has_logical	(WorkbookControlGUI const *wbcg);

#endif /* GNUMERIC_WORKBOOK_EDIT_H */
