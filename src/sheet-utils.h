#ifndef _GNM_SHEET_UTILS_H
# define _GNM_SHEET_UTILS_H

#include <gnumeric.h>

G_BEGIN_DECLS

void gnm_sheet_guess_region (Sheet *sheet, GnmRange *region);
void gnm_sheet_guess_data_range (Sheet *sheet, GnmRange *region);

G_END_DECLS

#endif /* _GNM_SHEET_UTILS_H */
