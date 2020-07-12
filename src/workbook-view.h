#ifndef _GNM_WORKBOOK_VIEW_H_
# define _GNM_WORKBOOK_VIEW_H_

#include <gnumeric.h>
#include <dependent.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

struct _WorkbookView {
	GoView  base;

	Workbook *wb;
	GPtrArray *wb_controls;

	Sheet	  *current_sheet;	/* convenience */
	SheetView *current_sheet_view;

	/* preferences */
	gboolean   show_horizontal_scrollbar;
	gboolean   show_vertical_scrollbar;
	gboolean   show_notebook_tabs;
	gboolean   show_function_cell_markers;
	gboolean   show_extension_markers;
	gboolean   do_auto_completion;
	gboolean   is_protected;

	/* Non-normative size information */
	int preferred_width, preferred_height;

	/* The auto-expression */
	struct {
		GnmFunc *func;
		char *descr;
		GnmValue *value;
		gboolean use_max_precision;
		GnmDepManaged dep;
		gulong sheet_detached_sig;
	} auto_expr;

	/* selection */
	char	  *selection_description;

	/* Style for feedback */
	GnmStyle const	*current_style;
	SheetObject	*in_cell_combo;	/* validation or data slicer */
};

typedef struct {
	GObjectClass   base_class;
	void (*sheet_entered) (Sheet *sheet);
} WorkbookViewClass;

#define GNM_WORKBOOK_VIEW_TYPE     (workbook_view_get_type ())
#define GNM_WORKBOOK_VIEW(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNM_WORKBOOK_VIEW_TYPE, WorkbookView))
#define GNM_IS_WORKBOOK_VIEW(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_WORKBOOK_VIEW_TYPE))

/* Lifecycle */
GType		 workbook_view_get_type	  (void);
WorkbookView	*workbook_view_new	  (Workbook *wb);
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
void        	 wb_view_set_attribute	  (WorkbookView *wbv, char const *name,
					   char const *value);
void		 wb_view_preferred_size	  (WorkbookView *wbv,
					   int w_pixels, int h_pixels);
void		 wb_view_style_feedback   (WorkbookView *wbv);
void             wb_view_menus_update     (WorkbookView *wbv);
void		 wb_view_selection_desc   (WorkbookView *wbv, gboolean use_pos,
					   WorkbookControl *wbc);
void		 wb_view_edit_line_set	  (WorkbookView *wbv,
					   WorkbookControl *wbc);
void		 wb_view_auto_expr_recalc (WorkbookView *wbv);

/* I/O routines */
gboolean workbook_view_save_as (WorkbookView *wbv, GOFileSaver *fs,
				char const *uri, GOCmdContext *cc);
gboolean workbook_view_save	 (WorkbookView *wbv, GOCmdContext *cc);
void	 workbook_view_save_to_output (WorkbookView *wbv,
				       GOFileSaver const *fs,
				       GsfOutput *output,
				       GOIOContext *io_context);
void     workbook_view_save_to_uri (WorkbookView *wbv, GOFileSaver const *fs,
				    char const *uri, GOIOContext *io_context);

WorkbookView *workbook_view_new_from_input (GsfInput *input,
                                            const char *uri,
                                            GOFileOpener const *file_opener,
                                            GOIOContext *io_context,
                                            gchar const *encoding);
WorkbookView *workbook_view_new_from_uri  (char const *uri,
                                           GOFileOpener const *file_opener,
                                           GOIOContext *io_context,
                                           gchar const *encoding);

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
