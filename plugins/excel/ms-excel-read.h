/*
 * ms-excel.h: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 */
#ifndef GNUMERIC_MS_EXCEL_H
#define GNUMERIC_MS_EXCEL_H

#include "ms-ole.h"
#include "ms-biff.h"

extern Workbook *ms_excelReadWorkbook(MS_OLE_FILE *file) ;

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
} MS_EXCEL_SHEET ;

void ms_excel_sheet_insert (MS_EXCEL_SHEET *sheet, int xfidx, int col, int row, char *text) ;

typedef struct _MS_EXCEL_PALETTE
{
  int *red ;
  int *green ;
  int *blue ;
  int length ;
} MS_EXCEL_PALETTE ;

typedef struct _BIFF_FONT_DATA
{
  int height ;         // in 1/20ths of a point
  int italic ;         // boolean
  int struck_out ;     // boolean : strikethrough
  int color_idx ;
  int boldness ;       // 100->1000 dec, normal = 0x190, bold = 0x2bc
  int script ;         // sub = -1, none = 0, super = 1
  eBiffFontUnderline underline ;
  char *fontname ;
} BIFF_FONT_DATA ;

typedef struct _MS_EXCEL_WORKBOOK
{
  GList *boundsheet_data ;
  GList *XF_records ;
  GList *excel_sheets ;
  GList *font_data ;
  MS_EXCEL_PALETTE *palette ;
  // Gnumeric parallel workbook
  Workbook *gnum_wb ;
} MS_EXCEL_WORKBOOK ;

#endif
