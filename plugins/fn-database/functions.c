/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-database.c: Built-in database functions and functions registration
 *
 * Author:
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 *   Morten Welinder (terra@gnome.org)
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
#include <gnumeric.h>
#include <func.h>

#include <parse-util.h>
#include <str.h>
#include <cell.h>
#include <sheet.h>
#include <workbook.h>
#include <value.h>
#include <number-match.h>
#include <collect.h>
#include <rangefunc.h>
#include <gnm-i18n.h>

#include <math.h>
#include <string.h>

#include <goffice/app/go-plugin.h>
#include <gnm-plugin.h>

GNM_PLUGIN_MODULE_HEADER;

/***************************************************************************/

/**
 * find_cells_that_match :
 * Finds the cells from the given column that match the criteria.
 */
static GSList *
find_cells_that_match (Sheet *sheet, GnmValue const *database,
		       int col, GSList *criterias)
{
	GSList *ptr, *condition, *cells;
	int    row, first_row, last_row;
	gboolean add_flag;
	GnmCell *cell;
	GODateConventions const *date_conv =
		workbook_date_conv (sheet->workbook);

	cells = NULL;
	/* TODO : Why ignore the first row ?  What if there is no header ? */
	first_row = database->v_range.cell.a.row + 1;
	last_row  = database->v_range.cell.b.row;

	for (row = first_row; row <= last_row; row++) {
		cell = sheet_cell_get (sheet, col, row);

		if (cell != NULL)
			gnm_cell_eval (cell);

		if (gnm_cell_is_empty (cell))
			continue;

		add_flag = TRUE;
		for (ptr = criterias; ptr != NULL; ptr = ptr->next) {
			GnmDBCriteria const *current_criteria = ptr->data;

			add_flag = TRUE;
			condition = current_criteria->conditions;

			for (;condition != NULL ; condition = condition->next) {
				GnmCriteria const *cond = condition->data;
				GnmCell *tmp = sheet_cell_get (sheet,
					cond->column, row);
				if (tmp != NULL)
					gnm_cell_eval (tmp);
				if (gnm_cell_is_empty (tmp) ||
				    !cond->fun (tmp->value, cond->x, date_conv)) {
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
database_find_values (Sheet *sheet, GnmValue const *database,
		      int col, GSList *criterias,
		      CollectFlags flags,
		      int *pcount,
		      GnmValue **error,
		      gboolean floats)
{
	GSList *cells, *current;
	int cellcount, count;
	gnm_float *res1 = NULL;
	GnmValue **res2 = NULL;
	void *res;

	if (flags & ~(COLLECT_IGNORE_STRINGS | COLLECT_IGNORE_BOOLS | COLLECT_IGNORE_BLANKS)) {
		g_warning ("unsupported flags in database_find_values %x", flags);
	}

	/* FIXME: expand and sanitise this call later.  */
	cells = find_cells_that_match (sheet, database, col, criterias);

	cellcount = g_slist_length (cells);
	/* Allocate memory -- one extra to make sure we don't get NULL.  */
	if (floats)
		res = res1 = g_new (gnm_float, cellcount + 1);
	else
		res = res2 = g_new (GnmValue *, cellcount + 1);
	for (count = 0, current = cells; current; current = current->next) {
		GnmCell *cell = current->data;
		GnmValue *value = cell->value;

		if ((flags & COLLECT_IGNORE_STRINGS) && VALUE_IS_STRING (value))
			continue;
		if ((flags & COLLECT_IGNORE_BOOLS) && VALUE_IS_BOOLEAN (value))
			continue;
		if ((flags & COLLECT_IGNORE_BLANKS) && VALUE_IS_EMPTY (value))
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

static GnmValue *
database_float_range_function (GnmFuncEvalInfo *ei,
			       GnmValue const *database,
			       GnmValue const *field,
			       GnmValue const *criteria,
			       float_range_function_t func,
			       CollectFlags flags,
			       GnmStdError zero_count_error,
			       GnmStdError func_error)
{
	int fieldno;
	GSList *criterias = NULL;
	Sheet *sheet;
	int count;
	int err;
	gnm_float *vals = NULL;
	gnm_float fres;
	GnmValue *res;

	fieldno = find_column_of_field (ei->pos, database, field);
	if (fieldno < 0)
		return value_new_error_NUM (ei->pos);

	/* I don't like this -- minimal fix for now.  509427.  */
	if (criteria->type != VALUE_CELLRANGE)
		return value_new_error_NUM (ei->pos);

	criterias = parse_database_criteria (ei->pos, database, criteria);
	if (criterias == NULL)
		return value_new_error_NUM (ei->pos);

	sheet = eval_sheet (database->v_range.cell.a.sheet,
			    ei->pos->sheet);

	vals = database_find_values (sheet, database, fieldno, criterias,
				     flags, &count, &res, TRUE);

	if (!vals) {
		goto out;
	}

	if (count == 0 && zero_count_error != GNM_ERROR_UNKNOWN) {
		res = value_new_error_std (ei->pos, zero_count_error);
		goto out;
	}

	err = func (vals, count, &fres);
	if (err)
		res = value_new_error_std (ei->pos, func_error);
	else
		res = value_new_float (fres);

 out:
	if (criterias)
		free_criterias (criterias);
	g_free (vals);
	return res;
}

/***************************************************************************/

typedef int (*value_range_function_t) (GnmValue **, int, GnmValue **);

static GnmValue *
database_value_range_function (GnmFuncEvalInfo *ei,
			       GnmValue const *database,
			       GnmValue const *field,
			       GnmValue const *criteria,
			       value_range_function_t func,
			       CollectFlags flags,
			       GnmStdError zero_count_error,
			       GnmStdError func_error)
{
	int fieldno;
	GSList *criterias = NULL;
	Sheet *sheet;
	int count;
	int err;
	GnmValue **vals = NULL;
	GnmValue *res;

	fieldno = find_column_of_field (ei->pos, database, field);
	if (fieldno < 0)
		return value_new_error_NUM (ei->pos);

	/* I don't like this -- minimal fix for now.  509427.  */
	if (criteria->type != VALUE_CELLRANGE)
		return value_new_error_NUM (ei->pos);

	criterias = parse_database_criteria (ei->pos, database, criteria);
	if (criterias == NULL)
		return value_new_error_NUM (ei->pos);

	sheet = eval_sheet (database->v_range.cell.a.sheet,
			    ei->pos->sheet);

	vals = database_find_values (sheet, database, fieldno, criterias,
				     flags, &count, &res, FALSE);

	if (!vals) {
		goto out;
	}

	if (count == 0 && zero_count_error != GNM_ERROR_UNKNOWN) {
		res = value_new_error_std (ei->pos, zero_count_error);
		goto out;
	}

	err = func (vals, count, &res);
	if (err)
		res = value_new_error_std (ei->pos, func_error);

 out:
	if (criterias)
		free_criterias (criterias);
	g_free (vals);
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_daverage[] = {
	{ GNM_FUNC_HELP_OLD,
        F_("@FUNCTION=DAVERAGE\n"
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
	   "@field is an integer, for example 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criteria are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifying a " \
	   "value, e.g. ``3'' or ``John''. \n"\
	   "Each row in @criteria specifies a separate condition. "\
           "If a row in "\
	   "@database matches a row in @criteria, then that row is counted. "\
	   "Technically speaking, this a boolean OR operation between the "\
	   "rows in @criteria.\n"\
	   "If @criteria specifies more than one column, then each of the "\
	   "conditions in the specified columns must be true for the row in "\
	   "@database to match. Technically speaking, this is a boolean AND "\
	   "operation between the columns in @criteria.\n"
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
           "DAVERAGE(A1:C7, \"Salary\", A9:A11) equals 42296.3333.\n"
	   "DAVERAGE(A1:C7, \"Age\", A9:A11) equals 39.\n"
           "DAVERAGE(A1:C7, \"Salary\", A9:B11) equals 40782.5.\n"
	   "DAVERAGE(A1:C7, \"Age\", A9:B11) equals 36.\n"
	   "\n"
           "@SEEALSO=DCOUNT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_daverage (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      gnm_range_average,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      GNM_ERROR_NUM,
					      GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_dcount[] = {
	{ GNM_FUNC_HELP_OLD,
        F_("@FUNCTION=DCOUNT\n"
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
	   "@field is an integer, for example 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criteria are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifying a " \
	   "value, e.g. ``3'' or ``John''. \n"\
	   "Each row in @criteria specifies a separate condition. "\
           "If a row in "\
	   "@database matches a row in @criteria, then that row is counted. "\
	   "Technically speaking, this a boolean OR operation between the "\
	   "rows in @criteria.\n"\
	   "If @criteria specifies more than one column, then each of the "\
	   "conditions in the specified columns must be true for the row in "\
	   "@database to match. Technically speaking, this is a boolean AND "\
	   "operation between the columns in @criteria.\n"
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
           "DCOUNT(A1:C7, \"Salary\", A9:A11) equals 3.\n"
           "DCOUNT(A1:C7, \"Salary\", A9:B11) equals 2.\n"
           "DCOUNT(A1:C7, \"Name\", A9:B11) equals 0.\n"
	   "\n"
           "@SEEALSO=DAVERAGE")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_dcount (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      gnm_range_count,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      GNM_ERROR_UNKNOWN,
					      GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_dcounta[] = {
	{ GNM_FUNC_HELP_OLD,
        F_("@FUNCTION=DCOUNTA\n"
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
	   "@field is an integer, for example 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criteria are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifying a " \
	   "value, e.g. ``3'' or ``John''. \n"\
	   "Each row in @criteria specifies a separate condition. "\
           "If a row in "\
	   "@database matches a row in @criteria, then that row is counted. "\
	   "Technically speaking, this a boolean OR operation between the "\
	   "rows in @criteria.\n"\
	   "If @criteria specifies more than one column, then each of the "\
	   "conditions in the specified columns must be true for the row in "\
	   "@database to match. Technically speaking, this is a boolean AND "\
	   "operation between the columns in @criteria.\n"
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
           "DCOUNTA(A1:C7, \"Salary\", A9:A11) equals 3.\n"
           "DCOUNTA(A1:C7, \"Salary\", A9:B11) equals 2.\n"
           "DCOUNTA(A1:C7, \"Name\", A9:B11) equals 2.\n"
	   "\n"
           "@SEEALSO=DCOUNT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_dcounta (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      gnm_range_count,
					      COLLECT_IGNORE_BLANKS,
					      GNM_ERROR_UNKNOWN,
					      GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_dget[] = {
	{ GNM_FUNC_HELP_OLD,
        F_("@FUNCTION=DGET\n"
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
	   "@field is an integer, for example 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criteria are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifying a " \
	   "value, e.g. ``3'' or ``John''. \n"\
	   "Each row in @criteria specifies a separate condition. "\
           "If a row in "\
	   "@database matches a row in @criteria, then that row is counted. "\
	   "Technically speaking, this a boolean OR operation between the "\
	   "rows in @criteria.\n"\
	   "If @criteria specifies more than one column, then each of the "\
	   "conditions in the specified columns must be true for the row in "\
	   "@database to match. Technically speaking, this is a boolean AND "\
	   "operation between the columns in @criteria.\n"
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
           "DGET(A1:C7, \"Salary\", A9:A10) equals 34323.\n"
           "DGET(A1:C7, \"Name\", A9:A10) equals \"Clark\".\n"
	   "\n"
           "@SEEALSO=DCOUNT")
	},
	{ GNM_FUNC_HELP_END }
};

static int
range_first (GnmValue **xs, int n, GnmValue **res)
{
	if (n <= 0)
		return 1;

	*res = value_dup (xs[0]);
	return 0;
}

static GnmValue *
gnumeric_dget (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return database_value_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      range_first,
					      COLLECT_IGNORE_BLANKS,
					      GNM_ERROR_VALUE,
					      GNM_ERROR_NUM);
}

static GnmFuncHelp const help_dmax[] = {
	{ GNM_FUNC_HELP_OLD,
        F_("@FUNCTION=DMAX\n"
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
	   "@field is an integer, for example 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criteria are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifying a " \
	   "value, e.g. ``3'' or ``John''. \n"\
	   "Each row in @criteria specifies a separate condition. "\
           "If a row in "\
	   "@database matches a row in @criteria, then that row is counted. "\
	   "Technically speaking, this a boolean OR operation between the "\
	   "rows in @criteria.\n"\
	   "If @criteria specifies more than one column, then each of the "\
	   "conditions in the specified columns must be true for the row in "\
	   "@database to match. Technically speaking, this is a boolean AND "\
	   "operation between the columns in @criteria.\n"
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
           "DMAX(A1:C7, \"Salary\", A9:A11) equals 47242.\n"
           "DMAX(A1:C7, \"Age\", A9:A11) equals 45.\n"
           "DMAX(A1:C7, \"Age\", A9:B11) equals 43.\n"
	   "\n"
           "@SEEALSO=DMIN")
	},
	{ GNM_FUNC_HELP_END }
};

/***************************************************************************/

static GnmValue *
gnumeric_dmax (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      gnm_range_max,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      GNM_ERROR_NUM,
					      GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_dmin[] = {
	{ GNM_FUNC_HELP_OLD,
        F_("@FUNCTION=DMIN\n"
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
	   "@field is an integer, for example 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criteria are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifying a " \
	   "value, e.g. ``3'' or ``John''. \n"\
	   "Each row in @criteria specifies a separate condition. "\
           "If a row in "\
	   "@database matches a row in @criteria, then that row is counted. "\
	   "Technically speaking, this a boolean OR operation between the "\
	   "rows in @criteria.\n"\
	   "If @criteria specifies more than one column, then each of the "\
	   "conditions in the specified columns must be true for the row in "\
	   "@database to match. Technically speaking, this is a boolean AND "\
	   "operation between the columns in @criteria.\n"
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
           "DMIN(A1:C7, \"Salary\", A9:B11) equals 34323.\n"
           "DMIN(A1:C7, \"Age\", A9:B11) equals 29.\n"
	   "\n"
           "@SEEALSO=DMAX")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_dmin (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      gnm_range_min,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      GNM_ERROR_NUM,
					      GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_dproduct[] = {
	{ GNM_FUNC_HELP_OLD,
        F_("@FUNCTION=DPRODUCT\n"
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
	   "@field is an integer, for example 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criteria are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifying a " \
	   "value, e.g. ``3'' or ``John''. \n"\
	   "Each row in @criteria specifies a separate condition. "\
           "If a row in "\
	   "@database matches a row in @criteria, then that row is counted. "\
	   "Technically speaking, this a boolean OR operation between the "\
	   "rows in @criteria.\n"\
	   "If @criteria specifies more than one column, then each of the "\
	   "conditions in the specified columns must be true for the row in "\
	   "@database to match. Technically speaking, this is a boolean AND "\
	   "operation between the columns in @criteria.\n"
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
           "DPRODUCT(A1:C7, \"Age\", A9:B11) equals 1247.\n"
	   "\n"
           "@SEEALSO=DSUM")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_dproduct (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	/* FIXME: check what happens for zero count.  */
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      gnm_range_product,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      GNM_ERROR_UNKNOWN,
					      GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_dstdev[] = {
	{ GNM_FUNC_HELP_OLD,
        F_("@FUNCTION=DSTDEV\n"
           "@SYNTAX=DSTDEV(database,field,criteria)\n"

           "@DESCRIPTION="
           "DSTDEV function returns the estimate of the standard deviation "
	   "of a population based on a sample. The population consists of "
	   "numbers that match conditions specified.\n"
	   "\n"
	   "@database is a range of cells in which rows of related " \
	   "information are records and columns of data are fields. " \
	   "The first row of a database contains labels for each column. " \
	   "\n\n" \
	   "@field specifies which column is used in the function.  If " \
	   "@field is an integer, for example 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criteria are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifying a " \
	   "value, e.g. ``3'' or ``John''. \n"\
	   "Each row in @criteria specifies a separate condition. "\
           "If a row in "\
	   "@database matches a row in @criteria, then that row is counted. "\
	   "Technically speaking, this a boolean OR operation between the "\
	   "rows in @criteria.\n"\
	   "If @criteria specifies more than one column, then each of the "\
	   "conditions in the specified columns must be true for the row in "\
	   "@database to match. Technically speaking, this is a boolean AND "\
	   "operation between the columns in @criteria.\n"
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
           "DSTDEV(A1:C7, \"Age\", A9:B11) equals 9.89949.\n"
           "DSTDEV(A1:C7, \"Salary\", A9:B11) equals 9135.112506.\n"
	   "\n"
           "@SEEALSO=DSTDEVP")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_dstdev (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      gnm_range_stddev_est,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      GNM_ERROR_UNKNOWN,
					      GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_dstdevp[] = {
	{ GNM_FUNC_HELP_OLD,
        F_("@FUNCTION=DSTDEVP\n"
           "@SYNTAX=DSTDEVP(database,field,criteria)\n"

           "@DESCRIPTION="
           "DSTDEVP function returns the standard deviation of a population "
	   "based on the entire population. The population consists of "
	   "numbers that match conditions specified.\n"
	   "\n"
	   "@database is a range of cells in which rows of related " \
	   "information are records and columns of data are fields. " \
	   "The first row of a database contains labels for each column. " \
	   "\n\n" \
	   "@field specifies which column is used in the function.  If " \
	   "@field is an integer, for example 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criteria are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifying a " \
	   "value, e.g. ``3'' or ``John''. \n"\
	   "Each row in @criteria specifies a separate condition. "\
           "If a row in "\
	   "@database matches a row in @criteria, then that row is counted. "\
	   "Technically speaking, this a boolean OR operation between the "\
	   "rows in @criteria.\n"\
	   "If @criteria specifies more than one column, then each of the "\
	   "conditions in the specified columns must be true for the row in "\
	   "@database to match. Technically speaking, this is a boolean AND "\
	   "operation between the columns in @criteria.\n"
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
           "DSTDEVP(A1:C7, \"Age\", A9:B11) equals 7.\n"
           "DSTDEVP(A1:C7, \"Salary\", A9:B11) equals 6459.5.\n"
	   "\n"
           "@SEEALSO=DSTDEV")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_dstdevp (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      gnm_range_stddev_pop,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      GNM_ERROR_UNKNOWN,
					      GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_dsum[] = {
	{ GNM_FUNC_HELP_OLD,
        F_("@FUNCTION=DSUM\n"
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
	   "@field is an integer, for example 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criteria are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifying a " \
	   "value, e.g. ``3'' or ``John''. \n"\
	   "Each row in @criteria specifies a separate condition. "\
           "If a row in "\
	   "@database matches a row in @criteria, then that row is counted. "\
	   "Technically speaking, this a boolean OR operation between the "\
	   "rows in @criteria.\n"\
	   "If @criteria specifies more than one column, then each of the "\
	   "conditions in the specified columns must be true for the row in "\
	   "@database to match. Technically speaking, this is a boolean AND "\
	   "operation between the columns in @criteria.\n"
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
           "DSUM(A1:C7, \"Age\", A9:B11) equals 72.\n"
           "DSUM(A1:C7, \"Salary\", A9:B11) equals 81565.\n"
	   "\n"
           "@SEEALSO=DPRODUCT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_dsum (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	/* FIXME: check what happens for zero count.  */
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      gnm_range_sum,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      GNM_ERROR_UNKNOWN,
					      GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_dvar[] = {
	{ GNM_FUNC_HELP_OLD,
        F_("@FUNCTION=DVAR\n"
           "@SYNTAX=DVAR(database,field,criteria)\n"

           "@DESCRIPTION="
           "DVAR function returns the estimate of variance of a population "
	   "based on a sample. The population consists of numbers "
	   "that match conditions specified.\n"
	   "\n"
	   "@database is a range of cells in which rows of related " \
	   "information are records and columns of data are fields. " \
	   "The first row of a database contains labels for each column. " \
	   "\n\n" \
	   "@field specifies which column is used in the function.  If " \
	   "@field is an integer, for example 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criteria are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifying a " \
	   "value, e.g. ``3'' or ``John''. \n"\
	   "Each row in @criteria specifies a separate condition. "\
           "If a row in "\
	   "@database matches a row in @criteria, then that row is counted. "\
	   "Technically speaking, this a boolean OR operation between the "\
	   "rows in @criteria.\n"\
	   "If @criteria specifies more than one column, then each of the "\
	   "conditions in the specified columns must be true for the row in "\
	   "@database to match. Technically speaking, this is a boolean AND "\
	   "operation between the columns in @criteria.\n"
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
           "DVAR(A1:C7, \"Age\", A9:B11) equals 98.\n"
           "DVAR(A1:C7, \"Salary\", A9:B11) equals 83450280.5.\n"
	   "\n"
           "@SEEALSO=DVARP")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_dvar (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      gnm_range_var_est,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      GNM_ERROR_UNKNOWN,
					      GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_dvarp[] = {
	{ GNM_FUNC_HELP_OLD,
        F_("@FUNCTION=DVARP\n"
           "@SYNTAX=DVARP(database,field,criteria)\n"

           "@DESCRIPTION="
           "DVARP function returns the variance of a population based "
	   "on the entire population. The population consists of numbers "
	   "that match conditions specified.\n"
	   "\n"
	   "@database is a range of cells in which rows of related " \
	   "information are records and columns of data are fields. " \
	   "The first row of a database contains labels for each column. " \
	   "\n\n" \
	   "@field specifies which column is used in the function.  If " \
	   "@field is an integer, for example 2, the second column is used. " \
	   "Field can also be the label of a column.  For example, ``Age'' " \
	   "refers to the column with the label ``Age'' in @database range. " \
	   "\n\n" \
	   "@criteria is the range of cells which contains the specified " \
	   "conditions.  The first row of a @criteria should contain the " \
	   "labels of the fields for which the criteria are for.  Cells " \
	   "below the labels specify conditions, for example, ``>3'' or " \
	   "``<9''.  Equality condition can be given simply by specifying a " \
	   "value, e.g. ``3'' or ``John''. \n"\
	   "Each row in @criteria specifies a separate condition. "\
           "If a row in "\
	   "@database matches a row in @criteria, then that row is counted. "\
	   "Technically speaking, this a boolean OR operation between the "\
	   "rows in @criteria.\n"\
	   "If @criteria specifies more than one column, then each of the "\
	   "conditions in the specified columns must be true for the row in "\
	   "@database to match. Technically speaking, this is a boolean AND "\
	   "operation between the columns in @criteria.\n"
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
           "DVARP(A1:C7, \"Age\", A9:B11) equals 49.\n"
           "DVARP(A1:C7, \"Salary\", A9:B11) equals 41725140.25.\n"
	   "\n"
           "@SEEALSO=DVAR")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_dvarp (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return database_float_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      gnm_range_var_pop,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      GNM_ERROR_UNKNOWN,
					      GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_getpivotdata[] = {
	{ GNM_FUNC_HELP_OLD,
        F_("@FUNCTION=GETPIVOTDATA\n"
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
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_getpivotdata (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	int  col, row;
	GnmCell *cell;

	col = find_column_of_field (ei->pos, argv[0], argv[1]);
	if (col == -1)
		return value_new_error_REF (ei->pos);

	row = argv[0]->v_range.cell.b.row;
	cell = sheet_cell_get (ei->pos->sheet, col, row);

	/* FIXME: Lots of stuff missing */

	if (cell != NULL)
		gnm_cell_eval (cell);

	if (gnm_cell_is_empty (cell) ||
	    !VALUE_IS_NUMBER (cell->value))
		return value_new_error_REF (ei->pos);

        return value_new_float (value_get_as_float (cell->value));
}

/***************************************************************************/

const GnmFuncDescriptor database_functions[] = {
	{ "daverage", "rSr", N_("database,field,criteria"),
	  help_daverage,   gnumeric_daverage, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dcount",   "rSr", N_("database,field,criteria"),
	  help_dcount,     gnumeric_dcount, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dcounta",  "rSr", N_("database,field,criteria"),
	  help_dcounta,    gnumeric_dcounta, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dget",     "rSr", N_("database,field,criteria"),
	  help_dget,       gnumeric_dget, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dmax",     "rSr", N_("database,field,criteria"),
	  help_dmax,       gnumeric_dmax, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dmin",     "rSr", N_("database,field,criteria"),
	  help_dmin,       gnumeric_dmin, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dproduct", "rSr", N_("database,field,criteria"),
	  help_dproduct,   gnumeric_dproduct, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dstdev",   "rSr", N_("database,field,criteria"),
	  help_dstdev,     gnumeric_dstdev, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dstdevp",  "rSr", N_("database,field,criteria"),
	  help_dstdevp,    gnumeric_dstdevp, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dsum",     "rSr", N_("database,field,criteria"),
	  help_dsum,       gnumeric_dsum, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dvar",     "rSr", N_("database,field,criteria"),
	  help_dvar,       gnumeric_dvar, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dvarp",    "rSr", N_("database,field,criteria"),
	  help_dvarp,      gnumeric_dvarp, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

/* XL stores in lookup */
	{ "getpivotdata", "rs", N_("pivot_table,field_name"),
	  help_getpivotdata, gnumeric_getpivotdata, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_SUBSET, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

        {NULL}
};
