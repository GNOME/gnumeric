/*
 * ms-formula-read.h: MS Excel -> Gnumeric formula conversion
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 */
#ifndef GNUMERIC_MS_FORMULA_W_H
#define GNUMERIC_MS_FORMULA_W_H

#include <glib.h>

#include "ms-excel-write.h"
#include "ms-biff.h"
#include "formula-types.h"

guint32
ms_excel_write_formula (BIFF_PUT *bp, ExcelSheet *sheet, ExprTree *expr,
			int fn_col, int fn_row);

#endif
