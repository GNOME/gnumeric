/**
 * lotus-formula.c: Lotus 123 formula support for Gnumeric
 *
 * Author:
 *    See: README
 *    Michael Meeks <michael@imagiantor.com>
 * Revamped in Aug 2002
 *    Jody Goldberg <jody@gnome.org>
 * New 123 formats done September 2005
 *    Morten Welinder (terra@gnome.org)
 *
 * http://groups.google.com/group/comp.apps.spreadsheets/msg/ea9dfa8a825c6d87?hl=en&
 **/
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <string.h>
#include "lotus.h"
#include "lotus-types.h"
#include "lotus-formula.h"

#include <expr.h>
#include <expr-impl.h>
#include <parse-util.h>
#include <value.h>
#include <gutils.h>
#include <func.h>
#include <gsf/gsf-utils.h>

#define FORMULA_DEBUG 0


typedef struct _LFuncInfo LFuncInfo;

struct _LFuncInfo {
	short args;
	unsigned short ordinal;
	char const *lotus_name;
	char const *gnumeric_name;
	int (*handler) (GnmExprList **stack, LFuncInfo const *func, guint8 const *data, const GnmParsePos *orig);
};


static int wk1_std_func	   (GnmExprList **stack, LFuncInfo const *func, guint8 const *data, const GnmParsePos *orig);
/* a,b,c -> a,,-c,b */
static int wk1_nper_func   (GnmExprList **stack, LFuncInfo const *func, guint8 const *data, const GnmParsePos *orig);
/* year - 1900 */
static int wk1_year_func   (GnmExprList **stack, LFuncInfo const *func, guint8 const *data, const GnmParsePos *orig);
/* find - 1 */
static int wk1_find_func   (GnmExprList **stack, LFuncInfo const *func, guint8 const *data, const GnmParsePos *orig);
static int wk1_fin_func    (GnmExprList **stack, LFuncInfo const *func, guint8 const *data, const GnmParsePos *orig);
static int wk1_rate_func   (GnmExprList **stack, LFuncInfo const *func, guint8 const *data, const GnmParsePos *orig);

