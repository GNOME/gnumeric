/*
 * ms-formula.h: MS Excel -> Gnumeric formula conversion
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 */
#ifndef GNUMERIC_MS_FORMULA_H
#define GNUMERIC_MS_FORMULA_H

#include "ms-excel.h"
#include "ms-biff.h"

void ms_excel_parse_formula (MS_EXCEL_SHEET *sheet, BIFF_QUERY *q) ;

#define FORMULA_PTG_MAX                0x7f

#define FORMULA_PTG_EXP                0x01
#define FORMULA_PTG_FUNC               0x21
#define FORMULA_PTG_FUNC_VAR           0x22
#define FORMULA_PTG_REF                0x24
#define FORMULA_PTG_AREA               0x25
#define FORMULA_PTG_MEM_AREA           0x26

typedef struct _FORMULA_OP_DATA
{
  BYTE formula_ptg ;
  char *prefix ;
  char *mid ;
  char *suffix ;
  int  precedance ;
} FORMULA_OP_DATA ;

typedef struct _FORMULA_FUNC_DATA
{
  int function_idx ;
  char *prefix ;
  char *mid ;
  char *suffix ;
  int multi_arg ;
  int precedance ;
} FORMULA_FUNC_DATA ;

#endif
