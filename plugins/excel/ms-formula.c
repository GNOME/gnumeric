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

#define FORMULA_DEBUG 0

#define NO_PRECEDENCE 256

typedef struct _FORMULA_FUNC_DATA
{
	char *prefix ;
	int num_args ; /* -1 for multi-arg */
} FORMULA_FUNC_DATA ;

/**
 * Various bits of data for operators
 * see S59E2B.HTM for formula_ptg values
 * formula PTG, prefix, middle, suffix, precedence
 **/

typedef struct _FORMULA_OP_DATA
{
	Operation op;
} FORMULA_OP_DATA ;

  /* Binary operator tokens */
FORMULA_OP_DATA formula_op_data[] = {
	{ OPER_ADD  }, /* ptgAdd : Addition */
	{ OPER_SUB  }, /* ptgSub : Subtraction */
	{ OPER_MULT }, /* ptgMul : Multiplication */
	{ OPER_DIV }, /* ptgDiv : Division */
	{ OPER_EXP }, /* ptgPower : Exponentiation */
	{ OPER_CONCAT }, /* ptgConcat : Concatenation */
	{ OPER_LT }, /* ptgLT : Less Than */
	{ OPER_LTE }, /* ptgLTE : Less Than or Equal */
	{ OPER_EQUAL }, /* ptgEQ : Equal */
	{ OPER_GTE }, /* ptgGTE : Greater Than or Equal */
	{ OPER_GT }, /* ptgGT : Greater Than */
	{ OPER_NOT_EQUAL }, /* ptgNE : Not Equal */
/* FIXME: These need implementing ... */
	{ OPER_ADD }, /* ptgIsect : Intersection */
	{ OPER_ADD }, /* ptgUnion : Union */
	{ OPER_ADD }, /* ptgRange : Range */
} ;
#define FORMULA_OP_DATA_LEN   (sizeof(formula_op_data)/sizeof(FORMULA_OP_DATA))
#define FORMULA_OP_START      0x03

/**
 * FIXME: This table needs properly populating, preferably from xlcall.h (?)
 * Functions in order, zero based, with number of arguments or '-1' for vararg.
 **/
