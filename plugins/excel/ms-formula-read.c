/*
 * ms-formula-read.c: MS Excel -> Gnumeric formula conversion
 *
 * Authors:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2005 Jody Goldberg
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "ms-formula-read.h"
#include "excel.h"
#include "ms-biff.h"
#include "formula-types.h"
#include "boot.h"
#include <gutils.h>
#include <func.h>
#include <value.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <parse-util.h>
#include <sheet.h>
#include <workbook.h>
#include <goffice/goffice.h>
#include <gsf/gsf-utils.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "gnumeric:read_expr"
/* #define NO_DEBUG_EXCEL */
#ifndef NO_DEBUG_EXCEL
#define d(level, code)	do { if (ms_excel_formula_debug > level) { code } } while (0)
#else
#define d(level, code)
#endif

ExcelFuncDesc const excel_func_desc [] = {
	{ 0,   "COUNT",             0, 30, XL_STD, 1, 'V', "R" },
	{ 1,   "IF",                2,  3, XL_STD, 3, 'V', "VRR" },
	{ 2,   "ISNA",              1,  1, XL_STD,  1, 'V', "V" },
	{ 3,   "ISERROR",           1,  1, XL_STD,  1, 'V', "V" },
	{ 4,   "SUM",               0, 30, XL_STD, 1, 'V', "R" },
	{ 5,   "AVERAGE",           1, 30, XL_STD, 1, 'V', "R" },
	{ 6,   "MIN",               1, 30, XL_STD, 1, 'V', "R" },
	{ 7,   "MAX",               1, 30, XL_STD, 1, 'V', "R" },
	{ 8,   "ROW",               0,  1, XL_STD, 1, 'V', "R" },
	{ 9,   "COLUMN",            0,  1, XL_STD, 1, 'V', "R" },

	{ 10,  "NA",                0,  0, XL_STD,  0, 'V', NULL },
	{ 11,  "NPV",               2, 30, XL_STD, 2, 'V', "VR" },
	{ 12,  "STDEV",             1, 30, XL_STD, 1, 'V', "R" },
	{ 13,  "DOLLAR",            1,  2, XL_STD, 1, 'V', "V" },
	{ 14,  "FIXED",             2,  3, XL_STD, 3, 'V', "VVV" }, /*pre biff4 VV and fixed */
	{ 15,  "SIN",               1,  1, XL_STD,  1, 'V', "V" },
	{ 16,  "COS",               1,  1, XL_STD,  1, 'V', "V" },
	{ 17,  "TAN",               1,  1, XL_STD,  1, 'V', "V" },
	{ 18,  "ATAN",              1,  1, XL_STD,  1, 'V', "V" },
	{ 19,  "PI",                0,  0, XL_STD,  0, 'V', NULL },

	{ 20,  "SQRT",              1,  1, XL_STD,  1, 'V', "V" },
	{ 21,  "EXP",               1,  1, XL_STD,  1, 'V', "V" },
	{ 22,  "LN",                1,  1, XL_STD,  1, 'V', "V" },
	{ 23,  "LOG10",             1,  1, XL_STD,  1, 'V', "V" },
	{ 24,  "ABS",               1,  1, XL_STD,  1, 'V', "V" },
	{ 25,  "INT",               1,  1, XL_STD,  1, 'V', "V" },
	{ 26,  "SIGN",              1,  1, XL_STD,  1, 'V', "V" },
	{ 27,  "ROUND",             2,  2, XL_STD,  2, 'V', "VV" },
	{ 28,  "LOOKUP",            2,  3, XL_STD, 2, 'V', "VR" },
	{ 29,  "INDEX",             2,  4, XL_VOLATILE, 4, 'R', "RVVV" },  /* array form has only 3 */

	{ 30,  "REPT",              2,  2, XL_STD,  2, 'V', "VV" },
	{ 31,  "MID",               3,  3, XL_STD,  3, 'V', "VVV" },
	{ 32,  "LEN",               1,  1, XL_STD,  1, 'V', "V" },
	{ 33,  "VALUE",             1,  1, XL_STD,  1, 'V', "V" },
	{ 34,  "TRUE",              0,  0, XL_STD,  0, 'V', NULL },
	{ 35,  "FALSE",             0,  0, XL_STD,  0, 'V', NULL },
	{ 36,  "AND",               1, 30, XL_STD, 1, 'V', "R" },
	{ 37,  "OR",                1, 30, XL_STD, 1, 'V', "R" },
	{ 38,  "NOT",               1,  1, XL_STD,  1, 'V', "V" },
	{ 39,  "MOD",               2,  2, XL_STD,  2, 'V', "VV" },

	{ 40,  "DCOUNT",            3,  3, XL_STD,  3, 'V', "RRR" },
	{ 41,  "DSUM",              3,  3, XL_STD,  3, 'V', "RRR" },
	{ 42,  "DAVERAGE",          3,  3, XL_STD,  3, 'V', "RRR" },
	{ 43,  "DMIN",              3,  3, XL_STD,  3, 'V', "RRR" },
	{ 44,  "DMAX",              3,  3, XL_STD,  3, 'V', "RRR" },
	{ 45,  "DSTDEV",            3,  3, XL_STD,  3, 'V', "RRR" },
	{ 46,  "VAR",               1, 30, XL_STD, 1, 'V', "R" },
	{ 47,  "DVAR",              3,  3, XL_STD,  3, 'V', "RRR" },
	{ 48,  "TEXT",              2,  2, XL_STD,  2, 'V', "VV" },
	{ 49,  "LINEST",            1,  4, XL_STD, 4, 'A', "RRVV" },       /*pre biff3 RR */

	{ 50,  "TREND",             1,  4, XL_STD, 4, 'A', "RRRV" },       /*pre biff3 RRR */
	{ 51,  "LOGEST",            1,  4, XL_STD, 4, 'A', "RRVV" },       /*pre biff3 RR */
	{ 52,  "GROWTH",            1,  4, XL_STD, 4, 'A', "RRRV" },       /*pre biff3 RRR */
	{ 53,  "GOTO",             -1, -1, XL_XLM },
	{ 54,  "HALT",             -1, -1, XL_XLM },
	{ 55,  "RETURN",           -1, -1, XL_XLM },
	{ 56,  "PV",                3,  5, XL_STD, 5, 'V', "VVVVV" },      /* type is optional */
	{ 57,  "FV",                3,  5, XL_STD, 5, 'V', "VVVVV" },      /* type is optional */
	{ 58,  "NPER",              3,  5, XL_STD, 5, 'V', "VVVVV" },      /* type is optional */
	{ 59,  "PMT",               3,  5, XL_STD, 5, 'V', "VVVVV" },      /* type is optional */

	{ 60,  "RATE",              3,  6, XL_STD, 6, 'V', "VVVVVV" },     /* guess is optional */
	{ 61,  "MIRR",              3,  3, XL_STD,  3, 'V', "RVV" },
	{ 62,  "IRR",               1,  2, XL_STD, 2, 'V', "RV" }, /* guess is optional */
	{ 63,  "RAND",              0,  0, XL_VOLATILE,  0, 'V',  NULL },
	{ 64,  "MATCH",             2,  3, XL_STD, 3, 'V', "VRR" },        /* match_type is optional */
	{ 65,  "DATE",              3,  3, XL_STD,  3, 'V', "VVV" },
	{ 66,  "TIME",              3,  3, XL_STD,  3, 'V', "VVV" },
	{ 67,  "DAY",               1,  1, XL_STD,  1, 'V', "V" },
	{ 68,  "MONTH",             1,  1, XL_STD,  1, 'V', "V" },
	{ 69,  "YEAR",              1,  1, XL_STD,  1, 'V', "V" },

	{ 70,  "WEEKDAY",           1,  2, XL_STD, 2, 'V', "VV" }, /* Return type added in biff5/7 */
	{ 71,  "HOUR",              1,  1, XL_STD,  1, 'V', "V" },
	{ 72,  "MINUTE",            1,  1, XL_STD,  1, 'V', "V" },
	{ 73,  "SECOND",            1,  1, XL_STD,  1, 'V', "V" },
	{ 74,  "NOW",               0,  0, XL_VOLATILE,  0, 'V',  NULL },
	{ 75,  "AREAS",             1,  1, XL_STD,  1, 'V', "R" },
	{ 76,  "ROWS",              1,  1, XL_STD,  1, 'V', "R" },
	{ 77,  "COLUMNS",           1,  1, XL_STD,  1, 'V', "R" },
	{ 78,  "OFFSET",            3,  5, XL_STD, 5, 'R', "RVVVV" },
	{ 79,  "ABSREF",           -1, -1, XL_XLM,    2 },

	{ 80,  "RELREF",           -1, -1, XL_XLM },
	{ 81,  "ARGUMENT",         -1, -1, XL_XLM },
	{ 82,  "SEARCH",            2,  3, XL_STD, 3, 'V', "VVV" },        /* Start_num is optional */
	{ 83,  "TRANSPOSE",         1,  1, XL_STD,  1, 'A', "A" },
	{ 84,  "ERROR",            -1, -1, XL_XLM },
	{ 85,  "STEP",             -1, -1, XL_XLM },
	{ 86,  "TYPE",              1,  1, XL_STD,  1, 'V', "V" },
	{ 87,  "ECHO",             -1, -1, XL_XLM },
	{ 88,  "SETNAME",          -1, -1, XL_XLM },
	{ 89,  "CALLER",           -1, -1, XL_XLM },

	{ 90,  "DEREF",            -1, -1, XL_XLM },
	{ 91,  "WINDOWS",          -1, -1, XL_XLM },
	{ 92,  "SERIES",            4,  4, XL_STD,  4, 'V', "VVVA" },  /* See bug #572127 */
	{ 93,  "DOCUMENTS",        -1, -1, XL_XLM },
	{ 94,  "ACTIVE.CELL",      -1, -1, XL_XLM },
	{ 95,  "SELECTION",        -1, -1, XL_XLM },
	{ 96,  "RESULT",           -1, -1, XL_XLM },
	{ 97,  "ATAN2",             2,  2, XL_STD,  2, 'V', "VV" },
	{ 98,  "ASIN",              1,  1, XL_STD,  1, 'V', "V" },
	{ 99,  "ACOS",              1,  1, XL_STD,  1, 'V', "V" },

	{ 100, "CHOOSE",            2, 30, XL_STD, 2, 'V', "VR" },
	{ 101, "HLOOKUP",           3,  4, XL_STD, 4, 'V', "VRRV" },       /* pre biff5/7 it was VRR */
	{ 102, "VLOOKUP",           3,  4, XL_STD, 4, 'V', "VRRV" },       /* pre biff5/7 it was VRR */
	{ 103, "LINKS",            -1, -1, XL_XLM },
	{ 104, "INPUT",            -1, -1, XL_XLM },
	{ 105, "ISREF",             1,  1, XL_STD,  1, 'V', "R" }, /* This a guess */
	{ 106, "GET.FORMULA",       1,  1, XL_XLM,  1, 'V', "R" }, /* This is a guess (wallenbach 'function plot 2D') */
	{ 107, "GET.NAME",          1,  1, XL_XLM,  1, 'V', "R" }, /* This is a guess */
	{ 108, "SET.VALUE",        -1, -1, XL_XLM },
	{ 109, "LOG",               1,  2, XL_STD, 2, 'V', "VV" }, /* Base is optional */

	{ 110, "EXEC",             -1, -1, XL_XLM },
	{ 111, "CHAR",              1,  1, XL_STD,  1, 'V', "V" },
	{ 112, "LOWER",             1,  1, XL_STD,  1, 'V', "V" },
	{ 113, "UPPER",             1,  1, XL_STD,  1, 'V', "V" },
	{ 114, "PROPER",            1,  1, XL_STD,  1, 'V', "V" },
	{ 115, "LEFT",              1,  2, XL_STD, 2, 'V', "VV" },    /* Num_chars is optional */
	{ 116, "RIGHT",             1,  2, XL_STD, 2, 'V', "VV" },    /* Num_chars is optional */
	{ 117, "EXACT",             2,  2, XL_STD,  2, 'V', "VV" },
	{ 118, "TRIM",              1,  1, XL_STD,  1, 'V', "V" },
	{ 119, "REPLACE",           4,  4, XL_STD,  4, 'V', "VVVV" },

	{ 120, "SUBSTITUTE",        3,  4, XL_STD, 4, 'V', "VVVV" },    /* Instance num is optional */
	{ 121, "CODE",              1,  1, XL_STD,  1, 'V', "V" },
	{ 122, "NAMES",            -1, -1, XL_XLM },
	{ 123, "DIRECTORY",        -1, -1, XL_XLM },
	{ 124, "FIND",              2,  3, XL_STD, 3, 'V', "VVV" },/* start_num is optional */
	{ 125, "CELL",              1,  2, XL_VOLATILE, 2, 'V', "VR" },
	{ 126, "ISERR",             1,  1, XL_STD,  1, 'V', "V" },
	{ 127, "ISTEXT",            1,  1, XL_STD,  1, 'V', "V" },
	{ 128, "ISNUMBER",          1,  1, XL_STD,  1, 'V', "V" },
	{ 129, "ISBLANK",           1,  1, XL_STD,  1, 'V', "V" },

	{ 130, "T",                 1,  1, XL_STD,  1, 'V', "R" },
	{ 131, "N",                 1,  1, XL_STD,  1, 'V', "R" },
	{ 132, "FOPEN",            -1, -1, XL_XLM },
	{ 133, "FCLOSE",           -1, -1, XL_XLM },
	{ 134, "FSIZE",            -1, -1, XL_XLM },
	{ 135, "FREADLN",          -1, -1, XL_XLM },
	{ 136, "FREAD",            -1, -1, XL_XLM },
	{ 137, "FWRITELN",         -1, -1, XL_XLM },
	{ 138, "FWRITE",           -1, -1, XL_XLM },
	{ 139, "FPOS",             -1, -1, XL_XLM },

	{ 140, "DATEVALUE",         1,  1, XL_STD,  1, 'V', "V" },
	{ 141, "TIMEVALUE",         1,  1, XL_STD,  1, 'V', "V" },
	{ 142, "SLN",               3,  3, XL_STD,  3, 'V', "VVV" },
	{ 143, "SYD",               4,  4, XL_STD,  4, 'V', "VVVV" },
	{ 144, "DDB",               4,  5, XL_STD, 5, 'V', "VVVVV" },      /* Factor is optional */
	{ 145, "GET.DEF",          -1, -1, XL_XLM },
	{ 146, "REFTEXT",          -1, -1, XL_XLM },
	{ 147, "TEXTREF",          -1, -1, XL_XLM },
	{ 148, "INDIRECT",          1,  2, XL_VOLATILE, 2, 'R', "VV" },    /* ai is optional */
	{ 149, "REGISTER",         -1, -1, XL_XLM },

	{ 150, "CALL",             -1, -1, XL_XLM },
	{ 151, "ADD.BAR",          -1, -1, XL_XLM },
	{ 152, "ADD.MENU",         -1, -1, XL_XLM },
	{ 153, "ADD.COMMAND",      -1, -1, XL_XLM },
	{ 154, "ENABLE.COMMAND",   -1, -1, XL_XLM },
	{ 155, "CHECK.COMMAND",    -1, -1, XL_XLM },
	{ 156, "RENAME.COMMAND",   -1, -1, XL_XLM },
	{ 157, "SHOW.BAR",         -1, -1, XL_XLM },
	{ 158, "DELETE.MENU",      -1, -1, XL_XLM },
	{ 159, "DELETE.COMMAND",   -1, -1, XL_XLM },

	{ 160, "GET.CHART.ITEM",   -1, -1, XL_XLM },
	{ 161, "DIALOG.BOX",       -1, -1, XL_XLM },
	{ 162, "CLEAN",             1,  1, XL_STD,  1, 'V', "V" },
	{ 163, "MDETERM",           1,  1, XL_STD,  1, 'V', "A" },
	{ 164, "MINVERSE",          1,  1, XL_STD,  1, 'A', "A" },
	{ 165, "MMULT",             2,  2, XL_STD,  2, 'A', "AA" },
	{ 166, "FILES",            -1, -1, XL_XLM },
	{ 167, "IPMT",              4,  6, XL_STD, 6, 'V', "VVVVVV" },     /* Type is optional */
	{ 168, "PPMT",              4,  6, XL_STD, 6, 'V', "VVVVVV" },
	{ 169, "COUNTA",            0, 30, XL_STD, 1, 'V', "R" },

	{ 170, "CANCELKEY",        -1, -1, XL_XLM },
	{ 171, "FOR",              -1, -1, XL_XLM },
	{ 172, "WHILE",            -1, -1, XL_XLM },
	{ 173, "BREAK",            -1, -1, XL_XLM },
	{ 174, "NEXT",             -1, -1, XL_XLM },
	{ 175, "INITIATE",         -1, -1, XL_XLM },
	{ 176, "REQUEST",          -1, -1, XL_XLM },
	{ 177, "POKE",             -1, -1, XL_XLM },
	{ 178, "EXECUTE",          -1, -1, XL_XLM },
	{ 179, "TERMINATE",        -1, -1, XL_XLM },

	{ 180, "RESTART",          -1, -1, XL_XLM },
	{ 181, "HELP",             -1, -1, XL_XLM },
	{ 182, "GET.BAR",          -1, -1, XL_XLM },
	{ 183, "PRODUCT",           0, 30, XL_STD, 1, 'V', "R" },
	{ 184, "FACT",              1,  1, XL_STD,  1, 'V', "V" },
	{ 185, "GET.CELL",         -1, -1, XL_XLM },
	{ 186, "GET.WORKSPACE",    -1, -1, XL_XLM },
	{ 187, "GET.WINDOW",       -1, -1, XL_XLM },
	{ 188, "GET.DOCUMENT",     -1, -1, XL_XLM },
	{ 189, "DPRODUCT",          3,  3, XL_STD,  3, 'V', "RRR" },

	{ 190, "ISNONTEXT",         1,  1, XL_STD,  1, 'V', "V" },
	{ 191, "GET.NOTE",         -1, -1, XL_XLM },
	{ 192, "NOTE",             -1, -1, XL_XLM },
	{ 193, "STDEVP",            1, 30, XL_STD, 1, 'V', "R" },
	{ 194, "VARP",              1, 30, XL_STD, 1, 'V', "R" },
	{ 195, "DSTDEVP",           3,  3, XL_STD,  3, 'V', "RRR" },
	{ 196, "DVARP",             3,  3, XL_STD,  3, 'V', "RRR" },
	{ 197, "TRUNC",             1,  2, XL_STD, 2, 'V', "VV" },      /* pre-biff3 it was 'V' */
	{ 198, "ISLOGICAL",         1,  1, XL_STD,  1, 'V', "V" },
	{ 199, "DCOUNTA",           3,  3, XL_STD,  3, 'V', "RRR" },

	{ 200, "DELETE.BAR",       -1, -1, XL_XLM },
	{ 201, "UNREGISTER",       -1, -1, XL_XLM },
	{ 202, "EXCELFUNC202",     -1, -1, XL_UNKNOWN },
	{ 203, "EXCELFUNC203",     -1, -1, XL_UNKNOWN },
	{ 204, "USDOLLAR",          1,  2, XL_STD, 2, 'V', "VV" },
	{ 205, "FINDB",             2,  3, XL_STD, 3, 'V', "VVV" },        /* start_num is optional */
	{ 206, "SEARCHB",           2,  3, XL_STD, 3, 'V', "VVV" },        /* Start_num is optional */
	{ 207, "REPLACEB",          4,  4, XL_STD,  4, 'V', "VVVV" },
	{ 208, "LEFTB",             1,  2, XL_STD, 2, 'V', "VV" }, /* Num_chars is optional */
	{ 209, "RIGHTB",            1,  2, XL_STD, 2, 'V', "VV" }, /* Num_chars is optional */

	{ 210, "MIDB",              3,  3, XL_STD,  3, 'V', "VVV" },
	{ 211, "LENB",              1,  1, XL_STD,  1, 'V', "V" },
	{ 212, "ROUNDUP",           2,  2, XL_STD,  2, 'V', "VV" },
	{ 213, "ROUNDDOWN",         2,  2, XL_STD,  2, 'V', "VV" },
	{ 214, "ASC",               1,  1, XL_STD,  1, 'V', "V" },
	{ 215, "DBCS",              1,  1, XL_STD,  1, 'V', "V" },
	{ 216, "RANK",              2,  3, XL_STD, 3, 'V', "VRV" },        /* OOo claims added in biff4 */
	{ 217, "EXCELFUNC217",     -1, -1, XL_UNKNOWN },
	{ 218, "EXCELFUNC218",     -1, -1, XL_UNKNOWN },

/* added in biff3 */
	{ 219, "ADDRESS",           2,  5, XL_STD, 5, 'V', "VVVVV" },

	{ 220, "DAYS360",           2,  3, XL_STD, 3, 'V', "VVV" },        /* pre-biff5/7 VV */
	{ 221, "TODAY",             0,  0, XL_VOLATILE,  0, 'V', NULL },
	{ 222, "VDB",               5,  7, XL_STD, 7, 'V', "VVVVVVV" },
	{ 223, "ELSE",             -1, -1, XL_XLM },
	{ 224, "ELSE.IF",          -1, -1, XL_XLM },
	{ 225, "END.IF",           -1, -1, XL_XLM },
	{ 226, "FOR.CELL",         -1, -1, XL_XLM },
	{ 227, "MEDIAN",            1, 30, XL_STD, 1, 'V', "R" },
	{ 228, "SUMPRODUCT",        1, 30, XL_STD, 1, 'V', "A" },
	{ 229, "SINH",              1,  1, XL_STD,  1, 'V', "V" },

	{ 230, "COSH",              1,  1, XL_STD,  1, 'V', "V" },
	{ 231, "TANH",              1,  1, XL_STD,  1, 'V', "V" },
	{ 232, "ASINH",             1,  1, XL_STD,  1, 'V', "V" },
	{ 233, "ACOSH",             1,  1, XL_STD,  1, 'V', "V" },
	{ 234, "ATANH",             1,  1, XL_STD,  1, 'V', "V" },
	{ 235, "DGET",              3,  3, XL_STD,  3, 'V', "RRR" },
	{ 236, "CREATE.OBJECT",    -1, -1, XL_XLM },
	{ 237, "VOLATILE",         -1, -1, XL_XLM },
	{ 238, "LAST.ERROR",       -1, -1, XL_XLM },
	{ 239, "CUSTOM.UNDO",      -1, -1, XL_XLM },

	{ 240, "CUSTOM.REPEAT",    -1, -1, XL_XLM },
	{ 241, "FORMULA.CONVERT",  -1, -1, XL_XLM },
	{ 242, "GET.LINK.INFO",    -1, -1, XL_XLM },
	{ 243, "TEXT.BOX",         -1, -1, XL_XLM },
	{ 244, "INFO",              1,  1, XL_STD, 1, 'V', "V" },
	{ 245, "GROUP",            -1, -1, XL_XLM },
	{ 246, "GET.OBJECT",       -1, -1, XL_XLM },

/* added in biff4 */
	{ 247, "DB",                4,  5, XL_STD, 5, 'V',"VVVVV" },       /* month is optional */
	{ 248, "PAUSE",            -1, -1, XL_XLM },
	{ 249, "EXCELFUNC249",     -1, -1, XL_UNKNOWN },

	{ 250, "EXCELFUNC250",     -1, -1, XL_UNKNOWN },
	{ 251, "RESUME",           -1, -1, XL_XLM },
	{ 252, "FREQUENCY",         2,  2, XL_STD,  2, 'A',"RR" },
	{ 253, "ADD.TOOLBAR",      -1, -1, XL_XLM },
	{ 254, "DELETE.TOOLBAR",   -1, -1, XL_XLM },
	{ 255, "extension slot",   -1, -1, XL_MAGIC },
	{ 256, "RESET.TOOLBAR",    -1, -1, XL_XLM },
	{ 257, "EVALUATE",         -1, -1, XL_XLM },
	{ 258, "GET.TOOLBAR",      -1, -1, XL_XLM },
	{ 259, "GET.TOOL",         -1, -1, XL_XLM },

	{ 260, "SPELLING.CHECK",   -1, -1, XL_XLM },
	{ 261, "ERROR.TYPE",        1,  1, XL_STD,  1, 'V', "V" },
	{ 262, "APP.TITLE",        -1, -1, XL_XLM },
	{ 263, "WINDOW.TITLE",     -1, -1, XL_XLM },
	{ 264, "SAVE.TOOLBAR",     -1, -1, XL_XLM },
	{ 265, "ENABLE.TOOL",      -1, -1, XL_XLM },
	{ 266, "PRESS.TOOL",       -1, -1, XL_XLM },
	{ 267, "REGISTER.ID",      -1, -1, XL_XLM },
	{ 268, "GET.WORKBOOK",     -1, -1, XL_XLM },
	{ 269, "AVEDEV",            1, 30, XL_STD, 1, 'V', "R" },

	{ 270, "BETADIST",          3,  5, XL_STD, 1, 'V', "V" },
	{ 271, "GAMMALN",           1,  1, XL_STD,  1, 'V', "V" },
	{ 272, "BETAINV",           3,  5, XL_STD, 1, 'V', "V" },
	{ 273, "BINOMDIST",         4,  4, XL_STD,  4, 'V', "VVVV" },
	{ 274, "CHIDIST",           2,  2, XL_STD,  2, 'V', "VV" },
	{ 275, "CHIINV",            2,  2, XL_STD,  2, 'V', "VV" },
	{ 276, "COMBIN",            2,  2, XL_STD,  2, 'V', "VV" },
	{ 277, "CONFIDENCE",        3,  3, XL_STD,  3, 'V', "VVV" },
	{ 278, "CRITBINOM",         3,  3, XL_STD,  3, 'V', "VVV" },
	{ 279, "EVEN",              1,  1, XL_STD,  1, 'V', "V" },

	{ 280, "EXPONDIST",         3,  3, XL_STD,  3, 'V', "VVV" },
	{ 281, "FDIST",             3,  3, XL_STD,  3, 'V', "VVV" },
	{ 282, "FINV",              3,  3, XL_STD,  3, 'V', "VVV" },
	{ 283, "FISHER",            1,  1, XL_STD,  1, 'V', "V" },
	{ 284, "FISHERINV",         1,  1, XL_STD,  1, 'V', "V" },
	{ 285, "FLOOR",             2,  2, XL_STD,  2, 'V', "VV" },
	{ 286, "GAMMADIST",         4,  4, XL_STD,  4, 'V', "VVVV" },
	{ 287, "GAMMAINV",          3,  3, XL_STD,  3, 'V', "VVV" },
	{ 288, "CEILING",           2,  2, XL_STD,  2, 'V', "VV" },
	{ 289, "HYPGEOMDIST",       4,  4, XL_STD,  4, 'V', "VVVV" },

	{ 290, "LOGNORMDIST",       3,  3, XL_STD,  3, 'V', "VVV" },
	{ 291, "LOGINV",            3,  3, XL_STD,  3, 'V', "VVV" },
	{ 292, "NEGBINOMDIST",      3,  3, XL_STD,  3, 'V', "VVV" },
	{ 293, "NORMDIST",          4,  4, XL_STD,  4, 'V', "VVVV" },
	{ 294, "NORMSDIST",         1,  1, XL_STD,  1, 'V', "V" },
	{ 295, "NORMINV",           3,  3, XL_STD,  3, 'V', "VVV" },
	{ 296, "NORMSINV",          1,  1, XL_STD,  1, 'V', "V" },
	{ 297, "STANDARDIZE",       3,  3, XL_STD,  3, 'V', "VVV" },
	{ 298, "ODD",               1,  1, XL_STD,  1, 'V', "V" },
	{ 299, "PERMUT",            2,  2, XL_STD,  2, 'V', "VV" },

	{ 300, "POISSON",           3,  3, XL_STD,  3, 'V', "VVV" },
	{ 301, "TDIST",             3,  3, XL_STD,  3, 'V', "VVV" },
	{ 302, "WEIBULL",           4,  4, XL_STD,  4, 'V', "VVVV" },
	{ 303, "SUMXMY2",           2,  2, XL_STD,  2, 'V', "AA" },
	{ 304, "SUMX2MY2",          2,  2, XL_STD,  2, 'V', "AA" },
	{ 305, "SUMX2PY2",          2,  2, XL_STD,  2, 'V', "AA" },
	{ 306, "CHITEST",           2,  2, XL_STD,  2, 'V', "AA" },
	{ 307, "CORREL",            2,  2, XL_STD,  2, 'V', "AA" },
	{ 308, "COVAR",             2,  2, XL_STD,  2, 'V', "AA" },
	{ 309, "FORECAST",          3,  3, XL_STD,  3, 'V', "VAA" },

	{ 310, "FTEST",             2,  2, XL_STD,  2, 'V', "AA" },
	{ 311, "INTERCEPT",         2,  2, XL_STD,  2, 'V', "AA" },
	{ 312, "PEARSON",           2,  2, XL_STD,  2, 'V', "AA" },
	{ 313, "RSQ",               2,  2, XL_STD,  2, 'V', "AA" },
	{ 314, "STEYX",             2,  2, XL_STD,  2, 'V', "AA" },
	{ 315, "SLOPE",             2,  2, XL_STD,  2, 'V', "AA" },
	{ 316, "TTEST",             4,  4, XL_STD,  4, 'V', "AAVV" },
	{ 317, "PROB",              3,  4, XL_STD, 3, 'V', "AAV" },        /* upper_limit is optional */
	{ 318, "DEVSQ",             1, 30, XL_STD, 1, 'V', "R" },
	{ 319, "GEOMEAN",           1, 30, XL_STD, 1, 'V', "R" },

	{ 320, "HARMEAN",           1, 30, XL_STD, 1, 'V', "R" },
	{ 321, "SUMSQ",             0, 30, XL_STD, 1, 'V', "R" },
	{ 322, "KURT",              1, 30, XL_STD, 1, 'V', "R" },
	{ 323, "SKEW",              1, 30, XL_STD, 1, 'V', "R" },
	{ 324, "ZTEST",             2,  3, XL_STD, 2, 'V', "RV" }, /* sigma is optional */
	{ 325, "LARGE",             2,  2, XL_STD,  2, 'V', "RV" },
	{ 326, "SMALL",             2,  2, XL_STD,  2, 'V', "RV" },
	{ 327, "QUARTILE",          2,  2, XL_STD,  2, 'V', "RV" },
	{ 328, "PERCENTILE",        2,  2, XL_STD,  2, 'V', "RV" },
	{ 329, "PERCENTRANK",       2,  3, XL_STD, 2, 'V', "RV" }, /* Significance is optional */

	{ 330, "MODE",              1, 30, XL_STD, 1, 'V', "A" },
	{ 331, "TRIMMEAN",          2,  2, XL_STD,  2, 'V', "RV" },
	{ 332, "TINV",              2,  2, XL_STD,  2, 'V', "VV" },
	{ 333, "EXCELFUNC333",     -1, -1, XL_UNKNOWN },
	{ 334, "MOVIE.COMMAND",    -1, -1, XL_XLM },
	{ 335, "GET.MOVIE",        -1, -1, XL_XLM },

/* Added in biff5/7 */
	{ 336, "CONCATENATE",       0, 30, XL_STD, 1, 'V', "V" },
	{ 337, "POWER",             2,  2, XL_STD,  2, 'V', "VV" },
	{ 338, "PIVOT.ADD.DATA",   -1, -1, XL_XLM },
	{ 339, "GET.PIVOT.TABLE",  -1, -1, XL_XLM },

	{ 340, "GET.PIVOT.FIELD",  -1, -1, XL_XLM },
	{ 341, "GET.PIVOT.ITEM",   -1, -1, XL_XLM },
	{ 342, "RADIANS",           1,  1, XL_STD,  1, 'V', "V" },
	{ 343, "DEGREES",           1,  1, XL_STD,  1, 'V', "V" },
	{ 344, "SUBTOTAL",          2, 30, XL_STD, 2, 'V', "VR" },
	{ 345, "SUMIF",             2,  3, XL_STD, 3, 'V', "RVR" },        /* Actual range is optional */
	{ 346, "COUNTIF",           2,  2, XL_STD,  2, 'V', "RV" },
	{ 347, "COUNTBLANK",        1,  1, XL_STD,  1, 'V', "R" },
	{ 348, "SCENARIO.GET",     -1, -1, XL_XLM },
	{ 349, "OPTIONS.LISTS.GET",-1, -1, XL_XLM },

	{ 350, "ISPMT",             4,  4, XL_STD,  4, 'V', "VVVV" },
	{ 351, "DATEDIF",           3,  3, XL_STD,  3, 'V', "VVV" },
	{ 352, "DATESTRING",        1,  1, XL_STD,  1, 'V', "V" },
	{ 353, "NUMBERSTRING",      2,  2, XL_STD,  2, 'V', "VV" },
	{ 354, "ROMAN",             1,  2, XL_STD, 2, 'V', "VV" },
	{ 355, "OPEN.DIALOG",      -1, -1, XL_XLM },
	{ 356, "SAVE.DIALOG",      -1, -1, XL_XLM },
	{ 357, "VIEW.GET",         -1, -1, XL_XLM },
	{ 358, "GETPIVOTDATA",      2, 30, XL_STD, 2, 'V', "RVV" },        /* changed in biff8 */

/* Added in Biff8 */
	{ 359, "HYPERLINK",         1,  2, XL_STD, 2, 'V', "VV" }, /* cell_contents is optional */

	{ 360, "PHONETIC",          1,  1, XL_STD,  1, 'V', "V" },
	{ 361, "AVERAGEA",          1, 30, XL_STD, 1, 'V', "R" },
	{ 362, "MAXA",              1, 30, XL_STD, 1, 'V', "R" },
	{ 363, "MINA",              1, 30, XL_STD, 1, 'V', "R" },
	{ 364, "STDEVPA",           1, 30, XL_STD, 1, 'V', "R" },
	{ 365, "VARPA",             1, 30, XL_STD, 1, 'V', "R" },
	{ 366, "STDEVA",            1, 30, XL_STD, 1, 'V', "R" },
	{ 367, "VARA",              1, 30, XL_STD, 1, 'V', "R" },

/* New in XP ? imports strangely */
	{ 368, "BAHTTEXT",          1,  1, XL_STD, 1, 'V', "V" },
	{ 369, "THAIDAYOFWEEK",     1,  1, XL_STD, 1, 'V', "V" },

	{ 370, "THAIDIGIT",         1,  1, XL_STD, 1, 'V', "V" },
	{ 371, "THAIMONTHOFYEAR",   1,  1, XL_STD, 1, 'V', "V" },
	{ 372, "THAINUMSOUND",      1,  1, XL_STD, 1, 'V', "V" },
	{ 373, "THAINUMSTRING",     1,  1, XL_STD, 1, 'V', "V" },
	{ 374, "THAISTRINGLENGTH",  1,  1, XL_STD, 1, 'V', "V" },
	{ 375, "ISTHAIDIGIT",       1,  1, XL_STD, 1, 'V', "V" },
	{ 376, "ROUNDBAHTDOWN",     1,  1, XL_STD, 1, 'V', "V" },
	{ 377, "ROUNDBAHTUP",       1,  1, XL_STD, 1, 'V', "V" },
	{ 378, "THAIYEAR",          1,  1, XL_STD, 1, 'V', "V" },
	{ 379, "RTD",               2,  5, XL_STD, 1, 'V', "V" },
	{ 380, "ISHYPERLINK",       1,  1, XL_STD, 1, 'V', "V" }
};
int excel_func_desc_size = G_N_ELEMENTS (excel_func_desc);
GHashTable *excel_func_by_name = NULL;

