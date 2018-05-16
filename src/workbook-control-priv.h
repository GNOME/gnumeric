#ifndef _GNM_WORKBOOK_CONTROL_PRIV_H_
# define _GNM_WORKBOOK_CONTROL_PRIV_H_

#include <workbook-control.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

struct _WorkbookControl {
	GODocControl base;

	WorkbookView *wb_view;

	gulong clipboard_changed_signal;
};
typedef struct {
	GODocControlClass base;

	/* Create a new control of the same form */
	WorkbookControl *(*control_new) (WorkbookControl *wbc, WorkbookView *wbv, Workbook *wb,
					 void *extra);
	void (*init_state) (WorkbookControl *wbc);

	/* Actions on the workbook UI */
	void (*style_feedback)	    (WorkbookControl *wbc, GnmStyle const *changes);
	void (*edit_line_set)	    (WorkbookControl *wbc, char const *text);
	void (*edit_finish)	    (WorkbookControl *wbc, gboolean accept);
	void (*selection_descr_set) (WorkbookControl *wbc, char const *text);
	void (*update_action_sensitivity)  (WorkbookControl *wbc);
	struct _WbcSheetManager {
		void (*add)	(WorkbookControl *wbc, SheetView *sv);
		void (*remove)	(WorkbookControl *wbc, Sheet *sheet);
		void (*focus)   (WorkbookControl *wbc, Sheet *sheet);
		void (*remove_all) (WorkbookControl *wbc);
	} sheet;
	struct _WbcUndoRedo {
		void (*truncate)(WorkbookControl *wbc, int n, gboolean is_undo);
		void (*pop)	(WorkbookControl *wbc, gboolean is_undo);
		void (*push)	(WorkbookControl *wbc, gboolean is_undo,
				 char const *text, gpointer key);
		void (*labels)	(WorkbookControl *wbc,
				 char const *undo, char const *redo);
	} undo_redo;
	struct _WbcMenuState {
		void (*update)      (WorkbookControl *wbc, int flags);
	} menu_state;

	gboolean (*claim_selection)      (WorkbookControl *wbc);
	void	 (*paste_from_selection) (WorkbookControl *wbc,
					  GnmPasteTarget const *pt);
	int	  (*validation_msg)	 (WorkbookControl *wbc, ValidationStyle v,
					  char const *title, char const *msg);
} WorkbookControlClass;

#define GNM_WBC_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), GNM_WBC_TYPE, WorkbookControlClass))

G_END_DECLS

#endif /* _GNM_WORKBOOK_CONTROL_PRIV_H_ */
