#ifndef GNUMERIC_PATTERN_H
#define GNUMERIC_PATTERN_H

#include "style.h"
#include <gnome.h>
#include <libgnomeprint/gnome-print.h>

#define GNUMERIC_SHEET_PATTERNS 18

GdkPixmap * gnumeric_pattern_get_stipple(gint const index);

gboolean    gnumeric_background_set_gc	(MStyle *style, GdkGC *gc,
					 GnomeCanvas *canvas,
					 gboolean const is_selected);
gboolean    gnumeric_background_set_pc	(MStyle *style, GnomePrintContext *context);

#endif /* GNUMERIC_PATTERN_H */
