/*
 * fn-lookup.c:  Built in lookup functions and functions registration
 *
 * Authors:
 *  Michael Meeks <michael@imaginator.com>
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 */

#include <config.h>
#include "numbers.h"
#include "utils.h"
#include "func.h"


static char *help_address = {
	N_("@FUNCTION=ADDRESS\n"
	   "@SYNTAX=ADDRESS(row_num,col_num[,abs_num,a1,text])\n"

	   "@DESCRIPTION="
	   "ADDRESS returns a cell address as text for specified row "
	   "and column numbers. "
	   "\n"
	   "If abs_num is 1 or omitted, ADDRESS returns absolute reference. "
	   "If abs_num is 2 ADDRESS returns absolute row and relative column. "
	   "If abs_num is 3 ADDRESS returns relative row and absolute column. "
	   "If abs_num is 4 ADDRESS returns relative reference. "
	   "If abs_num is greater than 4 ADDRESS returns #NUM! error. "
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
	   "@SEEALSO=")
};

static Value *
gnumeric_address (FunctionEvalInfo *ei, Value **args)
{
        int   row, col, abs_num, a1, err;
	gchar *text, *buf;
	Value *v;

	row = value_get_as_int (args[0]);
	col = value_get_as_int (args[1]);

	if (row < 1 || col < 1)
	        return function_error (ei, gnumeric_err_NUM);

	if (args[2] == NULL)
	        abs_num = 1;
	else
	        abs_num = value_get_as_int (args [2]);

	if (args[3] == NULL)
	        a1 = 1;
	else {
	        a1 = value_get_as_bool (args[3], &err);
		if (err)
		        return function_error (ei, gnumeric_err_VALUE);
	}

	if (args[4] == NULL) {
	        text = g_new(gchar, 1);
	        text[0] = '\0';
	} else {
	        gchar *p = args[4]->v.str->str;
		int   n=0, space=0;

		text = g_new(gchar, strlen(p) + 3);
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
		return function_error (ei, gnumeric_err_NUM);
	}
	v = value_new_string (buf);
	g_free(text);
	g_free(buf);

	return v;
}

