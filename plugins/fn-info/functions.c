/*
 * fn-information.c:  Information built-in functions
 *
 * Authors:
 *  Jukka-Pekka Iivonen (iivonen@iki.fi)
 *  Jody Goldberg (jgoldberg@home.com)
 *  Morten Welinder (terra@diku.dk)
 */
#include <config.h>
#include "gnumeric.h"
#include "utils.h"
#include "func.h"
#include "number-match.h"
#include <sys/utsname.h>


enum Value_Class {
	VALUE_CLASS_NUMBER = 1,
	VALUE_CLASS_TEXT = 2,
	VALUE_CLASS_BOOL = 4,
	VALUE_CLASS_FORMULA = 8,
	VALUE_CLASS_ERROR = 16,
	VALUE_CLASS_ARRAY = 64,
	VALUE_CLASS_BOGUS = -1,
};


static enum Value_Class
get_value_class (ExprTree *expr, Sheet *sheet, int col, int row)
{
	char *error_string;
	Value *value;
	enum Value_Class res;

	value = eval_expr (sheet, expr, col, row, &error_string);
	if (value) {
		switch (value->type) {
		case VALUE_INTEGER:
		case VALUE_FLOAT:
			res = VALUE_CLASS_NUMBER;
			break;
		case VALUE_STRING:
			res = VALUE_CLASS_TEXT;
			break;
		case VALUE_ARRAY:
			res = VALUE_CLASS_ARRAY;
			break;
		default:
			res = VALUE_CLASS_BOGUS;
			break;
		}
	} else
		res = VALUE_CLASS_ERROR;

	return res;
}


