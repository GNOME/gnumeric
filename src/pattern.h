/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_PATTERN_H_
# define _GNM_PATTERN_H_

#include "style.h"
#include <goffice/cut-n-paste/foocanvas/foo-canvas.h>

G_BEGIN_DECLS

#define GNUMERIC_SHEET_PATTERNS 25

gboolean    gnumeric_background_set_gc	(GnmStyle const *style,
					 GdkGC *gc, FooCanvas *canvas,
					 gboolean const is_selected);
gboolean    gnumeric_background_set_gtk	(GnmStyle const *style,
					 cairo_t *context);

G_END_DECLS

#endif /* _GNM_PATTERN_H_ */
