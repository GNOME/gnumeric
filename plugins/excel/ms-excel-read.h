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

extern Workbook *ms_excelReadWorkbook(MS_OLE *file) ;

typedef struct _BIFF_BOUNDSHEET_DATA
{
  LONG streamStartPos ;
  eBiff_filetype type ;
  eBiff_hidden   hidden ;
  char *name ;
} BIFF_BOUNDSHEET_DATA ;

typedef struct _MS_EXCEL_SHEET
{
	Sheet *gnum_sheet ;
	struct _MS_EXCEL_WORKBOOK *wb ;
	eBiff_version ver ;
	GList *array_formulae ;      
} MS_EXCEL_SHEET ;

extern void ms_excel_sheet_insert (MS_EXCEL_SHEET *sheet, int xfidx, int col, int row, char *text) ;
extern void ms_excel_set_cell_xf(MS_EXCEL_SHEET *sheet, Cell *cell, guint16 xfidx) ;

typedef struct _MS_EXCEL_PALETTE
{
  int *red ;
  int *green ;
  int *blue ;
  int length ;
} MS_EXCEL_PALETTE ;

typedef struct _BIFF_FONT_DATA
{
  int height ;         /* in 1/20ths of a point   */
  int italic ;         /* boolean                 */
  int struck_out ;     /* boolean : strikethrough */
  int color_idx ;
  int boldness ;       /* 100->1000 dec, normal = 0x190, bold = 0x2bc */
  int script ;         /* sub = -1, none = 0, super = 1 */
  eBiffFontUnderline underline ;
  char *fontname ;
} BIFF_FONT_DATA ;

typedef struct _MS_EXCEL_WORKBOOK
{
	GList *boundsheet_data ;
	GHashTable *XF_cell_records ;
	GHashTable *XF_style_records ;
	GList *excel_sheets ;
	GList *font_data ;
	MS_EXCEL_PALETTE *palette ;
	/**
	 *    Global strings kludge, works for me,
	 * Caveat Emptor -- njl195@zepler.org
	 **/
	char *global_strings;
	int global_string_max;

	/**
	 * Gnumeric parallel workbook
	 **/
	Workbook *gnum_wb ;
} MS_EXCEL_WORKBOOK ;

#endif