static const LFuncInfo functions_lotus[] = {
	{  0,  0x1F, "NA",	     "NA",	     wk1_std_func },
	{  1,  0x20, "ERR",	     NULL,	     wk1_std_func },
	{  1,  0x21, "ABS",	     "ABS",	     wk1_std_func },
	{  1,  0x22, "INT",	     "TRUNC",	     wk1_std_func },
	{  1,  0x23, "SQRT",	     "SQRT",	     wk1_std_func },
	{  1,  0x24, "LOG",	     "LOG",	     wk1_std_func },
	{  1,  0x25, "LN",	     "LN",	     wk1_std_func },
	{  0,  0x26, "PI",	     "PI",	     wk1_std_func },
	{  1,  0x27, "SIN",	     "SIN",	     wk1_std_func },
	{  1,  0x28, "COS",	     "COS",	     wk1_std_func },
	{  1,  0x29, "TAN",	     "TAN",	     wk1_std_func },
	{  2,  0x2A, "ATAN2",	     "ATAN2",	     wk1_std_func },
	{  1,  0x2B, "ATAN",	     "ATAN",	     wk1_std_func },
	{  1,  0x2C, "ASIN",	     "ASIN",	     wk1_std_func },
	{  1,  0x2D, "ACOS",	     "ACOS",	     wk1_std_func },
	{  1,  0x2E, "EXP",	     "EXP",	     wk1_std_func },
	{  2,  0x2F, "MOD",	     "MOD",	     wk1_std_func },
	{ -1,  0x30, "CHOOSE",	     "CHOOSE",	     wk1_std_func },
	{  1,  0x31, "ISNA",	     "ISNA",	     wk1_std_func },
	{  1,  0x32, "ISERR",	     "ISERR",	     wk1_std_func },
	{  0,  0x33, "FALSE",	     "FALSE",	     wk1_std_func },
	{  0,  0x34, "TRUE",	     "TRUE",	     wk1_std_func },
	{  0,  0x35, "RAND",	     "RAND",	     wk1_std_func },
	{  3,  0x36, "DATE",	     "DATE",	     wk1_std_func },
	{  0,  0x37, "TODAY",	     "TODAY",	     wk1_std_func },
	{  3,  0x38, "PMT",	     "PMT",	     wk1_fin_func },
	{  3,  0x39, "PV",	     "PV",	     wk1_fin_func },
	{  3,  0x3A, "FV",	     "FV",	     wk1_fin_func },
	{  3,  0x3B, "IF",	     "IF",	     wk1_std_func },
	{  1,  0x3C, "DAY",	     "DAY",	     wk1_std_func },
	{  1,  0x3D, "MONTH",	     "MONTH",	     wk1_std_func },
	{  1,  0x3E, "YEAR",	     "YEAR",	     wk1_year_func },
	{  2,  0x3F, "ROUND",	     "ROUND",	     wk1_std_func },
	{  3,  0x40, "TIME",	     "TIME",	     wk1_std_func },
	{  1,  0x41, "HOUR",	     "HOUR",	     wk1_std_func },
	{  1,  0x42, "MINUTE",	     "MINUTE",	     wk1_std_func },
	{  1,  0x43, "SECOND",	     "SECOND",	     wk1_std_func },
	{  1,  0x44, "ISNUMBER",     "ISNONTEXT",    wk1_std_func },
	{  1,  0x45, "ISSTRING",     "ISTEXT",	     wk1_std_func },
	{  1,  0x46, "LENGTH",	     "LEN",	     wk1_std_func },
	{  1,  0x47, "VALUE",	     "VALUE",	     wk1_std_func },
	{  2,  0x48, "STRING",	     "FIXED",	     wk1_std_func },
	{  3,  0x49, "MID",	     "MID",	     wk1_std_func },
	{  1,  0x4A, "CHAR",	     "CHAR",	     wk1_std_func },
	{  1,  0x4B, "CODE",	     "CODE",	     wk1_std_func },
	{  3,  0x4C, "FIND",	     "FIND",	     wk1_find_func },
	{  1,  0x4D, "DATEVALUE",    "DATEVALUE",    wk1_std_func },
	{  1,  0x4E, "TIMEVALUE",    "TIMEVALUE",    wk1_std_func },
	{  1,  0x4F, "CELLPOINTER",  NULL,	     wk1_std_func },
	{ -1,  0x50, "SUM",	     "SUM",	     wk1_std_func },
	{ -1,  0x51, "AVG",	     "AVERAGEA",     wk1_std_func },
	{ -1,  0x52, "COUNT",	     "COUNTA",	     wk1_std_func },
	{ -1,  0x53, "MIN",	     "MINA",	     wk1_std_func },
	{ -1,  0x54, "MAX",	     "MAXA",	     wk1_std_func },
	{  3,  0x55, "VLOOKUP",	     "VLOOKUP",	     wk1_std_func },
	{  2,  0x56, "NPV",	     "NPV",	     wk1_std_func },
	{ -1,  0x57, "VAR",	     "VARPA",	     wk1_std_func },
	{ -1,  0x58, "STD",	     "STDEVPA",	     wk1_std_func },
	{  2,  0x59, "IRR",	     "IRR",	     wk1_fin_func },
	{  3,  0x5A, "HLOOKUP",	     "HLOOKUP",	     wk1_std_func },
	{ -2,  0x5B, "DSUM",	     "DSUM",	     wk1_std_func },
	{ -2,  0x5C, "DAVG",	     "DAVERAGE",     wk1_std_func },
	{ -2,  0x5D, "DCNT",	     "DCOUNTA",	     wk1_std_func },
	{ -2,  0x5E, "DMIN",	     "DMIN",	     wk1_std_func },
	{ -2,  0x5F, "DMAX",	     "DMAX",	     wk1_std_func },
	{ -2,  0x60, "DVAR",	     "DVARP",	     wk1_std_func },
	{ -2,  0x61, "DSTD",	     "DSTDEVP",	     wk1_std_func },
	{ -3,  0x62, "INDEX",	     "INDEX",	     wk1_std_func },
	{  1,  0x63, "COLS",	     "COLUMNS",	     wk1_std_func },
	{  1,  0x64, "ROWS",	     "ROWS",	     wk1_std_func },
	{  2,  0x65, "REPEAT",	     "REPT",	     wk1_std_func },
	{  1,  0x66, "UPPER",	     "UPPER",	     wk1_std_func },
	{  1,  0x67, "LOWER",	     "LOWER",	     wk1_std_func },
	{  2,  0x68, "LEFT",	     "LEFT",	     wk1_std_func },
	{  2,  0x69, "RIGHT",	     "RIGHT",	     wk1_std_func },
	{  4,  0x6A, "REPLACE",	     "REPLACE",	     wk1_std_func },
	{  1,  0x6B, "PROPER",	     "PROPER",	     wk1_std_func },
	{  2,  0x6C, "CELL",	     "CELL",	     wk1_std_func },
	{  1,  0x6D, "TRIM",	     "TRIM",	     wk1_std_func },
	{  1,  0x6E, "CLEAN",	     "CLEAN",	     wk1_std_func },
	{  1,  0x6F, "S",	     "T",	     wk1_std_func },
	{  1,  0x70, "N",	     "N",	     wk1_std_func },
	{  2,  0x71, "EXACT",	     "EXACT",	     wk1_std_func },
	{  1,  0x72, "APP",	     NULL,	     wk1_std_func },
	{  1,  0x73, "@",	     "INDIRECT",     wk1_std_func },
	{  3,  0x74, "RATE",	     "RATE",	     wk1_rate_func },
	{  3,  0x75, "TERM",	     NULL,	     wk1_std_func },
	/* TERM ($1,$2,$3) is NPER ($2,$1,0,$3) */
	{  3,  0x76, "CTERM",	     NULL,	     wk1_nper_func },
	/* CTERM ($1,$2,$3) is NPER ($1,0,$3,$2) */
	{  3,  0x77, "SLN",	     "SLN",	     wk1_std_func },
	{  4,  0x78, "SYD",	     "SYD",	     wk1_std_func },
	{  4,  0x79, "DDB",	     "DDB",	     wk1_std_func },
	/* 0x7A is SPLFUNC which needs special handling */
	/* WK4 and up.	This list from wkrel9.txt */
	{  1,  0x7B, "SHEETS",	     "SHEETS",	     wk1_std_func },
	{  1,  0x7C, "INFO",	     "INFO",	     wk1_std_func },
	{ -1,  0x7D, "SUMPRODUCT",   "SUMPRODUCT",   wk1_std_func },
	{  1,  0x7E, "ISRANGE",	     "ISREF",	     wk1_std_func },
	{ -1,  0x7F, "DGET",	     "DGET",	     wk1_std_func },
	{ -1,  0x80, "DQUERY",	     NULL,	     wk1_std_func },
	{  4,  0x81, "COORD",	     NULL,	     wk1_std_func },
	{  0,  0x83, "TODAY",	     "TODAY",	     wk1_std_func },
	{ -1,  0x84, "VDB",	     "VDB",	     wk1_std_func },
	{ -1,  0x85, "DVARS",	     "DVAR",	     wk1_std_func },
	{ -1,  0x86, "DSTDS",	     "DSTDEV",	     wk1_std_func },
	{ -1,  0x87, "VARS",	     "VARA",	     wk1_std_func },
	{ -1,  0x88, "STDS",	     "STDEVA",	     wk1_std_func },
	{  2,  0x89, "D360",	     NULL,	     wk1_std_func },
	{  1,  0x8B, "ISAPP",	     NULL,	     wk1_std_func },
	{  1,  0x8C, "ISAAF",	     NULL,	     wk1_std_func },
	{  1,  0x8D, "WEEKDAY",	     "WEEKDAY",	     wk1_std_func },
	{  3,  0x8E, "DATEDIF",	     "DATEDIF",	     wk1_std_func },
	{ -1,  0x8F, "RANK",	     "RANK",	     wk1_std_func },
	{  2,  0x90, "NUMBERSTRING", NULL,	     wk1_std_func },
	{  1,  0x91, "DATESTRING",   NULL,	     wk1_std_func },
	{  1,  0x92, "DECIMAL",	     "HEX2DEC",	     wk1_std_func },
	{  1,  0x93, "HEX",	     "DEC2HEX",	     wk1_std_func },
	{  4,  0x94, "DB",	     "DB",	     wk1_std_func },
	{  4,  0x95, "PMTI",	     NULL,	     wk1_std_func },
	{  4,  0x96, "SPI",	     NULL,	     wk1_std_func },
	{  1,  0x97, "FULLP",	     NULL,	     wk1_std_func },
	{  1,  0x98, "HALFP",	     NULL,	     wk1_std_func },
	{ -1,  0x99, "PUREAVG",	     "AVERAGE",	     wk1_std_func },
	{ -1,  0x9A, "PURECOUNT",    "COUNT",	     wk1_std_func },
	{ -1,  0x9B, "PUREMAX",	     "MAX",	     wk1_std_func },
	{ -1,  0x9C, "PUREMIN",	     "MIN",	     wk1_std_func },
	{ -1,  0x9D, "PURESTD",	     "STDEVP",	     wk1_std_func },
	{ -1,  0x9E, "PUREVAR",	     "VARP",	     wk1_std_func },
	{ -1,  0x9F, "PURESTDS",     "STDEV",	     wk1_std_func },
	{ -1,  0xA0, "PUREVARS",     "VAR",	     wk1_std_func },
	{  3,  0xA1, "PMT2",	     NULL,	     wk1_std_func },
	{  3,  0xA2, "PV2",	     NULL,	     wk1_std_func },
	{  3,  0xA3, "FV2",	     NULL,	     wk1_std_func },
	{  3,  0xA4, "TERM2",	     NULL,	     wk1_std_func },
	/* Above 0xA6 is only by name or (unimplemented) "darwin"  */
	{  1,  0xA7, "FACT",	     "FACT",	     wk1_std_func },
	{  1,  0xAA, "SINH",	     "SINH",	     wk1_std_func },
	{  1,  0xAB, "ASINH",	     "ASINH",	     wk1_std_func },
	{  1,  0xAF, "GAMMALN",	     "GAMMALN",	     wk1_std_func },
	{  1,  0xB0, "GAMMA",	     NULL,	     wk1_std_func },
	{  2,  0xB1, "COMBIN",	     "COMBIN",	     wk1_std_func },
	{  1,  0xB2, "FACTLN",	     NULL,	     wk1_std_func },
	{  2,  0xB3, "BETA",	     "BETA",	     wk1_std_func },
	{  1,  0xB4, "COSH",	     "COSH",	     wk1_std_func },
	{  1,  0xB5, "TANH",	     "TANH",	     wk1_std_func },
	{  1,  0xB6, "SECH",	     NULL,	     wk1_std_func },
	{  1,  0xB7, "COTH",	     NULL,	     wk1_std_func },
	{  1,  0xB8, "ACOSH",	     "ACOSH",	     wk1_std_func },
	{  1,  0xB9, "ATANH",	     "ATANH",	     wk1_std_func },
	{  1,  0xBA, "ASECH",	     NULL,	     wk1_std_func },
	{  1,  0xBB, "ACSCH",	     NULL,	     wk1_std_func },
	{  1,  0xBC, "ACOTH",	     NULL,	     wk1_std_func },
	{ -1,  0xBE, "AVEDEV",	     "AVEDEV",	     wk1_std_func },
	{  2,  0xBF, "BESSELI",	     "BESSELI",	     wk1_std_func },
	{  2,  0xC0, "BESSELJ",	     "BESSELJ",	     wk1_std_func },
	{  2,  0xC1, "BESSELK",	     "BESSELK",	     wk1_std_func },
	{  2,  0xC2, "BESSELY",	     "BESSELY",	     wk1_std_func },
	{  3,  0xC3, "BETAI",	     NULL,	     wk1_std_func },
	{ -1,  0xCA, "DEVSQ",	     "DEVSQ",	     wk1_std_func },
	{  1,  0xCC, "ERFC",	     "ERFC",	     wk1_std_func },
	{ -1,  0xD2, "GEOMEAN",      "GEOMEAN",	     wk1_std_func },
	{ -1,  0xD3, "HARMEAN",      "HARMEAN",	     wk1_std_func },
	{ -1,  0xD3, "KURTOSIS",     "KURT",	     wk1_std_func },
	{ -1,  0xDB, "SKEWNESS",     "SKEW",	     wk1_std_func },
	{  1,  0xDC, "SQRTPI",	     "SQRTPI",	     wk1_std_func },
	{  1,  0xF2, "DEGTORAD",     "RADIANS",	     wk1_std_func },
	{ -2,  0xF3, "DPURECOUNT",   "DCOUNT",	     wk1_std_func },
	{  1,  0xF8, "RADTODEG",     "DEGREES",	     wk1_std_func },
	{  1,  0xFA, "SIGN",	     "SIGN",	     wk1_std_func },
	{ -1,  0xFB, "SUMSQ",	     "SUMSQ",	     wk1_std_func },
	{  1, 0x101, "EVEN",	     "EVEN",	     wk1_std_func },
	{  1, 0x103, "ODD",	     "ODD",	     wk1_std_func },
	{  1, 0x119, "ISEMPTY",	     "ISBLANK",	     wk1_std_func }
};

