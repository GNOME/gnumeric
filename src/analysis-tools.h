#ifndef GNUMERIC_ANALYSIS_TOOLS_H
#define GNUMERIC_ANALYSIS_TOOLS_H

#include "numbers.h"
#include "gnumeric.h"

typedef struct {
        gboolean summary_statistics;
        gboolean confidence_level;
        gboolean kth_largest;
        gboolean kth_smallest;
        int      k_smallest, k_largest;
        gnum_float  c_level;
        GtkWidget *entry[5];
} descriptive_stat_tool_t;

typedef enum {
  DiscreteDistribution, UniformDistribution, NormalDistribution,
  BernoulliDistribution, BinomialDistribution, PoissonDistribution,
  PatternedDistribution, NegativeBinomialDistribution, ExponentialDistribution
} random_distribution_t;

typedef struct {
        int start_col, start_row;
        int end_col, end_row;
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
        NewSheetOutput, NewWorkbookOutput, RangeOutput
} data_analysis_output_type_t;

typedef struct {
        data_analysis_output_type_t type;
        Sheet                       *sheet;
        int                         start_col, cols;
        int                         start_row, rows;
        gboolean                    labels_flag;
} data_analysis_output_t;


void autofit_column  (data_analysis_output_t *dao, int col);
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

#endif
