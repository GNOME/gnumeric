/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNUMERIC_COMMANDS_H
#define GNUMERIC_COMMANDS_H

#include "gnumeric.h"
#include "format-template.h"
#include "sort.h"

void command_undo (WorkbookControl *wbc);
void command_redo (WorkbookControl *wbc);
void command_setup_combos	(WorkbookControl *wbc);
void command_list_release	(GSList *cmds);

gboolean cmd_set_text		(WorkbookControl *wbc, Sheet *sheet,
				 CellPos const *pos, const char *new_text);

gboolean cmd_area_set_text	(WorkbookControl *wbc, EvalPos const *pos,
				 const char *text, gboolean as_array);

gboolean cmd_set_date_time	(WorkbookControl *wbc, Sheet *sheet,
				 const CellPos *pos, gboolean is_date);

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
				 const ExprRelocateInfo *info,
				 gboolean move_selection);
gboolean cmd_paste_copy		(WorkbookControl *wbc,
				 const PasteTarget *pt, CellRegion *content);

gboolean cmd_rename_sheet	(WorkbookControl *wbc,
				 const char *old_name, const char *new_name);

gboolean cmd_sort		(WorkbookControl *wbc, SortData *data);

gboolean cmd_format		(WorkbookControl *wbc, Sheet *sheet,
				 MStyle *style, StyleBorder **borders,
				 char const *opt_translated_name);

gboolean cmd_autofill		(WorkbookControl *wbc, Sheet *sheet,
				 int base_col, int base_row,
				 int w, int h, int end_col, int end_row);

gboolean cmd_clear_selection	(WorkbookControl *wbc, Sheet *sheet,
				 int const clear_flags);

gboolean cmd_autoformat         (WorkbookControl *wbc, Sheet *sheet, FormatTemplate *ft);

gboolean cmd_colrow_hide_selection (WorkbookControl *wbc, Sheet *sheet,
				    gboolean is_cols, gboolean visible);
gboolean cmd_colrow_outline_change (WorkbookControl *wbc, Sheet *sheet,
				    gboolean is_cols, int index, int depth);

gboolean cmd_merge_cells	(WorkbookControl *wbc, Sheet *sheet,
				 GList const *selection);
gboolean cmd_unmerge_cells	(WorkbookControl *wbc, Sheet *sheet,
				 GList const *selection);

gboolean cmd_search_replace     (WorkbookControl *wbc, Sheet *sheet, SearchReplace *sr);

gboolean cmd_colrow_std_size    (WorkbookControl *wbc, Sheet *sheet,
				 gboolean is_cols, double new_default);

gboolean cmd_zoom               (WorkbookControl *wbc, GSList *sheets, double factor);

#endif /* GNUMERIC_COMMANDS_H */
