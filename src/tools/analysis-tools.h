#ifndef GNUMERIC_ANALYSIS_TOOLS_H
#define GNUMERIC_ANALYSIS_TOOLS_H

#include "gnumeric.h"
#include "numbers.h"
#include <widgets/gnumeric-expr-entry.h>
#include <glade/glade.h>


/* the following enum and char *[] must stay synchronized! */
typedef enum {
	GROUPED_BY_ROW = 0,
	GROUPED_BY_COL = 1,
	GROUPED_BY_AREA = 2,
	GROUPED_BY_BIN = 3
} group_by_t;


#define ANALYSISTOOLS_OUTPUT_GROUP static const char *output_group[] = {\
	"newsheet-button",\
	"newworkbook-button",\
	"outputrange-button",\
	"inplace-button",          /* only in advanced filter  */\
	0\
};

typedef struct {
        gboolean summary_statistics;
        gboolean confidence_level;
        gboolean kth_largest;
        gboolean kth_smallest;
        int      k_smallest, k_largest;
        gnum_float  c_level;
} descriptive_stat_tool_t;

typedef enum {
  DiscreteDistribution, UniformDistribution, NormalDistribution,
  BernoulliDistribution, BinomialDistribution, PoissonDistribution,
  PatternedDistribution, NegativeBinomialDistribution, ExponentialDistribution
} random_distribution_t;

typedef struct {
	Value *range;
} discrete_random_tool_t;

typedef struct {
        gnum_float lower_limit;
        gnum_float upper_limit;
} uniform_random_tool_t;

typedef struct {
        gnum_float mean;
        gnum_float stdev;
} normal_random_tool_t;

typedef struct {
        gnum_float p;
} bernoulli_random_tool_t;

typedef struct {
        gnum_float p;
        int     trials;
} binomial_random_tool_t;

typedef struct {
        gnum_float p;
        int     f;
} negbinom_random_tool_t;

typedef struct {
        gnum_float lambda;
} poisson_random_tool_t;

typedef struct {
        gnum_float b;
} exponential_random_tool_t;

typedef struct {
        gnum_float from, to;
        gnum_float step;
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
        NewSheetOutput, NewWorkbookOutput, RangeOutput, InPlaceOutput
} data_analysis_output_type_t;

typedef struct {
        data_analysis_output_type_t type;
        Sheet                       *sheet;
        int                         start_col, cols;
        int                         start_row, rows;
        gboolean                    labels_flag;
	gboolean                    autofit_flag;
} data_analysis_output_t;

typedef struct {
	gboolean max_given;
	gboolean min_given;
	gnum_float max;
	gnum_float min;
	gint n;
} histogram_calc_bin_info_t;

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


void autofit_column  (data_analysis_output_t *dao, int col);
void autofit_columns (data_analysis_output_t *dao, int from, int to);

void set_cell        (data_analysis_output_t *dao, int col, int row, const char *text);
void set_cell_printf (data_analysis_output_t *dao,
		      int col, int row, const char *fmt, ...)
		      G_GNUC_PRINTF (4, 5);
void set_cell_value  (data_analysis_output_t *dao, int col, int row, Value *v);
void set_cell_float  (data_analysis_output_t *dao,
		      int col, int row, gnum_float v);
void set_cell_int    (data_analysis_output_t *dao,
		      int col, int row, int v);
void set_cell_na     (data_analysis_output_t *dao,
		      int col, int row);

void prepare_output  (WorkbookControl *wbc,
		      data_analysis_output_t *dao, const char *name);
void tool_load_selection (GenericToolState *state, gboolean allow_multiple);
gboolean tool_destroy (GtkObject *w, GenericToolState  *state);
void dialog_tool_init_outputs (GenericToolState *state, GCallback sensitivity_cb);
void dialog_tool_init_buttons (GenericToolState *state, GCallback ok_function);
void error_in_entry (GenericToolState *state, GtkWidget *entry, const char *err_str);
int parse_output (GenericToolState *state, data_analysis_output_t *dao);

#endif
