/**
 * lotus-formula.c: Lotus 123 formula support for Gnumeric
 *
 * Author:
 *    See: README
 *    Michael Meeks <michael@imagiantor.com>
 **/
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "lotus.h"
#include "lotus-types.h"
#include "lotus-formula.h"

#include <expr.h>
#include <parse-util.h>
#include <value.h>
#include <gutils.h>
#include <func.h>

#define FORMULA_DEBUG 0

typedef struct {
	gint     args; /* -1 for multiple arguments */
	guint16  idx;
	const gchar   *name;
	enum { NORMAL, UNARY, BINOP } special;
	guint32  data;
} func_struct_t;

const func_struct_t functions[] = {
	{ 1, 0x08, "-",		UNARY, GNM_EXPR_OP_UNARY_NEG },
	{ 2, 0x09, "+",		BINOP, GNM_EXPR_OP_ADD },
	{ 2, 0x0A, "-",		BINOP, GNM_EXPR_OP_SUB },
	{ 2, 0x0B, "*",		BINOP, GNM_EXPR_OP_MULT },
	{ 2, 0x0C, "/",		BINOP, GNM_EXPR_OP_DIV },
	{ 2, 0x0D, "pow",	BINOP, GNM_EXPR_OP_EXP },
	{ 2, 0x0E, "EQ",	BINOP, GNM_EXPR_OP_EQUAL },
	{ 2, 0x0F, "NE",	BINOP, GNM_EXPR_OP_NOT_EQUAL },
	{ 2, 0x10, "LE",	BINOP, GNM_EXPR_OP_LTE },
	{ 2, 0x11, "GE",	BINOP, GNM_EXPR_OP_GTE },
	{ 2, 0x12, "LT",	BINOP, GNM_EXPR_OP_LT },
	{ 2, 0x13, "GT",	BINOP, GNM_EXPR_OP_GT },
/*	{ 2, 0x14, "bit-and",	BINOP, OPER_AND },
	{ 2, 0x15, "bit-or",	BINOP, OPER_OR }, FIXME */
	{ 1, 0x16, "NOT",	NORMAL, 0 },
	{ 1, 0x17, "+",		UNARY, GNM_EXPR_OP_UNARY_PLUS },
	{ 0, 0x1F, "NA",	NORMAL, 0 },
	{ 1, 0x20, "ERR",	NORMAL, 0 },
	{ 1, 0x21, "abs",	NORMAL, 0 },
	{ 1, 0x22, "floor",	NORMAL, 0 },
	{ 1, 0x23, "sqrt",	NORMAL, 0 },
	{ 1, 0x24, "LOG10",	NORMAL, 0 },
	{ 1, 0x25, "log",	NORMAL, 0 },
	{ 0, 0x26, "PI",	NORMAL, 0 },
	{ 1, 0x27, "sin",	NORMAL, 0 },
	{ 1, 0x28, "cos",	NORMAL, 0 },
	{ 1, 0x29, "tan",	NORMAL, 0 },
	{ 2, 0x2A, "ATAN2",	NORMAL, 0 },
	{ 1, 0x2B, "atan",	NORMAL, 0 },
	{ 1, 0x2C, "asin",	NORMAL, 0 },
	{ 1, 0x2D, "acos",	NORMAL, 0 },
	{ 1, 0x2E, "exp",	NORMAL, 0 },
	{ 2, 0x2F, "MOD",	NORMAL, 0 },
	{ -1, 0x30, "CHOOSE",	NORMAL, 0 },
	{ 1, 0x31, "ISNA",	NORMAL, 0 },
	{ 1, 0x32, "ISERR",	NORMAL, 0 },
	{ 0, 0x33, "FALSE",	NORMAL, 0 },
	{ 0, 0x34, "TRUE",	NORMAL, 0 },
	{ 0, 0x35, "RAND",	NORMAL, 0 },
	{ 3, 0x36, "DATE",	NORMAL, 0 },
	{ 0, 0x37, "TODAY",	NORMAL, 0 },
	{ 3, 0x38, "PMT",	NORMAL, 0 },
	{ 3, 0x39, "PV",	NORMAL, 0 },
	{ 3, 0x3A, "FV",	NORMAL, 0 },
	{ 3, 0x3B, "IF",	NORMAL, 0 },
	{ 1, 0x3C, "DAY",	NORMAL, 0 },
	{ 1, 0x3D, "MONTH",	NORMAL, 0 },
	{ 1, 0x3E, "YEAR",	NORMAL, 0 },
	{ 2, 0x3F, "ROUND",	NORMAL, 0 },
	{ 3, 0x40, "TIME",	NORMAL, 0 },
	{ 1, 0x41, "HOUR",	NORMAL, 0 },
	{ 1, 0x42, "MINUTE",	NORMAL, 0 },
	{ 1, 0x43, "SECOND",	NORMAL, 0 },
	{ 1, 0x44, "ISNUMBER",	NORMAL, 0 },
	{ 1, 0x45, "ISTEXT",	NORMAL, 0 },
	{ 1, 0x46, "LEN",	NORMAL, 0 },
	{ 1, 0x47, "VALUE",	NORMAL, 0 },
	{ 2, 0x48, "FIXED",	NORMAL, 0 },
	{ 3, 0x49, "MID",	NORMAL, 0 },
	{ 1, 0x4A, "CHAR",	NORMAL, 0 },
	{ 1, 0x4B, "CODE",	NORMAL, 0 },
	{ 2, 0x4C, "FIND",	NORMAL, 0 },
	{ 1, 0x4D, "DATEVALUE",	NORMAL, 0 },
	{ 1, 0x4E, "TIMEVALUE", NORMAL, 0 },
	{ 1, 0x4F, "CELLPOINTER", NORMAL, 0 },

        /* 50-54 take an additional byte for the number of args */
	{ -1, 0x50, "SUM",	NORMAL, 0 },
	{ -1, 0x51, "AVERAGE",	NORMAL, 0 },
	{ -1, 0x52, "COUNT",	NORMAL, 0 },
	{ -1, 0x53, "MIN",	NORMAL, 0 },
	{ -1, 0x54, "MAX",	NORMAL, 0 },
	{ 3, 0x55, "VLOOKUP",	NORMAL, 0 },
	{ 2, 0x56, "NPV",	NORMAL, 0 },
	{ 1, 0x57, "VAR",	NORMAL, 0 },
	{ 1, 0x58, "STDEVP",	NORMAL, 0 },
	{ 2, 0x59, "IRR",	NORMAL, 0 },
	{ 3, 0x5A, "HLOOKUP",	NORMAL, 0 },
	{ 3, 0x5B, "DSUM",	NORMAL, 0 },
	{ 3, 0x5C, "DAVERAGE",	NORMAL, 0 },
	{ 3, 0x5D, "DCOUNT",	NORMAL, 0 },
	{ 3, 0x5E, "DMIN",	NORMAL, 0 },
	{ 3, 0x5F, "DMAX",	NORMAL, 0 },
	{ 3, 0x60, "DVAR",	NORMAL, 0 },
	{ 1, 0x61, "DSTD",	NORMAL, 0 },
	{ 3, 0x62, "INDEX",	NORMAL, 0 },
	{ 1, 0x63, "COLUMNS",	NORMAL, 0 },
	{ 1, 0x64, "ROWS",	NORMAL, 0 },
	{ 2, 0x65, "REPT",	NORMAL, 0 },
	{ 1, 0x66, "UPPER",	NORMAL, 0 },
	{ 1, 0x67, "LOWER",	NORMAL, 0 },
	{ 2, 0x68, "LEFT",	NORMAL, 0 },
	{ 2, 0x69, "RIGHT",	NORMAL, 0 },
	{ 4, 0x6A, "REPLACE",	NORMAL, 0 },
	{ 1, 0x6B, "PROPER",	NORMAL, 0 },
	{ 2, 0x6C, "CELL",	NORMAL, 0 },
	{ 1, 0x6D, "TRIM",	NORMAL, 0 },
	{ 1, 0x6E, "CLEAN",	NORMAL, 0 },
	{ 2, 0x71, "EXACT",	NORMAL, 0 },
	{ 1, 0x72, "CALL",	NORMAL, 0 },
	{ 1, 0x73, "INDIRECT",	NORMAL, 0 },
	{ 3, 0x74, "RATE",	NORMAL, 0 },
	{ 3, 0x75, "TERM",	NORMAL, 0 },
	{ 3, 0x76, "NPER",	NORMAL, 0 },
	{ 3, 0x77, "SLN",	NORMAL, 0 },
	{ 4, 0x78, "SYD",	NORMAL, 0 },
	{ 4, 0x79, "DDB",	NORMAL, 0 },

	/* A pile of extras that WK4 and greater generates when writing WK1 */
	{ 1, 0x9C, "SPLFUNC",	NORMAL, 0 },
	{ 1, 0x9D, "LEVELS",	NORMAL, 0 },
	{ 1, 0x9E, "INFO",	NORMAL, 0 },
	{ 1, 0x9F, "SUMPROD",	NORMAL, 0 },
	{ 1, 0xA0, "ISRANGE",	NORMAL, 0 },
	{ 3, 0xA1, "DGET",	NORMAL, 0 },
	{ 1, 0xA2, "DQUERY",	NORMAL, 0 },
	{ 1, 0xA3, "COORD",	NORMAL, 0 },
	{ 1, 0xA4, "DDB2",	NORMAL, 0 },
	{ 1, 0xA5, "DVARS",	NORMAL, 0 },
	{ 1, 0xA6, "DSTDS",	NORMAL, 0 },
	{ 1, 0xA7, "VARS",	NORMAL, 0 },
	{ 1, 0xA8, "STDS",	NORMAL, 0 },
	{ 1, 0xA9, "D360",	NORMAL, 0 },
	{ 1, 0xAA, "BLANK",	NORMAL, 0 },
	{ 1, 0xAB, "ISAPP",	NORMAL, 0 },
	{ 1, 0xAC, "ISAAF",	NORMAL, 0 },
	{ 1, 0xAD, "WEEKDAY",	NORMAL, 0 },
	{ 1, 0xAE, "DATEDIF",	NORMAL, 0 },
	{ 1, 0xAF, "RANK",	NORMAL, 0 },
	{ 1, 0xB0, "NUMBERSTR",	NORMAL, 0 },
	{ 1, 0xB1, "DATESTR",	NORMAL, 0 },
	{ 1, 0xB2, "DECIMAL",	NORMAL, 0 },
	{ 1, 0xB3, "HEX",	NORMAL, 0 },
	{ 1, 0xB4, "DB",	NORMAL, 0 },
	{ 1, 0xB5, "GANRI",	NORMAL, 0 },
	{ 1, 0xB6, "GANKIN",	NORMAL, 0 },
	{ 1, 0xB7, "FULLP",	NORMAL, 0 },
	{ 1, 0xB8, "HALFP",	NORMAL, 0 },
	{ 1, 0xB9, "PUREAVG",	NORMAL, 0 },
	{ 1, 0xBA, "PURECOUNT",	NORMAL, 0 },
	{ 1, 0xBB, "PUREMAX",	NORMAL, 0 },
	{ 1, 0xBC, "PUREMIN",	NORMAL, 0 },
	{ 1, 0xBD, "PURESTD",	NORMAL, 0 },
	{ 1, 0xBE, "PUREVAR",	NORMAL, 0 },
	{ 1, 0xBF, "PURESTDS",	NORMAL, 0 },
	{ 1, 0xC0, "PUREVARS",	NORMAL, 0 },
	{ 1, 0xC1, "PMT1",	NORMAL, 0 },
	{ 1, 0xC2, "PV1",	NORMAL, 0 },
	{ 1, 0xC3, "FV1",	NORMAL, 0 },
	{ 1, 0xC4, "TERM1",	NORMAL, 0 },
	{ 1, 0xC5, "DSUMDIFF",	NORMAL, 0 },
	{ 1, 0xC6, "DAVGDIFF",	NORMAL, 0 },
	{ 1, 0xC7, "DCOUNTDIFF",NORMAL, 0 },
	{ 1, 0xC8, "DMINDIFF",	NORMAL, 0 },
	{ 1, 0xC9, "DMAXDIFF",	NORMAL, 0 },
	{ 1, 0xCA, "DVARDIFF",	NORMAL, 0 },
	{ 1, 0xCB, "DSTDDIFF",	NORMAL, 0 },
	{ 1, 0xCC, "INDEXDIFF",	NORMAL, 0 },
	{ 1, 0xCD, "QMARK",	NORMAL, 0 },
	{ 1, 0xCE, "QQMARK",	NORMAL, 0 },
	{ 1, 0xCF, "ISEMPTY",	NORMAL, 0 }
};

