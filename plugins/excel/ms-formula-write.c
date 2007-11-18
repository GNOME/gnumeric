/* vim: set sw=8 ts=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * ms-formula-write.c: export GnmExpr to xls
 *
 * Authors:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2005 Jody Goldberg
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "ms-formula-write.h"
#include "ms-excel-write.h"
#include "formula-types.h"
#include "excel.h"
#include "ms-biff.h"
#include "formula-types.h"
#include "boot.h"

#include <sheet.h>
#include <gutils.h>
#include <workbook-priv.h>
#include <func.h>
#include <value.h>
#include <cell.h>
#include <expr.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <str.h>
#include <parse-util.h>
#include <goffice/app/io-context.h>

#include <gsf/gsf-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#define FORMULA_DEBUG 0

static guint
sheet_pair_hash (ExcelSheetPair const *sp)
{
	return ((GPOINTER_TO_INT(sp->a) >> 2) & 0xffff) | ((GPOINTER_TO_INT(sp->b) << 14) & 0xffff0000);
}

static gint
sheet_pair_cmp (ExcelSheetPair const *a, ExcelSheetPair const *b)
{
	return a->a == b->a && a->b == b->b;
}

static void
sheet_pair_add_if_unknown (GHashTable *hash, ExcelSheetPair const *pair)
{
	if (NULL == g_hash_table_lookup (hash, pair)) {
		ExcelSheetPair *new_pair = g_new (ExcelSheetPair, 1);
		new_pair->a = pair->a;
		new_pair->b = pair->b;
		new_pair->idx_a = new_pair->idx_b = 0;
		g_hash_table_insert (hash, new_pair, new_pair);
		/* fprintf (stderr, "Adding %p:%p\n", pair->a, pair->b); */
	}
}

void
excel_write_prep_sheet (ExcelWriteState *ewb, Sheet const *sheet)
{
	ExcelSheetPair pair;
	pair.a = pair.b = sheet;
	if (pair.a != NULL)
		sheet_pair_add_if_unknown (ewb->sheet_pairs, &pair);
}

/**
 * excel_write_prep_expr :
 * @ewb :
 * @texpr :
 *
 *  Searches for interesting functions, names, or sheets.
 * and builds a database of things to write out later.
 **/
static void
do_excel_write_prep_expr (ExcelWriteState *ewb, GnmExpr const *expr)
{
	switch (GNM_EXPR_GET_OPER (expr)) {

	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY:
		do_excel_write_prep_expr (ewb, expr->binary.value_a);
		do_excel_write_prep_expr (ewb, expr->binary.value_b);
		break;

	case GNM_EXPR_OP_ANY_UNARY:
		do_excel_write_prep_expr (ewb, expr->unary.value);
		break;

	case GNM_EXPR_OP_FUNCALL: {
		GnmFunc *func = expr->func.func;
		ExcelFunc *ef = g_hash_table_lookup (ewb->function_map, func);
		int i;

		for (i = 0; i < expr->func.argc; i++)
			do_excel_write_prep_expr (ewb, expr->func.argv[i]);

		if (ef != NULL)
			return;

		ef = g_new (ExcelFunc, 1);
		if (!(func->flags & (GNM_FUNC_IS_PLACEHOLDER|GNM_FUNC_IS_WORKBOOK_LOCAL))) {
			for (i = 0; i < excel_func_desc_size; i++)
				if (!g_ascii_strcasecmp (excel_func_desc[i].name, func->name)) {
					ef->efunc = excel_func_desc + i;
					ef->idx = i;
					ef->macro_name = NULL;
					break;
				}
		} else
			i = excel_func_desc_size;

		if (i >= excel_func_desc_size) {
			ef->efunc = NULL;
			if (func->flags & GNM_FUNC_IS_WORKBOOK_LOCAL) {
				ef->macro_name = g_strdup (func->name);
				ef->idx = -1;
			} else {
				g_ptr_array_add (ewb->externnames, func);
				ef->macro_name = NULL;
				ef->idx = ewb->externnames->len;
			}
		}
		g_hash_table_insert (ewb->function_map, func, ef);
		break;
	}
	case GNM_EXPR_OP_ARRAY_CORNER:
		do_excel_write_prep_expr (ewb, expr->array_corner.expr);
		break;
	case GNM_EXPR_OP_ARRAY_ELEM:
		break;

	case GNM_EXPR_OP_SET: {
		int i;

		for (i = 0; i < expr->set.argc; i++)
			do_excel_write_prep_expr (ewb, expr->set.argv[i]);
		break;
	}

	case GNM_EXPR_OP_CELLREF:
		excel_write_prep_sheet (ewb, expr->cellref.ref.sheet);
		break;

	case GNM_EXPR_OP_CONSTANT: {
		GnmValue const *v = expr->constant.value;
		if (v->type == VALUE_CELLRANGE) {
			ExcelSheetPair pair;
			pair.a = v->v_range.cell.a.sheet;
			pair.b = v->v_range.cell.b.sheet;
			if (pair.a != NULL) {
				if (pair.b == NULL)
					pair.b = pair.a;
				sheet_pair_add_if_unknown (ewb->sheet_pairs, &pair);
			}
		}
		break;
	}

	default:
		break;
	}
}

