/*
 * qpro-read.c: Read Quatro Pro files
 *
 * Copyright (C) 2002 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 *
 * Docs for the format used to be at
 *
 *   www.corel.com/partners_developers/ds/CO32SDK/docs/qp7/Qpf3recd.htm
 *   www.corel.com/partners_developers/ds/CO32SDK/docs/qp7/Qpf2intr.htm
 *
 * Try Wayback!
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <string.h>
#include "qpro.h"

#include <gutils.h>
#include <func.h>
#include <goffice/goffice.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <cell.h>
#include <value.h>
#include <expr.h>
#include <mstyle.h>
#include <sheet-style.h>
#include <style-color.h>
#include <parse-util.h>
#include <gnm-plugin.h>

#include <gsf/gsf-utils.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-msole.h>

GNM_PLUGIN_MODULE_HEADER;

gboolean qpro_file_probe (GOFileOpener const *fo, GsfInput *input,
			  GOFileProbeLevel pl);
void     qpro_file_open (GOFileOpener const *fo, GOIOContext *context,
			 WorkbookView *new_wb_view, GsfInput *input);

static gboolean
qpro_check_signature (GsfInput *input)
{
	guint8 const *header;
	guint16 version;

	if (gsf_input_seek (input, 0, G_SEEK_SET) ||
	    NULL == (header = gsf_input_read (input, 2+2+2, NULL)) ||
	    GSF_LE_GET_GUINT16 (header + 0) != 0 ||
	    GSF_LE_GET_GUINT16 (header + 2) != 2)
		return FALSE;
	version = GSF_LE_GET_GUINT16 (header + 4);
	return (version == 0x1001 || /* 'WB1' format, documented */
		version == 0x1002 || /* 'WB2' format, documented */
		version == 0x1006 ||  /* qpro 6.0 ?? */
		version == 0x1007);  /* qpro 7.0 ?? */
}

gboolean
qpro_file_probe (GOFileOpener const *fo, GsfInput *input, GOFileProbeLevel pl)
{
	GsfInfile *ole;
	GsfInput  *stream;
	gboolean res = FALSE;

	/* check for >= QPro 6.0 which is OLE based */
	ole = gsf_infile_msole_new (input, NULL);
	if (ole != NULL) {
		stream = gsf_infile_child_by_name (GSF_INFILE (ole),
						   "PerfectOffice_MAIN");
		if (stream != NULL) {
			res = qpro_check_signature (stream);
			g_object_unref (stream);
		}
		g_object_unref (ole);
	} else
		res = qpro_check_signature (input);

	return res;
}

typedef struct {
	GsfInput	*input;
	GOIOContext	*io_context;
	WorkbookView	*wbv;
	Workbook	*wb;
	Sheet		*cur_sheet;
	GIConv          converter;
	gboolean        corrupted;
} QProReadState;

static void
corrupted (QProReadState *state)
{
	if (!state->corrupted) {
		state->corrupted = TRUE;
		g_printerr (_("File is most likely corrupted.\n"));
	}
}

static void
q_condition_barf (QProReadState *state, const char *cond)
{
	corrupted (state);
	/* Translation is screwed here.  */
	g_printerr ("Condition \"%s\" failed.\n", cond);
}

