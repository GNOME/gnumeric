#ifndef GNUMERIC_TOOLS_H
#define GNUMERIC_TOOLS_H

#include "analysis-tools.h"
#include "regression.h"

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
			   Value *input_range1, Value *input_range2,
			   gnum_float alpha,
			   data_analysis_output_t *dao);
RegressionResult
regression_tool           (WorkbookControl *context, Sheet *sheet,
			   GSList *x_input, Value *y_input,
			   group_by_t group_by,
			   gnum_float alpha, data_analysis_output_t *dao,
			   int intercept);
int ttest_paired_tool     (WorkbookControl *context, Sheet *sheet,
			   Value *input_range1, Value *input_range2,
			   gnum_float mean_diff, gnum_float alpha,
			   data_analysis_output_t *dao);
int ttest_eq_var_tool     (WorkbookControl *context, Sheet *sheet,
			   Value *input_range1, Value *input_range2,
			   gnum_float mean_diff, gnum_float alpha,
			   data_analysis_output_t *dao);
int ttest_neq_var_tool    (WorkbookControl *context, Sheet *sheet,
			   Value *input_range1, Value *input_range2,
			   gnum_float mean_diff, gnum_float alpha,
			   data_analysis_output_t *dao);
int ztest_tool            (WorkbookControl *context, Sheet *sheet,
			   Value *range_input1, Value *range_input2,
			   gnum_float mean_diff, gnum_float var1, gnum_float var2,
			   gnum_float alpha, data_analysis_output_t *dao);
int random_tool           (WorkbookControl *context, Sheet *sheet,
			   int vars, int count, random_distribution_t distribution,
			   random_tool_t *param, data_analysis_output_t *dao);
int average_tool          (WorkbookControl *context, Sheet *sheet,
			   GSList *input, group_by_t group_by,
			   int interval, int std_error_flag,
			   data_analysis_output_t *dao);
int exp_smoothing_tool    (WorkbookControl *context, Sheet *sheet,
			   GSList *input, group_by_t group_by,
			   gnum_float damp_fact, int std_error_flag,
			   data_analysis_output_t *dao);
int fourier_tool          (WorkbookControl *context, Sheet *sheet,
			   GSList *input, group_by_t group_by,
			   int inverse_flag,
			   data_analysis_output_t *dao);
int ranking_tool          (WorkbookControl *context, Sheet *sheet,
			   GSList *input, group_by_t group_by,
			   gboolean av_ties_flag,
			   data_analysis_output_t *dao);
int anova_single_factor_tool (WorkbookControl *context, Sheet *sheet,
			      GSList *input, group_by_t group_by,
			      gnum_float alpha, data_analysis_output_t *dao);
int anova_two_factor_without_r_tool (WorkbookControl *context, Sheet *sheet,
				     Value *input, gnum_float alpha,
				     data_analysis_output_t *dao);
int anova_two_factor_with_r_tool (WorkbookControl *context, Sheet *sheet,
				  Value *input, int rows_per_sample,
				  gnum_float alpha, data_analysis_output_t *dao);
int histogram_tool        (WorkbookControl *context, Sheet *sheet,
			   GSList *input, Value *bin, group_by_t group_by,
			   gboolean bin_labels, gboolean pareto, gboolean percentage,
			   gboolean cumulative, gboolean chart,
			   histogram_calc_bin_info_t *bin_info,
			   data_analysis_output_t *dao);

#endif