void
excel_write_prep_expr (ExcelWriteState *ewb, GnmExprTop const *texpr)
{
	do_excel_write_prep_expr (ewb, texpr->expr);
}

/**
 * excel_write_prep_expressions :
 * @ewb: state
 *
 * Initialize the data structures for exporting functions
 **/
void
excel_write_prep_expressions (ExcelWriteState *ewb)
{
	g_return_if_fail (ewb != NULL);

	ewb->sheet_pairs = g_hash_table_new_full (
		(GHashFunc) sheet_pair_hash, (GEqualFunc) sheet_pair_cmp,
		NULL, g_free);
}

/****************************************************************************/

typedef enum {
	XL_REF		= 0,	/* R */
	XL_VAL		= 1,	/* V */
	XL_ARRAY	= 2,	/* A */
	XL_ROOT		= 3,	/* v */
	NUM_XL_TYPES	= 3
} XLOpType;
typedef enum {
	CTXT_CELL  = 0,
	CTXT_ARRAY = 1,
	CTXT_NAME  = 2,
	NUM_CTXTS
} XLContextType;

typedef struct {
	ExcelWriteState *ewb;
	Sheet		*sheet;
	int		 col, row;
	gboolean	 use_name_variant;
	XLContextType	 context;

	/* Accumulator for storing arrays after the expression */
	GSList		*arrays;
} PolishData;

static void write_node (PolishData *pd, GnmExpr const *expr,
			int paren_level, XLOpType target_type);

#define CLASS_REF	0x00
#define CLASS_VAL	0x20
#define CLASS_ARRAY	0x40
static const guint8 xl_op_class[NUM_CTXTS][NUM_XL_TYPES][NUM_XL_TYPES+1] = {
	{ /* CELL | Wants REF	Wants VAL	Wants ARR	From Root */
/* From REF */	 { CLASS_REF,	CLASS_VAL,	CLASS_ARRAY,	CLASS_VAL },
/* From VAL */	 { CLASS_VAL,	CLASS_VAL,	CLASS_VAL,	CLASS_VAL },
/* From ARRAY */ { CLASS_ARRAY,	CLASS_VAL,	CLASS_ARRAY,	CLASS_VAL },
	},
	{ /* ARR  | Wants REF	Wants VAL	Wants ARR	From Root */
/* From REF */	 { CLASS_REF,	CLASS_VAL,	CLASS_ARRAY,	CLASS_VAL },
/* From VAL */	 { CLASS_ARRAY,	CLASS_VAL,	CLASS_ARRAY,	CLASS_VAL },
/* From ARRAY */ { CLASS_ARRAY,	CLASS_VAL,	CLASS_ARRAY,	CLASS_ARRAY },
	},
	{ /* NAME | Wants REF	Wants VAL	Wants ARR	From Root */
/* From REF */	 { CLASS_REF,	CLASS_ARRAY,	CLASS_ARRAY,	CLASS_REF },
/* From VAL */	 { CLASS_ARRAY,	CLASS_ARRAY,	CLASS_ARRAY,	CLASS_ARRAY },
/* From ARRAY */ { CLASS_ARRAY,	CLASS_ARRAY,	CLASS_ARRAY,	CLASS_ARRAY },
	}
};

static XLOpType
xl_map_char_to_type (char t)
{
	if (t == 'V')
		return XL_VAL;
	if (t == 'R')
		return XL_REF;
	if (t == 'A')
		return XL_ARRAY;
	if (t == 'v')
		return XL_ROOT;
	g_warning ("unknown op class '%c' assuming val", t);
	return XL_VAL;
}

static guint8
xl_get_op_class (PolishData *pd, XLOpType src_type, XLOpType target_type)
{
	return xl_op_class[pd->context][src_type][target_type];
}

static void
push_guint8 (PolishData *pd, guint8 b)
{
	ms_biff_put_var_write (pd->ewb->bp, &b, sizeof(guint8));
}

