#ifndef GNUMERIC_STF_UTIL_H
#define GNUMERIC_STF_UTIL_H

#include <stdlib.h>
#include <errno.h>
#include <gnome.h>
#include "stf.h"

#define WARN_COLS_MSG _("STF: Input data has more then the maximum number of columns %d")
#define WARN_ROWS_MSG _("STF: Input data has more then the maximum number of rows %d")

gboolean stf_is_line_terminator (const char *character);

void stf_set_scroll_region_and_prevent_center (GnomeCanvas *canvas, GnomeCanvasRect *rectangle, double width, double height);

Range stf_source_get_extent (FileSource_t *src);

#endif /* GNUMERIC_STF_UTIL_H */
