#ifndef GNUMERIC_ANALYSIS_TOOLS_H
#define GNUMERIC_ANALYSIS_TOOLS_H

#include "gnumeric.h"
#include "numbers.h"
#include <widgets/gnumeric-expr-entry.h>
#include <glade/glade.h>
#include "dao.h"

typedef enum {
	TOOL_ENGINE_UPDATE_DAO = 0,
	TOOL_ENGINE_UPDATE_DESCRIPTOR,
	TOOL_ENGINE_PREPARE_OUTPUT_RANGE,
	TOOL_ENGINE_LAST_VALIDITY_CHECK,
	TOOL_ENGINE_FORMAT_OUTPUT_RANGE,
	TOOL_ENGINE_PERFORM_CALC,
	TOOL_ENGINE_CLEAN_UP
} analysis_tool_engine_t;

typedef gboolean (* analysis_tool_engine) (data_analysis_output_t *dao, gpointer specs, 
					   analysis_tool_engine_t selector, gpointer result);


/* the following enum and char *[] must stay synchronized! */
typedef enum {
	GROUPED_BY_ROW = 0,
	GROUPED_BY_COL = 1,
	GROUPED_BY_AREA = 2,
	GROUPED_BY_BIN = 3
} group_by_t;

typedef struct {
	gboolean max_given;
	gboolean min_given;
	gnum_float max;
	gnum_float min;
	gint n;
} histogram_calc_bin_info_t;

#endif