static GHashTable *lotus_funcname_to_info;
#define LOTUS_MAX_ORDINAL 0x119
static const LFuncInfo *lotus_ordinal_to_info[1 + LOTUS_MAX_ORDINAL] = {NULL};

static const LFuncInfo functions_works[] = {
	{  0,  0x1F, "NA",	     "NA",	     wk1_std_func },
	{  1,  0x20, "ERR",	     NULL,	     wk1_std_func },
	{  1,  0x21, "ABS",	     "ABS",	     wk1_std_func },
	{  1,  0x22, "INT",	     "TRUNC",	     wk1_std_func },
	{  1,  0x23, "SQRT",	     "SQRT",	     wk1_std_func },
	{  1,  0x24, "LOG",	     "LOG",	     wk1_std_func },
	{  1,  0x25, "LN",	     "LN",	     wk1_std_func },
	{  0,  0x26, "PI",	     "PI",	     wk1_std_func },
	{  1,  0x27, "SIN",	     "SIN",	     wk1_std_func },
	{  1,  0x28, "COS",	     "COS",	     wk1_std_func },
	{  1,  0x29, "TAN",	     "TAN",	     wk1_std_func },
	{  2,  0x2A, "ATAN2",	     "ATAN2",	     wk1_std_func },
	{  1,  0x2B, "ATAN",	     "ATAN",	     wk1_std_func },
	{  1,  0x2C, "ASIN",	     "ASIN",	     wk1_std_func },
	{  1,  0x2D, "ACOS",	     "ACOS",	     wk1_std_func },
	{  1,  0x2E, "EXP",	     "EXP",	     wk1_std_func },
	{  2,  0x2F, "MOD",	     "MOD",	     wk1_std_func },
	{ -1,  0x30, "CHOOSE",	     "CHOOSE",	     wk1_std_func },
	{  1,  0x31, "ISNA",	     "ISNA",	     wk1_std_func },
	{  1,  0x32, "ISERR",	     "ISERR",	     wk1_std_func },
	{  0,  0x33, "FALSE",	     "FALSE",	     wk1_std_func },
	{  0,  0x34, "TRUE",	     "TRUE",	     wk1_std_func },
	{  0,  0x35, "RAND",	     "RAND",	     wk1_std_func },
	{  3,  0x36, "DATE",	     "DATE",	     wk1_std_func },
	{  0,  0x37, "TODAY",	     "TODAY",	     wk1_std_func },
	{  3,  0x38, "PMT",	     "PMT",	     wk1_fin_func },
	{  3,  0x39, "PV",	     "PV",	     wk1_fin_func },
	{  3,  0x3A, "FV",	     "FV",	     wk1_fin_func },
	{  3,  0x3B, "IF",	     "IF",	     wk1_std_func },
	{  1,  0x3C, "DAY",	     "DAY",	     wk1_std_func },
	{  1,  0x3D, "MONTH",	     "MONTH",	     wk1_std_func },
	{  1,  0x3E, "YEAR",	     "YEAR",	     wk1_year_func },
	{  2,  0x3F, "ROUND",	     "ROUND",	     wk1_std_func },
	{  3,  0x40, "TIME",	     "TIME",	     wk1_std_func },
	{  1,  0x41, "HOUR",	     "HOUR",	     wk1_std_func },
	{  1,  0x42, "MINUTE",	     "MINUTE",	     wk1_std_func },
	{  1,  0x43, "SECOND",	     "SECOND",	     wk1_std_func },
	{  1,  0x44, "ISNUMBER",     "ISNONTEXT",    wk1_std_func },
	{  1,  0x45, "ISSTRING",     "ISTEXT",	     wk1_std_func },
	{  1,  0x46, "LENGTH",	     "LEN",	     wk1_std_func },
	{  1,  0x47, "VALUE",	     "VALUE",	     wk1_std_func },
	{  2,  0x48, "STRING",	     "FIXED",	     wk1_std_func },
	{  3,  0x49, "MID",	     "MID",	     wk1_std_func },
	{  1,  0x4A, "CHAR",	     "CHAR",	     wk1_std_func },
	{  1,  0x4B, "CODE",	     "CODE",	     wk1_std_func },
	{  3,  0x4C, "FIND",	     "FIND",	     wk1_find_func },
	{  1,  0x4D, "DATEVALUE",    "DATEVALUE",    wk1_std_func },
	{  1,  0x4E, "TIMEVALUE",    "TIMEVALUE",    wk1_std_func },
	{  1,  0x4F, "CELLPOINTER",  NULL,	     wk1_std_func },
	{ -1,  0x50, "SUM",	     "SUM",	     wk1_std_func },
	{ -1,  0x51, "AVG",	     "AVERAGEA",     wk1_std_func },
	{ -1,  0x52, "COUNT",	     "COUNTA",	     wk1_std_func },
	{ -1,  0x53, "MIN",	     "MINA",	     wk1_std_func },
	{ -1,  0x54, "MAX",	     "MAXA",	     wk1_std_func },
	{  3,  0x55, "VLOOKUP",	     "VLOOKUP",	     wk1_std_func },
	{  2,  0x56, "NPV",	     "NPV",	     wk1_std_func },
	{ -1,  0x57, "VAR",	     "VARPA",	     wk1_std_func },
	{ -1,  0x58, "STD",	     "STDEVPA",	     wk1_std_func },
	{  2,  0x59, "IRR",	     "IRR",	     wk1_fin_func },
	{  3,  0x5A, "HLOOKUP",	     "HLOOKUP",	     wk1_std_func },
	{ -2,  0x5B, "DSUM",	     "DSUM",	     wk1_std_func },
	{ -2,  0x5C, "DAVG",	     "DAVERAGE",     wk1_std_func },
	{ -2,  0x5D, "DCNT",	     "DCOUNTA",	     wk1_std_func },
	{ -2,  0x5E, "DMIN",	     "DMIN",	     wk1_std_func },
	{ -2,  0x5F, "DMAX",	     "DMAX",	     wk1_std_func },
	{ -2,  0x60, "DVAR",	     "DVARP",	     wk1_std_func },
	{ -2,  0x61, "DSTD",	     "DSTDEVP",	     wk1_std_func },
	{ -3,  0x62, "INDEX",	     "INDEX",	     wk1_std_func },
	{  1,  0x63, "COLS",	     "COLUMNS",	     wk1_std_func },
	{  1,  0x64, "ROWS",	     "ROWS",	     wk1_std_func },
	{  2,  0x65, "REPEAT",	     "REPT",	     wk1_std_func },
	{  1,  0x66, "UPPER",	     "UPPER",	     wk1_std_func },
	{  1,  0x67, "LOWER",	     "LOWER",	     wk1_std_func },
	{  2,  0x68, "LEFT",	     "LEFT",	     wk1_std_func },
	{  2,  0x69, "RIGHT",	     "RIGHT",	     wk1_std_func },
	{  4,  0x6A, "REPLACE",	     "REPLACE",	     wk1_std_func },
	{  1,  0x6B, "PROPER",	     "PROPER",	     wk1_std_func },
	{  2,  0x6C, "CELL",	     "CELL",	     wk1_std_func },
	{  1,  0x6D, "TRIM",	     "TRIM",	     wk1_std_func },
	{  1,  0x6E, "CLEAN",	     "CLEAN",	     wk1_std_func },
	{  1,  0x6F, "S",	     "T",	     wk1_std_func },
	{  1,  0x70, "N",	     "N",	     wk1_std_func },
	{  2,  0x71, "EXACT",	     "EXACT",	     wk1_std_func },
	{  1,  0x72, "APP",	     NULL,	     wk1_std_func },
	{  1,  0x73, "@",	     "INDIRECT",     wk1_std_func },
	{  3,  0x74, "RATE",	     "RATE",	     wk1_rate_func },
	{  3,  0x75, "TERM",	     NULL,	     wk1_std_func },
	/* TERM ($1,$2,$3) is NPER ($2,$1,0,$3) */
	{  3,  0x76, "CTERM",	     NULL,	     wk1_nper_func },
	/* CTERM ($1,$2,$3) is NPER ($1,0,$3,$2) */
	{  3,  0x77, "SLN",	     "SLN",	     wk1_std_func },
	{  4,  0x78, "SYD",	     "SYD",	     wk1_std_func },
	{  4,  0x79, "DDB",	     "DDB",	     wk1_std_func },
	/* 0x7A is SPLFUNC which needs special handling */

	/* This is obviously incomplete */
	{  -1,  0x8D, "AND",	     "AND",	     wk1_std_func },
	{  -1,  0x8E, "OR",	     "OR",	     wk1_std_func }
};

