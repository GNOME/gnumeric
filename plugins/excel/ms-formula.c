/*
 * ms-formula.c: MS Excel -> Gnumeric formula conversion
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 */

#include <fcntl.h>
#include <assert.h>
#include <config.h>
#include <stdio.h>
#include <gnome.h>
#include "gnumeric.h"

#include "utils.h"

#include "ms-excel.h"
#include "ms-excel-biff.h"
#include "ms-formula.h"

#define NO_PRECEDENCE 256

/**
 * Various bits of data for operators
 * see S59E2B.HTM for formula_ptg values
 * formula PTG, prefix, middle, suffix, precedence
 **/
FORMULA_OP_DATA formula_op_data[] = {
  /* Binary operator tokens */
  { 0x03, 1, "+",  30  }, /* ptgAdd : Addition */
  { 0x04, 1, "-",  30  }, /* ptgSub : Subtraction */
  { 0x05, 1, "*",  48 }, /* ptgMul : Multiplication */
  { 0x06, 1, "/",  32 }, /* ptgDiv : Division */
  { 0x07, 1, "^",  60 }, /* ptgPower : Exponentiation */
  { 0x08, 1, "&",  28 }, /* ptgConcat : Concatenation */
  { 0x09, 1, "<",  24 }, /* ptgLT : Less Than */
  { 0x0a, 1, "<=", 24 }, /* ptgLTE : Less Than or Equal */
  { 0x0b, 1, "=",  20 }, /* ptgEQ : Equal */
  { 0x0c, 1, ">=", 24 }, /* ptgGTE : Greater Than or Equal */
  { 0x0d, 1, ">" , 24 }, /* ptgGT : Greater Than */
  { 0x0e, 1, "<>", 20 }, /* ptgNE : Not Equal */
  { 0x0f, 1, " " , 62 }, /* ptgIsect : Intersection */
  { 0x10, 1, "," , 62 }, /* ptgUnion : Union */
  { 0x11, 1, ":" , 63 }, /* ptgRange : Range */
  /* Unary operator tokens */
  { 0x12, 0, "+",  64 }, /* ptgUplus : Unary Plus */
  { 0x13, 0, "-",  64 }, /* ptgUminux : Unary Minus */
  { 0x14, 0, "%",  64 }  /* ptgPercent : Percent Sign */
} ;
#define FORMULA_OP_DATA_LEN   (sizeof(formula_op_data)/sizeof(FORMULA_OP_DATA))

/* FIXME: the standard function indexes need to be found from xlcall.h */
/**
 * Various bits of data for functions
 * function index, prefix, middle, suffix, multiple args?, precedence
 **/
