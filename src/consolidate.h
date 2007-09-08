#ifndef GNUMERIC_CONSOLIDATE_H
#define GNUMERIC_CONSOLIDATE_H

#include "gnumeric.h"
#include <tools/dao.h>
#include <tools/tools.h>

typedef enum {
	/*
	 * These can be both set, both unset or
	 * one of them can be set. Indicates
	 * what sort of consolidation we will
	 * execute
	 */
	CONSOLIDATE_ROW_LABELS   = 1 << 0,
	CONSOLIDATE_COL_LABELS   = 1 << 1,

	/*
	 * If set the row and/or column labels
	 * will be copied to the destination area
	 */
	CONSOLIDATE_COPY_LABELS  = 1 << 2,

	/* If this is set we put the outcome
	 * of our formulas into the destination
	 * otherwise we put formulas
	 */
	CONSOLIDATE_PUT_VALUES   = 1 << 3
} GnmConsolidateMode;

struct _GnmConsolidate {
	GnmFunc *fd;

	GSList      *src;

	GnmConsolidateMode mode;
};

GnmConsolidate *consolidate_new  (void);
void         consolidate_free (GnmConsolidate *cs, gboolean content_only);

void         consolidate_set_function    (GnmConsolidate *cs, GnmFunc *fd);
void         consolidate_set_mode        (GnmConsolidate *cs,
					  GnmConsolidateMode mode);

gboolean     consolidate_add_source      (GnmConsolidate *cs, GnmValue *range);
gboolean     consolidate_check_destination (GnmConsolidate *cs,
					    data_analysis_output_t *dao);

gboolean tool_consolidate_engine (data_analysis_output_t *dao, gpointer specs,
			     analysis_tool_engine_t selector, gpointer result);

#endif
