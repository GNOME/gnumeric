/*
 * sheet-autofill.c: Provides the autofill features
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org), 1998
 *
 * This is more complex than it would look at first sight,
 * as we have to support autofilling of mixed data and
 * we have to do the filling accordingly.
 *
 * The idea is that the autofill routines first classify
 * the source cells into different types and the deltas
 * are computed on a per-group basis.
 */
#include <config.h>

#include <gnome.h>
#include <string.h>
#include <ctype.h>
#include "gnumeric.h"
#include "sheet-autofill.h"
#include "dates.h"

typedef enum {

	/*
	 * FILL_INVALID: Should never happen, used only as a flag
	 */
	FILL_INVALID,
	
	/*
	 * FILL_EMPTY: Cell is empty
	 */
	FILL_EMPTY,

	/*
	 * FILL_STRING_CONSTANT: We could not figure any
	 * method of autoincrement, just duplicate this constant
	 */
	FILL_STRING_CONSTANT,

	/*
	 * FILL_STRING_WITH_NUMBER: The string contains a number
	 * that is in a position where we can auto-increment.
	 */
	FILL_STRING_WITH_NUMBER,

	/*
	 * FILL_STRING_LIST: The string matches a component in
	 * one of the autofill string lists.
	 */
	FILL_STRING_LIST,

	/*
	 * FILL_NUMBER: We are dealing with a number
	 */
	FILL_NUMBER,

	/*
	 * FILL_FORMULA: This is a formula
	 */
	FILL_FORMULA
} FillType;

typedef struct {
	int    count;
	char **items;
} AutoFillList;

struct FillItem {
	FillType  type;
	Cell      *reference;
	
	union {
		ExprTree *formula;
		Value    *value;
		String   *str;
		struct {
			AutoFillList *list;
			int           num;
			int           was_i18n;
		} list;
		struct {
			String   *str;
			int       pos, num;
		} numstr;
	} v;

	int delta_is_float;
	union {
		double    d_float;
		int       d_int;
	} delta;

	struct FillItem *group_last;
};

typedef struct FillItem FillItem;

static GList *autofill_lists;

void
autofill_register_list (char **list)
{
	AutoFillList *afl;
	char **p = list;
	
	while (*p)
		p++;

	afl = g_new (AutoFillList, 1);
	afl->count = p - list;
	afl->items = list;

	autofill_lists = g_list_prepend (autofill_lists, afl);
}

static AutoFillList *
matches_list (String *str, int *n, int *is_i18n)
{
	GList *l;
	char *s = str->str;

	for (l = autofill_lists; l; l = l->next){
		AutoFillList *afl = l->data;
		int i;
		
		for (i = 0; i < afl->count; i++){
			char *english_text, *translated_text;

			english_text = afl->items [i];
			if (*english_text == '*')
				english_text++;
			
			if ((strcasecmp (english_text, s) == 0)){
				*is_i18n = FALSE;
				*n = i;
				return afl;
			}

			translated_text = _(afl->items [i]);
			if (*translated_text == '*')
				translated_text++;
			
			if (strcasecmp (translated_text, s) == 0){
				*is_i18n = TRUE;
				*n = i;
				return afl;
			}
		}
	}
	return NULL;
}

static int
string_has_number (String *str, int *num, int *pos)
{
	char *s = str->str, *p;
	int l = strlen (s);
	gboolean found_number = FALSE;
	
	if (isdigit (*s)){
		*num = atoi (s);
		*pos = 0;
		return TRUE;
	}
	if (l <= 1)
		return FALSE;
	
	for (p = s + l - 1; p > str->str && isdigit (*p); p--)
		found_number = TRUE;

	if (!found_number)
		return FALSE;
	
	p++;
	*num = atoi (p);
	*pos = p - str->str;

	return TRUE;
}

static void
fill_item_destroy (FillItem *fi)
{
	switch (fi->type){
	case FILL_STRING_LIST:
		break;
		
	case FILL_STRING_CONSTANT:
		string_unref (fi->v.str);
		break;

	case FILL_STRING_WITH_NUMBER:
		string_unref (fi->v.numstr.str);
		break;
		
	default:
	}
	g_free (fi);
}

