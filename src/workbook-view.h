#ifndef GNUMERIC_WORKBOOK_VIEW_H
#define GNUMERIC_WORKBOOK_VIEW_H

#include "sheet.h"

/* Actions on the workbooks UI */

/* enable/disable paste/paste_special
 * 0 = both disabled
 * 1 = paste enabled
 * 2 = both enabled
 */
void workbook_view_set_paste_state (Workbook *wb, int const state);

#endif /* GNUMERIC_WORKBOOK_VIEW_H */
