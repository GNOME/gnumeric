/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * ms-formula-write.c: MS Excel <- Gnumeric formula conversion
 * See: S59E2B.HTM
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1998-2001 Michael Meeks
 *          2002 Jody Goldberg
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include <string.h>
#include "ms-formula-write.h"
#include "excel.h"
#include "ms-biff.h"
#include "formula-types.h"
#include "boot.h"
#include <gutils.h>
#include <workbook-priv.h>
#include <func.h>
#include <value.h>
#include <expr.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <str.h>
#include <parse-util.h>
#include <io-context.h>

#include <gsf/gsf-utils.h>

#define FORMULA_DEBUG 0

typedef struct {
	FormulaFuncData const *fd;
	int idx;
} ExcelFunc;

static void
excel_write_prep_expr (ExcelWriteState *ewb, GnmExpr const *expr)
{
	switch (expr->any.oper) {

	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY:
		excel_write_prep_expr (ewb, expr->binary.value_a);
		excel_write_prep_expr (ewb, expr->binary.value_b);
		break;

	case GNM_EXPR_OP_ANY_UNARY:
		excel_write_prep_expr (ewb, expr->unary.value);
		break;

	case GNM_EXPR_OP_FUNCALL: {
		GnmFunc *func = expr->func.func;
		GnmExprList *l;
		ExcelFunc *ef = g_hash_table_lookup (ewb->function_map, func);
		int i;

		for (l = expr->func.arg_list; l; l = l->next)
			excel_write_prep_expr (ewb, l->data);

		if (ef != NULL)
			return;

		ef = g_new (ExcelFunc, 1);
		for (i = 0; i < FORMULA_FUNC_DATA_LEN; i++)
			if (!g_ascii_strcasecmp (formula_func_data[i].name, func->name)) {
				ef->fd = formula_func_data + i;
				ef->idx = i;
				break;
			}

		if (i >= FORMULA_FUNC_DATA_LEN) {
			g_ptr_array_add (ewb->externnames, func);
			ef->fd = NULL;
			ef->idx = ewb->externnames->len;
		}
		g_hash_table_insert (ewb->function_map, func, ef);
		break;
	}

	case GNM_EXPR_OP_SET: {
		GnmExprList *l;

		for (l = expr->set.set; l; l = l->next)
			excel_write_prep_expr (ewb, l->data);
		break;
	}

	case GNM_EXPR_OP_CELLREF:
	case GNM_EXPR_OP_CONSTANT:
		break;

	default:
		break;
	}
}

/**
 * excel_write_prep_expressions :
 * @ewb: state
 *
 *  Searches for interesting formula / names
 * and builds a database of things to write out later.
 **/
void
excel_write_prep_expressions (ExcelWriteState *ewb)
{
	g_return_if_fail (ewb != NULL);

	WORKBOOK_FOREACH_DEPENDENT (ewb->gnum_wb, dep, 
		excel_write_prep_expr (ewb, dep->expression););

#warning Walk all the names too
}

/****************************************************************************/

#define OP_REF(o)   (o + FORMULA_CLASS_REF)
#define OP_VALUE(o) (o + FORMULA_CLASS_VALUE)
#define OP_ARRAY(o) (o + FORMULA_CLASS_ARRAY)

/* Parse it into memory, unlikely to be too big */
typedef struct {
	ExcelWriteState *ewb;
	Sheet	 	*sheet;
	int	  	 col, row;
	GList		*arrays; /* A list of Value *'s ? */
} PolishData;

static void write_node (PolishData *pd, GnmExpr const *expr, int paren_level);

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
push_guint32 (PolishData *pd, guint32 b)
{
	guint8 data[4];
	GSF_LE_SET_GUINT32 (data, b);
	ms_biff_put_var_write (pd->ewb->bp, data, sizeof(data));
}

