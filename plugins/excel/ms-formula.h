/*
 * ms-formula.h: MS Excel -> Gnumeric formula conversion
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 */
#ifndef GNUMERIC_MS_FORMULA_H
#define GNUMERIC_MS_FORMULA_H

#include <glib.h>

#include "ms-excel.h"
#include "ms-biff.h"

char *ms_excel_parse_formula (MS_EXCEL_SHEET *sheet, guint8 *mem,
			      int fn_col, int fn_row,
			      int shared, guint16 length) ;

/**
 * See S59E2B.HTM
 **/

#define FORMULA_PTG_MAX                0x7f

#define FORMULA_PTG_EXP                0x01
#define FORMULA_PTG_PAREN              0x15
#define FORMULA_PTG_MISSARG            0x16
#define FORMULA_PTG_STR                0x17
#define FORMULA_PTG_ATTR               0x19
#define FORMULA_PTG_ERR                0x1c
#define FORMULA_PTG_BOOL               0x1d
#define FORMULA_PTG_INT                0x1e
#define FORMULA_PTG_NUM                0x1f /* 8 byte IEEE floating point number */
#define FORMULA_PTG_ARRAY              0x20
#define FORMULA_PTG_FUNC               0x21
#define FORMULA_PTG_FUNC_VAR           0x22
#define FORMULA_PTG_NAME               0x23
#define FORMULA_PTG_REF                0x24
#define FORMULA_PTG_AREA               0x25
#define FORMULA_PTG_MEM_AREA           0x26
#define FORMULA_PTG_REFN               0x2c
#define FORMULA_PTG_AREAN              0x2d
#define FORMULA_PTG_NAME_X             0x39
#define FORMULA_PTG_REF_3D             0x3a
#define FORMULA_PTG_AREA_3D            0x3b

typedef struct _FORMULA_ARRAY_DATA
{
  int src_col, src_row, dest_col, dest_row ;
} FORMULA_ARRAY_DATA ;

typedef struct _FORMULA_OP_DATA
{
  gboolean infix ; /* ie. not unary */
  char *mid ;
  int  precedence ;
} FORMULA_OP_DATA ;

typedef struct _FORMULA_FUNC_DATA
{
	char *prefix ;
	int num_args ; /* -1 for multi-arg */
} FORMULA_FUNC_DATA ;

#endif
