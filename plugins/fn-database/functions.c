/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-database.c: Built-in database functions and functions registration
 *
 * Author:
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 *   Morten Welinder (terra@diku.dk)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include <func.h>

#include <parse-util.h>
#include <str.h>
#include <cell.h>
#include <sheet.h>
#include <value.h>
#include <number-match.h>
#include <collect.h>
#include <rangefunc.h>

#include <math.h>
#include <string.h>

#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

/***************************************************************************/

/**
 * find_cells_that_match :
 * Finds the cells from the given column that match the criteria.
 */
static GSList *
find_cells_that_match (Sheet *sheet, Value *database,
		       int col, GSList *criterias)
{
	GSList *ptr, *condition, *cells;
	int    row, first_row, last_row;
	gboolean add_flag;
	Cell *cell;

	cells = NULL;
	/* TODO : Why ignore the first row ?  What if there is no header ? */
	first_row = database->v_range.cell.a.row + 1;
	last_row  = database->v_range.cell.b.row;

	for (row = first_row; row <= last_row; row++) {
		cell = sheet_cell_get (sheet, col, row);
		if (cell_is_blank (cell))
			continue;

		add_flag = TRUE;
		for (ptr = criterias; ptr != NULL; ptr = ptr->next) {
			database_criteria_t const *current_criteria = ptr->data;

			add_flag = TRUE;
			condition = current_criteria->conditions;

			for (;condition != NULL ; condition = condition->next) {
				func_criteria_t const *cond = condition->data;
				Cell const *tmp = sheet_cell_get (sheet,
					cond->column, row);

				if (cell_is_blank (tmp) ||
				    !cond->fun (tmp->value, cond->x)) {
					add_flag = FALSE;
					break;
				}
			}

			if (add_flag)
				break;
		}
		if (add_flag)
			cells = g_slist_prepend (cells, cell);
	}

	return g_slist_reverse (cells);
}

static void *
database_find_values (Sheet *sheet, Value *database,
		      int col, GSList *criterias,
		      CollectFlags flags,
		      int *pcount,
		      Value **error,
		      gboolean floats)
{
	GSList *cells, *current;
	int cellcount, count;
	gnum_float *res1 = NULL;
	Value **res2 = NULL;
	void *res;

	/* FIXME: expand and sanitise this call later.  */
	cells = find_cells_that_match (sheet, database, col, criterias);

	cellcount = g_slist_length (cells);
	/* Allocate memory -- one extra to make sure we don't get NULL.  */
	if (floats)
		res = res1 = g_new (gnum_float, cellcount + 1);
	else
		res = res2 = g_new (Value *, cellcount + 1);
	for (count = 0, current = cells; current; current = current->next) {
		Cell *cell = current->data;
		Value *value = cell->value;

		if ((flags & COLLECT_IGNORE_STRINGS) && value->type == VALUE_STRING)
			continue;
		if ((flags & COLLECT_IGNORE_BOOLS) && value->type == VALUE_BOOLEAN)
			continue;
		if (floats)
			res1[count++] = value_get_as_float (value);
		else
			res2[count++] = value;
	}

	*pcount = count;
	g_slist_free (cells);
	return res;
}

/***************************************************************************/

