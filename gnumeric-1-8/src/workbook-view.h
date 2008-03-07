/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_WORKBOOK_VIEW_H_
# define _GNM_WORKBOOK_VIEW_H_

#include "gnumeric.h"
#include <glib-object.h>
#include <gsf/gsf.h>

G_BEGIN_DECLS

struct _WorkbookView {
	GObject  base;

	Workbook *wb;
	GPtrArray *wb_controls;

	Sheet	  *current_sheet;	/* convenience */
	SheetView *current_sheet_view;

	/* preferences */
	gboolean   show_horizontal_scrollbar;
	gboolean   show_vertical_scrollbar;
	gboolean   show_notebook_tabs;
	gboolean   do_auto_completion;
	gboolean   is_protected;

	/* Non-normative size information */
	int preferred_width, preferred_height;

	/* The auto-expression */
	GnmFunc   *auto_expr_func;
	char	  *auto_expr_descr;
	char	  *auto_expr_text;
	gboolean  auto_expr_use_max_precision;

	/* selection */
	char	  *selection_description;

	/* Style for feedback */
	GnmStyle const	*current_style;
	SheetObject	*validation_combo;
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
void		 wb_view_attach_control	  (WorkbookView *wbv, WorkbookControl *wbc);
void		 wb_view_detach_control	  (WorkbookControl *wbc);
void             wb_view_detach_from_workbook (WorkbookView *wbv);

/* Information */
GODoc		*wb_view_get_doc	  (WorkbookView const *wbv);
Workbook	*wb_view_get_workbook	  (WorkbookView const *wbv);
int		 wb_view_get_index_in_wb  (WorkbookView const *wbv);
Sheet		*wb_view_cur_sheet	  (WorkbookView const *wbv);
SheetView	*wb_view_cur_sheet_view	  (WorkbookView const *wbv);
void		 wb_view_sheet_focus	  (WorkbookView *wbv, Sheet *sheet);
void		 wb_view_sheet_add	  (WorkbookView *wbv, Sheet *new_sheet);
gboolean	 wb_view_is_protected	  (WorkbookView *wbv, gboolean check_sheet);

/* Manipulation */
void         	 wb_view_set_attribute	  (WorkbookView *wbv, char const *name,
					   char const *value);
void		 wb_view_preferred_size	  (WorkbookView *wbv,
					   int w_pixels, int h_pixels);
void		 wb_view_style_feedback   (WorkbookView *wbv);
void             wb_view_menus_update     (WorkbookView *wbv);
void		 wb_view_selection_desc   (WorkbookView *wbv, gboolean use_pos,
					   WorkbookControl *optional_wbc);
void		 wb_view_edit_line_set	  (WorkbookView *wbv,
					   WorkbookControl *optional_wbc);
void		 wb_view_auto_expr_recalc (WorkbookView *wbv);

/* I/O routines */
gboolean wb_view_save_as (WorkbookView *wbv, GOFileSaver *fs,
			  char const *file_name, GOCmdContext *cc);
gboolean wb_view_save	 (WorkbookView *wbv, GOCmdContext *cc);
void	 wbv_save_to_output (WorkbookView *wbv, GOFileSaver const *fs,
			     GsfOutput *output, IOContext *io_context);
gboolean wb_view_sendto	 (WorkbookView *wbv, GOCmdContext *cc);

WorkbookView *wb_view_new_from_input  (GsfInput *input,
				       GOFileOpener const *optional_format,
				       IOContext *io_context,
				       gchar const *optional_encoding);
WorkbookView *wb_view_new_from_uri  (char const *uri,
				     GOFileOpener const *optional_format,
				     IOContext *io_context,
				     gchar const *optional_encoding);

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


G_END_DECLS

#endif /* _GNM_WORKBOOK_VIEW_H_ */