static GHashTable *works_funcname_to_info;
#define WORKS_MAX_ORDINAL 0x8E
static const LFuncInfo *works_ordinal_to_info[1 + WORKS_MAX_ORDINAL] = {NULL};


static void
parse_list_push_expr (GnmExprList **list, GnmExpr const *pd)
{
	g_return_if_fail (pd != NULL);
	*list = gnm_expr_list_prepend (*list, pd) ;
}

static void
parse_list_push_value (GnmExprList **list, GnmValue *v)
{
	parse_list_push_expr (list, gnm_expr_new_constant (v));
}

static GnmExpr const *
parse_list_pop (GnmExprList **list, const GnmParsePos *orig)
{
	GnmExprList *tmp = *list;
	if (tmp != NULL) {
		GnmExpr const *ans = tmp->data ;
		*list = g_slist_remove (*list, ans) ;
		return ans ;
	}

	g_warning ("%s: Incorrect number of parsed formula arguments",
		   cell_coord_name (orig->eval.col, orig->eval.row));
	return gnm_expr_new_constant (value_new_error_REF (NULL));
}

/**
 * Returns a new list composed of the last n items pop'd off the list.
 **/
static GnmExprList *
parse_list_last_n (GnmExprList **list, gint n, const GnmParsePos *orig)
{
	GnmExprList *l = NULL;
	while (n-- > 0)
		l = gnm_expr_list_prepend (l, parse_list_pop (list, orig));
	return l;
}

