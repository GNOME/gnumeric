/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#ifndef __SIMULATION_H__
#define __SIMULATION_H__

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
} simulation_t;

gchar *
simulation_tool (WorkbookControl        *wbc,
		 data_analysis_output_t *dao,
		 simulation_t           *sim);

#endif

