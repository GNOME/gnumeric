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
  DiscreteDistribution, UniformDistribution, NormalDistribution, 
  BernoulliDistribution, BinomialDistribution, PoissonDistribution,
  PatternedDistribution
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
        float_t lambda;
} poisson_random_tool_t;

typedef struct {
        float_t from, to;
        float_t step;
        int     repeat_number;
        int     repeat_sequence;
} patterned_random_tool_t;

typedef union {
        discrete_random_tool_t  discrete;
        uniform_random_tool_t   uniform;
        normal_random_tool_t    normal;
        bernoulli_random_tool_t bernoulli;
        binomial_random_tool_t  binomial;
        poisson_random_tool_t   poisson;
        patterned_random_tool_t patterned;
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

int correlation_tool (Workbook *wb, Sheet *current_sheet, 
		      Range *input_range, int columns_flag,
		      data_analysis_output_t *dao);
int covariance_tool (Workbook *wb, Sheet *current_sheet, 
		     Range *input_range, int columns_flag,
		     data_analysis_output_t *dao);
int descriptive_stat_tool (Workbook *wb, Sheet *current_sheet, 
			   Range *input_range, int columns_flag,
			   descriptive_stat_tool_t *ds,
			   data_analysis_output_t *dao);
int sampling_tool (Workbook *wb, Sheet *sheet, Range *input_range,
		   gboolean periodic_flag, int size,
		   data_analysis_output_t *dao);
int ftest_tool (Workbook *wb, Sheet *sheet, Range *input_range1, 
		Range *input_range2, float_t alpha,
		data_analysis_output_t *dao);
int regression_tool (Workbook *wb, Sheet *sheet, Range *input_range1, 
		     Range *input_range2, float_t alpha,
		     data_analysis_output_t *dao);
int ttest_paired_tool (Workbook *wb, Sheet *sheet, Range *input_range1, 
		       Range *input_range2, float_t mean_diff, float_t alpha,
		       data_analysis_output_t *dao);
int ttest_eq_var_tool (Workbook *wb, Sheet *sheet, Range *input_range1, 
		       Range *input_range2, float_t mean_diff, float_t alpha,
		       data_analysis_output_t *dao);
int ttest_neq_var_tool (Workbook *wb, Sheet *sheet, Range *input_range1, 
			Range *input_range2, float_t mean_diff, float_t alpha,
			data_analysis_output_t *dao);
int ztest_tool (Workbook *wb, Sheet *sheet, Range *range_input1, 
		Range *range_input2, float_t mean_diff, 
		float_t var1, float_t var2, float_t alpha,
		data_analysis_output_t *dao);
int random_tool (Workbook *wb, Sheet *sheet, int vars, int count,
		 random_distribution_t distribution,
		 random_tool_t *param, data_analysis_output_t *dao);
int average_tool (Workbook *wb, Sheet *sheet, Range *range, int interval, 
		  int std_error_flag, data_analysis_output_t *dao);
int ranking_tool (Workbook *wb, Sheet *sheet, Range *input_range,
		  int columns_flag, data_analysis_output_t *dao);
int anova_single_factor_tool (Workbook *wb, Sheet *sheet, Range *range,
			      int columns_flag, float_t alpha, 
			      data_analysis_output_t *dao);
int anova_two_factor_without_r_tool (Workbook *wb, Sheet *sheet, Range *range,
				     float_t alpha, 
				     data_analysis_output_t *dao);

GList *add_strings_to_glist (const char *strs[]);

#endif /* GNUMERIC_DIALOGS_H */
