/*
 * fn-information.c:  Information built-in functions
 *
 * Authors:
 *  Jukka-Pekka Iivonen (iivonen@iki.fi)
 *  Jody Goldberg (jgoldberg@home.com)
 *  Morten Welinder (terra@diku.dk)
 *  Almer S. Tigelaar (almer@gnome.org)
 */
#include <config.h>
#include "gnumeric.h"
#include "parse-util.h"
#include "func.h"
#include "cell.h"
#include "str.h"
#include "sheet.h"
#include "workbook.h"
#include "format.h"
#include "formats.h"
#include "style.h"
#include "sheet-style.h"
#include "number-match.h"
#include <sys/utsname.h>
#include <math.h>

enum Value_Class {
	VALUE_CLASS_NUMBER  = 1,
	VALUE_CLASS_TEXT    = 2,
	VALUE_CLASS_BOOL    = 4,
	VALUE_CLASS_FORMULA = 8,
	VALUE_CLASS_ERROR   = 16,
	VALUE_CLASS_ARRAY   = 64,
	VALUE_CLASS_BOGUS   = -1,
};


static enum Value_Class
get_value_class (FunctionEvalInfo *ei, ExprTree *expr)
{
	Value *value;
	enum Value_Class res;

	value = eval_expr (ei->pos, expr,
			   EVAL_PERMIT_NON_SCALAR|EVAL_PERMIT_EMPTY);
	if (value) {
		switch (value->type) {
		case VALUE_INTEGER:
		case VALUE_FLOAT:
			res = VALUE_CLASS_NUMBER;
			break;
		case VALUE_STRING:
			res = VALUE_CLASS_TEXT;
			break;
		case VALUE_BOOLEAN:
			res = VALUE_CLASS_BOOL;
			break;
		case VALUE_ERROR:
			res = VALUE_CLASS_ERROR;
			break;
		case VALUE_ARRAY:
			res = VALUE_CLASS_ARRAY;
			break;
		case VALUE_EMPTY:
		default:
			res = VALUE_CLASS_BOGUS;
			break;
		}
		value_release (value);
	} else
		res = VALUE_CLASS_ERROR;

	return res;
}

/***************************************************************************/

