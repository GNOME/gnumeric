/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * lotus-formula.c: Lotus 123 formula support for Gnumeric
 *
 * Author:
 *    See: README
 *    Michael Meeks <michael@imagiantor.com>
 * Revamped in Aug 2002
 *    Jody Goldberg <jody@gnome.org>
 **/
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <string.h>
#include "lotus.h"
#include "lotus-types.h"
#include "lotus-formula.h"

#include <expr.h>
#include <parse-util.h>
#include <value.h>
#include <gutils.h>
#include <func.h>
#include <gsf/gsf-utils.h>

#define FORMULA_DEBUG 0

typedef struct _Wk1Func Wk1Func;
struct _Wk1Func {
	int		 args; /* -1 for multiple arguments */
	unsigned	 idx;
	char const	*name;
	int (*handler) (GnmExprList **stack, Wk1Func const *func, guint8 const *data, int col, int row);
	guint32  data;
};

static int wk1_unary_func  (GnmExprList **stack, Wk1Func const *func, guint8 const *data, int col, int row);
static int wk1_binary_func (GnmExprList **stack, Wk1Func const *func, guint8 const *data, int col, int row);
static int wk1_std_func	   (GnmExprList **stack, Wk1Func const *func, guint8 const *data, int col, int row);
/* a,b,c -> a,,-c,b */
static int wk1_nper_func   (GnmExprList **stack, Wk1Func const *func, guint8 const *data, int col, int row);
/* year - 1900 */
static int wk1_year_func   (GnmExprList **stack, Wk1Func const *func, guint8 const *data, int col, int row);
/* find - 1 */
static int wk1_find_func   (GnmExprList **stack, Wk1Func const *func, guint8 const *data, int col, int row);
/* a,b,c -> b,c,-a */
static int wk1_fv_pv_pmt_func (GnmExprList **stack, Wk1Func const *func, guint8 const *data, int col, int row);
/* a,b -> b,a */
static int wk1_irr_func    (GnmExprList **stack, Wk1Func const *func, guint8 const *data, int col, int row);
static int wk1_rate_func   (GnmExprList **stack, Wk1Func const *func, guint8 const *data, int col, int row);

