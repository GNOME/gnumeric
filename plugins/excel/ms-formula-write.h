/*
 * ms-formula-read.h: MS Excel -> Gnumeric formula conversion
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *
 * (C) 1998, 1999, 2000 Michael Meeks
 */
#ifndef GNUMERIC_MS_FORMULA_W_H
#define GNUMERIC_MS_FORMULA_W_H

#include <glib.h>

#include "ms-excel-write.h"
#include "ms-biff.h"
#include "formula-types.h"

extern guint32 ms_excel_write_formula    (BiffPut *bp, ExcelSheet *sheet,
					  ExprTree *expr,
					  int fn_col, int fn_row);

typedef enum { EXCEL_NAME, EXCEL_EXTERNNAME } formula_write_t;
extern void    ms_formula_build_pre_data (ExcelSheet *sheet, ExprTree *tree);
extern void    ms_formula_write_pre_data (BiffPut *bp, ExcelSheet *sheet,
					  formula_write_t which,
					  eBiff_version ver);

extern void    ms_formula_cache_init     (ExcelSheet *sheet);
extern void    ms_formula_cache_shutdown (ExcelSheet *sheet);

#endif