static Value *
database_float_range_function (FunctionEvalInfo *ei,
			       Value *database,
			       Value *field,
			       Value *criteria,
			       float_range_function_t func,
			       CollectFlags flags,
			       const char *zero_count_error,
			       const char *func_error)
{
	int fieldno;
	GSList *criterias = NULL;
	Sheet *sheet;
	int count;
	int err;
	gnum_float *vals = NULL;
	gnum_float fres;
	Value *res;

	fieldno = find_column_of_field (ei->pos, database, field);
	if (fieldno < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	criterias = parse_database_criteria (ei->pos, database, criteria);
	if (criterias == NULL)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	sheet = eval_sheet (database->v_range.cell.a.sheet,
			    ei->pos->sheet);

	vals = database_find_values (sheet, database, fieldno, criterias,
				     flags, &count, &res, TRUE);

	if (!vals) {
		goto out;
	}

	if (count == 0 && zero_count_error) {
		res = value_new_error (ei->pos, zero_count_error);
		goto out;
	}

	err = func (vals, count, &fres);
	if (err)
		res = value_new_error (ei->pos, func_error);
	else
		res = value_new_float (fres);

 out:
	if (criterias)
		free_criterias (criterias);
	g_free (vals);
	return res;
}

/***************************************************************************/

typedef int (*value_range_function_t) (Value **, int, Value **);

static Value *
database_value_range_function (FunctionEvalInfo *ei,
			       Value *database,
			       Value *field,
			       Value *criteria,
			       value_range_function_t func,
			       CollectFlags flags,
			       const char *zero_count_error,
			       const char *func_error)
{
	int fieldno;
	GSList *criterias = NULL;
	Sheet *sheet;
	int count;
	int err;
	Value **vals = NULL;
	Value *res;

	fieldno = find_column_of_field (ei->pos, database, field);
	if (fieldno < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	criterias = parse_database_criteria (ei->pos, database, criteria);
	if (criterias == NULL)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	sheet = eval_sheet (database->v_range.cell.a.sheet,
			    ei->pos->sheet);

	vals = database_find_values (sheet, database, fieldno, criterias,
				     flags, &count, &res, FALSE);

	if (!vals) {
		goto out;
	}

	if (count == 0 && zero_count_error) {
		res = value_new_error (ei->pos, zero_count_error);
		goto out;
	}

	err = func (vals, count, &res);
	if (err)
		res = value_new_error (ei->pos, func_error);

 out:
	if (criterias)
		free_criterias (criterias);
	g_free (vals);
	return res;
}

/***************************************************************************/

#define DB_ARGUMENT_HELP \
	   "@database is a range of cells in which rows of related " \
	   "information are records and columns of data are fields. " \
	   "The first row of a database contains labels for each column. " \
	   "\n\n" \
	   "@field specifies which column is used in the function.  If " \
	   "@field is an integer, for example. 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criterias are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifing a " \
	   "value, e.g. ``3'' or ``John''.  Each row in @criteria specifies " \
	   "a separate condition, i.e. if a row in @database matches with " \
	   "one of the rows in @criteria then that row is counted in " \
	   "(technically speaking boolean OR between the rows in " \
	   "@criteria).  If @criteria specifies more than one columns then " \
	   "each of the conditions in these columns should be true that " \
	   "the row in @database matches (again technically speaking " \
	   "boolean AND between the columns in each row in @criteria). " \
           "\n" \
	   "@EXAMPLES=\n" \
	   "Let us assume that the range A1:C7 contain the following " \
	   "values:\n" \
	   "Name    Age     Salary\n" \
	   "John    34      54342\n" \
	   "Bill    35      22343\n" \
	   "Clark   29      34323\n" \
	   "Bob     43      47242\n" \
	   "Susan   37      42932\n" \
	   "Jill    45      45324\n" \
	   "\n" \
	   "In addition, the cells A9:B11 contain the following values:\n" \
	   "Age     Salary\n" \
	   "<30\n" \
	   ">40     >46000\n"


/***************************************************************************/

static const char *help_daverage = {
        N_("@FUNCTION=DAVERAGE\n"
           "@SYNTAX=DAVERAGE(database,field,criteria)\n"

           "@DESCRIPTION="
           "DAVERAGE function returns the average of the values in a list "
	   "or database that match conditions specified.\n"
	   "\n"
	   "@database is a range of cells in which rows of related " \
	   "information are records and columns of data are fields. " \
	   "The first row of a database contains labels for each column. " \
	   "\n\n" \
	   "@field specifies which column is used in the function.  If " \
	   "@field is an integer, for example. 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criterias are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifing a " \
	   "value, e.g. ``3'' or ``John''.  Each row in @criteria specifies " \
	   "a separate condition, i.e. if a row in @database matches with " \
	   "one of the rows in @criteria then that row is counted in " \
	   "(technically speaking boolean OR between the rows in " \
	   "@criteria).  If @criteria specifies more than one columns then " \
	   "each of the conditions in these columns should be true that " \
	   "the row in @database matches (again technically speaking " \
	   "boolean AND between the columns in each row in @criteria). " \
           "\n" \
	   "@EXAMPLES=\n" \
	   "Let us assume that the range A1:C7 contain the following " \
	   "values:\n" \
	   "Name    Age     Salary\n" \
	   "John    34      54342\n" \
	   "Bill    35      22343\n" \
	   "Clark   29      34323\n" \
	   "Bob     43      47242\n" \
	   "Susan   37      42932\n" \
	   "Jill    45      45324\n" \
	   "\n" \
	   "In addition, the cells A9:B11 contain the following values:\n" \
	   "Age     Salary\n" \
	   "<30\n" \
	   ">40     >46000\n"
	   "\n"
	   "@EXAMPLES=\n"
           "DAVERAGE(A1:C7, \"Salary\", A9:A11) equals 42296.3333.\n"
	   "DAVERAGE(A1:C7, \"Age\", A9:A11) equals 39.\n"
           "DAVERAGE(A1:C7, \"Salary\", A9:B11) equals 40782.5.\n"
	   "DAVERAGE(A1:C7, \"Age\", A9:B11) equals 36.\n"
	   "\n"
           "@SEEALSO=DCOUNT")
};

static Value *
gnumeric_daverage (FunctionEvalInfo *ei, Value **argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      range_average,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      gnumeric_err_NUM,
					      gnumeric_err_NUM);
}

/***************************************************************************/

static const char *help_dcount = {
        N_("@FUNCTION=DCOUNT\n"
           "@SYNTAX=DCOUNT(database,field,criteria)\n"

           "@DESCRIPTION="
           "DCOUNT function counts the cells that contain numbers in a "
	   "database that match conditions specified.\n"
	   "\n"
	   "@database is a range of cells in which rows of related " \
	   "information are records and columns of data are fields. " \
	   "The first row of a database contains labels for each column. " \
	   "\n\n" \
	   "@field specifies which column is used in the function.  If " \
	   "@field is an integer, for example. 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criterias are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifing a " \
	   "value, e.g. ``3'' or ``John''.  Each row in @criteria specifies " \
	   "a separate condition, i.e. if a row in @database matches with " \
	   "one of the rows in @criteria then that row is counted in " \
	   "(technically speaking boolean OR between the rows in " \
	   "@criteria).  If @criteria specifies more than one columns then " \
	   "each of the conditions in these columns should be true that " \
	   "the row in @database matches (again technically speaking " \
	   "boolean AND between the columns in each row in @criteria). " \
           "\n" \
	   "@EXAMPLES=\n" \
	   "Let us assume that the range A1:C7 contain the following " \
	   "values:\n" \
	   "Name    Age     Salary\n" \
	   "John    34      54342\n" \
	   "Bill    35      22343\n" \
	   "Clark   29      34323\n" \
	   "Bob     43      47242\n" \
	   "Susan   37      42932\n" \
	   "Jill    45      45324\n" \
	   "\n" \
	   "In addition, the cells A9:B11 contain the following values:\n" \
	   "Age     Salary\n" \
	   "<30\n" \
	   ">40     >46000\n"
	   "\n"
	   "@EXAMPLES=\n"
           "DCOUNT(A1:C7, \"Salary\", A9:A11) equals 3.\n"
           "DCOUNT(A1:C7, \"Salary\", A9:B11) equals 2.\n"
           "DCOUNT(A1:C7, \"Name\", A9:B11) equals 0.\n"
	   "\n"
           "@SEEALSO=DAVERAGE")
};

static Value *
gnumeric_dcount (FunctionEvalInfo *ei, Value **argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      range_count,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      NULL,
					      gnumeric_err_NUM);
}

