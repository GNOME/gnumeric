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

typedef struct _ExcelSheet
{
	MSContainer container;

	Sheet *gnum_sheet;
	struct _ExcelWorkbook *wb;
	GHashTable *shared_formulae;
	double base_char_width;
	double base_char_width_default;

	gboolean freeze_panes : 1;
} ExcelSheet;

typedef struct _BiffBoundsheetData
{
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
} BiffSharedFormula;

/* Use the upper left corner as the key to a collection of shared formulas */
BiffSharedFormula *ms_excel_sheet_shared_formula (ExcelSheet const *sheet,
						  CellPos const    *key);

typedef struct _ExcelPalette
{
	int *red;
	int *green;
	int *blue;
	int length;
	StyleColor **gnum_cols;
} ExcelPalette;

typedef struct _BiffFontData
{
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

typedef struct _BiffExternSheetData {
	Sheet *first_sheet;
	Sheet *last_sheet;
} XLExternSheetV8;

typedef struct _BiffFormatData {
	guint16 idx;
	char *name;
} BiffFormatData;

typedef struct _ExcelWorkbook
{
	MSContainer  container;
	IOContext   *context;

	GPtrArray	 *excel_sheets;
	GHashTable	 *boundsheet_data_by_stream;
	GHashTable	 *boundsheet_data_by_index;
	GPtrArray	 *XF_cell_records;
	GHashTable	 *font_data;
	GHashTable	 *format_data; /* leave as a hash */
	GPtrArray	 *names;
	GArray		 *extern_sheet_v8;
	GPtrArray	 *extern_sheet_v7;
	ExcelPalette	 *palette;
	char		**global_strings;
	guint32		  global_string_max;

	gboolean          warn_unsupported_graphs;
	GSList		 *delayed_names;

	ExprTreeSharer   *expr_sharer;

	/**
	 * Gnumeric parallel workbook
   	 **/
	Workbook            *gnum_wb;
} ExcelWorkbook;

char       *biff_get_text (guint8 const *ptr, guint32 length, guint32 *byte_length);
char const *biff_get_error_text (guint8 err);

GnmExpr const	 	*ms_excel_workbook_get_name  (ExcelWorkbook const *ewb, int idx);
ExcelSheet		*ms_excel_workbook_get_sheet (ExcelWorkbook const *wb, guint idx);
XLExternSheetV8 const	*ms_excel_workbook_get_externsheet_v8 (ExcelWorkbook const *wb,
							       guint idx);
Sheet 			*ms_excel_workbook_get_externsheet_v7 (ExcelWorkbook const *wb,
							       int idx, int sheet_index);

MsBiffBofData * ms_biff_bof_data_new (BiffQuery * q);
void ms_biff_bof_data_destroy (MsBiffBofData * data);

StyleColor  *ms_excel_palette_get (ExcelPalette const *pal, gint idx);

void	    ms_excel_read_imdata (BiffQuery *q);

/* A utility routine to handle unexpected BIFF records */
void          ms_excel_unexpected_biff (BiffQuery *q,
					char const *state,
					int debug_level);

void ms_excel_read_cleanup (void);
void ms_excel_read_init (void);

#endif
