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
#include "ms-formula-write.h"
#include "excel.h"
#include "ms-biff.h"
#include "formula-types.h"
#include "boot.h"
#include <gutils.h>
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
/*#define DO_IT (ms_excel_formula_debug > 0)*/
#define DO_IT (1)

#define OP_REF(o)   (o + FORMULA_CLASS_REF)
#define OP_VALUE(o) (o + FORMULA_CLASS_VALUE)
#define OP_ARRAY(o) (o + FORMULA_CLASS_ARRAY)

typedef struct _PolishData PolishData;
typedef struct _FormulaCacheEntry FormulaCacheEntry;

static void write_node (PolishData *pd, GnmExpr const *tree, int paren_level);

struct _FormulaCacheEntry {
	enum { CACHE_STD, CACHE_ENAME_V8, CACHE_ENAME_V7 } type;
	union {
		struct {
			const char *name;
		} ename_v8;
		struct {
			const char *name;
			guint16 idx;
		} ename_v7;
		struct {
			const FormulaFuncData *fd;
			guint16 idx;
		} std;
	} u;
};

void
ms_formula_cache_init (ExcelSheet *sheet)
{
	sheet->formula_cache = g_hash_table_new (gnumeric_strcase_hash,
						 gnumeric_strcase_equal);
}

static gboolean
cache_remove (gchar *name, FormulaCacheEntry *fce, void *dummy)
{
	g_free (fce);
	return TRUE;
}

void
ms_formula_cache_shutdown (ExcelSheet *sheet)
{
	g_hash_table_foreach_remove (sheet->formula_cache,
				     (GHRFunc)cache_remove, NULL);
	g_hash_table_destroy (sheet->formula_cache);
}

static FormulaCacheEntry *
formula_cache_new_std (ExcelSheet *sheet, int i)
{
	FormulaCacheEntry *fce = g_new (FormulaCacheEntry, 1);

	fce->type       = CACHE_STD;
	fce->u.std.fd   = &formula_func_data[i];
	fce->u.std.idx  = i;
	g_hash_table_insert (sheet->formula_cache,
			     (char *)(formula_func_data[i].prefix), fce);

	return fce;
}

static FormulaCacheEntry *
formula_cache_new_ename (ExcelSheet *sheet, const char *name)
{
	FormulaCacheEntry *fce = g_new (FormulaCacheEntry, 1);

	if (sheet->wb->ver >= MS_BIFF_V8) {
		fce->type            = CACHE_ENAME_V8;
		fce->u.ename_v8.name = name;
	} else {
		fce->type            = CACHE_ENAME_V7;
		fce->u.ename_v7.idx  = -1;
		fce->u.ename_v7.name = name;
	}

	g_hash_table_insert (sheet->formula_cache, (char *)name, fce);

	return fce;
}

static FormulaCacheEntry *
get_formula_index (ExcelSheet *sheet, const gchar *name)
{
	int i;
	FormulaCacheEntry *fce;

	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (sheet->formula_cache != NULL, NULL);

	if ((fce = g_hash_table_lookup (sheet->formula_cache, name))) {
#if FORMULA_DEBUG > 0
		printf ("Found '%s' in fn cache\n", name);
#endif
		return fce;
	}

	for (i = 0; i < FORMULA_FUNC_DATA_LEN; i++) {
		if (!g_ascii_strcasecmp (formula_func_data[i].prefix, name)) {
			fce = formula_cache_new_std (sheet, i);
#if FORMULA_DEBUG > 0
			printf ("Caching and returning '%s' as %d\n", name, i);
#endif
			return fce;
		}
	}

	return NULL;
}

/**
 * ms_formula_build_pre_data:
 * @sheet: The sheet to do it on
 * @tree: a expression tree in the sheet.
 *
 *  Searches for interesting formula / names
 * and builds a database of things to write out later.
 **/