static void
parse_list_push_expr (GnmExprList **list, GnmExpr const *pd)
{
	if (!pd)
		printf ("FIXME: Pushing nothing onto lotus function stack\n");
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
	i = gnumeric_get_le_uint16 (dataa);
	if (i & 0x8000) {
		ref->col_relative = TRUE;
		ref->col = sign_extend (i & 0x3fff);
	} else {
		ref->col_relative = FALSE;
		ref->col = i & 0x3fff;
	}

	i = gnumeric_get_le_uint16 (datab);
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

static gint32
find_function (guint16 idx)
{
	guint lp;

	for (lp = 0; lp < sizeof (functions) / sizeof (func_struct_t); lp++) {
		if (idx == functions[lp].idx)
			return lp;
	}
	return -1;
}

static gint32
make_function (GnmExprList **stack, guint16 idx, guint8 const *data, int col, int row)
{
	gint32 ans, numargs;
	func_struct_t const *f = functions + idx;

	if (f->args < 0) {
		numargs = *(data + 1);
		ans     = 2;
	} else {
		numargs = f->args;
		ans     = 1;
	}

	switch (f->special) {
	case NORMAL: {
		GnmExprList  *args = parse_list_last_n (stack, numargs, col, row);
		FunctionDefinition *func = func_lookup_by_name (f->name, NULL);
		if (func == NULL)
			func = function_add_placeholder (f->name, "Lotus ");
		parse_list_push_expr (stack, gnm_expr_new_funcall (func, args));
		break;
	}

	case BINOP: {
		GnmExpr const *r = parse_list_pop (stack, col, row);
		GnmExpr const *l = parse_list_pop (stack, col, row);
		parse_list_push_expr (stack, gnm_expr_new_binary (l, f->data, r));
		break;
	}

	case UNARY: {
		GnmExpr const *r = parse_list_pop (stack, col, row);
		parse_list_push_expr (stack, gnm_expr_new_unary (f->data, r));
		break;
	}

	default:
		g_warning ("Unknown formula type");
	};

	return ans;
}


GnmExpr const *
lotus_parse_formula (Sheet *sheet, guint32 col, guint32 row,
		     guint8 const *data, guint32 len)
{
	GnmExprList *stack = NULL;
	guint     i;
	CellRef   a, b;
	Value    *v;
	gboolean  done  = FALSE;
	gboolean  error = FALSE;

	for (i = 0; (i < len) & !done;) {
		switch (data[i]) {
		case LOTUS_FORMULA_CONSTANT:
			v = value_new_float (gnumeric_get_le_double (data + i + 1));
			parse_list_push_value (&stack, v);
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

			v = value_new_cellrange (&a, &b, col, row);
			parse_list_push_value (&stack, v);
			i += 9;
			break;
		case LOTUS_FORMULA_RETURN:
			done = TRUE;
			break;
		case LOTUS_FORMULA_BRACKET:
			i += 1; /* Ignore */
			break;
		case LOTUS_FORMULA_INTEGER:
			v = value_new_int (gnumeric_get_le_int16 (data + i + 1));
			parse_list_push_value (&stack, v);
			i += 3;
			break;

		case LOTUS_FORMULA_STRING:
			v = value_new_string (data + i + 1);
			parse_list_push_value (&stack, v);
			i += 2 + strlen (data + i + 1);
			break;

		case LOTUS_FORMULA_UNARY_PLUS:
			i++;
			break;

		default: /* Sacrifice speed for elegance every time */
		{
			gint32 idx = find_function (data[i]);
			if (idx == -1) {
				error = TRUE;
				done  = TRUE;
				g_warning ("%s : unknown PTG 0x%x", cell_coord_name (col, row), (unsigned)data[i]);
			} else
				i += make_function (&stack, idx, data + i, col, row);
		}
		}
	}
	return parse_list_pop (&stack, col, row);
}
