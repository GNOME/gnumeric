#ifndef GNUMERIC_WORKBOOK_CONTROL_H
#define GNUMERIC_WORKBOOK_CONTROL_H

#include "gnumeric.h"
#include <gtk/gtkobject.h>

struct _WorkbookControl {
	GtkObject  gtk_object;

	WorkbookView *wb_view;
	GSList *template_list;
};
typedef struct {
	GtkObjectClass   gtk_object_class;

	/* Create a new control of the same form */
	WorkbookControl *(*control_new) (WorkbookControl *wbc, WorkbookView *wbv, Workbook *wb);

	/* Actions on the workbook UI */
	void (*title_set)	(WorkbookControl *wbc, char const *title);
	void (*size_pixels_set)	(WorkbookControl *wbc, int width, int height);
	void (*prefs_update)	(WorkbookControl *wbc);
	void (*progress_set)	(WorkbookControl *wbc, gfloat val);
	void (*format_feedback)	(WorkbookControl *wbc, MStyle *style);
	void (*zoom_feedback)	(WorkbookControl *wbc);
	void (*edit_line_set)   (WorkbookControl *wbc, char const *text);
	void (*auto_expr_value) (WorkbookControl *wbc, char const *value);
	struct {
		void (*add)	(WorkbookControl *wbc, Sheet *sheet);
		void (*remove)	(WorkbookControl *wbc, Sheet *sheet);
		void (*rename)  (WorkbookControl *wbc, Sheet *sheet);
		void (*focus)   (WorkbookControl *wbc, Sheet *sheet);
		void (*move)    (WorkbookControl *wbc, Sheet *sheet, int dir);
	} sheet;
	struct {
		void (*clear)	(WorkbookControl *wbc, gboolean is_undo);
		void (*pop)	(WorkbookControl *wbc, gboolean is_undo);
		void (*push)	(WorkbookControl *wbc,
				 char const *text, gboolean is_undo);
		void (*labels)	(WorkbookControl *wbc,
				 char const *undo, char const *redo);
	} undo_redo;
	struct {
		void (*special_enable) (WorkbookControl *wbc, gboolean enable);
		void (*from_selection) (WorkbookControl *wbc,
					PasteTarget const *pt, guint32 time);
	} paste;
	struct {
		void (*system)	(WorkbookControl *wbc, char const *msg);
		void (*plugin)	(WorkbookControl *wbc, char const *msg);
		void (*read)	(WorkbookControl *wbc, char const *msg);
		void (*save)	(WorkbookControl *wbc, char const *msg);
		void (*invalid)	(WorkbookControl *wbc,
				 char const *msg, char const *val);
	} error;
} WorkbookControlClass;

#define WORKBOOK_CONTROL_TYPE     (workbook_control_get_type ())
#define WORKBOOK_CONTROL(obj)     (GTK_CHECK_CAST ((obj), WORKBOOK_CONTROL_TYPE, WorkbookControl))
#define WORKBOOK_CONTROL_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), WORKBOOK_CONTROL_TYPE, WorkbookControlClass))
#define IS_WORKBOOK_CONTROL(o)	  (GTK_CHECK_TYPE ((o), WORKBOOK_CONTROL_TYPE))

GtkType workbook_control_get_type    (void);
void 	workbook_control_init	     (WorkbookControl *wbc,
				      WorkbookView *optional_view,
				      Workbook *optional_wb);
void    workbook_control_sheets_init (WorkbookControl *wbc);

/* Create a new control of the same form */
WorkbookControl *wb_control_wrapper_new (WorkbookControl *wbc,
					 WorkbookView *wbv, Workbook *wb);

void wb_control_title_set	     (WorkbookControl *wbc, char const *title);
void wb_control_size_pixels_set	     (WorkbookControl *wbc, int w, int h);
void wb_control_prefs_update	     (WorkbookControl *wbc);
void wb_control_format_feedback	     (WorkbookControl *wbc, MStyle *style);
void wb_control_zoom_feedback	     (WorkbookControl *wbc);
void wb_control_edit_line_set        (WorkbookControl *wbc, char const *text);

void wb_control_sheet_add	     (WorkbookControl *wbc, Sheet *sheet);
void wb_control_sheet_remove	     (WorkbookControl *wbc, Sheet *sheet);
void wb_control_sheet_rename	     (WorkbookControl *wbc, Sheet *sheet);
void wb_control_sheet_focus	     (WorkbookControl *wbc, Sheet *sheet);
void wb_control_sheet_move	     (WorkbookControl *wbc, Sheet *sheet, int dir);

void wb_control_auto_expr_value	     (WorkbookControl *wbc, char const *value);

void wb_control_undo_redo_clear	     (WorkbookControl *wbc, gboolean is_undo);
void wb_control_undo_redo_pop	     (WorkbookControl *wbc, gboolean is_undo);
void wb_control_undo_redo_push	     (WorkbookControl *wbc,
				      char const *text, gboolean is_undo);
void wb_control_undo_redo_labels     (WorkbookControl *wbc,
				      char const *undo, char const *redo);

void wb_control_paste_special_enable (WorkbookControl *wbc, gboolean enable);
void wb_control_paste_from_selection (WorkbookControl *wbc,
				      PasteTarget const *pt, guint32 time);
/*
 * These routines represent the exceptions that can arise.
 * NOTE : The selection is quite limited by IDL's intentional non-support for
 *        inheritance (single or multiple).
 */
void wb_control_system_err	  (WorkbookControl *wbc, char const *msg);
void wb_control_plugin_err	  (WorkbookControl *wbc, char const *msg);
void wb_control_read_err	  (WorkbookControl *wbc, char const *msg);
void wb_control_save_err	  (WorkbookControl *wbc, char const *msg);
void wb_control_splits_array_err  (WorkbookControl *wbc, char const *cmd);
void wb_control_invalid_err	  (WorkbookControl *wbc, char const *msg,
				   char const *val);
void wb_control_push_err_template (WorkbookControl *wbc, const char *str);
void wb_control_pop_err_template  (WorkbookControl *wbc);

WorkbookView *wb_control_view		(WorkbookControl *wbc);
Workbook     *wb_control_workbook	(WorkbookControl *wbc);
Sheet        *wb_control_cur_sheet	(WorkbookControl *wbc);

gboolean      workbook_parse_and_jump   (WorkbookControl *wbc, const char *text);

#endif /* GNUMERIC_WORKBOOK_CONTROL_H */