const Wk1Func functions[] = {
	{  1, 0x08, "-",	wk1_unary_func,  GNM_EXPR_OP_UNARY_NEG },
	{  2, 0x09, "+",	wk1_binary_func, GNM_EXPR_OP_ADD },
	{  2, 0x0A, "-",	wk1_binary_func, GNM_EXPR_OP_SUB },
	{  2, 0x0B, "*",	wk1_binary_func, GNM_EXPR_OP_MULT },
	{  2, 0x0C, "/",	wk1_binary_func, GNM_EXPR_OP_DIV },
	{  2, 0x0D, "pow",	wk1_binary_func, GNM_EXPR_OP_EXP },
	{  2, 0x0E, "EQ",	wk1_binary_func, GNM_EXPR_OP_EQUAL },
	{  2, 0x0F, "NE",	wk1_binary_func, GNM_EXPR_OP_NOT_EQUAL },
	{  2, 0x10, "LE",	wk1_binary_func, GNM_EXPR_OP_LTE },
	{  2, 0x11, "GE",	wk1_binary_func, GNM_EXPR_OP_GTE },
	{  2, 0x12, "LT",	wk1_binary_func, GNM_EXPR_OP_LT },
	{  2, 0x13, "GT",	wk1_binary_func, GNM_EXPR_OP_GT },
	{  2, 0x14, "BITAND",	wk1_std_func, 0 },	/* from number theory */
	{  2, 0x15, "BITOR",	wk1_std_func, 0 },	/* from number theory */
	{  1, 0x16, "NOT",	wk1_std_func, 0 },
	{  1, 0x17, "+",	wk1_unary_func, GNM_EXPR_OP_UNARY_PLUS },
	{  0, 0x1F, "NA",	wk1_std_func, 0 },
	{  1, 0x20, "ERR",	wk1_std_func, 0 },
	{  1, 0x21, "abs",	wk1_std_func, 0 },
	{  1, 0x22, "floor",	wk1_std_func, 0 },
	{  1, 0x23, "sqrt",	wk1_std_func, 0 },
	{  1, 0x24, "LOG10",	wk1_std_func, 0 },
	{  1, 0x25, "log",	wk1_std_func, 0 },
	{  0, 0x26, "PI",	wk1_std_func, 0 },
	{  1, 0x27, "sin",	wk1_std_func, 0 },
	{  1, 0x28, "cos",	wk1_std_func, 0 },
	{  1, 0x29, "tan",	wk1_std_func, 0 },
	{  2, 0x2A, "ATAN2",	wk1_std_func, 0 },
	{  1, 0x2B, "atan",	wk1_std_func, 0 },
	{  1, 0x2C, "asin",	wk1_std_func, 0 },
	{  1, 0x2D, "acos",	wk1_std_func, 0 },
	{  1, 0x2E, "exp",	wk1_std_func, 0 },
	{  2, 0x2F, "MOD",	wk1_std_func, 0 },
	{ -1, 0x30, "CHOOSE",	wk1_std_func, 0 },
	{  1, 0x31, "ISNA",	wk1_std_func, 0 },
	{  1, 0x32, "ISERR",	wk1_std_func, 0 },
	{  0, 0x33, "FALSE",	wk1_std_func, 0 },
	{  0, 0x34, "TRUE",	wk1_std_func, 0 },
	{  0, 0x35, "RAND",	wk1_std_func, 0 },
	{  3, 0x36, "DATE",	wk1_std_func, 0 },
	{  0, 0x37, "TODAY",	wk1_std_func, 0 },
	{  3, 0x38, "PMT",	wk1_fv_pv_pmt_func, 0 },
	{  3, 0x39, "PV",	wk1_fv_pv_pmt_func, 0 },
	{  3, 0x3A, "FV",	wk1_fv_pv_pmt_func, 0 },
	{  3, 0x3B, "IF",	wk1_std_func, 0 },
	{  1, 0x3C, "DAY",	wk1_std_func, 0 },
	{  1, 0x3D, "MONTH",	wk1_std_func, 0 },
	{  1, 0x3E, "YEAR",	wk1_year_func, 0 },
	{  2, 0x3F, "ROUND",	wk1_std_func, 0 },
	{  3, 0x40, "TIME",	wk1_std_func, 0 },
	{  1, 0x41, "HOUR",	wk1_std_func, 0 },
	{  1, 0x42, "MINUTE",	wk1_std_func, 0 },
	{  1, 0x43, "SECOND",	wk1_std_func, 0 },
	{  1, 0x44, "ISNUMBER",	wk1_std_func, 0 },
	{  1, 0x45, "ISTEXT",	wk1_std_func, 0 },
	{  1, 0x46, "LEN",	wk1_std_func, 0 },
	{  1, 0x47, "VALUE",	wk1_std_func, 0 },
	{  2, 0x48, "FIXED",	wk1_std_func, 0 },
	{  3, 0x49, "MID",	wk1_std_func, 0 },
	{  1, 0x4A, "CHAR",	wk1_std_func, 0 },
	{  1, 0x4B, "CODE",	wk1_std_func, 0 },
	{  3, 0x4C, "FIND",	wk1_find_func, 0 },
	{  1, 0x4D, "DATEVALUE",wk1_std_func, 0 },
	{  1, 0x4E, "TIMEVALUE",wk1_std_func, 0 },
	{  1, 0x4F, "CELLPOINTER", wk1_std_func, 0 },
	{ -1, 0x50, "SUM",	wk1_std_func, 0 },
	{ -1, 0x51, "AVERAGE",	wk1_std_func, 0 },
	{ -1, 0x52, "COUNT",	wk1_std_func, 0 },
	{ -1, 0x53, "MIN",	wk1_std_func, 0 },
	{ -1, 0x54, "MAX",	wk1_std_func, 0 },

	{  3, 0x55, "VLOOKUP",	wk1_std_func, 0 },
	{  2, 0x56, "NPV",	wk1_std_func, 0 },
	{ -1, 0x57, "VAR",	wk1_std_func, 0 },
	{ -1, 0x58, "STDEVP",	wk1_std_func, 0 },
	{  2, 0x59, "IRR",	wk1_irr_func, 0 },
	{  3, 0x5A, "HLOOKUP",	wk1_std_func, 0 },
	{ -2, 0x5B, "DSUM",	wk1_std_func, 0 },
	{ -2, 0x5C, "DAVERAGE",	wk1_std_func, 0 },
	{ -2, 0x5D, "DCOUNT",	wk1_std_func, 0 },
	{ -2, 0x5E, "DMIN",	wk1_std_func, 0 },
	{ -2, 0x5F, "DMAX",	wk1_std_func, 0 },
	{ -2, 0x60, "DVAR",	wk1_std_func, 0 },
	{ -2, 0x61, "DSTD",	wk1_std_func, 0 },
	{ -3, 0x62, "INDEX",	wk1_std_func, 0 },
	{  1, 0x63, "COLUMNS",	wk1_std_func, 0 },
	{  1, 0x64, "ROWS",	wk1_std_func, 0 },
	{  2, 0x65, "REPT",	wk1_std_func, 0 },
	{  1, 0x66, "UPPER",	wk1_std_func, 0 },
	{  1, 0x67, "LOWER",	wk1_std_func, 0 },
	{  2, 0x68, "LEFT",	wk1_std_func, 0 },
	{  2, 0x69, "RIGHT",	wk1_std_func, 0 },
	{  4, 0x6A, "REPLACE",	wk1_std_func, 0 },
	{  1, 0x6B, "PROPER",	wk1_std_func, 0 },
	{  2, 0x6C, "CELL",	wk1_std_func, 0 },
	{  1, 0x6D, "TRIM",	wk1_std_func, 0 },
	{  1, 0x6E, "CLEAN",	wk1_std_func, 0 },
	{  2, 0x71, "EXACT",	wk1_std_func, 0 },
	{  1, 0x72, "CALL",	wk1_std_func, 0 },
	{  1, 0x73, "INDIRECT",	wk1_std_func, 0 },
	{  3, 0x74, "RATE",	wk1_rate_func, 0 },
	{  3, 0x75, "TERM",	wk1_std_func, 0 },
	{  3, 0x76, "NPER",	wk1_nper_func, 0 },
	{  3, 0x77, "SLN",	wk1_std_func, 0 },
	{  4, 0x78, "SYD",	wk1_std_func, 0 },
	{  4, 0x79, "DDB",	wk1_std_func, 0 },

	/* A pile of extras that WK4 and greater generates when writing WK1 */
	{  1, 0x9C, "SPLFUNC",	wk1_std_func, 0 },
	{  1, 0x9D, "LEVELS",	wk1_std_func, 0 },
	{  1, 0x9E, "INFO",	wk1_std_func, 0 },
	{ -1, 0x9F, "SUMPROD",	wk1_std_func, 0 },
	{ -1, 0xA0, "ISRANGE",	wk1_std_func, 0 },
	{  3, 0xA1, "DGET",	wk1_std_func, 0 },
	{  3, 0xA2, "DQUERY",	wk1_std_func, 0 },
	{  1, 0xA3, "COORD",	wk1_std_func, 0 },
	{  4, 0xA4, "VDB",	wk1_std_func, 0 },
	{  3, 0xA5, "DVARS",	wk1_std_func, 0 },
	{  3, 0xA6, "DSTDS",	wk1_std_func, 0 },
	{ -1, 0xA7, "VARS",	wk1_std_func, 0 },
	{ -1, 0xA8, "STDS",	wk1_std_func, 0 },
	{  1, 0xA9, "D360",	wk1_std_func, 0 },
	{  1, 0xAA, "BLANK",	wk1_std_func, 0 },
	{  1, 0xAB, "ISAPP",	wk1_std_func, 0 },
	{  1, 0xAC, "ISAAF",	wk1_std_func, 0 },
	{  1, 0xAD, "WEEKDAY",	wk1_std_func, 0 },
	{  1, 0xAE, "DATEDIF",	wk1_std_func, 0 },
	{  1, 0xAF, "RANK",	wk1_std_func, 0 },
	{  1, 0xB0, "NUMBERSTR",wk1_std_func, 0 },
	{  1, 0xB1, "DATESTR",	wk1_std_func, 0 },
	{  1, 0xB2, "DECIMAL",	wk1_std_func, 0 },
	{  1, 0xB3, "HEX",	wk1_std_func, 0 },
	{  1, 0xB4, "DB",	wk1_std_func, 0 },
	{  1, 0xB5, "GANRI",	wk1_std_func, 0 },
	{  1, 0xB6, "GANKIN",	wk1_std_func, 0 },
	{  1, 0xB7, "FULLP",	wk1_std_func, 0 },
	{  1, 0xB8, "HALFP",	wk1_std_func, 0 },
	{  1, 0xB9, "PUREAVG",	wk1_std_func, 0 },
	{  1, 0xBA, "PURECOUNT",wk1_std_func, 0 },
	{  1, 0xBB, "PUREMAX",	wk1_std_func, 0 },
	{  1, 0xBC, "PUREMIN",	wk1_std_func, 0 },
	{  1, 0xBD, "PURESTD",	wk1_std_func, 0 },
	{  1, 0xBE, "PUREVAR",	wk1_std_func, 0 },
	{  1, 0xBF, "PURESTDS",	wk1_std_func, 0 },
	{  1, 0xC0, "PUREVARS",	wk1_std_func, 0 },
	{  1, 0xC1, "PMT1",	wk1_std_func, 0 },
	{  1, 0xC2, "PV1",	wk1_std_func, 0 },
	{  1, 0xC3, "FV1",	wk1_std_func, 0 },
	{  1, 0xC4, "TERM1",	wk1_std_func, 0 },
	{  1, 0xC5, "DSUMDIFF",	wk1_std_func, 0 },
	{  1, 0xC6, "DAVGDIFF",	wk1_std_func, 0 },
	{  1, 0xC7, "DCOUNTDIFF",wk1_std_func, 0 },
	{  1, 0xC8, "DMINDIFF",	wk1_std_func, 0 },
	{  1, 0xC9, "DMAXDIFF",	wk1_std_func, 0 },
	{  1, 0xCA, "DVARDIFF",	wk1_std_func, 0 },
	{  1, 0xCB, "DSTDDIFF",	wk1_std_func, 0 },
	{  1, 0xCC, "INDEXDIFF",wk1_std_func, 0 },
	{  1, 0xCD, "QMARK",	wk1_std_func, 0 },
	{  1, 0xCE, "QQMARK",	wk1_std_func, 0 },
	{ -1, 0xCF, "ISEMPTY",	wk1_std_func, 0 }
};

