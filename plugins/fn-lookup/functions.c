/*
 * fn-lookup.c:  Built in lookup functions and functions registration
 *
 * Authors:
 *  Michael Meeks <michael@imaginator.com>
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 */

#include <config.h>
#include <glib.h>
#include "numbers.h"
#include "gutils.h"
#include "func.h"
#include "eval.h"
#include "cell.h"

/***************************************************************************/

static char *help_address = {
	N_("@FUNCTION=ADDRESS\n"
	   "@SYNTAX=ADDRESS(row_num,col_num[,abs_num,a1,text])\n"

	   "@DESCRIPTION="
	   "ADDRESS returns a cell address as text for specified row "
	   "and column numbers. "
	   "\n"
	   "If @abs_num is 1 or omitted, ADDRESS returns absolute reference. "
	   "If @abs_num is 2 ADDRESS returns absolute row and relative "
	   "column.  If @abs_num is 3 ADDRESS returns relative row and "
	   "absolute column. "
	   "If @abs_num is 4 ADDRESS returns relative reference. "
	   "If @abs_num is greater than 4 ADDRESS returns #NUM! error. "
	   "\n"
	   "@a1 is a logical value that specifies the reference style.  If "
	   "@a1 is TRUE or omitted, ADDRESS returns an A1-style reference, "
	   "i.e. $D$4.  Otherwise ADDRESS returns an R1C1-style reference, "
	   "i.e. R4C4. "
	   "\n"
	   "@text specifies the name of the worksheet to be used as the "
	   "external reference.  "
	   "\n"
	   "If @row_num or @col_num is less than one, ADDRESS returns #NUM! "
	   "error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_address (FunctionEvalInfo *ei, Value **args)
{
        int   row, col, abs_num, a1;
	gchar *text, *buf;
	Value *v;

	row = value_get_as_int (args[0]);
	col = value_get_as_int (args[1]);

	if (row < 1 || col < 1)
	        return value_new_error (ei->pos, gnumeric_err_NUM);

	if (args[2] == NULL)
	        abs_num = 1;
	else
	        abs_num = value_get_as_int (args [2]);

	if (args[3] == NULL)
	        a1 = 1;
	else {
		gboolean err;
	        a1 = value_get_as_bool (args[3], &err);
		if (err)
		        return value_new_error (ei->pos, gnumeric_err_VALUE);
	}

	if (args[4] == NULL) {
	        text = g_new(gchar, 1);
	        text[0] = '\0';
	} else {
	        gchar *p = args[4]->v.str->str;
		int   space = 0;

		text = g_new(gchar, strlen(p) + 4);
		while (*p)
			if (*p++ == ' ')
			        space = 1;
		if (space)
		        sprintf(text, "'%s'", args[4]->v.str->str);
		else
		        strcpy(text, args[4]->v.str->str);
		strcat(text, "!");
	}

	buf = g_new(gchar, strlen(text) + 50);

	switch (abs_num) {
	case 1:
	        if (a1)
		        sprintf(buf, "%s$%s$%d", text, col_name(col-1), row);
		else
		        sprintf(buf, "%sR%dC%d", text, row, col);
		break;
	case 2:
	        if (a1)
		        sprintf(buf, "%s%s$%d", text, col_name(col-1), row);
		else
		        sprintf(buf, "%sR%dC[%d]", text, row, col);
		break;
	case 3:
	        if (a1)
		        sprintf(buf, "%s$%s%d", text, col_name(col-1), row);
		else
		        sprintf(buf, "%sR[%d]C%d", text, row, col);
		break;
	case 4:
	        if (a1)
		        sprintf(buf, "%s%s%d", text, col_name(col-1), row);
		else
		        sprintf(buf, "%sR[%d]C[%d]", text, row, col);
		break;
	default:
	        g_free(text);
	        g_free(buf);
		return value_new_error (ei->pos, gnumeric_err_NUM);
	}
	v = value_new_string (buf);
	g_free(text);
	g_free(buf);

	return v;
}

/***************************************************************************/

static char *help_choose = {
	N_("@FUNCTION=CHOOSE\n"
	   "@SYNTAX=CHOOSE(index[,value1][,value2]...)\n"

	   "@DESCRIPTION="
	   "CHOOSE returns the value of index @index. "
	   "@index is rounded to an integer if it is not."
	   "\n"
	   "If @index < 1 or @index > number of values: returns #VAL!."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=IF")
};

static Value *
gnumeric_choose (FunctionEvalInfo *ei, GList *l)
{
	int     index;
	int     argc;
	Value  *v;

	argc =  g_list_length (l);

	if (argc < 1 || !l->data)
		return value_new_error (ei->pos, _("#ARG!"));

	v = eval_expr (ei->pos, l->data, EVAL_STRICT);
	if (!v)
		return NULL;

	if ((v->type != VALUE_INTEGER) && (v->type != VALUE_FLOAT)) {
		value_release (v);
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	}

	index = value_get_as_int(v);
	value_release (v);
	l = g_list_next (l);

	while (l){
		index--;
		if (!index)
			return eval_expr (ei->pos, l->data, EVAL_PERMIT_NON_SCALAR);
		l = g_list_next (l);
	}
	return value_new_error (ei->pos, gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_vlookup = {
	N_("@FUNCTION=VLOOKUP\n"
	   "@SYNTAX=VLOOKUP(value,range,column,[approximate])\n"

	   "@DESCRIPTION="
	   "VLOOKUP function finds the row in range that has a first "
	   "column similar to value.  If @approximate is not true it finds "
	   "the row with an exact equivilance.  If @approximate is true, "
	   "then the values must be sorted in order of ascending value for "
	   "correct function; in this case it finds the row with value less "
	   "than @value.  It returns the value in the row found at a 1 based "
	   "offset in @column columns into the @range."
	   "\n"
	   "Returns #NUM! if @column < 0. "
	   "Returns #REF! if @column falls outside @range."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=HLOOKUP")
};

static int
lookup_similar (const Value *data, const Value *templ,
		const Value *next_largest, int approx)
{
	int ans;

	g_return_val_if_fail (data != NULL, 0);
	g_return_val_if_fail (templ != NULL, 0);

	switch (templ->type){
	case VALUE_EMPTY:
	case VALUE_BOOLEAN:
	case VALUE_INTEGER:
	case VALUE_FLOAT:
	{
		float_t a, b;
		
		a = value_get_as_float (data);
		b = value_get_as_float (templ);
		
		if (a == b)
			return 1;
		
		else if (approx && a < b){
			if (!next_largest)
				return -1;
			else if (value_get_as_float (next_largest) <= a)
				return -1;
		}
		return 0;
		break;
	}
	case VALUE_STRING:
	case VALUE_ERROR:
	default:
	{
		char *a, *b;

		a = value_get_as_string (data);
		b = value_get_as_string (templ);

		if (approx){
			ans = strcasecmp (a,b);
			if (approx && ans < 0){
				if (next_largest){
					char *c = value_get_as_string
					        (next_largest);
					int cmp = strcasecmp (a,c);
					g_free (c);
					if (cmp >= 0) {
						g_free (a);
						g_free (b);
						return -1;
					}
				} else {
					g_free (a);
					g_free (b);
					return -1;
				}
			}
		}
		else
			ans = strcmp (a, b);
		g_free (a);
		g_free (b);
		return (ans == 0);
		break;
	}
	}
	return 0;
}

static Value *
gnumeric_vlookup (FunctionEvalInfo *ei, Value **args)
{
	const Value *next_largest = NULL;
	int height, lp, approx, col_idx, next_largest_row = 0;
	
	height = value_area_get_height (ei->pos, args[1]);
	col_idx = value_get_as_int (args[2]);

	if (col_idx <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	if (col_idx > value_area_get_width (ei->pos, args [1]))
		return value_new_error (ei->pos, gnumeric_err_REF);

	if (args [3]){
		gboolean err;
		approx = value_get_as_bool (args [3], &err);
		if (err)
			return value_new_error (ei->pos, gnumeric_err_VALUE);
	} else
		approx = 1;

	for (lp = 0; lp < height; lp++){
		int compare;
		const Value *v;

		v = value_area_fetch_x_y (ei->pos, args[1], 0, lp);

		g_return_val_if_fail (v != NULL, NULL);

		compare = lookup_similar (v, args[0], next_largest, approx);

		if (compare == 1){
			const Value *v;

			v = value_area_fetch_x_y (ei->pos, args [1],
						  col_idx-1, lp);
			g_return_val_if_fail (v != NULL, NULL);

			return value_duplicate (v);
		}
		if (compare < 0){
			next_largest = v;
			next_largest_row = lp;
		}
	}
	if (approx && next_largest){
	        const Value *v;

		v = value_area_fetch_x_y (ei->pos, args [1], col_idx-1,
					   next_largest_row);
		g_return_val_if_fail (v != NULL, NULL);
		return value_duplicate (v);
	}
	else
		return value_new_error (ei->pos, gnumeric_err_NA);

	return NULL;
}

/***************************************************************************/

static char *help_hlookup = {
	N_("@FUNCTION=HLOOKUP\n"
	   "@SYNTAX=HLOOKUP(value,range,row,[approximate])\n"

	   "@DESCRIPTION="
	   "HLOOKUP function finds the col in range that has a first "
	   "row cell similar to value.  If @approximate is not true it finds "
	   "the col with an exact equivilance.  If @approximate is true, "
	   "then the values must be sorted in order of ascending value for "
	   "correct function; in this case it finds the col with value less "
	   "than @value it returns the value in the col found at a 1 based "
	   "offset in @row rows into the @range."
	   "\n"
	   "Returns #NUM! if @row < 0. "
	   "Returns #REF! if @row falls outside @range."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=VLOOKUP")
};

static Value *
gnumeric_hlookup (FunctionEvalInfo *ei, Value **args) 
{
	const Value *next_largest = NULL;
	int height, lp, approx, row_idx, next_largest_col = 0;
	
	row_idx = value_get_as_int (args [2]);
	height  = value_area_get_width (ei->pos, args [1]);

	if (row_idx <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	if (row_idx > value_area_get_height (ei->pos, args [1]))
		return value_new_error (ei->pos, gnumeric_err_REF);

	if (args [3]){
		gboolean err;
		approx = value_get_as_bool (args [3], &err);
		if (err)
			return value_new_error (ei->pos, gnumeric_err_VALUE);
	} else
		approx = 1;

	for (lp = 0; lp < height; lp++){
		int compare;
		const Value *v;

		v = value_area_fetch_x_y (ei->pos, args[1],lp, 0);

		g_return_val_if_fail (v != NULL, NULL);

		compare = lookup_similar (v, args[0], next_largest, approx);

		if (compare == 1){
			const Value *v;

			v = value_area_fetch_x_y (ei->pos, args [1],
						  lp, row_idx-1);
			g_return_val_if_fail (v != NULL, NULL);

			return value_duplicate (v);
		}

		if (compare < 0){
			next_largest = v;
			next_largest_col = lp;
		}
	}
	if (approx && next_largest){
		const Value *v;

		v = value_area_fetch_x_y (ei->pos, args [1],
					   next_largest_col, row_idx-1);
		g_return_val_if_fail (v != NULL, NULL);

		return value_duplicate (v);
	}
	else
		return value_new_error (ei->pos, gnumeric_err_NA);

	return NULL;
}

/***************************************************************************/

static char *help_lookup = {
	N_("@FUNCTION=LOOKUP\n"
	   "@SYNTAX=LOOKUP(value,vector1,vector2)\n"

	   "@DESCRIPTION="
	   "The LOOKUP function finds the row index of 'value' in @vector1 "
	   "and returns the contents of value2 at that row index. "
	   "If the area is longer than it is wide then the sense of the "
	   "search is rotated. Alternatively a single array can be used."
	   "\n"
	   "If LOOKUP can't find @value it uses the next largest value less "
	   "than value. "
	   "The data must be sorted. "
	   "\n"
	   "If @value is smaller than the first value it returns #N/A"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=VLOOKUP,HLOOKUP")
};

/* Not very efficient ! */
static Value *
gnumeric_lookup (FunctionEvalInfo *ei, Value **args)
{
	int height, width;
	const Value *next_largest = NULL;
	int next_largest_x = 0;
	int next_largest_y = 0;
	
	height  = value_area_get_height (ei->pos, args[1]);
	width   = value_area_get_width  (ei->pos, args[1]);

	if ((args[1]->type == VALUE_ARRAY)) {
		if (args[2])
			return value_new_error (ei->pos, _("Type Mismatch"));

	} else if (args[1]->type == VALUE_CELLRANGE) {
		if (!args[2])
			return value_new_error (ei->pos,
			  _("Invalid number of arguments"));

	} else
		return value_new_error (ei->pos, _("Type Mismatch"));
	
	{
		Value *src, *dest;
		int    x_offset=0, y_offset=0, lpx, lpy, maxx, maxy;
		int    tmp, compare, touched;

		if (args[1]->type == VALUE_ARRAY) {
			src = dest = args[1];
			if (width>height)
				y_offset = 1;
			else
				x_offset = 1;
		} else {
			src = args[1];
			dest = args[2];
		}
		maxy  = value_area_get_height (ei->pos, src);
		maxx  = value_area_get_width  (ei->pos, src);
		if ((tmp=value_area_get_height (ei->pos, dest))<maxy)
			maxy=tmp;
		if ((tmp=value_area_get_width (ei->pos, dest))<maxx)
			maxx=tmp;

		touched = 0;
		for (lpx=0,lpy=0;lpx<maxx && lpy<maxy;) {
			const Value *v = value_area_fetch_x_y
			  (ei->pos, src, lpx, lpy);
			compare = lookup_similar (v, args[0], next_largest, 1);
			if (compare == 1)
				return value_duplicate
				  (value_duplicate(value_area_fetch_x_y
						   (ei->pos, dest,
						    lpx + x_offset,
						    lpy + y_offset)));
			if (compare < 0) {
				next_largest = v;
				next_largest_x = lpx;
				next_largest_y = lpy;
			} else
				break;

			if (width>height)
				lpx++;
			else
				lpy++;
		}

		if (!next_largest)
			return value_new_error (ei->pos, gnumeric_err_NA);

		return value_duplicate (value_area_fetch_x_y
					(ei->pos, dest,
					 next_largest_x+x_offset,
					 next_largest_y+y_offset));
	}
}

static char *help_match = {
	N_("@FUNCTION=MATCH\n"
	   "@SYNTAX=MATCH(seek,vector1[,type])\n"

	   "@DESCRIPTION="
	   "The MATCH function finds the row index of 'value' in @vector1 "
	   "and returns it. "
	   "If the area is longer than it is wide then the sense of the "
	   "search is rotated. Alternatively a single array can be used. "
	   "if type = 1,  finds largest value <= seek,"
	   "if type = 0,  finds first value == seek,"
	   "if type = -1, finds smallest value >= seek,"
	   "\n"
	   "If LOOKUP can't find @value it uses the next largest value less "
	   "than value. "
	   "The data must be sorted. "
	   "\n"
	   "If @value is smaller than the first value it returns #N/A"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=LOOKUP")
};

/*
 * Not very convinced this is accurate.
 */
static Value *
gnumeric_match (FunctionEvalInfo *ei, Value **args)
{
	int height, width;
	const Value *next_largest = NULL;
	int next_largest_x = 0;
	int next_largest_y = 0;
	int type;
	
	height  = value_area_get_height (ei->pos, args[1]);
	width   = value_area_get_width  (ei->pos, args[1]);

	if (height != 1 && width != 1)
		return value_new_error (ei->pos, gnumeric_err_NA);

	if (args[2])
		type = value_get_as_int (args[2]);
	else
		type = 1;

	if (type != 0) {
		g_warning ("function : Match() : match_type %d is not supported, only 0 is supported.", type);
		return value_new_error (ei->pos, gnumeric_err_NA);
	}

	if ((args[1]->type == VALUE_ARRAY)) {
		if (args[2])
			return value_new_error (ei->pos, _("Type Mismatch"));

	} else if (args[1]->type == VALUE_CELLRANGE) {
		if (!args[2])
			return value_new_error (ei->pos, _("Invalid number of arguments"));

	} else
		return value_new_error (ei->pos, _("Type Mismatch"));
	
	{
		int    x_offset=0, y_offset=0, lpx, lpy, maxx, maxy;
		int    tmp, compare, touched;

		if (args[1]->type == VALUE_ARRAY) {
			if (width > height)
				y_offset = 1;
			else
				x_offset = 1;
		}
		maxy  = value_area_get_height (ei->pos, args[1]);
		maxx  = value_area_get_width  (ei->pos, args[1]);

		if ((tmp = value_area_get_height (ei->pos, args[1])) < maxy)
			maxy = tmp;
		if ((tmp = value_area_get_width (ei->pos, args[1])) < maxx)
			maxx = tmp;

		touched = 0;
		for (lpx = 0, lpy = 0;lpx < maxx && lpy < maxy;) {
			const Value *v = value_area_fetch_x_y (ei->pos, args[1], lpx, lpy);
			compare = lookup_similar (v, args[0], next_largest, 1);
			if (compare == 1) {
				/* type = 0 : Find the first value exactly equal to
				 * the target value.  No order is assumed for the target_range.
				 */
				if (type == 0) {
					if (width > height)
						return value_new_int (lpx + 1);
					else
						return value_new_int (lpy + 1);
				}
			}
			if (compare < 0) {
				next_largest = v;
				next_largest_x = lpx;
				next_largest_y = lpy;
			} else 
				break;

			if (width > height)
				lpx++;
			else
				lpy++;
		}

		if (!next_largest && type != -1)
			return value_new_error (ei->pos, gnumeric_err_NA);

		if (width > height)
			return value_new_int (lpx + 1);
		else
			return value_new_int (lpy + 1);

	}
}

/***************************************************************************/

static char *help_indirect = {
	N_("@FUNCTION=INDIRECT\n"
	   "@SYNTAX=INDIRECT(ref_text, [format])\n"

	   "@DESCRIPTION="
	   "INDIRECT function returns the contents of the cell pointed to "
	   "by the ref_text string. The string specifices a single cell "
	   "reference the format of which is either A1 or R1C1 style. The "
	   "style is set by the format boolean, which defaults to the former."
	   "\n"
	   "If ref_text is not a valid reference returns #REF! "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_indirect (FunctionEvalInfo *ei, Value **args) 
{
	char*          text;
	gboolean       a1_style;
	Cell          *dest_cell, *calling_cell;
	CellRef        ref;
	int            col, row;
	gboolean       error = FALSE;

	text = value_get_as_string (args[0]);
	if (args[1])
		a1_style = value_get_as_bool (args[1], &error);
	else
		a1_style = TRUE;

	if (error) {
		g_free (text);
		return value_new_error (ei->pos, gnumeric_err_REF);
	}

	if (a1_style)
		error = !cellref_a1_get (&ref, text, ei->pos->eval.col,
					 ei->pos->eval.row);
	else
		error = !cellref_r1c1_get (&ref, text, ei->pos->eval.col,
					   ei->pos->eval.row);
	g_free (text);

	if (error)
		return value_new_error (ei->pos, gnumeric_err_REF);

	cell_get_abs_col_row (&ref, &ei->pos->eval, &col, &row);
	dest_cell = sheet_cell_get (ei->pos->sheet, col, row);

	calling_cell = sheet_cell_get (ei->pos->sheet,
				       ei->pos->eval.col, ei->pos->eval.row);

	/* A dependency on the indirection cell if we do not already depend on it */
	cell_add_explicit_dependency (calling_cell, &ref);

	if (!dest_cell)
		return value_new_int (0);
	else
		return value_duplicate (dest_cell->value);
}


/*
 * FIXME: The concept of multiple range references needs core support.
 *        hence this whole implementation is a cop-out really.
 */
static char *help_index = {
	N_("@FUNCTION=INDEX\n"
	   "@SYNTAX=INDEX(reference, [row, col, area])\n"

	   "@DESCRIPTION="
	   "The INDEX function returns a reference to the cell at a offset "
	   "into the reference specified by row, col."
	   "\n"
	   "If things go wrong returns #REF! "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_index (FunctionEvalInfo *ei, Value **args) 
{
	Value *area = args[0];
	int    col_off = 0, row_off = 0;
	
	if (args[3] &&
	    value_get_as_int (args[3]) != 1) {
		g_warning ("Multiple range references unimplemented");
		return value_new_error (ei->pos, gnumeric_err_REF);
	}

	if (args[1])
		row_off = value_get_as_int (args[1]) - 1;

	if (args[2])
		col_off = value_get_as_int (args[2]) - 1;

	if (col_off < 0 ||
	    col_off >= value_area_get_width (ei->pos, area))
		return value_new_error (ei->pos, gnumeric_err_NUM);

	if (row_off < 0 ||
	    row_off >= value_area_get_height (ei->pos, area))
		return value_new_error (ei->pos, gnumeric_err_REF);

	return value_duplicate (value_area_fetch_x_y (ei->pos, area, col_off, row_off));
}

/***************************************************************************/

static char *help_column = {
	N_("@FUNCTION=COLUMN\n"
	   "@SYNTAX=COLUMN([reference])\n"

	   "@DESCRIPTION="
	   "The COLUMN function returns an array of the column numbers "
	   "taking a default argument of the containing cell position."
	   "\n"
	   "If @reference is neither an array nor a reference nor a range "
	   "returns #VALUE!."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=COLUMNS,ROW,ROWS")
};

static Value *
gnumeric_column (FunctionEvalInfo *ei, GList *nodes)
{
	ExprTree *expr;

	if (!nodes || !nodes->data)
		return value_new_int (ei->pos->eval.col+1);

	expr = (ExprTree *)nodes->data;

	if (expr->oper == OPER_VAR)
		return value_new_int (cell_ref_get_abs_col (&expr->u.ref,
							    ei->pos) + 1);
	if (expr->oper == OPER_CONSTANT &&
	    expr->u.constant->type == VALUE_CELLRANGE)
	{
		int i, j, col;
		Value const * range = expr->u.constant;
		CellRef const * a = &range->v.cell_range.cell_a;
		CellRef const * b = &range->v.cell_range.cell_b;
		Value * res = value_new_array (b->col - a->col + 1,
					       b->row - a->row + 1);

		col = cell_ref_get_abs_col (a, ei->pos) + 1;
		for (i = b->col - a->col ; i >= 0 ; --i)
			for (j = b->row - a->row ; j >= 0 ; --j)
				value_array_set(res, i, j,
						value_new_int(col+i));

		return res;
	}

	return value_new_error (ei->pos, gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_columns = {
	N_("@FUNCTION=COLUMNS\n"
	   "@SYNTAX=COLUMNS(reference)\n"

	   "@DESCRIPTION="
	   "The COLUMNS function returns the number of columns in area or "
	   "array reference."
	   "\n"
	   "If @reference is neither an array nor a reference nor a range "
	   "returns #VALUE!."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=COLUMN,ROW,ROWS")
};

/* FIXME: Needs Array support to be even slightly meaningful */
static Value *
gnumeric_columns (FunctionEvalInfo *ei, Value **args)
{
	return value_new_int (value_area_get_width (ei->pos, args [0]));
}

/***************************************************************************/

static char *help_offset = {
	N_("@FUNCTION=OFFSET\n"
	   "@SYNTAX=OFFSET(range,row,col,height,width)\n"

	   "@DESCRIPTION="
	   "The OFFSET function returns a cell range. "
	   "The cell range starts at offset (@col,@row) from @range, "
	   "and is of height @height and width @width."
	   "\n"
	   "If range is neither a reference nor a range returns #VALUE!.  "
	   "If either height or width is omitted the height or width "
	   "of the reference is used."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=COLUMN,COLUMNS,ROWS")
};

static Value *
gnumeric_offset (FunctionEvalInfo *ei, Value **args)
{
	CellRef a;
	CellRef b;
	int width, height;

	g_return_val_if_fail (args [0]->type == VALUE_CELLRANGE, NULL);

	cell_ref_make_abs (&a, &args[0]->v.cell_range.cell_a, ei->pos);
	cell_ref_make_abs (&b, &args[0]->v.cell_range.cell_b, ei->pos);

	a.row += value_get_as_int (args[1]);
	a.col += value_get_as_int (args[2]);

	width = (args[3] != NULL)
	    ? value_get_as_int (args[3])
	    : value_area_get_width (ei->pos, args [0]);
	height = (args[4] != NULL)
	    ? value_get_as_int (args[4])
	    : value_area_get_height (ei->pos, args [0]);

	if (width < 1 || height < 1)
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	else if (a.row < 0 || a.col < 0)
		return value_new_error (ei->pos, gnumeric_err_REF);

	/* Special case of a single cell */
	if (width == 1 && height == 1)
	{
		/* FIXME FIXME : do we need to check for recalc here ?? */
		Cell const * c =
		    sheet_cell_fetch (eval_sheet (a.sheet, ei->pos->sheet),
				      a.col, a.row);
		return value_duplicate (c->value);
	}

	b.row += width-1;
	b.col += height-1;
	return value_new_cellrange (&a, &b, ei->pos->eval.col, ei->pos->eval.row);
}

/***************************************************************************/

static char *help_row = {
	N_("@FUNCTION=ROW\n"
	   "@SYNTAX=ROW([reference])\n"

	   "@DESCRIPTION="
	   "The ROW function returns an array of the row numbers taking "
	   "a default argument of the containing cell position."
	   "\n"
	   "If @reference is neither an array nor a reference nor a range "
	   "returns #VALUE!."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=COLUMN,COLUMNS,ROWS")
};

static Value *
gnumeric_row (FunctionEvalInfo *ei, GList *nodes)
{
	ExprTree *expr;

	if (!nodes || !nodes->data)
		return value_new_int (ei->pos->eval.row+1);

	expr = (ExprTree *)nodes->data;

	if (expr->oper == OPER_VAR)
		return value_new_int (cell_ref_get_abs_row (&expr->u.ref,
							    ei->pos) + 1);
	if (expr->oper == OPER_CONSTANT &&
	    expr->u.constant->type == VALUE_CELLRANGE)
	{
		int i, j, row;
		Value const * range = expr->u.constant;
		CellRef const * a = &range->v.cell_range.cell_a;
		CellRef const * b = &range->v.cell_range.cell_b;
		Value * res = value_new_array (b->col - a->col + 1,
					       b->row - a->row + 1);

		row = cell_ref_get_abs_row (a, ei->pos) + 1;
		for (i = b->col - a->col ; i >= 0 ; --i)
			for (j = b->row - a->row ; j >= 0 ; --j)
				value_array_set(res, i, j,
						value_new_int(row+j));

		return res;
	}

	return value_new_error (ei->pos, gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_rows = {
	N_("@FUNCTION=ROWS\n"
	   "@SYNTAX=ROWS(reference)\n"

	   "@DESCRIPTION="
	   "The ROWS function returns the number of rows in area or array "
	   "reference."
	   "\n"
	   "If @reference is neither an array nor a reference nor a range "
	   "returns #VALUE!."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=COLUMN,ROW,ROWS")
};

/* FIXME: Needs Array support to be enven slightly meaningful */
static Value *
gnumeric_rows (FunctionEvalInfo *ei, Value **args)
{
	return value_new_int (value_area_get_height (ei->pos, args [0]));
}

/***************************************************************************/

static char *help_hyperlink = {
	N_("@FUNCTION=HYPERLINK\n"
	   "@SYNTAX=HYPERLINK(reference)\n"

	   "@DESCRIPTION="
	   "The HYPERLINK function currently returns its 2nd argument, "
	   "or if that is omitted the 1st argument."
	   "\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_hyperlink (FunctionEvalInfo *ei, Value **args)
{
	Value const * v = args[1];
	if (v == NULL)
		v = args[0];
	return value_duplicate (v);
}

/***************************************************************************/

static char *help_transpose = {
	N_("@FUNCTION=TRANSPOSE\n"
	   "@SYNTAX=TRANSPOSE(matrix)\n"

	   "@DESCRIPTION="
	   "TRANSPOSE function returns the transpose of the input "
	   "@matrix."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=MMULT")
};


static Value *
gnumeric_transpose (FunctionEvalInfo *ei, Value **argv)
{
	EvalPosition const * const ep = ei->pos;
        Value const * const matrix = argv[0];
	int	r, c;
        Value *res;

	int const cols = value_area_get_width (ep, matrix);
	int const rows = value_area_get_height (ep, matrix);

	/* Return the value directly for a singleton */
	if (rows == 1 && cols == 1)
		return value_duplicate(value_area_get_x_y (ep, matrix, 0, 0));

	res = g_new (Value, 1);
	res->type = VALUE_ARRAY;
	res->v.array.x = rows;
	res->v.array.y = cols;
	res->v.array.vals = g_new (Value **, rows);

	for (r = 0; r < rows; ++r){
		res->v.array.vals [r] = g_new (Value *, cols);
		for (c = 0; c < cols; ++c)
			res->v.array.vals[r][c] = 
			    value_duplicate(value_area_get_x_y (ep, matrix,
								c, r));
	}

	return res;
}

/***************************************************************************/

void
lookup_functions_init (void)
{
	FunctionCategory *cat = function_get_category (_("Data / Lookup"));

	function_add_args  (cat, "address",   "ff|ffs",
			    "row_num,col_num,abs_num,a1,text",
			    &help_address,  gnumeric_address);
        function_add_nodes (cat, "choose",     0,     "index,value...",
			    &help_choose,   gnumeric_choose);
	function_add_nodes (cat, "column",    "?",    "ref",
			    &help_column,   gnumeric_column);
	function_add_args  (cat, "columns",   "A",    "ref",
			    &help_columns, gnumeric_columns);
	function_add_args  (cat, "hlookup",
			    "?Af|b","val,range,col_idx,approx",
			    &help_hlookup, gnumeric_hlookup);
	function_add_args  (cat, "hyperlink",
			    "s|?","link_location, contents",
			    &help_hyperlink, gnumeric_hyperlink);
	function_add_args  (cat, "indirect",  "s|b","ref_string,format",
			    &help_indirect, gnumeric_indirect);
	function_add_args  (cat, "index",     "A|fff","reference,row,col,area",
			    &help_index,    gnumeric_index);
	function_add_args  (cat, "lookup",    "?A|r", "val,range,range",
			    &help_lookup,   gnumeric_lookup);
	function_add_args  (cat, "match",     "?A|f", "val,range,approx",
			    &help_match,    gnumeric_match);
	function_add_args  (cat, "offset",    "rff|ff","ref,row,col,hight,width",
			    &help_offset,   gnumeric_offset);
	function_add_nodes (cat, "row",       "?",    "ref",
			    &help_row,      gnumeric_row);
	function_add_args  (cat, "rows",      "A",    "ref",
			    &help_rows,    gnumeric_rows);
	function_add_args  (cat, "transpose","A",
			    "array",
			    &help_transpose,   gnumeric_transpose);
	function_add_args  (cat, "vlookup",
			    "?Af|b","val,range,col_idx,approx",
			    &help_vlookup, gnumeric_vlookup);
}
