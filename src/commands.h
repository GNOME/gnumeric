#ifndef GNUMERIC_COMMAND_H
#define GNUMERIC_COMMAND_H

#include "gnumeric.h"
#include "command-context.h"

void command_undo (CommandContext *context, Workbook *wb);
void command_redo (CommandContext *context, Workbook *wb);

void command_list_pop_top_undo (Workbook *wb);
void command_list_release      (GSList *cmds);

gboolean cmd_set_text (CommandContext *context,
		       Sheet *sheet, CellPos const * const pos,
		       char * new_text,
		       String const * const old_text);

gboolean cmd_set_date_time (CommandContext *context, gboolean is_date,
			    Sheet *sheet, int col, int row);

gboolean cmd_insert_cols (CommandContext *context,
			  Sheet *sheet, int start_col, int count);
gboolean cmd_insert_rows (CommandContext *context,
			  Sheet *sheet, int start_row, int count);
gboolean cmd_delete_cols (CommandContext *context,
			  Sheet *sheet, int start_col, int count);
gboolean cmd_delete_rows (CommandContext *context,
			  Sheet *sheet, int start_row, int count);

gboolean cmd_resize_row_col (CommandContext *context, gboolean is_col,
			     Sheet *sheet, int index);

gboolean cmd_paste_cut (CommandContext *context,
			ExprRelocateInfo const * const info);

gboolean cmd_rename_sheet (CommandContext *context, Workbook *wb,
			   const char *old_name, const char *new_name);

#endif /* GNUMERIC_COMMAND_CONTEXT_H */
