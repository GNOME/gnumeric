/*
 * ms-formula-read.h: MS Excel -> Gnumeric formula conversion
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1998-2001 Michael Meeks
 *          2002 Jody Goldberg
 */
#ifndef GNUMERIC_MS_FORMULA_W_H
#define GNUMERIC_MS_FORMULA_W_H

#include <gnumeric.h>
#include "ms-excel-write.h"
#include "ms-biff.h"
#include "formula-types.h"

guint32 excel_write_formula    (ExcelWriteState *ewb, GnmExpr const *expr,
				Sheet *sheet, int fn_col, int fn_row,
				gboolean shared);

void excel_write_prep_expressions (ExcelWriteState *ewb);
void excel_write_prep_expr  	  (ExcelWriteState *ewb, GnmExpr const *expr);
void excel_write_prep_sheet	  (ExcelWriteState *ewb, Sheet const *sheet);

#endif /* GNUMERIC_MS_FORMULA_W_H */
