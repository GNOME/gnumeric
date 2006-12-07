/*
** analysis-histogram.h
** 
** Made by Solarion
** Login   <gnumeric-hacker@digitasaru.net>
** 
** Started on  Mon Dec  4 20:36:28 2006 Johnny Q. Hacker
** Last update Wed Dec  6 20:11:39 2006 Johnny Q. Hacker
*/

#ifndef ANALYSIS_HISTOGRAM_H
#define ANALYSIS_HISTOGRAM_H

typedef struct {
	analysis_tools_error_code_t err;
	WorkbookControl *wbc;
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
	gnm_float max;
	gnm_float min;
	gint       n;

} analysis_tools_data_histogram_t;

gboolean analysis_tool_histogram_engine (data_analysis_output_t *dao, gpointer specs,
					   analysis_tool_engine_t selector, gpointer result);

#endif 	    /* !ANALYSIS_HISTOGRAM_H */
