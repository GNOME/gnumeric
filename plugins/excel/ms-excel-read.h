/**
 * ms-excel.h: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998, 1999, 2000 Michael Meeks
 **/
#ifndef GNUMERIC_MS_EXCEL_H
#define GNUMERIC_MS_EXCEL_H

#include "ms-biff.h"
#include "ms-excel-biff.h"
#include "ms-container.h"

typedef struct _ExcelSheet
{
	MSContainer container;

	Sheet *gnum_sheet;
	struct _ExcelWorkbook *wb;
	GHashTable *shared_formulae;
	double base_char_width;
	double base_char_width_default;
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
	guint16 sup_idx;
	guint16 first_tab;
	guint16 last_tab;
} BiffExternSheetData;

typedef struct _BiffFormatData {
	guint16 idx;
	char *name;
} BiffFormatData;

typedef struct _ExcelWorkbook
{
	MSContainer container;

	GPtrArray           *excel_sheets;
	GHashTable          *boundsheet_data_by_stream;
	GHashTable          *boundsheet_data_by_index;
	GPtrArray           *XF_cell_records;
	GHashTable          *font_data;
	GHashTable          *format_data; /* leave as a hash */
	GPtrArray           *name_data;
	int                  read_drawing_group;
	BiffExternSheetData *extern_sheets;
	guint16              num_extern_sheets;
	ExcelPalette        *palette;
	char               **global_strings;
	guint32              global_string_max;

	/* Indexed in the order they are read */
	GPtrArray           *charts;

	/**
	 * Gnumeric parallel workbook
   	 **/
	Workbook            *gnum_wb;
} ExcelWorkbook;

ExcelSheet * ms_excel_workbook_get_sheet (ExcelWorkbook *wb, guint idx);
Sheet* biff_get_externsheet_name (ExcelWorkbook *wb, guint16 idx, gboolean get_first);
char* biff_get_text (guint8 const *ptr, guint32 length, guint32 *byte_length);
char const* biff_get_error_text (guint8 err);
ExprTree* biff_name_data_get_name (ExcelSheet const *sheet, int idx);

MsBiffBofData * ms_biff_bof_data_new (BiffQuery * q);
void ms_biff_bof_data_destroy (MsBiffBofData * data);

StyleFormat *biff_format_data_lookup (ExcelWorkbook *wb, guint16 idx);
StyleColor  *ms_excel_palette_get (ExcelPalette const *pal, gint idx);

void	    ms_excel_read_imdata (BiffQuery *q);

/* A utility routine to handle unexpected BIFF records */
void          ms_excel_unexpected_biff (BiffQuery *q,
					char const *state,
					int debug_level);

void ms_excel_read_cleanup (void);

#endif
