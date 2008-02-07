/*
 * ms-formula-read.h: MS Excel -> Gnumeric formula conversion
 *
 * Authors:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2005 Jody Goldberg
 */
#ifndef GNM_MS_FORMULA_R_H
#define GNM_MS_FORMULA_R_H

#include <glib.h>

#include "ms-excel-read.h"
#include "ms-biff.h"

GnmExprTop const *
excel_parse_formula (MSContainer const *container,
		     ExcelReadSheet const *esheet,
		     int fn_col, int fn_row,
		     guint8 const *mem, guint16 length, guint16 array_length,
		     gboolean shared,
		     gboolean *array_element);

#endif /* GNM_MS_FORMULA_R_H */
