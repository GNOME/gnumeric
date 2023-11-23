#ifndef _GNM_WORKBOOK_PRIV_H_
# define _GNM_WORKBOOK_PRIV_H_

#include <workbook.h>
#include <goffice/goffice.h>
#include <numbers.h>

G_BEGIN_DECLS

struct _Workbook {
	GODoc	doc;

#ifndef __GI_SCANNER__
	GPtrArray *wb_views;

	GPtrArray  *sheets;
	GHashTable *sheet_hash_private;
	GHashTable *sheet_order_dependents;
	GHashTable *sheet_local_functions;

	gboolean sheet_size_cached;
	GnmSheetSize sheet_size;

	gboolean is_placeholder;

	GOFileFormatLevel  file_format_level;
	GOFileFormatLevel  file_export_format_level;
	GOFileSaver	*file_saver;
	GOFileSaver	*file_exporter;
	char            *last_export_uri;

	/* Undo support */
	GSList	   *undo_commands;
	GSList	   *redo_commands;

	GnmNamedExprCollection *names;

	/* Calculation options */
	struct {
		gboolean enabled;
		int      max_number;
		gnm_float tolerance;
	} iteration;
	gboolean recalc_auto;
	GODateConventions const *date_conv;

	gboolean during_destruction;
	gboolean being_reordered;
	gboolean recursive_dirty_enabled;
	gboolean being_loaded;
#endif
};

typedef struct {
	GODocClass base;

	void (*sheet_order_changed) (Workbook *wb);
	void (*sheet_added)         (Workbook *wb);
	void (*sheet_deleted)       (Workbook *wb);
} WorkbookClass;

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
	WORKBOOK_FOREACH_VIEW((wb), view,				\
		WORKBOOK_VIEW_FOREACH_CONTROL(view, control, code);)

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

G_END_DECLS

#endif /* _GNM_WORKBOOK_PRIV_H_ */
