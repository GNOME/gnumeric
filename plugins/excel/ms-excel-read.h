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

typedef struct _ExcelWorkbook	ExcelWorkbook;
typedef struct _ExcelSheet	ExcelSheet;

struct _ExcelSheet {
	MSContainer container;

	Sheet		*sheet;
	ExcelWorkbook	*ewb;
	GHashTable	*shared_formulae, *tables;
	GPtrArray	*externsheet_v7;

	gboolean freeze_panes;
};

typedef struct {
	guint16 index;
	guint32 streamStartPos;
	MsBiffFileType type;
	MsBiffHidden   hidden;
	char *name;
	ExcelSheet *sheet;
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
XLSharedFormula *excel_sheet_shared_formula (ExcelSheet const *sheet,
					     CellPos const    *key);
XLDataTable	*excel_sheet_data_table	    (ExcelSheet const *esheet,
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
	GPtrArray	 *names;
	GPtrArray	 *supbooks;
	GArray		 *externsheet_v8;
	GPtrArray	 *externsheet_v7;
	ExcelPalette	 *palette;
	char		**global_strings;
	guint32		  global_string_max;

	gboolean          warn_unsupported_graphs;
	GSList		 *delayed_names;

	ExprTreeSharer   *expr_sharer;

	Workbook            *gnum_wb;
};

char       *biff_get_text (guint8 const *ptr, guint32 length, guint32 *byte_length);
char const *biff_get_error_text (guint8 err);

GnmExpr const	*excel_workbook_get_name (ExcelWorkbook const *ewb,
					  ExcelSheet const *esheet, guint16 i,
					  Sheet *sheet);
Sheet		*excel_externsheet_v7	 (ExcelWorkbook const *wb,
					  ExcelSheet const *esheet, gint16 i);
void		 excel_externsheet_v8	 (ExcelWorkbook const *wb,  gint16 i,
					  Sheet **first, Sheet **last);

MsBiffBofData  *ms_biff_bof_data_new     (BiffQuery * q);
void		ms_biff_bof_data_destroy (MsBiffBofData * data);

StyleColor  *excel_palette_get (ExcelPalette const *pal, gint idx);

void	    excel_read_IMDATA (BiffQuery *q);

/* A utility routine to handle unexpected BIFF records */
void excel_unexpected_biff (BiffQuery *q, char const *state, int debug_level);

void excel_read_cleanup (void);
void excel_read_init (void);

#endif