FORMULA_FUNC_DATA formula_func_data[] =
{
  { 0x00, "COUNT (", 0, ")", 2, 0 },
  { 0x01, "0x1 (", 0, ")", 8, 0 },
  { 0x02, "ISNA (", 0, ")", 1, 0 },
  { 0x03, "ISERROR (", 0, ")", 1, 0 },
  { 0x04, "SUM (", 0,  ")", -1, 0 },
  { 0x05, "AVERAGE (", 0,  ")", -1, 0 },
  { 0x06, "MIN (", 0,  ")", -1, 0 },
  { 0x07, "MAX (", 0,  ")", -1, 0 },
  { 0x08, "0x8 (", 0, ")", 8, 0 },
  { 0x09, "COLUMN (", 0, ")", -1, 0 },
  { 0x0a, "0xa (", 0, ")", 8, 0 },
  { 0x0b, "0xb (", 0, ")", 8, 0 },
  { 0x0c, "STDEV (", 0, ")", -1, 0 },
  { 0x0d, "DOLLAR (", 0, ")", 1, 0 },
  { 0x0e, "FIXED (", 0, ")", 1, 0 },
  { 0x0f, "SIN (", 0, ")", 1, 0 },
  { 0x10, "COS (", 0, ")", 1, 0 },
  { 0x11, "TAN (", 0, ")", 1, 0 },
  { 0x12, "ATAN (", 0, ")", 1, 0 },
  { 0x14, "SQRT (", 0, ")", 1, 0 },
  { 0x15, "EXP (", 0, ")", 1, 0 },
  { 0x16, "LN (", 0, ")", 1, 0 },
  { 0x17, "LOG10 (", 0, ")", 1, 0 },
  { 0x1a, "SIGN (", 0, ")", 1, 0 },
  { 0x1b, "0x1b (", 0, ")", 8, 0 },
  { 0x1c, "0x1c (", 0, ")", 8, 0 },
  { 0x1d, "0x1d (", 0, ")", 8, 0 },
  { 0x1e, "REPT (", 0, ")", 2, 0 },
  { 0x1f, "MID (", 0, ")", 3, 0 },
  { 0x20, "LEN (", 0,  ")", 1, 0 },
  { 0x21, "VALUE (", 0,  ")", 1, 0 },
  { 0x22, "0x22 (", 0, ")", 8, 0 },
  { 0x23, "0x23 (", 0, ")", 8, 0 },
  { 0x24, "AND (", 0,  ")", -1, 0 },
  { 0x25, "OR (",  0,  ")", -1, 0 },
  { 0x26, "NOT (",  0,  ")", 1, 0 },
  { 0x27, "MOD (", 0, ")", 2, 0 },
  { 0x28, "0x28 (", 0, ")", 8, 0 },
  { 0x29, "0x29 (", 0, ")", 8, 0 },
  { 0x2a, "0x2a (", 0, ")", 8, 0 },
  { 0x2b, "0x2b (", 0, ")", 8, 0 },
  { 0x2c, "0x2c (", 0, ")", 8, 0 },
  { 0x2d, "0x2d (", 0, ")", 8, 0 },
  { 0x2e, "VAR (", 0, ")", -1, 0 },
  { 0x30, "REPLACE (", 0, ")", 2, 0 },
  { 0x31, "LINEST (", 0, ")", 2, 0 },
  { 0x32, "TREND (", 0, ")", 4, 0 },
  { 0x33, "LOGEST (", 0, ")", 1, 0 },
  { 0x34, "GROWTH (", 0, ")", -1, 0 },
  { 0x35, "0x35 (", 0, ")", 8, 0 },
  { 0x36, "0x36 (", 0, ")", 8, 0 },
  { 0x37, "0x37 (", 0, ")", 8, 0 },
  { 0x38, "0x38 (", 0, ")", 8, 0 },
  { 0x39, "0x39 (", 0, ")", 8, 0 },
  { 0x3a, "0x3a (", 0, ")", 8, 0 },
  { 0x3b, "0x3b (", 0, ")", 8, 0 },
  { 0x3c, "0x3c (", 0, ")", 8, 0 },
  { 0x3d, "0x3d (", 0, ")", 8, 0 },
  { 0x3e, "0x3e (", 0, ")", 8, 0 },
  { 0x3f, "RAND (", 0, ")", 0, 0 },
  { 0x40, "0x40 (", 0, ")", 8, 0 },
  { 0x41, "DATE (", 0, ")", 3, 0 },
  { 0x42, "TIME (", 0, ")", 3, 0 },
  { 0x43, "DAY (", 0, ")", 1, 0 },
  { 0x44, "MONTH (", 0, ")", 1, 0 },
  { 0x45, "YEAR (", 0, ")", 1, 0 },
  { 0x46, "WEEKDAY (", 0, ")", 1, 0 },
  { 0x47, "HOUR (", 0, ")", 1, 0 },
  { 0x48, "MINUTE (", 0, ")", 1, 0 },
  { 0x49, "SECOND (", 0, ")", 1, 0 },
  { 0x4a, "NOW (", 0, ")", 0, 0 },
  { 0x4b, "AREAS (", 0, ")", 1, 0 },
  { 0x4c, "0x4c (", 0, ")", 8, 0 },
  { 0x4d, "COLUMNS (", 0, ")", 1, 0 },
  { 0x4e, "OFFSET (", 0, ")", -1, 0 },
  { 0x4f, "0x4f (", 0, ")", 8, 0 },
  { 0x50, "0x50 (", 0, ")", 8, 0 },
  { 0x51, "0x51 (", 0, ")", 8, 0 },
  { 0x52, "SEARCH (", 0, ")", 3, 0 },
  { 0x53, "0x53 (", 0, ")", 8, 0 },
  { 0x54, "0x54 (", 0, ")", 8, 0 },
  { 0x55, "0x55 (", 0, ")", 8, 0 },
  { 0x56, "TYPE (", 0, ")", 1, 0 },
  { 0x57, "0x57 (", 0, ")", 8, 0 },
  { 0x58, "0x58 (", 0, ")", 8, 0 },
  { 0x59, "0x59 (", 0, ")", 8, 0 },
  { 0x5a, "0x5a (", 0, ")", 8, 0 },
  { 0x5b, "0x5b (", 0, ")", 8, 0 },
  { 0x5c, "0x5c (", 0, ")", 8, 0 },
  { 0x5d, "0x5d (", 0, ")", 8, 0 },
  { 0x5e, "0x5e (", 0, ")", 8, 0 },
  { 0x5f, "0x5f (", 0, ")", 8, 0 },
  { 0x60, "0x60 (", 0, ")", 8, 0 },
  { 0x61, "ATAN2 (", 0, ")", 2, 0 },
  { 0x62, "ASIN (", 0, ")", 1, 0 },
  { 0x63, "ACOS (", 0, ")", 1, 0 },
  { 0x64, "0x64 (", 0, ")", 8, 0 },
  { 0x65, "0x65 (", 0, ")", 8, 0 },
  { 0x65, "HLOOKUP (", 0, ")", -1, 0 },
  { 0x66, "VLOOKUP (", 0, ")", -1, 0 },
  { 0x67, "0x67 (", 0, ")", 8, 0 },
  { 0x68, "0x68 (", 0, ")", 8, 0 },
  { 0x69, "ISREF (", 0, ")", 1, 0 },
  { 0x6a, "0x6a (", 0, ")", 8, 0 },
  { 0x6b, "0x6b (", 0, ")", 8, 0 },
  { 0x6c, "0x6c (", 0, ")", 8, 0 },
  { 0x6d, "LOG (", 0, ")", 1, 0 },
  { 0x6e, "0x6e (", 0, ")", 8, 0 },
  { 0x6f, "CHAR (", 0, ")", 1, 0 },
  { 0x70, "LOWER (",   0, ")", 1, 0 },
  { 0x71, "UPPER (",   0, ")", 1, 0 },
  { 0x72, "PROPER (",   0, ")", 1, 0 },
  { 0x73, "LEFT (",   0, ")", 2, 0 },
  { 0x74, "RIGHT (",   0, ")", 2, 0 },
  { 0x75, "EXACT (",   0, ")", 2, 0 },
  { 0x76, "TRIM (",   0, ")", 1, 0 },
  { 0x77, "0x77 (",   0, ")", 2, 0 },
  { 0x78, "SUBSTITUTE (",   0, ")", -1, 0 },
  { 0x79, "CODE (",   0, ")", 1, 0 },
  { 0x7a, "0x7a (", 0, ")", 8, 0 },
  { 0x7b, "0x7b (", 0, ")", 8, 0 },
  { 0x7c, "FIND (",   0, ")", -1, 0 },
  { 0x7d, "CELL (",   0, ")", 2, 0 },
  { 0x7e, "ISERR (", 0, ")", 1, 0 },
  { 0x7f, "ISTEXT (", 0, ")", 1, 0 },
  { 0x80, "ISNUMBER (", 0, ")", 1, 0 },
  { 0x81, "ISBLANK (", 0, ")", 1, 0 },
  { 0x82, "T (", 0, ")", 1, 0 },
  { 0x83, "N (", 0, ")", 1, 0 },
  { 0x84, "0x84 (", 0, ")", 8, 0 },
  { 0x85, "0x85 (", 0, ")", 8, 0 },
  { 0x86, "0x86 (", 0, ")", 8, 0 },
  { 0x87, "0x87 (", 0, ")", 8, 0 },
  { 0x88, "0x88 (", 0, ")", 8, 0 },
  { 0x89, "0x89 (", 0, ")", 8, 0 },
  { 0x8a, "0x8a (", 0, ")", 8, 0 },
  { 0x8b, "0x8b (", 0, ")", 8, 0 },
  { 0x8c, "DATEVALUE (", 0, ")", 1, 0 },
  { 0x8d, "0x8d (", 0, ")", 8, 0 },
  { 0x8e, "0x8e (", 0, ")", 8, 0 },
  { 0x8f, "0x8f (", 0, ")", 8, 0 },
  { 0xa2, "CLEAN (", 0, ")", 1, 0 },
  { 0xa9, "COUNTA (", 0, ")", -1, 0 },
  { 0xbe, "ISNONTEXT (", 0, ")", 1, 0 },
  { 0xc1, "STDEVP (", 0, ")", -1, 0 },
  { 0xc2, "VARP (", 0, ")", -1, 0 },
  { 0xc5, "TRUNC (", 0, ")", 1, 0 },
  { 0xc6, "ISLOGICAL (", 0, ")", 1, 0 },
  { 0xdb, "ADDRESS (", 0, ")", -1, 0 },
  { 0xdc, "DAYS360 (", 0, ")", 2, 0 },
  { 0xdd, "TODAY (", 0, ")", 0, 0 },
  { 0xe3, "MEDIAN (", 0, ")", -1, 0 },
  { 0xe5, "SINH (", 0, ")", 1, 0 },
  { 0xe6, "COSH (", 0, ")", 1, 0 },
  { 0xe7, "TANH (", 0, ")", 1, 0 },
  { 0xe8, "ASINH (", 0, ")", 1, 0 },
  { 0xe9, "ACOSH (", 0, ")", 1, 0 },
  { 0xea, "ATANH (", 0, ")", 1, 0 },
  { 0xf4, "INFO (", 0, ")", 1, 0 },
  { 0xfc, "FREQUENCY (", 0, ")", 2, 0 },
  { 0xff, "MAGIC (", 0, ")", -1, 0 }, /* Dodgy Special Case ! */
  { 0x105, "ERROR.TYPE (", 0, ")", 1, 0 },
  { 0x10d, "AVEDEV (", 0, ")", -1, 0 },
  { 0x10e, "BETADIST (", 0, ")", 3, 0 },
  { 0x10f, "GAMMALN (", 0, ")", 1, 0 },
  { 0x110, "BETAINV (", 0, ")", 3, 0 },
  { 0x111, "BINOMDIST (", 0, ")", 4, 0 },
  { 0x112, "CHIDIST (", 0, ")", 2, 0 },
  { 0x113, "CHIINV (", 0, ")", 2, 0 },
  { 0x114, "0x114 (", 0, ")", 3, 0 },
  { 0x115, "CONFIDENCE (", 0, ")", 3, 0 },
  { 0x116, "CRITBINOM (", 0, ")", 3, 0 },
  { 0x117, "0x117 (", 0, ")", 8, 0 },
  { 0x118, "EXPONDIST (", 0, ")", 3, 0 },
  { 0x119, "FDIST (", 0, ")", 3, 0 },
  { 0x11a, "FINV (", 0, ")", 3, 0 },
  { 0x11b, "FISHER (", 0, ")", 1, 0 },
  { 0x11c, "FISHERINV (", 0, ")", 1, 0 },
  { 0x11e, "GAMMADIST (", 0, ")", 4, 0 },
  { 0x11f, "GAMMAINV (", 0, ")", 3, 0 },
  { 0x120, "CEILING (", 0, ")", 2, 0 },
  { 0x121, "HYPGEOMDIST (", 0, ")", 4, 0 },
  { 0x122, "LOGNOMRDIST (", 0, ")", 3, 0 },
  { 0x123, "LOGINV (", 0, ")", 3, 0 },
  { 0x124, "NEGBINOMDIST (", 0, ")", 3, 0 },
  { 0x125, "NORMDIST (", 0, ")", 4, 0 },
  { 0x126, "NOMRSDIST (", 0, ")", 1, 0 },
  { 0x127, "NORMINV (", 0, ")", 3, 0 },
  { 0x128, "NORMSINV (", 0, ")", 1, 0 },
  { 0x129, "STANDARDIZE (", 0, ")", 3, 0 },
  { 0x12a, "0x12a (", 0, ")", 8, 0 },
  { 0x12b, "PERMUT (", 0, ")", 2, 0 },
  { 0x12c, "POISSON (", 0, ")", 3, 0 },
  { 0x12d, "TDIST (", 0, ")", 3, 0 },
  { 0x12e, "WEIBULL (", 0, ")", 4, 0 },
  { 0x12f, "ZTEST (", 0, ")", 3, 0 },
  { 0x132, "CHITEST (", 0, ")", 2, 0 },
  { 0x133, "CORREL (", 0, ")", 2, 0 },
  { 0x134, "COVAR (", 0, ")", 2, 0 },
  { 0x135, "FORECAST (", 0, ")", 3, 0 },
  { 0x136, "FTEST (", 0, ")", 2, 0 },
  { 0x137, "INTERCEPT (", 0, ")", 2, 0 },
  { 0x138, "PEARSON (", 0, ")", 2, 0 },
  { 0x139, "RSQ (", 0, ")", 2, 0 },
  { 0x13a, "STEYX (", 0, ")", 2, 0 },
  { 0x13b, "SLOPE (", 0, ")", 2, 0 },
  { 0x13c, "TTEST (", 0, ")", 4, 0 },
  { 0x13d, "PROB (", 0, ")", 3, 0 },
  { 0x13e, "DEVSQ (", 0, ")", -1, 0 },
  { 0x13f, "GEOMEAN (", 0, ")", -1, 0 },
  { 0x140, "HARMEAN (", 0, ")", -1, 0 },
  { 0x142, "KURT (", 0, ")", -1, 0 },
  { 0x143, "SKEW (", 0, ")", -1, 0 },
  { 0x144, "ZTEST (", 0, ")", 3, 0 },
  { 0x145, "LARGE (", 0, ")", 2, 0 },
  { 0x146, "SMALL (", 0, ")", 2, 0 },
  { 0x147, "QUARTILE (", 0, ")", 2, 0 },
  { 0x148, "PERCENTILE (", 0, ")", 2, 0 },
  { 0x149, "PERCENTRANK (", 0, ")", 2, 0 },
  { 0x14a, "MODE (", 0, ")", -1, 0 },
  { 0x14b, "TRIMMEAN (", 0, ")", 2, 0 },
  { 0x14c, "TINV (", 0, ")", 2, 0 },
  { 0x150, "CONCATENATE (", 0, ")", 2, 0 },
  { 0x156, "RADIANS (", 0, ")", 1, 0 },
  { 0x15b, "COUNTBLANK (", 0, ")", 1, 0 },
  { 0x167, "HYPERLINK (", 0, ")", -1, 0 }
};
#define FORMULA_FUNC_DATA_LEN (sizeof(formula_func_data)/sizeof(FORMULA_FUNC_DATA))

