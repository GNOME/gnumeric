#ifndef GNUMERIC_WORKBOOK_VIEW_H
#define GNUMERIC_WORKBOOK_VIEW_H

#include "gnumeric.h"
#include "file.h"
#include <glib-object.h>

struct _WorkbookView {
	GObject  base;

	Workbook *wb;
	GPtrArray *wb_controls;

	Sheet	  *current_sheet;

	/* preferences */
	gboolean   show_horizontal_scrollbar;
	gboolean   show_vertical_scrollbar;
	gboolean   show_notebook_tabs;
	gboolean   do_auto_completion;
	gboolean   is_protected;

	/* Non-normative size information */
	int preferred_width, preferred_height;

	/* The auto-expression */
	GnmExpr const *auto_expr;
	char	  *auto_expr_desc;
	char	  *auto_expr_value_as_string;

	/* selection */
	char	  *selection_description;

	/* Format for feedback */
	MStyle    *current_format;
};

typedef struct {
	GObjectClass   gtk_object_class;
	void (*sheet_entered) (Sheet *sheet);
} WorkbookViewClass;

#define WORKBOOK_VIEW_TYPE     (workbook_view_get_type ())
#define WORKBOOK_VIEW(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), WORKBOOK_VIEW_TYPE, WorkbookView))
#define WORKBOOK_VIEW_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), WORKBOOK_VIEW_TYPE, WorkbookViewClass))
#define IS_WORKBOOK_VIEW(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), WORKBOOK_VIEW_TYPE))

/* Lifecycle */
GType		 workbook_view_get_type	  (void);
WorkbookView	*workbook_view_new	  (Workbook *optional_workbook);
void             workbook_view_init       (WorkbookView *wbv,
					   Workbook *optional_workbook);
void		 wb_view_attach_control	  (WorkbookView *wbv, WorkbookControl *wbc);
void		 wb_view_detach_control	  (WorkbookControl *wbc);

/* Information */
Workbook	*wb_view_workbook	  (WorkbookView *wbv);
Sheet		*wb_view_cur_sheet	  (WorkbookView *wbv);
void		 wb_view_sheet_focus	  (WorkbookView *wbv, Sheet *sheet);
void		 wb_view_sheet_add	  (WorkbookView *wbv, Sheet *new_sheet);
gboolean	 wb_view_is_protected	  (WorkbookView *wbv, gboolean check_sheet);

/* Manipulation */
void         	 wb_view_set_attribute	  (WorkbookView *wbv, char const *name,
					   char const *value);
void		 wb_view_preferred_size	  (WorkbookView *wbv,
					   int w_pixels, int h_pixels);
void		 wb_view_prefs_update	  (WorkbookView *wbv);
void		 wb_view_format_feedback  (WorkbookView *wbv, gboolean display);
void             wb_view_menus_update     (WorkbookView *wbv);
void		 wb_view_selection_desc   (WorkbookView *wbv, gboolean use_pos,
					   WorkbookControl *optional_wbc);
void		 wb_view_edit_line_set	  (WorkbookView *wbv,
					   WorkbookControl *optional_wbc);
void		 wb_view_auto_expr_recalc (WorkbookView *wbv, gboolean display);
void		 wb_view_auto_expr	  (WorkbookView *wbv,
					   char const *name, char const *expr);

/* I/O routines */
gboolean wb_view_save_as     (WorkbookView *wbv, GnumFileSaver *fs,
			      char const *file_name, CommandContext *context);
gboolean wb_view_save        (WorkbookView *wbv, CommandContext *context);
gboolean wb_view_open        (WorkbookView *wbv, WorkbookControl *wbc,
                              char const *file_name,
			      gboolean display_errors);
gboolean wb_view_open_custom (WorkbookView *wbv, WorkbookControl *wbc,
                              GnumFileOpener const *fo, char const *file_name,
			      gboolean display_errors);

#define WORKBOOK_VIEW_FOREACH_CONTROL(wbv, control, code)			\
do {										\
	int jNd;								\
	GPtrArray *wb_controls = (wbv)->wb_controls;				\
	if (wb_controls != NULL) /* Reverse is important during destruction */	\
		for (jNd = wb_controls->len; jNd-- > 0 ;) {			\
			WorkbookControl *control =				\
				g_ptr_array_index (wb_controls, jNd);		\
			code							\
		}								\
} while (0)


#endif /* GNUMERIC_WORKBOOK_VIEW_H */
