#ifndef GNM_SHEET_DIFF_H
#define GNM_SHEET_DIFF_H

#include "gnumeric.h"

G_BEGIN_DECLS

typedef struct GnmDiffState_ GnmDiffState;

typedef struct {
	// Start comparison of two workbooks.
	gboolean (*diff_start) (GnmDiffState *state);

	// Finish comparison started with above.
	void (*diff_end) (GnmDiffState *state);

	// Clean up allocations
	// This is not actually called by code here
	void (*dtor) (GnmDiffState *state);

	/* ------------------------------ */

	// Start looking at a sheet.  Either sheet might be NULL.
	void (*sheet_start) (GnmDiffState *state,
			     Sheet const *os, Sheet const *ns);

	// Finish sheet started with above.
	void (*sheet_end) (GnmDiffState *state);

	// The order of sheets has changed.
	void (*sheet_order_changed) (GnmDiffState *state);

	// An integer attribute of the sheet has changed.
	void (*sheet_attr_int_changed) (GnmDiffState *state, const char *name,
					int o, int n);

	/* ------------------------------ */

	// Column or Row information changed
	void (*colrow_changed) (GnmDiffState *state,
				ColRowInfo const *oc, ColRowInfo const *nc,
				gboolean is_cols, int i);

	/* ------------------------------ */

	// A cell was changed/added/removed.
	void (*cell_changed) (GnmDiffState *state,
			      GnmCell const *oc, GnmCell const *nc);

	/* ------------------------------ */

	// The style of an area was changed.
	void (*style_changed) (GnmDiffState *state, GnmRange const *r,
			       GnmStyle const *os, GnmStyle const *ns);

	/* ------------------------------ */

	// A defined name was changed
	void (*name_changed) (GnmDiffState *state,
			      GnmNamedExpr const *on, GnmNamedExpr const *nn);
} GnmDiffActions;


gboolean gnm_diff_sheets (const GnmDiffActions *actions, GnmDiffState *state,
			  Sheet *old_sheet, Sheet *new_sheet);

int gnm_diff_workbooks (const GnmDiffActions *actions, GnmDiffState *state,
			Workbook *old_wb, Workbook *new_wb);

G_END_DECLS

#endif /* GNM_SHEET_DIFF_H */