static void
write_cellref_v7 (PolishData *pd, CellRef const *ref,
		  guint8 *out_col, guint16 *out_row)
{
	guint    row, col;

	if (ref->col_relative)
		col = ref->col + pd->col;
	else
		col = ref->col;
	if (ref->row_relative)
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
write_cellref_v8 (PolishData *pd, CellRef const *ref,
		  guint16 *out_col, guint16 *out_row)
{
	guint    row, col;

	if (ref->col_relative)
		col = ref->col + pd->col;
	else
		col = ref->col;
	if (ref->row_relative)
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
	excel_write_string (pd->ewb->bp, txt, STR_ONE_BYTE_LENGTH);
}

static void
write_area (PolishData *pd, CellRef const *a, CellRef const *b)
{
	guint8 data[24];

	if (a->sheet == NULL && b->sheet == NULL) {
		push_guint8 (pd, OP_REF (FORMULA_PTG_AREA));
		if (pd->ewb->bp->version <= MS_BIFF_V7) {
			write_cellref_v7 (pd, a,
					  data + 4, (guint16 *)(data + 0));
			write_cellref_v7 (pd, b,
					  data + 5, (guint16 *)(data + 2));
			ms_biff_put_var_write (pd->ewb->bp, data, 6);
		} else {
			write_cellref_v8 (pd, a,
					  (guint16 *)(data + 4), (guint16 *)(data + 0));
			write_cellref_v8 (pd, b,
					  (guint16 *)(data + 6), (guint16 *)(data + 2));
			ms_biff_put_var_write (pd->ewb->bp, data, 8);
		}
	} else {
		g_return_if_fail (a->sheet != NULL);

		push_guint8 (pd, OP_REF (FORMULA_PTG_AREA_3D));
		if (pd->ewb->bp->version <= MS_BIFF_V7) {
			guint16 idx_a, idx_b;
			gint16 ixals;

			/* FIXME no external references for now */
			g_return_if_fail (pd->ewb->gnum_wb == a->sheet->workbook);

			idx_a = a->sheet->index_in_wb + 1;
			idx_b = (b->sheet != NULL)
				? a->sheet->index_in_wb + 1 : idx_a;

			ixals = -(idx_a-1);

			GSF_LE_SET_GUINT16 (data, ixals);
			GSF_LE_SET_GUINT32 (data +  2, 0x0);
			GSF_LE_SET_GUINT32 (data +  6, 0x0);
			GSF_LE_SET_GUINT16 (data + 10, idx_a);
			GSF_LE_SET_GUINT16 (data + 12, idx_b);
			write_cellref_v7 (pd, a,
					  data + 18, (guint16 *)(data + 14));
			write_cellref_v7 (pd, b,
					  data + 19, (guint16 *)(data + 16));
			ms_biff_put_var_write (pd->ewb->bp, data, 20);
		} else {
			guint16 extn_idx = excel_write_get_externsheet_idx (pd->ewb,
									    a->sheet,
									    b->sheet);
			GSF_LE_SET_GUINT16 (data, extn_idx);
			write_cellref_v8 (pd, a,
					  (guint16 *)(data + 6), (guint16 *)(data + 2));
			write_cellref_v8 (pd, b,
					  (guint16 *)(data + 8), (guint16 *)(data + 4));
			ms_biff_put_var_write (pd->ewb->bp, data, 10);
		}
	}
}

static void
write_ref (PolishData *pd, CellRef const *ref)
{
	guint8 data[24];

	g_return_if_fail (pd);
	g_return_if_fail (ref);

	if (ref->sheet == NULL) {
		push_guint8 (pd, OP_VALUE (FORMULA_PTG_REF));
		if (pd->ewb->bp->version <= MS_BIFF_V7) {
			write_cellref_v7 (pd, ref, data + 2, (guint16 *)data);
			ms_biff_put_var_write (pd->ewb->bp, data, 3);
		} else { /* Duff docs */
			write_cellref_v8 (pd, ref, (guint16 *)(data + 2), (guint16 *)data);
			ms_biff_put_var_write (pd->ewb->bp, data, 4);
		}
	} else {
		push_guint8 (pd, OP_VALUE (FORMULA_PTG_REF_3D));
		if (pd->ewb->bp->version <= MS_BIFF_V7) {
			guint16 idx_a;
			gint16 ixals;

			/* FIXME no external references for now */
			g_return_if_fail (pd->ewb->gnum_wb == ref->sheet->workbook);

			idx_a = ref->sheet->index_in_wb + 1;
			ixals = -(idx_a-1);
			GSF_LE_SET_GUINT16 (data, ixals);
			GSF_LE_SET_GUINT32 (data +  2, 0x0);
			GSF_LE_SET_GUINT32 (data +  6, 0x0);
			GSF_LE_SET_GUINT16 (data + 10, idx_a);
			GSF_LE_SET_GUINT16 (data + 12, idx_a);
			write_cellref_v7 (pd, ref, data + 16,
					  (guint16 *)(data + 14));
			ms_biff_put_var_write (pd->ewb->bp, data, 17);
		} else {
			guint16 extn_idx = excel_write_get_externsheet_idx (pd->ewb,
									    ref->sheet,
									    NULL);
			GSF_LE_SET_GUINT16 (data, extn_idx);
			write_cellref_v8 (pd, ref, (guint16 *)(data + 2),
					  (guint16 *)(data + 1));
			ms_biff_put_var_write (pd->ewb->bp, data, 6);
		}
	}
}

static void
write_funcall (PolishData *pd, GnmExpr const *expr)
{
	GnmExprList *args;
	gint     num_args = 0;
	gboolean prompt   = 0;
	gboolean cmdequiv = 0;
	GnmFunc *func = expr->func.func;
	ExcelFunc *ef = g_hash_table_lookup (pd->ewb->function_map, func);

	g_return_if_fail (ef != NULL);

	if (ef->fd == NULL) {
		push_guint8  (pd, FORMULA_PTG_NAME_X);
		if (pd->ewb->bp->version <= MS_BIFF_V7) {
			/* I write the Addin Magic entry after all the other sheets
			 * in the workbook,  and this is a 1 based ordinal.
			 */
			push_guint16 (pd, pd->ewb->sheets->len + 1);
			push_guint32 (pd, 0); /* reserved */
			push_guint32 (pd, 0); /* reserved */
			push_guint16 (pd, ef->idx);
			push_guint32 (pd, 0); /* reserved */
			push_guint32 (pd, 0); /* reserved */
			push_guint32 (pd, 0); /* reserved */
		} else {
			/* I write the Addin Magic entry 1st */
			push_guint16 (pd, 0);
			push_guint16 (pd, ef->idx);
			push_guint16 (pd, 0); /* reserved */
		}
	}

	for (args = expr->func.arg_list ; args != NULL; ) {
		write_node (pd, args->data, 0);
		num_args++;
		args = args->next;
		if (ef->fd != NULL && args != NULL && num_args == ef->fd->num_args) {
			gnm_io_warning (pd->ewb->io_context, 
				_("Too many arguments for function, MS Excel expects exactly %d and we have more"),
				ef->fd->num_args);
		}
	}

	if (num_args >= 128) {
		g_warning ("Too many args for XL, it can only handle 128");
		num_args = 128;
	}

	if (ef->fd != NULL) {
#if FORMULA_DEBUG > 1
		printf ("Writing function '%s' as idx %d, args %d\n",
			name, ef->u.std.idx, fce->u.std.fd->num_args);
#endif

		if (ef->fd->num_args < 0) {
			push_guint8  (pd, FORMULA_PTG_FUNC_VAR);
			push_guint8  (pd, num_args | (prompt&0x80));
			push_guint16 (pd, ef->idx | (cmdequiv&0x8000));
		} else {
			/* If XL requires more arguments than we do
			 * pad the remainder with missing args
			 */
			while (num_args++ < ef->fd->num_args)
				push_guint8 (pd, FORMULA_PTG_MISSARG);

			push_guint8  (pd, FORMULA_PTG_FUNC);
			push_guint16 (pd, ef->idx);
		}
	} else { /* Undocumented :-) */
		push_guint8  (pd, FORMULA_PTG_FUNC_VAR + 0x20);
		push_guint8  (pd, (num_args + 1) | (prompt&0x80));
		push_guint16 (pd, 0xff | (cmdequiv&0x8000));
	}
}

static void
write_node (PolishData *pd, GnmExpr const *expr, int paren_level)
{
	static struct {
		guint8 xl_op;
		int prec;	              /* Precedences -- should match parser.y  */
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
		{ 0, 0, 0, 0 },	/* Array    */
		{ 0, 0, 0, 0 }, /* Set      */
		{ FORMULA_PTG_RANGE,	 9, 1, 0 },
		{ FORMULA_PTG_INTERSECT, 8, 1, 0 }
	};
	int op;
	g_return_if_fail (pd);
	g_return_if_fail (expr);

	op = expr->any.oper;
	switch (op) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY : {
		int const prec = operations[op].prec;

		write_node  (pd, expr->binary.value_a,
			     prec - operations[op].assoc_left);
		write_node  (pd, expr->binary.value_b,
			     prec - operations[op].assoc_right);
		push_guint8 (pd, operations[op].xl_op);
		if (prec <= paren_level)
			push_guint8 (pd, FORMULA_PTG_PAREN);
		break;
	}

	case GNM_EXPR_OP_FUNCALL :
		write_funcall (pd, expr);
		break;

        case GNM_EXPR_OP_CONSTANT : {
		Value const *v = expr->constant.value;
		switch (v->type) {

		case VALUE_INTEGER : {
			guint8 data[10];
			int i = value_get_as_int (v);
			if (i >= 0 && i < 1<<16) {
				GSF_LE_SET_GUINT8  (data, FORMULA_PTG_INT);
				GSF_LE_SET_GUINT16 (data + 1, i);
				ms_biff_put_var_write (pd->ewb->bp, data, 3);
			} else {
				GSF_LE_SET_GUINT8 (data, FORMULA_PTG_NUM);
				gsf_le_set_double (data + 1, value_get_as_float (v));
				ms_biff_put_var_write (pd->ewb->bp, data, 9);
			}
			break;
		}
		case VALUE_FLOAT : {
			guint8 data[10];
			GSF_LE_SET_GUINT8 (data, FORMULA_PTG_NUM);
			gsf_le_set_double (data+1, value_get_as_float (v));
			ms_biff_put_var_write (pd->ewb->bp, data, 9);
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

		case VALUE_CELLRANGE: {
			write_area (pd, &v->v_range.cell.a,
				    &v->v_range.cell.b);
			break;
		}

                /* See S59E2B.HTM for some really duff docs */
		case VALUE_ARRAY : { /* Guestimation */
			guint8 data[8];

			if (v->v_array.x > 256 || v->v_array.y > 65536)
				g_warning ("Array far too big");

			GSF_LE_SET_GUINT8  (data + 0, FORMULA_PTG_ARRAY);
			GSF_LE_SET_GUINT8  (data + 1, v->v_array.x - 1);
			GSF_LE_SET_GUINT16 (data + 2, v->v_array.y - 1);
			GSF_LE_SET_GUINT16 (data + 4, 0x0); /* ? */
			GSF_LE_SET_GUINT16 (data + 6, 0x0); /* ? */
			ms_biff_put_var_write (pd->ewb->bp, data, 8);

			pd->arrays = g_list_append (pd->arrays, (gpointer)v);
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

		write_node (pd, expr->unary.value, operations[op].prec);
		push_guint8 (pd, operations[op].xl_op);
		if (prec <= paren_level)
			push_guint8 (pd, FORMULA_PTG_PAREN);
		break;
	}

	case GNM_EXPR_OP_CELLREF :
		write_ref (pd, &expr->cellref.ref);
		break;

	case GNM_EXPR_OP_NAME : {
		GnmNamedExpr const *n = expr->name.name;
		guint8 data[14];
		unsigned i;

		memset (data, 0, sizeof (data));

		for (i = 0; i < pd->ewb->names->len; i++)
			if (n == g_ptr_array_index (pd->ewb->names, i))
				break;

		if (i < pd->ewb->names->len) {
			GSF_LE_SET_GUINT8  (data + 0, FORMULA_PTG_NAME);
			GSF_LE_SET_GUINT16 (data + 1, i + 1);
			ms_biff_put_var_write (pd->ewb->bp, data, 15);
		} else {
			g_warning ("This entire mechanism is bogus.  It does not handle sheet local names");
		}
		break;
	}

	case GNM_EXPR_OP_ARRAY : {
		GnmExprArray const *array = &expr->array;
		guint8 data[5];
		GSF_LE_SET_GUINT8 (data, FORMULA_PTG_EXPR);
		GSF_LE_SET_GUINT16 (data+1, pd->row - array->y);
		GSF_LE_SET_GUINT16 (data+3, pd->col - array->x);
		ms_biff_put_var_write (pd->ewb->bp, data, 5);

		/* Be anal */
		g_return_if_fail (paren_level == 0);
		break;
	}

	default : {
		gchar *err = g_strdup_printf ("Unknown Operator %d",
					      expr->any.oper);
		write_string (pd, err);
		g_free (err);
		g_warning ("Unhandled expr type %d", expr->any.oper);
		break;
	}
	}
}

/* Writes the array and moves to next one */
/* See S59E2B.HTM for some really duff docs */
static void
write_arrays (PolishData *pd)
{
	Value   *array;
	guint16  lpx, lpy;
	guint8   data[8];

	array = pd->arrays->data;
	g_return_if_fail (array->type == VALUE_ARRAY);

	for (lpy = 0; lpy < array->v_array.y; lpy++) {
		for (lpx = 0; lpx < array->v_array.x; lpx++) {
			Value const *v = array->v_array.vals[lpx][lpy];

			if (VALUE_IS_NUMBER (v)) {
				push_guint8 (pd, 1);
				gsf_le_set_double (data, value_get_as_float (v));
				ms_biff_put_var_write (pd->ewb->bp, data, 8);
			} else { /* Can only be a string */
				char const *str = value_peek_string (v);
				push_guint8 (pd, 2);
				excel_write_string (pd->ewb->bp, str,
					STR_ONE_BYTE_LENGTH);
			}
		}
	}

	pd->arrays = g_list_next (pd->arrays);
}

guint32
excel_write_formula (ExcelWriteState *ewb, GnmExpr const *expr,
		     Sheet *sheet,
		     int fn_col, int fn_row, int paren_level)
{
	PolishData pd;
	unsigned start;
	guint32 len;

	g_return_val_if_fail (ewb, 0);
	g_return_val_if_fail (expr, 0);

	pd.col    = fn_col;
	pd.row    = fn_row;
	pd.sheet  = sheet;
	pd.ewb    = ewb;
	pd.arrays = NULL;

	start = ewb->bp->length;
	write_node (&pd, expr, 0);
	len = ewb->bp->length - start;

	if (pd.arrays) {
		push_guint16 (&pd, 0x0); /* Sad but true */
		push_guint8  (&pd, 0x0);

		while (pd.arrays)
			write_arrays (&pd);
	}

	return len;
}
