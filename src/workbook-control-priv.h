#ifndef GNUMERIC_WORKBOOK_CONTROL_PRIV_H
#define GNUMERIC_WORKBOOK_CONTROL_PRIV_H

#include "workbook-control.h"
#include <goffice/app/go-doc-control-impl.h>

struct _WorkbookControl {
	GODocControl base;

	WorkbookView *wb_view;

	/* When editing a cell: the cell (may be NULL) */
	GnmCell     *editing_cell;
	Sheet       *editing_sheet;
	gboolean     editing;

	gulong clipboard_changed_signal;
};
typedef struct {
	GODocControlClass base;

	/* Create a new control of the same form */
	WorkbookControl *(*control_new) (WorkbookControl *wbc, WorkbookView *wbv, Workbook *wb,
					 void *extra);
	void (*init_state) (WorkbookControl *wbc);

	/* Actions on the workbook UI */
	void (*set_title)	    (WorkbookControl *wbc, char const *title);
	void (*prefs_update)	    (WorkbookControl *wbc);
	void (*style_feedback)	    (WorkbookControl *wbc, GnmStyle const *changes);
	void (*zoom_feedback)	    (WorkbookControl *wbc);
	void (*edit_line_set)	    (WorkbookControl *wbc, char const *text);
	void (*edit_finish)	    (WorkbookControl *wbc, gboolean accept);
	void (*selection_descr_set) (WorkbookControl *wbc, char const *text);
	void (*auto_expr_value)	    (WorkbookControl *wbc);
	void (*update_action_sensitivity)  (WorkbookControl *wbc);
	struct {
		void (*add)	(WorkbookControl *wbc, SheetView *sv);
		void (*remove)	(WorkbookControl *wbc, Sheet *sheet);
		void (*rename)  (WorkbookControl *wbc, Sheet *sheet);
		void (*focus)   (WorkbookControl *wbc, Sheet *sheet);
		void (*move)    (WorkbookControl *wbc, Sheet *sheet,
				 int new_pos);
		void (*remove_all) (WorkbookControl *wbc);
	} sheet;
	struct {
		void (*truncate)(WorkbookControl *wbc, int n, gboolean is_undo);
		void (*pop)	(WorkbookControl *wbc, gboolean is_undo);
		void (*push)	(WorkbookControl *wbc, gboolean is_undo,
				 char const *text, gpointer key);
		void (*labels)	(WorkbookControl *wbc,
				 char const *undo, char const *redo);
	} undo_redo;
	struct {
		void (*update)      (WorkbookControl *wbc, int flags);
		void (*sheet_prefs) (WorkbookControl *wbc, Sheet const *sheet);
		void (*sheet_count) (WorkbookControl *wbc);
	} menu_state;

	gboolean (*claim_selection)      (WorkbookControl *wbc);
	void	 (*paste_from_selection) (WorkbookControl *wbc,
					  GnmPasteTarget const *pt);
	int	  (*validation_msg)	 (WorkbookControl *wbc, ValidationStyle v,
					  char const *title, char const *msg);
} WorkbookControlClass;

#define WORKBOOK_CONTROL_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), WORKBOOK_CONTROL_TYPE, WorkbookControlClass))

#endif /* GNUMERIC_WORKBOOK_CONTROL_PRIV_H */
