#ifndef GNUMERIC_COMMAND_H
#define GNUMERIC_COMMAND_H

#include "gnumeric.h"
#include "command-context.h"
#include "format-template.h"
#include "sort.h"

void command_undo (CommandContext *context, Workbook *wb);
void command_redo (CommandContext *context, Workbook *wb);
void command_list_release	(GSList *cmds);

gboolean cmd_set_text		(CommandContext *context, Sheet *sheet,
				 CellPos const *pos, const char *new_text);

gboolean cmd_area_set_text	(CommandContext *context, EvalPos const *pos,
				 const char *text, gboolean as_array);

gboolean cmd_set_date_time	(CommandContext *context, Sheet *sheet,
				 const CellPos *pos, gboolean is_date);

gboolean cmd_insert_cols	(CommandContext *context, Sheet *sheet,
				 int start_col, int count);
gboolean cmd_insert_rows	(CommandContext *context, Sheet *sheet,
				 int start_row, int count);
gboolean cmd_delete_cols	(CommandContext *context, Sheet *sheet,
				 int start_col, int count);
gboolean cmd_delete_rows	(CommandContext *context, Sheet *sheet,
				 int start_row, int count);

gboolean cmd_resize_row_col	(CommandContext *context, Sheet *sheet,
				 gboolean is_col, ColRowIndexList *selection,
				 int new_size);

gboolean cmd_paste_cut		(CommandContext *context,
				 const ExprRelocateInfo *info);
gboolean cmd_paste_copy		(CommandContext *context,
				 const PasteTarget *pt, CellRegion *content);

gboolean cmd_rename_sheet	(CommandContext *context, Workbook *wb,
				 const char *old_name, const char *new_name);


gboolean cmd_sort		(CommandContext *context, SortData *data);

gboolean cmd_format		(CommandContext *context, Sheet *sheet,
				 MStyle *style, MStyleBorder **borders);

gboolean cmd_autofill		(CommandContext *context, Sheet *sheet,
				 int base_col, int base_row,
				 int w, int h, int end_col, int end_row);

gboolean cmd_autoformat         (CommandContext *context, Sheet *sheet, FormatTemplate *ft);

gboolean cmd_clear_selection	(CommandContext *context, Sheet *sheet,
				 int const clear_flags);
gboolean cmd_hide_selection_rows_cols (CommandContext *context, Sheet *sheet,
				       gboolean is_cols, gboolean visible);

#endif /* GNUMERIC_COMMAND_H */