static GnmFunc *
lotus_placeholder (char const *lname)
{
	char *gname = g_strconcat ("LOTUS_", lname, NULL);
	GnmFunc *func = gnm_func_lookup (gname, NULL);
	if (!func)
		func = gnm_func_add_placeholder (NULL, gname, "Lotus");
	g_free (gname);
	return func;
}

static const GnmExpr *
lotus_negate (const GnmExpr *e)
{
	const GnmExpr *res;

	if (GNM_EXPR_GET_OPER (e) == GNM_EXPR_OP_UNARY_NEG) {
		res = gnm_expr_copy (e->unary.value);
		gnm_expr_free (e);
	} else {
		res = gnm_expr_new_unary (GNM_EXPR_OP_UNARY_NEG, e);
	}

	return res;
}

static int
wk1_std_func (GnmExprList **stack, LFuncInfo const *f,
	      guint8 const *data, const GnmParsePos *orig)
{
	GnmFunc *func = f->gnumeric_name
		? gnm_func_lookup (f->gnumeric_name, NULL)
		: NULL;
	int numargs, size;

	if (f->args < 0) {
		numargs = data[1];
		size = 2;
	} else {
		numargs = f->args;
		size = 1;
	}

	if (!func)
		func = lotus_placeholder (f->lotus_name);

	parse_list_push_expr (stack, gnm_expr_new_funcall (func,
		parse_list_last_n (stack, numargs, orig)));

	return size;
}
static int
wk1_nper_func (GnmExprList **stack, LFuncInfo const *func,
	       guint8 const *data, const GnmParsePos *orig)
{
	/* a,b,c -> a,,-c,b */
	return wk1_std_func (stack, func, data, orig);
}

static int
wk1_year_func (GnmExprList **stack, LFuncInfo const *func,
	       guint8 const *data, const GnmParsePos *orig)
{
	/* year - 1900 */
	return wk1_std_func (stack, func, data, orig);
}

static int
wk1_find_func (GnmExprList **stack, LFuncInfo const *func,
	       guint8 const *data, const GnmParsePos *orig)
{
	/* find - 1 */
	return wk1_std_func (stack, func, data, orig);
}

