#ifndef GNUMERIC_DIALOGS_H
#define GNUMERIC_DIALOGS_H

#include "gnumeric.h"
#include "cell.h"
#include "summary.h"

#ifdef ENABLE_BONOBO
void     dialog_graph_guru	(Workbook *wb);
#endif
void     dialog_formula_guru	(Workbook *wb);
void     dialog_plugin_manager  (Workbook *wb);
void   	 dialog_goto_cell       (Workbook *wb);
void   	 dialog_cell_format     (Workbook *wb, Sheet *sheet);
int    	 dialog_paste_special   (Workbook *wb);
void   	 dialog_insert_cells    (Workbook *wb, Sheet *sheet);
void   	 dialog_delete_cells    (Workbook *wb, Sheet *sheet);
void   	 dialog_zoom            (Workbook *wb, Sheet *sheet);
char   	*dialog_query_load_file (Workbook *wb);
void   	 dialog_about           (Workbook *wb);
void   	 dialog_define_names    (Workbook *wb);
void   	 dialog_cell_comment    (Workbook *wb, Cell *cell);
void   	 dialog_cell_sort       (Workbook *wb, Sheet *sheet);
void   	 dialog_workbook_attr   (Workbook *wb);
void   	 dialog_goal_seek       (Workbook *wb, Sheet *sheet);
void   	 dialog_solver          (Workbook *wb, Sheet *sheet);
void   	 dialog_printer_setup   (Workbook *wb, Sheet *sheet);
void   	 dialog_summary_update  (Workbook *wb, SummaryInfo *sin);
void     dialog_autocorrect     (Workbook *wb);
void   	 dialog_advanced_filter (Workbook *wb);
void   	 dialog_autosave        (Workbook *wb);
gboolean dialog_autosave_prompt (Workbook *wb);
void     dialog_data_analysis   (Workbook *wb, Sheet *sheet);
char   	*dialog_get_sheet_name  (Workbook *wb, const char *name);
void     dialog_sheet_order     (Workbook *wb);
gboolean dialog_get_number      (Workbook *wb,
				 const char *glade_file,
				 double *init_and_return_value);

FunctionDefinition *dialog_function_select (Workbook *wb);

#endif /* GNUMERIC_DIALOGS_H */