/***************************************************************************/

static const char *help_dcounta = {
        N_("@FUNCTION=DCOUNTA\n"
           "@SYNTAX=DCOUNTA(database,field,criteria)\n"

           "@DESCRIPTION="
           "DCOUNTA function counts the cells that contain data in a "
	   "database that match conditions specified.\n"
	   "\n"
	   "@database is a range of cells in which rows of related " \
	   "information are records and columns of data are fields. " \
	   "The first row of a database contains labels for each column. " \
	   "\n\n" \
	   "@field specifies which column is used in the function.  If " \
	   "@field is an integer, for example. 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criterias are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifing a " \
	   "value, e.g. ``3'' or ``John''.  Each row in @criteria specifies " \
	   "a separate condition, i.e. if a row in @database matches with " \
	   "one of the rows in @criteria then that row is counted in " \
	   "(technically speaking boolean OR between the rows in " \
	   "@criteria).  If @criteria specifies more than one columns then " \
	   "each of the conditions in these columns should be true that " \
	   "the row in @database matches (again technically speaking " \
	   "boolean AND between the columns in each row in @criteria). " \
           "\n" \
	   "@EXAMPLES=\n" \
	   "Let us assume that the range A1:C7 contain the following " \
	   "values:\n" \
	   "Name    Age     Salary\n" \
	   "John    34      54342\n" \
	   "Bill    35      22343\n" \
	   "Clark   29      34323\n" \
	   "Bob     43      47242\n" \
	   "Susan   37      42932\n" \
	   "Jill    45      45324\n" \
	   "\n" \
	   "In addition, the cells A9:B11 contain the following values:\n" \
	   "Age     Salary\n" \
	   "<30\n" \
	   ">40     >46000\n"
	   "\n"
	   "@EXAMPLES=\n"
           "DCOUNTA(A1:C7, \"Salary\", A9:A11) equals 3.\n"
           "DCOUNTA(A1:C7, \"Salary\", A9:B11) equals 2.\n"
           "DCOUNTA(A1:C7, \"Name\", A9:B11) equals 2.\n"
	   "\n"
           "@SEEALSO=DCOUNT")
};

