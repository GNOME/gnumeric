#ifndef GNM_EXCEL_FORMULA_TYPES_H
#define GNM_EXCEL_FORMULA_TYPES_H

/**
 * formula-types.h: The formula records
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2005 Jody Goldberg
 **/
#define FORMULA_PTG_EXPR		0x01
#define FORMULA_PTG_TBL			0x02
#define FORMULA_PTG_ADD			0x03
#define FORMULA_PTG_SUB			0x04
#define FORMULA_PTG_MULT		0x05
#define FORMULA_PTG_DIV			0x06
#define FORMULA_PTG_EXP			0x07
#define FORMULA_PTG_CONCAT		0x08
#define FORMULA_PTG_LT			0x09
#define FORMULA_PTG_LTE			0x0A
#define FORMULA_PTG_EQUAL		0x0B
#define FORMULA_PTG_GTE			0x0C
#define FORMULA_PTG_GT			0x0D
#define FORMULA_PTG_NOT_EQUAL		0x0E
#define FORMULA_PTG_INTERSECT		0x0f
#define FORMULA_PTG_UNION		0x10
#define FORMULA_PTG_RANGE		0x11
#define FORMULA_PTG_U_PLUS		0x12
#define FORMULA_PTG_U_MINUS		0x13
#define FORMULA_PTG_PERCENT		0x14
#define FORMULA_PTG_PAREN		0x15
#define FORMULA_PTG_MISSARG		0x16
#define FORMULA_PTG_STR			0x17
#define FORMULA_PTG_EXTENDED		0x18
#define FORMULA_PTG_ATTR		0x19
#define FORMULA_PTG_SHEET		0x1A	/* deprecated */
#define FORMULA_PTG_SHEET_END		0x1B	/* deprecated */
#define FORMULA_PTG_ERR			0x1C
#define FORMULA_PTG_BOOL		0x1D
#define FORMULA_PTG_INT			0x1E
#define FORMULA_PTG_NUM			0x1F /* 8 byte IEEE floating point number */

/* classed V alue, A rray, R reference */
#define FORMULA_PTG_ARRAY		0x20		/* A */
#define FORMULA_PTG_FUNC		0x21		/* depends on func */
#define FORMULA_PTG_FUNC_VAR		0x22		/* depends on func */
#define FORMULA_PTG_NAME		0x23		/* R */
#define FORMULA_PTG_REF			0x24		/* R + mapping */
#define FORMULA_PTG_AREA		0x25		/* R + mapping */
#define FORMULA_PTG_MEM_AREA		0x26		/* R + mapping */
#define FORMULA_PTG_MEM_ERR		0x27		/* R */
#define FORMULA_PTG_MEM_NO_MEM		0x28		/* R + mapping */
#define FORMULA_PTG_MEM_FUNC		0x29		/* R */
#define FORMULA_PTG_REF_ERR		0x2A		/* R */
#define FORMULA_PTG_AREA_ERR		0x2B		/* R */
#define FORMULA_PTG_REFN		0x2C		/* R, shared, conditional, validation, and for biff2-4 names */
#define FORMULA_PTG_AREAN		0x2D		/* R, shared, conditional, validation, and for biff2-4 names */
#define FORMULA_PTG_MEM_AREAN		0x2E		/* R */
#define FORMULA_PTG_NO_MEMN		0x2F		/* R */
/* nothing documented */
#define FORMULA_PTG_FUNC_CE		0x38	/* macro */
#define FORMULA_PTG_NAME_X		0x39		/* R */
#define FORMULA_PTG_REF_3D		0x3A		/* R */
#define FORMULA_PTG_AREA_3D		0x3B		/* R */
#define FORMULA_PTG_REF_ERR_3D		0x3C		/* R */
#define FORMULA_PTG_AREA_ERR_3D		0x3D		/* R */

#define FORMULA_PTG_MAX			0x7f

typedef enum {
	XL_STD		= 0,

	/* To catch the magic extension entry */
	XL_MAGIC	= 1 << 0,

	XL_VOLATILE	= 1 << 1,
	XL_XLM		= 1 << 2,
	XL_UNKNOWN	= 1 << 3
} ExcelFuncFlag;

typedef struct {
	int idx;
	char const *name;
	gint8	    min_args;
	gint8	    max_args;
	ExcelFuncFlag	 flags;
	guint8		 num_known_args;

	/* Use chars instead of XLOpType because that is easier to read
	 * and I am too lazy to make the massive edit it would take to
	 * change it. */
	char		 type;
	char const	*known_args;
} ExcelFuncDesc;

typedef struct {
	ExcelFuncDesc const *efunc;
	char *macro_name;
	int   idx;
} ExcelFunc;

extern ExcelFuncDesc const excel_func_desc[];
extern GHashTable *excel_func_by_name;
extern int excel_func_desc_size;

#endif /* GNM_EXCEL_FORMULA_TYPES_H */