void
ms_formula_build_pre_data (ExcelSheet *sheet, GnmExpr const *tree)
{
	g_return_if_fail (tree != NULL);
	g_return_if_fail (sheet != NULL);

	switch (tree->any.oper) {

	case GNM_EXPR_OP_ANY_BINARY:
		ms_formula_build_pre_data (sheet, tree->binary.value_a);
		ms_formula_build_pre_data (sheet, tree->binary.value_b);
		break;

	case GNM_EXPR_OP_ANY_UNARY:
		ms_formula_build_pre_data (sheet, tree->unary.value);
		break;

	case GNM_EXPR_OP_FUNCALL:
	{
		GnmExprList *l;
		FormulaCacheEntry *fce;
		const gchar *name = function_def_get_name (tree->func.func);

		for (l = tree->func.arg_list; l; l = l->next)
			ms_formula_build_pre_data (sheet, l->data);

		fce = get_formula_index (sheet, name);

		if (!fce)
			formula_cache_new_ename (sheet, name);

		break;
	}
	default:
		break;
	}
}

static int
queue_compare_fn (const FormulaCacheEntry *fcea,
		  const FormulaCacheEntry *fceb)
{
	return strcmp (fcea->u.ename_v7.name,
		       fceb->u.ename_v7.name);
}

/* See: S59D7E.HTM / ms_excel_externname */
static void
queue_externname (const char *key, FormulaCacheEntry *fce,
		  GList **l)
{
	g_return_if_fail (l != NULL);
	g_return_if_fail (key != NULL);
	g_return_if_fail (fce != NULL);

	if (fce->type == CACHE_ENAME_V7)
		*l = g_list_insert_sorted (*l, fce,
					   (GCompareFunc)queue_compare_fn);
}

void
ms_formula_write_pre_data (BiffPut *bp, ExcelSheet *sheet,
			   formula_write_t which,
			   MsBiffVersion ver)
{
	if (which == EXCEL_EXTERNNAME) {
		if (sheet->wb->ver <= MS_BIFF_V7) {
			GList *l = NULL;
			int idx = 1;

			g_hash_table_foreach (sheet->formula_cache,
					      (GHFunc)queue_externname,
					      &l);

			while (l) {
				FormulaCacheEntry *fce = l->data;
				guint8  data[8];
				gint    len;
				char   *txt, *buf;

				if (DO_IT) {
				ms_biff_put_var_next (bp, BIFF_EXTERNNAME);
				GSF_LE_SET_GUINT32 (data + 0, 0x0);
				GSF_LE_SET_GUINT16 (data + 4, 0x0);
				ms_biff_put_var_write (bp, data, 6);
				txt = g_strdup (fce->u.ename_v7.name);
				g_strup (txt); /* scraping the barrel here */
				len = biff_convert_text(&buf, txt, MS_BIFF_V7);
				biff_put_text (bp, buf, len,
					       MS_BIFF_V7,
					       TRUE, AS_PER_VER);
				g_free (buf);
				g_free (txt);
				GSF_LE_SET_GUINT32 (data, 0x171c0002); /* Magic hey :-) */
				ms_biff_put_var_write (bp, data, 4);
				ms_biff_put_commit (bp);
				}

				fce->u.ename_v7.idx = idx++;
				l = g_list_next (l);
			}
		}
	} else {
/*		g_warning ("Unimplemented pre-write");*/
	}
}

/* Parse it into memory, unlikely to be too big */
struct _PolishData {
	BiffPut     *bp;
	GList        *arrays; /* A list of Value *'s ? */
	ExcelSheet   *sheet;
	int           col;
	int           row;
	MsBiffVersion ver;
};

static void
push_guint8 (PolishData *pd, guint8 b)
{
	ms_biff_put_var_write (pd->bp, &b, sizeof(guint8));
}

static void
push_guint16 (PolishData *pd, guint16 b)
{
	guint8 data[2];
	GSF_LE_SET_GUINT16 (data, b);
	ms_biff_put_var_write (pd->bp, data, sizeof(data));
}

static void
push_guint32 (PolishData *pd, guint32 b)
{
	guint8 data[4];
	GSF_LE_SET_GUINT32 (data, b);
	ms_biff_put_var_write (pd->bp, data, sizeof(data));
}