/**
 *  A useful routine for extracting data from a common
 * storage structure.
 **/
static CellRef *getRefV7(MS_EXCEL_SHEET *sheet, BYTE col, WORD gbitrw, int curcol, int currow)
{
	CellRef *cr = (CellRef *)g_malloc(sizeof(CellRef)) ;
	cr->col          = col ;
	cr->row          = (gbitrw & 0x3fff) ;
	cr->row_relative = (gbitrw & 0x8000)==0x8000 ;
	cr->col_relative = (gbitrw & 0x4000)==0x4000 ;
	if (cr->row_relative)
		cr->row-= currow ;
	if (cr->col_relative)
		cr->col-= curcol ;
	cr->sheet = sheet->gnum_sheet ;
	/*  printf ("7Out : %d, %d  at %d, %d\n", cr->col, cr->row, curcol, currow) ; */
	return cr ;
}
/**
 *  A useful routine for extracting data from a common
 * storage structure.
 **/
static CellRef *getRefV8(MS_EXCEL_SHEET *sheet, WORD row, WORD gbitcl, int curcol, int currow)
{
	CellRef *cr = (CellRef *)g_malloc(sizeof(CellRef)) ;
	cr->row          = row ;
	cr->col          = (gbitcl & 0x3fff) ;
	cr->row_relative = (gbitcl & 0x8000)==0x8000 ;
	cr->col_relative = (gbitcl & 0x4000)==0x4000 ;
	if (cr->row_relative)
		cr->row-= currow ;
	if (cr->col_relative)
		cr->col-= curcol ;
	cr->sheet = sheet->gnum_sheet ;
	/*  printf ("8Out : %d, %d  at %d, %d\n", cr->col, cr->row, curcol, currow) ; */
	return cr ;
}

