#ifndef GNUMERIC_TOOLS_H
#define GNUMERIC_TOOLS_H

#include "analysis-tools.h"

int correlation_tool      (WorkbookControl *context, Sheet *current_sheet,
			   GSList *input, group_by_t group_by,
			   data_analysis_output_t *dao);
int covariance_tool       (WorkbookControl *context, Sheet *current_sheet,
			   GSList *input, group_by_t group_by,
			   data_analysis_output_t *dao);
int descriptive_stat_tool (WorkbookControl *context, Sheet *current_sheet,
			   GSList *input, group_by_t group_by,
			   descriptive_stat_tool_t *ds,
			   data_analysis_output_t *dao);
int sampling_tool         (WorkbookControl *context, Sheet *sheet,
			   GSList *input, group_by_t group_by,
			   gboolean periodic_flag, guint size, guint number, 
			   data_analysis_output_t *dao);
int ftest_tool            (WorkbookControl *context, Sheet *sheet,
			   Range *input_range1, Range *input_range2,
			   gnum_float alpha,
			   data_analysis_output_t *dao);
int regression_tool       (WorkbookControl *context, Sheet *sheet,
			   Range *input_rangeys, Range *input_rangexs,
			   gnum_float alpha, data_analysis_output_t *dao,
			   int intercept, int xdim);
int ttest_paired_tool     (WorkbookControl *context, Sheet *sheet,
			   Range *input_range1, Range *input_range2,
			   gnum_float mean_diff, gnum_float alpha,
			   data_analysis_output_t *dao);
int ttest_eq_var_tool     (WorkbookControl *context, Sheet *sheet,
			   Range *input_range1, Range *input_range2,
			   gnum_float mean_diff, gnum_float alpha,
			   data_analysis_output_t *dao);
int ttest_neq_var_tool    (WorkbookControl *context, Sheet *sheet,
			   Range *input_range1, Range *input_range2,
			   gnum_float mean_diff, gnum_float alpha,
			   data_analysis_output_t *dao);
int ztest_tool            (WorkbookControl *context, Sheet *sheet,
			   Range *range_input1, Range *range_input2,
			   gnum_float mean_diff, gnum_float var1, gnum_float var2,
			   gnum_float alpha, data_analysis_output_t *dao);
int random_tool           (WorkbookControl *context, Sheet *sheet,
			   int vars, int count, random_distribution_t distribution,
			   random_tool_t *param, data_analysis_output_t *dao);
int average_tool          (WorkbookControl *context, Sheet *sheet,
			   Range *range, int interval, int std_error_flag,
			   data_analysis_output_t *dao);
int exp_smoothing_tool    (WorkbookControl *context, Sheet *sheet,
			   Range *range, gnum_float damp_fact,
			   int std_error_flag,
			   data_analysis_output_t *dao);
int ranking_tool          (WorkbookControl *context, Sheet *sheet,
			   Range *input_range, int columns_flag,
			   data_analysis_output_t *dao);

int anova_single_factor_tool        (WorkbookControl *context, Sheet *sheet,
				     Range *range, int columns_flag,
				     gnum_float alpha, data_analysis_output_t *dao);
int anova_two_factor_without_r_tool (WorkbookControl *context, Sheet *sheet,
				     Range *range, gnum_float alpha,
				     data_analysis_output_t *dao);
int anova_two_factor_with_r_tool (WorkbookControl *context, Sheet *sheet,
				  Range *range, int rows_per_sample,
				  gnum_float alpha, data_analysis_output_t *dao);
int histogram_tool (WorkbookControl *context, Sheet *sheet,
		    Range *range1, Range *range2,
		    gboolean labels, gboolean sorted,
		    gboolean percentage, gboolean chart,
		    data_analysis_output_t *dao);

#endif
