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
	Sheet *a, *b;
	int idx_a, idx_b;
} ExcelSheetPair;

typedef struct {
	/* Don't use StyleFont.  In the case where a font does not exist on the
	 * display system it does the wrong thing.  MStyle can contain an
	 * invalid font.  StyleFont gets remapped to the default
	 */
	guint32    color;
	char const *font_name;
	double	  size_pts;
	gboolean  is_bold;
	gboolean  is_italic;
	gboolean  is_auto;
	StyleUnderlineType	underline;
	gboolean		strikethrough;
} ExcelFont;

typedef struct {
	ExcelWriteState	*ewb;
	Sheet		*gnum_sheet;
	unsigned	 streamPos;
	guint32		 boundsheetPos;
	gint32		 max_col, max_row;
	guint16		 col_xf    [SHEET_MAX_COLS];
	MStyle		*col_style [SHEET_MAX_COLS];
} ExcelWriteSheet;

struct _ExcelWriteState {
	BiffPut	      *bp;

	IOContext     *io_context;
	Workbook      const *gnum_wb;
	WorkbookView  const *gnum_wb_view;
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
	GHashTable    *sheet_pairs;

	/* we use the ewb as a closure for things, this is useful */
	int tmp_counter;

	gboolean	 double_stream_file;
	GPtrArray	*externnames;
	GHashTable	*names;
	unsigned	 streamPos;

	/* no need to use a full fledged two table, we already know that the
	 * Strings are unique. */
	struct {
		GHashTable *strings;
		GPtrArray  *indicies;
	} sst;

	unsigned obj_count;
};

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
#define PALETTE_AUTO_FONT 0x7fff
#define FILL_NONE 0
#define FILL_SOLID 1
#define FILL_MAGIC FILL_NONE
#define BORDER_MAGIC STYLE_BORDER_NONE

typedef enum {
	STR_NO_LENGTH		= 0,
	STR_ONE_BYTE_LENGTH	= 1,
	STR_TWO_BYTE_LENGTH	= 2,
	STR_LENGTH_MASK		= 3,
	/* biff7 will always be LEN_IN_BYTES,
	 * biff8 will respect the flag and default to length in characters */
	STR_LEN_IN_BYTES	= 4,
	STR_SUPPRESS_HEADER	= 8
} WriteStringFlags;

unsigned excel_write_string_len (guint8 const *txt, unsigned *bytes);
unsigned excel_write_string	(BiffPut *bp, guint8 const *txt,
				 WriteStringFlags flags);

int excel_write_workbook (ExcelWriteState *wb, GsfOutfile *file);

int excel_write_get_externsheet_idx (ExcelWriteState *wb,
				     Sheet *gnum_sheeta,
				     Sheet *gnum_sheetb);

int excel_write_map_errcode (Value const * const v);

#endif
