#ifndef GNUMERIC_WORKBOOK_CONTROL_GUI_H
#define GNUMERIC_WORKBOOK_CONTROL_GUI_H

#include "workbook-control.h"
#include "gui-gnumeric.h"
#include <gtk/gtkwindow.h>

#define WORKBOOK_CONTROL_GUI_TYPE     (workbook_control_gui_get_type ())
#define WORKBOOK_CONTROL_GUI(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), WORKBOOK_CONTROL_GUI_TYPE, WorkbookControlGUI))
#define WORKBOOK_CONTROL_GUI_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), WORKBOOK_CONTROL_GUI_TYPE, WorkbookControlClassGUI))
#define IS_WORKBOOK_CONTROL_GUI(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), WORKBOOK_CONTROL_GUI_TYPE))

GType            workbook_control_gui_get_type  (void);
WorkbookControl *workbook_control_gui_new       (WorkbookView *optional_view,
						 Workbook *optional_wb);
void		 workbook_control_gui_init      (WorkbookControlGUI *wbcg,
						 WorkbookView *optional_view,
						 Workbook *optional_wb);

GtkWindow	*wbcg_toplevel	  (WorkbookControlGUI *wbcg);
SheetControlGUI *wbcg_cur_scg	  (WorkbookControlGUI *wbcg);
Sheet		*wbcg_focus_cur_scg (WorkbookControlGUI *wbcg);

gboolean   wbcg_ui_update_begin	  (WorkbookControlGUI *wbcg);
void	   wbcg_ui_update_end	  (WorkbookControlGUI *wbcg);

gboolean   wbcg_rangesel_possible (WorkbookControlGUI const *wbcg);
gboolean   wbcg_is_editing	  (WorkbookControlGUI const *wbcg);
void       wbcg_autosave_cancel	  (WorkbookControlGUI *wbcg);
void       wbcg_autosave_set      (WorkbookControlGUI *wbcg,
				   int minutes, gboolean prompt);
void	   wb_control_gui_set_status_text (WorkbookControlGUI *wbcg,
					   char const *text);

#endif /* GNUMERIC_WORKBOOK_CONTROL_GUI_H */
