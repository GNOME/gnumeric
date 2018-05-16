#ifndef GNM_SHEET_DIFF_H
#define GNM_SHEET_DIFF_H

#include <gnumeric.h>

G_BEGIN_DECLS

typedef struct {
	// Start comparison of two workbooks.
	gboolean (*diff_start) (gpointer user);

	// Finish comparison started with above.
	void (*diff_end) (gpointer user);

	// Clean up allocations
	// This is not actually called by code here
	void (*dtor) (gpointer user);

	/* ------------------------------ */

	// Start looking at a sheet.  Either sheet might be NULL.
	void (*sheet_start) (gpointer user,
			     Sheet const *os, Sheet const *ns);

	// Finish sheet started with above.
	void (*sheet_end) (gpointer user);

	// The order of sheets has changed.
	void (*sheet_order_changed) (gpointer user);

	// An integer attribute of the sheet has changed.
	void (*sheet_attr_int_changed) (gpointer user, const char *name,
					int o, int n);

	/* ------------------------------ */

	// Column or Row information changed
	void (*colrow_changed) (gpointer user,
				ColRowInfo const *oc, ColRowInfo const *nc,
				gboolean is_cols, int i);

	/* ------------------------------ */

	// A cell was changed/added/removed.
	void (*cell_changed) (gpointer user,
			      GnmCell const *oc, GnmCell const *nc);

	/* ------------------------------ */

	// The style of an area was changed.
	void (*style_changed) (gpointer user, GnmRange const *r,
			       GnmStyle const *os, GnmStyle const *ns);

	/* ------------------------------ */

	// A defined name was changed
	void (*name_changed) (gpointer user,
			      GnmNamedExpr const *on, GnmNamedExpr const *nn);
} GnmDiffActions;


gboolean gnm_diff_sheets (const GnmDiffActions *actions, gpointer user,
			  Sheet *old_sheet, Sheet *new_sheet);

int gnm_diff_workbooks (const GnmDiffActions *actions, gpointer user,
			Workbook *old_wb, Workbook *new_wb);

G_END_DECLS

#endif /* GNM_SHEET_DIFF_H */