static GnmFunc *
xl2010_synonyms (const char *name)
{
	Workbook *wb = NULL;
	static const struct {
		const char *newname;
		const char *oldname;
	} names[] = {
		{ "beta.inv", "betainv" },
		{ "binom.dist", "binomdist" },
		{ "chisq.dist.rt", "chidist" },
		{ "chisq.inv", "r.qchisq" },
		{ "chisq.inv.rt", "chiinv" },
		{ "chisq.test", "chitest" },
		{ "confidence.norm", "confidence" },
		{ "covariance.p", "covar" },
		{ "expon.dist", "expondist" },
		{ "f.dist.rt", "fdist" },
		{ "f.inv", "r.qf" },
		{ "f.inv.rt", "finv" },
		{ "f.test", "ftest" },
		{ "gamma.dist", "gammadist" },
		{ "gamma.inv", "gammainv" },
		{ "hypgeom.dist", "hypgeomdist" },
		{ "lognorm.inv", "loginv" },
		{ "mode.sngl", "mode" },
		{ "norm.dist", "normdist" },
		{ "norm.inv", "norminv" },
		{ "norm.s.inv", "normsinv" },
		{ "percentile.inc", "percentile" },
		{ "percentrank.inc", "percentrank" },
		{ "poisson.dist", "poisson" },
		{ "quartile.inc", "quartile" },
		{ "rank.eq", "rank" },
		{ "stdev.p", "stdevp" },
		{ "stdev.s", "stdev" },
		{ "t.inv.2t", "tinv" },
		{ "t.test", "ttest" },
		{ "var.p", "varp" },
		{ "var.s", "var" },
		{ "weibull.dist", "weibull" },
		{ "z.test", "ztest" }
	};
	unsigned ui;

	for (ui = 0; ui < G_N_ELEMENTS (names); ui++)
		if (g_ascii_strcasecmp (name, names[ui].newname) == 0)
			return gnm_func_lookup (names[ui].oldname, wb);

	return NULL;
}


