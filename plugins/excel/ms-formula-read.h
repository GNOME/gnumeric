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

ExprTree *
ms_excel_parse_formula (ExcelSheet const *sheet,
			guint8 const *mem,
			int fn_col, int fn_row,
			gboolean shared, guint16 length,
			gboolean *array_element) ;

#endif
