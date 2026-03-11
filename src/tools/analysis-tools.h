#ifndef GNM_ANALYSIS_TOOLS_H_
#define GNM_ANALYSIS_TOOLS_H_

#include <gnumeric.h>
#include <numbers.h>
#include <tools/dao.h>


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

typedef enum {
	GROUPED_BY_ROW = 0,
	GROUPED_BY_COL = 1,
	GROUPED_BY_AREA = 2,
	GROUPED_BY_BIN = 3
} group_by_t;

#define GNM_TYPE_ANALYSIS_TOOL (gnm_analysis_tool_get_type ())
G_DECLARE_DERIVABLE_TYPE (GnmAnalysisTool, gnm_analysis_tool, GNM, ANALYSIS_TOOL, GObject)

struct _GnmAnalysisToolClass {
	GObjectClass parent_class;

	gboolean (*update_dao) (GnmAnalysisTool *tool, data_analysis_output_t *dao);
	char *   (*update_descriptor) (GnmAnalysisTool *tool, data_analysis_output_t *dao);
	gboolean (*prepare_output_range) (GnmAnalysisTool *tool, data_analysis_output_t *dao);
	gboolean (*last_validity_check) (GnmAnalysisTool *tool, data_analysis_output_t *dao);
	gboolean (*format_output_range) (GnmAnalysisTool *tool, data_analysis_output_t *dao);
	gboolean (*perform_calc) (GnmAnalysisTool *tool, data_analysis_output_t *dao);
};

gboolean gnm_analysis_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao);
char *   gnm_analysis_tool_update_descriptor (GnmAnalysisTool *tool, data_analysis_output_t *dao);
gboolean gnm_analysis_tool_prepare_output_range (GnmAnalysisTool *tool, data_analysis_output_t *dao);
gboolean gnm_analysis_tool_last_validity_check (GnmAnalysisTool *tool, data_analysis_output_t *dao);
gboolean gnm_analysis_tool_format_output_range (GnmAnalysisTool *tool, data_analysis_output_t *dao);
gboolean gnm_analysis_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao);


/********************************************************************/
/* Section 2: Undoable Tools using the first  common generic data struct */

typedef struct {
	analysis_tools_error_code_t err;
	WorkbookControl *wbc;
	GSList     *input;
	group_by_t group_by;
	gboolean   labels;
} analysis_tools_data_generic_t;

#define GNM_TYPE_GENERIC_ANALYSIS_TOOL (gnm_generic_analysis_tool_get_type ())
GType gnm_generic_analysis_tool_get_type (void);
typedef struct _GnmGenericAnalysisTool GnmGenericAnalysisTool;
typedef struct _GnmGenericAnalysisToolClass GnmGenericAnalysisToolClass;

struct _GnmGenericAnalysisTool {
	GnmAnalysisTool parent;
	analysis_tools_data_generic_t base;
};

struct _GnmGenericAnalysisToolClass {
	GnmAnalysisToolClass parent_class;
};

#define GNM_GENERIC_ANALYSIS_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_GENERIC_ANALYSIS_TOOL, GnmGenericAnalysisTool))
#define GNM_IS_GENERIC_ANALYSIS_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_GENERIC_ANALYSIS_TOOL))

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GnmGenericAnalysisTool, g_object_unref)


/**************** Correlation Tool ***************/

#define GNM_TYPE_CORRELATION_TOOL (gnm_correlation_tool_get_type ())
G_DECLARE_FINAL_TYPE (GnmCorrelationTool, gnm_correlation_tool, GNM, CORRELATION_TOOL, GnmGenericAnalysisTool)

GnmAnalysisTool *gnm_correlation_tool_new (void);

/**************** Covariance Tool  ***************/

#define GNM_TYPE_COVARIANCE_TOOL (gnm_covariance_tool_get_type ())
G_DECLARE_FINAL_TYPE (GnmCovarianceTool, gnm_covariance_tool, GNM, COVARIANCE_TOOL, GnmGenericAnalysisTool)

GnmAnalysisTool *gnm_covariance_tool_new (void);


/********************************************************************/
/* Section 3: Undoable Tools using the first  common generic    */
/*                data struct augmented with some simple fields     */

/************** Single Factor ANOVA  *************/

#define GNM_TYPE_ANOVA_SINGLE_TOOL (gnm_anova_single_tool_get_type ())
GType gnm_anova_single_tool_get_type (void);
typedef struct _GnmAnovaSingleTool GnmAnovaSingleTool;
typedef struct _GnmAnovaSingleToolClass GnmAnovaSingleToolClass;

struct _GnmAnovaSingleTool {
	GnmGenericAnalysisTool parent;
	gnm_float alpha;
};