static GnmExpr const *
xl_expr_err (ExcelReadSheet const *esheet, int col, int row,
	     char const *msg, char const *str)
{
	if (esheet != NULL && esheet->sheet != NULL) {
		g_warning ("%s!%s : %s",
			   esheet->sheet->name_unquoted,
			   cell_coord_name (col, row), msg);
	} else if (col >= 0 && row >= 0) {
		g_warning ("%s : %s", cell_coord_name (col, row), msg);
	} else {
		g_warning ("%s", msg);
	}

	return gnm_expr_new_constant (value_new_error (NULL, str));
}

/**
 *  A useful routine for extracting data from a common
 * storage structure.
 **/
static void
getRefV7 (GnmCellRef *cr,
	  guint8 col, guint16 gbitrw, int curcol, int currow,
	  gboolean const shared)
{
	guint16 const row = (guint16)(gbitrw & 0x3fff);

	d (2, g_printerr ("7In : 0x%x, 0x%x  at %s%s\n", col, gbitrw,
		      cell_coord_name (curcol, currow), (shared?" (shared)":"")););

	cr->sheet = NULL;

	cr->row_relative = (gbitrw & 0x8000) != 0;
	if (cr->row_relative) {
		if (shared) {
			/* ICK ! XL is storing signed numbers without storing
			 * the sign bit.  we need to assume that if the 13th
			 * bit is set it is meant to be negative.  then we
			 * reinstate the sign bit and allow compiler to handle
			 * sign extension.
			 */
			if (row & 0x2000)
				cr->row = (gint16)(row | 0xc000);
			else
				cr->row = row;
		} else
			cr->row = row - currow;
	} else {
		/* By construction this cannot exceed 0x3fff. */
		cr->row = row;
	}

	cr->col_relative = (gbitrw & 0x4000) != 0;
	if (cr->col_relative) {
		if (shared)
			cr->col = (gint8)col;
		else
			cr->col = col - curcol;
	} else {
		/* By construction this cannot exceed 0xff. */
		cr->col = col;
	}
}

