/**
 * ms-excel-write.h: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998, 1999, 2000 Michael Meeks
 **/
#ifndef GNUMERIC_MS_EXCEL_WRITE_H
#define GNUMERIC_MS_EXCEL_WRITE_H

#include "ms-biff.h"
#include "ms-excel-biff.h"
#include "ms-excel-util.h"
#include "style.h"

typedef struct _ExcelFont     ExcelFont;
typedef struct _ExcelSheet    ExcelSheet;
typedef struct _ExcelWorkbook ExcelWorkbook;
typedef struct _XF       XF;
typedef struct _Fonts    Fonts;
typedef struct _Formats  Formats;
typedef struct _Palette  Palette;

struct _ExcelFont {
	StyleFont  *style_font;
	guint32    color;
	gboolean  is_auto;
	StyleUnderlineType underline;
	gboolean  strikethrough;
};

struct _Palette {
	TwoWayTable *two_way_table;
	guint8 entry_in_use[EXCEL_DEF_PAL_LEN];
};

struct _Fonts {
	TwoWayTable *two_way_table;
};

struct _Formats {
	TwoWayTable *two_way_table;
};

struct _XF {
	TwoWayTable *two_way_table;
	MStyle      *default_style;
};

struct _ExcelSheet {
	ExcelWorkbook *wb;
	Sheet         *gnum_sheet;
	GArray        *dbcells;
	unsigned       streamPos;
	guint32        boundsheetPos;
	gint32         max_col, max_row;
	GHashTable    *formula_cache;
	guint16	       col_xf [SHEET_MAX_COLS];
};

struct _ExcelWorkbook {
	IOContext     *io_context;
	Workbook      *gnum_wb;
	WorkbookView  *gnum_wb_view;
	GPtrArray     *sheets;
	MsBiffVersion  ver;
	XF            *xf;
	Palette       *pal;
	Fonts         *fonts;
	Formats       *formats;
	GPtrArray     *names;
	unsigned       streamPos;
};

typedef enum {
	AS_PER_VER,  /* Biff7: byte length, UTF8, Biff8: word length, unicode */
	SIXTEEN_BIT, /* word length, Biff7: UTF8, Biff8: unicode */
	EIGHT_BIT    /* byte length, Biff7: UTF8, Biff8: unicode */
} PutType;

#define XF_RESERVED 21
#define XF_MAGIC 0
#define FONTS_MINIMUM 5
#define FONT_SKIP 4
#define FONT_MAGIC 0
#define FORMAT_MAGIC 0
#define PALETTE_BLACK 8
#define PALETTE_WHITE 9
#define PALETTE_AUTO_PATTERN 64
#define PALETTE_AUTO_BACK 65
#define PALETTE_AUTO_FONT 127
#define FILL_NONE 0
#define FILL_SOLID 1
#define FILL_MAGIC FILL_NONE
#define BORDER_MAGIC STYLE_BORDER_NONE

extern int
biff_convert_text (char **buf, const char *txt, MsBiffVersion ver);
extern int
biff_put_text (BiffPut *bp, const char *txt, int len, MsBiffVersion ver,
	       gboolean write_len, PutType how);
extern int ms_excel_write_ExcelWorkbook (GsfOutfile *file, ExcelWorkbook *wb,
					 MsBiffVersion ver);
extern int ms_excel_write_get_sheet_idx (ExcelWorkbook *wb, Sheet *gnum_sheet);
extern int ms_excel_write_get_externsheet_idx (ExcelWorkbook *wb,
					       Sheet *gnum_sheeta,
					       Sheet *gnum_sheetb);
extern int
ms_excel_write_map_errcode (Value const * const v);

#endif