static Value *
gnumeric_dcounta (FunctionEvalInfo *ei, Value **argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      range_count,
					      COLLECT_IGNORE_BLANKS,
					      NULL,
					      gnumeric_err_NUM);
}

/***************************************************************************/

static const char *help_dget = {
        N_("@FUNCTION=DGET\n"
           "@SYNTAX=DGET(database,field,criteria)\n"

           "@DESCRIPTION="
           "DGET function returns a single value from a column that "
	   "match conditions specified.\n"
	   "\n"
	   "@database is a range of cells in which rows of related " \
	   "information are records and columns of data are fields. " \
	   "The first row of a database contains labels for each column. " \
	   "\n\n" \
	   "@field specifies which column is used in the function.  If " \
	   "@field is an integer, for example. 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criterias are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifing a " \
	   "value, e.g. ``3'' or ``John''.  Each row in @criteria specifies " \
	   "a separate condition, i.e. if a row in @database matches with " \
	   "one of the rows in @criteria then that row is counted in " \
	   "(technically speaking boolean OR between the rows in " \
	   "@criteria).  If @criteria specifies more than one columns then " \
	   "each of the conditions in these columns should be true that " \
	   "the row in @database matches (again technically speaking " \
	   "boolean AND between the columns in each row in @criteria). " \
           "\n" \
	   "@EXAMPLES=\n" \
	   "Let us assume that the range A1:C7 contain the following " \
	   "values:\n" \
	   "Name    Age     Salary\n" \
	   "John    34      54342\n" \
	   "Bill    35      22343\n" \
	   "Clark   29      34323\n" \
	   "Bob     43      47242\n" \
	   "Susan   37      42932\n" \
	   "Jill    45      45324\n" \
	   "\n" \
	   "In addition, the cells A9:B11 contain the following values:\n" \
	   "Age     Salary\n" \
	   "<30\n" \
	   ">40     >46000\n"
	   "\n"
	   "* If none of the items match the conditions, DGET returns #VALUE! "
	   "error.\n"
	   "* If more than one items match the conditions, DGET returns #NUM! "
	   "error.\n"
	   "\n"
	   "@EXAMPLES=\n"
           "DGET(A1:C7, \"Salary\", A9:A10) equals 34323.\n"
           "DGET(A1:C7, \"Name\", A9:A10) equals \"Clark\".\n"
	   "\n"
           "@SEEALSO=DCOUNT")
};

static int
range_first (Value **xs, int n, Value **res)
{
	if (n <= 0)
		return 1;

	*res = value_duplicate (xs[0]);
	return 0;
}

static Value *
gnumeric_dget (FunctionEvalInfo *ei, Value **argv)
{
	return database_value_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      range_first,
					      COLLECT_IGNORE_BLANKS,
					      gnumeric_err_VALUE,
					      gnumeric_err_NUM);
}

static const char *help_dmax = {
        N_("@FUNCTION=DMAX\n"
           "@SYNTAX=DMAX(database,field,criteria)\n"

           "@DESCRIPTION="
           "DMAX function returns the largest number in a column that "
	   "match conditions specified.\n"
	   "\n"
	   "@database is a range of cells in which rows of related " \
	   "information are records and columns of data are fields. " \
	   "The first row of a database contains labels for each column. " \
	   "\n\n" \
	   "@field specifies which column is used in the function.  If " \
	   "@field is an integer, for example. 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criterias are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifing a " \
	   "value, e.g. ``3'' or ``John''.  Each row in @criteria specifies " \
	   "a separate condition, i.e. if a row in @database matches with " \
	   "one of the rows in @criteria then that row is counted in " \
	   "(technically speaking boolean OR between the rows in " \
	   "@criteria).  If @criteria specifies more than one columns then " \
	   "each of the conditions in these columns should be true that " \
	   "the row in @database matches (again technically speaking " \
	   "boolean AND between the columns in each row in @criteria). " \
           "\n" \
	   "@EXAMPLES=\n" \
	   "Let us assume that the range A1:C7 contain the following " \
	   "values:\n" \
	   "Name    Age     Salary\n" \
	   "John    34      54342\n" \
	   "Bill    35      22343\n" \
	   "Clark   29      34323\n" \
	   "Bob     43      47242\n" \
	   "Susan   37      42932\n" \
	   "Jill    45      45324\n" \
	   "\n" \
	   "In addition, the cells A9:B11 contain the following values:\n" \
	   "Age     Salary\n" \
	   "<30\n" \
	   ">40     >46000\n"
	   "\n"
	   "@EXAMPLES=\n"
           "DMAX(A1:C7, \"Salary\", A9:A11) equals 47242.\n"
           "DMAX(A1:C7, \"Age\", A9:A11) equals 45.\n"
           "DMAX(A1:C7, \"Age\", A9:B11) equals 43.\n"
	   "\n"
           "@SEEALSO=DMIN")
};

