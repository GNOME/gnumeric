#ifndef GNUMERIC_COMMAND_H
#define GNUMERIC_COMMAND_H

#include "command-context.h"

void command_undo (CommandContext *context, Workbook *wb);
void command_redo (CommandContext *context, Workbook *wb);

void command_list_release (GSList *cmds);

gboolean cmd_set_text (CommandContext *context,
		       Sheet *sheet, CellPos const * const pos,
		       char const * const new_text,
		       String const * const old_text);

gboolean cmd_insert_cols (CommandContext *context,
			  Sheet *sheet, int start_col, int count);
gboolean cmd_insert_rows (CommandContext *context,
			  Sheet *sheet, int start_row, int count);
gboolean cmd_delete_cols (CommandContext *context,
			  Sheet *sheet, int start_col, int count);
gboolean cmd_delete_rows (CommandContext *context,
			  Sheet *sheet, int start_row, int count);

#endif /* GNUMERIC_COMMAND_CONTEXT_H */