typedef struct _PARSE_DATA
{
	char *name ;
	int  precedence ;
} PARSE_DATA ;

static PARSE_DATA *parse_data_new (char *buffer, int precedence)
{
	PARSE_DATA *ans = g_new (PARSE_DATA, 1) ;
	if (!buffer)
		ans->name = g_strdup("") ;
	else
		ans->name = buffer ;
	ans->precedence = precedence ;
	return ans ;
}

static void parse_data_free (PARSE_DATA *ptr)
{
	if (ptr->name)
		g_free (ptr->name) ;
	g_free (ptr) ;
}

typedef struct _PARSE_LIST
{
	GList *data ;
	int   length ;
} PARSE_LIST ;

static PARSE_LIST *parse_list_new ()
{
	PARSE_LIST *ans = (PARSE_LIST *)g_malloc (sizeof(PARSE_LIST)) ;
	ans->data   = 0 ;
	ans->length = 0 ;
	return ans ;
}

static void parse_list_push (PARSE_LIST *list, PARSE_DATA *pd)
{
/*	printf ("Pushing '%s'\n", pd->name) ; */
	list->data = g_list_append (list->data, pd) ;
	list->length++ ;
}

static void parse_list_push_raw (PARSE_LIST *list, char *buffer, int precedence)
{
	parse_list_push(list, parse_data_new (buffer, precedence)) ;
}

