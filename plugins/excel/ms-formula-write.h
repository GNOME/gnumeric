/*
 * ms-formula-read.h: MS Excel -> Gnumeric formula conversion
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1998-2001 Michael Meeks
 *     2002-2003 Jody Goldberg
 */
#ifndef GNUMERIC_MS_FORMULA_WRITE_H
#define GNUMERIC_MS_FORMULA_WRITE_H

#include <gnumeric.h>
#include "excel.h"

typedef enum {
	EXCEL_CALLED_FROM_CELL,
	EXCEL_CALLED_FROM_ARRAY,
	EXCEL_CALLED_FROM_SHARED,
	EXCEL_CALLED_FROM_CONDITION,
	EXCEL_CALLED_FROM_VALIDATION,
	EXCEL_CALLED_FROM_NAME
} ExcelFuncContext;

guint32 excel_write_formula    (ExcelWriteState *ewb, GnmExpr const *expr,
				Sheet *sheet, int fn_col, int fn_row,
				ExcelFuncContext context);

void excel_write_prep_expressions (ExcelWriteState *ewb);
void excel_write_prep_expr  	  (ExcelWriteState *ewb, GnmExpr const *expr);
void excel_write_prep_sheet	  (ExcelWriteState *ewb, Sheet const *sheet);

#endif /* GNUMERIC_MS_FORMULA_W_H */
