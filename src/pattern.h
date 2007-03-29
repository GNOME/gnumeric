#ifndef GNUMERIC_PATTERN_H
#define GNUMERIC_PATTERN_H

#include "style.h"
#include <goffice/cut-n-paste/foocanvas/foo-canvas.h>

#define GNUMERIC_SHEET_PATTERNS 25

gboolean    gnumeric_background_set_gc	(GnmStyle const *style,
					 GdkGC *gc, FooCanvas *canvas,
					 gboolean const is_selected);
gboolean    gnumeric_background_set_gtk	(GnmStyle const *style,
					 cairo_t *context);

#endif /* GNUMERIC_PATTERN_H */
