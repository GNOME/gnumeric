#ifndef GNUMERIC_ANALYSIS_TOOLS_H
#define GNUMERIC_ANALYSIS_TOOLS_H

#include "gnumeric.h"
#include "numbers.h"
#include "dao.h"
#include "tools.h"
#include "regression.h"
#include "complex.h"

/*******************************************************************/
/* Section 1: gui utility functions for the tools                  */

/*******************************************************************/
/* Section 2: not undoable tools                                   */

/*******************************************************************/
/* Section 3: Undoable tools and their data structures             */

typedef enum {
	analysis_tools_noerr = 0,
	analysis_tools_reported_err,
	analysis_tools_reported_err_input,
	analysis_tools_missing_data,
	analysis_tools_too_few_cols,
	analysis_tools_too_few_rows,
	analysis_tools_replication_invalid,
	analysis_tools_REG_invalid_dimensions
} analysis_tools_error_code_t;


/********************************************************************/
/* Subsection 3a: Undoable Tools using the first  common generic data struct */

typedef struct {
	analysis_tools_error_code_t err;
	WorkbookControl *wbc;
	GSList     *input;
	group_by_t group_by;
	gboolean   labels;
} analysis_tools_data_generic_t;

/**************** Correlation Tool ***************/

gboolean analysis_tool_correlation_engine  (data_analysis_output_t *dao, gpointer specs,
					    analysis_tool_engine_t selector, gpointer result);


/**************** Covariance Tool  ***************/

gboolean analysis_tool_covariance_engine  (data_analysis_output_t *dao, gpointer specs,
					    analysis_tool_engine_t selector, gpointer result);


/********************************************************************/
/* Subsection 3b: Undoable Tools using the first  common generic    */
/*                data struct augmented with some simple fields     */

/************** Single Factor ANOVA  *************/

typedef struct {
	analysis_tools_data_generic_t base;
	gnm_float alpha;
} analysis_tools_data_anova_single_t;

gboolean analysis_tool_anova_single_engine (data_analysis_output_t *dao, gpointer specs,
					   analysis_tool_engine_t selector, gpointer result);

/********** Descriptive Statistics Tool **********/

typedef struct {
	analysis_tools_data_generic_t base;
        gboolean summary_statistics;
        gboolean confidence_level;
        gboolean kth_largest;
        gboolean kth_smallest;
        gboolean use_ssmedian;
        int      k_smallest;
	int      k_largest;
        gnm_float  c_level;
} analysis_tools_data_descriptive_t;

gboolean analysis_tool_descriptive_engine (data_analysis_output_t *dao, gpointer specs,
					   analysis_tool_engine_t selector, gpointer result);


/************** Moving Averages **** *************/

typedef struct {
	analysis_tools_data_generic_t base;
	int interval;
	int std_error_flag;
} analysis_tools_data_moving_average_t;

gboolean analysis_tool_moving_average_engine (data_analysis_output_t *dao, gpointer specs,
					      analysis_tool_engine_t selector, gpointer result);


/************** Exponential Smoothing  *************/

typedef struct {
	analysis_tools_data_generic_t base;
	gnm_float damp_fact;
	int std_error_flag;
} analysis_tools_data_exponential_smoothing_t;

gboolean analysis_tool_exponential_smoothing_engine (data_analysis_output_t *dao, gpointer specs,
					   analysis_tool_engine_t selector, gpointer result);


/************** Fourier Analysis **** *************/

typedef struct {
	analysis_tools_data_generic_t base;
	gboolean inverse;
} analysis_tools_data_fourier_t;

gboolean analysis_tool_fourier_engine (data_analysis_output_t *dao, gpointer specs,
				       analysis_tool_engine_t selector, gpointer result);


/************** Sampling Tool **********************/

typedef struct {
	analysis_tools_data_generic_t base;
	gboolean periodic;
	guint size;
	guint number;
} analysis_tools_data_sampling_t;

gboolean analysis_tool_sampling_engine (data_analysis_output_t *dao, gpointer specs,
				       analysis_tool_engine_t selector, gpointer result);


/************** Ranking Tool *************************/

typedef struct {
	analysis_tools_data_generic_t base;
	gboolean av_ties;
} analysis_tools_data_ranking_t;