/**
 * A useful routine for extracting data from a common storage structure.
 **/
static void
getRefV8 (GnmCellRef *cr,
	  guint16 row, guint16 gbitcl, int curcol, int currow,
	  gboolean const shared, GnmSheetSize const *ss)
{
	guint8 const col = (guint8)(gbitcl & 0xff);

	d (2, g_printerr ("8In : 0x%x, 0x%x  at %s%s\n", row, gbitcl,
		      cell_coord_name (curcol, currow), (shared?" (shared)":"")););

	cr->sheet = NULL;

	cr->row_relative = (gbitcl & 0x8000) != 0;
	if (cr->row_relative) {
		if (shared)
			cr->row = (gint16)row;
		else
			cr->row = row - currow;
	} else {
		cr->row = row;
		if (row >= ss->max_rows) {
			g_warning ("Row too big: %d", row);
			cr->row = ss->max_rows - 1;
		}
	}

	cr->col_relative = (gbitcl & 0x4000) != 0;
	if (cr->col_relative) {
		if (shared)
			cr->col = (gint8)col;
		else
			cr->col = col - curcol;
	} else {
		/* By construction this cannot exceed 0xff. */
		cr->col = col;
	}
}

static void
parse_list_push (GnmExprList **list, GnmExpr const *pd)
{
	d (5, g_printerr ("Push 0x%p\n", (void *)pd););
	if (pd == NULL) {
		g_warning ("FIXME: Pushing nothing onto excel function stack");
		pd = xl_expr_err (NULL, -1, -1,
			"Incorrect number of parsed formula arguments",
			"#WrongArgs!");
	}
	*list = gnm_expr_list_prepend (*list, pd);
}

static void
parse_list_push_raw (GnmExprList **list, GnmValue *v)
{
	parse_list_push (list, gnm_expr_new_constant (v));
}

static GnmExpr const *
parse_list_pop (GnmExprList **list)
{
	/* Get the head */
	GnmExprList *tmp = *list;
	if (tmp != NULL) {
		GnmExpr const *ans = tmp->data;
		*list = g_slist_remove (*list, ans);
		d (5, g_printerr ("Pop 0x%p\n", (void *)ans););
		return ans;
	}

	return xl_expr_err (NULL, -1, -1,
		"Incorrect number of parsed formula arguments",
		"#WrongArgs!");
}

/**
 * Returns a new list composed of the last n items pop'd off the list.
 **/
static GnmExprList *
parse_list_last_n (GnmExprList **list, gint n)
{
	GnmExprList *l = NULL;
	while (n-->0)
		l = gnm_expr_list_prepend (l, parse_list_pop(list));
	return l;
}


static void
parse_list_free (GnmExprList **list)
{
	while (*list)
		gnm_expr_free (parse_list_pop(list));
}