static char *help_cell = {
	N_("@FUNCTION=CELL\n"
	   "@SYNTAX=CELL(ref)\n"

	   "@DESCRIPTION="
	   "CELL returns information about the formatting, location, or "
	   "contents of a cell. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

typedef struct {
	char *format;
	char *output;
} translate_t;
static translate_t translate_table[] = {
	{ "General", "G" },
	{ "0", "F0" },
	{ "#,##0", ",0" },
	{ "0.00", "F2" },
	{ "#,##0.00", ",2" },
	{ "$#,##0_);($#,##0)", "C0" },
	{ "$#,##0_);[Red]($#,##0)", "C0-" },
	{ "$#,##0.00_);($#,##0.00)", "C2" },
	{ "$#,##0.00_);[Red]($#,##0.00)", "C2-" },
	{ "0%", "P0" },
	{ "0.00%", "P2" },
	{ "0.00e+00", "S2" },
	{ "# ?/?", "G" },
	{ "# ??/??", "G" },
	{ "m/d/yy", "D4" },
	{ "m/d/yy h:mm", "D4" },
	{ "mm/dd/yy", "D4" },
	{ "d-mmm-yy", "D1" },
	{ "dd-mmm-yy", "D1" },
	{ "d-mmm", "D2" },
	{ "dd-mmm", "D2" },
	{ "mmm-yy", "D3" },
	{ "mm/dd", "D5" },
	{ "h:mm am/pm", "D7" },
	{ "h:mm:ss am/pm", "D6" },
	{ "h:mm", "D9" },
	{ "h:mm:ss", "D8" }
};

static Value *
translate_cell_format (StyleFormat const *format)
{
	int i;
	char *fmt;
	const int translate_table_count = sizeof (translate_table) / sizeof(translate_t);

	if (format == NULL)
		return value_new_string ("G");

	fmt = style_format_as_XL (format, FALSE);

	/*
	 * TODO : What does this do in different locales ??
	 */
	for (i = 0; i < translate_table_count; i++) {
		const translate_t *t = &translate_table[i];
		
		if (!g_strcasecmp (fmt, t->format)) {
			g_free (fmt);
			return value_new_string (t->output);
		}
	}

	g_free (fmt);
	return value_new_string ("G");
}

static FormatCharacteristics
retrieve_format_info (Sheet *sheet, int col, int row)
{
	MStyle *mstyle = sheet_style_get (sheet, col, row);
	StyleFormat *format = mstyle_get_format (mstyle);
	char *fmt = style_format_as_XL (format, FALSE);
	FormatCharacteristics info;

	cell_format_classify (fmt, &info);
	g_free (fmt);

	return info;
}

static Value *
gnumeric_cell (FunctionEvalInfo *ei, Value **argv)
{
	const char *info_type = value_peek_string (argv[0]);
	CellRef ref = argv [1]->v_range.cell.a;
	
	if (!g_strcasecmp(info_type, "address")) {
		/* Reference of the first cell in reference, as text. */
		return value_new_string (cell_coord_name (ref.col, ref.row));
	} else if (!g_strcasecmp (info_type, "col")) {
		return value_new_int (ref.col + 1);
	} else if (!g_strcasecmp (info_type, "color")) {
		FormatCharacteristics info = retrieve_format_info (ei->pos->sheet, ref.col, ref.row);

		/* 0x01 = first bit (1) indicating negative colors */
		return (info.negative_fmt & 0x01) ? value_new_int (1) : value_new_int (0);
	} else if (!g_strcasecmp (info_type, "contents")) {
		Cell *cell = sheet_cell_get (ei->pos->sheet, ref.col, ref.row);
		
		if (cell && cell->value)
			return value_duplicate (cell->value);
		return value_new_empty ();
	} else if (!g_strcasecmp (info_type, "filename"))	{
		char *name = ei->pos->sheet->workbook->filename;

		if (name == NULL)
			return value_new_string ("");
		else
			return value_new_string (name);
	} else if (!g_strcasecmp (info_type, "format")) {
		MStyle *mstyle = sheet_style_get (ei->pos->sheet, ref.col, ref.row);
		
		return translate_cell_format (mstyle_get_format (mstyle));
	} else if (!g_strcasecmp (info_type, "parentheses")) {
		FormatCharacteristics info = retrieve_format_info (ei->pos->sheet, ref.col, ref.row);

		/* 0x02 = second bit (2) indicating parentheses */
		return (info.negative_fmt & 0x02) ? value_new_int (1) : value_new_int (0);
	} else if (!g_strcasecmp (info_type, "prefix")) {
		MStyle *mstyle = sheet_style_get (ei->pos->sheet, ref.col, ref.row);
		Cell *cell = sheet_cell_get (ei->pos->sheet, ref.col, ref.row);
		
		if (cell && cell->value && cell->value->type == VALUE_STRING) {
			switch (mstyle_get_align_h (mstyle)) {
			case HALIGN_GENERAL: return value_new_string ("'");
			case HALIGN_LEFT:    return value_new_string ("'");
			case HALIGN_RIGHT:   return value_new_string ("\"");
			case HALIGN_CENTER:  return value_new_string ("^");
			case HALIGN_FILL:    return value_new_string ("\\");
			default : 	     return value_new_string ("");
			}
		}
		return value_new_string ("");
	} else if (!g_strcasecmp (info_type, "protect")) {
		/*
		 * FIXME: We can only implement this when we have proper
		 * cell locking in place. For now we will ALWAYS return 0
		 * this is correct as no cell can ever be locked.
		 */
		/* 0 if the cell is not locked, and 1 if the cell is locked. */
		return value_new_int (0);
	} else if (!g_strcasecmp (info_type, "row")) {
		return value_new_int (ref.row + 1);
	} else if (!g_strcasecmp (info_type, "type")) {
		Cell *cell;

		cell = sheet_cell_get (ei->pos->sheet, ref.col, ref.row);
		if (cell && cell->value) {
			if (cell->value->type == VALUE_STRING)
				return value_new_string ("l");
			else
				return value_new_string ("v");
		}
		return value_new_string ("b");
	} else if (!g_strcasecmp (info_type, "width")) {
		ColRowInfo *info = sheet_col_get_info (ei->pos->sheet, ref.col);
		double charwidth;
		int    cellwidth;

		charwidth = style_font_get_width (gnumeric_default_font);
		cellwidth = info->size_pts;

		return value_new_int (rint (cellwidth / charwidth));
	}

	return value_new_error (ei->pos, _("Unknown info_type"));
}

/***************************************************************************/

static char *help_countblank = {
        N_("@FUNCTION=COUNTBLANK\n"
           "@SYNTAX=COUNTBLANK(range)\n"

           "@DESCRIPTION="
           "COUNTBLANK returns the number of blank cells in a @range. "
	   "This function is Excel compatible. "
           "\n"
	   "@EXAMPLES=\n"
	   "\n"
           "@SEEALSO=COUNT")
};

static Value *
cb_countblank (Sheet *sheet, int col, int row,
	       Cell *cell, void *user_data)
{
	if (!cell_is_blank (cell))
		*((int *)user_data) -= 1;
	return NULL;
}

static Value *
gnumeric_countblank (FunctionEvalInfo *ei, Value **args)
{
	RangeRef const * const r = &args[0]->v_range.cell;

	/* FIXME : This does not handle 3D references */
	int count = (abs(r->a.col - r->b.col) + 1) *
		    (abs(r->a.row - r->b.row) + 1);

	workbook_foreach_cell_in_range (ei->pos, args[0], TRUE,
					&cb_countblank, &count);

	return value_new_int (count);
}

/***************************************************************************/

static char *help_info = {
	N_("@FUNCTION=INFO\n"
	   "@SYNTAX=INFO()\n"

	   "@DESCRIPTION="
	   "INFO returns information about the current operating environment. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};


static Value *
gnumeric_info (FunctionEvalInfo *ei, Value **argv)
{
	char const * const info_type = value_peek_string (argv[0]);
	if (!g_strcasecmp (info_type, "directory")) {
		/* Path of the current directory or folder.  */
		return value_new_error (ei->pos, _("Unimplemented"));
	} else if (!g_strcasecmp (info_type, "memavail")) {
		/* Amount of memory available, in bytes.  */
		return value_new_int (15 << 20);  /* Good enough... */
	} else if (!g_strcasecmp (info_type, "memused")) {
		/* Amount of memory being used for data.  */
		return value_new_int (1 << 20);  /* Good enough... */
	} else if (!g_strcasecmp (info_type, "numfile")) {
		/* Number of active worksheets.  */
		return value_new_int (1);  /* Good enough... */
	} else if (!g_strcasecmp (info_type, "origin")) {
		/* Absolute A1-style reference, as text, prepended with "$A:"
		 * for Lotus 1-2-3 release 3.x compatibility. Returns the cell
		 * reference of the top and leftmost cell visible in the
		 * window, based on the current scrolling position.
		 */
		return value_new_error (ei->pos, _("Unimplemented"));
	} else if (!g_strcasecmp (info_type, "osversion")) {
		/* Current operating system version, as text.  */
		struct utsname unamedata;

		if (uname (&unamedata) == -1)
			return value_new_error (ei->pos,
						_("Unknown version"));
		else {
			char *tmp = g_strdup_printf (_("%s version %s"),
						     unamedata.sysname,
						     unamedata.release);
			Value *res = value_new_string (tmp);
			g_free (tmp);
			return res;
		}
	} else if (!g_strcasecmp (info_type, "recalc")) {
		/* Current recalculation mode; returns "Automatic" or "Manual".  */
		return value_new_string (_("Automatic"));
	} else if (!g_strcasecmp (info_type, "release")) {
		/* Version of Gnumeric (Well, Microsoft Excel), as text.  */
		return value_new_string (GNUMERIC_VERSION);
	} else if (!g_strcasecmp (info_type, "system")) {
		/* Name of the operating environment.  */
		struct utsname unamedata;

		if (uname (&unamedata) == -1)
			return value_new_error (ei->pos, _("Unknown system"));
		else
			return value_new_string (unamedata.sysname);
	} else if (!g_strcasecmp (info_type, "totmem")) {
		/* Total memory available, including memory already in use, in
		 * bytes.
		 */
		return value_new_int (16 << 20);  /* Good enough... */
	}

	return value_new_error (ei->pos, _("Unknown info_type"));
}

/***************************************************************************/

static char *help_iserror = {
	N_("@FUNCTION=ISERROR\n"
	   "@SYNTAX=ISERROR(value)\n"

	   "@DESCRIPTION="
	   "ISERROR returns a TRUE value if the expression has an error\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=ERROR")
};

/* A utility routine to evaluate a single argument and return any errors
 * directly
 */
static Value *
gnumeric_check_for_err (FunctionEvalInfo *ei, GList *expr_node_list,
			Value ** err)
{
	Value * tmp;

	if (g_list_length (expr_node_list) != 1) {
		*err = value_new_error(ei->pos,
				       _("Argument mismatch"));
		return NULL;
	}
	tmp = eval_expr (ei->pos, (ExprTree *) expr_node_list->data, EVAL_STRICT);

	if (tmp != NULL) {
		if (tmp->type == VALUE_ERROR)
			return tmp;
		value_release (tmp);
	}
	return NULL;
}

static Value *
gnumeric_iserror (FunctionEvalInfo *ei, GList *expr_node_list)
{
	Value * res, *err = NULL;
	res = gnumeric_check_for_err (ei, expr_node_list, &err);
	if (err != NULL)
		return err;

	if (res) {
		value_release (res);
		return value_new_bool (TRUE);
	} else
		return value_new_bool (FALSE);
}

/***************************************************************************/

static char *help_isna = {
	N_("@FUNCTION=ISNA\n"
	   "@SYNTAX=ISNA(value)\n"

	   "@DESCRIPTION="
	   "ISNA returns TRUE if the value is the #N/A error value. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

/*
 * We need to operator directly in the input expression in order to bypass
 * the error handling mechanism
 */
static Value *
gnumeric_isna (FunctionEvalInfo *ei, GList *expr_node_list)
{
	Value * res, *err = NULL;
	gboolean b;

	res = gnumeric_check_for_err (ei, expr_node_list, &err);
	if (err != NULL)
		return err;

	b = (res && !strcmp (gnumeric_err_NA, res->v_err.mesg->str));
	if (res) value_release (res);
	return value_new_bool (b);
}

/***************************************************************************/

static char *help_iserr = {
	N_("@FUNCTION=ISERR\n"
	   "@SYNTAX=ISERR(value)\n"

	   "@DESCRIPTION="
	   "ISERR returns TRUE if the value is any error value except #N/A. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_iserr (FunctionEvalInfo *ei, GList *expr_node_list)
{
	Value * res, *err = NULL;
	gboolean b;

	res = gnumeric_check_for_err (ei, expr_node_list, &err);
	if (err != NULL)
		return err;

	b = (res && strcmp (gnumeric_err_NA, res->v_err.mesg->str));
	if (res) value_release (res);
	return value_new_bool (b);
}

/***************************************************************************/

static char *help_error_type = {
	N_("@FUNCTION=ERROR.TYPE\n"
	   "@SYNTAX=ERROR(value)\n"

	   "@DESCRIPTION="
	   "ERROR.TYPE returns an error number corresponding to the given "
	   "error value.  The error numbers for error values are\n"
	   "#DIV/0!    2\n"
	   "#VALUE!    3\n"
	   "#REF!      4\n"
	   "#NAME!     5\n"
	   "#NUM!      6\n"
	   "#NA!       7\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "ERROR.TYPE(NA()) equals 7.\n"
	   "\n"
	   "@SEEALSO=ISERROR")
};

static Value *
gnumeric_error_type (FunctionEvalInfo *ei, GList *expr_node_list)
{
	int retval = -1;
	char const * mesg;
	Value * res, *err = NULL;
	res = gnumeric_check_for_err (ei, expr_node_list, &err);
	if (err != NULL)
		return err;
	if (res == NULL)
		return value_new_error (ei->pos, gnumeric_err_NA);

	mesg = res->v_err.mesg->str;
	if (!strcmp (gnumeric_err_NULL, mesg))
		retval = 1;
	else if (!strcmp (gnumeric_err_DIV0, mesg))
		retval = 2;
	else if (!strcmp (gnumeric_err_VALUE, mesg))
		retval = 3;
	else if (!strcmp (gnumeric_err_REF, mesg))
		retval = 4;
	else if (!strcmp (gnumeric_err_NAME, mesg))
		retval = 5;
	else if (!strcmp (gnumeric_err_NUM, mesg))
		retval = 6;
	else if (!strcmp (gnumeric_err_NA, mesg))
		retval = 7;
	else {
		value_release (res);
		return value_new_error (ei->pos, gnumeric_err_NA);
	}

	value_release (res);
	return value_new_int (retval);
}

/***************************************************************************/

static char *help_na = {
	N_("@FUNCTION=NA\n"
	   "@SYNTAX=NA()\n"

	   "@DESCRIPTION="
	   "NA returns the error value #N/A. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_na (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (ei->pos, gnumeric_err_NA);
}

/***************************************************************************/

static char *help_error = {
	N_("@FUNCTION=ERROR\n"
	   "@SYNTAX=ERROR(text)\n"

	   "@DESCRIPTION="
	   "ERROR return the specified error\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=ISERROR")
};

static Value *
gnumeric_error (FunctionEvalInfo *ei, Value *argv[])
{
	return value_new_error (ei->pos, value_peek_string (argv[0]));
}

/***************************************************************************/

static char *help_isblank = {
	N_("@FUNCTION=ISBLANK\n"
	   "@SYNTAX=ISBLANK(value)\n"

	   "@DESCRIPTION="
	   "ISBLANK returns TRUE if the value is blank. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_isblank (FunctionEvalInfo *ei, GList *expr_node_list)
{
	gboolean result = FALSE;
	ExprTree *expr;
	if (g_list_length (expr_node_list) != 1)
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));

	expr = expr_node_list->data;

	/* How can this happen ? */
	if (expr == NULL)
		return value_new_bool (FALSE);

	/* Handle pointless arrays */
	if (expr->any.oper == OPER_ARRAY) {
		if (expr->array.rows != 1 || expr->array.cols != 1)
			return value_new_bool (FALSE);
		expr = expr->array.corner.expr;
	}

	if (expr->any.oper == OPER_VAR) {
		CellRef const *ref = &expr->var.ref;
		Sheet const *sheet = eval_sheet (ref->sheet, ei->pos->sheet);
		int row, col;
		cell_get_abs_col_row(ref, &ei->pos->eval, &col, &row);
		result = cell_is_blank(sheet_cell_get(sheet, col, row));
	}
	return value_new_bool (result);
}

/***************************************************************************/

static char *help_iseven = {
	N_("@FUNCTION=ISEVEN\n"
	   "@SYNTAX=ISEVEN(value)\n"

	   "@DESCRIPTION="
	   "ISEVEN returns TRUE if the number is even. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=ISODD")
};

static Value *
gnumeric_iseven (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_bool (!(value_get_as_int (argv[0]) & 1));
}

/***************************************************************************/

static char *help_islogical = {
	N_("@FUNCTION=ISLOGICAL\n"
	   "@SYNTAX=ISLOGICAL(value)\n"

	   "@DESCRIPTION="
	   "ISLOGICAL returns TRUE if the value is a logical value. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_islogical (FunctionEvalInfo *ei, GList *expr_node_list)
{
	enum Value_Class cl;

	if (g_list_length (expr_node_list) != 1)
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));

	cl = get_value_class (ei, expr_node_list->data);

	return value_new_bool (cl == VALUE_CLASS_BOOL);
}

/***************************************************************************/

static char *help_isnontext = {
	N_("@FUNCTION=ISNONTEXT\n"
	   "@SYNTAX=ISNONTEXT(value)\n"

	   "@DESCRIPTION="
	   "ISNONTEXT Returns TRUE if the value is not text. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=ISTEXT")
};

static Value *
gnumeric_isnontext (FunctionEvalInfo *ei, GList *expr_node_list)
{
	if (g_list_length (expr_node_list) != 1)
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));

	return value_new_bool (get_value_class (ei, expr_node_list->data)
			       != VALUE_CLASS_TEXT);
}

/***************************************************************************/

static char *help_isnumber = {
	N_("@FUNCTION=ISNUMBER\n"
	   "@SYNTAX=ISNUMBER(value)\n"

	   "@DESCRIPTION="
	   "ISNUMBER returns TRUE if the value is a number. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_isnumber (FunctionEvalInfo *ei, GList *expr_node_list)
{
	if (g_list_length (expr_node_list) != 1)
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));

	return value_new_bool (get_value_class (ei, expr_node_list->data)
			       == VALUE_CLASS_NUMBER);
}

/***************************************************************************/

static char *help_isodd = {
	N_("@FUNCTION=ISODD\n"
	   "@SYNTAX=ISODD(value)\n"

	   "@DESCRIPTION="
	   "ISODD returns TRUE if the number is odd. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=ISEVEN")
};

static Value *
gnumeric_isodd (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_bool (value_get_as_int (argv[0]) & 1);
}

/***************************************************************************/

static char *help_isref = {
	N_("@FUNCTION=ISREF\n"
	   "@SYNTAX=ISREF(value)\n"

	   "@DESCRIPTION="
	   "ISREF returns TRUE if the value is a reference. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_isref (FunctionEvalInfo *ei, GList *expr_node_list)
{
	ExprTree *t;

	if (g_list_length (expr_node_list) != 1)
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));

	t = expr_node_list->data;
	if (!t)
		return NULL;

	return value_new_bool (t->any.oper == OPER_VAR);
}

/***************************************************************************/

static char *help_istext = {
	N_("@FUNCTION=ISTEXT\n"
	   "@SYNTAX=ISTEXT(value)\n"

	   "@DESCRIPTION="
	   "ISTEXT returns TRUE if the value is text. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=ISNONTEXT")
};

static Value *
gnumeric_istext (FunctionEvalInfo *ei, GList *expr_node_list)
{
	if (g_list_length (expr_node_list) != 1)
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));

	return value_new_bool (get_value_class (ei, expr_node_list->data)
			       == VALUE_CLASS_TEXT);
}

/***************************************************************************/

static char *help_n = {
	N_("@FUNCTION=N\n"
	   "@SYNTAX=N(value)\n"

	   "@DESCRIPTION="
	   "N returns a value converted to a number.  Strings containing "
	   "text are converted to the zero value. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_n (FunctionEvalInfo *ei, Value **argv)
{
	const char *str;
	Value *v;

	if (argv[0]->type == VALUE_BOOLEAN)
		return value_new_int (value_get_as_int(argv[0]));

	if (VALUE_IS_NUMBER (argv[0]))
		return value_duplicate (argv[0]);

	if (argv[0]->type != VALUE_STRING)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	str = value_peek_string (argv[0]);
	if (NULL != (v = format_match (str, NULL, NULL)))
		return v;
	return value_new_float (0);
}

/***************************************************************************/

static char *help_type = {
	N_("@FUNCTION=TYPE\n"
	   "@SYNTAX=TYPE(value)\n"

	   "@DESCRIPTION="
	   "TYPE returns a number indicating the data type of a value. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_type (FunctionEvalInfo *ei, GList *expr_node_list)
{
	if (g_list_length (expr_node_list) != 1)
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));

	return value_new_int (get_value_class (ei, expr_node_list->data));
}

/***************************************************************************/

void information_functions_init (void);
void
information_functions_init (void)
{
	FunctionCategory *cat = function_get_category_with_translation ("Information", _("Information"));

	function_add_args  (cat, "cell", "sr", "info_type, cell",
			    &help_cell, gnumeric_cell);
        function_add_args  (cat, "countblank", "r",  "range",
			    &help_countblank, gnumeric_countblank);
	function_add_args  (cat, "error",   "s",  "text",
			    &help_error,   gnumeric_error);
	function_add_nodes (cat, "error.type", NULL, "",
			    &help_error_type, gnumeric_error_type);
	function_add_args  (cat, "info", "s", "info_type",
			    &help_info, gnumeric_info);
	function_add_nodes (cat, "isblank", NULL, "value",
			    &help_isblank, gnumeric_isblank);
	function_add_nodes (cat, "iserr", NULL,   "",
			    &help_iserr,   gnumeric_iserr);
	function_add_nodes (cat, "iserror", NULL,   "",
			    &help_iserror, gnumeric_iserror);
	function_add_args  (cat, "iseven", "?", "value",
			    &help_iseven, gnumeric_iseven);
	function_add_nodes (cat, "islogical", NULL, "value",
			    &help_islogical, gnumeric_islogical);
	function_add_nodes (cat, "isna", NULL,   "",
			    &help_isna,    gnumeric_isna);
	function_add_nodes (cat, "isnontext", NULL, "value",
			    &help_isnontext, gnumeric_isnontext);
	function_add_nodes (cat, "isnumber", NULL, "value",
			    &help_isnumber, gnumeric_isnumber);
	function_add_args  (cat, "isodd", "?", "value",
			    &help_isodd, gnumeric_isodd);
	function_add_nodes (cat, "isref", NULL, "value",
			    &help_isref, gnumeric_isref);
	function_add_nodes (cat, "istext", NULL, "value",
			    &help_istext, gnumeric_istext);
	function_add_args  (cat, "n", "?", "value",
			    &help_n, gnumeric_n);
	function_add_args  (cat, "na",      "",  "",
			    &help_na,      gnumeric_na);
	function_add_nodes (cat, "type",   NULL, "value",
			    &help_type, gnumeric_type);
}
