#ifndef GNUMERIC_WORKBOOK_H
#define GNUMERIC_WORKBOOK_H

#define WORKBOOK_TYPE        (workbook_get_type ())
#define WORKBOOK(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), WORKBOOK_TYPE, Workbook))
#define WORKBOOK_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), WORKBOOK_TYPE, WorkbookClass))
#define IS_WORKBOOK(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), WORKBOOK_TYPE))
#define IS_WORKBOOK_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), WORKBOOK_TYPE))

#include "gnumeric.h"
#include "summary.h"
#include "file.h"
#include <glib-object.h>

struct _Workbook {
	GObject  base;

	GPtrArray *wb_views;

	GPtrArray  *sheets;
	GHashTable *sheet_hash_private;
	GHashTable *sheet_order_dependents;

	gboolean modified;

	gchar          *filename;
	FileFormatLevel file_format_level;
	GnumFileSaver  *file_saver;

	/* Undo support */
	GSList	   *undo_commands;
	GSList	   *redo_commands;

	/* User defined names */
	GList      *names;

	/* Attached summary information */
	SummaryInfo *summary_info;

	struct {
		gboolean enabled;
		int      max_number;
		double   tolerance;
	} iteration;
	gboolean during_destruction;
	gboolean recursive_dirty_enabled;
};

typedef struct {
	GObjectClass   base;

	void (*summary_changed)     (Workbook *wb);
	void (*filename_changed)    (Workbook *wb);
	void (*sheet_order_changed) (Workbook *wb);
	void (*sheet_added)         (Workbook *wb);
	void (*sheet_deleted)       (Workbook *wb);
} WorkbookClass;

GType       workbook_get_type            (void);
Workbook   *workbook_new                 (void);
Workbook   *workbook_new_with_sheets     (int sheet_count);
void	    workbook_unref		 (Workbook *wb);

/* Sheet support routines */
GList      *workbook_sheets              (Workbook const *wb);
int         workbook_sheet_count         (Workbook const *wb);
Sheet      *workbook_sheet_by_index	 (Workbook const *wb, int i);
Sheet      *workbook_sheet_by_name       (Workbook const *wb, char const *sheet_name);
void        workbook_sheet_attach        (Workbook *wb, Sheet *new_sheet,
					  Sheet const *insert_after);
gboolean    workbook_sheet_detach        (Workbook *wb, Sheet *sheet);
Sheet	   *workbook_sheet_add		 (Workbook *wb,
					  Sheet const *insert_after,
					  gboolean make_dirty);
void        workbook_sheet_delete        (Sheet *sheet);
gboolean    workbook_sheet_move          (Sheet *sheet, int direction);
char       *workbook_sheet_get_free_name (Workbook *wb,
					  const char *base,
					  gboolean always_suffix,
					  gboolean handle_counter);
gboolean    workbook_sheet_reorder       (Workbook *wb, 
					  GSList *new_order, 
					  GSList *new_sheets);
gboolean    workbook_sheet_reorganize    (Workbook *wb,
					  GSList *changed_names, GSList *new_order,  
					  GSList *new_names,  GSList *old_names,
					  GSList **new_sheets, GSList *color_changed,
					  GSList *colors_fore, GSList *colors_back,
					  GSList *protection_changed, GSList *new_locks,
					  CommandContext *cc);

/* IO Routines */
gboolean       workbook_set_filename   (Workbook *wb, const gchar *);
const gchar   *workbook_get_filename   (Workbook *wb);
gboolean       workbook_set_saveinfo   (Workbook *wb, const gchar *,
                                        FileFormatLevel, GnumFileSaver *);
GnumFileSaver *workbook_get_file_saver (Workbook *wb);

void        workbook_print               (Workbook *, gboolean);

void        workbook_set_dirty           (Workbook *wb, gboolean is_dirty);
gboolean    workbook_is_dirty            (Workbook const *wb);
gboolean    workbook_is_pristine         (Workbook const *wb);
char       *workbook_selection_to_string (Workbook *wb, Sheet *base_sheet);

void        workbook_add_summary_info    (Workbook *wb, SummaryItem *sit);

/* See also sheet_cell_foreach_range */
Value	   *workbook_foreach_cell_in_range (EvalPos const *pos,
					    Value const	  *cell_range,
					    CellIterFlags  flags,
					    CellIterFunc   handler,
					    gpointer	   closure);
GPtrArray  *workbook_cells               (Workbook *wb, gboolean comments);

/* Calculation */
void     workbook_recalc                 (Workbook *wb);	/* in eval.c */
void     workbook_recalc_all             (Workbook *wb);	/* in eval.c */
gboolean workbook_enable_recursive_dirty (Workbook *wb, gboolean enable);
void     workbook_iteration_enabled	 (Workbook *wb, gboolean enable);
void     workbook_iteration_max_number	 (Workbook *wb, int max_number);
void     workbook_iteration_tolerance	 (Workbook *wb, double tolerance);

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


#define WORKBOOK_FOREACH_SHEET(wb, sheet, code)					\
  do {										\
	unsigned _sheetno;							\
	for (_sheetno = 0; _sheetno < (wb)->sheets->len; _sheetno++) {		\
		Sheet *sheet = g_ptr_array_index ((wb)->sheets, _sheetno);	\
		code;								\
	}									\
  } while (0)

/*
 * Walk the dependents.  WARNING: Note, that it is only valid to muck with
 * the current dependency in the code.
 */
#define WORKBOOK_FOREACH_DEPENDENT(wb, dep, code)			\
  do {									\
	/* Maybe external deps here.  */				\
									\
	WORKBOOK_FOREACH_SHEET(wb, _wfd_sheet, {			\
		SHEET_FOREACH_DEPENDENT (_wfd_sheet, dep, code);	\
	});								\
  } while (0)

#endif
