/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#ifndef __SIMULATION_H__
#define __SIMULATION_H__

typedef enum {
	MedianErr = 1, ModeErr = 2, StddevErr = 4, VarErr = 8, SkewErr = 16,
	KurtosisErr = 32
} sim_errmask_t;

typedef struct {
	gnum_float *min;
	gnum_float *max;
	gnum_float *mean;
	gnum_float *median;
	gnum_float *mode;
	gnum_float *stddev;
	gnum_float *var;
	gnum_float *skew;
	gnum_float *kurtosis;
	gnum_float *range;
	gnum_float *confidence;
	gnum_float *lower;
	gnum_float *upper;
	int        *errmask;
} simstats_t;

typedef struct {
        int n_input_vars;
        int n_output_vars;
	int n_vars;

        int first_round;
        int last_round;
        int n_iterations;

	Value    *inputs;
	Value    *outputs;
	RangeRef *ref_inputs;
	RangeRef *ref_outputs;
	GSList   *list_inputs;
	GSList   *list_outputs;
	gchar    **cellnames;

	GTimeVal          start, end;

	simstats_t **stats;
} simulation_t;

gchar *simulation_tool (WorkbookControl        *wbc,
			data_analysis_output_t *dao,
			simulation_t           *sim);
void   simulation_tool_destroy (simulation_t *sim);

#endif