static gboolean
make_function (GnmExprList **stack, int fn_idx, int numargs, Workbook *wb)
{
	GnmFunc *name = NULL;

	if (fn_idx == 0xff) {
		/*
		 * This is undocumented.
		 * function 0xff seems to indicate an external function whose
		 * name should be on top of the stack.
		 */
		/* FIXME FIXME FIXME : How to handle missing trailing args ?? */
		GnmExprList *args = parse_list_last_n (stack, numargs-1);
		GnmExpr const *tmp = parse_list_pop (stack);
		char const *f_name = NULL;

		if (tmp != NULL) {
			if (GNM_EXPR_GET_OPER (tmp) == GNM_EXPR_OP_CONSTANT &&
			    VALUE_IS_STRING (tmp->constant.value))
				f_name = value_peek_string (tmp->constant.value);
			else if (GNM_EXPR_GET_OPER (tmp) == GNM_EXPR_OP_NAME)
				f_name = expr_name_name (tmp->name.name);
		}

		if (f_name == NULL) {
			if (tmp)
				gnm_expr_free (tmp);
			parse_list_free (&args);
			parse_list_push_raw (stack,
				value_new_error (NULL, _("Broken function")));
			g_warning ("So much for that theory.");
			return FALSE;
		}

		name = NULL;

		if (g_str_has_prefix (f_name, "_xlfn.")) {
			name = gnm_func_lookup (f_name + 6, wb);
			if (name)
				/* This happens for IFERROR, for example. */
				f_name += 6;
			else
				name = xl2010_synonyms (f_name + 6);
		} else if (g_str_has_prefix (f_name, "_xlfnodf.") &&
			   (name = gnm_func_lookup (f_name + 9, wb))) {
			/* This happens for GAMMA, for example. */
			f_name += 9;
		}

		if (!name)
			name = gnm_func_lookup (f_name, wb);
		d (2, g_printerr ("Function '%s' of %d args\n",
			       f_name, numargs););

		if (name == NULL)
			name = gnm_func_add_placeholder (wb, f_name, "UNKNOWN");

		gnm_expr_free (tmp);
		parse_list_push (stack, gnm_expr_new_funcall (name, args));
		return TRUE;
	} else if (fn_idx >= 0 && fn_idx < excel_func_desc_size) {
		ExcelFuncDesc const *fd = excel_func_desc + fn_idx;
		GnmExprList *args;

		d (2, g_printerr ("Function '%s', %d, max args: %d flags = 0x%x\n",
			       fd->name, numargs, fd->max_args, fd->flags););

		if (numargs < 0) { /* fixed, use the built in */
			int const available_args =
				(*stack != NULL) ? g_slist_length (*stack) : 0;
			/* handle missing trailing arguments */
			numargs = fd->max_args;
			if (numargs > available_args)
				numargs = available_args;
		}

		if (fd->flags & XL_UNKNOWN)
			g_warning("This sheet uses an Excel function "
				  "('%s') for which we do \n"
				  "not have adequate documentation.  "
				  "Please forward a copy (if possible) to\n"
				  "gnumeric-list@gnome.org.  Thanks",
				  fd->name);

		args = parse_list_last_n (stack, numargs);
		if (fd->name) {
			name = gnm_func_lookup (fd->name, wb);
			if (name == NULL)
				name = gnm_func_add_placeholder (wb, fd->name, "UNKNOWN");
		}
		/* This should not happen */
		if (!name) {
			char *txt;
			txt = g_strdup_printf ("[Function '%s']",
					       fd->name ? fd->name : "?");
			g_warning ("Unknown %s", txt);
			parse_list_push_raw (stack, value_new_error (NULL, txt));
			g_free (txt);

			parse_list_free (&args);
			return FALSE;
		}
		parse_list_push (stack, gnm_expr_new_funcall (name, args));
		return TRUE;
	} else
		g_warning ("FIXME, unimplemented fn 0x%x, with %d args",
			fn_idx, numargs);
	return FALSE;
}

static int
is_string_concats (GnmExpr const *e, GString *accum)
{
	GnmValue const *v = gnm_expr_get_constant (e);
	if (v && VALUE_IS_STRING (v)) {
		if (accum)
			g_string_append (accum, value_peek_string (v));
		return 1;
	}

	if (GNM_EXPR_GET_OPER(e) == GNM_EXPR_OP_CAT) {
		int l, r;

		l = is_string_concats (e->binary.value_a, accum);
		if (!l)
			return 0;

		r = is_string_concats (e->binary.value_b, accum);
		if (!r)
			return 0;

		return l + r;
	}

	return 0;
}

static GnmExpr const *
undo_save_hacks (GnmExpr const *e)
{
	if (GNM_EXPR_GET_OPER(e) == GNM_EXPR_OP_PAREN &&
	    is_string_concats (e->unary.value, NULL) >= 2) {
		GString *accum = g_string_new (NULL);
		(void)is_string_concats (e->unary.value, accum);
		gnm_expr_free (e);
		return gnm_expr_new_constant (value_new_string_nocopy (g_string_free (accum, FALSE)));
	}

	return e;
}


/**
 * ms_excel_dump_cellname: internal utility to dump the current location safely.
 */
static void
ms_excel_dump_cellname (GnmXLImporter const *importer, ExcelReadSheet const *esheet,
			int fn_col, int fn_row)
{
	if (esheet && esheet->sheet && esheet->sheet->name_unquoted)
		g_printerr ("%s!", esheet->sheet->name_unquoted);
	else if (importer && importer->wb && go_doc_get_uri (GO_DOC (importer->wb))) {
		g_printerr ("[%s]", go_doc_get_uri (GO_DOC (importer->wb)));
		return;
	}
	g_printerr ("%s%d : ", col_name(fn_col), fn_row+1);
}

/* Binary operator tokens */
static GnmExprOp const binary_ops [] = {
	GNM_EXPR_OP_ADD,	/* 0x03, ptgAdd */
	GNM_EXPR_OP_SUB,	/* 0x04, ptgSub */
	GNM_EXPR_OP_MULT,	/* 0x05, ptgMul */
	GNM_EXPR_OP_DIV,	/* 0x06, ptgDiv */
	GNM_EXPR_OP_EXP,	/* 0x07, ptgPower */
	GNM_EXPR_OP_CAT,	/* 0x08, ptgConcat */
	GNM_EXPR_OP_LT,		/* 0x09, ptgLT */
	GNM_EXPR_OP_LTE,	/* 0x0a, ptgLTE */
	GNM_EXPR_OP_EQUAL,	/* 0x0b, ptgEQ */
	GNM_EXPR_OP_GTE,	/* 0x0c, ptgGTE */
	GNM_EXPR_OP_GT,		/* 0x0d, ptgGT */
	GNM_EXPR_OP_NOT_EQUAL,	/* 0x0e, ptgNE */

	GNM_EXPR_OP_INTERSECT,	/* 0x0f, ptgIsect : Intersection */
	0, /* handled elsewhere	   0x10, ptgUnion : Union */
	GNM_EXPR_OP_RANGE_CTOR	/* 0x11, ptgRange : Range */
};

static GnmExprOp const unary_ops [] = {
	GNM_EXPR_OP_UNARY_PLUS,/* 0x12, ptgU_PLUS  */
	GNM_EXPR_OP_UNARY_NEG,	/* 0x13, ptgU_MINUS */
	GNM_EXPR_OP_PERCENTAGE,	/* 0x14, ptgPERCENT */
	GNM_EXPR_OP_PAREN,      /* 0x15, pgtPAREN */
};

static gboolean
excel_formula_parses_ref_sheets (MSContainer const *container, guint8 const *data,
				 Sheet **first, Sheet **last)
{
	if (container->importer->ver >= MS_BIFF_V8) {
		ExcelExternSheetV8 const *es =
			excel_externsheet_v8 (container->importer, GSF_LE_GET_GUINT16 (data));

		if (es != NULL) {
			if (es->first == XL_EXTERNSHEET_MAGIC_DELETED ||
			    es->last == XL_EXTERNSHEET_MAGIC_DELETED)
				return TRUE;
			*first = es->first;
			*last = es->last;
		}
	} else {
		gint16 ixals = GSF_LE_GET_GINT16 (data);
		gint16 a = GSF_LE_GET_GINT16 (data + 10);
		gint16 b = GSF_LE_GET_GINT16 (data + 12);

		if (a < 0 || b < 0) /* deleted sheets */
			return TRUE;

		d (1, g_printerr (" : 0x%hx : 0x%hx : 0x%hx\n", ixals, a, b););

		/* ixals < 0 == reference within the current workbook
		 *    ixals == negative one based index into containers externsheet table
		 *    a and b
		 *    < 0 refers to a deleted sheet
		 *    ==0 refers to the current sheet (should not occur in global names)
		 *    > 0  1 based indicies to the sheets in the workbook
		 *    a is always the same as ixals
		 *    3d references use -ixals and b.  Since global names
		 *    precede the BOUNDSHEET records and b refers to their
		 *    content XL cheated and just spews an externsheet for
		 *    every sheet if there are any 3d references.
		 *
		 *    see pivot.xls as an example of expressions with a&b as
		 *    sheet indicies before the boundsheets.
		 **/
		if (ixals < 0) {
			*first = excel_externsheet_v7 (container, -ixals);
			*last = (a == b) ? *first
				: ((b > 0)
				   ? excel_externsheet_v7 (container, b)
				   : ms_container_sheet (container));
		} else {
			*first = excel_externsheet_v7 (container, ixals);
			*last  = excel_externsheet_v7 (container, b);
		}
	}

	if (*first == XL_EXTERNSHEET_MAGIC_SELFREF) {
		*first = *last = NULL;
		g_warning ("So much for that theory.  Please send us a copy of this workbook");
	} else if (*last == XL_EXTERNSHEET_MAGIC_SELFREF) {
		*last = *first;
		g_warning ("so much for that theory.  Please send us a copy of this workbook");
	} else if (*first != NULL && *last == NULL)
		*last = *first;

	return FALSE;
}

static char const *ptg_name[] = {
	"PTG ZERO ???",	"PTG_EXPR",	"PTG_TBL",	"PTG_ADD",
	"PTG_SUB",	"PTG_MULT",	"PTG_DIV",	"PTG_EXP",
	"PTG_CONCAT",	"PTG_LT",	"PTG_LTE",	"PTG_EQUAL",
	"PTG_GTE",	"PTG_GT",	"PTG_NOT_EQUAL", "PTG_INTERSECT",

	"PTG_UNION",	"PTG_RANGE",	"PTG_U_PLUS",	"PTG_U_MINUS",
	"PTG_PERCENT",	"PTG_PAREN",	"PTG_MISSARG",	"PTG_STR",
	"PTG_EXTENDED", "PTG_ATTR",	"PTG_SHEET",	"PTG_SHEET_END",
	"PTG_ERR",	"PTG_BOOL",	"PTG_INT",	"PTG_NUM",

	"PTG_ARRAY",	"PTG_FUNC",	"PTG_FUNC_VAR", "PTG_NAME",
	"PTG_REF",	"PTG_AREA",	"PTG_MEM_AREA", "PTG_MEM_ERR",
	"PTG_MEM_NO_MEM", "PTG_MEM_FUNC", "PTG_REF_ERR", "PTG_AREA_ERR",
	"PTG_REFN",	"PTG_AREAN",	"PTG_MEM_AREAN", "PTG_NO_MEMN",

	"PTG_30",	"PTG_31",	"PTG_32",	"PTG_33",
	"PTG_34",	"PTG_35",	"PTG_36",	"PTG_37",

	"PTG_FUNC_CE",	"PTG_NAME_X",	"PTG_REF_3D",	"PTG_AREA_3D",
	"PTG_REF_ERR_3D", "PTG_AREA_ERR_3D", "PTG_3E",	"PTG_3F"
};


static gboolean
check_formula_len (int left, int needed)
{
	if (needed > left) {
		g_warning ("File is most likely corrupted.\n"
			   "(Needed %d bytes for formula item, has only %d.)",
			   needed, left);
		return TRUE;
	}
	return FALSE;
}


#define CHECK_FORMULA_LEN(len_)					\
do {								\
	ptg_length = (len_);					\
	if (check_formula_len (len_left, ptg_length + 1)) {	\
		goto length_error;				\
	}							\
} while (0)							\

#define CHECK_FORMULA_ARRAY_LEN(len_)				\
do {								\
	int needed_ = len_;					\
	int left_ = array_length - (array_data - array_data0);	\
	if (check_formula_len (left_, needed_)) {		\
                if (v)						\
			value_release (v);			\
		goto length_error;				\
	}							\
} while (0)

/**
 * Parse that RP Excel formula, see S59E2B.HTM
 * Return a dynamicly allocated GnmExpr containing the formula, or NULL
 **/