struct _GnmAnovaSingleToolClass {
	GnmGenericAnalysisToolClass parent_class;
};

#define GNM_ANOVA_SINGLE_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_ANOVA_SINGLE_TOOL, GnmAnovaSingleTool))
#define GNM_IS_ANOVA_SINGLE_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_ANOVA_SINGLE_TOOL))

GnmAnalysisTool *gnm_anova_single_tool_new (void);

/********** Descriptive Statistics Tool **********/

#define GNM_TYPE_DESCRIPTIVE_TOOL (gnm_descriptive_tool_get_type ())
GType gnm_descriptive_tool_get_type (void);
typedef struct _GnmDescriptiveTool GnmDescriptiveTool;
typedef struct _GnmDescriptiveToolClass GnmDescriptiveToolClass;

struct _GnmDescriptiveTool {
	GnmGenericAnalysisTool parent;
	gboolean summary_statistics;
	gboolean confidence_level;
	gboolean kth_largest;
	gboolean kth_smallest;
	gboolean use_ssmedian;
	int      k_smallest;
	int      k_largest;
	gnm_float  c_level;
};

struct _GnmDescriptiveToolClass {
	GnmGenericAnalysisToolClass parent_class;
};

#define GNM_DESCRIPTIVE_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_DESCRIPTIVE_TOOL, GnmDescriptiveTool))
#define GNM_IS_DESCRIPTIVE_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_DESCRIPTIVE_TOOL))

GnmAnalysisTool *gnm_descriptive_tool_new (void);


/************** Moving Averages **** *************/

typedef enum {
	moving_average_type_sma = 0,
	moving_average_type_cma,
	moving_average_type_wma,
	moving_average_type_spencer_ma,
	moving_average_type_central_sma
} moving_average_type_t;

#define GNM_TYPE_MOVING_AVERAGE_TOOL (gnm_moving_average_tool_get_type ())
GType gnm_moving_average_tool_get_type (void);
typedef struct _GnmMovingAverageTool GnmMovingAverageTool;
typedef struct _GnmMovingAverageToolClass GnmMovingAverageToolClass;

struct _GnmMovingAverageTool {
	GnmGenericAnalysisTool parent;
	int interval;
	int std_error_flag;
	int df;
	int offset;
	gboolean show_graph;
	moving_average_type_t ma_type;
};

struct _GnmMovingAverageToolClass {
	GnmGenericAnalysisToolClass parent_class;
};

#define GNM_MOVING_AVERAGE_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_MOVING_AVERAGE_TOOL, GnmMovingAverageTool))
#define GNM_IS_MOVING_AVERAGE_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_MOVING_AVERAGE_TOOL))

GnmAnalysisTool *gnm_moving_average_tool_new (void);


/************** Fourier Analysis **** *************/

#define GNM_TYPE_FOURIER_TOOL (gnm_fourier_tool_get_type ())
GType gnm_fourier_tool_get_type (void);
typedef struct _GnmFourierTool GnmFourierTool;
typedef struct _GnmFourierToolClass GnmFourierToolClass;

struct _GnmFourierTool {
	GnmGenericAnalysisTool parent;
	gboolean inverse;
};

struct _GnmFourierToolClass {
	GnmGenericAnalysisToolClass parent_class;
};

#define GNM_FOURIER_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_FOURIER_TOOL, GnmFourierTool))
#define GNM_IS_FOURIER_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_FOURIER_TOOL))

GnmAnalysisTool *gnm_fourier_tool_new (void);


/************** Sampling Tool **********************/

#define GNM_TYPE_SAMPLING_TOOL (gnm_sampling_tool_get_type ())
GType gnm_sampling_tool_get_type (void);
typedef struct _GnmSamplingTool GnmSamplingTool;
typedef struct _GnmSamplingToolClass GnmSamplingToolClass;

struct _GnmSamplingTool {
	GnmGenericAnalysisTool parent;
	gboolean periodic;
	gboolean row_major;
	guint offset;
	guint size;
	guint period;
	guint number;
};

struct _GnmSamplingToolClass {
	GnmGenericAnalysisToolClass parent_class;
};

#define GNM_SAMPLING_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_SAMPLING_TOOL, GnmSamplingTool))
#define GNM_IS_SAMPLING_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_SAMPLING_TOOL))

GnmAnalysisTool *gnm_sampling_tool_new (void);


/************** Ranking Tool *************************/

#define GNM_TYPE_RANKING_TOOL (gnm_ranking_tool_get_type ())
GType gnm_ranking_tool_get_type (void);
typedef struct _GnmRankingTool GnmRankingTool;
typedef struct _GnmRankingToolClass GnmRankingToolClass;

