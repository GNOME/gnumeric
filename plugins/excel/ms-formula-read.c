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
  { 1, "+",  30  }, /* ptgAdd : Addition */
  { 1, "-",  30  }, /* ptgSub : Subtraction */
  { 1, "*",  48 }, /* ptgMul : Multiplication */
  { 1, "/",  32 }, /* ptgDiv : Division */
  { 1, "^",  60 }, /* ptgPower : Exponentiation */
  { 1, "&",  28 }, /* ptgConcat : Concatenation */
  { 1, "<",  24 }, /* ptgLT : Less Than */
  { 1, "<=", 24 }, /* ptgLTE : Less Than or Equal */
  { 1, "=",  20 }, /* ptgEQ : Equal */
  { 1, ">=", 24 }, /* ptgGTE : Greater Than or Equal */
  { 1, ">" , 24 }, /* ptgGT : Greater Than */
  { 1, "<>", 20 }, /* ptgNE : Not Equal */
  { 1, " " , 62 }, /* ptgIsect : Intersection */
  { 1, "," , 62 }, /* ptgUnion : Union */
  { 1, ":" , 63 }, /* ptgRange : Range */
  /* Unary operator tokens */
  { 0, "+",  64 }, /* ptgUplus : Unary Plus */
  { 0, "-",  64 }, /* ptgUminux : Unary Minus */
  { 0, "%",  64 }  /* ptgPercent : Percent Sign */
} ;
#define FORMULA_OP_DATA_LEN   (sizeof(formula_op_data)/sizeof(FORMULA_OP_DATA))
#define FORMULA_OP_START      0x03

/* FIXME: the standard function indexes need to be found from xlcall.h */
/**
 * Various bits of data for functions
 * function index, prefix, middle, suffix, multiple args?, precedence
 **/
