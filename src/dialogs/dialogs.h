#ifndef GNUMERIC_DIALOGS_H
#define GNUMERIC_DIALOGS_H

#include "sheet.h"
#include "cell.h"

typedef struct {
        gboolean summary_statistics;
        gboolean confidence_level;
        gboolean kth_largest;
        gboolean kth_smallest;
        int      k_smallest, k_largest;
        float_t  c_level;
        GtkWidget *entry[5];
} descriptive_stat_tool_t;

typedef enum {
        NewSheetOutput, NewWorkbookOutput, RangeOutput
} data_analysis_output_type_t;

typedef struct {
        data_analysis_output_type_t type;
        Sheet                       *sheet;
        int                         start_col, cols;
        int                         start_row, rows;
} data_analysis_output_t;

void  dialog_goto_cell       (Workbook *wb);
void  dialog_cell_format     (Workbook *wb, Sheet *sheet);
int   dialog_paste_special   (Workbook *wb);
void  dialog_insert_cells    (Workbook *wb, Sheet *sheet);
void  dialog_delete_cells    (Workbook *wb, Sheet *sheet);
void  dialog_zoom            (Workbook *wb, Sheet *sheet);
char *dialog_query_load_file (Workbook *wb);
void  dialog_about           (Workbook *wb);
void  dialog_define_names    (Workbook *wb);
void  dialog_cell_comment    (Workbook *wb, Cell *cell);
void  dialog_cell_sort       (Workbook *wb, Sheet *sheet);
char *dialog_function_wizard (Workbook *wb, FunctionDefinition *fd);
void  dialog_goal_seek       (Workbook *wb, Sheet *sheet);
void  dialog_solver          (Workbook *wb, Sheet *sheet);
void  dialog_printer_setup   (Workbook *wb);

FunctionDefinition *dialog_function_select (Workbook *wb);

void  dialog_data_analysis   (Workbook *wb, Sheet *sheet);

void correlation_tool (Workbook *wb, Sheet *current_sheet, 
		       Range *input_range, int columns_flag,
		       data_analysis_output_t *dao);
void covariance_tool (Workbook *wb, Sheet *current_sheet, 
		      Range *input_range, int columns_flag,
		      data_analysis_output_t *dao);
void
descriptive_stat_tool (Workbook *wb, Sheet *current_sheet, 
                       Range *input_range, int columns_flag,
		       descriptive_stat_tool_t *ds,
		       data_analysis_output_t *dao);

#endif /* GNUMERIC_DIALOGS_H */