static int
wk1_fin_func (GnmExprList **stack, LFuncInfo const *f,
	      guint8 const *data, const GnmParsePos *orig)
{
	GnmFunc *func;
	GnmExprList *largs, *gargs;
	const GnmExpr *expr;

	g_assert (f->gnumeric_name != NULL);
	g_assert (f->args > 0);

	func = gnm_func_lookup (f->gnumeric_name, NULL);
	if (!func)
		func = lotus_placeholder (f->lotus_name);

	largs = parse_list_last_n (stack, f->args, orig);
	switch (f->ordinal) {
	case 0x38: case 0x39: case 0x3A: {
		/* FV/PV/PMT: a,b,c -> b,c,-a */
		largs->data = (gpointer)lotus_negate (largs->data);
		gargs = largs->next;
		largs->next = NULL;
		gargs->next->next = largs;
		break;
	}
	case 0x59:
		/* IRR: a,b -> b,a */
		gargs = g_slist_reverse (largs);
		break;
	default:
		g_assert_not_reached ();
	}
	expr = gnm_expr_new_funcall (func, gargs);

	parse_list_push_expr (stack, expr);
	return 1;
}

static int
wk1_rate_func (GnmExprList **stack, LFuncInfo const *func,
	       guint8 const *data, const GnmParsePos *orig)
{
	return wk1_std_func (stack, func, data, orig);
}

static gint32
make_function (LotusState *state, GnmExprList **stack, guint8 const *data, const GnmParsePos *orig)
{
	/* This is ok as we have more than 256 entries.  */
	LFuncInfo const *f = NULL;

	if (state->is_works) {
		if (*data <= WORKS_MAX_ORDINAL)
			f = works_ordinal_to_info[*data];
	} else {
		if (*data <= LOTUS_MAX_ORDINAL)
			f = lotus_ordinal_to_info[*data];
	}

	if (f == NULL) {
		g_warning ("%s: unknown PTG 0x%x",
			   cell_coord_name (orig->eval.col, orig->eval.row),
			   *data);
		return 1;
	}

	return (f->handler) (stack, f, data, orig);
}

static void
get_cellref (GnmCellRef *ref, guint8 const *dataa, guint8 const *datab,
	     const GnmParsePos *orig)
{
	guint16 i;
	GnmSheetSize const *ss = gnm_sheet_get_size (orig->sheet);

	ref->sheet = NULL;

	i = GSF_LE_GET_GUINT16 (dataa);
	ref->col_relative = (i & 0x8000) != 0;
	ref->col = (i & 0xfff) % ss->max_cols;
	if (ref->col_relative && (i & 0x1000))
		ref->col = -ref->col;

	i = GSF_LE_GET_GUINT16 (datab);
	ref->row_relative = (i & 0x8000) != 0;
	ref->row = (i & 0xfff) % ss->max_rows;
	if (ref->row_relative && (i & 0x1000))
		ref->row = -ref->row;

#if FORMULA_DEBUG > 0
	g_printerr ("0x%x 0x%x -> (%d, %d)\n",
		    GSF_LE_GET_GUINT16 (dataa),
		    GSF_LE_GET_GUINT16 (datab),
		    ref->col, ref->row);
#endif
}

#define HANDLE_BINARY(op)						\
  {									\
	GnmExpr const *r = parse_list_pop (&stack, orig);		\
	GnmExpr const *l = parse_list_pop (&stack, orig);		\
	parse_list_push_expr (&stack, gnm_expr_new_binary (l, op, r));	\
        i++;								\
        break;								\
  }

#define HANDLE_UNARY(op)						\
  {									\
	GnmExpr const *a = parse_list_pop (&stack, orig);		\
	parse_list_push_expr (&stack, gnm_expr_new_unary (op, a));	\
        i++;								\
        break;								\
  }

static void
handle_named_func (GnmExprList **stack, GnmParsePos const *orig,
		   char const *gname, char const *lname, int args)
{
	GnmFunc *func = gnm_func_lookup (gname, NULL);
	if (!func) {
		g_assert (lname != NULL);
		func = lotus_placeholder (lname);
	}

	parse_list_push_expr (stack, gnm_expr_new_funcall (func,
		parse_list_last_n (stack, args, orig)));
}


