#ifndef GNUMERIC_WORKBOOK_CONTROL_GUI_H
#define GNUMERIC_WORKBOOK_CONTROL_GUI_H

#include "workbook-control.h"
#include "gui-gnumeric.h"

#define WORKBOOK_CONTROL_GUI_TYPE     (workbook_control_get_type ())
#define WORKBOOK_CONTROL_GUI(obj)     (GTK_CHECK_CAST ((obj), WORKBOOK_CONTROL_GUI_TYPE, WorkbookControlGUI))
#define WORKBOOK_CONTROL_GUI_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), WORKBOOK_CONTROL_GUI_TYPE, WorkbookControlClassGUI))
#define IS_WORKBOOK_CONTROL_GUI(o)    (GTK_CHECK_TYPE ((o), WORKBOOK_CONTROL_GUI_TYPE))

GtkType          workbook_control_gui_get_type  (void);
WorkbookControl *workbook_control_gui_new       (WorkbookView *optional_view,
						 Workbook *optional_wb);
void		 workbook_control_gui_init      (WorkbookControlGUI *wbcg,
						 WorkbookView *optional_view,
						 Workbook *optional_wb);

GtkWindow *wb_control_gui_toplevel        (WorkbookControlGUI *wbcg);
Sheet *    wb_control_gui_focus_cur_sheet (WorkbookControlGUI *wbcg);
SheetControlGUI *wb_control_gui_cur_sheet (WorkbookControlGUI *wbcg);

gboolean   wbcg_ui_update_begin	  (WorkbookControlGUI *wbcg);
void	   wbcg_ui_update_end	  (WorkbookControlGUI *wbcg);

gboolean   wbcg_rangesel_possible	  (WorkbookControlGUI const *wbcg);
gboolean   wb_control_gui_is_editing	  (WorkbookControlGUI const *wbcg);
void       wb_control_gui_autosave_cancel (WorkbookControlGUI *wbcg);
void       wb_control_gui_autosave_set    (WorkbookControlGUI *wbcg,
					   int minutes, gboolean prompt);
void	   wb_control_gui_set_status_text (WorkbookControlGUI *wbcg,
					   char const *text);

#endif /* GNUMERIC_WORKBOOK_CONTROL_GUI_H */
