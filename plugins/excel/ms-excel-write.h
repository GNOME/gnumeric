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

typedef struct {
	StyleFont  *style_font;
	guint32    color;
	gboolean  is_auto;
	StyleUnderlineType underline;
	gboolean  strikethrough;
} ExcelFont;

typedef struct {
	ExcelWriteState	*ewb;
	Sheet		*gnum_sheet;
	GArray		*dbcells;
	unsigned	 streamPos;
	guint32		 boundsheetPos;
	gint32		 max_col, max_row;
	guint16		 col_xf [SHEET_MAX_COLS];
} ExcelSheet;

struct _ExcelWriteState {
	BiffPut	      *bp;

	IOContext     *io_context;
	Workbook      *gnum_wb;
	WorkbookView  *gnum_wb_view;
	GPtrArray     *sheets;

	struct {
		TwoWayTable *two_way_table;
		MStyle      *default_style;
	} xf;
	struct {
		TwoWayTable *two_way_table;
		guint8 entry_in_use[EXCEL_DEF_PAL_LEN];
	} pal;
	struct {
		TwoWayTable *two_way_table;
	} fonts;
	struct {
		TwoWayTable *two_way_table;
	} formats;

	GHashTable    *function_map;

	GPtrArray     *externnames;
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

int biff_convert_text (char **buf, const char *txt, MsBiffVersion ver);
int biff_put_text (BiffPut *bp, const char *txt, int len,
		   gboolean write_len, PutType how);

int excel_write_workbook (ExcelWriteState *wb, GsfOutfile *file);

int excel_write_get_externsheet_idx (ExcelWriteState *wb,
				     Sheet *gnum_sheeta,
				     Sheet *gnum_sheetb);

int excel_write_map_errcode (Value const * const v);

#endif
