#ifndef GNUMERIC_WORKBOOK_CONTROL_PRIV_H
#define GNUMERIC_WORKBOOK_CONTROL_PRIV_H

#include "command-context-priv.h"
#include "workbook-control.h"

struct _WorkbookControl {
	CommandContext	context;

	WorkbookView *wb_view;
};
typedef struct {
	CommandContextClass   context_class;

	/* Create a new control of the same form */
	WorkbookControl *(*control_new) (WorkbookControl *wbc, WorkbookView *wbv, Workbook *wb);
	void (*init_state) (WorkbookControl *wbc);

	/* Actions on the workbook UI */
	void (*title_set)	    (WorkbookControl *wbc, char const *title);
	void (*prefs_update)	    (WorkbookControl *wbc);
	void (*format_feedback)	    (WorkbookControl *wbc);
	void (*zoom_feedback)	    (WorkbookControl *wbc);
	void (*edit_line_set)	    (WorkbookControl *wbc, char const *text);
	void (*edit_finish)	    (WorkbookControl *wbc, gboolean accept);
	void (*selection_descr_set) (WorkbookControl *wbc, char const *text);
	void (*set_sensitive)	    (WorkbookControl *wbc, gboolean sensitive);
	void (*edit_set_sensitive)  (WorkbookControl *wbc,
				     gboolean flag1, gboolean flag2);
	void (*auto_expr_value)	    (WorkbookControl *wbc);
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
		void (*clear)	(WorkbookControl *wbc, gboolean is_undo);
		void (*truncate)(WorkbookControl *wbc, int n, gboolean is_undo);
		void (*pop)	(WorkbookControl *wbc, gboolean is_undo);
		void (*push)	(WorkbookControl *wbc,
				 char const *text, gboolean is_undo);
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
					  PasteTarget const *pt);
	int	  (*validation_msg)	 (WorkbookControl *wbc, ValidationStyle v,
					  char const *title, char const *msg);
} WorkbookControlClass;

#define WORKBOOK_CONTROL_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), WORKBOOK_CONTROL_TYPE, WorkbookControlClass))

#endif /* GNUMERIC_WORKBOOK_CONTROL_PRIV_H */
