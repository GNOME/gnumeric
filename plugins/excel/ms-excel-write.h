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

#include <libole2/ms-ole.h>

#include "ms-biff.h"
#include "ms-excel-biff.h"
#include "ms-excel-util.h"
#include "style.h"

typedef struct _ExcelFont     ExcelFont;
typedef struct _ExcelCell     ExcelCell;
typedef struct _ExcelSheet    ExcelSheet;
typedef struct _ExcelWorkbook ExcelWorkbook;
typedef struct _XF       XF;
typedef struct _Fonts    Fonts;
typedef struct _Formats  Formats;
typedef struct _Palette  Palette;

struct _ExcelFont {
	StyleFont  *style_font;
	guint32    color;
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

struct _ExcelCell {
	gint     xf;
	Cell    *gnum_cell;
};

struct _ExcelSheet {
	ExcelWorkbook *wb;
	Sheet         *gnum_sheet;
	GArray        *dbcells;
	MsOlePos       streamPos;
	guint32        boundsheetPos;
	gint32         max_col, max_row;
	GHashTable    *formula_cache;
	gpointer       cell_used_map;
	ExcelCell    **cells;
	double         base_char_width;
	double         base_char_width_default;
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
	MsOlePos   streamPos;
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
#define PALETTE_BLACK 0
#define PALETTE_ALSO_BLACK 8
#define PALETTE_WHITE 1
#define FILL_MAGIC 0
#define BORDER_MAGIC STYLE_BORDER_NONE

extern int
biff_put_text (BiffPut *bp, const char *txt, MsBiffVersion ver,
	       gboolean write_len, PutType how);
extern int ms_excel_write_ExcelWorkbook (MsOle *file, ExcelWorkbook *wb,
				          MsBiffVersion ver);
extern int ms_excel_write_get_sheet_idx (ExcelWorkbook *wb, Sheet *gnum_sheet);
extern int ms_excel_write_get_externsheet_idx (ExcelWorkbook *wb,
					       Sheet *gnum_sheeta,
					       Sheet *gnum_sheetb);
extern int
ms_excel_write_map_errcode (Value const * const v);

#endif