FORMULA_FUNC_DATA formula_func_data[] =
{
	{ "COUNT", 2 },
	{ "IF", 8 },
	{ "ISNA", 1 },
	{ "ISERROR", 1 },
	{ "SUM", -1 },
	{ "AVERAGE", -1 },
	{ "MIN", -1 },
	{ "MAX", -1 },
	{ "0x8", 8 },
	{ "COLUMN", -1 },
	{ "0xa", 8 },
	{ "0xb", 8 },
	{ "STDEV", -1 },
	{ "DOLLAR", 1 },
	{ "FIXED", 1 },
	{ "SIN", 1 },
	{ "COS", 1 },
	{ "TAN", 1 },
	{ "ATAN", 1 },
	{ "0x13", 8 },
	{ "SQRT", 1 },
	{ "EXP", 1 },
	{ "LN", 1 },
	{ "LOG10", 1 },
	{ "0x18", 8 },
	{ "0x19", 8 },
	{ "SIGN", 1 },
	{ "0x1b", 8 },
	{ "0x1c", 8 },
	{ "0x1d", 8 },
	{ "REPT", 2 },
	{ "MID", 3 },
	{ "LEN", 1 },
	{ "VALUE", 1 },
	{ "0x22", 8 },
	{ "0x23", 8 },
	{ "AND", -1 },
	{ "OR", -1 },
	{ "NOT", 1 },
	{ "MOD", 2 },
	{ "0x28", 8 },
	{ "0x29", 8 },
	{ "0x2a", 8 },
	{ "0x2b", 8 },
	{ "0x2c", 8 },
	{ "0x2d", 8 },
	{ "VAR", -1 },
	{ "0x2f", 8 },
	{ "REPLACE", 2 },
	{ "LINEST", 2 },
	{ "TREND", 4 },
	{ "LOGEST", 1 },
	{ "GROWTH", -1 },
	{ "0x35", 8 },
	{ "0x36", 8 },
	{ "0x37", 8 },
	{ "0x38", 8 },
	{ "0x39", 8 },
	{ "0x3a", 8 },
	{ "0x3b", 8 },
	{ "0x3c", 8 },
	{ "0x3d", 8 },
	{ "0x3e", 8 },
	{ "RAND", 0 },
	{ "0x40", 8 },
	{ "DATE", 3 },
	{ "TIME", 3 },
	{ "DAY", 1 },
	{ "MONTH", 1 },
	{ "YEAR", 1 },
	{ "WEEKDAY", 1 },
	{ "HOUR", 1 },
	{ "MINUTE", 1 },
	{ "SECOND", 1 },
	{ "NOW", 0 },
	{ "AREAS", 1 },
	{ "0x4c", 8 },
	{ "COLUMNS", 1 },
	{ "OFFSET", -1 },
	{ "0x4f", 8 },
	{ "0x50", 8 },
	{ "0x51", 8 },
	{ "SEARCH", 3 },
	{ "0x53", 8 },
	{ "0x54", 8 },
	{ "0x55", 8 },
	{ "TYPE", 1 },
	{ "0x57", 8 },
	{ "0x58", 8 },
	{ "0x59", 8 },
	{ "0x5a", 8 },
	{ "0x5b", 8 },
	{ "0x5c", 8 },
	{ "0x5d", 8 },
	{ "0x5e", 8 },
	{ "0x5f", 8 },
	{ "0x60", 8 },
	{ "ATAN2", 2 },
	{ "ASIN", 1 },
	{ "ACOS", 1 },
	{ "0x64", 8 },
	{ "HLOOKUP", -1 },
	{ "VLOOKUP", -1 },
	{ "0x67", 8 },
	{ "0x68", 8 },
	{ "ISREF", 1 },
	{ "0x6a", 8 },
	{ "0x6b", 8 },
	{ "0x6c", 8 },
	{ "LOG", 1 },
	{ "0x6e", 8 },
	{ "CHAR", 1 },
	{ "LOWER", 1 },
	{ "UPPER", 1 },
	{ "PROPER", 1 },
	{ "LEFT", 2 },
	{ "RIGHT", 2 },
	{ "EXACT", 2 },
	{ "TRIM", 1 },
	{ "0x77", 2 },
	{ "SUBSTITUTE", -1 },
	{ "CODE", 1 },
	{ "0x7a", 8 },
	{ "0x7b", 8 },
	{ "FIND", -1 },
	{ "CELL", 2 },
	{ "ISERR", 1 },
	{ "ISTEXT", 1 },
	{ "ISNUMBER", 1 },
	{ "ISBLANK", 1 },
	{ "T", 1 },
	{ "N", 1 },
	{ "0x84", 8 },
	{ "0x85", 8 },
	{ "0x86", 8 },
	{ "0x87", 8 },
	{ "0x88", 8 },
	{ "0x89", 8 },
	{ "0x8a", 8 },
	{ "0x8b", 8 },
	{ "DATEVALUE", 1 },
	{ "0x8d", 8 },
	{ "0x8e", 8 },
	{ "0x8f", 8 },
	{ "0x90", 8 },
	{ "0x91", 8 },
	{ "0x92", 8 },
	{ "0x93", 8 },
	{ "0x94", 8 },
	{ "0x95", 8 },
	{ "0x96", 8 },
	{ "0x97", 8 },
	{ "0x98", 8 },
	{ "0x99", 8 },
	{ "0x9a", 8 },
	{ "0x9b", 8 },
	{ "0x9c", 8 },
	{ "0x9d", 8 },
	{ "0x9e", 8 },
	{ "0x9f", 8 },
	{ "0xa0", 8 },
	{ "0xa1", 8 },
	{ "CLEAN", 1 },
	{ "0xa3", 8 },
	{ "0xa4", 8 },
	{ "0xa5", 8 },
	{ "0xa6", 8 },
	{ "0xa7", 8 },
	{ "0xa8", 8 },
	{ "COUNTA", -1 },
	{ "0xaa", 8 },
	{ "0xab", 8 },
	{ "0xac", 8 },
	{ "0xa8", 8 },
	{ "0xae", 8 },
	{ "0xaf", 8 },
	{ "0xb0", 8 },
	{ "0xb1", 8 },
	{ "0xb2", 8 },
	{ "0xb3", 8 },
	{ "0xb4", 8 },
	{ "0xb5", 8 },
	{ "0xb6", 8 },
	{ "0xb7", 8 },
	{ "0xb8", 8 },
	{ "0xb9", 8 },
	{ "0xba", 8 },
	{ "0xbb", 8 },
	{ "0xbc", 8 },
	{ "0xbd", 8 },
	{ "ISNONTEXT", 1 },
	{ "0xbf", 8 },
	{ "0xc0", 8 },
	{ "STDEVP", -1 },
	{ "VARP", -1 },
	{ "0xc3", 8 },
	{ "0xc4", 8 },
	{ "TRUNC", 1 },
	{ "ISLOGICAL", 1 },
	{ "0xc7", 8 },
	{ "0xc8", 8 },
	{ "0xc9", 8 },
	{ "0xca", 8 },
	{ "0xcb", 8 },
	{ "0xcc", 8 },
	{ "0xcd", 8 },
	{ "0xce", 8 },
	{ "0xcf", 8 },
	{ "0xd0", 8 },
	{ "0xd1", 8 },
	{ "0xd2", 8 },
	{ "0xd3", 8 },
	{ "0xd4", 8 },
	{ "0xd5", 8 },
	{ "0xd6", 8 },
	{ "0xd7", 8 },
	{ "0xd8", 8 },
	{ "0xd9", 8 },
	{ "0xda", 8 },
	{ "ADDRESS", -1 },
	{ "DAYS360", 2 },
	{ "TODAY", 0 },
	{ "0xde", 8 },
	{ "0xdf", 8 },
	{ "0xe0", 8 },
	{ "0xe1", 8 },
	{ "0xe2", 8 },
	{ "MEDIAN", -1 },
	{ "0xe4", 8 },
	{ "SINH", 1 },
	{ "COSH", 1 },
	{ "TANH", 1 },
	{ "ASINH", 1 },
	{ "ACOSH", 1 },
	{ "ATANH", 1 },
	{ "0xeb", 8 },
	{ "0xec", 8 },
	{ "0xed", 8 },
	{ "0xee", 8 },
	{ "0xef", 8 },
	{ "0xf0", 8 },
	{ "0xf1", 8 },
	{ "0xf2", 8 },
	{ "0xf3", 8 },
	{ "INFO", 1 },
	{ "0xf5", 8 },
	{ "0xf6", 8 },
	{ "0xf7", 8 },
	{ "0xf8", 8 },
	{ "0xf9", 8 },
	{ "0xfa", 8 },
	{ "0xfb", 8 },
	{ "FREQUENCY", 2 },
	{ "0xfd", 8 },
	{ "0xfe", 8 },
	{ "MAGIC", -1 }, /* Dodgy special case */
	{ "0x100", 8 },
	{ "0x101", 8 },
	{ "0x102", 8 },
	{ "0x103", 8 },
	{ "0x104", 8 },
	{ "ERROR.TYPE", 1 },
	{ "0x106", 8 },
	{ "0x107", 8 },
	{ "0x108", 8 },
	{ "0x109", 8 },
	{ "0x10a", 8 },
	{ "0x10b", 8 },
	{ "0x10c", 8 },
	{ "AVEDEV", -1 },
	{ "BETADIST", 3 },
	{ "GAMMALN", 1 },
	{ "BETAINV", 3 },
	{ "BINOMDIST", 4 },
	{ "CHIDIST", 2 },
	{ "CHIINV", 2 },
	{ "0x114", 3 },
	{ "CONFIDENCE", 3 },
	{ "CRITBINOM", 3 },
	{ "0x117", 8 },
	{ "EXPONDIST", 3 },
	{ "FDIST", 3 },
	{ "FINV", 3 },
	{ "FISHER", 1 },
	{ "FISHERINV", 1 },
	{ "0x11d", 8 },
	{ "GAMMADIST", 4 },
	{ "GAMMAINV", 3 },
	{ "CEILING", 2 },
	{ "HYPGEOMDIST", 4 },
	{ "LOGNOMRDIST", 3 },
	{ "LOGINV", 3 },
	{ "NEGBINOMDIST", 3 },
	{ "NORMDIST", 4 },
	{ "NOMRSDIST", 1 },
	{ "NORMINV", 3 },
	{ "NORMSINV", 1 },
	{ "STANDARDIZE", 3 },
	{ "0x12a", 8 },
	{ "PERMUT", 2 },
	{ "POISSON", 3 },
	{ "TDIST", 3 },
	{ "WEIBULL", 4 },
	{ "ZTEST", 3 },
	{ "0x130", 8 },
	{ "0x131", 8 },
	{ "CHITEST", 2 },
	{ "CORREL", 2 },
	{ "COVAR", 2 },
	{ "FORECAST", 3 },
	{ "FTEST", 2 },
	{ "INTERCEPT", 2 },
	{ "PEARSON", 2 },
	{ "RSQ", 2 },
	{ "STEYX", 2 },
	{ "SLOPE", 2 },
	{ "TTEST", 4 },
	{ "PROB", 3 },
	{ "DEVSQ", -1 },
	{ "GEOMEAN", -1 },
	{ "HARMEAN", -1 },
	{ "0x141", 8 },
	{ "KURT", -1 },
	{ "SKEW", -1 },
	{ "ZTEST", 3 },
	{ "LARGE", 2 },
	{ "SMALL", 2 },
	{ "QUARTILE", 2 },
	{ "PERCENTILE", 2 },
	{ "PERCENTRANK", 2 },
	{ "MODE", -1 },
	{ "TRIMMEAN", 2 },
	{ "TINV", 2 },
	{ "0x14d", 8 },
	{ "0x14e", 8 },
	{ "0x14f", 8 },
	{ "CONCATENATE", 2 },
	{ "0x151", 8 },
	{ "0x152", 8 },
	{ "0x153", 8 },
	{ "0x154", 8 },
	{ "0x155", 8 },
	{ "RADIANS", 1 },
	{ "0x157", 8 },
	{ "0x158", 8 },
	{ "0x159", 8 },
	{ "0x15a", 8 },
	{ "COUNTBLANK", 1 },
	{ "0x15c", 8 },
	{ "0x15d", 8 },
	{ "0x15e", 8 },
	{ "0x15f", 8 },
	{ "0x160", 8 },
	{ "0x161", 8 },
	{ "0x162", 8 },
	{ "0x163", 8 },
	{ "0x164", 8 },
	{ "0x165", 8 },
	{ "0x166", 8 },
	{ "HYPERLINK", -1 },
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
					int n, int precedence)
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
	slen+= 2 + 1 + n ; /* Brackets Commas and termination */

	ans = g_new (char, slen) ;

	strcpy (ans, prefix) ;
	strcat (ans, "(") ;
	put = ans + strlen(ans) ;

	for (lp=0;lp<n;lp++)
		put += g_snprintf (put, slen, "%c%s", lp?',':' ', args[lp]) ;

	strcat (put, ")") ;
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

		parse_list_comma_delimit_n (stack, "", numargs-1, NO_PRECEDENCE) ;
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
		if (fn_idx > 0 && fn_idx < FORMULA_FUNC_DATA_LEN)
		{
			const FORMULA_FUNC_DATA *fd = &formula_func_data[fn_idx] ;
			
			if (fd->num_args != -1)
				numargs = fd->num_args ;
			
			parse_list_comma_delimit_n (stack, (fd->prefix)?fd->prefix:"",
						    numargs, 0) ;
			return 1 ;
		}
		else
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
		{
			int top_left_col = BIFF_GETWORD(cur+2) ;
			int top_left_row = BIFF_GETWORD(cur+0) ;
			printf ("FIXME: I'm found in an ARRAY record ... %d %d\n",
				top_left_col, top_left_row) ;
			/* Just push a null string onto the stack, just to get the
			   XF info sorted safely */
			parse_list_push_raw (stack, g_strdup(""), NO_PRECEDENCE) ;
			ptg_length = 4 ;
			break ;
		}
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
			guint32 len ;