gboolean analysis_tool_ranking_engine (data_analysis_output_t *dao, gpointer specs,
				       analysis_tool_engine_t selector, gpointer result);


/****************  Regression  ********************/

typedef struct {
	analysis_tools_data_generic_t base;
	GnmValue      *y_input;
	gnm_float alpha;
	gint       intercept;

} analysis_tools_data_regression_t;

gboolean analysis_tool_regression_engine (data_analysis_output_t *dao, gpointer specs,
					   analysis_tool_engine_t selector, gpointer result);


/********************************************************************/
/* Subsection 3c: Undoable Tools using the second common generic    */
/*                data struct augmented with some simple fields     */

/*********************** FTest ************************/

typedef struct {
	analysis_tools_error_code_t err;
	WorkbookControl *wbc;
	GnmValue *range_1;
	GnmValue *range_2;
	gboolean   labels;
	gnm_float alpha;
} analysis_tools_data_ftest_t;

gboolean analysis_tool_ftest_engine (data_analysis_output_t *dao, gpointer specs,
				     analysis_tool_engine_t selector, gpointer result);

/*********************** TTest paired *****************/

typedef struct {
	analysis_tools_data_ftest_t base;
	gnm_float mean_diff;
	gnm_float var1;
	gnm_float var2;
} analysis_tools_data_ttests_t;

gboolean analysis_tool_ttest_paired_engine (data_analysis_output_t *dao, gpointer specs,
				     analysis_tool_engine_t selector, gpointer result);


/*********************** TTest equal varinaces *********/

gboolean analysis_tool_ttest_eqvar_engine (data_analysis_output_t *dao, gpointer specs,
				     analysis_tool_engine_t selector, gpointer result);


/*********************** TTest unequal varinaces *******/

gboolean analysis_tool_ttest_neqvar_engine (data_analysis_output_t *dao, gpointer specs,
					    analysis_tool_engine_t selector, gpointer result);


/*********************** ZTest ************************/

gboolean analysis_tool_ztest_engine (data_analysis_output_t *dao, gpointer specs,
				     analysis_tool_engine_t selector, gpointer result);


/********************************************************************/
/* Subsection 3d: Undoable Tools using their own data struct        */

/****************  2-Factor ANOVA  ***************/

typedef struct {
	analysis_tools_error_code_t err;
	WorkbookControl *wbc;
	GnmValue     *input;
	group_by_t group_by;
	gboolean   labels;
	GSList    *row_input_range;
	GSList    *col_input_range;
	gnm_float alpha;
	gint       replication;
	gint       rows;
	guint       n_c;
	guint       n_r;
} analysis_tools_data_anova_two_factor_t;

gboolean analysis_tool_anova_two_factor_engine (data_analysis_output_t *dao, gpointer specs,
					   analysis_tool_engine_t selector, gpointer result);

/*THINGS NEEDED FOR THE HISTOGRAM SPLIT-OUT
 *We will almost certainly move these around further later, but for
 *now, getting things to work (and particularly compile) is more
 *important.
 */
/*
 *  new_data_set_list:
 *  @ranges: GSList *           the data location
 *  @group_by: group_by_t       how to group the data
 *  @ignore_non_num: gboolean   whether simply to ignore non-numerical values
 *  @read_label: gboolean       whether the first entry contains a label
 */
GPtrArray *
new_data_set_list (GSList *ranges, group_by_t group_by,
		   gboolean ignore_non_num, gboolean read_labels, Sheet *sheet);

/*
 *  prepare_input_range:
 *  @input_range:
 *  @group_by:
 *
 */
void
prepare_input_range (GSList **input_range, group_by_t group_by);


/*************************************************************************/
/*
 *  data_set_t: a data set format (optionally) keeping track of missing
 *  observations.
 *
 */
typedef struct {
        GArray  *data;
	char *label;
	GSList *missing;
	gboolean complete;
	gboolean read_label;
} data_set_t;

/*
 *  destroy_data_set_list:
 *  @the_list:
 */
void
destroy_data_set_list (GPtrArray * the_list);

gnm_float *
range_sort (gnm_float const *xs, int n);

gboolean gnm_check_input_range_list_homogeneity (GSList *input_range);

void gnm_fourier_fft (complex_t const *in, int n, int skip,
					  complex_t **fourier, gboolean inverse);

#endif