/***************************************************************************/

static Value *
gnumeric_dmax (FunctionEvalInfo *ei, Value **argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      range_max,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      gnumeric_err_NUM,
					      gnumeric_err_NUM);
}

/***************************************************************************/

static const char *help_dmin = {
        N_("@FUNCTION=DMIN\n"
           "@SYNTAX=DMIN(database,field,criteria)\n"

           "@DESCRIPTION="
           "DMIN function returns the smallest number in a column that "
	   "match conditions specified.\n"
	   "\n"
	   "@database is a range of cells in which rows of related " \
	   "information are records and columns of data are fields. " \
	   "The first row of a database contains labels for each column. " \
	   "\n\n" \
	   "@field specifies which column is used in the function.  If " \
	   "@field is an integer, for example. 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criterias are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifing a " \
	   "value, e.g. ``3'' or ``John''.  Each row in @criteria specifies " \
	   "a separate condition, i.e. if a row in @database matches with " \
	   "one of the rows in @criteria then that row is counted in " \
	   "(technically speaking boolean OR between the rows in " \
	   "@criteria).  If @criteria specifies more than one columns then " \
	   "each of the conditions in these columns should be true that " \
	   "the row in @database matches (again technically speaking " \
	   "boolean AND between the columns in each row in @criteria). " \
           "\n" \
	   "@EXAMPLES=\n" \
	   "Let us assume that the range A1:C7 contain the following " \
	   "values:\n" \
	   "Name    Age     Salary\n" \
	   "John    34      54342\n" \
	   "Bill    35      22343\n" \
	   "Clark   29      34323\n" \
	   "Bob     43      47242\n" \
	   "Susan   37      42932\n" \
	   "Jill    45      45324\n" \
	   "\n" \
	   "In addition, the cells A9:B11 contain the following values:\n" \
	   "Age     Salary\n" \
	   "<30\n" \
	   ">40     >46000\n"
	   "\n"
	   "@EXAMPLES=\n"
           "DMIN(A1:C7, \"Salary\", A9:B11) equals 34323.\n"
           "DMIN(A1:C7, \"Age\", A9:B11) equals 29.\n"
	   "\n"
           "@SEEALSO=DMAX")
};

static Value *
gnumeric_dmin (FunctionEvalInfo *ei, Value **argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      range_min,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      gnumeric_err_NUM,
					      gnumeric_err_NUM);
}

/***************************************************************************/

static const char *help_dproduct = {
        N_("@FUNCTION=DPRODUCT\n"
           "@SYNTAX=DPRODUCT(database,field,criteria)\n"

           "@DESCRIPTION="
           "DPRODUCT function returns the product of numbers in a column "
	   "that match conditions specified.\n"
	   "\n"
	   "@database is a range of cells in which rows of related " \
	   "information are records and columns of data are fields. " \
	   "The first row of a database contains labels for each column. " \
	   "\n\n" \
	   "@field specifies which column is used in the function.  If " \
	   "@field is an integer, for example. 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criterias are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifing a " \
	   "value, e.g. ``3'' or ``John''.  Each row in @criteria specifies " \
	   "a separate condition, i.e. if a row in @database matches with " \
	   "one of the rows in @criteria then that row is counted in " \
	   "(technically speaking boolean OR between the rows in " \
	   "@criteria).  If @criteria specifies more than one columns then " \
	   "each of the conditions in these columns should be true that " \
	   "the row in @database matches (again technically speaking " \
	   "boolean AND between the columns in each row in @criteria). " \
           "\n" \
	   "@EXAMPLES=\n" \
	   "Let us assume that the range A1:C7 contain the following " \
	   "values:\n" \
	   "Name    Age     Salary\n" \
	   "John    34      54342\n" \
	   "Bill    35      22343\n" \
	   "Clark   29      34323\n" \
	   "Bob     43      47242\n" \
	   "Susan   37      42932\n" \
	   "Jill    45      45324\n" \
	   "\n" \
	   "In addition, the cells A9:B11 contain the following values:\n" \
	   "Age     Salary\n" \
	   "<30\n" \
	   ">40     >46000\n"
	   "\n"
	   "@EXAMPLES=\n"
           "DPRODUCT(A1:C7, \"Age\", A9:B11) equals 1247.\n"
	   "\n"
           "@SEEALSO=DSUM")
};

static Value *
gnumeric_dproduct (FunctionEvalInfo *ei, Value **argv)
{
	/* FIXME: check what happens for zero count.  */
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      range_product,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      NULL,
					      gnumeric_err_NUM);
}

/***************************************************************************/

