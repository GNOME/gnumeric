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
void	    workbook_sheet_detach        (Workbook *wb, Sheet *sheet, gboolean recalc);
gboolean    workbook_sheet_hide_controls (Workbook *wb, Sheet *sheet);
void        workbook_sheet_unhide_controls (Workbook *wb, Sheet *sheet);
Sheet	   *workbook_sheet_add		 (Workbook *wb,
					  Sheet const *insert_after,
					  gboolean make_dirty);
void        workbook_sheet_delete        (Sheet *sheet);
gboolean    workbook_sheet_move          (Sheet *sheet, int direction);
char       *workbook_sheet_get_free_name (Workbook *wb,
					  char const *base,
					  gboolean always_suffix,
					  gboolean handle_counter);
gboolean    workbook_sheet_reorder       (Workbook *wb,
					  GSList *new_order);
gboolean    workbook_sheet_reorder_by_idx(Workbook *wb,
					  GSList *new_order);
gboolean    workbook_sheet_recolor       (Workbook *wb,
					  GSList *sheets,
					  GSList *fore,
					  GSList *back);
gboolean    workbook_sheet_rename        (Workbook *wb,
					  GSList *sheet_indices,
					  GSList *new_names,
					  GnmCmdContext *cc);
gboolean    workbook_sheet_rename_check  (Workbook *wb,
					  GSList *sheet_indices,
					  GSList *new_names,
					  GSList *sheet_indices_deleted,
					  GnmCmdContext *cc);
gboolean    workbook_sheet_change_protection  (Workbook *wb,
					       GSList *sheets,
					       GSList *locks);
gboolean    workbook_sheet_change_visibility  (Workbook *wb,
					       GSList *sheets,
					       GSList *visibility);

unsigned    workbook_find_command	(Workbook *wb,
					 gboolean is_undo, gpointer cmd);


/* IO Routines */
gboolean       workbook_set_uri		(Workbook *wb, char const *uri);
char const    *workbook_get_uri		(Workbook const *wb);
char const    *workbook_get_basename	(Workbook const *wb);

gboolean       workbook_set_saveinfo	(Workbook *wb,
					 FileFormatLevel, GnmFileSaver *);
GnmFileSaver *workbook_get_file_saver	(Workbook *wb);

gboolean    workbook_is_pristine	(Workbook const *wb);
void        workbook_set_dirty		(Workbook *wb, gboolean is_dirty);
gboolean    workbook_is_dirty		(Workbook const *wb);
void        workbook_set_placeholder	(Workbook *wb, gboolean is_placeholder);
gboolean    workbook_is_placeholder	(Workbook const *wb);

void         workbook_add_summary_info    (Workbook *wb, SummaryItem *sit);
SummaryInfo *workbook_metadata    	  (Workbook const *wb);

/* See also sheet_cell_foreach_range */
GnmValue   *workbook_foreach_cell_in_range (GnmEvalPos const  *pos,
					    GnmValue const *cell_range,
					    CellIterFlags   flags,
					    CellIterFunc    handler,
					    gpointer	    closure);
GPtrArray  *workbook_cells               (Workbook *wb, gboolean comments);
GSList     *workbook_local_functions	 (Workbook const *wb);

/* Calculation */
void     workbook_recalc                 (Workbook *wb);	/* in eval.c */
void     workbook_recalc_all             (Workbook *wb);	/* in eval.c */
gboolean workbook_enable_recursive_dirty (Workbook *wb, gboolean enable);
void     workbook_autorecalc_enable	 (Workbook *wb, gboolean enable);
gboolean workbook_autorecalc             (Workbook *wb);
void     workbook_iteration_enabled	 (Workbook *wb, gboolean enable);
void     workbook_iteration_max_number	 (Workbook *wb, int max_number);
void     workbook_iteration_tolerance	 (Workbook *wb, double tolerance);

GnmDateConventions const *workbook_date_conv (Workbook const *wb);
gboolean workbook_set_1904 (Workbook *wb, gboolean flag);

void workbook_attach_view (Workbook *wb, WorkbookView *wbv);
void workbook_detach_view (WorkbookView *wbv);

#endif /* GNUMERIC_WORKBOOK_H */
