#ifndef GNUMERIC_PATTERN_H
#define GNUMERIC_PATTERN_H

#include "style.h"
#include <goffice/cut-n-paste/foocanvas/foo-canvas.h>

#ifdef WITH_GNOME_PRINT
#include <libgnomeprint/gnome-print.h>
#endif

#define GNUMERIC_SHEET_PATTERNS 25

gboolean    gnumeric_background_set_gc	(GnmStyle const *style,
					 GdkGC *gc, FooCanvas *canvas,
					 gboolean const is_selected);
#ifdef WITH_GNOME_PRINT
gboolean    gnumeric_background_set_pc	(GnmStyle const *style,
					 GnomePrintContext *context);
#endif

#endif /* GNUMERIC_PATTERN_H */