static const char *help_dstdev = {
        N_("@FUNCTION=DSTDEV\n"
           "@SYNTAX=DSTDEV(database,field,criteria)\n"

           "@DESCRIPTION="
           "DSTDEV function returns the estimate of the standard deviation "
	   "of a population based on a sample. The populations consists of "
	   "numbers that match conditions specified.\n"
	   "\n"
	   "@database is a range of cells in which rows of related " \
	   "information are records and columns of data are fields. " \
	   "The first row of a database contains labels for each column. " \
	   "\n\n" \
	   "@field specifies which column is used in the function.  If " \
	   "@field is an integer, for example. 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criterias are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifing a " \
	   "value, e.g. ``3'' or ``John''.  Each row in @criteria specifies " \
	   "a separate condition, i.e. if a row in @database matches with " \
	   "one of the rows in @criteria then that row is counted in " \
	   "(technically speaking boolean OR between the rows in " \
	   "@criteria).  If @criteria specifies more than one columns then " \
	   "each of the conditions in these columns should be true that " \
	   "the row in @database matches (again technically speaking " \
	   "boolean AND between the columns in each row in @criteria). " \
           "\n" \
	   "@EXAMPLES=\n" \
	   "Let us assume that the range A1:C7 contain the following " \
	   "values:\n" \
	   "Name    Age     Salary\n" \
	   "John    34      54342\n" \
	   "Bill    35      22343\n" \
	   "Clark   29      34323\n" \
	   "Bob     43      47242\n" \
	   "Susan   37      42932\n" \
	   "Jill    45      45324\n" \
	   "\n" \
	   "In addition, the cells A9:B11 contain the following values:\n" \
	   "Age     Salary\n" \
	   "<30\n" \
	   ">40     >46000\n"
	   "\n"
	   "@EXAMPLES=\n"
           "DSTDEV(A1:C7, \"Age\", A9:B11) equals 9.89949.\n"
           "DSTDEV(A1:C7, \"Salary\", A9:B11) equals 9135.112506.\n"
	   "\n"
           "@SEEALSO=DSTDEVP")
};

static Value *
gnumeric_dstdev (FunctionEvalInfo *ei, Value **argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      range_stddev_est,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      NULL,
					      gnumeric_err_NUM);
}

/***************************************************************************/

static const char *help_dstdevp = {
        N_("@FUNCTION=DSTDEVP\n"
           "@SYNTAX=DSTDEVP(database,field,criteria)\n"

           "@DESCRIPTION="
           "DSTDEVP function returns the standard deviation of a population "
	   "based on the entire populations. The populations consists of "
	   "numbers that match conditions specified.\n"
	   "\n"
	   "@database is a range of cells in which rows of related " \
	   "information are records and columns of data are fields. " \
	   "The first row of a database contains labels for each column. " \
	   "\n\n" \
	   "@field specifies which column is used in the function.  If " \
	   "@field is an integer, for example. 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criterias are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifing a " \
	   "value, e.g. ``3'' or ``John''.  Each row in @criteria specifies " \
	   "a separate condition, i.e. if a row in @database matches with " \
	   "one of the rows in @criteria then that row is counted in " \
	   "(technically speaking boolean OR between the rows in " \
	   "@criteria).  If @criteria specifies more than one columns then " \
	   "each of the conditions in these columns should be true that " \
	   "the row in @database matches (again technically speaking " \
	   "boolean AND between the columns in each row in @criteria). " \
           "\n" \
	   "@EXAMPLES=\n" \
	   "Let us assume that the range A1:C7 contain the following " \
	   "values:\n" \
	   "Name    Age     Salary\n" \
	   "John    34      54342\n" \
	   "Bill    35      22343\n" \
	   "Clark   29      34323\n" \
	   "Bob     43      47242\n" \
	   "Susan   37      42932\n" \
	   "Jill    45      45324\n" \
	   "\n" \
	   "In addition, the cells A9:B11 contain the following values:\n" \
	   "Age     Salary\n" \
	   "<30\n" \
	   ">40     >46000\n"
	   "\n"
	   "@EXAMPLES=\n"
           "DSTDEVP(A1:C7, \"Age\", A9:B11) equals 7.\n"
           "DSTDEVP(A1:C7, \"Salary\", A9:B11) equals 6459.5.\n"
	   "\n"
           "@SEEALSO=DSTDEV")
};

static Value *
gnumeric_dstdevp (FunctionEvalInfo *ei, Value **argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      range_stddev_pop,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      NULL,
					      gnumeric_err_NUM);
}

/***************************************************************************/