static void
push_guint16 (PolishData *pd, guint16 b)
{
	guint8 data[2];
	GSF_LE_SET_GUINT16 (data, b);
	ms_biff_put_var_write (pd->ewb->bp, data, sizeof(data));
}

static void
push_gint16 (PolishData *pd, gint16 b)
{
	guint8 data[2];
	GSF_LE_SET_GINT16 (data, b);
	ms_biff_put_var_write (pd->ewb->bp, data, sizeof(data));
}

static void
push_guint32 (PolishData *pd, guint32 b)
{
	guint8 data[4];
	GSF_LE_SET_GUINT32 (data, b);
	ms_biff_put_var_write (pd->ewb->bp, data, sizeof(data));
}

static void
write_cellref_v7 (PolishData *pd, GnmCellRef const *ref,
		  guint8 *out_col, guint8 *out_row)
{
	guint    row, col;

	if (pd->use_name_variant)
		col = ref->col & 0xff;
	else if (ref->col_relative)
		col = ref->col + pd->col;
	else
		col = ref->col;

	if (ref->row_relative && !pd->use_name_variant)
		row = ref->row + pd->row;
	else
		row = ref->row;

	if (ref->col_relative)
		row |= 0x4000;
	if (ref->row_relative)
		row |= 0x8000;

	GSF_LE_SET_GUINT16 (out_row, row);
	GSF_LE_SET_GUINT8  (out_col, col);
}

static void
write_cellref_v8 (PolishData *pd, GnmCellRef const *ref,
		  guint8 *out_col, guint8 *out_row)
{
	guint    row, col;

	if (pd->use_name_variant)
		col = ref->col & 0xff;
	else if (ref->col_relative)
		col = ref->col + pd->col;
	else
		col = ref->col;

	if (ref->row_relative && !pd->use_name_variant)
		row = ref->row + pd->row;
	else
		row = ref->row;

	if (ref->col_relative)
		col |= 0x4000;
	if (ref->row_relative)
		col |= 0x8000;

	GSF_LE_SET_GUINT16 (out_row, row);
	GSF_LE_SET_GUINT16 (out_col, col);
}

static void
write_string (PolishData *pd, gchar const *txt)
{
	push_guint8 (pd, FORMULA_PTG_STR);
	excel_write_string (pd->ewb->bp, STR_ONE_BYTE_LENGTH, txt);
}

static void
excel_formula_write_CELLREF (PolishData *pd, GnmCellRef const *ref,
			     Sheet *sheet_b, XLOpType target_type)
{
	guint8 data[24];
	guint8 ptg;
	guint8 op_class = xl_get_op_class (pd, XL_REF, target_type);

	g_return_if_fail (pd);
	g_return_if_fail (ref);

	if (ref->sheet == NULL) {

		g_return_if_fail (sheet_b == NULL);

		if (pd->context == CTXT_NAME) {
			g_warning ("XL does not support unqualified references in global names");
		}

		ptg = (pd->use_name_variant && (ref->col_relative || ref->row_relative))
			? FORMULA_PTG_REFN : FORMULA_PTG_REF;
		push_guint8 (pd, ptg + op_class);
		if (pd->ewb->bp->version <= MS_BIFF_V7) {
			write_cellref_v7 (pd, ref, data + 2, data);
			ms_biff_put_var_write (pd->ewb->bp, data, 3);
		} else {
			write_cellref_v8 (pd, ref, data + 2, data);
			ms_biff_put_var_write (pd->ewb->bp, data, 4);
		}
	} else {
		push_guint8 (pd, FORMULA_PTG_REF_3D + op_class);
		if (pd->ewb->bp->version <= MS_BIFF_V7) {
			guint16 idx_a, idx_b;
			gint16 ixals;

			/* FIXME no external references for now */
			g_return_if_fail (pd->ewb->base.wb == ref->sheet->workbook);

			idx_a = ref->sheet->index_in_wb;
			idx_b = (sheet_b != NULL) ? sheet_b->index_in_wb : idx_a;
			ixals = -(idx_a+1);
			GSF_LE_SET_GUINT16 (data, ixals);
			GSF_LE_SET_GUINT32 (data +  2, 0x0);
			GSF_LE_SET_GUINT32 (data +  6, 0x0);
			GSF_LE_SET_GUINT16 (data + 10, idx_a);
			GSF_LE_SET_GUINT16 (data + 12, idx_b);
			write_cellref_v7 (pd, ref, data + 16, data + 14);
			ms_biff_put_var_write (pd->ewb->bp, data, 17);
		} else {
			guint16 extn_idx = excel_write_get_externsheet_idx (
						pd->ewb, ref->sheet, sheet_b);
			GSF_LE_SET_GUINT16 (data, extn_idx);
			write_cellref_v8 (pd, ref, data + 4, data + 2);
			ms_biff_put_var_write (pd->ewb->bp, data, 6);
		}
	}
}

