#ifndef GNUMERIC_WORKBOOK_CONTROL_H
#define GNUMERIC_WORKBOOK_CONTROL_H

#include "gnumeric.h"
#include "validation.h"
#include <glib-object.h>

#define WORKBOOK_CONTROL_TYPE     (workbook_control_get_type ())
#define WORKBOOK_CONTROL(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), WORKBOOK_CONTROL_TYPE, WorkbookControl))
#define IS_WORKBOOK_CONTROL(o)	  (G_TYPE_CHECK_INSTANCE_TYPE ((o), WORKBOOK_CONTROL_TYPE))

GType workbook_control_get_type    (void);
void 	workbook_control_set_view    (WorkbookControl *wbc,
				      WorkbookView *optional_view,
				      Workbook *optional_wb);
void    workbook_control_init_state  (WorkbookControl *wbc);

/* Create a new control of the same form */
WorkbookControl *wb_control_wrapper_new (WorkbookControl *wbc,
					 WorkbookView *wbv, Workbook *wb);

void wb_control_title_set	     (WorkbookControl *wbc, char const *title);
void wb_control_prefs_update	     (WorkbookControl *wbc);
void wb_control_format_feedback	     (WorkbookControl *wbc);
void wb_control_zoom_feedback	     (WorkbookControl *wbc);
void wb_control_edit_line_set        (WorkbookControl *wbc, char const *text);
void wb_control_selection_descr_set  (WorkbookControl *wbc, char const *text);
void wb_control_set_sensitive	     (WorkbookControl *wbc, gboolean sensitive);
void wb_control_edit_set_sensitive   (WorkbookControl *wbc,
				      gboolean ok_cancel_flag,
				      gboolean func_guru_flag);
void wb_control_auto_expr_value	     (WorkbookControl *wbc);

void wb_control_sheet_add	     (WorkbookControl *wbc, SheetView *sv);
void wb_control_sheet_remove	     (WorkbookControl *wbc, Sheet *sheet);
void wb_control_sheet_rename	     (WorkbookControl *wbc, Sheet *sheet);
void wb_control_sheet_focus	     (WorkbookControl *wbc, Sheet *sheet);
void wb_control_sheet_move	     (WorkbookControl *wbc, Sheet *sheet,
				      int new_pos);
void wb_control_sheet_remove_all     (WorkbookControl *wbc);

void wb_control_undo_redo_clear	     (WorkbookControl *wbc, gboolean is_undo);
void wb_control_undo_redo_truncate   (WorkbookControl *wbc, int n, gboolean is_undo);
void wb_control_undo_redo_pop	     (WorkbookControl *wbc, gboolean is_undo);
void wb_control_undo_redo_push	     (WorkbookControl *wbc,
				      char const *text, gboolean is_undo);
void wb_control_undo_redo_labels     (WorkbookControl *wbc,
				      char const *undo, char const *redo);
int  wb_control_validation_msg	     (WorkbookControl *wbc, ValidationStyle v,
				      char const *title, char const *msg);

/* Menu state update flags, use them to specify which menu items to update */
enum {
	MS_INSERT_COLS      = 1 << 0,
	MS_INSERT_ROWS      = 1 << 1,
	MS_INSERT_CELLS     = 1 << 2,
	MS_SHOWHIDE_DETAIL  = 1 << 3,
	MS_PASTE_SPECIAL    = 1 << 4,
	MS_PRINT_SETUP      = 1 << 5,
	MS_SEARCH_REPLACE   = 1 << 6,
	MS_DEFINE_NAME      = 1 << 7,
	MS_CONSOLIDATE      = 1 << 8,
	MS_FREEZE_VS_THAW   = 1 << 9
};

#define MS_ALL \
        (MS_INSERT_COLS | MS_INSERT_ROWS | MS_INSERT_CELLS | \
	 MS_SHOWHIDE_DETAIL | MS_PASTE_SPECIAL | MS_PRINT_SETUP | \
	 MS_SEARCH_REPLACE | MS_DEFINE_NAME | MS_CONSOLIDATE | MS_FREEZE_VS_THAW)
#define MS_GURU_MENU_ITEMS \
        (MS_PRINT_SETUP | MS_SEARCH_REPLACE | MS_DEFINE_NAME | MS_CONSOLIDATE)

void wb_control_menu_state_sheet_prefs	(WorkbookControl *wbc, Sheet const *s);
void wb_control_menu_state_sheet_count	(WorkbookControl *wbc);
void wb_control_menu_state_update	(WorkbookControl *wbc, Sheet const *s,
					 int flags);

void wb_control_paste_from_selection (WorkbookControl *wbc,
				      PasteTarget const *pt);
gboolean wb_control_claim_selection  (WorkbookControl *wbc);

WorkbookView *wb_control_view		(WorkbookControl *wbc);
Workbook     *wb_control_workbook	(WorkbookControl *wbc);
Sheet        *wb_control_cur_sheet	(WorkbookControl *wbc);

gboolean      wb_control_parse_and_jump (WorkbookControl *wbc, char const *text);

#endif /* GNUMERIC_WORKBOOK_CONTROL_H */