static const char *help_dsum = {
        N_("@FUNCTION=DSUM\n"
           "@SYNTAX=DSUM(database,field,criteria)\n"

           "@DESCRIPTION="
           "DSUM function returns the sum of numbers in a column "
	   "that match conditions specified.\n"
	   "\n"
	   "@database is a range of cells in which rows of related " \
	   "information are records and columns of data are fields. " \
	   "The first row of a database contains labels for each column. " \
	   "\n\n" \
	   "@field specifies which column is used in the function.  If " \
	   "@field is an integer, for example. 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criterias are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifing a " \
	   "value, e.g. ``3'' or ``John''.  Each row in @criteria specifies " \
	   "a separate condition, i.e. if a row in @database matches with " \
	   "one of the rows in @criteria then that row is counted in " \
	   "(technically speaking boolean OR between the rows in " \
	   "@criteria).  If @criteria specifies more than one columns then " \
	   "each of the conditions in these columns should be true that " \
	   "the row in @database matches (again technically speaking " \
	   "boolean AND between the columns in each row in @criteria). " \
           "\n" \
	   "@EXAMPLES=\n" \
	   "Let us assume that the range A1:C7 contain the following " \
	   "values:\n" \
	   "Name    Age     Salary\n" \
	   "John    34      54342\n" \
	   "Bill    35      22343\n" \
	   "Clark   29      34323\n" \
	   "Bob     43      47242\n" \
	   "Susan   37      42932\n" \
	   "Jill    45      45324\n" \
	   "\n" \
	   "In addition, the cells A9:B11 contain the following values:\n" \
	   "Age     Salary\n" \
	   "<30\n" \
	   ">40     >46000\n"
	   "\n"
	   "@EXAMPLES=\n"
           "DSUM(A1:C7, \"Age\", A9:B11) equals 72.\n"
           "DSUM(A1:C7, \"Salary\", A9:B11) equals 81565.\n"
	   "\n"
           "@SEEALSO=DPRODUCT")
};

static Value *
gnumeric_dsum (FunctionEvalInfo *ei, Value **argv)
{
	/* FIXME: check what happens for zero count.  */
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      range_sum,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      NULL,
					      gnumeric_err_NUM);
}

/***************************************************************************/

static const char *help_dvar = {
        N_("@FUNCTION=DVAR\n"
           "@SYNTAX=DVAR(database,field,criteria)\n"

           "@DESCRIPTION="
           "DVAR function returns the estimate of variance of a population "
	   "based on a sample. The populations consists of numbers "
	   "that match conditions specified.\n"
	   "\n"
	   "@database is a range of cells in which rows of related " \
	   "information are records and columns of data are fields. " \
	   "The first row of a database contains labels for each column. " \
	   "\n\n" \
	   "@field specifies which column is used in the function.  If " \
	   "@field is an integer, for example. 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criterias are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifing a " \
	   "value, e.g. ``3'' or ``John''.  Each row in @criteria specifies " \
	   "a separate condition, i.e. if a row in @database matches with " \
	   "one of the rows in @criteria then that row is counted in " \
	   "(technically speaking boolean OR between the rows in " \
	   "@criteria).  If @criteria specifies more than one columns then " \
	   "each of the conditions in these columns should be true that " \
	   "the row in @database matches (again technically speaking " \
	   "boolean AND between the columns in each row in @criteria). " \
           "\n" \
	   "@EXAMPLES=\n" \
	   "Let us assume that the range A1:C7 contain the following " \
	   "values:\n" \
	   "Name    Age     Salary\n" \
	   "John    34      54342\n" \
	   "Bill    35      22343\n" \
	   "Clark   29      34323\n" \
	   "Bob     43      47242\n" \
	   "Susan   37      42932\n" \
	   "Jill    45      45324\n" \
	   "\n" \
	   "In addition, the cells A9:B11 contain the following values:\n" \
	   "Age     Salary\n" \
	   "<30\n" \
	   ">40     >46000\n"
	   "\n"
	   "@EXAMPLES=\n"
           "DVAR(A1:C7, \"Age\", A9:B11) equals 98.\n"
           "DVAR(A1:C7, \"Salary\", A9:B11) equals 83450280.5.\n"
	   "\n"
           "@SEEALSO=DVARP")
};

static Value *
gnumeric_dvar (FunctionEvalInfo *ei, Value **argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      range_var_est,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      NULL,
					      gnumeric_err_NUM);
}

/***************************************************************************/

