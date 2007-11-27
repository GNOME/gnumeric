/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_WORKBOOK_H_
# define _GNM_WORKBOOK_H_

#include "gnumeric.h"
#include <goffice/app/file.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define WORKBOOK_TYPE        (workbook_get_type ())
#define WORKBOOK(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), WORKBOOK_TYPE, Workbook))
#define IS_WORKBOOK(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), WORKBOOK_TYPE))

GType       workbook_get_type            (void);
Workbook   *workbook_new                 (void);
Workbook   *workbook_new_with_sheets     (int sheet_count);

/* Sheet support routines */
GList      *workbook_sheets              (Workbook const *wb);
int         workbook_sheet_count         (Workbook const *wb);
Sheet      *workbook_sheet_by_index	 (Workbook const *wb, int i);
Sheet      *workbook_sheet_by_name       (Workbook const *wb, char const *sheet_name);
void        workbook_sheet_attach        (Workbook *wb, Sheet *new_sheet);
void        workbook_sheet_attach_at_pos (Workbook *wb, Sheet *new_sheet, int pos);
Sheet	   *workbook_sheet_add		 (Workbook *wb, int pos);
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

/* IO Routines */
gboolean       workbook_set_saveinfo	(Workbook *wb, FileFormatLevel lev,
					 GOFileSaver *saver);
void           workbook_update_history  (Workbook *wb);
GOFileSaver *workbook_get_file_saver	(Workbook *wb);

/* See also sheet_cell_foreach_range */
GnmValue   *workbook_foreach_cell_in_range (GnmEvalPos const  *pos,
					    GnmValue const *cell_range,
					    CellIterFlags   flags,
					    CellIterFunc    handler,
					    gpointer	    closure);
GPtrArray  *workbook_cells               (Workbook *wb, gboolean comments,
					  GnmSheetVisibility vis);
GSList     *workbook_local_functions	 (Workbook const *wb);

/* Calculation */
void     workbook_recalc                 (Workbook *wb);	/* in eval.c */
void     workbook_recalc_all             (Workbook *wb);	/* in eval.c */
gboolean workbook_enable_recursive_dirty (Workbook *wb, gboolean enable);
void     workbook_set_recalcmode	 (Workbook *wb, gboolean enable);
gboolean workbook_get_recalcmode         (Workbook const *wb);
void     workbook_iteration_enabled	 (Workbook *wb, gboolean enable);
void     workbook_iteration_max_number	 (Workbook *wb, int max_number);
void     workbook_iteration_tolerance	 (Workbook *wb, double tolerance);

GODateConventions const *workbook_date_conv (Workbook const *wb);
gboolean workbook_set_1904 (Workbook *wb, gboolean flag);

void workbook_attach_view (WorkbookView *wbv);
void workbook_detach_view (WorkbookView *wbv);

WorkbookSheetState *workbook_sheet_state_new (Workbook const *wb);
void workbook_sheet_state_free (WorkbookSheetState *wss);
void workbook_sheet_state_restore (Workbook *wb, WorkbookSheetState const *wss);
int workbook_sheet_state_size	(WorkbookSheetState const *wss);
char *workbook_sheet_state_diff (WorkbookSheetState const *wss_a,
				 WorkbookSheetState const *wss_b);

G_END_DECLS

#endif /* _GNM_WORKBOOK_H_ */
