#ifndef GNUMERIC_WORKBOOK_H
#define GNUMERIC_WORKBOOK_H

#define WORKBOOK_TYPE        (workbook_get_type ())
#define WORKBOOK(o)          (GTK_CHECK_CAST ((o), WORKBOOK_TYPE, Workbook))
#define WORKBOOK_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), WORKBOOK_TYPE, WorkbookClass))
#define IS_WORKBOOK(o)       (GTK_CHECK_TYPE ((o), WORKBOOK_TYPE))
#define IS_WORKBOOK_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), WORKBOOK_TYPE))

#include "gnumeric.h"
#include "summary.h"
#include "file.h"
#include <gtk/gtkobject.h>

typedef struct _WorkbookPrivate WorkbookPrivate;
struct _Workbook {
	GtkObject  gtk_object;

	GPtrArray *wb_views;

	GPtrArray  *sheets;
	GHashTable *sheet_hash_private;

	/* Attribute list */
	GList *attributes;

	gchar          *filename;
	FileFormatLevel file_format_level;
	GnumFileSaver  *file_saver;
	gint            file_saver_sig_id;

	/* Undo support */
	GSList	   *undo_commands;
	GSList	   *redo_commands;

	/* User defined names */
	GList      *names;

	/* All objects with expressions */
	GList      *dependents;

	/* The dependents to be evaluated */
	GList     *eval_queue;
	int        max_iterations;

	guint8     generation;

	/* Attached summary information */
	SummaryInfo *summary_info;

	void       *corba_server;

	WorkbookPrivate *priv;
};

typedef struct {
	GtkObjectClass   gtk_parent_class;

	/* Signals */
	void (*cell_changed)  (Sheet *sheet, char *contents,
			       int col, int row);
} WorkbookClass;

GtkType     workbook_get_type            (void);
Workbook   *workbook_new                 (void);
Workbook   *workbook_new_with_sheets     (int sheet_count);
void	    workbook_unref		 (Workbook *wb);

/* Sheet support routines */
GList      *workbook_sheets              (Workbook const *wb);
int         workbook_sheet_count         (Workbook const *wb);
int	    workbook_sheet_index_get	 (Workbook const *wb, Sheet const * sheet);
Sheet      *workbook_sheet_by_index	 (Workbook *wb, int i);
Sheet      *workbook_sheet_by_name       (Workbook *wb, const char *sheet_name);
void        workbook_sheet_attach        (Workbook *, Sheet *new_sheet,
					  Sheet const *insert_after);
gboolean    workbook_sheet_detach        (Workbook *, Sheet *);
Sheet	   *workbook_sheet_add		 (Workbook *wb,
					  Sheet const *insert_after,
					  gboolean make_dirty);
void        workbook_sheet_delete        (Sheet *sheet);
void        workbook_sheet_move          (Sheet *sheet, int direction);
char       *workbook_sheet_get_free_name (Workbook *wb,
					  const char *base,
					  gboolean always_suffix,
					  gboolean handle_counter);
gboolean    workbook_sheet_rename        (WorkbookControl *,
					  Workbook *wb,
					  const char *old_name,
					  const char *new_name);

/* IO Routines */
gboolean       workbook_set_filename   (Workbook *wb, const char *);
gboolean       workbook_set_saveinfo   (Workbook *wb, const gchar *,
                                        FileFormatLevel, GnumFileSaver *);
GnumFileSaver *workbook_get_file_saver (Workbook *wb);

void        workbook_print               (Workbook *, gboolean);

void        workbook_set_dirty           (Workbook *wb, gboolean is_dirty);
gboolean    workbook_is_dirty            (Workbook *wb);
gboolean    workbook_is_pristine         (Workbook *wb);
char       *workbook_selection_to_string (Workbook *wb, Sheet *base_sheet);

GSList     *workbook_expr_relocate       (Workbook *wb,
					  ExprRelocateInfo const *info);
void        workbook_expr_unrelocate     (Workbook *wb, GSList *info);
void        workbook_expr_unrelocate_free(GSList *info);

/* See also sheet_cell_foreach_range */
Value	   *workbook_foreach_cell_in_range (EvalPos const *pos,
					    Value const	  *cell_range,
					    gboolean	   only_existing,
					    ForeachCellCB  handler,
					    void	  *closure);
GPtrArray  *workbook_cells               (Workbook *wb, gboolean comments);


/*
 * Does any pending recalculations
 */
void        workbook_recalc              (Workbook *wb);
void        workbook_recalc_all          (Workbook *wb);
void        workbook_calc_spans          (Workbook *wb, SpanCalcFlags const flags);

/*
 * Hooks for CORBA bootstrap: they create the
 */
void workbook_corba_setup    (Workbook *);
void workbook_corba_shutdown (Workbook *);

void workbook_attach_view (Workbook *wb, WorkbookView *wbv);
void workbook_detach_view (WorkbookView *wbv);

#define WORKBOOK_FOREACH_VIEW(wb, view, code)					\
do {										\
	int InD;								\
	GPtrArray *wb_views = (wb)->wb_views;					\
	if (wb_views != NULL) /* Reverse is important during destruction */	\
		for (InD = wb_views->len; InD-- > 0; ) {			\
			WorkbookView *view = g_ptr_array_index (wb_views, InD);	\
			code							\
		}								\
} while (0)

#define WORKBOOK_FOREACH_CONTROL(wb, view, control, code)		\
	WORKBOOK_FOREACH_VIEW((wb), view, 				\
		WORKBOOK_VIEW_FOREACH_CONTROL(view, control, code);)

#endif
