#ifndef GNUMERIC_EXCEL_BIFF_H
#define GNUMERIC_EXCEL_BIFF_H

/**
 * ms-excel-biff.h: MS Excel BIFF header for Gnumeric
 * contains data about the Excel BIFF records
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998, 1999, 2000 Michael Meeks
 **/

#include "excel.h"
#include "ms-biff.h"

/* Pass this a BiffQuery * */
#define EX_GETROW(p)      (GSF_LE_GET_GUINT16(p->data + 0))
#define EX_GETCOL(p)      (GSF_LE_GET_GUINT16(p->data + 2))
#define EX_GETXF(p)       (GSF_LE_GET_GUINT16(p->data + 4))
#define EX_GETSTRLEN(p)   (GSF_LE_GET_GUINT16(p->data + 6))

#define EX_SETROW(p,d)    (GSF_LE_SET_GUINT16(p + 0, d))
#define EX_SETCOL(p,d)    (GSF_LE_SET_GUINT16(p + 2, d))
#define EX_SETXF(p,d)     (GSF_LE_SET_GUINT16(p + 4, d))

/* Version info types as found in various Biff records */
typedef enum { MS_BIFF_TYPE_Workbook=0, MS_BIFF_TYPE_VBModule=1, MS_BIFF_TYPE_Worksheet=2,
	       MS_BIFF_TYPE_Chart=3, MS_BIFF_TYPE_Macrosheet=4, MS_BIFF_TYPE_Workspace=5,
	       MS_BIFF_TYPE_Unknown=6 } MsBiffFileType ;


/* Cell / XF types */
typedef enum { MS_BIFF_H_VISIBLE=0, MS_BIFF_H_HIDDEN=1,
	       MS_BIFF_H_VERY_HIDDEN=2 } MsBiffHidden ;
typedef enum { MS_BIFF_X_STYLE=0, MS_BIFF_X_CELL=1 } MsBiffXfType ;
typedef enum { MS_BIFF_F_MS=0, MS_BIFF_F_LOTUS=1 } MsBiffFormat ;
typedef enum { MS_BIFF_E_CONTEXT=0, MS_BIFF_E_LEFT_TO_RIGHT=1,
	       MS_BIFF_E_RIGHT_TO_LEFT=2 } MsBiffEastern ;

typedef enum {
	MS_BIFF_F_U_NONE = 1,
	MS_BIFF_F_U_SINGLE = 2,
	MS_BIFF_F_U_DOUBLE = 3,
	MS_BIFF_F_U_SINGLE_ACC = 4,
	MS_BIFF_F_U_DOUBLE_ACC = 5
} MsBiffFontUnderline ;

typedef enum { MS_BIFF_F_S_NONE, MS_BIFF_F_S_SUB, MS_BIFF_F_S_SUPER } MsBiffFontScript ;

typedef enum {	/* Horizontal alignment */
	MS_BIFF_H_A_GENERAL = 0,
	MS_BIFF_H_A_LEFT    = 1,
	MS_BIFF_H_A_CENTER  = 2,
	MS_BIFF_H_A_RIGHT   = 3,
	MS_BIFF_H_A_FILL    = 4,
	MS_BIFF_H_A_JUSTIFTY = 5,
	MS_BIFF_H_A_CENTER_ACROSS_SELECTION = 6
} MsBiffHAlign;

typedef enum {	/* Vertical alignment */
	MS_BIFF_V_A_TOP     = 0,
	MS_BIFF_V_A_CENTER  = 1,
	MS_BIFF_V_A_BOTTOM  = 2,
	MS_BIFF_V_A_JUSTIFY = 3
} MsBiffVAlign;

typedef enum { /* Differences to parent styles */
	MS_BIFF_D_FORMAT_BIT = 10,
	MS_BIFF_D_FONT_BIT   = 11,
	MS_BIFF_D_ALIGN_BIT  = 12,
	MS_BIFF_D_BORDER_BIT = 13,
	MS_BIFF_D_FILL_BIT   = 14,
	MS_BIFF_D_LOCK_BIT   = 15
} MsBiffDifferences;

typedef struct
{
  MsBiffVersion version ;
  MsBiffFileType type ;
} MsBiffBofData ;

typedef enum {
	MsBiffMaxRowsV7 = 16384,
	MsBiffMaxRowsV8 = 65536
} MsBiffMaxRows;

extern MsBiffBofData *new_ms_biff_bof_data  (BiffQuery *pos) ;
extern void           free_ms_biff_bof_data (MsBiffBofData *data) ;

#include "biff-types.h"

#endif
