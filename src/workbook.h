#ifndef GNUMERIC_WORKBOOK_H
#define GNUMERIC_WORKBOOK_H

#define WORKBOOK_TYPE        (workbook_get_type ())
#define WORKBOOK(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), WORKBOOK_TYPE, Workbook))
#define IS_WORKBOOK(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), WORKBOOK_TYPE))

#include "gnumeric.h"
#include "summary.h"
#include "file.h"
#include <glib-object.h>

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
char const    *workbook_get_filename   (Workbook *wb);
gboolean       workbook_set_saveinfo   (Workbook *wb, const gchar *,
                                        FileFormatLevel, GnumFileSaver *);
GnumFileSaver *workbook_get_file_saver (Workbook *wb);

void        workbook_print               (Workbook *, gboolean);

void        workbook_set_dirty           (Workbook *wb, gboolean is_dirty);
gboolean    workbook_is_dirty            (Workbook const *wb);
gboolean    workbook_is_pristine         (Workbook const *wb);
char       *workbook_selection_to_string (Workbook *wb, Sheet *base_sheet);

void         workbook_add_summary_info    (Workbook *wb, SummaryItem *sit);
SummaryInfo *workbook_metadata    	  (Workbook *wb);

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

void workbook_calc_spans  (Workbook *wb, SpanCalcFlags const flags);

void workbook_attach_view (Workbook *wb, WorkbookView *wbv);
void workbook_detach_view (WorkbookView *wbv);

#endif