static char *help_choose = {
	N_("@FUNCTION=CHOOSE\n"
	   "@SYNTAX=CHOOSE(index[,value1][,value2]...)\n"

	   "@DESCRIPTION="
	   "CHOOSE returns the value of index @index."
	   "index is rounded to an integer if it is not."
	   "\n"
	   "if index < 1 or index > number of values: returns #VAL!."
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
		return function_error (ei, _("#ARG!"));

	v = eval_expr (ei, l->data);
	if (!v)
		return NULL;

	if ((v->type != VALUE_INTEGER) && (v->type != VALUE_FLOAT)) {
		value_release (v);
		return function_error (ei, gnumeric_err_VALUE);
	}

	index = value_get_as_int(v);
	value_release (v);
	l = g_list_next (l);

	while (l){
		index--;
		if (!index)
			return eval_expr (ei, l->data);
		l = g_list_next (l);
	}
	return function_error (ei, gnumeric_err_VALUE);
}

static char *help_vlookup = {
	N_("@FUNCTION=VLOOKUP\n"
	   "@SYNTAX=VLOOKUP(value,range,column,[approximate])\n"

	   "@DESCRIPTION="
	   "The VLOOKUP function finds the row in range that has a first "
	   "column similar to value.  If approximate is not true it finds "
	   "the row with an exact equivilance.  If approximate is true, "
	   "then the values must be sorted in order of ascending value for "
	   "correct function; in this case it finds the row with value less "
	   "than value.  it returns the value in the row found at a 1 based "
	   "offset in column columns into the range."
	   "\n"
	   "Returns #NUM! if column < 0."
	   "Returns #REF! if column falls outside range."
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
	
	height = value_area_get_height (&ei->pos, args[1]);
	col_idx = value_get_as_int (args[2]);

	if (col_idx <= 0)
		return function_error (ei, gnumeric_err_NUM);

	if (col_idx > value_area_get_width (&ei->pos, args [1]))
		return function_error (ei, gnumeric_err_REF);

	if (args [3]){
		int err;

		approx = value_get_as_bool (args [3], &err);

		if (err)
			return function_error (ei, gnumeric_err_VALUE);
	} else
		approx = 1;

	for (lp = 0; lp < height; lp++){
		int compare;
		const Value *v;

		v = value_area_get_at_x_y (&ei->pos, args[1], 0, lp);

		g_return_val_if_fail (v != NULL, NULL);

		compare = lookup_similar (v, args[0], next_largest, approx);

		if (compare == 1){
			const Value *v;

			v = value_area_get_at_x_y (&ei->pos, args [1], col_idx-1, lp);
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

		v = value_area_get_at_x_y (&ei->pos, args [1], col_idx-1,
					   next_largest_row);
		g_return_val_if_fail (v != NULL, NULL);
		return value_duplicate (v);
	}
	else
		return function_error (ei, gnumeric_err_NA);

	return NULL;
}

static char *help_hlookup = {
	N_("@FUNCTION=HLOOKUP\n"
	   "@SYNTAX=HLOOKUP(value,range,row,[approximate])\n"

	   "@DESCRIPTION="
	   "The HLOOKUP function finds the col in range that has a first "
	   "row cell similar to value.  If approximate is not true it finds "
	   "the col with an exact equivilance.  If approximate is true, "
	   "then the values must be sorted in order of ascending value for "
	   "correct function; in this case it finds the col with value less "
	   "than value it returns the value in the col found at a 1 based "
	   "offset in row rows into the range."
	   "\n"
	   "Returns #NUM! if row < 0."
	   "Returns #REF! if row falls outside range."
	   "\n"
	   "@SEEALSO=VLOOKUP")
};

static Value *
gnumeric_hlookup (FunctionEvalInfo *ei, Value **args) 
{
	const Value *next_largest = NULL;
	int height, lp, approx, row_idx, next_largest_col = 0;
	
	row_idx = value_get_as_int (args [2]);
	height  = value_area_get_width (&ei->pos, args [1]);

	if (row_idx <= 0)
		return function_error (ei, gnumeric_err_NUM);

	if (row_idx > value_area_get_height (&ei->pos, args [1]))
		return function_error (ei, gnumeric_err_REF);

	if (args [3]){
		int err;
		approx = value_get_as_bool (args [3], &err);

		if (err)
			return function_error (ei, gnumeric_err_VALUE);
	} else
		approx = 1;

	for (lp = 0; lp < height; lp++){
		int compare;
		const Value *v;

		v = value_area_get_at_x_y (&ei->pos, args[1],lp, 0);

		g_return_val_if_fail (v != NULL, NULL);

		compare = lookup_similar (v, args[0], next_largest, approx);

		if (compare == 1){
			const Value *v;

			v = value_area_get_at_x_y (&ei->pos, args [1], lp, row_idx-1);
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

		v = value_area_get_at_x_y (&ei->pos, args [1],
					   next_largest_col, row_idx-1);
		g_return_val_if_fail (v != NULL, NULL);

		return value_duplicate (v);
	}
	else
		return function_error (ei, gnumeric_err_NA);

	return NULL;
}

static char *help_lookup = {
	N_("@FUNCTION=LOOKUP\n"
	   "@SYNTAX=LOOKUP(value,vector1,vector2)\n"

	   "@DESCRIPTION="
	   "The LOOKUP function finds the row index of 'value' in vector1 "
	   "and returns the contents of value2 at that row index. "
	   "If the area is longer than it is wide then the sense of the "
	   "search is rotated. Alternatively a single array can be used."
	   "\n"
	   "If LOOKUP can't find value it uses the next largest value less "
	   "than value. "
	   "The data must be sorted. "
	   "\n"
	   "If value is smaller than the first value it returns #N/A"
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
	
	height  = value_area_get_height (&ei->pos, args[1]);
	width   = value_area_get_width  (&ei->pos, args[1]);

	if ((args[1]->type == VALUE_ARRAY)) {
		if (args[2])
			return function_error (ei, _("Type Mismatch"));

	} else if (args[1]->type == VALUE_CELLRANGE) {
		if (!args[2])
			return function_error (ei, _("Invalid number of arguments"));

	} else
		return function_error (ei, _("Type Mismatch"));
	
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
		maxy  = value_area_get_height (&ei->pos, src);
		maxx  = value_area_get_width  (&ei->pos, dest);
		if ((tmp=value_area_get_height (&ei->pos, src))<maxy)
			maxy=tmp;
		if ((tmp=value_area_get_width (&ei->pos, src))<maxx)
			maxx=tmp;

		touched = 0;
		for (lpx=0,lpy=0;lpx<maxx && lpy<maxy;) {
			const Value *v = value_area_get_at_x_y (&ei->pos, src, lpx, lpy);
			compare = lookup_similar (v, args[0], next_largest, 1);
			if (compare == 1)
				return value_duplicate (value_duplicate (value_area_get_at_x_y (&ei->pos, dest, next_largest_x+x_offset,
													  next_largest_y+y_offset)));
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
			return function_error (ei, gnumeric_err_NA);

		return value_duplicate (value_area_get_at_x_y (&ei->pos, dest,
							       next_largest_x+x_offset,
							       next_largest_y+y_offset));
	}
}



static char *help_column = {
	N_("@FUNCTION=COLUMN\n"
	   "@SYNTAX=COLUMN([reference])\n"

	   "@DESCRIPTION="
	   "The COLUMN function returns an array of the column numbers "
	   "taking a default argument of the containing cell position."
	   "\n"
	   "If reference is neither an array nor a reference nor a range "
	   "returns #VALUE!."
	   "\n"
	   "@SEEALSO=COLUMNS,ROW,ROWS")
};

/* FIXME: Needs Array support to be enven slightly meaningful */
static Value *
gnumeric_column (FunctionEvalInfo *ei, GList *nodes)
{
	Value *v;

	if (!nodes || !nodes->data)
		return value_new_int (ei->pos.eval_col+1);

	v = eval_expr (ei, nodes->data);
	if (!v)
		return NULL;

	switch (v->type){
	case VALUE_CELLRANGE:
		value_release (v);
		return function_error (ei, _("Arrays not yet supported"));
	case VALUE_ARRAY:
		value_release (v);
		return function_error (ei, _("Unimplemented"));
	default:
		value_release (v);
		return function_error (ei, gnumeric_err_VALUE);
	}
}

static char *help_columns = {
	N_("@FUNCTION=COLUMNS\n"
	   "@SYNTAX=COLUMNS(reference)\n"

	   "@DESCRIPTION="
	   "The COLUMNS function returns the number of columns in area or "
	   "array reference."
	   "\n"
	   "If reference is neither an array nor a reference nor a range "
	   "returns #VALUE!."
	   "\n"
	   "@SEEALSO=COLUMN,ROW,ROWS")
};

/* FIXME: Needs Array support to be enven slightly meaningful */
static Value *
gnumeric_columns (FunctionEvalInfo *ei, Value **args)
{
	return value_new_int (value_area_get_width (&ei->pos, args [0]));
}

static char *help_offset = {
	N_("@FUNCTION=OFFSET\n"
	   "@SYNTAX=OFFSET(range,row,col,height,width)\n"

	   "@DESCRIPTION="
	   "The OFFSET function returns a cell range."
	   "The cell range starts at offset (col,row) from range, "
	   "and is of height @height and width @width."
	   "\n"
	   "If range is neither a reference nor a range returns #VALUE!."
	   "\n"
	   "@SEEALSO=COLUMN,COLUMNS,ROWS")
};

static Value *
gnumeric_offset (FunctionEvalInfo *ei, Value **args)
{
	CellRef a;
	CellRef b;
	int tw, th;

	g_return_val_if_fail (args [0]->type == VALUE_CELLRANGE, NULL);

	memcpy (&a, &args[0]->v.cell_range.cell_a, sizeof (CellRef));

	a.row += value_get_as_int (args[1]);
	a.col += value_get_as_int (args[2]);

	memcpy (&b, &a, sizeof(CellRef));

	tw = value_get_as_int (args[3]);
	th = value_get_as_int (args[4]);

	if (tw < 0 || th < 0)
		return function_error (ei, gnumeric_err_VALUE);
	else if (a.row < 0 || a.col < 0)
		return function_error (ei, gnumeric_err_REF);

	b.row += tw;
	b.col += th;
	return value_new_cellrange (&a, &b);
}

static char *help_row = {
	N_("@FUNCTION=ROW\n"
	   "@SYNTAX=ROW([reference])\n"

	   "@DESCRIPTION="
	   "The ROW function returns an array of the row numbers taking "
	   "a default argument of the containing cell position."
	   "\n"
	   "If reference is neither an array nor a reference nor a range "
	   "returns #VALUE!."
	   "\n"
	   "@SEEALSO=COLUMN,COLUMNS,ROWS")
};

/* FIXME: Needs Array support to be enven slightly meaningful */
static Value *
gnumeric_row (FunctionEvalInfo *ei, GList *nodes)
{
	Value *v;

	if (!nodes || !nodes->data)
		return value_new_int (ei->pos.eval_row+1);

	v = eval_expr (ei, nodes->data);
	if (!v)
		return NULL;

	switch (v->type){
	case VALUE_CELLRANGE:
		value_release (v);
		return function_error (ei, _("Arrays not yet supported"));

	case VALUE_ARRAY:
		value_release (v);
		return function_error (ei, _("Unimplemented"));
	default:
		value_release (v);
		return function_error (ei, gnumeric_err_VALUE);
	}
}

static char *help_rows = {
	N_("@FUNCTION=ROWS\n"
	   "@SYNTAX=ROWS(reference)\n"

	   "@DESCRIPTION="
	   "The ROWS function returns the number of rows in area or array "
	   "reference."
	   "\n"
	   "If reference is neither an array nor a reference nor a range "
	   "returns #VALUE!."
	   "\n"
	   "@SEEALSO=COLUMN,ROW,ROWS")
};

/* FIXME: Needs Array support to be enven slightly meaningful */
static Value *
gnumeric_rows (FunctionEvalInfo *ei, Value **args)
{
	return value_new_int (value_area_get_height (&ei->pos, args [0]));
}

void lookup_functions_init()
{
	FunctionCategory *cat = function_get_category (_("Data / Lookup"));

	function_add_args  (cat, "address",   "ff|ffs", "row_num,col_num,abs_num,a1,text",
			   &help_address,  gnumeric_address);
        function_add_nodes (cat, "choose",     0,     "index,value...",
			    &help_choose,  gnumeric_choose);
	function_add_nodes (cat, "column",    "?",    "ref",
			    &help_column,  gnumeric_column);
	function_add_args  (cat, "columns",   "A",    "ref",
			    &help_columns, gnumeric_columns);
	function_add_args  (cat, "hlookup",   "?Af|b","val,range,col_idx,approx",
			    &help_hlookup, gnumeric_hlookup);
	function_add_args  (cat, "lookup",    "?A|r", "val,range,range",
			    &help_lookup,  gnumeric_lookup);
	function_add_args  (cat, "offset",    "rffff","ref,row,col,hight,width",
			    &help_offset,  gnumeric_offset);
	function_add_nodes (cat, "row",       "?",    "ref",
			    &help_row,     gnumeric_row);
	function_add_args  (cat, "rows",      "A",    "ref",
			    &help_rows,    gnumeric_rows);
	function_add_args  (cat, "vlookup",   "?Af|b","val,range,col_idx,approx",
			    &help_vlookup, gnumeric_vlookup);
}
