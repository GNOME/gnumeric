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

typedef struct _ExcelSheet    ExcelSheet;
typedef struct _ExcelWorkbook ExcelWorkbook;
typedef struct _XF       XF;
typedef struct _Fonts    Fonts;
typedef struct _Formats  Formats;
typedef struct _Palette  Palette;

struct _Palette {
	GHashTable *col_to_idx;
};

struct _Fonts {
	GHashTable *StyleFont_to_idx;
};

struct _Formats {
	GHashTable *StyleFormat_to_idx;
};

struct _XF {
	GHashTable *Style_to_idx;
};

struct _ExcelSheet {
	ExcelWorkbook *wb;
	Sheet         *gnum_sheet;
	GArray        *dbcells;
	MsOlePos   streamPos;
	guint32        boundsheetPos;
	guint32        maxx;
	guint32        maxy;
};

struct _ExcelWorkbook {
	Workbook      *gnum_wb;
	GPtrArray     *sheets;
	eBiff_version  ver;
	Palette       *pal;
	Fonts         *fonts;
	Formats       *formats;
	MsOlePos   streamPos;
};

extern int ms_excel_write_ExcelWorkbook (MsOle *file, ExcelWorkbook *wb,
				    eBiff_version ver);
extern int ms_excel_write_get_sheet_idx (ExcelWorkbook *wb, Sheet *gnum_sheet);
extern int ms_excel_write_get_externsheet_idx (ExcelWorkbook *wb,
					       Sheet *gnum_sheeta,
					       Sheet *gnum_sheetb);

typedef enum { AS_PER_VER, SIXTEEN_BIT, EIGHT_BIT } PutType;
extern int
biff_put_text (BiffPut *bp, char *txt, eBiff_version ver,
	       gboolean write_len, PutType how);

#endif