struct _GnmRankingTool {
	GnmGenericAnalysisTool parent;
	gboolean av_ties;
};

struct _GnmRankingToolClass {
	GnmGenericAnalysisToolClass parent_class;
};

#define GNM_RANKING_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_RANKING_TOOL, GnmRankingTool))
#define GNM_IS_RANKING_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_RANKING_TOOL))

GnmAnalysisTool *gnm_ranking_tool_new (void);


/********************************************************************/
/* Section 4: Undoable Tools using the second common generic    */
/*                data struct augmented with some simple fields     */

typedef struct {
	analysis_tools_error_code_t err;
	WorkbookControl *wbc;
	GnmValue *range_1;
	GnmValue *range_2;
	gboolean   labels;
	gnm_float alpha;
} analysis_tools_data_generic_b_t;

#define GNM_TYPE_GENERIC_B_ANALYSIS_TOOL (gnm_generic_b_analysis_tool_get_type ())
GType gnm_generic_b_analysis_tool_get_type (void);
typedef struct _GnmGenericBAnalysisTool GnmGenericBAnalysisTool;
typedef struct _GnmGenericBAnalysisToolClass GnmGenericBAnalysisToolClass;

struct _GnmGenericBAnalysisTool {
	GnmAnalysisTool parent;
	analysis_tools_data_generic_b_t base;
};

struct _GnmGenericBAnalysisToolClass {
	GnmAnalysisToolClass parent_class;
};

#define GNM_GENERIC_B_ANALYSIS_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_GENERIC_B_ANALYSIS_TOOL, GnmGenericBAnalysisTool))
#define GNM_IS_GENERIC_B_ANALYSIS_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_GENERIC_B_ANALYSIS_TOOL))

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GnmGenericBAnalysisTool, g_object_unref)

/*********************** FTest ************************/

#define GNM_TYPE_FTEST_TOOL (gnm_ftest_tool_get_type ())
GType gnm_ftest_tool_get_type (void);
typedef struct _GnmFTestTool GnmFTestTool;
typedef struct _GnmFTestToolClass GnmFTestToolClass;

struct _GnmFTestTool {
	GnmGenericBAnalysisTool parent;
};

struct _GnmFTestToolClass {
	GnmGenericBAnalysisToolClass parent_class;
};

#define GNM_FTEST_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_FTEST_TOOL, GnmFTestTool))
#define GNM_IS_FTEST_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_FTEST_TOOL))

GnmAnalysisTool *gnm_ftest_tool_new (void);

/****************  Regression  ********************/

#define GNM_TYPE_REGRESSION_TOOL (gnm_regression_tool_get_type ())
GType gnm_regression_tool_get_type (void);
typedef struct _GnmRegressionTool GnmRegressionTool;
typedef struct _GnmRegressionToolClass GnmRegressionToolClass;

struct _GnmRegressionTool {
	GnmGenericBAnalysisTool parent;
	group_by_t group_by;
	gboolean   intercept;
	gboolean   multiple_regression;
        gboolean   multiple_y;
        gboolean   residual;
	GSList    *indep_vars;
};

struct _GnmRegressionToolClass {
	GnmGenericBAnalysisToolClass parent_class;
};

#define GNM_REGRESSION_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_REGRESSION_TOOL, GnmRegressionTool))
#define GNM_IS_REGRESSION_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_REGRESSION_TOOL))

GnmAnalysisTool *gnm_regression_tool_new (void);

/*********************** TTest paired *****************/

#define GNM_TYPE_TTEST_PAIRED_TOOL (gnm_ttest_paired_tool_get_type ())
GType gnm_ttest_paired_tool_get_type (void);
typedef struct _GnmTTestPairedTool GnmTTestPairedTool;
typedef struct _GnmTTestPairedToolClass GnmTTestPairedToolClass;

struct _GnmTTestPairedTool {
	GnmGenericBAnalysisTool parent;
	gnm_float mean_diff;
	gnm_float var1;
	gnm_float var2;
};

struct _GnmTTestPairedToolClass {
	GnmGenericBAnalysisToolClass parent_class;
};

#define GNM_TTEST_PAIRED_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_TTEST_PAIRED_TOOL, GnmTTestPairedTool))
#define GNM_IS_TTEST_PAIRED_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_TTEST_PAIRED_TOOL))

GnmAnalysisTool *gnm_ttest_paired_tool_new (void);


/*********************** TTest equal variances *********/

#define GNM_TYPE_TTEST_EQVAR_TOOL (gnm_ttest_eqvar_tool_get_type ())
GType gnm_ttest_eqvar_tool_get_type (void);
typedef struct _GnmTTestEqVarTool GnmTTestEqVarTool;
typedef struct _GnmTTestEqVarToolClass GnmTTestEqVarToolClass;

