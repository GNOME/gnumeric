#ifndef GNUMERIC_PATTERN_H
#define GNUMERIC_PATTERN_H

#include "style.h"
#include <libfoocanvas/foo-canvas.h>
#include <libgnomeprint/gnome-print.h>

#define GNUMERIC_SHEET_PATTERNS 25

gboolean    gnumeric_background_set_gc	(GnmStyle const *style,
					 GdkGC *gc, FooCanvas *canvas,
					 gboolean const is_selected);
gboolean    gnumeric_background_set_pc	(GnmStyle const *style,
					 GnomePrintContext *context);

#endif /* GNUMERIC_PATTERN_H */
