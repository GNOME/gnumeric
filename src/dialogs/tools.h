/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef GNUMERIC_TOOLS_H
#define GNUMERIC_TOOLS_H

#include "analysis-tools.h"
#include "regression.h"

/* Section 1: gui utility functions for the tools */

typedef enum {
	TOOL_CORRELATION = 1,       /* use GenericToolState */
	TOOL_COVARIANCE = 2,        /* use GenericToolState */
	TOOL_RANK_PERCENTILE = 3,   /* use GenericToolState */
	TOOL_HISTOGRAM = 5,   /* use GenericToolState */
	TOOL_FOURIER = 6,   /* use GenericToolState */
	TOOL_GENERIC = 10,          /* all smaller types are generic */
	TOOL_DESC_STATS = 11,
	TOOL_TTEST = 12,
	TOOL_SAMPLING = 13,
	TOOL_AVERAGE = 14,
	TOOL_REGRESSION = 15,
	TOOL_ANOVA_SINGLE = 16,
	TOOL_ANOVA_TWO_FACTOR = 17,
	TOOL_FTEST = 18,
	TOOL_RANDOM = 19,
	TOOL_EXP_SMOOTHING = 20,
	TOOL_ADVANCED_FILTER = 21
} ToolType;

#define GENERIC_TOOL_STATE     ToolType  const type;\
	GladeXML  *gui;\
	GtkWidget *dialog;\
	GnumericExprEntry *input_entry;\
	GnumericExprEntry *input_entry_2;\
	GnumericExprEntry *output_entry;\
        GtkWidget *clear_outputrange_button;\
        GtkWidget *retain_format_button;\
        GtkWidget *retain_comments_button;\
	GtkWidget *ok_button;\
	GtkWidget *cancel_button;\
	GtkWidget *apply_button;\
	GtkWidget *help_button;\
	const char *help_link;\
	char *input_var1_str;\
	char *input_var2_str;\
	GtkWidget *new_sheet;\
	GtkWidget *new_workbook;\
	GtkWidget *output_range;\
	Sheet	  *sheet;\
	Workbook  *wb;\
	WorkbookControlGUI  *wbcg;\
	GtkAccelGroup *accel;\
	GtkWidget *warning_dialog;\
	GtkWidget *warning;

typedef struct {
	GENERIC_TOOL_STATE
} GenericToolState;

void tool_load_selection (GenericToolState *state, gboolean allow_multiple);
gboolean tool_destroy (GtkObject *w, GenericToolState  *state);
void dialog_tool_init_buttons (GenericToolState *state, GCallback ok_function);
void error_in_entry (GenericToolState *state, GtkWidget *entry, const char *err_str);

/* Section 2: not undoable tools */

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
			   gboolean inverse_flag,
			   data_analysis_output_t *dao);
int ranking_tool          (WorkbookControl *context, Sheet *sheet,
			   GSList *input, group_by_t group_by,
			   gboolean av_ties_flag,
			   data_analysis_output_t *dao);
int histogram_tool        (WorkbookControl *context, Sheet *sheet,
			   GSList *input, Value *bin, group_by_t group_by,
			   gboolean bin_labels, gboolean pareto, gboolean percentage,
			   gboolean cumulative, gboolean chart,
			   histogram_calc_bin_info_t *bin_info,
			   data_analysis_output_t *dao);

/* Section 3: Undoable tools and their data structures */

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



typedef struct {
	ANALYSIS_TOOLS_DATA_GENERIC;
} analysis_tools_data_generic_t;

/**************** Correlation Tool ***************/

gboolean analysis_tool_correlation_engine  (data_analysis_output_t *dao, gpointer specs, 
					    analysis_tool_engine_t selector, gpointer result);


/************** Single Factor ANOVA  *************/

typedef struct {
	ANALYSIS_TOOLS_DATA_GENERIC;
	gnum_float alpha;
} analysis_tools_data_anova_single_t;

gboolean analysis_tool_anova_single_engine (data_analysis_output_t *dao, gpointer specs, 
					   analysis_tool_engine_t selector, gpointer result);

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

#endif
