/*
 * ms-formula-write.c: MS Excel <- Gnumeric formula conversion
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
#include "func.h"

#include "utils.h"

#include "excel.h"
#include "ms-biff.h"
#include "ms-formula-write.h"
#include "formula-types.h"

#define FORMULA_DEBUG 0

/* FIXME: Leaks like a leaky bucket */

typedef struct {
	const FormulaFuncData *fd;
	guint16 idx;
} FormulaCacheEntry;

/* Lookup Name -> integer */
static GHashTable *formula_cache = NULL;

static FormulaCacheEntry *
get_formula_index (const gchar *name)
{
	int i;
	FormulaCacheEntry *fce;

	g_return_val_if_fail (name, NULL);

	if (!formula_cache)
		formula_cache = g_hash_table_new (g_str_hash, g_str_equal);

	if ((fce = g_hash_table_lookup (formula_cache, name))) {
#if FORMULA_DEBUG > 0
		printf ("Found '%s' in fn cache\n", name);
#endif
		return fce;
	} else {
		for (i=0;i<FORMULA_FUNC_DATA_LEN;i++) {
			if (!g_strcasecmp (formula_func_data[i].prefix,
					   name)) {
				FormulaCacheEntry *fce = g_new (FormulaCacheEntry, 1);
				fce->fd  = &formula_func_data[i];
				fce->idx = i;
				g_hash_table_insert (formula_cache, fce->fd->prefix, fce);
#if FORMULA_DEBUG > 0
				printf ("Caching and returning '%s' as %d\n", name, i);
#endif
				return fce;
			}
		}
		g_warning ("Serious error, unknown function");
		return NULL;
	}
}

/* Parse it into memory, unlikely to be too big */
typedef struct {
	BIFF_PUT     *bp;
	GList        *arrays; /* A list of Value *'s ? */
	ExcelSheet   *sheet;
	int           col;
	int           row;
	eBiff_version ver;
} PolishData;

static void
push_guint8 (PolishData *pd, guint8 b)
{
	ms_biff_put_var_write (pd->bp, &b, sizeof(guint8));
}

static void
push_guint16 (PolishData *pd, guint16 b)
{
	guint8 data[2];
	BIFF_SET_GUINT16 (data, b);
	ms_biff_put_var_write (pd->bp, data, sizeof(data));
}

static void
push_guint32 (PolishData *pd, guint32 b)
{
	guint8 data[4];
	BIFF_SET_GUINT32 (data, b);
	ms_biff_put_var_write (pd->bp, data, sizeof(data));
}

static void
write_cellref_v7 (PolishData *pd, const CellRef *ref, guint8 *out_col, guint16 *out_row)
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
		row|=0x8000;
	if (ref->row_relative)
		row|=0x4000;

	BIFF_SET_GUINT16 (out_row, row);
	BIFF_SET_GUINT8  (out_col, col);
}

static void
write_string (PolishData *pd, gchar *txt)
{
	push_guint8 (pd, FORMULA_PTG_STR);
	biff_put_text (pd->bp, txt, eBiffV8, TRUE, SIXTEEN_BIT);
}

/**
 * Recursion is just so fun.
 **/