static GnmExprTop const *
lotus_parse_formula_old (LotusState *state, GnmParsePos *orig,
			 guint8 const *data, guint32 len)
{
	GnmExprList *stack = NULL;
	guint     i;
	GnmCellRef   a, b;
	gboolean done = FALSE;
	GnmExprTop const *res;

	for (i = 0; (i < len) && !done;) {
		switch (data[i]) {
		case LOTUS_FORMULA_CONSTANT:
			parse_list_push_value (&stack,
				value_new_float (gsf_le_get_double (data + i + 1)));
			i += 9;
			break;

		case LOTUS_FORMULA_VARIABLE:
			get_cellref (&a, data + i + 1, data + i + 3, orig);
			parse_list_push_expr (&stack, gnm_expr_new_cellref (&a));
			i += 5;
			break;

		case LOTUS_FORMULA_RANGE:
			get_cellref (&a, data + i + 1, data + i + 3, orig);
			get_cellref (&b, data + i + 5, data + i + 7, orig);
			parse_list_push_value (&stack,
				value_new_cellrange (&a, &b, orig->eval.col, orig->eval.row));
			i += 9;
			break;

		case LOTUS_FORMULA_RETURN:
			done = TRUE;
			break;

		case LOTUS_FORMULA_BRACKET:
			i += 1; /* Ignore */
			break;

		case LOTUS_FORMULA_INTEGER:
			parse_list_push_value (&stack,
					       value_new_int (GSF_LE_GET_GINT16 (data + i + 1)));
			i += 3;
			break;

		case LOTUS_FORMULA_STRING:
			parse_list_push_value (&stack,
				lotus_new_string (data + i + 1, state->lmbcs_group));
			i += 2 + strlen (data + i + 1);
			break;

		/* Note: ordinals differ between versions.  */
		case 0x08: HANDLE_UNARY (GNM_EXPR_OP_UNARY_NEG);
		case 0x09: HANDLE_BINARY (GNM_EXPR_OP_ADD);
		case 0x0A: HANDLE_BINARY (GNM_EXPR_OP_SUB);
		case 0x0B: HANDLE_BINARY (GNM_EXPR_OP_MULT);
		case 0x0C: HANDLE_BINARY (GNM_EXPR_OP_DIV);
		case 0x0D: HANDLE_BINARY (GNM_EXPR_OP_EXP);
		case 0x0E: HANDLE_BINARY (GNM_EXPR_OP_EQUAL);
		case 0x0F: HANDLE_BINARY (GNM_EXPR_OP_NOT_EQUAL);
		case 0x10: HANDLE_BINARY (GNM_EXPR_OP_LTE);
		case 0x11: HANDLE_BINARY (GNM_EXPR_OP_GTE);
		case 0x12: HANDLE_BINARY (GNM_EXPR_OP_LT);
		case 0x13: HANDLE_BINARY (GNM_EXPR_OP_GT);
		case 0x14:
			/* FIXME: Check if we need bit version.  */
			handle_named_func (&stack, orig, "AND", NULL, 2);
			i++;
			break;
		case 0x15:
			/* FIXME: Check if we need bit version.  */
			handle_named_func (&stack, orig, "OR", NULL, 2);
			i++;
			break;
		case 0x16:
			/* FIXME: Check if we need bit version.  */
			handle_named_func (&stack, orig, "NOT", NULL, 1);
			i++;
			break;
		case 0x17: HANDLE_UNARY (GNM_EXPR_OP_UNARY_PLUS);
		/* Works */
		case 0x18: HANDLE_BINARY (GNM_EXPR_OP_CAT);

		default:
			i += make_function (state, &stack, data + i, orig);
		}
	}

	res = stack ? gnm_expr_top_new (parse_list_pop (&stack, orig)) : NULL;
	if (stack) {
		g_warning ("%s: args remain on stack",
			   cell_coord_name (orig->eval.col, orig->eval.row));
		while (stack)
			gnm_expr_free (parse_list_pop (&stack, orig));
	}

	return res;
}

static void
get_new_cellref (GnmCellRef *dst, int relbits, const guint8 *data,
		 const GnmParsePos *orig)
{
	dst->row = GSF_LE_GET_GUINT16 (data);
	dst->sheet = lotus_get_sheet (orig->sheet->workbook, data[2]);
	dst->col = data[3];

	dst->row_relative = (relbits & 1) != 0;
	if (dst->row_relative)
		dst->row -= orig->eval.row;

	dst->col_relative = (relbits & 2) != 0;
	if (dst->col_relative)
		dst->col -= orig->eval.col;
}

static GnmExprTop const *
lotus_parse_formula_new (LotusState *state, GnmParsePos *orig,
			 guint8 const *data, guint32 len)
{
	GnmExprList *stack = NULL;
	guint i;
	gboolean done = FALSE;
	gboolean uses_snum = (state->version <= LOTUS_VERSION_123V4);
	GnmExprTop const *res;

	for (i = 0; i < len && !done;) {
		switch (data[i]) {
		case LOTUS_FORMULA_CONSTANT:
			parse_list_push_value (&stack,
				lotus_load_treal (data + i + 1));
			i += 11;
			break;

		case LOTUS_FORMULA_VARIABLE: {
			GnmCellRef a;
			get_new_cellref (&a, data[1] & 7, data + i + 2, orig);
			if (a.sheet == orig->sheet)
				a.sheet = NULL;
			parse_list_push_expr (&stack, gnm_expr_new_cellref (&a));
			i += 6;
			break;
		}

		case LOTUS_FORMULA_RANGE: {
			GnmCellRef a, b;
			get_new_cellref (&a, data[1] & 7, data + i + 2, orig);
			get_new_cellref (&b, (data[1] >> 3) & 7, data + i + 6, orig);
			if (b.sheet == a.sheet)
				b.sheet = NULL;
			if (a.sheet == orig->sheet && b.sheet == NULL)
				a.sheet = NULL;
			parse_list_push_value
				(&stack,
				 value_new_cellrange (&a, &b,
						      orig->eval.col,
						      orig->eval.row));
			i += 10;
			break;
		}

		case LOTUS_FORMULA_RETURN:
			done = TRUE;
			break;

		case LOTUS_FORMULA_BRACKET:
			i += 1; /* Ignore */
			break;

		case LOTUS_FORMULA_PACKED_NUMBER: {
			GnmValue *val;
			if (uses_snum) {
				val = lotus_smallnum (GSF_LE_GET_GUINT16 (data + i + 1));
				i += 3;
			} else {
				val = lotus_unpack_number (GSF_LE_GET_GUINT32 (data + i + 1));
				i += 5;
			}

			parse_list_push_value (&stack, val);
			break;
		}

		case LOTUS_FORMULA_STRING:
			parse_list_push_value (&stack,
				lotus_new_string (data + i + 1, state->lmbcs_group));
			i += 2 + strlen (data + i + 1);
			break;

		case LOTUS_FORMULA_NAMED:
		case LOTUS_FORMULA_ABS_NAMED:
			g_warning ("Named ranges not implemented.");
			i += 1;  /* Guess */
			break;

		case LOTUS_FORMULA_ERR_RREF:
			parse_list_push_value (&stack,
					       value_new_error_REF (NULL));
			i += 5;
			break;

		case LOTUS_FORMULA_ERR_CREF:
			parse_list_push_value (&stack,
					       value_new_error_REF (NULL));
			i += 6;
			break;


		case LOTUS_FORMULA_ERR_CONSTANT:
			parse_list_push_value (&stack,
					       value_new_error_VALUE (NULL));
			i += 12;
			break;

		case LOTUS_FORMULA_OP_NEG: HANDLE_UNARY (GNM_EXPR_OP_UNARY_NEG);
		case LOTUS_FORMULA_OP_PLU: HANDLE_BINARY (GNM_EXPR_OP_ADD);
		case LOTUS_FORMULA_OP_MNS: HANDLE_BINARY (GNM_EXPR_OP_SUB);
		case LOTUS_FORMULA_OP_MUL: HANDLE_BINARY (GNM_EXPR_OP_MULT);
		case LOTUS_FORMULA_OP_DIV: HANDLE_BINARY (GNM_EXPR_OP_DIV);
		case LOTUS_FORMULA_OP_POW: HANDLE_BINARY (GNM_EXPR_OP_EXP);
		case LOTUS_FORMULA_OP_EQ: HANDLE_BINARY (GNM_EXPR_OP_EQUAL);
		case LOTUS_FORMULA_OP_NE: HANDLE_BINARY (GNM_EXPR_OP_NOT_EQUAL);
		case LOTUS_FORMULA_OP_LE: HANDLE_BINARY (GNM_EXPR_OP_LTE);
		case LOTUS_FORMULA_OP_GE: HANDLE_BINARY (GNM_EXPR_OP_GTE);
		case LOTUS_FORMULA_OP_LT: HANDLE_BINARY (GNM_EXPR_OP_LT);
		case LOTUS_FORMULA_OP_GT: HANDLE_BINARY (GNM_EXPR_OP_GT);
		case LOTUS_FORMULA_OP_UPLU: HANDLE_UNARY (GNM_EXPR_OP_UNARY_PLUS);
		case LOTUS_FORMULA_OP_CAT: HANDLE_BINARY (GNM_EXPR_OP_CAT);

		case LOTUS_FORMULA_OP_AND:
			/* FIXME: Check if we need bit versions.  */
			handle_named_func (&stack, orig, "AND", NULL, 2);
			i++;
			break;
		case LOTUS_FORMULA_OP_OR:
			handle_named_func (&stack, orig, "OR", NULL, 2);
			i++;
			break;
		case LOTUS_FORMULA_OP_NOT:
			handle_named_func (&stack, orig, "NOT", NULL, 1);
			i++;
			break;

		case LOTUS_FORMULA_SPLFUNC: {
			int args = data[i + 1];
			int fnamelen = GSF_LE_GET_GUINT16 (data + i + 2);
			char *name = lotus_get_lmbcs (data + (i + 4),
						      len - (i + 4),
						      state->lmbcs_group);
			size_t namelen;
			char *p;
			const LFuncInfo *f;

			if (name == NULL)
				name = g_strdup ("bogus");

			/* Get rid of the '(' in the name.  */
			namelen = strlen (name);
			if (namelen && name[namelen - 1] == '(')
				name[--namelen] = 0;
			/* There is a weird prefix -- ignore it.  */
			for (p = name + namelen; p > name; p--)
				if (!g_ascii_isalnum (p[-1]))
					break;

			f = g_hash_table_lookup ((state->is_works) ?
						 works_funcname_to_info
						 : lotus_funcname_to_info, p);
			handle_named_func (&stack, orig,
					   f ? f->gnumeric_name : "",
					   p,
					   args);

			g_free (name);
			i += 4 + fnamelen;
			break;
		}

		default:
			i += make_function (state, &stack, data + i, orig);
		}
	}

	res = stack ? gnm_expr_top_new (parse_list_pop (&stack, orig)) : NULL;
	if (stack) {
		g_warning ("%s: args remain on stack",
			   cell_coord_name (orig->eval.col, orig->eval.row));
		while (stack)
			gnm_expr_free (parse_list_pop (&stack, orig));
	}

	return res;
}