static const char *help_dvarp = {
        N_("@FUNCTION=DVARP\n"
           "@SYNTAX=DVARP(database,field,criteria)\n"

           "@DESCRIPTION="
           "DVARP function returns the variance of a population based "
	   "on the entire populations. The populations consists of numbers "
	   "that match conditions specified.\n"
	   "\n"
	   "@database is a range of cells in which rows of related " \
	   "information are records and columns of data are fields. " \
	   "The first row of a database contains labels for each column. " \
	   "\n\n" \
	   "@field specifies which column is used in the function.  If " \
	   "@field is an integer, for example. 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criterias are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifing a " \
	   "value, e.g. ``3'' or ``John''.  Each row in @criteria specifies " \
	   "a separate condition, i.e. if a row in @database matches with " \
	   "one of the rows in @criteria then that row is counted in " \
	   "(technically speaking boolean OR between the rows in " \
	   "@criteria).  If @criteria specifies more than one columns then " \
	   "each of the conditions in these columns should be true that " \
	   "the row in @database matches (again technically speaking " \
	   "boolean AND between the columns in each row in @criteria). " \
           "\n" \
	   "@EXAMPLES=\n" \
	   "Let us assume that the range A1:C7 contain the following " \
	   "values:\n" \
	   "Name    Age     Salary\n" \
	   "John    34      54342\n" \
	   "Bill    35      22343\n" \
	   "Clark   29      34323\n" \
	   "Bob     43      47242\n" \
	   "Susan   37      42932\n" \
	   "Jill    45      45324\n" \
	   "\n" \
	   "In addition, the cells A9:B11 contain the following values:\n" \
	   "Age     Salary\n" \
	   "<30\n" \
	   ">40     >46000\n"
	   "\n"
	   "@EXAMPLES=\n"
           "DVARP(A1:C7, \"Age\", A9:B11) equals 49.\n"
           "DVARP(A1:C7, \"Salary\", A9:B11) equals 41725140.25.\n"
	   "\n"
           "@SEEALSO=DVAR")
};

static Value *
gnumeric_dvarp (FunctionEvalInfo *ei, Value **argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      range_var_pop,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      NULL,
					      gnumeric_err_NUM);
}

/***************************************************************************/

static const char *help_getpivotdata = {
        N_("@FUNCTION=GETPIVOTDATA\n"
           "@SYNTAX=GETPIVOTDATA(pivot_table,field_name)\n"

           "@DESCRIPTION="
           "GETPIVOTDATA function fetches summary data from a pivot table. "
	   "@pivot_table is a cell range containing the pivot table. "
	   "@field_name is the name of the field of which you want the "
	   "summary data.\n"
	   "\n"
	   "* If the summary data is unavailable, GETPIVOTDATA returns #REF! "
	   "error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
           "@SEEALSO=")
};

static Value *
gnumeric_getpivotdata (FunctionEvalInfo *ei, Value **argv)
{
	int  col, row;
	Cell *cell;

	col = find_column_of_field (ei->pos, argv[0], argv[1]);
	if (col == -1)
		return value_new_error (ei->pos, gnumeric_err_REF);

	row = argv[0]->v_range.cell.b.row;
	cell = sheet_cell_get (ei->pos->sheet, col, row);

	/* FIXME: Lots of stuff missing */

	if (cell_is_blank (cell) ||
	    !VALUE_IS_NUMBER (cell->value))
		return value_new_error (ei->pos, gnumeric_err_REF);

        return value_new_float (value_get_as_float (cell->value));
}

/***************************************************************************/

const GnmFuncDescriptor database_functions[] = {
	{ "daverage", "r?r", N_("database,field,criteria"),
	  &help_daverage,   gnumeric_daverage, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dcount",   "r?r", N_("database,field,criteria"),
	  &help_dcount,     gnumeric_dcount, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dcounta",  "r?r", N_("database,field,criteria"),
	  &help_dcounta,    gnumeric_dcounta, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dget",     "r?r", N_("database,field,criteria"),
	  &help_dget,       gnumeric_dget, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dmax",     "r?r", N_("database,field,criteria"),
	  &help_dmax,       gnumeric_dmax, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dmin",     "r?r", N_("database,field,criteria"),
	  &help_dmin,       gnumeric_dmin, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dproduct", "r?r", N_("database,field,criteria"),
	  &help_dproduct,   gnumeric_dproduct, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dstdev",   "r?r", N_("database,field,criteria"),
	  &help_dstdev,     gnumeric_dstdev, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dstdevp",  "r?r", N_("database,field,criteria"),
	  &help_dstdevp,    gnumeric_dstdevp, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dsum",     "r?r", N_("database,field,criteria"),
	  &help_dsum,       gnumeric_dsum, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dvar",     "r?r", N_("database,field,criteria"),
	  &help_dvar,       gnumeric_dvar, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dvarp",    "r?r", N_("database,field,criteria"),
	  &help_dvarp,      gnumeric_dvarp, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "getpivotdata", "rs", N_("pivot_table,field_name"),
	  &help_getpivotdata, gnumeric_getpivotdata, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        {NULL}
};