#define Q_CHECK_CONDITION(cond_)				\
	do {							\
		if (!(cond_)) {					\
			q_condition_barf (state, #cond_);	\
			goto error;				\
		}						\
	} while (0)


static GnmValue *
qpro_new_string (QProReadState *state, gchar const *data)
{
	return value_new_string_nocopy (
		g_convert_with_iconv (data, -1, state->converter,
				      NULL, NULL, NULL));
}


static guint8 const *
qpro_get_record (QProReadState *state, guint16 *id, guint16 *len)
{
	guint8 const *data;

	data = gsf_input_read (state->input, 4, NULL);
	Q_CHECK_CONDITION (data != NULL);

	*id  = GSF_LE_GET_GUINT16 (data + 0);
	*len = GSF_LE_GET_GUINT16 (data + 2);

#if 0
	g_printerr ("%hd with %hd\n", *id, *len);
#endif

	if (*len == 0)
		return "";

	data = gsf_input_read (state->input, *len, NULL);

	switch (*id) {
	case QPRO_UNDOCUMENTED_837:
	case QPRO_UNDOCUMENTED_907:
		break; /* Nothing. */
	default:
		Q_CHECK_CONDITION (*len < 0x2000);
	}

	Q_CHECK_CONDITION (data != NULL);

	return data;

error:
	return NULL;
}

#define validate(f,expected) qpro_validate_len (state, #f, len, expected)

static gboolean
qpro_validate_len (QProReadState *state, char const *id, guint16 len, int expected_len)
{
	if (expected_len >= 0 && len != expected_len) {
		corrupted (state);
		g_printerr ("Invalid '%s' record of length %hd instead of %d\n",
			    id, len, expected_len);
		return FALSE;
	}

	return TRUE;
}

enum { ARGS_UNKNOWN = -1, ARGS_COUNT_FOLLOWS = -2 };

static struct {
	char const *name;
	int args;
} const qpro_functions[QPRO_OP_LAST_FUNC - QPRO_OP_FIRST_FUNC + 1] = {
	{ "err", ARGS_UNKNOWN }, /* No args -- returns error.  */
	{ "abs", 1 },
	{ "int", 1 },
	{ "sqrt", 1 },
	{ "log", 1 },
	{ "ln", 1 },
	{ "pi", 0 },
	{ "sin", 1 },
	{ "cos", 1 },
	{ "tan", 1 },
	{ "atan2", 2 },
	{ "atan", 1 },
	{ "asin", 1 },
	{ "acos", 1 },
	{ "exp", 1 },
	{ "mod", 2 },
	{ "choose", ARGS_COUNT_FOLLOWS },
	{ "isna", 1 },
	{ "iserr", 1 },
	{ "false", 0 },
	{ "true", 0 },
	{ "rand", 0 },
	{ "date", 1 },
	{ "now", 0 },
	{ "pmt", ARGS_UNKNOWN }, /* (pv,Rate,Nper) */
	{ "pv", ARGS_UNKNOWN },  /* (pmt, Rate, Nper) */
	{ "fv", ARGS_UNKNOWN },  /* (pmt, Rate, Nper) */
	{ "if", 3 },
	{ "day", 1 },
	{ "month", 1 },
	{ "year", 1 },
	{ "round", 2 },
	{ "time", 1 },
	{ "hour", 1 },
	{ "minute", 1 },
	{ "second", 1 },
	{ "isnumber", 1 },
	{ "istext", 1 },
	{ "len", 1 },
	{ "n", 1 },
	{ "fixed", 2 },
	{ "mid", 3 },
	{ "char", 1 },
	{ "code", 1 },
	{ "find", ARGS_UNKNOWN }, /* (subString, String,StartNumber) */
	{ "dateval", ARGS_UNKNOWN }, /* @datevalue(datestring) */
	{ "timeval", ARGS_UNKNOWN }, /* @timevalue(timestring) */
	{ "cellptr", ARGS_UNKNOWN }, /* @cellpointer(attribute) -- the requested attribute of the current cell */
	{ "sum", ARGS_COUNT_FOLLOWS },
	{ "avg", ARGS_COUNT_FOLLOWS },
	{ "count", ARGS_COUNT_FOLLOWS },
	{ "min", ARGS_COUNT_FOLLOWS },
	{ "max", ARGS_COUNT_FOLLOWS },
	{ "vlookup", 3 },
	{ "npv1", ARGS_UNKNOWN },
	{ "var", ARGS_COUNT_FOLLOWS },
	{ "std", ARGS_COUNT_FOLLOWS },
	{ "irr", ARGS_UNKNOWN }, /* @irr(guess,block) */
	{ "hlookup", 3 }, /* @hlookup(X,Block,Row) */
	{ "dsum", 3 },
	{ "davg", 3 },
	{ "dcount", 3 },
	{ "dmin", 3 },
	{ "dmax", 3 },
	{ "dvar", 3 },
	{ "dstdev", 3 },
	{ "index2d", ARGS_UNKNOWN }, /* Possibly @index(Block, Column, Row, <Page>) */
	{ "columns", 1 },
	{ "rows", 1 },
	{ "rept", 1 },
	{ "upper", 1 },
	{ "lower", 1 },
	{ "left", 2 },
	{ "right", 2 },
	{ "replace", 4 },
	{ "proper", 1 },
	{ "cell", ARGS_UNKNOWN }, /* @cell(attribute,Block) */
	{ "trim", 1 },
	{ "clean", 1 },
	{ "s", ARGS_UNKNOWN }, /* @s(Block) -- The string value of the upper left cell in Block (blank, if it is a value entry). */
	{ "n", ARGS_UNKNOWN }, /* @n(block) -- The numberic vlue of the upper lwft cell in Block (0, if it is a label or blank). */
	{ "exact", 2 },
	{ "call", ARGS_UNKNOWN },
	{ "at", ARGS_UNKNOWN }, /* @@(Cell) */
	{ "rate", ARGS_UNKNOWN }, /* @rate(Fv,Pv,Nper) */
	{ "term", ARGS_UNKNOWN }, /* @term(Pmt,Rate,Fv) */
	{ "cterm", ARGS_UNKNOWN }, /* @cterm(Rate,Fv,Pv) */
	{ "sln", ARGS_UNKNOWN }, /* @sln(cost,salvage,life) */
	{ "syd", ARGS_UNKNOWN }, /* (cost,salvage,life,period) */
	{ "ddb", ARGS_UNKNOWN }, /* (cost,salvage,life,period) */
	{ "stdp", ARGS_COUNT_FOLLOWS },
	{ "varp", ARGS_COUNT_FOLLOWS },
	{ "dstdevp", 3 },
	{ "dvarp", 3 },
	{ "pval", ARGS_UNKNOWN }, /* (Rate,Nper,Pmt,<Fv>,<Type>) */
	{ "paymt", ARGS_UNKNOWN }, /* (Rate,Nper,Pv,<Fv>,<Type>) */
	{ "fval", ARGS_UNKNOWN }, /* (Rate,Nper,Pmt,<Fv>,<Type>) */
	{ "nper", ARGS_UNKNOWN }, /* (Rate,Pmt,Pv,<Fv><Type>) */
	{ "irate", ARGS_UNKNOWN }, /* (Nper,Pmt,Pv,<Fv>,<Type>) */
	{ "ipaymt", ARGS_UNKNOWN }, /* (Rate,Per,Nper,Pv,<Fv>,<Type>) */
	{ "ppaymt", ARGS_UNKNOWN }, /* (Rate,Per,Nper,Pv,<Fv>,<Type>) */
	{ "sumproduct", 2 },
	{ "memavail", ARGS_UNKNOWN }, /* No args.  */
	{ "mememsavail", ARGS_UNKNOWN }, /* No args.  Returns NA.  */
	{ "fileexists", ARGS_UNKNOWN }, /* @fileexists(FileName) */
	{ "curvalue", ARGS_UNKNOWN },
	{ "degrees", 1 },
	{ "radians", 1 },
	{ "hex2dec", 1 },
	{ "dec2hex", 2 },
	{ "today", 0 },
	{ "npv2", ARGS_UNKNOWN },
	{ "cellindex2d", ARGS_UNKNOWN },
	{ "version", ARGS_UNKNOWN },
	{ NULL, ARGS_UNKNOWN },
	{ NULL, ARGS_UNKNOWN },
	{ NULL, ARGS_UNKNOWN },
	{ NULL, ARGS_UNKNOWN },
	{ NULL, ARGS_UNKNOWN },
	{ NULL, ARGS_UNKNOWN },
	{ NULL, ARGS_UNKNOWN },
	{ NULL, ARGS_UNKNOWN },
	{ "sheets", ARGS_UNKNOWN },
	{ NULL, ARGS_UNKNOWN },
	{ NULL, ARGS_UNKNOWN },
	{ "index3d", ARGS_UNKNOWN },
	{ "cellindex3d", ARGS_UNKNOWN },
	{ "property", ARGS_UNKNOWN },
	{ "ddelink", ARGS_UNKNOWN },
	{ "command", ARGS_UNKNOWN }
};

#ifdef DEBUG_MISSING
static void
dump_missing_functions (void)
{
	static gboolean done = FALSE;
	int i;

	if (!done) {
		for (i = QPRO_OP_FIRST_FUNC; i <= QPRO_OP_LAST_FUNC; i++) {
			char const *name = qpro_functions[i - QPRO_OP_FIRST_FUNC].name;
			int args = qpro_functions[i - QPRO_OP_FIRST_FUNC].args;
			GnmFunc *f;
			int dummy;

			if (!name || args != ARGS_UNKNOWN)
				continue;

			f = gnm_func_lookup (name, NULL);
			if (f == 0) {
				g_warning ("%s is not known.", name);
				continue;
			}

			gnm_func_count_args (f, &dummy, &dummy);

			g_warning ("Function %s has args %s.",
				   name, f->arg_names ? f->arg_names : "?");
		}
		done = TRUE;
	}
}
#endif

static const GnmExpr *
expr_stack_pop (GSList **pstack)
{
	const GnmExpr *expr;
	GSList *next;

	g_return_val_if_fail (pstack != NULL, NULL);

	expr = (*pstack)->data;
	next = (*pstack)->next;

	g_slist_free_1 (*pstack);

	*pstack = next;
	return expr;
}

static void
qpro_parse_formula (QProReadState *state, int col, int row,
		    guint8 const *data, guint8 const *end)
{
	guint16 magic, ref_offset;
#if 0
	int flags = GSF_LE_GET_GUINT16 (data + 8);
	int length = GSF_LE_GET_GUINT16 (data + 10);
#endif
	GnmValue   *val;
	GSList *stack = NULL;
	GnmExprTop const *texpr = NULL;
	guint8 const *refs, *fmla;

#ifdef DEBUG_MISSING
	dump_missing_functions ();
#endif

	Q_CHECK_CONDITION (end - data >= 14);
	magic = GSF_LE_GET_GUINT16 (data + 6) & 0x7ff8;
	ref_offset = GSF_LE_GET_GUINT16 (data + 12);

	fmla = data + 14;
	refs = fmla + ref_offset;
	Q_CHECK_CONDITION (refs <= end);

#if 0
	puts (cell_coord_name (col, row));
	gsf_mem_dump (data, 14);
	gsf_mem_dump (fmla, refs-fmla);
	gsf_mem_dump (refs, end-refs);
#endif

	while (fmla < refs && *fmla != QPRO_OP_EOF) {
		QProOperators op = *fmla++;
		GnmExpr const *expr = NULL;
#if 0
		g_print ("Operator %d.\n", op);
#endif
		switch (op) {
		case QPRO_OP_CONST_FLOAT:
			Q_CHECK_CONDITION (refs - fmla >= 8);
			expr = gnm_expr_new_constant (value_new_float (
				gsf_le_get_double (fmla)));
			fmla += 8;
			break;

		case QPRO_OP_CELLREF: {
			GnmCellRef ref;
			guint16 tmp;

			Q_CHECK_CONDITION (end - refs >= 6);
			tmp = GSF_LE_GET_GUINT16 (refs + 4);
			ref.sheet = NULL;
			ref.col = *((gint8 *)(refs + 2));
			ref.col_relative = (tmp & 0x4000) ? TRUE : FALSE;
			ref.row_relative = (tmp & 0x2000) ? TRUE : FALSE;
			if (ref.row_relative)
				ref.row = (int)(((gint16)((tmp & 0x1fff) << 3)) >> 3);
			else
				ref.row = tmp & 0x1fff;
			expr = gnm_expr_new_cellref (&ref);
			refs += 6;
			break;
		}

		case QPRO_OP_RANGEREF: {
			GnmCellRef a, b;
			guint16 tmp;

			Q_CHECK_CONDITION (end - refs >= 10);

			tmp = GSF_LE_GET_GUINT16 (refs + 4);
			a.sheet = NULL;
			a.col = *((gint8 *)(refs + 2));
			a.col_relative = (tmp & 0x4000) ? TRUE : FALSE;
			a.row_relative = (tmp & 0x2000) ? TRUE : FALSE;
			if (a.row_relative)
				a.row = (int)(((gint16)((tmp & 0x1fff) << 3)) >> 3);
			else
				a.row = tmp & 0x1fff;

			tmp = GSF_LE_GET_GUINT16 (refs + 8);
			b.sheet = NULL;
			b.col = *((gint8 *)(refs + 6));
			b.col_relative = (tmp & 0x4000) ? TRUE : FALSE;
			b.row_relative = (tmp & 0x2000) ? TRUE : FALSE;
			if (b.row_relative)
				b.row = (int)(((gint16)((tmp & 0x1fff) << 3)) >> 3);
			else
				b.row = tmp & 0x1fff;

			expr = gnm_expr_new_constant (
				value_new_cellrange_unsafe (&a, &b));
			refs += 10;
			break;
		}
		case QPRO_OP_EOF:
			break; /* exit */

		case QPRO_OP_PAREN:
			break; /* Currently just ignore.  */

		case QPRO_OP_CONST_INT:
			Q_CHECK_CONDITION (refs - fmla >= 2);
			expr = gnm_expr_new_constant (
				value_new_int ((gint16)GSF_LE_GET_GUINT16 (fmla)));
			fmla += 2;
			break;

		case QPRO_OP_CONST_STR:
			expr = gnm_expr_new_constant (qpro_new_string (state, fmla));
			fmla += strlen (fmla) + 1;
			break;

		case QPRO_OP_DEFAULT_ARG:
			expr = gnm_expr_new_constant (value_new_empty ());
			break;

		case QPRO_OP_ADD: case QPRO_OP_SUB:
		case QPRO_OP_MULT: case QPRO_OP_DIV:
		case QPRO_OP_EXP:
		case QPRO_OP_EQ: case QPRO_OP_NE:
		case QPRO_OP_LE: case QPRO_OP_GE:
		case QPRO_OP_LT: case QPRO_OP_GT:
		case QPRO_OP_CONCAT: Q_CHECK_CONDITION (stack && stack->next); {
			static GnmExprOp const binop_map[] = {
				GNM_EXPR_OP_ADD,	GNM_EXPR_OP_SUB,
				GNM_EXPR_OP_MULT,	GNM_EXPR_OP_DIV,
				GNM_EXPR_OP_EXP,
				GNM_EXPR_OP_EQUAL,	GNM_EXPR_OP_NOT_EQUAL,
				GNM_EXPR_OP_LTE,	GNM_EXPR_OP_GTE,
				GNM_EXPR_OP_LT,		GNM_EXPR_OP_GT,
				0, 0, 0, 0,
				GNM_EXPR_OP_CAT
			};
			GnmExpr const *r = expr_stack_pop (&stack);
			GnmExpr const *l = expr_stack_pop (&stack);
			expr = gnm_expr_new_binary (
				l, binop_map [op - QPRO_OP_ADD], r);
			break;
		}

		case QPRO_OP_AND:
		case QPRO_OP_OR: Q_CHECK_CONDITION (stack && stack->next); {
			GnmFunc *f = gnm_func_lookup (op == QPRO_OP_OR ? "or" : "and",
						      NULL);
			GnmExpr const *r = expr_stack_pop (&stack);
			GnmExpr const *l = expr_stack_pop (&stack);
			expr = gnm_expr_new_funcall2 (f, l, r);
			break;
		}

		case QPRO_OP_NOT: Q_CHECK_CONDITION (stack); {
			GnmFunc *f = gnm_func_lookup ("NOT", NULL);
			GnmExpr const *a = expr_stack_pop (&stack);
			expr = gnm_expr_new_funcall1 (f, a);
			break;
		}

		case QPRO_OP_UNARY_NEG:
		case QPRO_OP_UNARY_PLUS:
			Q_CHECK_CONDITION (stack);
			expr = expr_stack_pop (&stack);
			expr = gnm_expr_new_unary ((op == QPRO_OP_UNARY_NEG)
						   ? GNM_EXPR_OP_UNARY_NEG
						   : GNM_EXPR_OP_UNARY_PLUS,
				expr);
			break;

		default:
			if (QPRO_OP_FIRST_FUNC <= op && op <= QPRO_OP_LAST_FUNC) {
				int idx = op - QPRO_OP_FIRST_FUNC;
				char const *name = qpro_functions[idx].name;
				int args = qpro_functions[idx].args;
				GnmExprList *arglist = NULL;
				GnmFunc *f;

				if (name == NULL) {
					g_warning ("QPRO function %d is not known.", op);
					break;
				}
				/* FIXME : Add support for workbook local functions */
				f = gnm_func_lookup (name, NULL);
				if (f == NULL) {
					g_warning ("QPRO function %s is not supported!",
						   name);
					break;
				}

				if (args == ARGS_UNKNOWN) {
					g_warning ("QPRO function %s is not supported.",
						   name);
					goto error;
				}

				if (args == ARGS_COUNT_FOLLOWS) {
					Q_CHECK_CONDITION (refs - fmla >= 1);
					args = fmla[0];
					fmla++;
				}

				while (stack && args > 0) {
					arglist = g_slist_prepend (arglist,
								   (gpointer)expr_stack_pop (&stack));
					args--;
				}
				if (args > 0) {
					g_printerr ("File is probably corrupted.\n"
						    "(Expression stack is short by %d arguments)",
						   args);
					while (args > 0) {
						GnmExpr const *e = gnm_expr_new_constant (value_new_empty ());
						arglist = g_slist_prepend (arglist,
									   (gpointer)e);
						args--;
					}
				}
				expr = gnm_expr_new_funcall (f, arglist);
				break;
			} else {
				corrupted (state);
				g_printerr ("Operator %d encountered.\n", op);
			}
		}
		if (expr != NULL) {
			stack = g_slist_prepend (stack, (gpointer)expr);
		}
	}
	Q_CHECK_CONDITION (fmla != refs);
	Q_CHECK_CONDITION (stack != NULL);
	Q_CHECK_CONDITION (stack->next == NULL);

	texpr = gnm_expr_top_new (expr_stack_pop (&stack));

	switch (magic) {
	case 0x7ff0:
		val = value_new_error_VALUE (NULL);
		break;
	case 0x7ff8: {
		guint16 id, len;
		int new_row, new_col;

	again:
		data = qpro_get_record (state, &id, &len);
		Q_CHECK_CONDITION (data != NULL);

		if (id == QPRO_UNDOCUMENTED_270) {
			/*
			 * poker.wk3 has a few of these.  They seem to be
			 * more or less embedding a copy of the formula
			 * record.
			 */
#if 0
			g_warning ("Encountered 270 record.");
#endif
			goto again;
		}

		Q_CHECK_CONDITION (id == QPRO_FORMULA_STRING);
		Q_CHECK_CONDITION (len >= 7);

		new_col = data[0];
		new_row = GSF_LE_GET_GUINT16 (data + 2);

		/* Be anal */
		Q_CHECK_CONDITION (col == new_col && row == new_row);

		val = qpro_new_string (state, data + 7);
		break;
	}
	default:
		val = value_new_float (gsf_le_get_double (data));
	}

	gnm_cell_set_expr_and_value
		(sheet_cell_fetch (state->cur_sheet, col, row),
		 texpr, val, TRUE);
	gnm_expr_top_unref (texpr);
	return;

error:
	{
		GSList *tmp;

		for (tmp = stack; tmp; tmp = tmp->next) {
			GnmExpr *expr = tmp->data;
#ifdef DEBUG_EXPR_STACK
			GnmParsePos pp;
			char *p;

			pp.wb = state->wb;
			pp.sheet = state->cur_sheet;
			pp.eval.col = col;
			pp.eval.row = row;

			p = gnm_expr_as_string (expr, &pp,
						gnm_conventions_default);
			g_printerr ("Expr: %s\n", p);
			g_free (p);
#endif
			gnm_expr_free (expr);
		}
		g_slist_free (stack);
	}

	if (texpr)
		gnm_expr_top_unref (texpr);
}

static GnmStyle *
qpro_get_style (QProReadState *state, guint8 const *data)
{
#if 0
	unsigned attr_id = GSF_LE_GET_GUINT16 (data) >> 3;
	g_printerr ("Get Attr %u\n", attr_id);
#endif
	return sheet_style_default (state->cur_sheet);
}

static void
qpro_read_sheet (QProReadState *state)
{
	guint16 id, len;
	guint8 const *data;

	/* We can use col_name as a quick proxy for the defaul q-pro sheet names */
	char const *def_name = col_name (workbook_sheet_count (state->wb));
	Sheet *sheet = sheet_new (state->wb, def_name, 256, 65536);

	state->cur_sheet = sheet;
	workbook_sheet_attach (state->wb, sheet);
	sheet_flag_recompute_spans (sheet);
#if 0
	g_printerr ("----------> start %s\n", def_name);
#endif
	while (NULL != (data = qpro_get_record (state, &id, &len))) {
		switch (id) {
		case QPRO_BLANK_CELL:
			if (validate (QPRO_BLANK_CELL, 6))
				sheet_style_set_pos (sheet,
					data[0], GSF_LE_GET_GUINT16 (data + 2),
					qpro_get_style (state, data + 4));
			break;

		case QPRO_INTEGER_CELL:
			if (validate (QPRO_INTEGER_CELL, 8)) {
				int col = data[0];
				int row = GSF_LE_GET_GUINT16 (data + 2);
				sheet_style_set_pos (sheet, col, row,
					qpro_get_style (state, data + 4));
				gnm_cell_assign_value (sheet_cell_fetch (sheet, col, row),
					value_new_int (GSF_LE_GET_GUINT16 (data + 6)));
			}
			break;

		case QPRO_FLOATING_POINT_CELL:
			if (validate (QPRO_FLOATING_POINT_CELL, 14)) {
				int col = data[0];
				int row = GSF_LE_GET_GUINT16 (data + 2);
				sheet_style_set_pos (sheet, col, row,
					qpro_get_style (state, data + 4));
				gnm_cell_assign_value (sheet_cell_fetch (sheet, col, row),
					value_new_float (gsf_le_get_double (data + 6)));
			}
			break;

		case QPRO_LABEL_CELL:
			if (validate (QPRO_LABEL_CELL, -1)) {
				int col = data[0];
				int row = GSF_LE_GET_GUINT16 (data + 2);
				GnmHAlign align = GNM_HALIGN_GENERAL;
				GnmStyle *as = qpro_get_style (state, data + 4);
				GnmHAlign asa = gnm_style_is_element_set (as, MSTYLE_ALIGN_H)
					? gnm_style_get_align_h (as)
					: GNM_HALIGN_GENERAL;
				if (asa == GNM_HALIGN_GENERAL)
					asa = GNM_HALIGN_LEFT;

				sheet_style_set_pos (sheet, col, row, as);
				switch (data[6]) {
				case '\'': align = GNM_HALIGN_LEFT; break;
				case '^':  align = GNM_HALIGN_CENTER; break;
				case '"':  align = GNM_HALIGN_RIGHT; break;
				case '\\': break; /* Repeat */
				case '|':  break; /* Page break */
				case 0:    break; /* Nothing */
				default:
					g_printerr ("Ignoring unknown alignment\n");
				}
				if (align != GNM_HALIGN_GENERAL && align != asa) {
					GnmStyle *s = gnm_style_new ();
					gnm_style_set_align_h (s, align);
					sheet_style_apply_pos (sheet, col, row,
							       s);
				}

				gnm_cell_assign_value (sheet_cell_fetch (sheet, col, row),
						   qpro_new_string (state, data + 7));
			}
			break;

		case QPRO_FORMULA_CELL:
			if (validate (QPRO_FORMULA_CELL, -1)) {
				int col = data[0];
				int row = GSF_LE_GET_GUINT16 (data + 2);
				sheet_style_set_pos (sheet, col, row,
					qpro_get_style (state, data + 4));

				qpro_parse_formula (state, col, row,
					data + 6, data + len);
			}
			break;

		case QPRO_END_OF_PAGE:
			break;

		case QPRO_COLUMN_SIZE:
			/* ignore this, we auto generate this info */
			break;

		case QPRO_PROTECTION:
			if (validate (QPRO_PROTECTION, 1))
				g_object_set (sheet,
					      "protected", (data[0] == 0xff),
					      NULL);
			break;

		case QPRO_PAGE_NAME:
			if (validate (QPRO_PAGE_NAME, -1)) {
				char *utf8name =
					g_convert_with_iconv (data, -1,
							      state->converter,
							      NULL, NULL, NULL);
#warning "This is wrong, but the workbook interface is confused and needs a control."
				g_object_set (sheet, "name", utf8name, NULL);
				g_free (utf8name);
			}
			break;

		case QPRO_PAGE_ATTRIBUTE:
			/* Documented at 30.  Observed at 34.  */
			if (validate (QPRO_PAGE_ATTRIBUTE, -1)) {
#warning TODO, mostly simple
			}
			break;

		case QPRO_DEFAULT_ROW_HEIGHT_PANE1:
		case QPRO_DEFAULT_ROW_HEIGHT_PANE2:
			if (validate (QPRO_DEFAULT_ROW_HEIGHT, 2)) {
			}
			break;

		case QPRO_DEFAULT_COL_WIDTH_PANE1:
		case QPRO_DEFAULT_COL_WIDTH_PANE2:
			if (validate (QPRO_DEFAULT_COL_WIDTH, 2)) {
			}
			break;

		case QPRO_MAX_FONT_PANE1:
		case QPRO_MAX_FONT_PANE2 :
			/* just ignore for now */
			break;

		case QPRO_PAGE_TAB_COLOR :
			if (validate (QPRO_PAGE_TAB_COLOR, 4)) {
				GnmColor *bc = gnm_color_new_rgb8 (
					data[0], data[1], data[2]);
				g_object_set (sheet,
					      "tab-background", bc,
					      NULL);
				style_color_unref (bc);
			}
			break;

		case QPRO_PAGE_ZOOM_FACTOR :
			if (validate (QPRO_PAGE_ZOOM_FACTOR, 4)) {
				guint16 low  = GSF_LE_GET_GUINT16 (data);
				guint16 high = GSF_LE_GET_GUINT16 (data + 2);

				if (low == 100) {
					if (high < 10 || high > 400)
						go_io_warning (state->io_context,
							_("Invalid zoom %hd %%"), high);
					else
						g_object_set (sheet, "zoom-factor", high / 100.0, NULL);
				}
			}
			break;
		}

		if (id == QPRO_END_OF_PAGE)
			break;
	}
#if 0
	g_printerr ("----------< end\n");
#endif
	state->cur_sheet = NULL;
}

static void
qpro_read_workbook (QProReadState *state, GsfInput *input)
{
	guint16 id, len;
	guint8 const *data;

	state->input = input;
	gsf_input_seek (input, 0, G_SEEK_SET);

	while (NULL != (data = qpro_get_record (state, &id, &len))) {
		switch (id) {
		case QPRO_BEGINNING_OF_FILE:
			if (validate (QPRO_BEGINNING_OF_FILE, 2)) {
				guint16 version;
				version = GSF_LE_GET_GUINT16 (data);
				(void)version;
			}
			break;
		case QPRO_BEGINNING_OF_PAGE:
			qpro_read_sheet (state);
			break;

		default :
			if (id > QPRO_LAST_SANE_ID)
				go_io_warning (state->io_context,
					_("Invalid record %d of length %hd"),
					id, len);
		};
		if (id == QPRO_END_OF_FILE)
			break;
	}
}

void
qpro_file_open (GOFileOpener const *fo, GOIOContext *context,
		WorkbookView *new_wb_view, GsfInput *input)
{
	QProReadState state;
	GsfInput  *stream = NULL;
	GsfInfile *ole;

	state.io_context = context;
	state.wbv = new_wb_view;
	state.wb = wb_view_get_workbook (new_wb_view);
	state.cur_sheet = NULL;
	state.converter	 = g_iconv_open ("UTF-8", "ISO-8859-1");
	state.corrupted = FALSE;

	/* check for >= QPro 6.0 which is OLE based */
	ole = gsf_infile_msole_new (input, NULL);
	if (ole != NULL) {
		stream = gsf_infile_child_by_name (GSF_INFILE (ole),
						   "PerfectOffice_MAIN");
		if (stream != NULL) {
			qpro_read_workbook (&state, stream);
			g_object_unref (stream);
		} else
			go_io_warning (context,
				_("Unable to find the PerfectOffice_MAIN stream.  Is this really a Quattro Pro file?"));
		g_object_unref (ole);
	} else
		qpro_read_workbook (&state, input);

	gsf_iconv_close (state.converter);
}