static void
excel_formula_write_AREA (PolishData *pd, GnmCellRef const *a, GnmCellRef const *b,
			  XLOpType target_type)
{
	guint8 data[24];
	guint8 ptg;
	guint8 op_class = xl_get_op_class (pd, XL_REF, target_type);

	if (a->sheet == NULL && b->sheet == NULL) {

		if (pd->context == CTXT_NAME) {
			g_warning ("XL does not support unqualified references in global names");
		}

		ptg = (pd->use_name_variant &&
		       (a->col_relative || a->row_relative ||
			b->col_relative || b->row_relative))
			? FORMULA_PTG_AREAN : FORMULA_PTG_AREA;
		push_guint8 (pd, ptg + op_class);
		if (pd->ewb->bp->version <= MS_BIFF_V7) {
			write_cellref_v7 (pd, a, data + 4, data);
			write_cellref_v7 (pd, b, data + 5, data + 2);
			ms_biff_put_var_write (pd->ewb->bp, data, 6);
		} else {
			write_cellref_v8 (pd, a, data + 4, data + 0);
			write_cellref_v8 (pd, b, data + 6, data + 2);
			ms_biff_put_var_write (pd->ewb->bp, data, 8);
		}
		return;
	}

	g_return_if_fail (a->sheet != NULL);

	if (a->col != b->col || a->row != b->row ||
	    a->col_relative != b->col_relative ||
	    a->row_relative != b->row_relative) {
		g_return_if_fail (a->sheet != NULL);

		push_guint8 (pd, FORMULA_PTG_AREA_3D + op_class);
		if (pd->ewb->bp->version <= MS_BIFF_V7) {
			guint16 idx_a, idx_b;
			gint16 ixals;

			/* FIXME no external references for now */
			g_return_if_fail (pd->ewb->base.wb == a->sheet->workbook);

			idx_a = a->sheet->index_in_wb;
			idx_b = (b->sheet != NULL)
				? b->sheet->index_in_wb : idx_a;

			ixals = -(idx_a+1);

			GSF_LE_SET_GUINT16 (data, ixals);
			GSF_LE_SET_GUINT32 (data +  2, 0x0);
			GSF_LE_SET_GUINT32 (data +  6, 0x0);
			GSF_LE_SET_GUINT16 (data + 10, idx_a);
			GSF_LE_SET_GUINT16 (data + 12, idx_b);
			write_cellref_v7 (pd, a, data + 18, data + 14);
			write_cellref_v7 (pd, b, data + 19, data + 16);
			ms_biff_put_var_write (pd->ewb->bp, data, 20);
		} else {
			guint16 extn_idx = excel_write_get_externsheet_idx (
						pd->ewb, a->sheet, b->sheet);
			GSF_LE_SET_GUINT16 (data, extn_idx);
			write_cellref_v8 (pd, a, data + 6, data + 2);
			write_cellref_v8 (pd, b, data + 8, data + 4);
			ms_biff_put_var_write (pd->ewb->bp, data, 10);
		}
	} else
		excel_formula_write_CELLREF (pd, a, b->sheet, target_type);
}

