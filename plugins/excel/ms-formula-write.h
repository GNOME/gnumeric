/*
 * ms-formula-read.h: MS Excel -> Gnumeric formula conversion
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998, 1999, 2000 Michael Meeks
 */
#ifndef GNUMERIC_MS_FORMULA_W_H
#define GNUMERIC_MS_FORMULA_W_H

#include <glib.h>

#include "ms-excel-write.h"
#include "ms-biff.h"
#include "formula-types.h"

guint32 excel_write_formula    (ExcelWriteState *ewb, GnmExpr const *expr,
				Sheet *sheet, int fn_col, int fn_row, int paren_level);

void excel_write_prep_expressions (ExcelWriteState *ewb);

#endif /* GNUMERIC_MS_FORMULA_W_H */
