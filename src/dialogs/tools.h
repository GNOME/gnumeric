#ifndef GNUMERIC_TOOLS_H
#define GNUMERIC_TOOLS_H

#include "analysis-tools.h"

int correlation_tool       (Workbook *wb, Sheet *current_sheet, 
			    Range *input_range, int columns_flag,
			    data_analysis_output_t *dao);
int covariance_tool       (Workbook *wb, Sheet *current_sheet, 
			   Range *input_range, int columns_flag,
			   data_analysis_output_t *dao);
int descriptive_stat_tool (Workbook *wb, Sheet *current_sheet, 
			   Range *input_range, int columns_flag,
			   descriptive_stat_tool_t *ds,
			   data_analysis_output_t *dao);
int sampling_tool         (Workbook *wb, Sheet *sheet, Range *input_range,
			   gboolean periodic_flag, int size,
			   data_analysis_output_t *dao);
int ftest_tool            (Workbook *wb, Sheet *sheet, Range *input_range1, 
			   Range *input_range2, float_t alpha,
			   data_analysis_output_t *dao);
int regression_tool       (Workbook *wb, Sheet *sheet, Range *input_rangeys, 
			   Range *input_rangexs, float_t alpha,
			   data_analysis_output_t *dao, int intercept,
			   int xdim);
int ttest_paired_tool     (Workbook *wb, Sheet *sheet, Range *input_range1, 
			   Range *input_range2, float_t mean_diff, float_t alpha,
			   data_analysis_output_t *dao);
int ttest_eq_var_tool     (Workbook *wb, Sheet *sheet, Range *input_range1, 
			   Range *input_range2, float_t mean_diff, float_t alpha,
			   data_analysis_output_t *dao);
int ttest_neq_var_tool    (Workbook *wb, Sheet *sheet, Range *input_range1, 
			   Range *input_range2, float_t mean_diff, float_t alpha,
			   data_analysis_output_t *dao);
int ztest_tool           (Workbook *wb, Sheet *sheet, Range *range_input1, 
			  Range *range_input2, float_t mean_diff, 
			  float_t var1, float_t var2, float_t alpha,
			  data_analysis_output_t *dao);
int random_tool          (Workbook *wb, Sheet *sheet, int vars, int count,
	                  random_distribution_t distribution,
	                  random_tool_t *param, data_analysis_output_t *dao);
int average_tool         (Workbook *wb, Sheet *sheet, Range *range, int interval, 
			  int std_error_flag, data_analysis_output_t *dao);
int ranking_tool         (Workbook *wb, Sheet *sheet, Range *input_range,
			  int columns_flag, data_analysis_output_t *dao);

int anova_single_factor_tool        (Workbook *wb, Sheet *sheet, Range *range,
				     int columns_flag, float_t alpha, 
				     data_analysis_output_t *dao);
int anova_two_factor_without_r_tool (Workbook *wb, Sheet *sheet, Range *range,
				     float_t alpha, 
				     data_analysis_output_t *dao);
int anova_two_factor_with_r_tool (Workbook *wb, Sheet *sheet, Range *range,
				  int rows_per_sample, float_t alpha, 
				  data_analysis_output_t *dao);
int histogram_tool (Workbook *wb, Sheet *sheet, Range *range1, Range *range2,
		    gboolean labels, gboolean sorted, gboolean percentage,
		    gboolean chart,
		    data_analysis_output_t *dao);

#endif