GnmExprTop const *
lotus_parse_formula (LotusState *state, GnmParsePos *pos,
		     guint8 const *data, guint32 len)
{
	const GnmExprTop *result = (state->version >= LOTUS_VERSION_123V4)
		? lotus_parse_formula_new (state, pos, data, len)
		: lotus_parse_formula_old (state, pos, data, len);

	if (!result)
		result = gnm_expr_top_new_constant (value_new_error_VALUE (NULL));

#if FORMULA_DEBUG > 0
	{
		char *txt = gnm_expr_top_as_string (result, pos, gnm_conventions_default);
		g_printerr ("Lotus: %s!%s: %s\n",
			    pos->sheet->name_unquoted,
			    cell_coord_name (pos->eval.col, pos->eval.row),
			    txt);
		g_free (txt);
	}
#endif

	return result;
}


void
lotus_formula_init (void)
{
	unsigned i;

	lotus_funcname_to_info = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; i < G_N_ELEMENTS (functions_lotus); i++) {
		const LFuncInfo *f = functions_lotus + i;
#if 1
		g_assert (f->ordinal < G_N_ELEMENTS (lotus_ordinal_to_info));
		if (f->gnumeric_name &&
		    !gnm_func_lookup (f->gnumeric_name, NULL)) {
			g_printerr ("Lotus function @%s maps to unknown function %s.\n",
				    f->lotus_name,
				    f->gnumeric_name);
		}
#endif
		if (f->ordinal <= LOTUS_MAX_ORDINAL)
			lotus_ordinal_to_info[f->ordinal] = f;
		g_hash_table_insert (lotus_funcname_to_info,
				     (gpointer)f->lotus_name,
				     (gpointer)f);
	}

	works_funcname_to_info = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; i < G_N_ELEMENTS (functions_works); i++) {
		const LFuncInfo *f = functions_works + i;
#if 1
		g_assert (f->ordinal < G_N_ELEMENTS (lotus_ordinal_to_info));
		if (f->gnumeric_name &&
		    !gnm_func_lookup (f->gnumeric_name, NULL)) {
			g_printerr ("Works function @%s maps to unknown function %s.\n",
				    f->lotus_name,
				    f->gnumeric_name);
		}
#endif

		if (f->ordinal <= WORKS_MAX_ORDINAL)
			works_ordinal_to_info[f->ordinal] = f;
		g_hash_table_insert (works_funcname_to_info,
				     (gpointer)f->lotus_name,
				     (gpointer)f);
	}

}

void
lotus_formula_shutdown (void)
{
	g_hash_table_destroy (lotus_funcname_to_info);
	g_hash_table_destroy (works_funcname_to_info);
}
