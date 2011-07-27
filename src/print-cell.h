/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_PRINT_CELL_H_
# define _GNM_PRINT_CELL_H_

#include "gnumeric.h"
#include "print-info.h"
#include <cairo.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

void gnm_gtk_print_cell_range (cairo_t *context,
			       Sheet const *sheet, GnmRange *range,
			       double base_x, double base_y,
			       PrintInformation const *pinfo);

G_END_DECLS

#endif /* _GNM_PRINT_CELL_H_ */
