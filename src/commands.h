/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNUMERIC_COMMANDS_H
#define GNUMERIC_COMMANDS_H

#include <gtk/gtkobject.h>

#include "consolidate.h"
#include "gnumeric.h"
#include "sort.h"

void command_undo (WorkbookControl *wbc);
void command_redo (WorkbookControl *wbc);
void command_setup_combos	(WorkbookControl *wbc);
void command_list_release	(GSList *cmds);

gboolean cmd_set_text		(WorkbookControl *wbc, Sheet *sheet,
				 CellPos const *pos, const char *new_text);

gboolean cmd_area_set_text	(WorkbookControl *wbc, ParsePos const *pos,
				 char const *text, gboolean as_array);

gboolean cmd_set_date_time	(WorkbookControl *wbc, Sheet *sheet,
				 CellPos const *pos, gboolean is_date);

gboolean cmd_insert_cols	(WorkbookControl *wbc, Sheet *sheet,
				 int start_col, int count);
gboolean cmd_insert_rows	(WorkbookControl *wbc, Sheet *sheet,
				 int start_row, int count);
gboolean cmd_delete_cols	(WorkbookControl *wbc, Sheet *sheet,
				 int start_col, int count);
gboolean cmd_delete_rows	(WorkbookControl *wbc, Sheet *sheet,
				 int start_row, int count);

gboolean cmd_resize_colrow	(WorkbookControl *wbc, Sheet *sheet,
				 gboolean is_col, ColRowIndexList *selection,
				 int new_size);

gboolean cmd_paste_cut		(WorkbookControl *wbc,
				 ExprRelocateInfo const *info,
				 gboolean move_selection,
				 char *cmd_descriptor);
gboolean cmd_paste_copy		(WorkbookControl *wbc,
				 PasteTarget const *pt, CellRegion *content);

gboolean cmd_rename_sheet	(WorkbookControl *wbc,
				 char const *old_name, char const *new_name);

gboolean cmd_sort		(WorkbookControl *wbc, SortData *data);

gboolean cmd_format		(WorkbookControl *wbc, Sheet *sheet,
				 MStyle *style, StyleBorder **borders,
				 char const *opt_translated_name);

gboolean cmd_autofill		(WorkbookControl *wbc, Sheet *sheet,
				 gboolean default_increment,
				 int base_col, int base_row,
				 int w, int h, int end_col, int end_row,
				 gboolean inverse_autofill);

gboolean cmd_clear_selection	(WorkbookControl *wbc, Sheet *sheet,
				 int clear_flags);

gboolean cmd_autoformat         (WorkbookControl *wbc, Sheet *sheet,
				 FormatTemplate const *ft);

gboolean cmd_colrow_hide_selection (WorkbookControl *wbc, Sheet *sheet,
				    gboolean is_cols, gboolean visible);
gboolean cmd_colrow_outline_change (WorkbookControl *wbc, Sheet *sheet,
				    gboolean is_cols, int index, int depth);

gboolean cmd_group              (WorkbookControl *wbc, Sheet *sheet,
				 gboolean is_cols, gboolean group);

gboolean cmd_merge_cells	(WorkbookControl *wbc, Sheet *sheet,
				 GSList const *selection);
gboolean cmd_unmerge_cells	(WorkbookControl *wbc, Sheet *sheet,
				 GSList const *selection);

gboolean cmd_search_replace     (WorkbookControl *wbc, Sheet *sheet, SearchReplace *sr);

gboolean cmd_colrow_std_size    (WorkbookControl *wbc, Sheet *sheet,
				 gboolean is_cols, double new_default);

gboolean cmd_consolidate        (WorkbookControl *wbc, Consolidate *cs);
				 
gboolean cmd_zoom               (WorkbookControl *wbc, GSList *sheets, double factor);

gboolean cmd_object_insert	(WorkbookControl *wbc, SheetObject *so,
				 Sheet *sheet);
gboolean cmd_object_delete	(WorkbookControl *wbc, SheetObject *so);
gboolean cmd_object_move	(WorkbookControl *wbc, SheetObject *so,
				 SheetObjectAnchor const *old_anchor,
				 gboolean resize);

#endif /* GNUMERIC_COMMANDS_H */