static void
write_funcall (PolishData *pd, GnmExpr const *expr,
	       XLOpType target_type)
{
	static guint8 const zeros [12];

	int arg;
	gboolean prompt   = FALSE;
	gboolean cmdequiv = FALSE;
	char const *arg_types = NULL;
	GnmFunc *func = expr->func.func;
	ExcelFunc *ef = g_hash_table_lookup (pd->ewb->function_map, func);
	XLOpType arg_type = XL_VAL; /* default */

	g_return_if_fail (ef != NULL);

	if (ef->efunc == NULL) {
		if (ef->macro_name != NULL) {
			push_guint8 (pd, FORMULA_PTG_NAME);
			push_guint16 (pd, ef->idx);
			ms_biff_put_var_write (pd->ewb->bp, zeros,
				(pd->ewb->bp->version <= MS_BIFF_V7) ? 12 : 2);
		} else {
			push_guint8 (pd, FORMULA_PTG_NAME_X);
			if (pd->ewb->bp->version <= MS_BIFF_V7) {
				/* The Magic Addin entry is after the real sheets
				 * for globals (names that call addins) and
				 * for locals (fomulas that call addins) */
				push_gint16  (pd, pd->ewb->esheets->len + 1);
				ms_biff_put_var_write (pd->ewb->bp, zeros, 8);
				push_guint16 (pd, ef->idx);
				ms_biff_put_var_write (pd->ewb->bp, zeros, 12);
			} else {
				/* I write the Addin Magic entry 1st */
				push_guint16 (pd, 0);
				push_guint16 (pd, ef->idx);
				push_guint16 (pd, 0); /* reserved */
			}
		}
	} else
		arg_types = ef->efunc->known_args;

	for (arg = 0; arg < expr->func.argc; arg++)
		if (ef->efunc != NULL && arg >= ef->efunc->max_args) {
			gnm_io_warning (pd->ewb->io_context,
				_("Too many arguments for function '%s', MS Excel can only handle %d not %d"),
				ef->efunc->name, ef->efunc->max_args, expr->func.argc);
			break;
		} else { /* convert the args */
			if (arg_types != NULL && *arg_types) {
				arg_type = xl_map_char_to_type (*arg_types);
				if (arg_types[1])
					arg_types++;
			}
			write_node (pd, expr->func.argv[arg], 0, arg_type);
		}

	if (ef->efunc != NULL) {
		guint8 op_class = xl_get_op_class (pd,
			xl_map_char_to_type (ef->efunc->type), target_type);

#if FORMULA_DEBUG > 1
		printf ("Writing function '%s' as idx %d, args %d\n",
			name, ef->u.std.idx, fce->u.std.efunc->num_known_args);
#endif

		/* If XL requires more arguments than we do
		 * pad the remainder with missing args */
		for ( ; arg < ef->efunc->min_args ; arg++)
			push_guint8 (pd, FORMULA_PTG_MISSARG);

		if (ef->efunc->min_args != ef->efunc->max_args) {
			push_guint8  (pd, FORMULA_PTG_FUNC_VAR + op_class);
			push_guint8  (pd, arg | (prompt ? 0x80 : 0));
			push_guint16 (pd, ef->idx  | (cmdequiv ? 0x8000 : 0));
		} else {
			push_guint8  (pd, FORMULA_PTG_FUNC + op_class);
			push_guint16 (pd, ef->idx);
		}
	} else { /* Undocumented, assume result is XL_VAL */
		push_guint8  (pd, FORMULA_PTG_FUNC_VAR +
			xl_get_op_class (pd,  XL_VAL, target_type));
		push_guint8  (pd, (arg + 1)	| (prompt   ? 0x80 : 0));
		push_guint16 (pd, 0xff		| (cmdequiv ? 0x8000 : 0));
	}
}

static void
excel_formula_write_NAME_v8 (PolishData *pd, GnmExpr const *expr,
			     XLOpType target_type)
{
	guint8 data [7];
	gpointer tmp;
	unsigned name_idx;

	memset (data, 0, sizeof data);

	tmp = g_hash_table_lookup (pd->ewb->names,
				   (gpointer)expr->name.name);
	g_return_if_fail (tmp != NULL);

	name_idx = GPOINTER_TO_UINT (tmp);
	if (expr->name.optional_scope == NULL) {
		GSF_LE_SET_GUINT8  (data + 0, FORMULA_PTG_NAME +
			xl_get_op_class (pd, XL_REF, target_type));
		GSF_LE_SET_GUINT16 (data + 1, name_idx);
		ms_biff_put_var_write (pd->ewb->bp, data, 5);
	} else {
		guint16 extn_idx = excel_write_get_externsheet_idx (pd->ewb,
			expr->name.optional_scope, NULL);
		GSF_LE_SET_GUINT8  (data + 0, FORMULA_PTG_NAME_X +
			xl_get_op_class (pd, XL_REF, target_type));
		GSF_LE_SET_GUINT16 (data + 1, extn_idx);
		GSF_LE_SET_GUINT16 (data + 3, name_idx);
		ms_biff_put_var_write (pd->ewb->bp, data, 7);
	}
}

