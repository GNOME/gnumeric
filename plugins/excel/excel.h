/**
 * excel.h: Excel support interface to gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
#ifndef GNUMERIC_EXCEL_H
#define GNUMERIC_EXCEL_H

#include "ms-ole.h"

extern Workbook *ms_excel_read_workbook  (MS_OLE *file);
extern void      ms_excel_write_workbook (MS_OLE *file, Workbook *wb);

#endif
