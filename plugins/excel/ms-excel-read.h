/**
 * ms-excel.h: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
#ifndef GNUMERIC_MS_EXCEL_H
#define GNUMERIC_MS_EXCEL_H

#include "ms-ole.h"
#include "ms-biff.h"
#include "ms-excel-biff.h"

typedef struct _ExcelSheet
{
	gboolean blank;
	Sheet *gnum_sheet;
	struct _ExcelWorkbook *wb;
	eBiff_version ver;
	GHashTable *shared_formulae;
} ExcelSheet;

typedef struct _BiffBoundsheetData
{
	guint16 index;
	guint32 streamStartPos;
	eBiff_filetype type;
	eBiff_hidden   hidden;
	char *name;
	ExcelSheet *sheet;
} BiffBoundsheetData;

typedef struct {
	guint16 col;
	guint16 row;	
} BiffSharedFormulaKey;

typedef struct {
	BiffSharedFormulaKey key;
	guint8 *data;
	guint32 data_len;
} BiffSharedFormula;

extern ExprTree *ms_excel_sheet_shared_formula (ExcelSheet *sheet,
						int shr_col, int shr_row,
						int col, int row);

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
	eBiffFontUnderline underline;
	char *fontname;
	StyleFont *style_font;
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
	GHashTable *boundsheet_data_by_stream;
	GHashTable *boundsheet_data_by_index;
	GPtrArray  *XF_cell_records;
	GPtrArray  *XF_style_records;
	GHashTable *font_data;
	GHashTable *format_data;
	GHashTable *name_data;		/* Seems to be for external names */
	GPtrArray  *internal_names;	/* For internal names */
	int read_drawing_group;
	GPtrArray *excel_sheets;
	BiffExternSheetData *extern_sheets;
	guint16 num_extern_sheets;
	ExcelPalette *palette;
	char **global_strings;
	int global_string_max;

	/**
	 * Gnumeric parallel workbook
   	 **/
	Workbook *gnum_wb;
} ExcelWorkbook;

extern Sheet* biff_get_externsheet_name (ExcelWorkbook *wb, guint16 idx, gboolean get_first);
extern char* biff_get_text (guint8 *ptr, guint32 length, guint32 *byte_length);
extern const char* biff_get_error_text (const guint8 err);
extern char* biff_name_data_get_name (ExcelSheet *sheet, guint16 idx);
extern char const * ms_excel_get_name (ExcelWorkbook *wb, int name_idx);

extern BIFF_BOF_DATA * ms_biff_bof_data_new (BiffQuery * q);
extern void ms_biff_bof_data_destroy (BIFF_BOF_DATA * data);

/* A utility routine to handle unexpected BIFF records */
extern void          ms_excel_unexpected_biff (BiffQuery *q,
					       char const * const state);
#endif