static FillItem *
fill_item_new (Cell *cell)
{
	Value     *value;
	ValueType  value_type;
	FillItem  *fi;

	fi = g_new (FillItem, 1);
	fi->type = FILL_EMPTY;

	fi->reference = cell;
	if (!cell)
		return fi;

	value = cell->value;
	value_type = value->type;
	
	if (cell->parsed_node){
		fi->type = FILL_FORMULA;
		fi->v.formula = cell->parsed_node;
		
		return fi;
	}

	if (value_type == VALUE_INTEGER || value_type == VALUE_FLOAT){
		fi->type    = FILL_NUMBER;
		fi->v.value = value;

		return fi;
	}

	if (value_type == VALUE_STRING){
		AutoFillList *list;
		int  num, pos, i18;
		
		fi->type = FILL_STRING_CONSTANT;
		fi->v.str = string_ref (value->v.str);
		
		list = matches_list (value->v.str, &num, &i18);
		if (list){
			fi->type = FILL_STRING_LIST;
			fi->v.list.list = list;
			fi->v.list.num  = num;
			fi->v.list.was_i18n = i18;
			return fi;
		}

		if (string_has_number (value->v.str, &num, &pos)){
			fi->type = FILL_STRING_WITH_NUMBER;
			fi->v.numstr.str = value->v.str;
			fi->v.numstr.num = num;
			fi->v.numstr.pos = pos;
		}
	}
	return fi;
}

/*
 * Computes the delta for the items of the same type in fill_item_list
 * and stores the delta result in the last element (we get this as
 * the parameter last
 */
static void
autofill_compute_delta (GList *list_last, GList *fill_item_list)
{
	FillItem *fi = list_last->data;
	FillItem *lfi;
	
	switch (fi->type){
	case FILL_NUMBER: {
		double a, b;
		
		if (!list_last->prev){
			if (fi->v.value->type == VALUE_INTEGER){
				fi->delta_is_float = FALSE;
				fi->delta.d_int = 1;
			} else {
				fi->delta_is_float = TRUE;
				fi->delta.d_float = 1.0;
			}
			return;
		}
		lfi = list_last->prev->data;

		if (fi->v.value->type == VALUE_INTEGER && lfi->v.value->type == VALUE_INTEGER){
			fi->delta_is_float = FALSE;
			fi->delta.d_int = fi->v.value->v.v_int - lfi->v.value->v.v_int;
			return;
		}

		a = value_get_as_double (lfi->v.value);
		b = value_get_as_double (fi->v.value);

		fi->delta_is_float = TRUE;
		fi->delta.d_float = b - a;
		return;
	}

	case FILL_STRING_WITH_NUMBER: 
		fi->delta_is_float = FALSE;
		fi->delta.d_int = 1;

		if (list_last->prev){
			lfi = list_last->prev->data;
				
			fi->delta.d_int = fi->v.numstr.num - lfi->v.numstr.num;
		}
		return;
		
	case FILL_STRING_LIST:
		fi->delta_is_float = FALSE;
		if (list_last->prev){
			lfi = list_last->prev->data;
			
			fi->delta.d_int = fi->v.list.num - lfi->v.list.num;
		} else
			fi->delta.d_int = 1;
		return;
		
	case FILL_EMPTY:
	case FILL_STRING_CONSTANT:
	case FILL_FORMULA:
	case FILL_INVALID:
		return;

	}
}

/*
 * Determines if two FillItems are compatible
 */
static int
type_is_compatible (FillItem *last, FillItem *current)
{
	if (last == NULL)
		return FALSE;

	if (last->type != current->type)
		return FALSE;

	if (last->type == FILL_STRING_LIST){
		if (last->v.list.list != current->v.list.list)
			return FALSE;
		if (last->v.list.was_i18n != current->v.list.was_i18n)
			return FALSE;
	}

	return TRUE;
}

static GList *
autofill_create_fill_items (Sheet *sheet, int x, int y, int region_count, int col_inc, int row_inc)
{
	FillItem *last;
	GList *item_list, *all_items, *l;
	int i;
	
	last = NULL;
	item_list = all_items = NULL;

	for (i = 0; i < region_count; i++){
		FillItem *fi;
		Cell *cell;
		
		cell = sheet_cell_get (sheet, x, y);
		fi = fill_item_new (cell);

		if (!type_is_compatible (last, fi)){
			if (last){
				all_items = g_list_append (all_items, item_list);
				item_list = NULL;
			}

			last = fi;
		}
		
		item_list = g_list_append (item_list, fi);
		
		x += col_inc;
		y += row_inc;
	}

	if (item_list)
		all_items = g_list_append (all_items, item_list);

	/*
	 * Make every non-ending group point to the end element
	 * and compute the deltas.
	 */
	for (l = all_items; l; l = l->next){
		GList *group = l->data, *ll, *last_item;
		FillItem *last_fi;

		last_item = g_list_last (group);
		last_fi = last_item->data;
		for (ll = group; ll; ll = ll->next){
			FillItem *fi = ll->data;

			fi->group_last = last_fi;
		}

		autofill_compute_delta (last_item, group);
	}

	return all_items;
}

static void
autofill_destroy_fill_items (GList *all_items)
{
	GList *l;

	for (l = all_items; l; l = l->next){
		GList *ll, *sub = l->data;

		for (ll = sub; ll; ll = ll->next){
			FillItem *fi = ll->data;
			
			fill_item_destroy (fi);
		}
		g_list_free (sub);
	}
	g_list_free (all_items);
}

