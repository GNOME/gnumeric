#ifndef GNUMERIC_CONSOLIDATE_H
#define GNUMERIC_CONSOLIDATE_H

#include "gnumeric.h"

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
} ConsolidateMode;

typedef struct _Consolidate {
	FunctionDefinition *fd;

	GlobalRange *dst;
	GSList      *src;

	ConsolidateMode mode;
} Consolidate;

Consolidate *consolidate_new  (void);
void         consolidate_free (Consolidate *cs);

void         consolidate_set_function    (Consolidate *cs, FunctionDefinition *fd);
void         consolidate_set_mode        (Consolidate *cs, ConsolidateMode mode);

gboolean     consolidate_set_destination (Consolidate *cs, Value *range);
gboolean     consolidate_add_source      (Consolidate *cs, Value *range);

Range        consolidate_get_dest_bounding_box (Consolidate *cs);
void         consolidate_apply                 (Consolidate *cs,
						WorkbookControl *wbc);

#endif
