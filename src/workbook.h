#ifndef _GNM_WORKBOOK_H_
# define _GNM_WORKBOOK_H_

#include <gnumeric.h>
#include <goffice/goffice.h>
#include <glib-object.h>
#include <gui-file.h>
#include <numbers.h>

G_BEGIN_DECLS

#define GNM_WORKBOOK_TYPE        (workbook_get_type ())
#define WORKBOOK(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_WORKBOOK_TYPE, Workbook))
#define GNM_IS_WORKBOOK(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_WORKBOOK_TYPE))

GType       workbook_get_type            (void);
Workbook   *workbook_new                 (void);
Workbook   *workbook_new_with_sheets     (int sheet_count);

void        workbook_mark_dirty          (Workbook *wb);

/* Sheet support routines */
GPtrArray  *workbook_sheets              (Workbook const *wb);
int         workbook_sheet_count         (Workbook const *wb);
Sheet      *workbook_sheet_by_index	 (Workbook const *wb, int i);
Sheet      *workbook_sheet_by_name       (Workbook const *wb, char const *sheet_name);
void        workbook_sheet_attach        (Workbook *wb, Sheet *new_sheet);
void        workbook_sheet_attach_at_pos (Workbook *wb, Sheet *new_sheet, int pos);
Sheet	   *workbook_sheet_add		 (Workbook *wb, int pos, int columns, int rows);
Sheet	   *workbook_sheet_add_with_type (Workbook *wb, GnmSheetType sheet_type, int pos, int columns, int rows);
void        workbook_sheet_delete        (Sheet *sheet);
void        workbook_sheet_move          (Sheet *sheet, int direction);
char       *workbook_sheet_get_free_name (Workbook *wb,
					  char const *base,
					  gboolean always_suffix,
					  gboolean handle_counter);
gboolean    workbook_sheet_reorder       (Workbook *wb,
					  GSList *new_order);
gboolean    workbook_sheet_rename        (Workbook *wb,
					  GSList *sheet_indices,
					  GSList *new_names,
					  GOCmdContext *cc);

unsigned    workbook_find_command	(Workbook *wb,
					 gboolean is_undo, gpointer cmd);

GnmExprSharer *workbook_share_expressions (Workbook *wb, gboolean freeit);
void        workbook_optimize_style     (Workbook *wb);

void        workbook_update_graphs      (Workbook *wb);

/* IO Routines */
gboolean       workbook_set_saveinfo	(Workbook *wb, GOFileFormatLevel lev,
					 GOFileSaver *saver);
void           workbook_update_history  (Workbook *wb, GnmFileSaveAsStyle type);
GOFileSaver *workbook_get_file_saver	(Workbook *wb);
GOFileSaver *workbook_get_file_exporter	(Workbook *wb);
gchar const *workbook_get_last_export_uri (Workbook *wb);
void         workbook_set_file_exporter	  (Workbook *wb, GOFileSaver *fs);
void         workbook_set_last_export_uri (Workbook *wb, const gchar *uri);

/* See also sheet_foreach_cell_in_region */
GnmValue   *workbook_foreach_cell_in_range (GnmEvalPos const  *pos,
					    GnmValue const *cell_range,
					    CellIterFlags   flags,
					    CellIterFunc    handler,
					    gpointer	    closure);
GPtrArray  *workbook_cells               (Workbook *wb, gboolean comments,
					  GnmSheetVisibility vis);

void workbook_foreach_name (Workbook const *wb, gboolean globals_only,
			    GHFunc func, gpointer data);


/* Calculation */
void     workbook_recalc                 (Workbook *wb); /* in dependent.c */
void     workbook_recalc_all             (Workbook *wb); /* in dependent.c */
gboolean workbook_enable_recursive_dirty (Workbook *wb, gboolean enable);
void     workbook_set_recalcmode	 (Workbook *wb, gboolean enable);
gboolean workbook_get_recalcmode         (Workbook const *wb);
void     workbook_iteration_enabled	 (Workbook *wb, gboolean enable);
void     workbook_iteration_max_number	 (Workbook *wb, int max_number);
void     workbook_iteration_tolerance	 (Workbook *wb, gnm_float tolerance);

GODateConventions const *workbook_date_conv (Workbook const *wb);
void workbook_set_date_conv (Workbook *wb, GODateConventions const *date_conv);
void workbook_set_1904 (Workbook *wb, gboolean base1904);

GnmSheetSize const *workbook_get_sheet_size (Workbook const *wb);

void workbook_attach_view (WorkbookView *wbv);
void workbook_detach_view (WorkbookView *wbv);

GType workbook_sheet_state_get_type (void);
WorkbookSheetState *workbook_sheet_state_new (Workbook const *wb);
void workbook_sheet_state_unref (WorkbookSheetState *wss);
void workbook_sheet_state_restore (Workbook *wb, WorkbookSheetState const *wss);
int workbook_sheet_state_size	(WorkbookSheetState const *wss);
char *workbook_sheet_state_diff (WorkbookSheetState const *wss_a,
				 WorkbookSheetState const *wss_b);

// For bindings only!
GSList *gnm_workbook_sheets0 (Workbook const *wb);

#define WORKBOOK_FOREACH_SHEET(wb, sheet, code)				\
  do {									\
	  const Workbook *wb_ = (wb);					\
	  unsigned sheetno_;						\
	  unsigned sheetcount_ = workbook_sheet_count (wb_);		\
	  for (sheetno_ = 0; sheetno_ < sheetcount_; sheetno_++) {	\
		  Sheet *sheet = workbook_sheet_by_index (wb_, sheetno_); \
		  code;							\
	  }								\
  } while (0)


G_END_DECLS

#endif /* _GNM_WORKBOOK_H_ */
