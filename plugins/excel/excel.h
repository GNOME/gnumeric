/**
 * excel.h: Excel support interface to gnumeric
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998, 1999, 2000 Michael Meeks
 **/
#ifndef GNUMERIC_EXCEL_H
#define GNUMERIC_EXCEL_H

#include <libole2/ms-ole.h>

#include "gnumeric.h"

typedef gboolean (*MsExcelReadGbFn) (IOContext *context, Workbook *wb, MsOle *f);
extern MsExcelReadGbFn ms_excel_read_gb;

typedef enum { MS_BIFF_V2 = 2,
	       MS_BIFF_V3 = 3,
	       MS_BIFF_V4 = 4,
	       MS_BIFF_V5 = 5, /* Excel 5.0 */
	       MS_BIFF_V7 = 7, /* Excel 95 */
	       MS_BIFF_V8 = 8, /* Excel 97 */
	       MS_BIFF_V_UNKNOWN = 0} MsBiffVersion ;

extern void   ms_excel_read_workbook  (IOContext *context,
                                       WorkbookView *new_wb, MsOle *file);
/*
 * Here's why the state which is carried from excel_check_write to
 * ms_excel_write_workbook is void *: The state is actually an
 * ExcelWorkbook * as defined in ms-excel-write.h. But we can't
 * import that definition here: There's a different definition of
 * ExcelWorkbook in ms-excel-read.h.
 */
extern int      ms_excel_check_write (IOContext *context, void **state,
                                      WorkbookView *wb, MsBiffVersion ver);
extern void     ms_excel_write_workbook (IOContext *context, MsOle *file,
                                         void *state, MsBiffVersion ver);
void ms_excel_write_free_state (void *state);


/* We need to use these for both read and write */
typedef struct {
	int r, g, b;
} EXCEL_PALETTE_ENTRY;
extern  EXCEL_PALETTE_ENTRY const excel_default_palette[];
#define EXCEL_DEF_PAL_LEN   56

extern  char const *excel_builtin_formats[];
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

#endif /* GNUMERIC_EXCEL_H */
