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
#include "gnumeric.h"
#include "sheet-autofill.h"

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

struct FillItem {
	FillType  type;

	union {
		ExprTree *formula;
		Value    *value;
		String   *str;
		struct {
			GList    *list;
			int      num;
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

typedef struct {
	int    count;
	char **items;
} AutoFillList;

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

static GList *
matches_list (String *str, int *n)
{
	return NULL;
}

static int
string_has_number (String *str, int *num, int *pos)
{
	return FALSE;
}

static void
fill_item_destroy (FillItem *fi)
{
	switch (fi->type){
	case FILL_STRING_LIST:
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
	Value     *value = cell->value;
	ValueType  value_type = value->type;
	FillItem  *fi;

	fi = g_new (FillItem, 1);
	fi->type = FILL_EMPTY;

	if (!cell)
		return fi;
	
	if (CELL_IS_FORMULA (cell)){
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
		void *list;
		int  num, pos;
		
		fi->type = FILL_STRING_CONSTANT;
		fi->v.str = string_ref (value->v.str);
		
		list = matches_list (value->v.str, &num);
		if (list){
			fi->type = FILL_STRING_LIST;
			fi->v.list.list = list;
			fi->v.list.num  = num;
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
				
			fi->delta.d_int = lfi->v.numstr.num - fi->v.numstr.num;
		}
		return;
		
	case FILL_EMPTY:
	case FILL_STRING_CONSTANT:
	case FILL_FORMULA:
	case FILL_INVALID:
	case FILL_STRING_LIST:
		return;

	}
}

static GList *
autofill_create_fill_items (Sheet *sheet, int x, int y, int region_count, int col_inc, int row_inc)
{
	FillType last_type;
	GList *item_list, *all_items, *l;
	int i;
	
	last_type = FILL_INVALID;
	item_list = all_items = NULL;

	for (i = 0; i < region_count; i++){
		FillItem *fi;
		Cell *cell;
		
		cell = sheet_cell_get (sheet, x, y);
		fi = fill_item_new (cell);

	        if (fi->type != last_type){
			if (last_type != FILL_INVALID){
				all_items = g_list_append (all_items, item_list);
				item_list = NULL;
			}

			last_type = fi->type;
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
		GList *group = l->data, *ll, *last;
		FillItem *last_fi;

		last = g_list_last (group);
		last_fi = last->data;
		for (ll = group; ll; ll = ll->next){
			FillItem *fi = ll->data;

			fi->group_last = last_fi;
		}

		autofill_compute_delta (last, group);
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
	switch (fi->type){
	case FILL_EMPTY:
	case FILL_INVALID:
		g_warning ("This case should not be handled here\n");
		return;

	case FILL_STRING_CONSTANT:
		cell_set_text (cell, fi->v.str->str);
		return;

	case FILL_STRING_WITH_NUMBER:
	case FILL_STRING_LIST:
		g_warning ("Not yet handled\n");
		return;

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
	
	case FILL_FORMULA: {
		char *text, *formula;
		
		text = expr_decode_tree (fi->v.formula, cell->col->pos, cell->row->pos);
		formula = g_copy_strings ("=", text, NULL);
		cell_set_text (cell, formula);
		g_free (text);
		g_free (formula);
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
	for (pos = region_count; pos < end_pos; pos++){
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

static char *months [] = {
	N_("Jannuary"),
	N_("February"),
	N_("March"),
	N_("April"),
	N_("May"),
	N_("June"),
	N_("July"),
	N_("August"),
	N_("September"),
	N_("October"),
	N_("November"),
	N_("December"),
	NULL
};

static char *weekdays [] = {
	N_("Monday"),
	N_("Tuesday"),
	N_("Wednesday"),
	N_("Thursday"),
	N_("Friday"),
	N_("Saturday"),
	N_("Sunday"),
	NULL
};

static void
autofill_init (void)
{
	autofill_register_list (months);
	autofill_register_list (weekdays);
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
	
	printf ("base_col=%d, base_row=%d\n", base_col, base_row);
	printf ("width=%d     height=%d\n", w, h);
	printf ("end_col=%d   end_row=%d\n", end_col, end_row);
	
	if (end_col != base_col + w - 1){
		for (range = 0; range < h; range++)
			sheet_autofill_dir (sheet, base_col, base_row+range, w, base_col, end_col+1, 1, 0);
	} else {
		for (range = 0; range < w; range++)
			sheet_autofill_dir (sheet, base_col+range, base_row, h, base_row, end_row+1, 0, 1);
	}
}

