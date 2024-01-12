#ifndef TOOLS_SIMULATION_H_
#define TOOLS_SIMULATION_H_

#include <tools/dao.h>

typedef enum {
	MedianErr = 1, ModeErr = 2, StddevErr = 4, VarErr = 8, SkewErr = 16,
	KurtosisErr = 32
} sim_errmask_t;

typedef struct {
	gnm_float *min;
	gnm_float *max;
	gnm_float *mean;
	gnm_float *median;
	gnm_float *mode;
	gnm_float *stddev;
	gnm_float *var;
	gnm_float *skew;
	gnm_float *kurtosis;
	gnm_float *range;
	gnm_float *confidence;
	gnm_float *lower;
	gnm_float *upper;
	int        *errmask;
} simstats_t;

typedef struct {
        int n_input_vars;
        int n_output_vars;
	int n_vars;

        int first_round;
        int last_round;
        int n_iterations;

	int max_time;

	GnmValue    *inputs;
	GnmValue    *outputs;
	GnmRangeRef *ref_inputs;
	GnmRangeRef *ref_outputs;
	GSList   *list_inputs;
	GSList   *list_outputs;
	gchar    **cellnames;

	gint64 start, end;

	simstats_t **stats;
} simulation_t;

gchar const *simulation_tool (WorkbookControl        *wbc,
			      data_analysis_output_t *dao,
			      simulation_t           *sim);
void   simulation_tool_destroy (simulation_t *sim);

#endif