static void
write_cellref_v7 (PolishData *pd, const CellRef *ref,
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
write_cellref_v8 (PolishData *pd, const CellRef *ref,
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
write_string (PolishData *pd, const gchar *txt)
{
	/* FIXME : Check this logic.  Why would we pass a NULL ? */
	if (txt == NULL)
		push_guint8 (pd, FORMULA_PTG_MISSARG);
	else {
		gint len;
		char *buf;
		push_guint8 (pd, FORMULA_PTG_STR);
		len = biff_convert_text(&buf, txt, pd->ver);
		biff_put_text (pd->bp, buf, len, pd->ver, TRUE, AS_PER_VER);
		g_free(buf);
	}
}

static void
write_area (PolishData *pd, CellRef const *a, CellRef const *b)
{
	guint8 data[24];

	if (a->sheet == NULL && b->sheet == NULL) {
		push_guint8 (pd, OP_REF (FORMULA_PTG_AREA));
		if (pd->ver <= MS_BIFF_V7) {
			write_cellref_v7 (pd, a,
					  data + 4, (guint16 *)(data + 0));
			write_cellref_v7 (pd, b,
					  data + 5, (guint16 *)(data + 2));
			ms_biff_put_var_write (pd->bp, data, 6);
		} else {
			write_cellref_v8 (pd, a,
					  (guint16 *)(data + 4), (guint16 *)(data + 0));
			write_cellref_v8 (pd, b,
					  (guint16 *)(data + 6), (guint16 *)(data + 2));
			ms_biff_put_var_write (pd->bp, data, 8);
		}
	} else {
		guint16 first_idx, second_idx;
		if (a->sheet)
			first_idx  = ms_excel_write_get_sheet_idx (pd->sheet->wb,
								   a->sheet);
		else
			first_idx  = ms_excel_write_get_sheet_idx (pd->sheet->wb,
								   pd->sheet->gnum_sheet);
		if (b->sheet)
			second_idx = ms_excel_write_get_sheet_idx (pd->sheet->wb,
								   b->sheet);
		else
			second_idx = first_idx;

		push_guint8 (pd, OP_REF (FORMULA_PTG_AREA_3D));
		if (pd->ver <= MS_BIFF_V7) {
			/* FIXME for now assume that the external reference is in the same workbook */
			if (pd->sheet->gnum_sheet->workbook != a->sheet->workbook) {
				g_warning ("References to external workbooks are not supported yet");
			}

			GSF_LE_SET_GUINT16 (data, 0xffff);
			GSF_LE_SET_GUINT32 (data +  2, 0x0);
			GSF_LE_SET_GUINT32 (data +  6, 0x0);
			GSF_LE_SET_GUINT16 (data + 10, first_idx);
			GSF_LE_SET_GUINT16 (data + 12, second_idx);
			write_cellref_v7 (pd, a,
					  data + 18, (guint16 *)(data + 14));
			write_cellref_v7 (pd, b,
					  data + 19, (guint16 *)(data + 16));
			ms_biff_put_var_write (pd->bp, data, 20);
		} else {
			guint16 extn_idx = ms_excel_write_get_externsheet_idx (pd->sheet->wb,
									       a->sheet,
									       b->sheet);
			GSF_LE_SET_GUINT16 (data, extn_idx);
			write_cellref_v8 (pd, a,
					  (guint16 *)(data + 6), (guint16 *)(data + 2));
			write_cellref_v8 (pd, b,
					  (guint16 *)(data + 8), (guint16 *)(data + 4));
			ms_biff_put_var_write (pd->bp, data, 10);
		}
	}
}

static void
write_ref (PolishData *pd, const CellRef *ref)
{
	guint8 data[24];

	g_return_if_fail (pd);
	g_return_if_fail (ref);

	if (!ref->sheet ||
	    ref->sheet == pd->sheet->gnum_sheet) {
		push_guint8 (pd, OP_VALUE (FORMULA_PTG_REF));
		if (pd->ver <= MS_BIFF_V7) {
			write_cellref_v7 (pd, ref, data + 2, (guint16 *)data);
			ms_biff_put_var_write (pd->bp, data, 3);
		} else { /* Duff docs */
			write_cellref_v8 (pd, ref, (guint16 *)(data + 2), (guint16 *)data);
			ms_biff_put_var_write (pd->bp, data, 4);
		}
	} else {
		push_guint8 (pd, OP_VALUE (FORMULA_PTG_REF_3D));
		if (pd->ver <= MS_BIFF_V7) {
			guint16 extn_idx = ms_excel_write_get_sheet_idx (pd->sheet->wb,
									 ref->sheet);
			GSF_LE_SET_GUINT16 (data, 0xffff); /* FIXME ? */
			GSF_LE_SET_GUINT32 (data +  2, 0x0);
			GSF_LE_SET_GUINT32 (data +  6, 0x0);
			GSF_LE_SET_GUINT16 (data + 10, extn_idx);
			GSF_LE_SET_GUINT16 (data + 12, extn_idx);
			write_cellref_v7 (pd, ref, data + 16,
					  (guint16 *)(data + 14));
			ms_biff_put_var_write (pd->bp, data, 17);
		} else {
			guint16 extn_idx = ms_excel_write_get_externsheet_idx (pd->sheet->wb,
									       ref->sheet,
									       NULL);
			GSF_LE_SET_GUINT16 (data, extn_idx);
			write_cellref_v8 (pd, ref, (guint16 *)(data + 2),
					  (guint16 *)(data + 1));
			ms_biff_put_var_write (pd->bp, data, 6);
		}
	}
}

static void
write_funcall (PolishData *pd, FormulaCacheEntry *fce, GnmExpr const *tree)
{
	GnmExprList *args     = tree->func.arg_list;
	gint     num_args = 0;
	gboolean prompt   = 0;
	gboolean cmdequiv = 0;

	/* Add in function */
	if (fce->type == CACHE_ENAME_V8 && pd->ver >= MS_BIFF_V8)
		write_string (pd, fce->u.ename_v8.name);
	else if (DO_IT) {
		if (fce->type == CACHE_ENAME_V7 && pd->ver <= MS_BIFF_V7) {
		push_guint8  (pd, FORMULA_PTG_NAME_X);
		push_guint16 (pd, 1);
		push_guint32 (pd, 0); /* reserved */
		push_guint32 (pd, 0); /* reserved */
		push_guint16 (pd, fce->u.ename_v7.idx);
		push_guint32 (pd, 0); /* reserved */
		push_guint32 (pd, 0); /* reserved */
		push_guint32 (pd, 0); /* reserved */
		}
	}

	for (args = tree->func.arg_list ; args ; ) {
		write_node (pd, args->data, 0);
		num_args++;
		args = args->next;
		if (args != NULL && num_args == fce->u.std.fd->num_args) {
			gnm_io_warning (pd->sheet->wb->io_context, 
				_("Too many arguments for function, MS Excel expects exactly %d and we have more"),
				fce->u.std.fd->num_args);
		}
	}

#if FORMULA_DEBUG > 1
	printf ("Writing function '%s' as idx %d, args %d\n",
		name, fce->u.std.idx, fce->u.std.fd->num_args);
#endif

	if (num_args >= 128) {
	}

	if (fce->type == CACHE_STD) {
		if (fce->u.std.fd->num_args < 0) {
			push_guint8  (pd, FORMULA_PTG_FUNC_VAR);
			push_guint8  (pd, num_args | (prompt&0x80));
			push_guint16 (pd, fce->u.std.idx | (cmdequiv&0x8000));
		} else {
			/* If XL requires more arguments than we do
			 * pad the remainder with missing args
			 */
			while (num_args++ < fce->u.std.fd->num_args)
				push_guint8 (pd, FORMULA_PTG_MISSARG);

			push_guint8  (pd, FORMULA_PTG_FUNC);
			push_guint16 (pd, fce->u.std.idx);
		}
	} else { /* Undocumented :-) */
		push_guint8  (pd, FORMULA_PTG_FUNC_VAR + 0x20);
		push_guint8  (pd, (num_args + 1) | (prompt&0x80));
		push_guint16 (pd, 0xff | (cmdequiv&0x8000));
	}
}

static void
write_node (PolishData *pd, GnmExpr const *tree, int paren_level)
{
	static const struct {
		guint8 xl_op;
		int prec;	              /* Precedences -- should match parser.y  */
		int assoc_left, assoc_right;  /* 0: no, 1: yes.  */
	} operations [] = {
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
		{ FORMULA_PTG_EXP,	 6, 0, 1 },
		{ FORMULA_PTG_CONCAT,	 2, 1, 0 },
		{ 0, 0, 0, 0 }, /* Funcall  */
		{ 0, 0, 0, 0 }, /* Name     */
		{ 0, 0, 0, 0 }, /* Constant */
		{ 0, 0, 0, 0 }, /* Var      */
		{ FORMULA_PTG_U_MINUS,	 5, 0, 0 }, /* Unary - */
		{ FORMULA_PTG_U_PLUS,	 5, 0, 0 }, /* Unary + */
		{ FORMULA_PTG_PERCENT,	 5, 0, 0 }, /* Percentage (NOT MODULO) */
		{ 0, 0, 0, 0 },	/* Array    */
		{ 0, 0, 0, 0 }, /* Set      */
		{ FORMULA_PTG_RANGE,	 0, 0, 0 }
	};
	int op;
	g_return_if_fail (pd);
	g_return_if_fail (tree);

	op = tree->any.oper;
	switch (op) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_ANY_BINARY : {
		int const prec = operations[op].prec;

		write_node  (pd, tree->binary.value_a,
			     prec - operations[op].assoc_left);
		write_node  (pd, tree->binary.value_b,
			     prec - operations[op].assoc_right);
		push_guint8 (pd, operations[op].xl_op);
		if (prec <= paren_level)
			push_guint8 (pd, FORMULA_PTG_PAREN);
		break;
	}

	case GNM_EXPR_OP_FUNCALL : {
		FormulaCacheEntry *fce;

		fce = get_formula_index (pd->sheet,
		                         function_def_get_name (tree->func.func));
		if (fce)
			write_funcall (pd, fce, tree);
		else {
			gchar const *name = function_def_get_name (tree->func.func);
			gchar *err = g_strdup_printf ("Untranslatable '%s'", name);
#if FORMULA_DEBUG > 0
			printf ("Untranslatable function '%s'\n", name);
#endif
			write_string (pd, err);
			g_free (err);
		}
		break;
	}
        case GNM_EXPR_OP_CONSTANT : {
		Value const *v = tree->constant.value;
		switch (v->type) {

		case VALUE_INTEGER : {
			guint8 data[10];
			int i = value_get_as_int (v);
			if (i >= 0 && i < 1<<16) {
				GSF_LE_SET_GUINT8  (data, FORMULA_PTG_INT);
				GSF_LE_SET_GUINT16 (data + 1, i);
				ms_biff_put_var_write (pd->bp, data, 3);
			} else {
				GSF_LE_SET_GUINT8 (data, FORMULA_PTG_NUM);
				gsf_le_set_double (data + 1, value_get_as_float (v));
				ms_biff_put_var_write (pd->bp, data, 9);
			}
			break;
		}
		case VALUE_FLOAT : {
			guint8 data[10];
			GSF_LE_SET_GUINT8 (data, FORMULA_PTG_NUM);
			gsf_le_set_double (data+1, value_get_as_float (v));
			ms_biff_put_var_write (pd->bp, data, 9);
			break;
		}
		case VALUE_BOOLEAN : {
			guint8 data[2];
			GSF_LE_SET_GUINT8 (data, FORMULA_PTG_BOOL);
			GSF_LE_SET_GUINT8 (data+1, v->v_bool.val ? 1 : 0);
			ms_biff_put_var_write (pd->bp, data, 2);
			break;
		}

		case VALUE_ERROR : {
			guint8 data[2];
			GSF_LE_SET_GUINT8 (data, FORMULA_PTG_ERR);
			GSF_LE_SET_GUINT8 (data+1, ms_excel_write_map_errcode (v));
			ms_biff_put_var_write (pd->bp, data, 2);
			break;
		}

		case VALUE_EMPTY : {
			guint8 data = FORMULA_PTG_MISSARG;
			ms_biff_put_var_write (pd->bp, &data, 1);
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
			ms_biff_put_var_write (pd->bp, data, 8);

			pd->arrays = g_list_append (pd->arrays, (gpointer)v);
			break;
		}

		default : {
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
	case GNM_EXPR_OP_ANY_UNARY : {
		int const prec = operations[op].prec;

		write_node (pd, tree->unary.value, operations[op].prec);
		push_guint8 (pd, operations[op].xl_op);
		if (prec <= paren_level)
			push_guint8 (pd, FORMULA_PTG_PAREN);
		break;
	}

	case GNM_EXPR_OP_CELLREF :
		write_ref (pd, &tree->cellref.ref);
		break;

	case GNM_EXPR_OP_NAME : {
		guint8 data[14];
		guint16 idx;
		for (idx = 0; idx <14; idx++) data[idx] = 0;

		for (idx = 0; idx < pd->sheet->wb->names->len; idx++)
			if (!strcmp(tree->name.name->name->str,
				    (char *) g_ptr_array_index (pd->sheet->wb->names, idx))) {

			    GSF_LE_SET_GUINT8  (data + 0, FORMULA_PTG_NAME);
			    GSF_LE_SET_GUINT16 (data + 1, idx + 1);
			    ms_biff_put_var_write (pd->bp, data, 15);
			    return;
			}
		break;
	}

	case GNM_EXPR_OP_ARRAY : {
		GnmExprArray const *array = &tree->array;
		guint8 data[5];
		GSF_LE_SET_GUINT8 (data, FORMULA_PTG_EXPR);
		GSF_LE_SET_GUINT16 (data+1, pd->row - array->y);
		GSF_LE_SET_GUINT16 (data+3, pd->col - array->x);
		ms_biff_put_var_write (pd->bp, data, 5);

		/* Be anal */
		g_return_if_fail (paren_level == 0);
		break;
	}

	default : {
		gchar *err = g_strdup_printf ("Unknown Operator %d",
					      tree->any.oper);
		write_string (pd, err);
		g_free (err);
		g_warning ("Unhandled node type %d", tree->any.oper);
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

	g_return_if_fail (pd);
	g_return_if_fail (pd->arrays);

	array = pd->arrays->data;
	g_return_if_fail (array->type == VALUE_ARRAY);

	for (lpy = 0; lpy < array->v_array.y; lpy++) {
		for (lpx = 0; lpx < array->v_array.x; lpx++) {
			const Value *v = array->v_array.vals[lpx][lpy];

			if (VALUE_IS_NUMBER (v)) {
				push_guint8 (pd, 1);
				gsf_le_set_double (data, value_get_as_float (v));
				ms_biff_put_var_write (pd->bp, data, 8);
			} else { /* Can only be a string */
				gint len;
				gchar *buf;
				gchar *txt = value_get_as_string (v);
				push_guint8 (pd, 2);
				len = biff_convert_text(&buf, txt, pd->ver);
				biff_put_text (pd->bp, buf, len,
					       pd->ver, TRUE, AS_PER_VER);
				g_free (buf);
				g_free (txt);
			}
		}
	}

	pd->arrays = g_list_next (pd->arrays);
}

guint32
ms_excel_write_formula (BiffPut *bp, ExcelSheet *sheet, GnmExpr const *expr,
			int fn_col, int fn_row, int paren_level)
{
	PolishData pd;
	unsigned start;
	guint32 len;

	g_return_val_if_fail (bp, 0);
	g_return_val_if_fail (expr, 0);
	g_return_val_if_fail (sheet, 0);

	pd.col    = fn_col;
	pd.row    = fn_row;
	pd.sheet  = sheet;
	pd.bp     = bp;
	pd.arrays = NULL;
	pd.ver    = sheet->wb->ver;

	start = bp->length;
	write_node (&pd, expr, 0);
	len = bp->length - start;

	if (pd.arrays) {
		push_guint16 (&pd, 0x0); /* Sad but true */
		push_guint8  (&pd, 0x0);

		while (pd.arrays)
			write_arrays (&pd);
	}

	return len;
}