static PARSE_DATA *parse_list_pop (PARSE_LIST *list)
{
	GList *tmp ;
	PARSE_DATA *ans ;
	tmp   = g_list_last (list->data) ;
	if (tmp == 0)
		return parse_data_new (g_strdup ("WrongArgs"), NO_PRECEDENCE) ;
	list->data = g_list_remove_link (list->data, tmp) ;
	ans  = tmp->data ;
	g_list_free (tmp) ;
	list->length-- ;
	return ans ;
}

static void parse_list_free (PARSE_LIST *list)
{
	while (list->data)
		parse_data_free (parse_list_pop(list)) ;
}

/**
 * This pops a load of arguments off the stack,
 * comman delimits them sticking result in 'into'
 * frees the stack space.
 **/
static void 
parse_list_comma_delimit_n (PARSE_LIST *stack, char *prefix,
					int n, char *suffix, int precedence)
{
	char **args, *ans, *put ;
	guint32 slen = 0 ;
	int lp ;

	args = g_new (char *, n) ;

	for (lp=0;lp<n;lp++)
	{
		PARSE_DATA  *dat = parse_list_pop (stack) ;
		args[n-lp-1] = dat->name ;
		slen += dat->name?strlen(dat->name):0 ;
		g_free (dat) ;
	}
	slen+= prefix?strlen(prefix):0 ;
	slen+= suffix?strlen(suffix):0 ;
	slen+= 1 + n ; /* Commas and termination */

	ans = g_new (char, slen) ;

	strcpy (ans, prefix) ;
	put = ans + strlen(ans) ;

	for (lp=0;lp<n;lp++)
		put += g_snprintf (put, slen, "%c%s", lp?',':' ', args[lp]) ;

	strcat (put, suffix) ;
	parse_list_push_raw (stack, ans, precedence) ;
}

/**
 * Prepends an '=' and handles nasty cases
 **/
static char *parse_list_to_equation (PARSE_LIST *list)
{
  if (list->length > 0 && list->data)
    {
      PARSE_DATA *pd ;
      char *formula ;
      
      pd = g_list_first (list->data)->data ;
      if (!pd)
	return g_strdup ("Stack too short") ;
      if (!pd->name)
	return g_strdup ("No data in stack entry") ;
      if (list->length>1)
	return g_strdup ("Too much data on stack\n") ;

      formula = (char *)g_malloc(strlen(pd->name)+2) ;
      if (!formula)
	return g_strdup ("Out of memory") ;

      strcpy (formula, "=") ;
      strcat (formula, pd->name) ;
/*      printf ("Formula : '%s'\n", formula) ; */
      return formula ;
    }
  else
    return g_strdup ("Nothing on stack") ;
}

/**
 * Should be in cell.c ?
 **/
static Cell *
duplicate_formula (Sheet *sheet, int src_col, int src_row, int dest_col, int dest_row)
{
  Cell *ref_cell, *new_cell;
  
  ref_cell = sheet_cell_get (sheet, src_col, src_row);
  if (!ref_cell || !ref_cell->parsed_node)
      return 0;
  
  new_cell = sheet_cell_new (sheet, dest_col, dest_row);
  cell_set_formula_tree_simple (new_cell, ref_cell->parsed_node);
  return new_cell ;
}

static char *
get_inter_sheet_ref (MS_EXCEL_WORKBOOK *wb, guint16 extn_idx)
{
	char *ans, *first, *last ;

	first = biff_get_externsheet_name (wb,
					   extn_idx, 1) ;
	last  = biff_get_externsheet_name (wb,
					   extn_idx, 0) ;
	ans = g_malloc ((first?strlen(first):0) + (last?strlen(last):0) + 3) ;
	ans[0] = 0 ;
	if (first!=last && first)
	{
		strcat (ans, first) ;
		strcat (ans, ":") ;
	}
	if (last)
	{
		strcat (ans, last) ;
		strcat (ans, "!") ;
	}
	return ans ;
}

static gboolean
make_function (PARSE_LIST *stack, int fn_idx, int numargs)
{
	int lp ;
	
	if (fn_idx == 0xff && numargs>1) /* Dodgy Special Case */
	{
		PARSE_DATA *fn, *args ;

		parse_list_comma_delimit_n (stack, "(", numargs-1, ")", NO_PRECEDENCE) ;
		args = parse_list_pop (stack) ;
		fn   = parse_list_pop (stack) ;
		if (!args || !fn || !args->name || !fn->name)
			parse_list_push_raw (stack, g_strdup (_("Broken function")), NO_PRECEDENCE) ;
		else {
/*			printf ("Fn : '%s', '%s'\n", fn->name, args->name) ; */
			parse_list_push_raw (stack, g_strconcat (fn->name, args->name, 0), NO_PRECEDENCE) ;
			parse_data_free (args) ;
			parse_data_free (fn) ;
		}
		return 1 ;
	}
	else
		for (lp=0;lp<FORMULA_FUNC_DATA_LEN;lp++)
		{
			if (formula_func_data[lp].function_idx == fn_idx)
			{
				const FORMULA_FUNC_DATA *fd = &formula_func_data[lp] ;
				
				if (fd->num_args != -1)
					numargs = fd->num_args ;
				
				parse_list_comma_delimit_n (stack, (fd->prefix)?fd->prefix:"",
							    numargs,
							    fd->suffix?fd->suffix:"", fd->precedence) ;
				return 1 ;
			}
		}
	if (lp==FORMULA_FUNC_DATA_LEN)
		printf ("FIXME, unimplemented fn 0x%x, with %d args\n", fn_idx, numargs) ;
	return 0 ;
}