/*			dump (mem, length) ;*/
			if (sheet->ver == eBiffV8)
			{
				str = biff_get_text (cur+2, BIFF_GETWORD(cur), &len) ;
				ptg_length = 2 + len ;
/*				printf ("v8+ PTG_STR '%s'\n", str) ; */
			}
			else
			{
				str = biff_get_text (cur+1, BIFF_GETBYTE(cur), &len) ;
				ptg_length = 1 + len ;
/*				printf ("<v7 PTG_STR '%s' len %d ptglen %d\n", str, len, ptg_length) ; */
			}
			parse_list_push_raw (stack, str, NO_PRECEDENCE) ;
			break ;
		}
		default:
		{
/*	    printf ("Search %d records\n", (int)FORMULA_OP_DATA_LEN) ; */
			if (ptgbase >= FORMULA_OP_START && ptgbase < FORMULA_OP_START+FORMULA_OP_DATA_LEN)
			{
				PARSE_DATA *arg1=0, *arg2 ;
				GList *tmp ;
				char *buffer=0 ;
				gint len = 0 ;
				FORMULA_OP_DATA *fd = &formula_op_data[ptgbase - FORMULA_OP_START] ;
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
			}
			else
				printf ("Unknown PTG 0x%x base %x\n", ptg, ptgbase), error=1 ;
		}
		break ;
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
