/*
 * ms-formula-read.h: MS Excel -> Gnumeric formula conversion
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998, 1999, 2000 Michael Meeks
 */
#ifndef GNUMERIC_MS_FORMULA_R_H
#define GNUMERIC_MS_FORMULA_R_H

#include <glib.h>

#include "ms-excel-read.h"
#include "ms-biff.h"

GnmExpr const *
excel_parse_formula (MSContainer const *container,
		     ExcelSheet const *esheet,
		     int fn_col, int fn_row,
		     guint8 const *mem, guint16 length,
		     gboolean shared,
		     gboolean *array_element);

#endif