/**
 * Parse that RP Excel formula, see S59E2B.HTM
 * Return a dynamicaly allocated string containing the formula
 **/
char *ms_excel_parse_formula (MS_EXCEL_SHEET *sheet, guint8 *mem,
			     int fn_col, int fn_row, guint16 length)
{
	Cell *cell ;
	int len_left = length ;
	guint8 *cur = mem + 1 ; /* this is so that the offsets and lengths
				   are identical to those in the documentation */
	PARSE_LIST *stack ;
	int error = 0 ;
	char *ans ;
	
/*	if (q->ls_op == BIFF_FORMULA)
	{
		fn_xf           = EX_GETXF(q) ;
		length = BIFF_GETWORD(q->data + 20) ;
		cur = q->data + 22 + 1 ;
	}
	else
	{
		g_assert (q->ls_op == BIFF_ARRAY) ;
		fn_xf = 0 ;
		length = BIFF_GETWORD(q->data + 12) ;
		cur = q->data + 14 + 1 ;
	} */
	
	stack = parse_list_new() ;      
	while (len_left>0 && !error)
	{
		int ptg_length = 0 ;
		int ptg = BIFF_GETBYTE(cur-1) ;
		int ptgbase = ((ptg & 0x40) ? (ptg | 0x20): ptg) & 0x3F ;
		if (ptg > FORMULA_PTG_MAX)
			break ;
		switch (ptgbase)
		{
		case FORMULA_PTG_REF:
		{
			CellRef *ref ;
			char *buffer ;
			if (sheet->ver == eBiffV8)
			{
				ref = getRefV8 (sheet, BIFF_GETWORD(cur), BIFF_GETWORD(cur + 2), fn_col, fn_row) ;
				ptg_length = 4 ;
			}
			else
			{
				ref = getRefV7 (sheet, BIFF_GETBYTE(cur+2), BIFF_GETWORD(cur), fn_col, fn_row) ;
				ptg_length = 3 ;
			}
			buffer = cellref_name (ref, sheet->gnum_sheet, fn_col, fn_row) ;
			parse_list_push_raw (stack, buffer, NO_PRECEDENCE) ;
/*	    printf ("%s\n", buffer) ; */
			g_free (ref) ;
			break ;
		}
		case FORMULA_PTG_NAME_X: /* FIXME: Not using sheet_idx at all ... */
		{
			char *txt ;
			guint16 extn_sheet_idx, extn_name_idx ;
			
			if (sheet->ver == eBiffV8)
			{
				extn_sheet_idx = BIFF_GETWORD(cur) ;
				extn_name_idx  = BIFF_GETWORD(cur+2) ;
/*				printf ("FIXME: v8 NameX : %d %d\n", extn_sheet_idx, extn_name_idx) ; */
				ptg_length = 6 ;
			}
			else
			{
				extn_sheet_idx = BIFF_GETWORD(cur) ;
				extn_name_idx  = BIFF_GETWORD(cur+10) ;
/*				printf ("FIXME: v7 NameX : %d %d\n", extn_sheet_idx, extn_name_idx) ; */
				ptg_length = 24 ;
			}
			if ((txt = biff_name_data_get_name (sheet, extn_name_idx)))
				parse_list_push_raw (stack, g_strdup (txt), NO_PRECEDENCE) ;
			else
				parse_list_push_raw (stack, g_strdup(_("no such name")), NO_PRECEDENCE) ;
			break ;
		}
		case FORMULA_PTG_REF_3D: /* see S59E2B.HTM */
		{
			CellRef *ref=0 ;
			char *buffer ;
			
			if (sheet->ver == eBiffV8)
			{
				guint16 extn_idx = BIFF_GETWORD(cur) ;
				char *ans, *intertxt, *ptr ;

				intertxt = get_inter_sheet_ref (sheet->wb, extn_idx) ;
				ref = getRefV8 (sheet, BIFF_GETWORD(cur+2), BIFF_GETWORD(cur + 4), fn_col, fn_row) ;
				ptr = cellref_name (ref, sheet->gnum_sheet, fn_col, fn_row) ;

				ans = g_new (char, strlen(intertxt)+strlen(ptr)+1) ;
				strcpy (ans, intertxt) ;
				strcat (ans, ptr) ;

				printf("Answer : '%s'\n", ans) ;
				parse_list_push_raw (stack, ans, NO_PRECEDENCE) ;
				g_free (intertxt) ;
				g_free (ptr) ;
				ptg_length = 6 ;
			}
			else
			{
				printf ("FIXME: Biff V7 3D refs are ugly !\n") ;
				error = 1 ;
				ref = 0 ;
				ptg_length = 16 ;
			}
			if (ref)
				g_free (ref) ;
		}
		break ;
		case FORMULA_PTG_AREA_3D: /* see S59E2B.HTM */
		{
			CellRef *ref=0 ;
			char *buffer ;
			
			if (sheet->ver == eBiffV8)
			{
				guint16 extn_idx = BIFF_GETWORD(cur) ;
				char *intertxt ;
				char *buffer, *fstr, *lstr ;
				CellRef *first, *last ;

				intertxt = get_inter_sheet_ref (sheet->wb, extn_idx) ; 
				first = getRefV8(sheet, BIFF_GETBYTE(cur+2), BIFF_GETWORD(cur+6), fn_col, fn_row) ;
				last  = getRefV8(sheet, BIFF_GETBYTE(cur+4), BIFF_GETWORD(cur+8), fn_col, fn_row) ;

				fstr = cellref_name (first, sheet->gnum_sheet, fn_col, fn_row) ;
				lstr = cellref_name (last, sheet->gnum_sheet, fn_col, fn_row) ;

				buffer = g_new (char, strlen(intertxt) + strlen(fstr) + 1
						+ strlen(lstr) + 1 ) ;
				strcpy (buffer, intertxt) ;
				strcat (buffer, fstr) ;
				strcat (buffer, ":") ;
				strcat (buffer, lstr) ;
				parse_list_push_raw(stack, buffer, NO_PRECEDENCE) ;
				g_free (intertxt) ;
				g_free (fstr) ;
				g_free (lstr) ;
				g_free (first) ;
				g_free (last) ;
				
				ptg_length = 10 ;
			}
			else
			{
				printf ("FIXME: Biff V7 3D refs are ugly !\n") ;
				error = 1 ;
				ref = 0 ;
				ptg_length = 20 ;
			}
			if (ref)
				g_free (ref) ;
		}
		break ;
		case FORMULA_PTG_AREA:
		{
			CellRef *first, *last ;
			char *fstr, *lstr, *buffer ;
			if (sheet->ver == eBiffV8)
			{
				first = getRefV8(sheet, BIFF_GETBYTE(cur+0), BIFF_GETWORD(cur+4), fn_col, fn_row) ;
				last  = getRefV8(sheet, BIFF_GETBYTE(cur+2), BIFF_GETWORD(cur+6), fn_col, fn_row) ;
				ptg_length = 8 ;
			}
			else
			{
				first = getRefV7(sheet, BIFF_GETBYTE(cur+4), BIFF_GETWORD(cur+0), fn_col, fn_row) ;
				last  = getRefV7(sheet, BIFF_GETBYTE(cur+5), BIFF_GETWORD(cur+2), fn_col, fn_row) ;
				ptg_length = 6 ;
			}
			fstr = cellref_name (first, sheet->gnum_sheet, fn_col, fn_row) ;
			lstr = cellref_name (last, sheet->gnum_sheet, fn_col, fn_row) ;
			buffer = g_new (char, strlen(fstr) + 1 + strlen(lstr) + 1) ;

			strcpy (buffer, fstr) ;
			strcat (buffer, ":") ;
			strcat (buffer, lstr) ;
			parse_list_push_raw(stack, buffer, NO_PRECEDENCE) ;

			g_free (fstr) ;
			g_free (lstr) ;
			g_free (first) ;
			g_free (last) ;
		}
		break ;
		case FORMULA_PTG_FUNC:
		{
			if (!make_function (stack, BIFF_GETWORD(cur), -1)) error = 1 ;
			ptg_length = 2 ;
			break ;
		}
		case FORMULA_PTG_FUNC_VAR:
		{
			int numargs = (BIFF_GETBYTE( cur ) & 0x7f) ;
			int prompt  = (BIFF_GETBYTE( cur ) & 0x80) ;   /* Prompts the user ?  */
			int iftab   = (BIFF_GETWORD(cur+1) & 0x7fff) ; /* index into fn table */
			int cmdquiv = (BIFF_GETWORD(cur+1) & 0x8000) ; /* is a command equiv.?*/

			if (!make_function (stack, iftab, numargs)) error = 1 ;
			ptg_length = 3 ;
			break ;
		}
		case FORMULA_PTG_NAME:
		{
			guint16 name_idx ; /* 1 based */
			if (sheet->ver == eBiffV8)
				name_idx = BIFF_GETWORD(cur+2) ;
			else
				name_idx = BIFF_GETWORD(cur) ;
			printf ("FIXME: Ptg Name: %d\n", name_idx) ;
			dump(mem, length) ;
			parse_list_push_raw (stack, g_strdup("Unknown name"), NO_PRECEDENCE) ;
		}
		case FORMULA_PTG_EXP: /* FIXME: the formula is the same as another record ... we need a cell_get_funtion call ! */
			printf ("FIXME: Array formula\n") ;
			error = 1 ;
			break ;
		case FORMULA_PTG_PAREN:
/*	  printf ("Ignoring redundant parenthesis ptg\n") ; */
			ptg_length = 0 ;
			break ;
		case FORMULA_PTG_MISSARG:
			parse_list_push_raw (stack, g_strdup (""), NO_PRECEDENCE) ;
			ptg_length = 0 ;
			break ;
		case FORMULA_PTG_ATTR: /* FIXME: not fully implemented */
		{
			guint8  grbit = BIFF_GETBYTE(cur) ;
			guint16 w     = BIFF_GETWORD(cur+1) ;
			ptg_length = 3 ;
			if (grbit & 0x40) /* AttrSpace */
			{
				guint8 num_space = BIFF_GETBYTE(cur+2) ;
				guint8 attrs     = BIFF_GETBYTE(cur+1) ;
				if (attrs == 00) /* bitFSpace : ignore it */
				/* Could perhaps pop top arg & append space ? */ ;
				else
					printf ("Redundant whitespace in formula 0x%x count %d\n", attrs, num_space) ;
			}
			if (grbit & 0x10) { /* AttrSum: Optimised SUM function */
				if (!make_function (stack, 0x04, 1))
				{
					error = 1 ;
					printf ("Error in optimised SUM\n") ;
				}
			} else
				printf ("Unknown PTG Attr 0x%x 0x%x\n", grbit, w) ;
		break ;
		}
		case FORMULA_PTG_ERR:
		{
			parse_list_push_raw (stack,
					     g_strdup(biff_get_error_text(BIFF_GETBYTE(cur))),
					     NO_PRECEDENCE) ;
			ptg_length = 1 ;
			break ;
		}
		case FORMULA_PTG_INT:
		{
			char buf[8]; /* max. "65535" */
			guint16 num = BIFF_GETWORD(cur) ;
			sprintf(buf,"%u",num);
			parse_list_push_raw (stack, g_strdup(buf), NO_PRECEDENCE) ;
			ptg_length = 2 ;
			break;
		}
		case FORMULA_PTG_BOOL:
		{
			parse_list_push_raw (stack, g_strdup(BIFF_GETBYTE(cur) ? "TRUE" : "FALSE" ), NO_PRECEDENCE) ;
			ptg_length = 1 ;
			break ;
		}
		case FORMULA_PTG_NUM:
		{
			double tmp = BIFF_GETDOUBLE(cur) ;
			char buf[65] ; /* should be long enough? */
			g_snprintf (buf, 64, "%f", tmp) ;
			parse_list_push_raw (stack, g_strdup(buf), NO_PRECEDENCE) ;
			ptg_length = 8 ;
			break ;
		}
		case FORMULA_PTG_STR:
		{
			char *str ;
/*			dump (mem, length) ;*/
			if (sheet->ver == eBiffV8)
			{
				str = biff_get_text (cur+2, BIFF_GETWORD(cur)) ;
				ptg_length = 2 + BIFF_GETWORD(cur) ;
/*				printf ("v8+ PTG_STR '%s'\n", str) ; */
			}
			else
			{
				int len = BIFF_GETBYTE(cur) ;
				str = biff_get_text (cur+1, len) ;
				ptg_length = 1 + len ;
/*				printf ("<v7 PTG_STR '%s' len %d ptglen %d\n", str, len, ptg_length) ; */
			}
			parse_list_push_raw (stack, str, NO_PRECEDENCE) ;
			break ;
		}
		default:
		{
			int lp ;
			
/*	    printf ("Search %d records\n", (int)FORMULA_OP_DATA_LEN) ; */
			for (lp=0;lp<FORMULA_OP_DATA_LEN;lp++)
			{
				if (ptgbase == formula_op_data[lp].formula_ptg)
				{
					PARSE_DATA *arg1=0, *arg2 ;
					GList *tmp ;
					char *buffer=0 ;
					gint len = 0 ;
					FORMULA_OP_DATA *fd = &formula_op_data[lp] ;
					int bracket_arg2 ;
					int bracket_arg1 ;
					
					arg2 = parse_list_pop (stack) ;
					bracket_arg2 = arg2->precedence<fd->precedence ;
					len = strlen(arg2->name) + (fd->mid?strlen(fd->mid):0) + 
					      (bracket_arg2?2:0) + 1 ;

					if (fd->infix)
					{
						arg1 = parse_list_pop (stack) ;
						bracket_arg1 = arg1->precedence<fd->precedence ;
						len += strlen (arg1->name) + (bracket_arg1?2:0) ;
						buffer = g_new (char, len) ;
						buffer[0] = '\0' ;

						if (bracket_arg1)
							strcat (buffer, "(") ;
						strcat (buffer, arg1->name) ;
						if (bracket_arg1)
							strcat (buffer, ")") ;
					}
					else
					{
						buffer = g_new (char, len) ;
						buffer[0] = '\0' ;
					}

					strcat (buffer, fd->mid?fd->mid:"") ;
					if (bracket_arg2)
						strcat (buffer, "(") ;
					strcat (buffer, arg2->name) ;
					if (bracket_arg2)
						strcat (buffer, ")") ;
					
/*		    printf ("Op : '%s'\n", buffer) ; */
					parse_list_push_raw(stack, buffer, fd->precedence) ;
					if (fd->infix)
						parse_data_free (arg1) ;
					parse_data_free (arg2) ;
					break ;
				}
			}
			if (lp==FORMULA_OP_DATA_LEN)
				printf ("Unknown PTG 0x%x base %x\n", ptg, ptgbase), error=1 ;
		}
		}
/*		printf ("Ptg 0x%x length (not inc. ptg byte) %d\n", ptgbase, ptg_length) ; */
		cur+=    (ptg_length+1) ;
		len_left-= (ptg_length+1) ;
	}
	if (error)
	{
		printf ("Unknown Formula/Array at [%d, %d]\n", fn_col, fn_row) ;
		printf ("formula data : \n") ;
		dump (mem, length) ;
		
		parse_list_free (stack) ;
		return g_strdup (_("Unknown formula")) ;
	}
	
	ans = parse_list_to_equation (stack) ;
	parse_list_free (stack) ;
	if (ans)
		return ans ;
	else
		return g_strdup(_("Empty parse list")) ;
}

void ms_excel_fixup_array_formulae (MS_EXCEL_SHEET *sheet)
{
	GList *tmp = sheet->array_formulae ;
	while (tmp)
	{
		FORMULA_ARRAY_DATA *dat = tmp->data ;
		printf ("Copying formula from %d,%d to %d,%d\n",
			dat->src_col, dat->src_row,
			dat->dest_col, dat->dest_row) ;
		duplicate_formula (sheet->gnum_sheet,
				   dat->src_col, dat->src_row,
				   dat->dest_col, dat->dest_row) ;
		tmp = tmp->next ;
	}
}