static void
parse_list_push_expr (GnmExprList **list, GnmExpr const *pd)
{
	g_return_if_fail (pd != NULL);
	*list = gnm_expr_list_prepend (*list, pd) ;
}

static void
parse_list_push_value (GnmExprList **list, Value *v)
{
	parse_list_push_expr (list, gnm_expr_new_constant (v));
}

static GnmExpr const *
parse_list_pop (GnmExprList **list, int col, int row)
{
	/* Get the head */
	GnmExprList *tmp = g_slist_nth (*list, 0);
	if (tmp != NULL) {
		GnmExpr const *ans = tmp->data ;
		*list = g_slist_remove (*list, ans) ;
		return ans ;
	}

	g_warning ("%s : Incorrect number of parsed formula arguments",
		   cell_coord_name (col, row));
	return gnm_expr_new_constant (value_new_error (NULL, "WrongArgs"));
}

/**
 * Returns a new list composed of the last n items pop'd off the list.
 **/
static GnmExprList *
parse_list_last_n (GnmExprList **list, gint n, int col, int row)
{
	GnmExprList *l = NULL;
	while (n-- > 0)
		l = gnm_expr_list_prepend (l, parse_list_pop (list, col, row));
	return l;
}


static int
wk1_unary_func  (GnmExprList **stack, Wk1Func const *f,
		 guint8 const *data, int col, int row)
{
	GnmExpr const *r = parse_list_pop (stack, col, row);
	parse_list_push_expr (stack, gnm_expr_new_unary (f->data, r));
	return 1;
}
static int
wk1_binary_func (GnmExprList **stack, Wk1Func const *f,
		 guint8 const *data, int col, int row)
{
	GnmExpr const *r = parse_list_pop (stack, col, row);
	GnmExpr const *l = parse_list_pop (stack, col, row);
	parse_list_push_expr (stack, gnm_expr_new_binary (l, f->data, r));
	return 1;
}