static void
write_node (PolishData *pd, ExprTree *tree)
{
	g_return_if_fail (pd);
	g_return_if_fail (tree);

	switch (tree->oper) {
	case OPER_EQUAL:
		write_node  (pd, tree->u.binary.value_a);
		write_node  (pd, tree->u.binary.value_b);
		push_guint8 (pd, FORMULA_PTG_EQUAL);
		break;
	case OPER_GT:
		write_node  (pd, tree->u.binary.value_a);
		write_node  (pd, tree->u.binary.value_b);
		push_guint8 (pd, FORMULA_PTG_GT);
		break;
	case OPER_LT:
		write_node  (pd, tree->u.binary.value_a);
		write_node  (pd, tree->u.binary.value_b);
		push_guint8 (pd, FORMULA_PTG_LT);
		break;
	case OPER_GTE:
		write_node  (pd, tree->u.binary.value_a);
		write_node  (pd, tree->u.binary.value_b);
		push_guint8 (pd, FORMULA_PTG_GTE);
		break;
	case OPER_LTE:
		write_node  (pd, tree->u.binary.value_a);
		write_node  (pd, tree->u.binary.value_b);
		push_guint8 (pd, FORMULA_PTG_LTE);
		break;
	case OPER_NOT_EQUAL:
		write_node  (pd, tree->u.binary.value_a);
		write_node  (pd, tree->u.binary.value_b);
		push_guint8 (pd, FORMULA_PTG_NOT_EQUAL);
		break;
	case OPER_ADD:
		write_node  (pd, tree->u.binary.value_a);
		write_node  (pd, tree->u.binary.value_b);
		push_guint8 (pd, FORMULA_PTG_ADD);
		break;
	case OPER_SUB:
		write_node  (pd, tree->u.binary.value_a);
		write_node  (pd, tree->u.binary.value_b);
		push_guint8 (pd, FORMULA_PTG_SUB);
		break;
	case OPER_MULT:
		write_node  (pd, tree->u.binary.value_a);
		write_node  (pd, tree->u.binary.value_b);
		push_guint8 (pd, FORMULA_PTG_MULT);
		break;
	case OPER_DIV:
		write_node  (pd, tree->u.binary.value_a);
		write_node  (pd, tree->u.binary.value_b);
		push_guint8 (pd, FORMULA_PTG_DIV);
		break;
	case OPER_EXP:
		write_node  (pd, tree->u.binary.value_a);
		write_node  (pd, tree->u.binary.value_b);
		push_guint8 (pd, FORMULA_PTG_EXP);
		break;
	case OPER_CONCAT:
		write_node  (pd, tree->u.binary.value_a);
		write_node  (pd, tree->u.binary.value_b);
		push_guint8 (pd, FORMULA_PTG_CONCAT);
		break;
	case OPER_FUNCALL:
	{
		FormulaCacheEntry *fce;
		gchar *name = tree->u.function.symbol->str;
		
		if ((fce = get_formula_index (tree->u.function.symbol->str))) {
			GList   *args     = tree->u.function.arg_list;
			gint     num_args = 0;
			gboolean prompt   = 0;
			gboolean cmdequiv = 0;

			while (args) {
				write_node (pd, args->data);
				args = g_list_next (args);
				num_args++;
			}

#if FORMULA_DEBUG > 1
			printf ("Writing function '%s' as idx %d, args %d\n",
				name, fce->idx, fce->fd->num_args);
#endif

			g_assert (num_args < 128);
			if (fce->fd->num_args < 0) {
				push_guint8  (pd, FORMULA_PTG_FUNC_VAR);
				push_guint8  (pd, num_args | (prompt&0x80));
				push_guint16 (pd, fce->idx | (cmdequiv&0x8000));
			} else {
				push_guint8  (pd, FORMULA_PTG_FUNC);
				push_guint16 (pd, fce->idx);
			}
		} else {
			gchar *err = g_strdup_printf ("Untranslatable '%s'", name);
#if FORMULA_DEBUG > 0
			printf ("Untranslatable function '%s'\n", name);
#endif
			write_string (pd, err);
			g_free (err);
		}
		break;
	}
        case OPER_CONSTANT:
	{
		Value *v = tree->u.constant;
		switch (v->type) {
		case VALUE_INTEGER:
		case VALUE_FLOAT:
		{
			guint8 data[10];
			BIFF_SET_GUINT8 (data, FORMULA_PTG_NUM);
			BIFF_SETDOUBLE (data+1, value_get_as_float (v));
			ms_biff_put_var_write (pd->bp, data, 9);
			break;
		}
		case VALUE_STRING:
			write_string (pd, v->v.str->str);
			break;
		case VALUE_CELLRANGE:
		{ /* FIXME: Could be 3D ! */
			guint8 data[6];
			push_guint8 (pd, FORMULA_PTG_AREA);
			write_cellref_v7 (pd, &v->v.cell_range.cell_a, &data[4], (guint16 *)&data[0]);
			write_cellref_v7 (pd, &v->v.cell_range.cell_b, &data[5], (guint16 *)&data[2]);
			ms_biff_put_var_write (pd->bp, data, 6);
			break;
		}
		default:
		{
			gchar *err = g_strdup_printf ("Uknown type %d\n", v->type);
			write_string (pd, err);
			g_free (err);
#if FORMULA_DEBUG > 0
			printf ("Unhandled type %d\n", v->type);
#endif
			break;
		}
		}
		break;
	}
	case OPER_NEG:
		write_node  (pd, tree->u.value);
		push_guint8 (pd, FORMULA_PTG_U_MINUS);
		break;
	case OPER_VAR:
	{
		guint8 data[3];
		push_guint8 (pd, FORMULA_PTG_REF);
		write_cellref_v7 (pd, &tree->u.ref, &data[2], (guint16 *)&data[0]);
		ms_biff_put_var_write (pd->bp, data, 3);
		break;
	}
	case OPER_ARRAY:
	default:
	{
		gchar *err = g_strdup_printf ("Unknown Operator %d", tree->oper);
		write_string (pd, err);
		g_free (err);
#if FORMULA_DEBUG > 0
		printf ("Unhandled node type %d\n", tree->oper);
#endif
		break;
	}
	}
}

void
ms_excel_write_formula (BIFF_PUT *bp, ExcelSheet *sheet, ExprTree *expr,
			int fn_col, int fn_row)
{
	PolishData *pd;
	
	g_return_if_fail (bp);
	g_return_if_fail (expr);
	g_return_if_fail (sheet);

	pd = g_new (PolishData, 1);
	pd->col    = fn_col;
	pd->row    = fn_row;
	pd->sheet  = sheet;
	pd->bp     = bp;
	pd->arrays = NULL;
	pd->ver    = sheet->wb->ver;

	write_node (pd, expr);
}
