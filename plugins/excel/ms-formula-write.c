/*
 * ms-formula-write.c: MS Excel <- Gnumeric formula conversion
 * See: S59E2B.HTM
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *
 * (C) 1998, 1999 Michael Meeks
 */

#include <fcntl.h>
#include <assert.h>
#include <stdio.h>

#include <config.h>
#include <gnome.h>

#include "gnumeric.h"
#include "func.h"

#include "utils.h"

#include "excel.h"
#include "ms-biff.h"
#include "ms-formula-write.h"
#include "formula-types.h"

#define FORMULA_DEBUG 0
extern int ms_excel_formula_debug;
/*#define DO_IT (ms_excel_formula_debug > 0)*/
#define DO_IT (1)

#define OP_REF(o)   (o + FORMULA_CLASS_REF)
#define OP_VALUE(o) (o + FORMULA_CLASS_VALUE)
#define OP_ARRAY(o) (o + FORMULA_CLASS_ARRAY)

typedef struct _PolishData PolishData;
typedef struct _FormulaCacheEntry FormulaCacheEntry;

static void write_node (PolishData *pd, ExprTree *tree);

/* FIXME: Leaks like a leaky bucket */

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
	sheet->formula_cache = g_hash_table_new (g_str_hash, g_str_equal);
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
			     formula_func_data[i].prefix, fce);

	return fce;
}