static void
excel_formula_write_NAME_v7 (PolishData *pd, GnmExpr const *expr,
			     XLOpType target_type)
{
	guint8 data [25];
	gpointer tmp;
	unsigned name_idx;

	memset (data, 0, sizeof data);

	tmp = g_hash_table_lookup (pd->ewb->names,
				   (gpointer)expr->name.name);
	g_return_if_fail (tmp != NULL);

	name_idx = GPOINTER_TO_UINT (tmp);
	if (expr->name.optional_scope == NULL) {
		GSF_LE_SET_GUINT8  (data + 0, FORMULA_PTG_NAME +
			xl_get_op_class (pd, XL_REF, target_type));
		GSF_LE_SET_GUINT16 (data + 1, name_idx);
		ms_biff_put_var_write (pd->ewb->bp, data, 15);
	} else {
		int externsheet = (pd->sheet == expr->name.optional_scope)
			? (int) (pd->ewb->esheets->len + 1)
			: expr->name.optional_scope->index_in_wb;
		guint32 id = ++(pd->ewb->unique_name_id);

		GSF_LE_SET_GUINT8  (data +  0, FORMULA_PTG_NAME_X +
			xl_get_op_class (pd, XL_REF, target_type));
		GSF_LE_SET_GUINT16 (data +  1, -(externsheet+1));
		GSF_LE_SET_GUINT16 (data + 11, name_idx);
		GSF_LE_SET_GUINT16 (data +  9,   1); /* undocumented marked 'reserved' */
		GSF_LE_SET_GUINT16 (data + 19, 0xf); /* undocumented marked 'reserved' */
		GSF_LE_SET_GUINT32 (data + 21, id); /* undocumented marked 'reserved' */
		ms_biff_put_var_write (pd->ewb->bp, data, 25);
	}
}

