#ifndef GNUMERIC_ANALYSIS_TOOLS_H
#define GNUMERIC_ANALYSIS_TOOLS_H

#include "gnumeric.h"
#include "numbers.h"
#include "dao.h"
#include "tools.h"
#include "regression.h"

/*******************************************************************/
/* Section 1: gui utility functions for the tools                  */

/*******************************************************************/
/* Section 2: not undoable tools                                   */

RegressionResult
regression_tool           (WorkbookControl *context, Sheet *sheet,
			   GSList *x_input, Value *y_input,
			   group_by_t group_by,
			   gnum_float alpha, data_analysis_output_t *dao,
			   int intercept);
/*******************************************************************/
/* Section 3: Undoable tools and their data structures             */

typedef enum {
	analysis_tools_noerr = 0,
	analysis_tools_missing_data,
	analysis_tools_too_few_cols,
	analysis_tools_too_few_rows,
	analysis_tools_replication_invalid
} analysis_tools_error_code_t;


#define ANALYSIS_TOOLS_DATA_GENERIC 	analysis_tools_error_code_t err;\
	                                WorkbookControlGUI *wbcg;       \
	                                GSList     *input;              \
	                                group_by_t group_by;            \
                                        gboolean   labels	

/********************************************************************/
/* Subsection 3a: Undoable Tools using the first  common generic data struct */

typedef struct {
	ANALYSIS_TOOLS_DATA_GENERIC;
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
	ANALYSIS_TOOLS_DATA_GENERIC;
	gnum_float alpha;
} analysis_tools_data_anova_single_t;

gboolean analysis_tool_anova_single_engine (data_analysis_output_t *dao, gpointer specs, 
					   analysis_tool_engine_t selector, gpointer result);

/********** Descriptive Statistics Tool **********/

typedef struct {
	ANALYSIS_TOOLS_DATA_GENERIC;
        gboolean summary_statistics;
        gboolean confidence_level;
        gboolean kth_largest;
        gboolean kth_smallest;
        int      k_smallest;
	int      k_largest;
        gnum_float  c_level;
} analysis_tools_data_descriptive_t;

gboolean analysis_tool_descriptive_engine (data_analysis_output_t *dao, gpointer specs, 
					   analysis_tool_engine_t selector, gpointer result);


/************** Moving Averages **** *************/

typedef struct {
	ANALYSIS_TOOLS_DATA_GENERIC;
	int interval;
	int std_error_flag;
} analysis_tools_data_moving_average_t;

gboolean analysis_tool_moving_average_engine (data_analysis_output_t *dao, gpointer specs, 
					      analysis_tool_engine_t selector, gpointer result);


/************** Exponential Smoothing  *************/

typedef struct {
	ANALYSIS_TOOLS_DATA_GENERIC;
	gnum_float damp_fact;
	int std_error_flag;
} analysis_tools_data_exponential_smoothing_t;

gboolean analysis_tool_exponential_smoothing_engine (data_analysis_output_t *dao, gpointer specs, 
					   analysis_tool_engine_t selector, gpointer result);


/************** Fourier Analysis **** *************/

typedef struct {
	ANALYSIS_TOOLS_DATA_GENERIC;
	gboolean inverse;
} analysis_tools_data_fourier_t;

gboolean analysis_tool_fourier_engine (data_analysis_output_t *dao, gpointer specs, 
				       analysis_tool_engine_t selector, gpointer result);


/************** Sampling Tool **********************/

typedef struct {
	ANALYSIS_TOOLS_DATA_GENERIC;
	gboolean periodic;
	guint size;
	guint number;
} analysis_tools_data_sampling_t;

gboolean analysis_tool_sampling_engine (data_analysis_output_t *dao, gpointer specs, 
				       analysis_tool_engine_t selector, gpointer result);


/************** Ranking Tool *************************/

typedef struct {
	ANALYSIS_TOOLS_DATA_GENERIC;
	gboolean av_ties;
} analysis_tools_data_ranking_t;

gboolean analysis_tool_ranking_engine (data_analysis_output_t *dao, gpointer specs, 
				       analysis_tool_engine_t selector, gpointer result);


/********************************************************************/
/* Subsection 3c: Undoable Tools using the second common generic    */
/*                data struct augmented with some simple fields     */

#define ANALYSIS_TOOLS_DATA_GENERIC_TWO_VARS	analysis_tools_error_code_t err;\
	                                WorkbookControlGUI *wbcg;               \
                                        Value *range_1;                         \
                                        Value *range_2;                         \
                                        gboolean   labels	

/*********************** FTest ************************/

typedef struct {
	ANALYSIS_TOOLS_DATA_GENERIC_TWO_VARS;
	gnum_float alpha;
} analysis_tools_data_ftest_t;

gboolean analysis_tool_ftest_engine (data_analysis_output_t *dao, gpointer specs, 
				     analysis_tool_engine_t selector, gpointer result);

/*********************** TTest paired *****************/

typedef struct {
	ANALYSIS_TOOLS_DATA_GENERIC_TWO_VARS;
	gnum_float alpha;
	gnum_float mean_diff;
	gnum_float var1;
	gnum_float var2;
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
	WorkbookControlGUI *wbcg;
	Value     *input;
	group_by_t group_by;
	gboolean   labels;
	GSList    *row_input_range;
	GSList    *col_input_range;
	gnum_float alpha;
	gint       replication;
	gint       rows;
	guint       n_c;
	guint       n_r;
} analysis_tools_data_anova_two_factor_t;

gboolean analysis_tool_anova_two_factor_engine (data_analysis_output_t *dao, gpointer specs, 
					   analysis_tool_engine_t selector, gpointer result);

/****************  Histogram  ********************/

typedef struct {
	analysis_tools_error_code_t err;
	WorkbookControlGUI *wbcg;
	GSList     *input;
	GSList     *bin;
	group_by_t group_by;
	gboolean   labels;
	gboolean   bin_labels;
	gboolean   pareto;
	gboolean   percentage;
	gboolean   cumulative;
	gboolean   chart;
	gboolean   max_given;
	gboolean   min_given;
	gnum_float max;
	gnum_float min;
	gint       n;
	
} analysis_tools_data_histogram_t;

gboolean analysis_tool_histogram_engine (data_analysis_output_t *dao, gpointer specs, 
					   analysis_tool_engine_t selector, gpointer result);

#endif