static int
wk1_std_func (GnmExprList **stack, Wk1Func const *f,
	      guint8 const *data, int col, int row)
{
	GnmFunc *func = gnm_func_lookup (f->name, NULL);
	int numargs, size;

	if (f->args < 0) {
		numargs = data[1];
		size = 2;
	} else {
		numargs = f->args;
		size = 1;
	}

	if (func == NULL) {
		func = gnm_func_add_placeholder (f->name, "Lotus ", FALSE);
		puts (cell_coord_name (col, row));
	}
	parse_list_push_expr (stack, gnm_expr_new_funcall (func,
		parse_list_last_n (stack, numargs, col, row)));

	return size;
}
static int
wk1_nper_func (GnmExprList **stack, Wk1Func const *func,
	       guint8 const *data, int col, int row)
{
	/* a,b,c -> a,,-c,b */
	return wk1_std_func (stack, func, data, col, row);
}
static int
wk1_year_func (GnmExprList **stack, Wk1Func const *func,
	       guint8 const *data, int col, int row)
{
	/* year - 1900 */
	return wk1_std_func (stack, func, data, col, row);
}
static int
wk1_find_func (GnmExprList **stack, Wk1Func const *func,
	       guint8 const *data, int col, int row)
{
	/* find - 1 */
	return wk1_std_func (stack, func, data, col, row);
}
static int
wk1_fv_pv_pmt_func (GnmExprList **stack, Wk1Func const *func,
		     guint8 const *data, int col, int row)
{
	/* a,b,c -> b,c,-a */
	return wk1_std_func (stack, func, data, col, row);
}
static int
wk1_irr_func (GnmExprList **stack, Wk1Func const *func,
		 guint8 const *data, int col, int row)
{
	/* a,b -> b,a */
	return wk1_std_func (stack, func, data, col, row);
}
static int
wk1_rate_func (GnmExprList **stack, Wk1Func const *func,
	       guint8 const *data, int col, int row)
{
	return wk1_std_func (stack, func, data, col, row);
}

