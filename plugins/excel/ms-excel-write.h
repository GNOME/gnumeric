/**
 * ms-excel-write.h: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
#ifndef GNUMERIC_MS_EXCEL_WRITE_H
#define GNUMERIC_MS_EXCEL_WRITE_H

#include "ms-ole.h"
#include "ms-biff.h"
#include "ms-excel-biff.h"

extern int ms_excel_write_workbook (MS_OLE *file, Workbook *wb,
				    eBiff_version ver);

#endif