FORMULA_FUNC_DATA formula_func_data[] =
{
	{ "COUNT", 2 },
	{ "IF", -1 },
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
	{ "ABS", 1 },
	{ "INT", 1 },
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
	{ "DCOUNT", 3 },
	{ "DSUM", 3 },
	{ "DAVERAGE", 3 },
	{ "DMIN", 3 },
	{ "DMAX", 3 },
	{ "DSTDEV", 3 },
	{ "VAR", -1 },
	{ "DVAR", 3 },
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
	{ "CHOOSE", -1 },
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
	{ "INDIRECT", -1 },
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
	{ "DPRODUCT", 3 },
	{ "ISNONTEXT", 1 },
	{ "0xbf", 8 },
	{ "0xc0", 8 },
	{ "STDEVP", -1 },
	{ "VARP", -1 },
	{ "DSTDEVP", 3 },
	{ "DVARP", 3 },
	{ "TRUNC", 1 },
	{ "ISLOGICAL", 1 },
	{ "DCOUNTA", 3 },
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
	{ "DGET", 3 },
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
 * Scads of nasty helper functions
 **/

static ExprTree *
expr_tree_string (const char *str)
{
	ExprTree *e = expr_tree_new();
	e->oper = OPER_CONSTANT;
	e->u.constant = value_str (str);
	return e;
}

static ExprTree *
expr_tree_value (Value *v)
{
	ExprTree *e = expr_tree_new();
	e->oper = OPER_CONSTANT;
	e->u.constant = v;
	return e;
}

static ExprTree *
expr_tree_cellref (const CellRef *r)
{
	ExprTree *e = expr_tree_new();
	e->oper = OPER_VAR;
	e->u.ref = *r;
	return e;
}

static ExprTree *
expr_tree_unary (Operation op, ExprTree *tr)
{
	ExprTree *e;
	g_return_val_if_fail (op==OPER_NEG, tr);
	e = expr_tree_new ();
	e->oper = op;
	e->u.value = tr;
	return e;
}

/**
 * End of nasty helper functions
 **/






/**
 *  A useful routine for extracting data from a common
 * storage structure.
 **/
static CellRef *
getRefV7(MS_EXCEL_SHEET *sheet, BYTE col, WORD gbitrw, int curcol, int currow, int shrfmla)
{
	CellRef *cr = (CellRef *)g_malloc(sizeof(CellRef)) ;
	cr->col          = col ;
	cr->row          = (gbitrw & 0x3fff) ;
	cr->row_relative = (gbitrw & 0x8000)==0x8000 ;
	cr->col_relative = (gbitrw & 0x4000)==0x4000 ;
	cr->sheet = sheet->gnum_sheet ;
	if (FORMULA_DEBUG>2)
		printf ("7In : 0x%x, 0x%x  at %d, %d shared %d\n", col, gbitrw,
			curcol, currow, shrfmla) ; 
	if (shrfmla) {  /* FIXME: Brutal hack, docs are vague */
		gint8 t = (cr->row&0x00ff);
		cr->row = currow+t ;
		t = (cr->col&0x00ff);
		cr->col = curcol+t ;
	}
	if (cr->row_relative)
		cr->row-= currow ;
	if (cr->col_relative)
		cr->col-= curcol ;
	return cr ;
}
/**
 *  A useful routine for extracting data from a common
 * storage structure.
 **/
static CellRef *
getRefV8(MS_EXCEL_SHEET *sheet, WORD row, WORD gbitcl, int curcol, int currow, int shrfmla)
{
	CellRef *cr = (CellRef *)g_malloc(sizeof(CellRef)) ;
	cr->sheet = sheet->gnum_sheet ;
	if (FORMULA_DEBUG>2)
		printf ("8In : 0x%x, 0x%x  at %d, %d shared %d\n", row, gbitcl,
			curcol, currow, shrfmla) ;
	cr->row          = row ;
	cr->col          = (gbitcl & 0x3fff) ;
	cr->row_relative = (gbitcl & 0x8000)==0x8000 ;
	cr->col_relative = (gbitcl & 0x4000)==0x4000 ;
	if (shrfmla) { /* FIXME: Brutal hack, docs are vague */
		gint8 t = (cr->row&0x00ff);
		if (FORMULA_DEBUG>2)
			printf ("row off %d ",t);
		cr->row = currow+t ;
		t = (cr->col&0x00ff);
		if (FORMULA_DEBUG>2)
			printf ("col off %d\n",t);
		cr->col = curcol+t ;
	}
	if (cr->row_relative)
		cr->row-= currow ;
	if (cr->col_relative)
		cr->col-= curcol ;
	if (FORMULA_DEBUG>2)
		printf ("Returns : %d,%d Rel:(%d %d)\n", cr->col, cr->row,
			cr->col_relative, cr->row_relative);
	return cr ;
}

typedef ExprTree PARSE_DATA;
typedef GList    PARSE_LIST;

static void
parse_list_push (PARSE_LIST **list, ExprTree *pd)
{
	if (FORMULA_DEBUG > 0 && !pd)
		printf ("FIXME: Pushing nothing onto stack\n");
	*list = g_list_append (*list, pd) ;
}
static void
parse_list_push_raw (PARSE_LIST **list, Value *v)
{
	parse_list_push (list, expr_tree_value (v));
}

static ExprTree *
parse_list_pop (PARSE_LIST **list)
{
	GList *tmp ;
	PARSE_DATA *ans ;
	tmp = g_list_last (*list) ;
	if (tmp == 0)
		return expr_tree_string ("WrongArgs");
	*list = g_list_remove_link (*list, tmp) ;
	ans  = tmp->data ;
	g_list_free (tmp) ;
	return ans ;
}

/**
 * Returns a new list composed of the last n items pop'd off the list.
 **/
static GList *
parse_list_last_n (PARSE_LIST **list, gint n)
{
	GList *l=0;
	while (n-->0)
		l=g_list_prepend (l, parse_list_pop(list));
	return l;
}


static void 
parse_list_free (PARSE_LIST **list)
{
	while (*list)
		expr_tree_unref (parse_list_pop(list));
}

static void
make_inter_sheet_ref (MS_EXCEL_WORKBOOK *wb, guint16 extn_idx, CellRef *a, CellRef *b)
{
	a->sheet = biff_get_externsheet_name (wb, extn_idx, 1) ;
	if (b)
		b->sheet = biff_get_externsheet_name (wb, extn_idx, 0) ;
}

static gboolean
make_function (PARSE_LIST **stack, int fn_idx, int numargs)
{
	int lp ;
	ExprTree *fn = expr_tree_new();
	Symbol *name=NULL;
	fn->oper = OPER_FUNCALL;

	if (fn_idx == 0xff && numargs>1) /* Dodgy Special Case */
	{
		ExprTree *tmp;
		fn->u.function.arg_list = parse_list_last_n (stack, numargs-1);
		tmp = parse_list_pop (stack) ;
		if (!tmp || tmp->oper != OPER_CONSTANT ||
		    tmp->u.constant->type != VALUE_STRING) {
			parse_list_free (&fn->u.function.arg_list);
			parse_list_push (stack, expr_tree_string (_("Broken function")));
			return 0;
		}
		else {
			name = symbol_lookup (global_symbol_table, tmp->u.constant->v.str->str);
			if (!name) {
				printf ("Fn : '%s'\n", tmp->u.constant->v.str->str);
				parse_list_free (&fn->u.function.arg_list);
				parse_list_push (stack, expr_tree_string (g_strdup_printf ("Duff fn '%s'", 
											   tmp->u.constant->v.str->str)));
				return 0;
			}
			symbol_ref (name);
			fn->u.function.symbol = name;
			parse_list_push (stack, fn);
		}
		return 1 ;
	}
	else
		if (fn_idx > 0 && fn_idx < FORMULA_FUNC_DATA_LEN)
		{
			const FORMULA_FUNC_DATA *fd = &formula_func_data[fn_idx] ;
			
			if (fd->num_args != -1) /* Right args for multi-arg funcs. */
				numargs = fd->num_args ;

			fn->u.function.arg_list = parse_list_last_n (stack, numargs);
			if (fd->prefix)
				name = symbol_lookup (global_symbol_table, fd->prefix);
			if (!name) {
				printf ("Unknown fn '%s'\n", fd->prefix);
				parse_list_free (&fn->u.function.arg_list);
				parse_list_push (stack, expr_tree_string (g_strdup_printf ("Duff fn '%s'", 
											   fd->prefix?fd->prefix:"Umm...")));
				return 0;
			}
			symbol_ref (name);
			fn->u.function.symbol = name;
			parse_list_push (stack, fn);
			return 1 ;
		}
		else
			printf ("FIXME, unimplemented fn 0x%x, with %d args\n", fn_idx, numargs) ;
	return 0 ;
}

/**
 * Parse that RP Excel formula, see S59E2B.HTM
 * Return a dynamicaly allocated string containing the formula, never NULL
 **/
ExprTree *
ms_excel_parse_formula (MS_EXCEL_SHEET *sheet, guint8 *mem,
			int fn_col, int fn_row, int shared,
			guint16 length)
{
	Cell *cell ;
	int len_left = length ;
	guint8 *cur = mem + 1 ; /* this is so that the offsets and lengths
				   are identical to those in the documentation */
	PARSE_LIST *stack = NULL;
	int error = 0 ;
	char *ans ;
	
	while (len_left>0 && !error)
	{
		int ptg_length = 0 ;
		int ptg = BIFF_GETBYTE(cur-1) ;
		int ptgbase = ((ptg & 0x40) ? (ptg | 0x20): ptg) & 0x3F ;
		if (ptg > FORMULA_PTG_MAX)
			break ;
		if (FORMULA_DEBUG>0)
			printf ("Ptg : 0x%x -> 0x%x\n", ptg, ptgbase) ;
		switch (ptgbase)
		{
		case FORMULA_PTG_REFN:
		case FORMULA_PTG_REF:
		{
			CellRef *ref=0;
			if (sheet->ver == eBiffV8)
			{
				ref = getRefV8 (sheet, BIFF_GETWORD(cur), BIFF_GETWORD(cur + 2),
						fn_col, fn_row, shared) ;
				ptg_length = 4 ;
			}
			else
			{
				ref = getRefV7 (sheet, BIFF_GETBYTE(cur+2), BIFF_GETWORD(cur),
						fn_col, fn_row, shared) ;
				ptg_length = 3 ;
			}
			parse_list_push (&stack, expr_tree_cellref (ref));
			if (ref) g_free (ref) ;
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
				parse_list_push_raw (&stack, value_str (txt));
			else
				parse_list_push_raw (&stack, value_str ("DuffName"));
			break ;
		}
		case FORMULA_PTG_REF_3D: /* see S59E2B.HTM */
		{
			CellRef *ref=0;
			if (sheet->ver == eBiffV8)
			{
				guint16 extn_idx = BIFF_GETWORD(cur) ;
				ref = getRefV8 (sheet, BIFF_GETWORD(cur+2), BIFF_GETWORD(cur + 4),
						fn_col, fn_row, 0) ;
				make_inter_sheet_ref (sheet->wb, extn_idx, ref, 0) ;
				parse_list_push (&stack, expr_tree_cellref (ref));
				ptg_length = 6 ;
			}
			else
			{
				printf ("FIXME: Biff V7 3D refs are ugly !\n") ;
				error = 1 ;
				ref = 0 ;
				ptg_length = 16 ;
			}
			if (ref) g_free (ref) ;
		}
		break ;
		case FORMULA_PTG_AREA_3D: /* see S59E2B.HTM */
		{
			CellRef *first=0, *last=0 ;
			
			if (sheet->ver == eBiffV8)
			{
				guint16 extn_idx = BIFF_GETWORD(cur) ;

				first = getRefV8(sheet, BIFF_GETBYTE(cur+2), BIFF_GETWORD(cur+6),
						 fn_col, fn_row, 0) ;
				last  = getRefV8(sheet, BIFF_GETBYTE(cur+4), BIFF_GETWORD(cur+8),
						 fn_col, fn_row, 0) ;

				make_inter_sheet_ref (sheet->wb, extn_idx, first, last) ;
				parse_list_push_raw (&stack, value_cellrange (first, last));
				ptg_length = 10 ;
			}
			else
			{
				printf ("FIXME: Biff V7 3D refs are ugly !\n") ;
				error = 1 ;
				ptg_length = 20 ;
			}
			if (first) g_free (first) ;
			if (last)  g_free (last) ;
		}
		break ;
		case FORMULA_PTG_AREAN:
		case FORMULA_PTG_AREA:
		{
			CellRef *first=0, *last=0 ;
			if (sheet->ver == eBiffV8)
			{
				first = getRefV8(sheet, BIFF_GETBYTE(cur+0), BIFF_GETWORD(cur+4),
						 fn_col, fn_row, shared) ;
				last  = getRefV8(sheet, BIFF_GETBYTE(cur+2), BIFF_GETWORD(cur+6),
						 fn_col, fn_row, shared) ;
				ptg_length = 8 ;
			}
			else
			{
				first = getRefV7(sheet, BIFF_GETBYTE(cur+4), BIFF_GETWORD(cur+0), fn_col, fn_row, shared) ;
				last  = getRefV7(sheet, BIFF_GETBYTE(cur+5), BIFF_GETWORD(cur+2), fn_col, fn_row, shared) ;
				ptg_length = 6 ;
			}

			parse_list_push_raw (&stack, value_cellrange (first, last));

			if (first) g_free (first) ;
			if (last)  g_free (last) ;
			break ;
		}
		case FORMULA_PTG_ARRAY:
		{
			Value *v;
			guint16 cols=BIFF_GETBYTE(cur+0);
			guint16 rows=BIFF_GETWORD(cur+1);
			guint16 lpx,lpy;
			guint8 *data=cur+3;
			

			if (cols==0) cols=256;
			v = value_array_new (cols, rows);
			ptg_length = 3;
			printf ("An Array how interesting: (%d,%d)\n", cols, rows);
			dump (mem, length);
#ifdef 0
			for (lpy=0;lpy<rows;lpy++) {
				for (lpx=0;lpx<cols;lpx++) {
					guint8 opts=BIFF_GETBYTE(data);
					if (opts == 1) {
						g_string_sprintfa (ans, MS_EXCEL_DOUBLE_FORMAT,
								   BIFF_GETDOUBLE(data+1));
						data+=9;
						ptg_length+=9;
					} else if (opts == 2) {
						guint32 len;
						g_string_append (ans, biff_get_text (data+2,
										     BIFF_GETBYTE(data+1),
										     &len));
						g_string_append (ans, ",");
						data+=len+2;
						ptg_length+=2+len;
					} else {
						printf ("Duff type\n");
						break;
					}
					g_string_append (ans, ";");
				}
			}
#endif
			parse_list_push_raw (&stack, v);
			break;
		}
		case FORMULA_PTG_FUNC:
		{
			if (!make_function (&stack, BIFF_GETWORD(cur), -1)) error = 1 ;
			ptg_length = 2 ;
			break ;
		}
		case FORMULA_PTG_FUNC_VAR:
		{
			int numargs = (BIFF_GETBYTE( cur ) & 0x7f) ;
			int prompt  = (BIFF_GETBYTE( cur ) & 0x80) ;   /* Prompts the user ?  */
			int iftab   = (BIFF_GETWORD(cur+1) & 0x7fff) ; /* index into fn table */
			int cmdquiv = (BIFF_GETWORD(cur+1) & 0x8000) ; /* is a command equiv.?*/

			if (!make_function (&stack, iftab, numargs)) error = 1 ;
			ptg_length = 3 ;
			break ;
		}
/*FIXME: REIMPLEMENT		case FORMULA_PTG_NAME:
		{
		guint16 name_idx ; *//* 1 based */
/*			char *txt;
			if (sheet->ver == eBiffV8)
				name_idx = BIFF_GETWORD(cur+2) ;
			else
				name_idx = BIFF_GETWORD(cur) ;
			txt = biff_name_data_get_name (sheet, name_idx);
			if (!txt) {
				if (FORMULA_DEBUG>0) {
					printf ("FIXME: Ptg Name not found: %d\n", name_idx) ;
					dump(mem, length) ;
				}
				parse_list_push_raw (&stack, g_strdup("Unknown name"), NO_PRECEDENCE) ;
			} else
				parse_list_push_raw (&stack, g_strdup(txt), NO_PRECEDENCE) ;
		}*/
		case FORMULA_PTG_EXP:
		{
			int top_left_col = BIFF_GETWORD(cur+2) ;
			int top_left_row = BIFF_GETWORD(cur+0) ;
			ExprTree *tr;
			tr = ms_excel_sheet_shared_formula (sheet, top_left_col,
							    top_left_row,
							    fn_col, fn_row) ;
			if (!tr) tr = expr_tree_string ("");
			parse_list_push (&stack, tr);
			ptg_length = length; /* Force it to be the only token 4 ; */
			break ;
		}
		case FORMULA_PTG_U_PLUS: /* Discard */
			break;
		case FORMULA_PTG_U_MINUS:
			parse_list_push (&stack, expr_tree_unary (OPER_NEG,
								  parse_list_pop (&stack)));
			break;
		case FORMULA_PTG_PAREN:
/*	  printf ("Ignoring redundant parenthesis ptg\n") ; */
			ptg_length = 0 ;
			break ;
		case FORMULA_PTG_MISSARG: /* FIXME: Need Null Arg. type. */
			parse_list_push_raw (&stack, value_str (""));
			ptg_length = 0 ;
			break ;
		case FORMULA_PTG_ATTR: /* FIXME: not fully implemented */
		{
			guint8  grbit = BIFF_GETBYTE(cur) ;
			guint16 w     = BIFF_GETWORD(cur+1) ;
			ptg_length = 3 ;
			if (grbit & 0x01) {
				if (FORMULA_DEBUG>0)
					printf ("A volatile function: so what\n") ;
			} else if (grbit & 0x02) { /* AttrIf: 'optimised' IF function */
				/* Who cares if the TRUE expr has a goto at the end */
				ExprTree *tr;
				if (FORMULA_DEBUG>2) {
					printf ("Optimised IF 0x%x 0x%x\n", grbit, w) ;
					dump (mem, length) ;
				}
				if (w)
					tr = ms_excel_parse_formula (sheet, cur+ptg_length,
								      fn_col, fn_row, shared,
								      w) ;
				else
					tr = expr_tree_value (value_str (""));
				parse_list_push (&stack, tr);
				ptg_length += w ;
			} else if (grbit & 0x04) { /* AttrChoose 'optimised' my foot. */
				guint16 len, lp;
				guint32 offset=0;
				guint8 *data=cur+3;
				ExprTree *tr;

				if (FORMULA_DEBUG>1) {
					printf ("'Optimised' choose\n");
					dump (mem,length);
				}
				for (lp=0;lp<w;lp++) { /* w = wCases */
					offset= BIFF_GETWORD(data);
					len = BIFF_GETWORD(data+2) - offset;
					if (FORMULA_DEBUG>1)
						printf ("Get from %d len %d [ = 0x%x ]\n",
							ptg_length+offset, len, *(cur+ptg_length+offset));
					tr = ms_excel_parse_formula (sheet, cur+ptg_length+offset,
								     fn_col, fn_row, shared,
								     len);
					data+=2;
					parse_list_push (&stack, tr);
				}
				ptg_length+=BIFF_GETWORD(data);
			} else if (grbit & 0x08) { /* AttrGoto */
				if (FORMULA_DEBUG>2) {
					printf ("Goto %d: cur = 0x%x\n", w, (int)(cur-mem)) ;
					dump (mem, length) ;
				}
				ptg_length = w ;
			} else if (grbit & 0x10) { /* AttrSum: 'optimised' SUM function */
				if (!make_function (&stack, 0x04, 1))
				{
					error = 1 ;
					printf ("Error in optimised SUM\n") ;
				}
			} else if (grbit & 0x40) { /* AttrSpace */
				guint8 num_space = BIFF_GETBYTE(cur+2) ;
				guint8 attrs     = BIFF_GETBYTE(cur+1) ;
				if (attrs == 00) /* bitFSpace : ignore it */
				/* Could perhaps pop top arg & append space ? */ ;
				else
					printf ("Redundant whitespace in formula 0x%x count %d\n", attrs, num_space) ;
			} else {
				if (FORMULA_DEBUG>0)
					printf ("Unknown PTG Attr 0x%x 0x%x\n", grbit, w) ;
				error = 1 ;
			}
		break ;
		}
		case FORMULA_PTG_ERR:
		{
			parse_list_push_raw (&stack,
					     value_str(biff_get_error_text(BIFF_GETBYTE(cur))));
			ptg_length = 1 ;
			break ;
		}
		case FORMULA_PTG_INT:
		{
			guint16 num = BIFF_GETWORD(cur) ;
			parse_list_push_raw (&stack, value_int(num));
			ptg_length = 2 ;
			break;
		}
		case FORMULA_PTG_BOOL:  /* FIXME: True / False */
		{
			parse_list_push_raw (&stack, value_int (BIFF_GETBYTE(cur)));
			ptg_length = 1 ;
			break ;
		}
		case FORMULA_PTG_NUM:
		{
			double tmp = BIFF_GETDOUBLE(cur) ;
			parse_list_push_raw (&stack, value_float (tmp));
			ptg_length = 8 ;
			break ;
		}
		case FORMULA_PTG_STR:
		{
			char *str;
			guint32 len;
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
			if (!str) str = g_strdup("");
			parse_list_push_raw (&stack, value_str (str));
			if (str)  g_free (str);
			break ;
		}
		default:
		{
/*	    printf ("Search %d records\n", (int)FORMULA_OP_DATA_LEN) ; */
			if (ptgbase >= FORMULA_OP_START && ptgbase < FORMULA_OP_START+FORMULA_OP_DATA_LEN) {
				FORMULA_OP_DATA *fd =
					&formula_op_data[ptgbase - FORMULA_OP_START];
				ExprTree *tr = expr_tree_new ();
				
				tr->oper = fd->op;
				tr->u.binary.value_b = parse_list_pop (&stack);
				tr->u.binary.value_a = parse_list_pop (&stack);
				parse_list_push (&stack, tr);
			} else {
				if (FORMULA_DEBUG>0)
					printf ("Unknown PTG 0x%x base %x\n", ptg, ptgbase);
				error=1 ;
			}
		}
		break ;
		}
/*		printf ("Ptg 0x%x length (not inc. ptg byte) %d\n", ptgbase, ptg_length) ; */
		cur+=    (ptg_length+1) ;
		len_left-= (ptg_length+1) ;
	}
	if (error)
	{
		if (FORMULA_DEBUG>0) {
			printf ("Unknown Formula/Array at [%d, %d]\n", fn_col, fn_row) ;
			printf ("formula data : \n") ;
			dump (mem, length) ;
		}
		
		parse_list_free (&stack) ;
		return expr_tree_string (_(" Unknown formula")) ;
	}
	
	if (!stack)
		return expr_tree_string ("Stack too short");
	if (g_list_length(stack) > 1)
		return expr_tree_string ("Too much data on stack");
	return parse_list_pop (&stack);
}