static void
autofill_cell (Cell *cell, int idx, FillItem *fi)
{
	cell_set_style (cell, fi->reference->style);
	
	switch (fi->type){
	case FILL_EMPTY:
	case FILL_INVALID:
		g_warning ("This case should not be handled here\n");
		return;

	case FILL_STRING_CONSTANT:
		cell_set_text (cell, fi->v.str->str);
		return;

	case FILL_STRING_WITH_NUMBER: {
		FillItem *last = fi->group_last;
		char buffer [sizeof (int) * 4], *v;
		int i;
		
		i = last->v.numstr.num + idx * last->delta.d_int;
		snprintf (buffer, sizeof (buffer)-1, "%d", i);
		
		if (last->v.numstr.pos == 0){
			char *p = last->v.numstr.str->str;

			while (*p && isdigit (*p))
			       p++;
			
			v = g_copy_strings (buffer, p, NULL);
		} else {
			char *n = g_strdup (last->v.numstr.str->str);
			n [last->v.numstr.pos] = 0;

			v = g_copy_strings (n, buffer, NULL);
			g_free (n);
		}
		cell_set_text (cell, v);
		g_free (v);
		return;
	}

	case FILL_NUMBER: {
		FillItem *last = fi->group_last;
		char buffer [50];
		
		if (last->delta_is_float){
			double d;
			
			d = last->v.value->v.v_float + idx * last->delta.d_float;
			snprintf (buffer, sizeof (buffer)-1, "%g", d);
			cell_set_text (cell, buffer);
		} else {
			int i;
			
			i = last->v.value->v.v_int + idx * last->delta.d_int;
			snprintf (buffer, sizeof (buffer)-1, "%d", i);
			cell_set_text (cell, buffer);
		}
		return;
	}
	
	case FILL_STRING_LIST: {
		FillItem *last = fi->group_last;
		char *text;
		int n;
		
		n = last->v.list.num + idx * last->delta.d_int;

		n %= last->v.list.list->count;

		if (n < 0)
			n = (last->v.list.list->count + n);

		text = last->v.list.list->items [n];
		if (last->v.list.was_i18n)
			text = _(text);

		if (*text == '*')
			text++;
		
		cell_set_text (cell, text);
			       
		return;
	}
		
	case FILL_FORMULA: {
		cell_set_formula_tree (cell, fi->v.formula);
		return;
	}
	
	}
}

static void
sheet_autofill_dir (Sheet *sheet,
		    int base_col,     int base_row,
		    int region_count,
		    int start_pos,    int end_pos,
		    int col_inc,      int row_inc)
{
	GList *all_items, *l, *m;
	int x = base_col;
	int y = base_row;
	int pos, sub_index, loops, group_count;
	
	/*
	 * Create the fill items 
	 */
	all_items = autofill_create_fill_items (
		sheet, base_col, base_row,
		region_count, col_inc, row_inc);
	
	x = base_col + region_count * col_inc;
	y = base_row + region_count * row_inc;
	
	/* Do the autofill */
	l = all_items;
	m = NULL;
	loops = sub_index = group_count = 0;
	for (pos = start_pos + region_count; pos < end_pos; pos++){
		FillItem *fi;
		Cell *cell;

		if ((m && m->next == NULL) || m == NULL){
			if (l == NULL){
				l = all_items;
				loops++;
			}
			m = l->data;
			group_count = g_list_length (m);
			sub_index = 1;
			l = l->next;
		} else {
			m = m->next;
			sub_index++;
		}
		fi = m->data;

		cell = sheet_cell_get (sheet, x, y);

		if (fi->type == FILL_EMPTY){
			if (cell)
				cell_destroy (cell);
		} else {
			if (!cell)
				cell = sheet_cell_new (sheet, x, y);

			autofill_cell (cell, loops * group_count + sub_index, fi);
		}
		
		x += col_inc;
		y += row_inc;
	}

	autofill_destroy_fill_items (all_items);
	
	workbook_recalc (sheet->workbook);
}

static void
autofill_init (void)
{
	autofill_register_list (day_short);
	autofill_register_list (day_long);
	autofill_register_list (month_short);
	autofill_register_list (month_long);
}

/*
 * Autofills a range of cells in the horizontal or the vertical direction
 */
void
sheet_autofill (Sheet *sheet, int base_col, int base_row, int w, int h, int end_col, int end_row)
{
	int range;
	static int autofill_inited;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (!autofill_inited){
		autofill_init ();
		autofill_inited = TRUE;
	}
	
	if (end_col != base_col + w - 1){
		for (range = 0; range < h; range++)
			sheet_autofill_dir (sheet, base_col, base_row+range, w, base_col, end_col+1, 1, 0);
	} else {
		for (range = 0; range < w; range++)
			sheet_autofill_dir (sheet, base_col+range, base_row, h, base_row, end_row+1, 0, 1);
	}
}

