#ifndef GNUMERIC_WORKBOOK_PRIV_H
#define GNUMERIC_WORKBOOK_PRIV_H

#include "workbook.h"
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

	SummaryInfo *summary_info;

	/* Calculation options */
	struct {
		gboolean enabled;
		int      max_number;
		double   tolerance;
	} iteration;
	gboolean auto_recalc;

	gboolean during_destruction : 1;
	gboolean being_reordered : 1;
	gboolean recursive_dirty_enabled : 1;
};

typedef struct {
	GObjectClass   base;

	void (*summary_changed)     (Workbook *wb);
	void (*filename_changed)    (Workbook *wb);
	void (*sheet_order_changed) (Workbook *wb);
	void (*sheet_added)         (Workbook *wb);
	void (*sheet_deleted)       (Workbook *wb);
} WorkbookClass;

#define WORKBOOK_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), WORKBOOK_TYPE, WorkbookClass))
#define IS_WORKBOOK_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), WORKBOOK_TYPE))

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

#endif /* GNUMERIC_WORKBOOK_PRIV_H */
