#ifndef GNUMERIC_WORKBOOK_CONTROL_GUI_H
#define GNUMERIC_WORKBOOK_CONTROL_GUI_H

#include "workbook-control.h"
#include "gui-gnumeric.h"
#include <gtk/gtkwindow.h>
#include <bonobo/bonobo-ui-component.h>

#define WORKBOOK_CONTROL_GUI_TYPE     (workbook_control_gui_get_type ())
#define WORKBOOK_CONTROL_GUI(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), WORKBOOK_CONTROL_GUI_TYPE, WorkbookControlGUI))
#define WORKBOOK_CONTROL_GUI_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), WORKBOOK_CONTROL_GUI_TYPE, WorkbookControlGUIClass))
#define IS_WORKBOOK_CONTROL_GUI(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), WORKBOOK_CONTROL_GUI_TYPE))

typedef struct _CustomXmlUI CustomXmlUI;

GType            workbook_control_gui_get_type  (void);
WorkbookControl *workbook_control_gui_new       (WorkbookView *optional_view,
						 Workbook *optional_wb);
void		 workbook_control_gui_init      (WorkbookControlGUI *wbcg,
						 WorkbookView *optional_view,
						 Workbook *optional_wb);

int      wbcg_sheet_to_page_index (WorkbookControlGUI *wbcg, Sheet *sheet,
				   SheetControlGUI **res);
GtkWindow	*wbcg_toplevel	  (WorkbookControlGUI *wbcg);
void	         wbcg_set_transient (WorkbookControlGUI *wbcg,
				     GtkWindow *window);
SheetControlGUI *wbcg_cur_scg	  (WorkbookControlGUI *wbcg);
Sheet		*wbcg_focus_cur_scg (WorkbookControlGUI *wbcg);

gboolean   wbcg_ui_update_begin	  (WorkbookControlGUI *wbcg);
void	   wbcg_ui_update_end	  (WorkbookControlGUI *wbcg);

gboolean   wbcg_rangesel_possible (WorkbookControlGUI const *wbcg);
gboolean   wbcg_is_editing	  (WorkbookControlGUI const *wbcg);
void       wbcg_toolbar_timer_clear (WorkbookControlGUI *wbcg);
void       wbcg_autosave_cancel	  (WorkbookControlGUI *wbcg);
void       wbcg_autosave_set      (WorkbookControlGUI *wbcg,
				   int minutes, gboolean prompt);
void	   wb_control_gui_set_status_text (WorkbookControlGUI *wbcg,
					   char const *text);

CustomXmlUI *register_xml_ui   (const char *xml_ui, const char *textdomain,
                                GSList *verb_list, BonoboUIVerbFn verb_fn,
                                gpointer verb_fn_data);
void         unregister_xml_ui (CustomXmlUI *ui);

#endif /* GNUMERIC_WORKBOOK_CONTROL_GUI_H */
