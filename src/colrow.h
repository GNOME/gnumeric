#ifndef GNUMERIC_COLROW_H
#define GNUMERIC_COLROW_H

#include <glib.h>
#include "gnumeric.h"

typedef GSList *ColRowVisList;

ColRowVisList col_row_get_visiblity_toggle (Sheet *sheet, gboolean const is_col,
					    gboolean const visible);
ColRowVisList col_row_vis_list_destroy     (ColRowVisList list);
void          col_row_set_visiblity        (Sheet *sheet, gboolean const is_col,
					    gboolean const visible,
					    ColRowVisList list);

#endif /* GNUMERIC_COLROW_H */
