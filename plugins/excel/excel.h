/**
 * excel.h: Excel support interface to gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
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

extern Workbook *ms_excel_read_workbook  (MS_OLE *file);
extern int       ms_excel_write_workbook (MS_OLE *file, Workbook *wb,
					  eBiff_version ver);

/* We need to use these for both read and write */
typedef struct {
	int b, g, r;
} EXCEL_PALETTE_ENTRY;
extern  EXCEL_PALETTE_ENTRY excel_default_palette[];
#define EXCEL_DEF_PAL_LEN   56

extern  char *excel_builtin_formats[];
#define EXCEL_BUILTIN_FORMAT_LEN 0x32

#endif