struct _GnmTTestEqVarTool {
	GnmGenericBAnalysisTool parent;
	gnm_float mean_diff;
};

struct _GnmTTestEqVarToolClass {
	GnmGenericBAnalysisToolClass parent_class;
};

#define GNM_TTEST_EQVAR_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_TTEST_EQVAR_TOOL, GnmTTestEqVarTool))
#define GNM_IS_TTEST_EQVAR_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_TTEST_EQVAR_TOOL))

GnmAnalysisTool *gnm_ttest_eqvar_tool_new (void);


/*********************** TTest unequal variances *******/

#define GNM_TYPE_TTEST_NEQVAR_TOOL (gnm_ttest_neqvar_tool_get_type ())
GType gnm_ttest_neqvar_tool_get_type (void);
typedef struct _GnmTTestNeqVarTool GnmTTestNeqVarTool;
typedef struct _GnmTTestNeqVarToolClass GnmTTestNeqVarToolClass;

struct _GnmTTestNeqVarTool {
	GnmGenericBAnalysisTool parent;
	gnm_float mean_diff;
};

struct _GnmTTestNeqVarToolClass {
	GnmGenericBAnalysisToolClass parent_class;
};

#define GNM_TTEST_NEQVAR_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_TTEST_NEQVAR_TOOL, GnmTTestNeqVarTool))
#define GNM_IS_TTEST_NEQVAR_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_TTEST_NEQVAR_TOOL))

GnmAnalysisTool *gnm_ttest_neqvar_tool_new (void);


/*********************** ZTest ************************/

#define GNM_TYPE_ZTEST_TOOL (gnm_ztest_tool_get_type ())
GType gnm_ztest_tool_get_type (void);
typedef struct _GnmZTestTool GnmZTestTool;
typedef struct _GnmZTestToolClass GnmZTestToolClass;

struct _GnmZTestTool {
	GnmGenericBAnalysisTool parent;
	gnm_float mean_diff;
	gnm_float var1;
	gnm_float var2;
};

struct _GnmZTestToolClass {
	GnmGenericBAnalysisToolClass parent_class;
};

#define GNM_ZTEST_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_ZTEST_TOOL, GnmZTestTool))
#define GNM_IS_ZTEST_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_ZTEST_TOOL))

GnmAnalysisTool *gnm_ztest_tool_new (void);


/****************  Advanced Filter  ********************/

#define GNM_TYPE_ADVANCED_FILTER_TOOL (gnm_advanced_filter_tool_get_type ())
GType gnm_advanced_filter_tool_get_type (void);
typedef struct _GnmAdvancedFilterTool GnmAdvancedFilterTool;
typedef struct _GnmAdvancedFilterToolClass GnmAdvancedFilterToolClass;

struct _GnmAdvancedFilterTool {
	GnmGenericBAnalysisTool parent;
	gboolean   unique_only_flag;
};

struct _GnmAdvancedFilterToolClass {
	GnmGenericBAnalysisToolClass parent_class;
};

#define GNM_ADVANCED_FILTER_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_ADVANCED_FILTER_TOOL, GnmAdvancedFilterTool))
#define GNM_IS_ADVANCED_FILTER_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_ADVANCED_FILTER_TOOL))

GnmAnalysisTool *gnm_advanced_filter_tool_new (void);


/********************************************************************/
/* Section 5     Functions also needed elsewhere.                  */


gboolean analysis_tool_check_input_homogeneity (GnmGenericAnalysisTool *gtool);

int analysis_tool_calc_length (GnmGenericAnalysisTool *gtool);

void analysis_tools_write_label       (GnmGenericAnalysisTool *gtool,
				       GnmValue *val, /* deprecated */
				       data_analysis_output_t *dao,
				       int x, int y, int i);
void analysis_tools_write_variable_label (GnmValue *val, /* deprecated */
					  data_analysis_output_t *dao,
					  int x, int y,
					  gboolean labels, int i);

gboolean analysis_tool_table (GnmGenericAnalysisTool *gtool,
			      data_analysis_output_t *dao,
			      gchar const *title, gchar const *functionname,
			      gboolean full_table);

void analysis_tool_prepare_input_range (GnmGenericAnalysisTool *gtool);

const GnmExpr *make_cellref (int dx, int dy);
const GnmExpr *make_rangeref (int dx0, int dy0, int dx1, int dy1);

void set_cell_text_row (data_analysis_output_t *dao,
			int col, int row, const char *text);
void set_cell_text_col (data_analysis_output_t *dao,
			int col, int row, const char *text);

#endif