static void
write_node (PolishData *pd, GnmExpr const *expr, int paren_level,
	    XLOpType target_type)
{
	static struct {
		guint8 xl_op;
		int prec;		      /* Precedences -- should match parser.y  */
		int assoc_left, assoc_right;  /* 0: no, 1: yes.  */
	} const operations [] = {
		{ FORMULA_PTG_EQUAL,	 1, 1, 0 },
		{ FORMULA_PTG_GT,	 1, 1, 0 },
		{ FORMULA_PTG_LT,	 1, 1, 0 },
		{ FORMULA_PTG_GTE,	 1, 1, 0 },
		{ FORMULA_PTG_LTE,	 1, 1, 0 },
		{ FORMULA_PTG_NOT_EQUAL, 1, 1, 0 },
		{ FORMULA_PTG_ADD,	 3, 1, 0 },
		{ FORMULA_PTG_SUB,	 3, 1, 0 },
		{ FORMULA_PTG_MULT,	 4, 1, 0 },
		{ FORMULA_PTG_DIV,	 4, 1, 0 },
		{ FORMULA_PTG_EXP,	 5, 0, 1 },
		{ FORMULA_PTG_CONCAT,	 2, 1, 0 },
		{ 0, 0, 0, 0 }, /* Funcall  */
		{ 0, 0, 0, 0 }, /* Name     */
		{ 0, 0, 0, 0 }, /* Constant */
		{ 0, 0, 0, 0 }, /* Var      */
		{ FORMULA_PTG_U_MINUS,	 7, 0, 0 }, /* Unary - */
		{ FORMULA_PTG_U_PLUS,	 7, 0, 0 }, /* Unary + */
		{ FORMULA_PTG_PERCENT,	 6, 0, 0 }, /* Percentage (NOT MODULO) */
		{ 0, 0, 0, 0 },	/* Array Corner   */
		{ 0, 0, 0, 0 },	/* Array Element  */
		{ 0, 0, 0, 0 }, /* Set      */
		{ FORMULA_PTG_RANGE,	 9, 1, 0 },
		{ FORMULA_PTG_INTERSECT, 8, 1, 0 }
	};
	const GnmExprOp op = GNM_EXPR_GET_OPER (expr);

	switch (op) {
	case GNM_EXPR_OP_ANY_BINARY :
		if (target_type != XL_ARRAY)
			target_type = XL_VAL;

	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT: {
		int const prec = operations[op].prec;

		write_node (pd, expr->binary.value_a,
			    prec - operations[op].assoc_left,
			    target_type);
		write_node (pd, expr->binary.value_b,
			    prec - operations[op].assoc_right,
			    target_type);
		push_guint8 (pd, operations[op].xl_op);
		if (prec <= paren_level)
			push_guint8 (pd, FORMULA_PTG_PAREN);
		break;
	}

	case GNM_EXPR_OP_FUNCALL :
		write_funcall (pd, expr, target_type);
		break;

        case GNM_EXPR_OP_CONSTANT : {
		GnmValue const *v = expr->constant.value;
		switch (v->type) {

		case VALUE_FLOAT: {
			guint8 data[10];
			gnm_float f = value_get_as_float (v);
			int i;

			if (f >= 0 && f <= 0xffff && f == (i = (int)f)) {
				GSF_LE_SET_GUINT8  (data, FORMULA_PTG_INT);
				GSF_LE_SET_GUINT16 (data + 1, i);
				ms_biff_put_var_write (pd->ewb->bp, data, 3);
			} else {
				GSF_LE_SET_GUINT8 (data, FORMULA_PTG_NUM);
				gsf_le_set_double (data + 1, f);
				ms_biff_put_var_write (pd->ewb->bp, data, 9);
			}
			break;
		}
		case VALUE_BOOLEAN : {
			guint8 data[2];
			GSF_LE_SET_GUINT8 (data, FORMULA_PTG_BOOL);
			GSF_LE_SET_GUINT8 (data+1, v->v_bool.val ? 1 : 0);
			ms_biff_put_var_write (pd->ewb->bp, data, 2);
			break;
		}

		case VALUE_ERROR : {
			guint8 data[2];
			GSF_LE_SET_GUINT8 (data, FORMULA_PTG_ERR);
			GSF_LE_SET_GUINT8 (data+1, excel_write_map_errcode (v));
			ms_biff_put_var_write (pd->ewb->bp, data, 2);
			break;
		}

		case VALUE_EMPTY : {
			guint8 data = FORMULA_PTG_MISSARG;
			ms_biff_put_var_write (pd->ewb->bp, &data, 1);
			break;
		}

		case VALUE_STRING:
			write_string (pd, v->v_str.val->str);
			break;

		case VALUE_CELLRANGE:
			excel_formula_write_AREA (pd,
				&v->v_range.cell.a, &v->v_range.cell.b,
				target_type);
			break;

		case VALUE_ARRAY : {
			guint8 data[8];

			if (v->v_array.x > 256 || v->v_array.y > 65536)
				g_warning ("Array far too big");

			/* Class is A or V
			 * name == A
			 * target_type == 'V' ? 'V' : 'A'
			 **/
			GSF_LE_SET_GUINT8  (data + 0, FORMULA_PTG_ARRAY +
				xl_get_op_class (pd, XL_ARRAY, target_type));
			GSF_LE_SET_GUINT8  (data + 1, v->v_array.x - 1);
			GSF_LE_SET_GUINT16 (data + 2, v->v_array.y - 1);
			GSF_LE_SET_GUINT16 (data + 4, 0x0); /* ? */
			GSF_LE_SET_GUINT16 (data + 6, 0x0); /* ? */
			ms_biff_put_var_write (pd->ewb->bp, data, 8);

			pd->arrays = g_slist_prepend (pd->arrays, (gpointer)v);
			break;
		}

		default : {
			gchar *err = g_strdup_printf ("Unknown value %d\n", v->type);
			write_string (pd, err);
			g_free (err);
			g_warning ("Unhandled value type %d", v->type);
			break;
		}
		}
		break;
	}
	case GNM_EXPR_OP_ANY_UNARY : {
		int const prec = operations[op].prec;

		write_node (pd, expr->unary.value, operations[op].prec,
			(target_type != XL_ARRAY) ? XL_VAL : XL_ARRAY);
		push_guint8 (pd, operations[op].xl_op);
		if (prec <= paren_level)
			push_guint8 (pd, FORMULA_PTG_PAREN);
		break;
	}

	case GNM_EXPR_OP_CELLREF :
		excel_formula_write_CELLREF (pd,
			&expr->cellref.ref, NULL, target_type);
		break;

	case GNM_EXPR_OP_NAME :
		if (pd->ewb->bp->version >= MS_BIFF_V8)
			excel_formula_write_NAME_v8 (pd, expr, target_type);
		else
			excel_formula_write_NAME_v7 (pd, expr, target_type);
		break;

	case GNM_EXPR_OP_ARRAY_CORNER :
	case GNM_EXPR_OP_ARRAY_ELEM : {
		guint8 data[5];
		int x, y, ptg;

		if (GNM_EXPR_OP_ARRAY_ELEM == op) {
			x = expr->array_elem.x;
			y = expr->array_elem.y;
		} else
			x = y = 0;

		ptg = FORMULA_PTG_EXPR;
		if (pd->sheet != NULL) {
			GnmExprArrayCorner const *corner = gnm_cell_is_array_corner (
				sheet_cell_get (pd->sheet, pd->col - x, pd->row - y));
			if (NULL != corner &&
			    gnm_expr_is_data_table (corner->expr, NULL, NULL))
				ptg = FORMULA_PTG_TBL;
		}

		GSF_LE_SET_GUINT8 (data, ptg);
		GSF_LE_SET_GUINT16 (data+1, pd->row - y);
		GSF_LE_SET_GUINT16 (data+3, pd->col - x);
		ms_biff_put_var_write (pd->ewb->bp, data, 5);

		/* Be anal */
		g_return_if_fail (paren_level == 0);
		break;
	}

	default : {
		gchar *err = g_strdup_printf ("Unknown Operator %d",
					      GNM_EXPR_GET_OPER (expr));
		write_string (pd, err);
		g_free (err);
		g_warning ("Unhandled expr type %d", GNM_EXPR_GET_OPER (expr));
		break;
	}
	}
}

