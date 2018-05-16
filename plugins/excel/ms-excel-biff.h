#ifndef GNM_EXCEL_BIFF_H
#define GNM_EXCEL_BIFF_H

/**
 * ms-excel-biff.h: MS Excel BIFF header for Gnumeric
 * contains data about the Excel BIFF records
 *
 * Authors:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2007 Jody Goldberg
 **/

#include "excel.h"
#include "ms-biff.h"

#define EX_SETROW(p,d)    (GSF_LE_SET_GUINT16(p + 0, d))
#define EX_SETCOL(p,d)    (GSF_LE_SET_GUINT16(p + 2, d))
#define EX_SETXF(p,d)     (GSF_LE_SET_GUINT16(p + 4, d))

/* Version info types as found in various Biff records */
typedef enum {
	MS_BIFF_TYPE_Workbook	= 0,
	MS_BIFF_TYPE_VBModule	= 1,
	MS_BIFF_TYPE_Worksheet	= 2,
	MS_BIFF_TYPE_Chart	= 3,
	MS_BIFF_TYPE_Macrosheet	= 4,
	MS_BIFF_TYPE_Workspace	= 5,
	MS_BIFF_TYPE_Unknown	= 6
} MsBiffFileType ;

/* Cell / XF types */
typedef enum { MS_BIFF_X_STYLE=0, MS_BIFF_X_CELL=1 } MsBiffXfType ;
typedef enum { MS_BIFF_F_MS=0, MS_BIFF_F_LOTUS=1 } MsBiffFormat ;

typedef enum {
	XLS_ULINE_NONE = 1,
	XLS_ULINE_SINGLE = 2,
	XLS_ULINE_DOUBLE = 3,
	XLS_ULINE_SINGLE_ACC = 4,
	XLS_ULINE_DOUBLE_ACC = 5
} MsBiffFontUnderline ;

typedef enum {
	MS_BIFF_H_A_GENERAL = 0,
	MS_BIFF_H_A_LEFT    = 1,
	MS_BIFF_H_A_CENTER  = 2,
	MS_BIFF_H_A_RIGHT   = 3,
	MS_BIFF_H_A_FILL    = 4,
	MS_BIFF_H_A_JUSTIFTY = 5,
	MS_BIFF_H_A_CENTER_ACROSS_SELECTION = 6,
	MS_BIFF_H_A_DISTRIBUTED = 7
} MsBiffHAlign;

typedef enum {
	MS_BIFF_V_A_TOP     = 0,
	MS_BIFF_V_A_CENTER  = 1,
	MS_BIFF_V_A_BOTTOM  = 2,
	MS_BIFF_V_A_JUSTIFY = 3,
	MS_BIFF_V_A_DISTRIBUTED = 4
} MsBiffVAlign;

typedef enum { /* Differences to parent styles */
	MS_BIFF_D_FORMAT_BIT = 10,
	MS_BIFF_D_FONT_BIT   = 11,
	MS_BIFF_D_ALIGN_BIT  = 12,
	MS_BIFF_D_BORDER_BIT = 13,
	MS_BIFF_D_FILL_BIT   = 14,
	MS_BIFF_D_LOCK_BIT   = 15
} MsBiffDifferences;

typedef struct {
	MsBiffVersion version;
	MsBiffFileType type;
} MsBiffBofData;

#define XLS_MaxRow_V7	16384
#define XLS_MaxRow_V8	65536
#define XLS_MaxCol	256

#include "biff-types.h"

#endif /* GNM_EXCEL_BIFF_H */
