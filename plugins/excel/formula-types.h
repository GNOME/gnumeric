/**
 * formula-types.h: A long and dull list of function record types.
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998, 1999, 2000 Michael Meeks
 **/
/**
 * See S59E2B.HTM for the spec.
 **/

#define FORMULA_PTG_MAX                0x7f

#define FORMULA_PTG_EXPR               0x01

#define FORMULA_PTG_ADD                0x03
#define FORMULA_PTG_SUB                0x04
#define FORMULA_PTG_MULT               0x05
#define FORMULA_PTG_DIV                0x06
#define FORMULA_PTG_EXP                0x07
#define FORMULA_PTG_CONCAT             0x08
#define FORMULA_PTG_LT                 0x09
#define FORMULA_PTG_LTE                0x0a
#define FORMULA_PTG_EQUAL              0x0b
#define FORMULA_PTG_GTE                0x0c
#define FORMULA_PTG_GT                 0x0d
#define FORMULA_PTG_NOT_EQUAL          0x0e

#define FORMULA_PTG_U_PLUS             0x12
#define FORMULA_PTG_U_MINUS            0x13
#define FORMULA_PTG_PERCENT            0x14
#define FORMULA_PTG_PAREN              0x15
#define FORMULA_PTG_MISSARG            0x16
#define FORMULA_PTG_STR                0x17
#define FORMULA_PTG_EXTENDED           0x18
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
#define FORMULA_PTG_MEM_AREAN          0x2e
#define FORMULA_PTG_NO_MEMN            0x2f
#define FORMULA_PTG_NAME_X             0x39
#define FORMULA_PTG_REF_3D             0x3a
#define FORMULA_PTG_AREA_3D            0x3b

/*
 * Classes of Formulae Values
 * These apply mainly to references and arrays
 * Ignore for:
 *    operators
 *    simple values (integer, string ... ) [ string ? ]
 */
#define FORMULA_CLASS_REF              0x00
#define FORMULA_CLASS_VALUE            0x20
#define FORMULA_CLASS_ARRAY            0x40
