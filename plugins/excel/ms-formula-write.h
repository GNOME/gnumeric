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
#ifndef GNM_MS_FORMULA_WRITE_H
#define GNM_MS_FORMULA_WRITE_H

#include <gnumeric.h>
#include "excel.h"

typedef enum {
	EXCEL_CALLED_FROM_CELL,
	EXCEL_CALLED_FROM_SHARED,
	EXCEL_CALLED_FROM_CONDITION,
	EXCEL_CALLED_FROM_VALIDATION,
	EXCEL_CALLED_FROM_VALIDATION_LIST,
	EXCEL_CALLED_FROM_NAME,
	EXCEL_CALLED_FROM_OBJ
} ExcelFuncContext;

guint32 excel_write_formula    (ExcelWriteState *ewb,
				GnmExprTop const *texpr,
				Sheet *sheet, int fn_col, int fn_row,
				ExcelFuncContext context);
guint32 excel_write_array_formula (ExcelWriteState *ewb,
				   GnmExprTop const *texpr,
				   Sheet *sheet, int fn_col, int fn_row);

void excel_write_prep_expressions (ExcelWriteState *ewb);
void excel_write_prep_expr	  (ExcelWriteState *ewb,
				   GnmExprTop const *texpr);
void excel_write_prep_sheet	  (ExcelWriteState *ewb, Sheet const *sheet);

#endif /* GNM_MS_FORMULA_W_H */