static GnmExpr const *
excel_parse_formula1 (MSContainer const *container,
		      ExcelReadSheet const *esheet,
		      int fn_col, int fn_row,
		      guint8 const *mem, guint16 length, guint16 array_length,
		      gboolean shared,
		      gboolean *array_element)
{
	MsBiffVersion const ver = container->importer->ver;
	GnmSheetSize const *ss = esheet
		? gnm_sheet_get_size (esheet->sheet)
		: workbook_get_sheet_size (container->importer->wb);

	/* so that the offsets and lengths match the documentation */
	guint8 const *cur = mem + 1;

	/* Array sizes and values are stored at the end of the stream */
	guint8 const *array_data0 = mem + length;
	guint8 const *array_data = array_data0;

	int len_left = length;
	GnmExprList *stack = NULL;
	gboolean error = FALSE;
	int ptg_length, ptg, ptgbase;
	guint8 eptg;

	if (array_element != NULL)
		*array_element = FALSE;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_formula_debug > 1) {
		ms_excel_dump_cellname (container->importer, esheet, fn_col, fn_row);
		g_printerr ("\n");
		if (ms_excel_formula_debug > 1) {
			gsf_mem_dump (mem, length);
		}
	}
#endif

	while (len_left > 0 && !error) {
		ptg_length = 0;
		ptg = GSF_LE_GET_GUINT8 (cur-1);
		ptgbase = ((ptg & 0x40) ? (ptg | 0x20): ptg) & 0x3F;
		if (ptg > FORMULA_PTG_MAX)
			break;
		d (2, {
			g_printerr ("Ptg : %s 0x%02x", ptg_name [ptgbase], ptg);
			if (ptg != ptgbase)
				g_printerr ("(0x%02x)", ptgbase);
			g_printerr ("\n");
		});

		switch (ptgbase) {
		case FORMULA_PTG_EXPR: {
			GnmExpr const *expr;
			XLSharedFormula *sf;
			GnmCellPos top_left;

			if (ver >= MS_BIFF_V3) {
				CHECK_FORMULA_LEN(4);
				top_left.col = GSF_LE_GET_GUINT16 (cur+2);
			} else {
				CHECK_FORMULA_LEN(3);
				top_left.col = GSF_LE_GET_GUINT8 (cur+2);
			}
			top_left.row = GSF_LE_GET_GUINT16 (cur+0);
			sf = excel_sheet_shared_formula (esheet, &top_left);

			if (sf == NULL) {
				if (top_left.col != fn_col || top_left.row != fn_row) {
					g_warning ("Unknown shared formula (key = %s) in ", cellpos_as_string (&top_left));
					ms_excel_dump_cellname (container->importer, esheet, fn_col, fn_row);
				}
				parse_list_free (&stack);
				return NULL;
			}

			if (sf->being_parsed) {
				g_warning ("Recursive shared formula, key = %s\n",
					   cellpos_as_string (&top_left));
				parse_list_free (&stack);
				return NULL;
			}

			if (sf->is_array) {
				if (array_element != NULL)
					*array_element = TRUE;
				else
					g_warning ("EXCEL: unexpected array\n");

				parse_list_free (&stack);
				return NULL;
			}

			d (0, g_printerr ("Parse shared formula\n"););
			sf->being_parsed = TRUE;
			expr = excel_parse_formula1 (container, esheet, fn_col, fn_row,
						     sf->data, sf->data_len, sf->array_data_len,
						     TRUE, array_element);
			sf->being_parsed = FALSE;
			parse_list_push (&stack, expr);
			ptg_length = length; /* Force it to be the only token */
			break;
		}

		case FORMULA_PTG_TBL: {
			XLDataTable *dt;
			GnmCellPos top_left;

			if (ver >= MS_BIFF_V3) {
				CHECK_FORMULA_LEN(4);
				top_left.col = GSF_LE_GET_GUINT16 (cur+2);
			} else {
				CHECK_FORMULA_LEN(3);
				top_left.col = GSF_LE_GET_GUINT8 (cur+2);
			}
			top_left.row = GSF_LE_GET_GUINT16 (cur+0);
			dt = excel_sheet_data_table (esheet, &top_left);

			if (dt == NULL) {
				if (top_left.col != fn_col || top_left.row != fn_row) {
					g_warning ("Unknown data table (key = %s) in ", cellpos_as_string (&top_left));
					ms_excel_dump_cellname (container->importer, esheet, fn_col, fn_row);
				}
			} else if (array_element != NULL) {
				*array_element = TRUE;
			} else {
				g_warning ("EXCEL: unexpected table\n");
			}

			parse_list_free (&stack);
			return NULL;
		}

		case FORMULA_PTG_ADD:
		case FORMULA_PTG_SUB:
		case FORMULA_PTG_MULT:
		case FORMULA_PTG_DIV:
		case FORMULA_PTG_EXP:
		case FORMULA_PTG_CONCAT:
		case FORMULA_PTG_LT:
		case FORMULA_PTG_LTE:
		case FORMULA_PTG_EQUAL:
		case FORMULA_PTG_GTE:
		case FORMULA_PTG_GT:
		case FORMULA_PTG_NOT_EQUAL:
		case FORMULA_PTG_INTERSECT: {
			GnmExpr const *r = parse_list_pop (&stack);
			GnmExpr const *l = parse_list_pop (&stack);
			parse_list_push (&stack, gnm_expr_new_binary (
				l,
				binary_ops [ptgbase - FORMULA_PTG_ADD],
				r));
			break;
		}

		case FORMULA_PTG_RANGE: {
			GnmExpr const *r = parse_list_pop (&stack);
			GnmExpr const *l = parse_list_pop (&stack);
			parse_list_push (&stack,
					 gnm_expr_new_range_ctor (l, r));
			break;
		}

		case FORMULA_PTG_UNION: {
			GnmExpr const *r = parse_list_pop (&stack);
			GnmExpr const *l = parse_list_pop (&stack);

			/* not exactly legal, but should be reasonable
			 * XL has union operator we have sets.
			 */
			if (GNM_EXPR_GET_OPER (l) != GNM_EXPR_OP_SET) {
				GnmExprList *args = gnm_expr_list_prepend (NULL, r);
				args = gnm_expr_list_prepend (args, l);
				parse_list_push (&stack, gnm_expr_new_set (args));
			} else {
				/* Barf!  -- MW.  */
				GnmExpr *ll = (GnmExpr *)l;
				ll->set.argc++;
				ll->set.argv = g_renew (GnmExprConstPtr,
							ll->set.argv,
							ll->set.argc);
				ll->set.argv[ll->set.argc - 1] = r;
				parse_list_push (&stack, l);
			}
			break;
		}

		case FORMULA_PTG_U_PLUS:
		case FORMULA_PTG_U_MINUS:
		case FORMULA_PTG_PERCENT:
		case FORMULA_PTG_PAREN: {
			GnmExpr const *e = gnm_expr_new_unary
				(unary_ops [ptgbase - FORMULA_PTG_U_PLUS],
				 parse_list_pop (&stack));
			e = undo_save_hacks (e);
			parse_list_push (&stack, e);
			break;
		}

		case FORMULA_PTG_MISSARG:
			parse_list_push_raw (&stack, value_new_empty ());
			ptg_length = 0;
			break;

		case FORMULA_PTG_ATTR: {
			/* FIXME: not fully implemented */
			guint8 grbit;
			guint16 w;
			if (ver >= MS_BIFF_V3) {
				CHECK_FORMULA_LEN(3);
				w = GSF_LE_GET_GUINT16 (cur+1);
			} else {
				CHECK_FORMULA_LEN(2);
				w = GSF_LE_GET_GUINT8 (cur+1);
			}

			grbit = GSF_LE_GET_GUINT8 (cur);
			if (grbit == 0x00) {
#if 0
				ms_excel_dump_cellname (container->importer, esheet, fn_col, fn_row);
				g_printerr ("Hmm, ptgAttr of type 0 ??\n"
					"I've seen a case where an instance of this with flag A and another with flag 3\n"
					"bracket a 1x1 array formula.  please send us this file.\n"
					"Flags = 0x%X\n", w);
#else
				; /* this looks ignoreable */
#endif
			} else if (grbit & 0x01) {
				d (2, g_printerr ("A volatile function\n"););

			/* AttrIf: stores jump to FALSE condition */
			} else if (grbit & 0x02) {
				/* Ignore cached result */
				d (2, g_printerr ("ATTR IF\n"););

			/* AttrChoose: stores table of inputs */
			} else if (grbit & 0x04) {
				/* Ignore the optimzation to specify which arg to use */
				d (2, g_printerr ("ATTR CHOOSE\n"););
				ptg_length = 2 * ((w + 1) /* args */ + 1 /* count */) + 1;

			/* AttrGoto: bytes/words to skip during _evaluation_.
			 * We still need to parse them */
			} else if (grbit & 0x08) {
				d (2, g_printerr ("ATTR GOTO\n"););

			/* AttrSum: 'optimised' SUM function */
			} else if (grbit & 0x10) {
				if (!make_function (&stack, 0x04, 1, container->importer->wb)) {
					error = TRUE;
					g_printerr ("Error in optimised SUM\n");
				}

			/* AttrSpace */
			} else if (grbit & 0x40) {
				guint8 num_space = GSF_LE_GET_GUINT8 (cur+2);
				guint8 attrs     = GSF_LE_GET_GUINT8 (cur+1);
				if (attrs == 0) /* bitFSpace: ignore it for now */
					;
				else
					d (2, g_printerr ("Redundant whitespace in formula 0x%x count %d\n", attrs, num_space););
			} else {
				ms_excel_dump_cellname (container->importer, esheet, fn_col, fn_row);
				g_printerr ("Unknown PTG Attr gr = 0x%x, w = 0x%x ptg = 0x%x\n", grbit, w, ptg);
				error = TRUE;
			}
		}
		break;

		case FORMULA_PTG_SHEET:
			CHECK_FORMULA_LEN(10);
			g_warning ("PTG_SHEET! please send us a copy of this file.");
			break;

		case FORMULA_PTG_SHEET_END:
			CHECK_FORMULA_LEN(4);
			g_warning ("PTG_SHEET_END! please send us a copy of this file.");
			break;

		case FORMULA_PTG_ERR:
			CHECK_FORMULA_LEN(1);
			parse_list_push_raw (&stack, xls_value_new_err (NULL, GSF_LE_GET_GUINT8 (cur)));
			break;

		case FORMULA_PTG_INT:
			CHECK_FORMULA_LEN(2);
			parse_list_push_raw (&stack, value_new_int (GSF_LE_GET_GUINT16 (cur)));
			break;

		case FORMULA_PTG_BOOL:
			CHECK_FORMULA_LEN(1);
			parse_list_push_raw (&stack, value_new_bool (GSF_LE_GET_GUINT8 (cur)));
			break;

		case FORMULA_PTG_NUM:
			CHECK_FORMULA_LEN(8);
			parse_list_push_raw (&stack, value_new_float (gsf_le_get_double (cur)));
			break;

		case FORMULA_PTG_STR: {
			char *str;
			guint32 byte_len;
			int char_len;

			CHECK_FORMULA_LEN(1);
			char_len = GSF_LE_GET_GUINT8 (cur);

			str = excel_get_text (container->importer, cur+1,
					      char_len, &byte_len, NULL,
					      len_left - 2);
			ptg_length = 1 + byte_len;

			if (str != NULL) {
				d (2, g_printerr ("   -> '%s'\n", str););
				parse_list_push_raw (&stack, value_new_string_nocopy (str));
			} else {
				d (2, g_printerr ("   -> \'\'\n"););
				parse_list_push_raw (&stack, value_new_string (""));
			}
			break;
		}

		case FORMULA_PTG_EXTENDED: { /* Extended Ptgs for Biff8 */
			CHECK_FORMULA_LEN(1);
			switch ((eptg = GSF_LE_GET_GUINT8 (cur))) {
			default:
				g_warning ("EXCEL: unknown ePtg type %02x", eptg);
				break;

			case 0x00: /* Reserved */
			case 0x04: /* Reserved */
			case 0x05: /* Reserved */
			case 0x08: /* Reserved */
			case 0x09: /* Reserved */
			case 0x11: /* Reserved */
			case 0x12: /* Reserved */
			case 0x13: /* Reserved */
			case 0x14: /* Reserved */
			case 0x15: /* Reserved */
			case 0x16: /* Reserved */
			case 0x17: /* Reserved */
			case 0x18: /* Reserved */
			case 0x1b: /* Reserved */
			case 0x1c: /* Reserved */
			case 0x1e: /* reserved */
			case 0x19: /* Invalid */
			case 0x1a: /* Invalid */
				g_warning ("EXCEL: unexpected ePtg type %02x", eptg);
				break;

			case 0x02: /* eptgElfRw,	No,  Ref */
			case 0x03: /* eptgElfCol,	No,  Ref */
			case 0x06: /* eptgElfRwV,	No,  Value */
			case 0x07: /* eptgElfColV,	No,  Value */
			case 0x0c: /* eptgElfRwS,	Yes, Ref */
			case 0x0d: /* eptgElfColS,	Yes, Ref */
			case 0x0e: /* eptgElfRwSV,	Yes, Value */
			case 0x0f: /* eptgElfColSV,	Yes, Value */
			{
				/* WARNING: No documentation for this.  However this seems
				 * to make sense.
				 *
				 * NOTE:
				 * I cheat here.
				 * This reference is really to the entire row/col
				 * left/below the specified cell.
				 *
				 * However we don't support that feature in gnumeric
				 * nor do we support taking the intersection of the
				 * vector and the calling cell.
				 *
				 * So
				 * Cheat.  and perform the intersection here.
				 * ie
				 *	A1 : x
				 *	A2 : 2  B2 : =x^2
				 * x is an eptgElfColV.  I replace that with a2
				 */
				GnmCellRef ref;
				CHECK_FORMULA_LEN(5);
				getRefV8 (&ref,
					  GSF_LE_GET_GUINT16 (cur + 1),
					  GSF_LE_GET_GUINT16 (cur + 3),
					  fn_col, fn_row, shared, ss);
				if ((eptg % 2))	/* Column are odd */
					ref.row = ref.row_relative ? 0 : fn_row;
				else		/* Row */
					ref.col = ref.col_relative ? 0 : fn_col;

				parse_list_push (&stack, gnm_expr_new_cellref (&ref));
				break;
			}

			case 0x01:
				ptg_length += 4;	/* eptgElfLel,		No,  Err */
				parse_list_push (&stack,
					xl_expr_err (esheet, fn_col, fn_row,
						     "undocumented extended ptg 1", "#REF!"));
				break;
			case 0x0a:
				ptg_length += 13;	/* eptgRadical,		No,  Ref */
				parse_list_push (&stack,
					xl_expr_err (esheet, fn_col, fn_row,
						     "undocumented extended ptg 0xA", "#REF!"));
				break;
			case 0x0b:
				ptg_length += 13;	/* eptgRadicalS,	Yes, Ref */
				parse_list_push (&stack,
					xl_expr_err (esheet, fn_col, fn_row,
						     "undocumented extended ptg 0xB", "#REF!"));
				break;
			case 0x10:
				ptg_length += 4;	/* eptgElfRadicalLel, No, Err */
				/* does not seem to put anything on the stack */
				gnm_expr_free (
					xl_expr_err (esheet, fn_col, fn_row,
						     "undocumented extended ptg 0x10", "#REF!"));
				break;
			case 0x1d:
				ptg_length += 4;	/* eptgSxName, No, Value */
				parse_list_push (&stack,
					xl_expr_err (esheet, fn_col, fn_row,
						     "undocumented extended ptg 0x1D", "#REF!"));
				break;
			}
		}
		break;

		case FORMULA_PTG_ARRAY: {
			unsigned cols, rows;
			unsigned lpx, lpy;
			GnmValue *v = NULL;

			CHECK_FORMULA_LEN(7);
			CHECK_FORMULA_ARRAY_LEN(3);
			cols = GSF_LE_GET_GUINT8  (array_data + 0);
			rows = GSF_LE_GET_GUINT16 (array_data + 1);
			array_data += 3;

			if (ver >= MS_BIFF_V8) {
				cols++;
				rows++;
			} else {
				if (cols == 0)
					cols = 256;
				if (rows == 0)
					rows = 1; /* ??? */
			}

			v = value_new_array (cols, rows);

#ifndef NO_DEBUG_EXCEL
			if (ms_excel_formula_debug > 4) {
				/* no way to dump the content because we have
				 * no idea how long it is
				 */
				g_printerr ("An Array how interesting: (%d,%d)\n",
					cols, rows);
			}
#endif
			for (lpy = 0; lpy < rows; lpy++) {
				for (lpx = 0; lpx < cols; lpx++) {
					GnmValue *elem;
					guint8 val_type;

					CHECK_FORMULA_ARRAY_LEN(1);
					val_type = GSF_LE_GET_GUINT8 (array_data);
					array_data++;
#ifndef NO_DEBUG_EXCEL
					if (ms_excel_formula_debug > 5) {
						g_printerr ("\tArray elem type 0x%x (%d,%d)\n", val_type, lpx, lpy);
					}
#endif
					switch (val_type) {
					case 0:
						CHECK_FORMULA_ARRAY_LEN(8);
						elem = value_new_empty ();
						array_data += 8;
						break;
					case 1:
						CHECK_FORMULA_ARRAY_LEN(8);
						elem = value_new_float (gsf_le_get_double (array_data));
						array_data += 8;
						break;

					case 2: {
						guint32 len, chars;
						char *str;

						if (ver >= MS_BIFF_V8) {
							CHECK_FORMULA_ARRAY_LEN(2);
							chars = GSF_LE_GET_GUINT16 (array_data);
							array_data += 2;
						} else {
							CHECK_FORMULA_ARRAY_LEN(1);
							chars = GSF_LE_GET_GUINT8 (array_data);
							array_data++;
						}
						str = excel_get_text
							(container->importer, array_data,
							 chars, &len, NULL,
							 array_length - (array_data - array_data0));
						array_data += len;

						if (str) {
#ifndef NO_DEBUG_EXCEL
							if (ms_excel_formula_debug > 5) {
								g_printerr ("\tString '%s'\n", str);
							}
#endif
							elem = value_new_string_nocopy (str);
						} else
							elem = value_new_string ("");
						break;
					}

					case 4:
						CHECK_FORMULA_ARRAY_LEN(8);
						elem = value_new_bool (array_data[0] ? TRUE : FALSE);
						array_data += 8;
						break;
					case 16:
						CHECK_FORMULA_ARRAY_LEN(8);
						elem = xls_value_new_err (NULL, array_data[0]);
						array_data += 8;
						break;

					default:
						g_printerr ("FIXME: Duff array item type %d @ %s%d:%d,%d\n",
							val_type, col_name(fn_col), fn_row+1, lpx, lpy);
						CHECK_FORMULA_ARRAY_LEN(8);
						gsf_mem_dump (array_data-1, 9);
						elem = value_new_empty ();
					}
					value_array_set (v, lpx, lpy, elem);
				}
			}
			parse_list_push_raw (&stack, v);
			break;
		}

		case FORMULA_PTG_FUNC: {
			/* index into fn table */
			int iftab;

			if (ver >= MS_BIFF_V4) {
				CHECK_FORMULA_LEN(2);
				iftab = GSF_LE_GET_GUINT16 (cur);
			} else {
				CHECK_FORMULA_LEN(1);
				iftab = GSF_LE_GET_GUINT8 (cur);
			}

			if (!make_function (&stack, iftab, -1, container->importer->wb)) {
				error = TRUE;
				g_printerr ("error making func\n");
			}
			break;
		}

		case FORMULA_PTG_FUNC_VAR: {
			int numargs;
			/* index into fn table */
			int iftab;
#if 0
			/* Prompts the user ?  */
			int const prompt  = (GSF_LE_GET_GUINT8 ( cur ) & 0x80);
			/* is a command equiv.?*/
			int const cmdquiv = (GSF_LE_GET_GUINT16 (cur+1) & 0x8000);
#endif
			if (ver >= MS_BIFF_V4) {
				CHECK_FORMULA_LEN(3);
				iftab = (GSF_LE_GET_GUINT16 (cur+1) & 0x7fff);
			} else {
				CHECK_FORMULA_LEN(2);
				iftab = GSF_LE_GET_GUINT8 (cur+1);
			}
			numargs = (GSF_LE_GET_GUINT8 (cur) & 0x7f);

			if (!make_function (&stack, iftab, numargs, container->importer->wb)) {
				error = TRUE;
				g_printerr ("error making func var\n");
			}
			break;
		}

		case FORMULA_PTG_NAME: {
			guint16 name_idx;
			GPtrArray    *names;
			GnmExpr const*name;
			GnmNamedExpr *nexpr = NULL;

			if (ver >= MS_BIFF_V8)
				CHECK_FORMULA_LEN(4);  /* Docs are wrong, no ixti */
			else if (ver >= MS_BIFF_V5)
				CHECK_FORMULA_LEN(14);
			else
				CHECK_FORMULA_LEN(10);
			name_idx = GSF_LE_GET_GUINT16 (cur);

			names = container->importer->names;
			if (name_idx < 1 || names->len < name_idx ||
			    (nexpr = g_ptr_array_index (names, name_idx-1)) == NULL) {

				/* this may be a named used within a name be
				 * cautious about creating a place holder, I'll
				 * guess that there will never be more than 256
				 * names in a file just to be safe */
				if (1 <= name_idx && name_idx < 256) {
					char *stub_name = g_strdup_printf ("FwdDecl%d", name_idx);
					if (name_idx >= names->len)
						g_ptr_array_set_size (names, name_idx);
					nexpr = g_ptr_array_index (names, name_idx-1) =
						expr_name_new (stub_name);
					name = gnm_expr_new_name (nexpr, NULL, NULL);
					d (1, g_printerr ("creating stub '%s'", stub_name););
					g_free (stub_name);
				} else
				{
					g_warning ("EXCEL: %x (of %x) UNKNOWN name %p.",
						   name_idx, names ? names->len : 0xffffffff, (void *)container);
					name = gnm_expr_new_constant (
						value_new_error_NAME (NULL));
				}
			} else
				name = gnm_expr_new_name (nexpr, NULL, NULL);

			parse_list_push (&stack, name);
			d (2, g_printerr ("Name idx %hu\n", name_idx););
		}
		break;

		case FORMULA_PTG_REF_ERR:
			CHECK_FORMULA_LEN(ver >= MS_BIFF_V8 ? 4 : 3);
			parse_list_push_raw (&stack, value_new_error_REF (NULL));
			break;

		case FORMULA_PTG_AREA_ERR:
			CHECK_FORMULA_LEN(ver >= MS_BIFF_V8 ? 8 : 6);
			parse_list_push_raw (&stack, value_new_error_REF (NULL));
			break;

		case FORMULA_PTG_REF: case FORMULA_PTG_REFN: {
			GnmCellRef ref;
			if (ver >= MS_BIFF_V8) {
				CHECK_FORMULA_LEN(4);
				getRefV8 (&ref,
					  GSF_LE_GET_GUINT16 (cur),
					  GSF_LE_GET_GUINT16 (cur + 2),
					  fn_col, fn_row, ptgbase == FORMULA_PTG_REFN,
					  ss);
			} else {
				CHECK_FORMULA_LEN(3);
				getRefV7 (&ref,
					  GSF_LE_GET_GUINT8 (cur+2),
					  GSF_LE_GET_GUINT16 (cur),
					  fn_col, fn_row, ptgbase == FORMULA_PTG_REFN);
			}
			parse_list_push (&stack, gnm_expr_new_cellref (&ref));
			break;
		}

		case FORMULA_PTG_AREA: case FORMULA_PTG_AREAN: {
			GnmCellRef first, last;
			if (ver >= MS_BIFF_V8) {
				CHECK_FORMULA_LEN(8);
				getRefV8 (&first,
					  GSF_LE_GET_GUINT16 (cur+0),
					  GSF_LE_GET_GUINT16 (cur+4),
					  fn_col, fn_row, ptgbase == FORMULA_PTG_AREAN,
					  ss);
				getRefV8 (&last,
					  GSF_LE_GET_GUINT16 (cur+2),
					  GSF_LE_GET_GUINT16 (cur+6),
					  fn_col, fn_row, ptgbase == FORMULA_PTG_AREAN,
					  ss);
			} else {
				CHECK_FORMULA_LEN(6);
				getRefV7 (&first,
					  GSF_LE_GET_GUINT8 (cur+4),
					  GSF_LE_GET_GUINT16 (cur+0),
					  fn_col, fn_row, ptgbase == FORMULA_PTG_AREAN);
				getRefV7 (&last,
					  GSF_LE_GET_GUINT8 (cur+5),
					  GSF_LE_GET_GUINT16 (cur+2),
					  fn_col, fn_row, ptgbase == FORMULA_PTG_AREAN);
			}

			parse_list_push_raw (&stack, value_new_cellrange (&first, &last, fn_col, fn_row));
			break;
		}

		case FORMULA_PTG_MEM_AREA:
		case FORMULA_PTG_MEM_ERR:
			/* ignore this, we handle at run time */
			CHECK_FORMULA_LEN(6);
			break;

		case FORMULA_PTG_MEM_FUNC:
			/* ignore this, we handle at run time */
			CHECK_FORMULA_LEN(2);
			break;

		case FORMULA_PTG_NAME_X: {
			guint16 name_idx; /* 1 based */
			GPtrArray    *names = NULL;
			GnmExpr const*name;
			GnmNamedExpr *nexpr = NULL;
			Sheet *sheet = NULL;

			if (ver >= MS_BIFF_V8) {
				guint16 sheet_idx;
				ExcelExternSheetV8 const *es;

				CHECK_FORMULA_LEN(6);
				sheet_idx = GSF_LE_GET_GINT16 (cur);
				es = excel_externsheet_v8 (container->importer, sheet_idx);

				if (es != NULL && es->supbook < container->importer->v8.supbook->len) {
					ExcelSupBook const *sup = &g_array_index (
						container->importer->v8.supbook,
						ExcelSupBook, es->supbook);
					if (sup->type == EXCEL_SUP_BOOK_SELFREF)
						names = container->importer->names;
					else
						names = sup->externname;

					sheet = es->first;
				}

				name_idx  = GSF_LE_GET_GUINT16 (cur+2);

				d (2, g_printerr ("name %hu : externsheet %hu\n",
					       name_idx, sheet_idx););
			} else {
				gint16 sheet_idx;

				CHECK_FORMULA_LEN(24);
				sheet_idx = GSF_LE_GET_GINT16 (cur);
				name_idx  = GSF_LE_GET_GUINT16 (cur+10);
#if 0
				gsf_mem_dump (cur, 24);
				d (-2, g_printerr ("name = %hu, externsheet = %hd\n",
					       name_idx, sheet_idx););
#endif
				if (sheet_idx < 0) {
					names = container->importer->names;
					sheet_idx = -sheet_idx;
				} else
					names = container->v7.externnames;
				sheet = excel_externsheet_v7 (container, sheet_idx);
			}

			if (names == NULL || name_idx < 1 || names->len < name_idx ||
			    (nexpr = g_ptr_array_index (names, name_idx-1)) == NULL) {
				g_warning ("EXCEL: %x (of %x) UNKNOWN name %p.",
					   name_idx, names ? names->len : 0xffffffff, (void *)container);
				name = gnm_expr_new_constant (
					value_new_error_REF (NULL));
			} else {
				/* See supbook_get_sheet for details */
				if (sheet == XL_EXTERNSHEET_MAGIC_SELFREF) {
					sheet = nexpr->pos.sheet;
					if (sheet == NULL)
						sheet = ms_container_sheet (container);
				} else if (sheet == XL_EXTERNSHEET_MAGIC_DELETED) {
					/* What?  Happens for #752179.  */
					sheet = NULL;
				}

				name = gnm_expr_new_name (nexpr, sheet, NULL);
			}

			parse_list_push (&stack, name);
			d (2, g_printerr ("Name idx %hu\n", name_idx););
		}
		break;

		case FORMULA_PTG_REF_3D: { /* see S59E2B.HTM */
			GnmCellRef first, last;
			if (ver >= MS_BIFF_V8) {
				CHECK_FORMULA_LEN(6);
				getRefV8 (&first,
					  GSF_LE_GET_GUINT16 (cur + 2),
					  GSF_LE_GET_GUINT16 (cur + 4),
					  fn_col, fn_row, FALSE, ss);
				last = first;
			} else {
				CHECK_FORMULA_LEN(17);
				getRefV7 (&first,
					  GSF_LE_GET_GUINT8  (cur + 16),
					  GSF_LE_GET_GUINT16 (cur + 14),
					  fn_col, fn_row, shared);
				last = first;
			}

			if (excel_formula_parses_ref_sheets (container, cur, &first.sheet, &last.sheet))
				parse_list_push_raw (&stack, value_new_error_REF (NULL));
			else if (first.sheet != last.sheet)
				parse_list_push_raw (&stack, value_new_cellrange (&first, &last, fn_col, fn_row));
			else
				parse_list_push (&stack, gnm_expr_new_cellref (&first));
			break;
		}

		case FORMULA_PTG_AREA_3D: { /* see S59E2B.HTM */
			/* See comments in FORMULA_PTG_REF_3D for correct handling of external references */
			GnmCellRef first, last;

			if (ver >= MS_BIFF_V8) {
				CHECK_FORMULA_LEN(10);
				getRefV8 (&first,
					  GSF_LE_GET_GUINT16 (cur+2),
					  GSF_LE_GET_GUINT16 (cur+6),
					  fn_col, fn_row, FALSE, ss);
				getRefV8 (&last,
					  GSF_LE_GET_GUINT16 (cur+4),
					  GSF_LE_GET_GUINT16 (cur+8),
					  fn_col, fn_row, FALSE, ss);
			} else {
				CHECK_FORMULA_LEN(20);
				getRefV7 (&first,
					  GSF_LE_GET_GUINT8 (cur+18),
					  GSF_LE_GET_GUINT16 (cur+14),
					  fn_col, fn_row, shared);
				getRefV7 (&last,
					  GSF_LE_GET_GUINT8 (cur+19),
					  GSF_LE_GET_GUINT16 (cur+16),
					  fn_col, fn_row, shared);
			}
			if (excel_formula_parses_ref_sheets (container, cur, &first.sheet, &last.sheet))
				parse_list_push_raw (&stack, value_new_error_REF (NULL));
			else
				parse_list_push_raw (&stack, value_new_cellrange (&first, &last, fn_col, fn_row));
			break;
		}

		case FORMULA_PTG_REF_ERR_3D:
			CHECK_FORMULA_LEN(ver >= MS_BIFF_V8 ? 6 : 17);
			parse_list_push_raw (&stack, value_new_error_REF (NULL));
			break;

		case FORMULA_PTG_AREA_ERR_3D:
			CHECK_FORMULA_LEN(ver >= MS_BIFF_V8 ? 10 : 20);
			parse_list_push_raw (&stack, value_new_error_REF (NULL));
			break;

		default:
			/* What the heck ??
			 * In some workbooks (hebrew.xls) expressions in EXTERNNAMEs
			 * seem to have a an extra 2 zero bytes at the end
			 **/
			if (len_left > 2) {
				g_warning ("EXCEL: Unhandled PTG 0x%x.", ptg);
				error = TRUE;
				ptg_length = 1;
			}
		}
/*		g_printerr ("Ptg 0x%x length (not inc. ptg byte) %d\n", ptgbase, ptg_length); */
		cur      += ptg_length + 1;
		len_left -= ptg_length + 1;
	}

 length_error:

	if (error) {
		g_printerr ("formula data: %s\n", (shared?" (shared)":"(NOT shared)"));
		gsf_mem_dump (mem, length);

		parse_list_free (&stack);
		return xl_expr_err (esheet, fn_col, fn_row,
				    "Unknown Formula/Array",
				    "#Unknown!");
	}

	if (stack == NULL)
		return xl_expr_err (esheet, fn_col, fn_row,
				    "Stack too short - unusual",
				    "#ShortStack!");
	if (gnm_expr_list_length (stack) > 1) {
		parse_list_free (&stack);
		return xl_expr_err (esheet, fn_col, fn_row,
				    "Too much data on stack - probable cause: fixed args function is var-arg",
				    "#LongStack!");
	}

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_formula_debug > 0 && esheet != NULL) {
		GnmParsePos pp;
		GnmExpr const *expr = stack->data;
		parse_pos_init (&pp, NULL, esheet->sheet, fn_col, fn_row);
		g_printerr ("%s\n",  gnm_expr_as_string (expr, &pp, gnm_conventions_default));
	}
#endif
	return parse_list_pop (&stack);
}
#undef CHECK_FORMULA_LEN
#undef CHECK_FORMULA_ARRAY_LEN

GnmExprTop const *
excel_parse_formula (MSContainer const *container,
		     ExcelReadSheet const *esheet,
		     int fn_col, int fn_row,
		     guint8 const *mem, guint16 length, guint16 array_length,
		     gboolean shared,
		     gboolean *array_element)
{
	GnmExprTop const *texpr =
		gnm_expr_top_new (excel_parse_formula1 (container, esheet,
							fn_col, fn_row,
							mem, length, array_length,
							shared,
							array_element));
	return texpr
		? gnm_expr_sharer_share (container->importer->expr_sharer, texpr)
		: NULL;
}
