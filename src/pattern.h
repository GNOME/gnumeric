#ifndef GNUMERIC_PATTERN_H
#define GNUMERIC_PATTERN_H

#include "style.h"
#include <libgnomecanvas/gnome-canvas.h>
#include <libgnomeprint/gnome-print.h>

#define GNUMERIC_SHEET_PATTERNS 25

GdkPixmap * gnumeric_pattern_get_stipple(gint index);

gboolean    gnumeric_background_set_gc	(MStyle const *style,
					 GdkGC *gc, GnomeCanvas *canvas,
					 gboolean const is_selected);
gboolean    gnumeric_background_set_pc	(MStyle const *style,
					 GnomePrintContext *context);

#endif /* GNUMERIC_PATTERN_H */
