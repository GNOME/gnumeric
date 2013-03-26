/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_PATTERN_H_
# define _GNM_PATTERN_H_

#include "style.h"

G_BEGIN_DECLS

#define GNUMERIC_SHEET_PATTERNS 25

gboolean    gnumeric_background_set	(GnmStyle const *mstyle,
					 cairo_t *cr,
					 gboolean const is_selected,
					 GtkStyleContext *ctxt);

G_END_DECLS

#endif /* _GNM_PATTERN_H_ */
