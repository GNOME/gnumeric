#ifndef _GNM_WORKBOOK_CONTROL_H_
# define _GNM_WORKBOOK_CONTROL_H_

#include <gnumeric.h>
#include <validation.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GNM_WBC_TYPE     (workbook_control_get_type ())
#define GNM_WBC(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNM_WBC_TYPE, WorkbookControl))
#define GNM_IS_WBC(o)	  (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_WBC_TYPE))

GType workbook_control_get_type    (void);
void  wb_control_set_view	   (WorkbookControl *wbc,
				    WorkbookView *optional_view,
				    Workbook *optional_wb);
void  wb_control_init_state	   (WorkbookControl *wbc);

/* Create a new control of the same form */
WorkbookControl *workbook_control_new_wrapper (WorkbookControl *wbc,
                                               WorkbookView *wbv, Workbook *wb,
                                               void *extra);

void wb_control_style_feedback	     (WorkbookControl *wbc, GnmStyle const *changes);
void wb_control_edit_line_set        (WorkbookControl *wbc, char const *text);
void wb_control_selection_descr_set  (WorkbookControl *wbc, char const *text);

void wb_control_sheet_add	     (WorkbookControl *wbc, SheetView *sv);
void wb_control_sheet_remove	     (WorkbookControl *wbc, Sheet *sheet);
void wb_control_sheet_focus	     (WorkbookControl *wbc, Sheet *sheet);
void wb_control_sheet_remove_all     (WorkbookControl *wbc);

void wb_control_undo_redo_truncate   (WorkbookControl *wbc, int n, gboolean is_undo);
void wb_control_undo_redo_pop	     (WorkbookControl *wbc, gboolean is_undo);
void wb_control_undo_redo_push	     (WorkbookControl *wbc, gboolean is_undo,
				      char const *text, gpointer key);
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
	MS_FREEZE_VS_THAW   = 1 << 9,
	MS_ADD_VS_REMOVE_FILTER = 1 << 10,
	MS_SHOW_PRINTAREA   = 1 << 11,
	MS_PAGE_BREAKS      = 1 << 12,
	MS_SELECT_OBJECT    = 1 << 13,
	MS_FILTER_STATE_CHANGED = 1 << 14,
	MS_COMMENT_LINKS_RANGE  = 1 << 15,
	MS_COMMENT_LINKS    = 1 << 16,
	MS_FILE_EXPORT_IMPORT = 1 << 17
};

#define MS_ALL \
    (MS_INSERT_COLS | MS_INSERT_ROWS | MS_INSERT_CELLS |		    \
     MS_SHOWHIDE_DETAIL | MS_PASTE_SPECIAL |				    \
     MS_PRINT_SETUP | MS_SEARCH_REPLACE | MS_DEFINE_NAME | MS_CONSOLIDATE | \
     MS_FREEZE_VS_THAW | MS_ADD_VS_REMOVE_FILTER | MS_SHOW_PRINTAREA |      \
     MS_PAGE_BREAKS | MS_SELECT_OBJECT | MS_FILTER_STATE_CHANGED | MS_FILE_EXPORT_IMPORT)
#define MS_GURU_MENU_ITEMS \
    (MS_PRINT_SETUP | MS_SEARCH_REPLACE | MS_DEFINE_NAME | MS_CONSOLIDATE)

void wb_control_menu_state_update	(WorkbookControl *wbc, int flags);
void wb_control_update_action_sensitivity (WorkbookControl *wbc);

void wb_control_paste_from_selection (WorkbookControl *wbc,
				      GnmPasteTarget const *pt);
gboolean wb_control_claim_selection  (WorkbookControl *wbc);

WorkbookView *wb_control_view		(WorkbookControl const *wbc);
Workbook     *wb_control_get_workbook	(WorkbookControl const *wbc);
GODoc	     *wb_control_get_doc	(WorkbookControl const *wbc);
Sheet        *wb_control_cur_sheet	(WorkbookControl const *wbc);
SheetView    *wb_control_cur_sheet_view	(WorkbookControl const *wbc);

gboolean      wb_control_parse_and_jump (WorkbookControl *wbc, char const *text);
gboolean      wb_control_jump (WorkbookControl *wbc, Sheet *sheet, const GnmRangeRef *r);

typedef enum {
	navigator_top,
	navigator_bottom,
	navigator_last,
	navigator_first
} wb_control_navigator_t;

void wb_control_navigate_to_cell (WorkbookControl *wbc, wb_control_navigator_t to);


G_END_DECLS

#endif /* _GNM_WORKBOOK_CONTROL_H_ */
