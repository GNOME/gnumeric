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

typedef struct _MS_EXCEL_WORKBOOK MS_EXCEL_WORKBOOK;

typedef struct _MS_EXCEL_SHEET
{
	gboolean blank;
	Sheet *gnum_sheet;
	MS_EXCEL_WORKBOOK *wb;
	eBiff_version ver;
} MS_EXCEL_SHEET;

typedef struct _BIFF_BOUNDSHEET_DATA
{
	guint16 index;
	guint32 streamStartPos;
	eBiff_filetype type;
	eBiff_hidden   hidden;
	char *name;
	MS_EXCEL_SHEET *sheet;
} BIFF_BOUNDSHEET_DATA;

typedef struct {
	guint16 col;
	guint16 row;	
} BIFF_SHARED_FORMULA_KEY;

typedef struct {
	BIFF_SHARED_FORMULA_KEY key;
	BYTE *data;
	guint32 data_len;
} BIFF_SHARED_FORMULA;

extern ExprTree *ms_excel_workbook_shared_formula (MS_EXCEL_WORKBOOK *wb,
						   int shr_col, int shr_row,
						   int col, int row);

typedef struct _MS_EXCEL_PALETTE
{
	int *red;
	int *green;
	int *blue;
	int length;
	StyleColor **gnum_cols;
} MS_EXCEL_PALETTE;

typedef struct _BIFF_FONT_DATA
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
} BIFF_FONT_DATA;

typedef struct _BIFF_EXTERNSHEET_DATA {
	guint16 sup_idx;
	guint16 first_tab;
	guint16 last_tab;
} BIFF_EXTERNSHEET_DATA;

typedef struct _BIFF_FORMAT_DATA {
	guint16 idx;
	char *name;
} BIFF_FORMAT_DATA;

struct _MS_EXCEL_WORKBOOK
{
	GHashTable *boundsheet_data_by_stream;
	GHashTable *boundsheet_data_by_index;
	GPtrArray  *XF_cell_records;
	GPtrArray  *XF_style_records;
	GHashTable *font_data;
	GHashTable *format_data;
	GPtrArray  *name_data;
	int read_drawing_group;
	GPtrArray *excel_sheets;
	BIFF_EXTERNSHEET_DATA *extern_sheets;
	guint16 num_extern_sheets;
	MS_EXCEL_PALETTE *palette;
	char **global_strings;
	int global_string_max;
	/* Stored here for convenience but cleared / sheet */
	GHashTable *shared_formulae;
	eBiff_version ver;

	/**
	 * Gnumeric parallel workbook
   	 **/
	Workbook *gnum_wb;
};

extern Sheet* biff_get_externsheet_name (MS_EXCEL_WORKBOOK *wb, guint16 idx, gboolean get_first);
extern char* biff_get_text (BYTE *ptr, guint32 length, guint32 *byte_length);
extern const char* biff_get_error_text (const guint8 err);
extern ExprTree *biff_name_data_get_name (MS_EXCEL_WORKBOOK *wb, guint16 idx);
extern BIFF_BOF_DATA * ms_biff_bof_data_new (BIFF_QUERY * q);
extern void ms_biff_bof_data_destroy (BIFF_BOF_DATA * data);
#endif