static gint32
make_function (GnmExprList **stack, guint8 const *data, int col, int row)
{
	Wk1Func const *f = NULL;
	unsigned i;

	for (i = 0; i < G_N_ELEMENTS (functions); i++)
		if (*data == functions [i].idx) {
			f = functions + i;
			break;
		}

	if (f == NULL) {
		g_warning ("%s : unknown PTG 0x%x", cell_coord_name (col, row), *data);
		return 1;
	}
	return (f->handler) (stack, f, data, col, row);
}

static gint16
sign_extend (guint16 num)
{
	gint16 i = (num << 3);
	return (i / 8);
}

/* FIXME: dodgy stuff, hacked for now */
static void
get_cellref (CellRef *ref, guint8 const *dataa, guint8 const *datab,
	     guint32 orig_col, guint32 orig_row)
{
	guint16 i;

	ref->sheet = NULL;
	i = GSF_LE_GET_GUINT16 (dataa);
	if (i & 0x8000) {
		ref->col_relative = TRUE;
		ref->col = sign_extend (i & 0x3fff);
	} else {
		ref->col_relative = FALSE;
		ref->col = i & 0x3fff;
	}

	i = GSF_LE_GET_GUINT16 (datab);
	if (i & 0x8000) {
		ref->row_relative = TRUE;
		ref->row = sign_extend (i & 0x3fff);
	} else {
		ref->row_relative = FALSE;
		ref->row = i & 0x3fff;
	}

#if FORMULA_DEBUG > 0
	printf ("0x%x 0x%x -> (%d, %d)\n", *(guint16 *)dataa, *(guint16 *)datab,
		ref->col, ref->row);
#endif
}

GnmExpr const *
lotus_parse_formula (LotusWk1Read *state, guint32 col, guint32 row,
		     guint8 const *data, guint32 len)
{
	GnmExprList *stack = NULL;
	guint     i;
	CellRef   a, b;
	gboolean  done  = FALSE;

	for (i = 0; (i < len) & !done;) {
		switch (data[i]) {
		case LOTUS_FORMULA_CONSTANT:
			parse_list_push_value (&stack,
				value_new_float (gsf_le_get_double (data + i + 1)));
			i += 9;
			break;

		case LOTUS_FORMULA_VARIABLE:
			get_cellref (&a, data + i + 1, data + i + 3, col, row);
			parse_list_push_expr (&stack, gnm_expr_new_cellref (&a));
			i += 5;
			break;

		case LOTUS_FORMULA_RANGE:
			get_cellref (&a, data + i + 1, data + i + 3, col, row);
			get_cellref (&b, data + i + 5, data + i + 7, col, row);
			parse_list_push_value (&stack,
				value_new_cellrange (&a, &b, col, row));
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
				lotus_new_string (state, data + i + 1));
			i += 2 + strlen (data + i + 1);
			break;

		case LOTUS_FORMULA_UNARY_PLUS:
			i++;
			break;

		default:
			i += make_function (&stack, data + i, col, row);
		}
	}

	if (gnm_expr_list_length (stack) != 1)
		g_warning ("%s : args remain on stack",
			   cell_coord_name (col, row));
	return parse_list_pop (&stack, col, row);
}
