#ifndef GNUMERIC_PATTERN_H
#define GNUMERIC_PATTERN_H

#include "style.h"
#include <gnome.h>

#define GNUMERIC_SHEET_PATTERNS 18

GdkPixmap * gnumeric_pattern_get_stipple(gint const index);

gboolean    gnumeric_background_set_gc	(MStyle *style, GdkGC *gc,
					 GnomeCanvas *canvas);

#endif /* GNUMERIC_PATTERN_H */