static FormulaCacheEntry *
formula_cache_new_ename (ExcelSheet *sheet, const char *name)
{
	FormulaCacheEntry *fce = g_new (FormulaCacheEntry, 1);

	if (sheet->wb->ver >= eBiffV8) {
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
		if (!g_strcasecmp (formula_func_data[i].prefix,
				   name)) {
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
ms_formula_build_pre_data (ExcelSheet *sheet, ExprTree *tree)
{
	g_return_if_fail (tree != NULL);
	g_return_if_fail (sheet != NULL);

	switch (tree->oper) {

	case OPER_ANY_BINARY:
		ms_formula_build_pre_data (sheet, tree->u.binary.value_a);
		ms_formula_build_pre_data (sheet, tree->u.binary.value_b);
		break;

	case OPER_ANY_UNARY:
		ms_formula_build_pre_data (sheet, tree->u.value);
		break;

	case OPER_FUNCALL:
	{
		GList  *l;
		FormulaCacheEntry *fce;
		const gchar *name = tree->u.function.symbol->str;

		for (l = tree->u.function.arg_list; l;
		     l = g_list_next (l))
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
			   eBiff_version ver)
{
	if (which == EXCEL_EXTERNNAME) {
		if (sheet->wb->ver <= eBiffV7) {
			GList *l = NULL;
			int idx = 1;

			g_hash_table_foreach (sheet->formula_cache,
					      (GHFunc)queue_externname,
					      &l);

			while (l) {
				FormulaCacheEntry *fce = l->data;
				guint8  data[8];
				char   *txt;

				if (DO_IT) {
				ms_biff_put_var_next (bp, BIFF_EXTERNNAME);
				MS_OLE_SET_GUINT32 (data + 0, 0x0);
				MS_OLE_SET_GUINT16 (data + 4, 0x0);
				ms_biff_put_var_write (bp, data, 6);
				txt = g_strdup (fce->u.ename_v7.name);
				g_strup (txt); /* scraping the barrel here */
				biff_put_text (bp, txt,
					       eBiffV7,
					       TRUE, AS_PER_VER);
				g_free (txt);
				MS_OLE_SET_GUINT32 (data, 0x171c0002); /* Magic hey :-) */
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
	eBiff_version ver;
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
	MS_OLE_SET_GUINT16 (data, b);
	ms_biff_put_var_write (pd->bp, data, sizeof(data));
}

static void
push_guint32 (PolishData *pd, guint32 b)
{
	guint8 data[4];
	MS_OLE_SET_GUINT32 (data, b);
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

	MS_OLE_SET_GUINT16 (out_row, row);
	MS_OLE_SET_GUINT8  (out_col, col);
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

	MS_OLE_SET_GUINT16 (out_row, row);
	MS_OLE_SET_GUINT16 (out_col, col);
}

static void
write_string (PolishData *pd, const gchar *txt)
{
	if (!txt || txt[0] == '\0')
		push_guint8 (pd, FORMULA_PTG_MISSARG);
	else {
		push_guint8 (pd, FORMULA_PTG_STR);
		biff_put_text (pd->bp, txt, pd->ver, TRUE, AS_PER_VER);
	}
}

static void
write_area (PolishData *pd, const CellRef *a, const CellRef *b)
{
	guint8 data[24];

	if (!a->sheet || !b->sheet ||
	    (a->sheet == pd->sheet->gnum_sheet &&
	     a->sheet == b->sheet)) {
		push_guint8 (pd, OP_REF (FORMULA_PTG_AREA));
		if (pd->ver <= eBiffV7) {
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
		if (pd->ver <= eBiffV7) {
			MS_OLE_SET_GUINT16 (data, 0); /* FIXME ? */
			MS_OLE_SET_GUINT32 (data +  2, 0x0);
			MS_OLE_SET_GUINT32 (data +  6, 0x0);
			MS_OLE_SET_GUINT16 (data + 10, first_idx);
			MS_OLE_SET_GUINT16 (data + 12, second_idx);
			write_cellref_v7 (pd, a,
					  data + 18, (guint16 *)(data + 14));
			write_cellref_v7 (pd, b,
					  data + 19, (guint16 *)(data + 16));
			ms_biff_put_var_write (pd->bp, data, 20);
		} else {
			guint16 extn_idx = ms_excel_write_get_externsheet_idx (pd->sheet->wb,
									       a->sheet,
									       b->sheet);
			MS_OLE_SET_GUINT16 (data, extn_idx);
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
		if (pd->ver <= eBiffV7) {
			write_cellref_v7 (pd, ref, data + 2, (guint16 *)data);
			ms_biff_put_var_write (pd->bp, data, 3);
		} else { /* Duff docs */
			write_cellref_v8 (pd, ref, (guint16 *)(data + 2), (guint16 *)data);
			ms_biff_put_var_write (pd->bp, data, 4);
		}
	} else {
		push_guint8 (pd, OP_VALUE (FORMULA_PTG_REF_3D));
		if (pd->ver <= eBiffV7) {
			guint16 extn_idx = ms_excel_write_get_sheet_idx (pd->sheet->wb,
									 ref->sheet);
			MS_OLE_SET_GUINT16 (data, 0); /* FIXME ? */
			MS_OLE_SET_GUINT32 (data +  2, 0x0);
			MS_OLE_SET_GUINT32 (data +  6, 0x0);
			MS_OLE_SET_GUINT16 (data + 10, extn_idx);
			MS_OLE_SET_GUINT16 (data + 12, extn_idx);
			write_cellref_v7 (pd, ref, data + 16,
					  (guint16 *)(data + 14));
			ms_biff_put_var_write (pd->bp, data, 17);
		} else {
			guint16 extn_idx = ms_excel_write_get_externsheet_idx (pd->sheet->wb,
									       ref->sheet,
									       NULL);
			MS_OLE_SET_GUINT16 (data, extn_idx);
			write_cellref_v8 (pd, ref, (guint16 *)(data + 2),
					  (guint16 *)(data + 1));
			ms_biff_put_var_write (pd->bp, data, 6);
		}
	}
}

static void
write_funcall (PolishData *pd, FormulaCacheEntry *fce, ExprTree *tree)
{
	GList   *args     = tree->u.function.arg_list;
	gint     num_args = 0;
	gboolean prompt   = 0;
	gboolean cmdequiv = 0;

	/* Add in function */
	if (fce->type == CACHE_ENAME_V8 && pd->ver >= eBiffV8)
		write_string (pd, fce->u.ename_v8.name);
	else if (DO_IT) {
		if (fce->type == CACHE_ENAME_V7 && pd->ver <= eBiffV7) {
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
	
	while (args) {
		write_node (pd, args->data);
		args = g_list_next (args);
		num_args++;
	}
	
#if FORMULA_DEBUG > 1
	printf ("Writing function '%s' as idx %d, args %d\n",
		name, fce->u.std.idx, fce->u.std.fd->num_args);
#endif
	
	g_assert (num_args < 128);
	if (fce->type == CACHE_STD) {
		if (fce->u.std.fd->num_args < 0) {
			push_guint8  (pd, FORMULA_PTG_FUNC_VAR);
			push_guint8  (pd, num_args | (prompt&0x80));
			push_guint16 (pd, fce->u.std.idx | (cmdequiv&0x8000));
		} else {
			push_guint8  (pd, FORMULA_PTG_FUNC);
			push_guint16 (pd, fce->u.std.idx);
		}
	} else if (DO_IT) { /* Undocumented :-) */
		push_guint8  (pd, FORMULA_PTG_FUNC_VAR + 0x20);
		push_guint8  (pd, (num_args + 1) | (prompt&0x80));
		push_guint16 (pd, 0xff | (cmdequiv&0x8000));
	}
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

		fce = get_formula_index (pd->sheet, 
					 tree->u.function.symbol->str);
		if (fce)
			write_funcall (pd, fce, tree);
		else {
			gchar *name = tree->u.function.symbol->str;
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
		{
			guint8 data[10];
			int i = value_get_as_int (v);
			if (i >= 0 && i < 1<<16) {
				MS_OLE_SET_GUINT8  (data, FORMULA_PTG_INT);
				MS_OLE_SET_GUINT16 (data + 1, i);
				ms_biff_put_var_write (pd->bp, data, 3);
			} else {
				MS_OLE_SET_GUINT8 (data, FORMULA_PTG_NUM);
				BIFF_SETDOUBLE (data + 1, value_get_as_float (v));
				ms_biff_put_var_write (pd->bp, data, 9);
			}
			break;
		}
		case VALUE_FLOAT:
		{
			guint8 data[10];
			MS_OLE_SET_GUINT8 (data, FORMULA_PTG_NUM);
			BIFF_SETDOUBLE (data+1, value_get_as_float (v));
			ms_biff_put_var_write (pd->bp, data, 9);
			break;
		}
		case VALUE_BOOLEAN:
		{
			guint8 data[2];
			MS_OLE_SET_GUINT8 (data, FORMULA_PTG_BOOL);
			MS_OLE_SET_GUINT8 (data+1, v->v.v_bool ? 1 : 0);
			ms_biff_put_var_write (pd->bp, data, 2);
			break;
		}

		case VALUE_ERROR:
		{
			guint8 data[2];
			MS_OLE_SET_GUINT8 (data, FORMULA_PTG_ERR);
			MS_OLE_SET_GUINT8 (data+1, ms_excel_write_map_errcode (v));
			ms_biff_put_var_write (pd->bp, data, 2);
			break;
		}

		case VALUE_EMPTY:
		{
			guint8 data = FORMULA_PTG_MISSARG;
			ms_biff_put_var_write (pd->bp, &data, 1);
			break;
		}

		case VALUE_STRING:
			write_string (pd, v->v.str->str);
			break;

		case VALUE_CELLRANGE:
		{ /* FIXME: Could be 3D ! */
			write_area (pd, &v->v.cell_range.cell_a,
				    &v->v.cell_range.cell_b);
			break;
		}

                /* See S59E2B.HTM for some really duff docs */
		case VALUE_ARRAY: /* Guestimation */
		{
			guint8 data[8];
			
			if (v->v.array.x > 256 ||
			    v->v.array.y > 65536)
				g_warning ("Array far too big");

			MS_OLE_SET_GUINT8  (data + 0, FORMULA_PTG_ARRAY);
			MS_OLE_SET_GUINT8  (data + 1, v->v.array.x - 1);
			MS_OLE_SET_GUINT16 (data + 2, v->v.array.y - 1);
			MS_OLE_SET_GUINT16 (data + 4, 0x0); /* ? */
			MS_OLE_SET_GUINT16 (data + 6, 0x0); /* ? */
			ms_biff_put_var_write (pd->bp, data, 8);

			pd->arrays = g_list_append (pd->arrays, v);
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

	case OPER_PERCENT:
		write_node  (pd, tree->u.value);
		push_guint8 (pd, FORMULA_PTG_PERCENT);
		break;

	case OPER_VAR:
		write_ref (pd, &tree->u.ref);
		break;

	case OPER_ARRAY:
	case OPER_NAME:
	default:
	{
		gchar *err = g_strdup_printf ("Unknown Operator %d", tree->oper);
		write_string (pd, err);
		g_free (err);
		printf ("Unhandled node type %d\n", tree->oper);
#if FORMULA_DEBUG > 0
#endif
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

	for (lpy=0; lpy < array->v.array.y; lpy++) {
		for (lpx=0; lpx < array->v.array.x; lpx++) {
			const Value *v = array->v.array.vals[lpx][lpy];
			
			if (VALUE_IS_NUMBER (v)) {
				push_guint8 (pd, 1);
				BIFF_SETDOUBLE (data, value_get_as_float (v));
				ms_biff_put_var_write (pd->bp, data, 8);
			} else { /* Can only be a string */
				gchar *txt = value_get_as_string (v);
				push_guint8 (pd, 2);
				biff_put_text (pd->bp, txt,
					       pd->ver, TRUE, AS_PER_VER);
				g_free (txt);
			}
		}
	}

	pd->arrays = g_list_next (pd->arrays);
}

guint32
ms_excel_write_formula (BiffPut *bp, ExcelSheet *sheet, ExprTree *expr,
			int fn_col, int fn_row)
{
	PolishData *pd;
	MsOlePos start;
	guint32 len;
	
	g_return_val_if_fail (bp, 0);
	g_return_val_if_fail (expr, 0);
	g_return_val_if_fail (sheet, 0);

	pd = g_new (PolishData, 1);
	pd->col    = fn_col;
	pd->row    = fn_row;
	pd->sheet  = sheet;
	pd->bp     = bp;
	pd->arrays = NULL;
	pd->ver    = sheet->wb->ver;

	start = bp->length;
	write_node (pd, expr);
	len = bp->length - start;

	if (pd->arrays) {
		push_guint16 (pd, 0x0); /* Sad but true */
		push_guint8  (pd, 0x0);

		while (pd->arrays)
			write_arrays (pd);
	}
	
	return len;
}
