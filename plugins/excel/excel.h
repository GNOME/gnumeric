/**
 * excel.h: Excel support interface to gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *
 * (C) 1998, 1999 Michael Meeks
 **/
#ifndef GNUMERIC_EXCEL_H
#define GNUMERIC_EXCEL_H

#include "ms-ole.h"
#include "sheet.h"

typedef enum _eBiff_version { eBiffV2=2, eBiffV3=3,
			      eBiffV4=4,
			      eBiffV5=5, /* Excel 5.0 */
			      eBiffV7=7, /* Excel 95 */
			      eBiffV8=8, /* Excel 97 */
			      eBiffVUnknown=0} eBiff_version ;

extern int   ms_excel_read_workbook  (CommandContext *context,
				      Workbook *wb, MsOle *file);
/*
 * Here's why the state which is carried from excel_check_write to
 * ms_excel_write_workbook is void *: The state is actually an
 * ExcelWorksheet * as defined in ms-excel-write.h. But we can't
 * import that definition here: There's a different definition of
 * ExcelWorksheet in ms-excel-read.h.
 */
extern int      ms_excel_check_write (CommandContext *context, void **state,
				      Workbook *wb, eBiff_version ver);
extern int      ms_excel_write_workbook (CommandContext *context, MsOle *file,
					 void *state, eBiff_version ver);

/* We need to use these for both read and write */
typedef struct {
	int r, g, b;
} EXCEL_PALETTE_ENTRY;
extern  EXCEL_PALETTE_ENTRY const excel_default_palette[];
#define EXCEL_DEF_PAL_LEN   56

extern  char *excel_builtin_formats[];
#define EXCEL_BUILTIN_FORMAT_LEN 0x32

typedef struct
{
	char *prefix ;
	int num_args ; /* -1 for multi-arg */
		       /* -2 for unknown args */
} FormulaFuncData;

extern FormulaFuncData formula_func_data[];
#define FORMULA_FUNC_DATA_LEN 368

#define ROW_BLOCK_MAX_LEN 32
#define WRITEACCESS_LEN  112
#endif