static char *help_cell = {
	N_("@FUNCTION=CELL\n"
	   "@SYNTAX=CELL()\n"

	   "@DESCRIPTION="
	   "CELL Returns information about the formatting, location, or contents of a cell. "
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_cell (struct FunctionDefinition *n,
	       Value *argv [], char **error_string)
{
	char * info_type = argv [0]->v.str->str;

	if (!strcasecmp(info_type, "address")) {
		/* Reference of the first cell in reference, as text. */
		*error_string = _("Unimplemented");
		return NULL;
	} else if (!strcasecmp (info_type, "col")) {
		/* Column number of the cell in reference. */
		*error_string = _("Unimplemented");
		return NULL;
	} else if (!strcasecmp (info_type, "color")) {
		/* 1 if the cell is formatted in color for negative values;
		 * otherwise returns 0 (zero).
		 */
		*error_string = _("Unimplemented");
		return NULL;
	} else if (!strcasecmp (info_type, "contents")) {
		/* Contents of the upper-left cell in reference. */
		*error_string = _("Unimplemented");
		return NULL;
	} else if (!strcasecmp (info_type, "filename"))	{
		/* Filename (including full path) of the file that contains
		 * reference, as text. Returns empty text ("") if the worksheet
		 * that contains reference has not yet been saved.
		 */
		*error_string = _("Unimplemented");
		return NULL;
	} else if (!strcasecmp (info_type, "format")) {
		/* Text value corresponding to the number format of the cell.
		 * The text values for the various formats are shown in the
		 * following table. Returns "-" at the end of the text value if
		 * the cell is formatted in color for negative values. Returns
		 * "()" at the end of the text value if the cell is formatted
		 * with parentheses for positive or all values.  "parentheses"
		 * 1 if the cell is formatted with parentheses for positive or
		 * all values; otherwise returns 0.
		 */
		*error_string = _("Unimplemented");
		return NULL;
	} else if (!strcasecmp (info_type, "prefix")) {
		/* Text value corresponding to the "label prefix" of the cell.
		 * Returns single quotation mark (') if the cell contains
		 * left-aligned text, double quotation mark (") if the cell
		 * contains right-aligned text, caret (^) if the cell contains
		 * centered text, backslash (\) if the cell contains
		 * fill-aligned text, and empty text ("") if the cell contains
		 * anything else.  
		 */
		*error_string = _("Unimplemented");
		return NULL;
	} else if (!strcasecmp (info_type, "protect")) {
		/* 0 if the cell is not locked, and 1 if the cell is locked. */
		*error_string = _("Unimplemented");
		return NULL;
	} else if (!strcasecmp (info_type, "row")) {
		/* Row number of the cell in reference. */
		*error_string = _("Unimplemented");
		return NULL;
	} else if (!strcasecmp (info_type, "type")) {
		/* Text value corresponding to the type of data in the cell.
		 * Returns "b" for blank if the cell is empty, "l" for label if
		 * the cell contains a text constant, and "v" for value if the
		 * cell contains anything else.
		 */
		*error_string = _("Unimplemented");
		return NULL;
	} else if (!strcasecmp (info_type, "width")) {
		/* Column width of the cell rounded off to an integer. Each
		 * unit of column width is equal to the width of one character
		 * in the default font size.
		 */
		*error_string = _("Unimplemented");
		return NULL;
	}

	*error_string = _("Unknown info_type");
	return NULL;
}


static char *help_countblank = {
        N_("@FUNCTION=COUNTBLANK\n"
           "@SYNTAX=COUNTBLANK(range)\n"

           "@DESCRIPTION="
           "COUNTBLANK returns the number of blank cells in a range. "
           "\n"
           "@SEEALSO=COUNT")
};

static Value *
gnumeric_countblank (struct FunctionDefinition *n,
		     Value *argv [], char **error_string)
{
        Sheet *sheet;
        Value *range;
	int   col_a, col_b, row_a, row_b;
	int   i, j;
	int   count;

	range = argv[0];
	sheet = range->v.cell_range.cell_a.sheet;
	col_a = range->v.cell_range.cell_a.col;
	col_b = range->v.cell_range.cell_b.col;
	row_a = range->v.cell_range.cell_a.row;
	row_b = range->v.cell_range.cell_b.row;
	count = 0;

	for (i=col_a; i<=col_b; i++)
	        for (j=row_a; j<=row_b; j++) {
		        Cell *cell = sheet_cell_get(sheet, i, j);

		        if (cell == NULL || cell->value == NULL)
			        count++;
		}

	return value_new_int (count);
}



static char *help_info = {
	N_("@FUNCTION=INFO\n"
	   "@SYNTAX=INFO()\n"

	   "@DESCRIPTION="
	   "INFO Returns information about the current operating environment. "
	   "\n"
	   "@SEEALSO=")
};


static Value *
gnumeric_info (struct FunctionDefinition *n,
	       Value *argv [], char **error_string)
{
	char const * const info_type = argv [0]->v.str->str;
	if (!strcasecmp (info_type, "directory")) {
		/* Path of the current directory or folder.  */
		*error_string = _("Unimplemented");
		return NULL;
	} else if (!strcasecmp (info_type, "memavail")) {
		/* Amount of memory available, in bytes.  */
		return value_new_int (15 << 20);  /* Good enough... */
	} else if (!strcasecmp (info_type, "memused")) {
		/* Amount of memory being used for data.  */
		return value_new_int (1 << 20);  /* Good enough... */
	} else if (!strcasecmp (info_type, "numfile")) {
		/* Number of active worksheets.  */
		return value_new_int (1);  /* Good enough... */
	} else if (!strcasecmp (info_type, "origin")) {
		/* Absolute A1-style reference, as text, prepended with "$A:" for
		 * Lotus 1-2-3 release 3.x compatibility. Returns the cell
		 * reference of the top and leftmost cell visible in the window,
		 * based on the current scrolling position.
		 */
		*error_string = _("Unimplemented");
		return NULL;
	} else if (!strcasecmp (info_type, "osversion")) {
		/* Current operating system version, as text.  */
		struct utsname unamedata;

		if (uname (&unamedata) == -1) {
			*error_string = _("Unknown version");
			return NULL;
		} else {
			char *tmp = g_strdup_printf (_("%s version %s"),
						     unamedata.sysname,
						     unamedata.release);
			Value *res = value_new_string (tmp);
			g_free (tmp);
			return res;
		}
	} else if (!strcasecmp (info_type, "recalc")) {
		/* Current recalculation mode; returns "Automatic" or "Manual".  */
		return value_new_string (_("Automatic"));
	} else if (!strcasecmp (info_type, "release")) {
		/* Version of Gnumeric (Well, Microsoft Excel), as text.  */
		return value_new_string (GNUMERIC_VERSION);
	} else if (!strcasecmp (info_type, "system")) {
		/* Name of the operating environment.  */
		struct utsname unamedata;

		if (uname (&unamedata) == -1) {
			*error_string = _("Unknown system");
			return NULL;
		} else
			return value_new_string (unamedata.sysname);
	} else if (!strcasecmp (info_type, "totmem")) {
		/* Total memory available, including memory already in use, in
		 * bytes.
		 */
		return value_new_int (16 << 20);  /* Good enough... */
	}

	*error_string = _("Unknown info_type");
	return NULL;
}


static char *help_isblank = {
	N_("@FUNCTION=ISBLANK\n"
	   "@SYNTAX=ISBLANK()\n"

	   "@DESCRIPTION="
	   "ISBLANK Returns TRUE if the value is blank. "
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_isblank (struct FunctionDefinition *n,
		  Value *argv [], char **error_string)
{
	int result = 0;
	/* TODO TODO TODO
	 * Fill in the blank
	 */
	return value_new_bool (result);
}


static char *help_iseven = {
	N_("@FUNCTION=ISEVEN\n"
	   "@SYNTAX=ISEVEN()\n"

	   "@DESCRIPTION="
	   "ISEVEN Returns TRUE if the number is even. "
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_iseven (struct FunctionDefinition *n,
		 Value *argv [], char **error_string)
{
	return value_new_bool (!(value_get_as_int (argv[0]) & 1));
}


static char *help_islogical = {
	N_("@FUNCTION=ISLOGICAL\n"
	   "@SYNTAX=ISLOGICAL()\n"

	   "@DESCRIPTION="
	   "ISLOGICAL Returns TRUE if the value is a logical value. "
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_islogical (Sheet *sheet, GList *expr_node_list,
		    int eval_col, int eval_row, char **error_string)
{
	enum Value_Class cl;

	if (g_list_length (expr_node_list) != 1){
		*error_string = _("Invalid number of arguments");
		return NULL;
	}

	cl = get_value_class (expr_node_list->data, sheet, eval_col, eval_row);

	/* FIXME -- this is a hack needed until we have a proper bool type.  */
	if (cl == VALUE_CLASS_NUMBER)
		cl = VALUE_CLASS_BOOL;
	return value_new_bool (cl == VALUE_CLASS_BOOL);
}


static char *help_isnontext = {
	N_("@FUNCTION=ISNONTEXT\n"
	   "@SYNTAX=ISNONTEXT()\n"

	   "@DESCRIPTION="
	   "ISNONTEXT Returns TRUE if the value is not text. "
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_isnontext (Sheet *sheet, GList *expr_node_list,
		    int eval_col, int eval_row, char **error_string)
{
	if (g_list_length (expr_node_list) != 1){
		*error_string = _("Invalid number of arguments");
		return NULL;
	}

	return value_new_bool (get_value_class (expr_node_list->data, sheet, eval_col, eval_row) != VALUE_CLASS_TEXT);
}


static char *help_isnumber = {
	N_("@FUNCTION=ISNUMBER\n"
	   "@SYNTAX=ISNUMBER()\n"

	   "@DESCRIPTION="
	   "ISNUMBER Returns TRUE if the value is a number. "
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_isnumber (Sheet *sheet, GList *expr_node_list,
		   int eval_col, int eval_row, char **error_string)
{
	if (g_list_length (expr_node_list) != 1){
		*error_string = _("Invalid number of arguments");
		return NULL;
	}

	return value_new_bool (get_value_class (expr_node_list->data, sheet, eval_col, eval_row) == VALUE_CLASS_NUMBER);
}


static char *help_isodd = {
	N_("@FUNCTION=ISODD\n"
	   "@SYNTAX=ISODD()\n"

	   "@DESCRIPTION="
	   "ISODD Returns TRUE if the number is odd. "
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_isodd (struct FunctionDefinition *n,
		Value *argv [], char **error_string)
{
	return value_new_bool (value_get_as_int (argv[0]) & 1);
}


static char *help_isref = {
	N_("@FUNCTION=ISREF\n"
	   "@SYNTAX=ISREF()\n"

	   "@DESCRIPTION="
	   "ISREF Returns TRUE if the value is a reference. "
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_isref (struct FunctionDefinition *n,
		Value *argv [], char **error_string)
{
	int result = 0;
	/* TODO TODO TODO
	 * Fill in the blank
	 */
	return value_new_bool (result);
}


static char *help_istext = {
	N_("@FUNCTION=ISTEXT\n"
	   "@SYNTAX=ISTEXT()\n"

	   "@DESCRIPTION="
	   "ISTEXT Returns TRUE if the value is text. "
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_istext (Sheet *sheet, GList *expr_node_list,
		 int eval_col, int eval_row, char **error_string)
{
	if (g_list_length (expr_node_list) != 1){
		*error_string = _("Invalid number of arguments");
		return NULL;
	}

	return value_new_bool (get_value_class (expr_node_list->data, sheet, eval_col, eval_row) == VALUE_CLASS_TEXT);
}


static char *help_n = {
	N_("@FUNCTION=N\n"
	   "@SYNTAX=N()\n"

	   "@DESCRIPTION="
	   "N Returns a value converted to a number. "
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_n (struct FunctionDefinition *n,
	    Value *argv [], char **error_string)
{
	const char *str;
	double v;
	char *format;

	if (VALUE_IS_NUMBER (argv[0]))
		return value_duplicate (argv[0]);

	if (argv[0]->type != VALUE_STRING) {
		*error_string = gnumeric_err_NUM;
		return NULL;
	}

	str = argv[0]->v.str->str;
	if (format_match (str, &v, &format))
		return value_new_float (v);
	else {
		*error_string = gnumeric_err_NUM;
		return NULL;
	}
}


static char *help_type = {
	N_("@FUNCTION=TYPE\n"
	   "@SYNTAX=TYPE()\n"

	   "@DESCRIPTION="
	   "TYPE Returns a number indicating the data type of a value. "
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_type (Sheet *sheet, GList *expr_node_list,
	       int eval_col, int eval_row, char **error_string)
{
	if (g_list_length (expr_node_list) != 1){
		*error_string = _("Invalid number of arguments");
		return NULL;
	}

	return value_new_int (get_value_class (expr_node_list->data, sheet, eval_col, eval_row));
}


FunctionDefinition information_functions [] = {
	{ "cell", "sr", "info_type, cell",	&help_cell,
	  NULL, gnumeric_cell},
        { "countblank", "r",  "range",		&help_countblank,
	  NULL, gnumeric_countblank },
	{ "info", "?", "info_type",		&help_info,
	  NULL, gnumeric_info},
	{ "isblank", "r", "value",		&help_isblank,
	  NULL, gnumeric_isblank},
	{ "iseven", "?", "value",		&help_iseven,
	  NULL, gnumeric_iseven},
	{ "islogical", NULL, "value",		&help_islogical,
	  gnumeric_islogical, NULL}, /* Handles args manually */
	{ "isnontext", NULL, "value",		&help_isnontext,
	  gnumeric_isnontext, NULL}, /* Handles args manually */
	{ "isnumber", NULL, "value",		&help_isnumber,
	  gnumeric_isnumber, NULL}, /* Handles args manually */
	{ "isodd", "?", "value",		&help_isodd,
	  NULL, gnumeric_isodd},
	{ "isref", "?", "value",		&help_isref,
	  NULL, gnumeric_isref},
	{ "istext", NULL, "value",		&help_istext,
	  gnumeric_istext, NULL }, /* Handles args manually */
	{ "n", "?", "value",			&help_n,
	  NULL, gnumeric_n},
	{ "type", NULL,   "value",              &help_type,
	  gnumeric_type, NULL }, /* Handles args manually */
        { NULL, NULL }
};