/* Writes the array and moves to next one */
/* See S59E2B.HTM for some really duff docs */
static void
write_arrays (PolishData *pd)
{
	GnmValue const *array;
	GSList  *ptr;
	int	 x, y;
	guint8   data[8];
	WriteStringFlags string_flags;

	string_flags = (pd->ewb->bp->version >= MS_BIFF_V8)
		? STR_TWO_BYTE_LENGTH : STR_ONE_BYTE_LENGTH;
	pd->arrays = g_slist_reverse (pd->arrays);
	for (ptr = pd->arrays ; ptr != NULL ; ptr = ptr->next) {
		array = ptr->data;
		if (pd->ewb->bp->version >= MS_BIFF_V8) {
			push_guint8  (pd, array->v_array.x - 1);
			push_guint16 (pd, array->v_array.y - 1);
		} else {
			push_guint8  (pd, (array->v_array.x == 256)
				      ? 0 : array->v_array.x);
			push_guint16 (pd, array->v_array.y);
		}

		for (y = 0; y < array->v_array.y; y++) {
			for (x = 0; x < array->v_array.x; x++) {
				GnmValue const *v = array->v_array.vals[x][y];

				if (VALUE_IS_BOOLEAN (v)) {
					push_guint8 (pd, 4);
					push_guint32 (pd, v->v_bool.val ? 1 : 0);
					push_guint32 (pd, 0);
				} else if (VALUE_IS_ERROR (v)) {
					push_guint8 (pd, 16);
					push_guint32 (pd, excel_write_map_errcode (v));
					push_guint32 (pd, 0);
				} else if (VALUE_IS_FLOAT (v)) {
					push_guint8 (pd, 1);
					gsf_le_set_double (data, value_get_as_float (v));
					ms_biff_put_var_write (pd->ewb->bp, data, 8);
				} else { /* Can only be a string */
					push_guint8 (pd, 2);
					excel_write_string (pd->ewb->bp, string_flags,
						value_peek_string (v));
				}
			}
		}
	}

	g_slist_free (pd->arrays);
	pd->arrays = NULL;
}

guint32
excel_write_array_formula (ExcelWriteState *ewb,
			   GnmExprArrayCorner const *array,
			   Sheet *sheet, int fn_col, int fn_row)
{
	PolishData pd;
	unsigned start;
	guint32 len;

	g_return_val_if_fail (ewb, 0);
	g_return_val_if_fail (array, 0);

	pd.col     = fn_col;
	pd.row     = fn_row;
	pd.sheet   = sheet;
	pd.ewb     = ewb;
	pd.arrays  = NULL;
	pd.context = CTXT_ARRAY;
	pd.use_name_variant = FALSE;

	start = ewb->bp->length;
	write_node (&pd, array->expr, 0, XL_ROOT);
	len = ewb->bp->length - start;

	write_arrays (&pd);

	return len;
}

/*
 * This is called for all expressions, including array expressions.
 * (But write_node will not write the inner expression for arrays.)
 */
guint32
excel_write_formula (ExcelWriteState *ewb, GnmExprTop const *texpr,
		     Sheet *sheet, int fn_col, int fn_row,
		     ExcelFuncContext context)
{
	PolishData pd;
	unsigned start;
	guint32 len;

	g_return_val_if_fail (ewb, 0);
	g_return_val_if_fail (texpr, 0);

	pd.col     = fn_col;
	pd.row     = fn_row;
	pd.sheet   = sheet;
	pd.ewb     = ewb;
	pd.arrays  = NULL;
	switch (context) {
	case EXCEL_CALLED_FROM_CELL:
		pd.context = CTXT_CELL;
		pd.use_name_variant = FALSE;
		break;
	case EXCEL_CALLED_FROM_SHARED:
		pd.context = CTXT_CELL;
		pd.use_name_variant = TRUE;
		break;
	case EXCEL_CALLED_FROM_NAME:
		pd.context = CTXT_NAME;
		pd.use_name_variant = TRUE;
		break;
	case EXCEL_CALLED_FROM_CONDITION:
	case EXCEL_CALLED_FROM_VALIDATION:
	default:
		pd.context = CTXT_ARRAY; /* What???  */
		pd.use_name_variant = TRUE;
	}

	start = ewb->bp->length;
	write_node (&pd, texpr->expr, 0, XL_ROOT);
	len = ewb->bp->length - start;

	write_arrays (&pd);

	return len;
}
