#ifndef GNUMERIC_DIALOGS_H
#define GNUMERIC_DIALOGS_H

#include "gnumeric.h"
#include "cell.h"
#include "summary.h"

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
  DiscreteDistribution, UniformDistribution, NormalDistribution, 
  BernoulliDistribution, BinomialDistribution, PoissonDistribution,
  PatternedDistribution, NegativeBinomialDistribution, ExponentialDistribution
} random_distribution_t;

typedef struct {
        int start_col, start_row;
        int end_col, end_row;
} discrete_random_tool_t;

typedef struct {
        float_t lower_limit;
        float_t upper_limit;
} uniform_random_tool_t;

typedef struct {
        float_t mean;
        float_t stdev;
} normal_random_tool_t;

typedef struct {
        float_t p;
} bernoulli_random_tool_t;

typedef struct {
        float_t p;
        int     trials;
} binomial_random_tool_t;

typedef struct {
        float_t p;
        int     f;
} negbinom_random_tool_t;

typedef struct {
        float_t lambda;
} poisson_random_tool_t;

typedef struct {
        float_t b;
} exponential_random_tool_t;

typedef struct {
        float_t from, to;
        float_t step;
        int     repeat_number;
        int     repeat_sequence;
} patterned_random_tool_t;

typedef union {
        discrete_random_tool_t    discrete;
        uniform_random_tool_t     uniform;
        normal_random_tool_t      normal;
        bernoulli_random_tool_t   bernoulli;
        binomial_random_tool_t    binomial;
        negbinom_random_tool_t    negbinom;
        poisson_random_tool_t     poisson;
        exponential_random_tool_t exponential;
        patterned_random_tool_t   patterned;
} random_tool_t;

typedef enum {
        NewSheetOutput, NewWorkbookOutput, RangeOutput
} data_analysis_output_type_t;

typedef struct {
        data_analysis_output_type_t type;
        Sheet                       *sheet;
        int                         start_col, cols;
        int                         start_row, rows;
        gboolean                    labels_flag;
} data_analysis_output_t;


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
char   	*dialog_function_wizard (Workbook *wb, FunctionDefinition *fd);
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
gboolean dialog_get_number      (Workbook *wb,
				 const char *glade_file,
				 double *init_and_return_value);
Cell *set_cell (data_analysis_output_t *dao, int col, int row, char *text);
void prepare_output (Workbook *wb, data_analysis_output_t *dao, char *name);


FunctionDefinition *dialog_function_select (Workbook *wb);


#endif /* GNUMERIC_DIALOGS_H */
