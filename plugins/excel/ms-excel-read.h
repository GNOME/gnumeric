/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * ms-excel.h: MS Excel support for Gnumeric
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2002 Michael Meeks
 **/
#ifndef GNUMERIC_MS_EXCEL_H
#define GNUMERIC_MS_EXCEL_H

#include "ms-biff.h"
#include "ms-excel-biff.h"
#include "ms-container.h"
#include <expr.h>

typedef struct {
	Workbook  *wb;
	Sheet 	  *first, *last;
	unsigned   supbook;
} ExcelExternSheetV8;
typedef enum {
	EXCEL_SUP_BOOK_STD,
	EXCEL_SUP_BOOK_SELFREF,
	EXCEL_SUP_BOOK_PLUGIN
} ExcelSupBookType;
typedef struct {
	ExcelSupBookType type;
	Workbook  *wb;
	GPtrArray *externname;
} ExcelSupBook;

typedef struct {
	MSContainer container;

	Sheet		*sheet;
	GHashTable	*shared_formulae, *tables;

	gboolean freeze_panes;
} ExcelReadSheet;

typedef struct {
	guint16 index;
	guint32 streamStartPos;
	MsBiffFileType type;
	MsBiffHidden   hidden;
	char *name;
	ExcelReadSheet *sheet;
} BiffBoundsheetData;

typedef struct {
	CellPos key;
	guint8 *data;
	guint32 data_len;
	gboolean is_array;
} XLSharedFormula;

typedef struct {
	CellPos key;
	guint8 *data;
	guint32 data_len;
} XLDataTable;

/* Use the upper left corner as the key to a collection of shared formulas */
XLSharedFormula *excel_sheet_shared_formula (ExcelReadSheet const *sheet,
					     CellPos const    *key);
XLDataTable	*excel_sheet_data_table	    (ExcelReadSheet const *esheet,
					     CellPos const    *key);

typedef struct {
	int *red;
	int *green;
	int *blue;
	int length;
	StyleColor **gnum_cols;
} ExcelPalette;

typedef struct {
	guint16 index;
	int height;         /* in 1/20ths of a point   */
	int italic;         /* boolean                 */
	int struck_out;     /* boolean : strikethrough */
	int color_idx;
	int boldness;       /* 100->1000 dec, normal = 0x190, bold = 0x2bc */
	int script;         /* sub = -1, none = 0, super = 1 */
	MsBiffFontUnderline underline;
	char *fontname;
} BiffFontData;

typedef struct {
	guint16 idx;
	char *name;
} BiffFormatData;

struct _ExcelWorkbook {
	MSContainer	  container;
	IOContext	 *context;
	WorkbookView	 *wbv;

	GPtrArray	 *excel_sheets;
	GHashTable	 *boundsheet_data_by_stream;
	GPtrArray	 *boundsheet_sheet_by_index;
	GPtrArray	 *XF_cell_records;
	GHashTable	 *font_data;
	GHashTable	 *format_data; /* leave as a hash */
	struct {
		GArray	 *supbook;
		GArray	 *externsheet;
	} v8; /* biff8 does this in the workbook */
	ExcelPalette	 *palette;
	char		**global_strings;
	guint32		  global_string_max;

	gboolean          is_gnumeric_1_0_x;

	ExprTreeSharer   *expr_sharer;

	Workbook            *gnum_wb;
};

char       *biff_get_text (guint8 const *ptr, guint32 length, guint32 *byte_length);
Value *biff_get_error (EvalPos const *pos, guint8 const err);

Sheet		*excel_externsheet_v7	 (MSContainer const *container, gint16 i);
ExcelExternSheetV8 const *excel_externsheet_v8 (ExcelWorkbook const *wb, gint16 i);

void		excel_read_EXTERNSHEET_v7 (BiffQuery const *q, MSContainer *container);
MsBiffBofData  *ms_biff_bof_data_new     (BiffQuery * q);
void		ms_biff_bof_data_destroy (MsBiffBofData * data);

StyleColor  *excel_palette_get (ExcelPalette const *pal, gint idx);

void	    excel_read_IMDATA (BiffQuery *q);

/* A utility routine to handle unexpected BIFF records */
void excel_unexpected_biff (BiffQuery *q, char const *state, int debug_level);

void excel_read_cleanup (void);
void excel_read_init (void);

#endif
