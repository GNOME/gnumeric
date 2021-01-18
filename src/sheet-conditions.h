#ifndef GNM_SHEET_CONDITIONS_H_
#define GNM_SHEET_CONDITIONS_H_

#include <gnumeric.h>

G_BEGIN_DECLS

void sheet_conditions_init (Sheet *sheet);
void sheet_conditions_uninit (Sheet *sheet);
// resize?

GnmStyleConditions *sheet_conditions_share_conditions_add (GnmStyleConditions *conds);
void sheet_conditions_share_conditions_remove (GnmStyleConditions *conds);

void sheet_conditions_add (Sheet *sheet, GnmRange const *r, GnmStyle *style);
void sheet_conditions_remove (Sheet *sheet, GnmRange const *r, GnmStyle *style);

void sheet_conditions_simplify (Sheet *sheet);
void sheet_conditions_dump (Sheet *sheet);

void sheet_conditions_link_unlink_dependents (Sheet *sheet,
					      GnmRange const *r,
					      gboolean qlink);

G_END_DECLS

#endif
