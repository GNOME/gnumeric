/**
 * excel.h: Excel support interface to gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
#ifndef GNUMERIC_EXCEL_H
#define GNUMERIC_EXCEL_H

#include "ms-ole.h"

typedef enum _eBiff_version { eBiffV2=2, eBiffV3=3,
			      eBiffV4=4, eBiffV5=5,
			      eBiffV7=7,
			      eBiffV8=8, eBiffVUnknown=0} eBiff_version ;

extern Workbook *ms_excel_read_workbook  (MS_OLE *file);
extern int       ms_excel_write_workbook (MS_OLE *file, Workbook *wb,
					  eBiff_version ver);

#endif

