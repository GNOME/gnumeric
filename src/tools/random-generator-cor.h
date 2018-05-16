#ifndef GNUMERIC_RANDOM_GENERATOR_COR_H
#define GNUMERIC_RANDOM_GENERATOR_COR_H

#include <gnumeric.h>
#include <numbers.h>
#include <tools/dao.h>
#include <tools/tools.h>

typedef enum {
	random_gen_cor_type_cov = 0,
	random_gen_cor_type_cholesky,
} random_gen_cor_type_t;

typedef struct {
	WorkbookControl *wbc;
	GnmValue        *matrix;
	random_gen_cor_type_t matrix_type;
	gint count;
	gint variables;
} tools_data_random_cor_t;

gboolean tool_random_cor_engine (GOCmdContext *gcc, data_analysis_output_t *dao,
				 gpointer specs,
				 analysis_tool_engine_t selector,
				 gpointer result);

#endif
