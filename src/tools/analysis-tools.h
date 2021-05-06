#ifndef GNUMERIC_ANALYSIS_TOOLS_H
#define GNUMERIC_ANALYSIS_TOOLS_H

#include <gnumeric.h>
#include <numbers.h>
#include <tools/dao.h>
#include <tools/tools.h>
#include <regression.h>
#include <func.h>


/*******************************************************************/
/* Section 1: Undoable tools and their data structures             */

typedef enum {
	analysis_tools_noerr = 0,
	analysis_tools_reported_err,
	analysis_tools_reported_err_input,
	analysis_tools_missing_data,
	analysis_tools_too_few_cols,
	analysis_tools_too_few_rows,
	analysis_tools_replication_invalid,
	analysis_tools_no_records_found,
	analysis_tools_invalid_field
} analysis_tools_error_code_t;


/********************************************************************/
/* Section 2: Undoable Tools using the first  common generic data struct */

typedef struct {
	analysis_tools_error_code_t err;
	WorkbookControl *wbc;
	GSList     *input;
	group_by_t group_by;
	gboolean   labels;
} analysis_tools_data_generic_t;

/**************** Correlation Tool ***************/

gboolean analysis_tool_correlation_engine (GOCmdContext *gcc,
					   data_analysis_output_t *dao,
					   gpointer specs,
					   analysis_tool_engine_t selector,
					   gpointer result);


/**************** Covariance Tool  ***************/

gboolean analysis_tool_covariance_engine (GOCmdContext *gcc,
					  data_analysis_output_t *dao,
					  gpointer specs,
					  analysis_tool_engine_t selector,
					  gpointer result);


/********************************************************************/
/* Section 3: Undoable Tools using the first  common generic    */
/*                data struct augmented with some simple fields     */

/************** Single Factor ANOVA  *************/

typedef struct {
	analysis_tools_data_generic_t base;
	gnm_float alpha;
} analysis_tools_data_anova_single_t;

gboolean analysis_tool_anova_single_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
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

gboolean analysis_tool_descriptive_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
					   analysis_tool_engine_t selector, gpointer result);


/************** Moving Averages **** *************/

typedef enum {
	moving_average_type_sma = 0,
	moving_average_type_cma,
	moving_average_type_wma,
	moving_average_type_spencer_ma,
	moving_average_type_central_sma
} moving_average_type_t;

typedef struct {
	analysis_tools_data_generic_t base;
	int interval;
	int std_error_flag;
	int df;
	int offset;
	gboolean show_graph;
	moving_average_type_t ma_type;
} analysis_tools_data_moving_average_t;

gboolean analysis_tool_moving_average_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
					      analysis_tool_engine_t selector, gpointer result);


/************** Fourier Analysis **** *************/

typedef struct {
	analysis_tools_data_generic_t base;
	gboolean inverse;
} analysis_tools_data_fourier_t;

gboolean analysis_tool_fourier_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
				       analysis_tool_engine_t selector, gpointer result);


/************** Sampling Tool **********************/

typedef struct {
	analysis_tools_data_generic_t base;
	gboolean periodic;
	gboolean row_major;
	guint offset;
	guint size;
	guint period;
	guint number;
} analysis_tools_data_sampling_t;

gboolean analysis_tool_sampling_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
				       analysis_tool_engine_t selector, gpointer result);


/************** Ranking Tool *************************/

typedef struct {
	analysis_tools_data_generic_t base;
	gboolean av_ties;
} analysis_tools_data_ranking_t;

gboolean analysis_tool_ranking_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
				       analysis_tool_engine_t selector, gpointer result);




/********************************************************************/
/* Section 4: Undoable Tools using the second common generic    */
/*                data struct augmented with some simple fields     */

/*********************** FTest ************************/

typedef struct {
	analysis_tools_error_code_t err;
	WorkbookControl *wbc;
	GnmValue *range_1;
	GnmValue *range_2;
	gboolean   labels;
	gnm_float alpha;
} analysis_tools_data_generic_b_t;

gboolean analysis_tool_ftest_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
				     analysis_tool_engine_t selector, gpointer result);

/****************  Regression  ********************/

typedef struct {
	analysis_tools_data_generic_b_t base;
	group_by_t group_by;
	gboolean   intercept;
	gboolean   multiple_regression;
        gboolean   multiple_y;
        gboolean   residual;
	GSList    *indep_vars;
} analysis_tools_data_regression_t;

gboolean analysis_tool_regression_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
					   analysis_tool_engine_t selector, gpointer result);
/*********************** TTest paired *****************/

typedef struct {
	analysis_tools_data_generic_b_t base;
	gnm_float mean_diff;
	gnm_float var1;
	gnm_float var2;
} analysis_tools_data_ttests_t;

gboolean analysis_tool_ttest_paired_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
				     analysis_tool_engine_t selector, gpointer result);


/*********************** TTest equal variances *********/

gboolean analysis_tool_ttest_eqvar_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
				     analysis_tool_engine_t selector, gpointer result);


/*********************** TTest unequal variances *******/

gboolean analysis_tool_ttest_neqvar_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
					    analysis_tool_engine_t selector, gpointer result);


/*********************** ZTest ************************/

gboolean analysis_tool_ztest_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
				     analysis_tool_engine_t selector, gpointer result);

/****************  Advanced Filter  ********************/

typedef struct {
	analysis_tools_data_generic_b_t base;
	gboolean   unique_only_flag;
} analysis_tools_data_advanced_filter_t;

gboolean analysis_tool_advanced_filter_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
					   analysis_tool_engine_t selector, gpointer result);




/********************************************************************/
/* Section 5     Functions also needed elsewhere.                  */

gboolean analysis_tool_generic_clean (gpointer specs);
gboolean analysis_tool_generic_b_clean (gpointer specs);

int analysis_tool_calc_length (analysis_tools_data_generic_t *info);

void analysis_tools_write_label       (GnmValue *val, /* depreceated */
				       data_analysis_output_t *dao,
				       analysis_tools_data_generic_t *info,
				       int x, int y, int i);
void analysis_tools_write_label_ftest (GnmValue *val, /* depreceated */
				       data_analysis_output_t *dao,
				       int x, int y,
				       gboolean labels, int i);

gboolean analysis_tool_table (data_analysis_output_t *dao,
			      analysis_tools_data_generic_t *info,
			      gchar const *title, gchar const *functionname,
			      gboolean full_table);

void prepare_input_range (GSList **input_range, group_by_t group_by);

const GnmExpr *make_cellref (int dx, int dy);
const GnmExpr *make_rangeref (int dx0, int dy0, int dx1, int dy1);

void set_cell_text_row (data_analysis_output_t *dao,
			int col, int row, const char *text);
void set_cell_text_col (data_analysis_output_t *dao,
			int col, int row, const char *text);

GnmFunc *analysis_tool_get_function (char const *name,
				     data_analysis_output_t *dao);

#endif
