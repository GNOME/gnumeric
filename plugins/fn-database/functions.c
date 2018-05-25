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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <func.h>

#include <parse-util.h>
#include <cell.h>
#include <sheet.h>
#include <workbook.h>
#include <value.h>
#include <criteria.h>
#include <collect.h>
#include <rangefunc.h>
#include <gnm-i18n.h>

#include <goffice/goffice.h>
#include <gnm-plugin.h>

#include <math.h>
#include <string.h>

GNM_PLUGIN_MODULE_HEADER;

#define GNM_HELP_ARG_DATABASE { GNM_FUNC_HELP_ARG, F_("database:a range in which rows " \
						      "of related information are records and " \
						      "columns of data are fields") }
#define GNM_HELP_ARG_FIELD { GNM_FUNC_HELP_ARG, F_("field:a string or integer specifying which " \
						   "field is to be used") }
#define GNM_HELP_ARG_CRITERIA { GNM_FUNC_HELP_ARG, F_("criteria:a range containing conditions" ) }

#define GNM_HELP_DESC_DATABASE { GNM_FUNC_HELP_DESCRIPTION, F_("@{database} is a range in which rows " \
							       "of related information are records and " \
							       "columns of data are fields. " \
							       "The first row of a database " \
							       "contains labels for each column.") }
#define GNM_HELP_DESC_FIELD { GNM_FUNC_HELP_DESCRIPTION, F_("@{field} is a string or integer specifying which " \
							    "field is to be used. If @{field} is an integer n " \
							    "then the nth column will be used. If @{field} " \
							    "is a string, then the column with the matching " \
							    "label will be used.") }
#define GNM_HELP_DESC_CRITERIA { GNM_FUNC_HELP_DESCRIPTION, F_("@{criteria} is a range containing conditions. " \
							       "The first row of a @{criteria} should contain "	\
							       "labels. Each label specifies to which field " \
							       "the conditions given in that column apply. " \
							       "Each cell below the label specifies a "	\
							       "condition such as \">3\" or \"<9\". An " \
							       "equality condition can be given by simply " \
							       "specifying a value, e. g. \"3\" or \"Jody\". " \
							       "For a record to be considered it must satisfy "	\
							       "all conditions in "	\
							       "at least one of the rows of @{criteria}.") }
#define GNM_HELP_EXAMPLE_DESC { GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the range A1:C7 contain " \
							   "the following values:\n\n" \
							   "Name    \tAge     \tSalary\n" \
							   "John    \t34      \t54342\n" \
							   "Bill    \t35      \t22343\n" \
							   "Clark   \t29      \t34323\n" \
							   "Bob     \t43      \t47242\n" \
							   "Susan   \t37      \t42932\n" \
							   "Jill    \t\t45      \t45324\n\n" \
							   "In addition, the cells A9:B11 contain the "	\
							   "following values:\n" \
							   "Age     \tSalary\n" \
							   "<30\n"	\
							   ">40     \t>46000\n") }

/***************************************************************************/

/**
 * find_cells_that_match :
 * Finds the cells from the given column that match the criteria.
 */

#warning  We should really be using find_rows_that_match from value.c

static GSList *
find_cells_that_match (Sheet *sheet, GnmValue const *database,
		       int col, GSList *criterias)
{
	GSList *ptr, *condition, *cells;
	int    row, first_row, last_row;
	gboolean add_flag;
	GnmCell *cell;
	int fake_col;
	GnmValue const *empty = value_new_empty ();

	cells = NULL;
	/* TODO : Why ignore the first row ?  What if there is no header ? */
	first_row = database->v_range.cell.a.row + 1;
	last_row  = database->v_range.cell.b.row;
	fake_col = database->v_range.cell.a.col;

	for (row = first_row; row <= last_row; row++) {
		cell = (col == -1)
			? sheet_cell_fetch (sheet, fake_col, row)
			: sheet_cell_get (sheet, col, row);

		if (cell != NULL)
			gnm_cell_eval (cell);

		if (col != -1 && gnm_cell_is_empty (cell))
			continue;

		add_flag = TRUE;
		for (ptr = criterias; ptr != NULL; ptr = ptr->next) {
			GnmDBCriteria const *current_criteria = ptr->data;

			add_flag = TRUE;
			condition = current_criteria->conditions;

			for (;condition != NULL ; condition = condition->next) {
				GnmCriteria *cond = condition->data;
				GnmCell *tmp = sheet_cell_get (sheet,
					cond->column, row);
				if (tmp != NULL)
					gnm_cell_eval (tmp);
				if (!cond->fun (tmp ? tmp->value : empty, cond)) {
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

	if (flags & ~(COLLECT_IGNORE_STRINGS |
		      COLLECT_IGNORE_BOOLS |
		      COLLECT_IGNORE_BLANKS |
		      COLLECT_IGNORE_ERRORS)) {
		g_warning ("unsupported flags in database_find_values %x", flags);
	}

	*error = NULL;

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
		if ((flags & COLLECT_IGNORE_ERRORS) && VALUE_IS_ERROR (value))
			continue;
		if (floats) {
			if (VALUE_IS_ERROR (value)) {
				// We're strict.
				*error = value_dup (value);
				g_free (res);
				res = NULL;
				break;
			}
			res1[count++] = value_get_as_float (value);
		} else {
			res2[count++] = value;
		}
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

	/* I don't like this -- minimal fix for now.  509427 and 751988.  */
	if (!VALUE_IS_CELLRANGE (criteria) ||
	    !VALUE_IS_CELLRANGE (database))
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
			       GnmStdError func_error,
			       gboolean allow_missing_field)
{
	int fieldno;
	GSList *criterias = NULL;
	Sheet *sheet;
	int count;
	int err;
	GnmValue **vals = NULL;
	GnmValue *res;

	/* I don't like this -- minimal fix for now.  509427 and 751392.  */
	if (!VALUE_IS_CELLRANGE (criteria) ||
	    !VALUE_IS_CELLRANGE (database))
		return value_new_error_NUM (ei->pos);

	if (allow_missing_field && VALUE_IS_EMPTY (field)) {
		flags = 0;
		fieldno = -1;
	} else {
		fieldno = find_column_of_field (ei->pos, database, field);
		if (fieldno < 0)
			return value_new_error_NUM (ei->pos);
	}

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
	{ GNM_FUNC_HELP_NAME, F_("DAVERAGE:average of the values in @{field} in @{database}"
				 " belonging to records that match @{criteria}") },
	GNM_HELP_ARG_DATABASE,
	GNM_HELP_ARG_FIELD,
	GNM_HELP_ARG_CRITERIA,
	GNM_HELP_DESC_DATABASE,
	GNM_HELP_DESC_FIELD,
	GNM_HELP_DESC_CRITERIA,
	GNM_HELP_EXAMPLE_DESC,
	{ GNM_FUNC_HELP_EXAMPLES, F_("DAVERAGE(A1:C7, \"Salary\", A9:A11) equals 42296.3333.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("DAVERAGE(A1:C7, \"Age\", A9:A11) equals 39.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("DAVERAGE(A1:C7, \"Salary\", A9:B11) equals 40782.5.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("DAVERAGE(A1:C7, \"Age\", A9:B11) equals 36.") },
	{ GNM_FUNC_HELP_SEEALSO, "DCOUNT" },
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
	{ GNM_FUNC_HELP_NAME, F_("DCOUNT:count of numbers in @{field} in @{database}"
				 " belonging to records that match @{criteria}") },
	GNM_HELP_ARG_DATABASE,
	GNM_HELP_ARG_FIELD,
	GNM_HELP_ARG_CRITERIA,
	GNM_HELP_DESC_DATABASE,
	GNM_HELP_DESC_FIELD,
	GNM_HELP_DESC_CRITERIA,
	GNM_HELP_EXAMPLE_DESC,
	{ GNM_FUNC_HELP_EXAMPLES, F_("DCOUNT(A1:C7, \"Salary\", A9:A11) equals 3.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("DCOUNT(A1:C7, \"Salary\", A9:B11) equals 2.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("DCOUNT(A1:C7, \"Name\", A9:B11) equals 0.") },
	{ GNM_FUNC_HELP_SEEALSO, "DAVERAGE,DCOUNTA" },
	{ GNM_FUNC_HELP_END }
};

static int
range_count (G_GNUC_UNUSED GnmValue **xs, int n, GnmValue **res)
{
	*res = value_new_int (n);
	return 0;
}


static GnmValue *
gnumeric_dcount (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return database_value_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      range_count,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS |
					      COLLECT_IGNORE_ERRORS,
					      GNM_ERROR_UNKNOWN,
					      GNM_ERROR_NUM,
					      TRUE);
}

/***************************************************************************/

static GnmFuncHelp const help_dcounta[] = {
	{ GNM_FUNC_HELP_NAME, F_("DCOUNTA:count of cells with data in @{field} in @{database}"
				 " belonging to records that match @{criteria}") },
	GNM_HELP_ARG_DATABASE,
	GNM_HELP_ARG_FIELD,
	GNM_HELP_ARG_CRITERIA,
	GNM_HELP_DESC_DATABASE,
	GNM_HELP_DESC_FIELD,
	GNM_HELP_DESC_CRITERIA,
	GNM_HELP_EXAMPLE_DESC,
	{ GNM_FUNC_HELP_EXAMPLES, F_("DCOUNTA(A1:C7, \"Salary\", A9:A11) equals 3.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("DCOUNTA(A1:C7, \"Salary\", A9:B11) equals 2.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("DCOUNTA(A1:C7, \"Name\", A9:B11) equals 2.") },
	{ GNM_FUNC_HELP_SEEALSO, "DCOUNT" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_dcounta (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return database_value_range_function (ei,
					      argv[0],
					      argv[1],
					      argv[2],
					      range_count,
					      COLLECT_IGNORE_BLANKS,
					      GNM_ERROR_UNKNOWN,
					      GNM_ERROR_NUM,
					      TRUE);
}

/***************************************************************************/

static GnmFuncHelp const help_dget[] = {
	{ GNM_FUNC_HELP_NAME, F_("DGET:a value from @{field} in @{database}"
				 " belonging to records that match @{criteria}") },
	GNM_HELP_ARG_DATABASE,
	GNM_HELP_ARG_FIELD,
	GNM_HELP_ARG_CRITERIA,
	GNM_HELP_DESC_DATABASE,
	GNM_HELP_DESC_FIELD,
	GNM_HELP_DESC_CRITERIA,
	GNM_HELP_EXAMPLE_DESC,
	{ GNM_FUNC_HELP_NOTE, F_("If none of the records match the conditions, DGET returns #VALUE!") },
	{ GNM_FUNC_HELP_NOTE, F_("If more than one record match the conditions, DGET returns #NUM!") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("DGET(A1:C7, \"Salary\", A9:A10) equals 34323.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("DGET(A1:C7, \"Name\", A9:A10) equals \"Clark\".") },
	{ GNM_FUNC_HELP_SEEALSO, "DCOUNT" },
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
					      GNM_ERROR_NUM,
					      FALSE);
}

static GnmFuncHelp const help_dmax[] = {
	{ GNM_FUNC_HELP_NAME, F_("DMAX:largest number in @{field} in @{database}"
				 " belonging to a record that match @{criteria}") },
	GNM_HELP_ARG_DATABASE,
	GNM_HELP_ARG_FIELD,
	GNM_HELP_ARG_CRITERIA,
	GNM_HELP_DESC_DATABASE,
	GNM_HELP_DESC_FIELD,
	GNM_HELP_DESC_CRITERIA,
	GNM_HELP_EXAMPLE_DESC,
	{ GNM_FUNC_HELP_EXAMPLES, F_("DMAX(A1:C7, \"Salary\", A9:A11) equals 47242.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("DMAX(A1:C7, \"Age\", A9:A11) equals 45.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("DMAX(A1:C7, \"Age\", A9:B11) equals 43.") },
	{ GNM_FUNC_HELP_SEEALSO, "DMIN" },
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
	{ GNM_FUNC_HELP_NAME, F_("DMIN:smallest number in @{field} in @{database}"
				 " belonging to a record that match @{criteria}") },
	GNM_HELP_ARG_DATABASE,
	GNM_HELP_ARG_FIELD,
	GNM_HELP_ARG_CRITERIA,
	GNM_HELP_DESC_DATABASE,
	GNM_HELP_DESC_FIELD,
	GNM_HELP_DESC_CRITERIA,
	GNM_HELP_EXAMPLE_DESC,
	{ GNM_FUNC_HELP_EXAMPLES, F_("DMIN(A1:C7, \"Salary\", A9:B11) equals 34323.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("DMIN(A1:C7, \"Age\", A9:B11) equals 29.") },
	{ GNM_FUNC_HELP_SEEALSO, "DCOUNT" },
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
	{ GNM_FUNC_HELP_NAME, F_("DPRODUCT:product of all values in @{field} in @{database}"
				 " belonging to records that match @{criteria}") },
	GNM_HELP_ARG_DATABASE,
	GNM_HELP_ARG_FIELD,
	GNM_HELP_ARG_CRITERIA,
	GNM_HELP_DESC_DATABASE,
	GNM_HELP_DESC_FIELD,
	GNM_HELP_DESC_CRITERIA,
	GNM_HELP_EXAMPLE_DESC,
	{ GNM_FUNC_HELP_EXAMPLES, F_("DPRODUCT(A1:C7, \"Age\", A9:B11) equals 1247.") },
	{ GNM_FUNC_HELP_SEEALSO, "DSUM" },
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
	{ GNM_FUNC_HELP_NAME, F_("DSTDEV:sample standard deviation of the values in @{field} in @{database}"
				 " belonging to records that match @{criteria}") },
	GNM_HELP_ARG_DATABASE,
	GNM_HELP_ARG_FIELD,
	GNM_HELP_ARG_CRITERIA,
	GNM_HELP_DESC_DATABASE,
	GNM_HELP_DESC_FIELD,
	GNM_HELP_DESC_CRITERIA,
	GNM_HELP_EXAMPLE_DESC,
	{ GNM_FUNC_HELP_EXAMPLES, F_("DSTDEV(A1:C7, \"Age\", A9:B11) equals 9.89949.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("DSTDEV(A1:C7, \"Salary\", A9:B11) equals 9135.112506.") },
	{ GNM_FUNC_HELP_SEEALSO, "DSTDEVP" },
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
	{ GNM_FUNC_HELP_NAME, F_("DSTDEVP:standard deviation of the population of "
				 "values in @{field} in @{database}"
				 " belonging to records that match @{criteria}") },
	GNM_HELP_ARG_DATABASE,
	GNM_HELP_ARG_FIELD,
	GNM_HELP_ARG_CRITERIA,
	GNM_HELP_DESC_DATABASE,
	GNM_HELP_DESC_FIELD,
	GNM_HELP_DESC_CRITERIA,
	GNM_HELP_EXAMPLE_DESC,
	{ GNM_FUNC_HELP_EXAMPLES, F_("DSTDEVP(A1:C7, \"Age\", A9:B11) equals 7.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("DSTDEVP(A1:C7, \"Salary\", A9:B11) equals 6459.5.") },
	{ GNM_FUNC_HELP_SEEALSO, "DSTDEV" },
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
	{ GNM_FUNC_HELP_NAME, F_("DSUM:sum of the values in @{field} in @{database}"
				 " belonging to records that match @{criteria}") },
	GNM_HELP_ARG_DATABASE,
	GNM_HELP_ARG_FIELD,
	GNM_HELP_ARG_CRITERIA,
	GNM_HELP_DESC_DATABASE,
	GNM_HELP_DESC_FIELD,
	GNM_HELP_DESC_CRITERIA,
	GNM_HELP_EXAMPLE_DESC,
	{ GNM_FUNC_HELP_EXAMPLES, F_("DSUM(A1:C7, \"Age\", A9:B11) equals 72.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("DSUM(A1:C7, \"Salary\", A9:B11) equals 81565.") },
	{ GNM_FUNC_HELP_SEEALSO, "DPRODUCT" },
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
	{ GNM_FUNC_HELP_NAME, F_("DVAR:sample variance of the values in @{field} in @{database}"
				 " belonging to records that match @{criteria}") },
	GNM_HELP_ARG_DATABASE,
	GNM_HELP_ARG_FIELD,
	GNM_HELP_ARG_CRITERIA,
	GNM_HELP_DESC_DATABASE,
	GNM_HELP_DESC_FIELD,
	GNM_HELP_DESC_CRITERIA,
	GNM_HELP_EXAMPLE_DESC,
	{ GNM_FUNC_HELP_EXAMPLES, F_("DVAR(A1:C7, \"Age\", A9:B11) equals 98.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("DVAR(A1:C7, \"Salary\", A9:B11) equals 83450280.5.") },
	{ GNM_FUNC_HELP_SEEALSO, "DVARP" },
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
	{ GNM_FUNC_HELP_NAME, F_("DVARP:variance of the population of "
				 "values in @{field} in @{database}"
				 " belonging to records that match @{criteria}") },
	GNM_HELP_ARG_DATABASE,
	GNM_HELP_ARG_FIELD,
	GNM_HELP_ARG_CRITERIA,
	GNM_HELP_DESC_DATABASE,
	GNM_HELP_DESC_FIELD,
	GNM_HELP_DESC_CRITERIA,
	GNM_HELP_EXAMPLE_DESC,
	{ GNM_FUNC_HELP_EXAMPLES, F_("DVARP(A1:C7, \"Age\", A9:B11) equals 49.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("DVARP(A1:C7, \"Salary\", A9:B11) equals 41725140.25.") },
	{ GNM_FUNC_HELP_SEEALSO, "DVAR" },
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
	{ GNM_FUNC_HELP_NAME, F_("GETPIVOTDATA:summary data from a pivot table") },
	{ GNM_FUNC_HELP_ARG, F_("pivot_table:cell range containing the pivot table") },
	{ GNM_FUNC_HELP_ARG, F_("field_name:name of the field for which the summary data is requested") },
	{ GNM_FUNC_HELP_NOTE, F_("If the summary data is unavailable, GETPIVOTDATA returns #REF!") },
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
	{ "daverage", "rSr",
	  help_daverage,   gnumeric_daverage, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "dcount",   "rSr",
	  help_dcount,     gnumeric_dcount, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "dcounta",  "rSr",
	  help_dcounta,    gnumeric_dcounta, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "dget",     "rSr",
	  help_dget,       gnumeric_dget, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "dmax",     "rSr",
	  help_dmax,       gnumeric_dmax, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "dmin",     "rSr",
	  help_dmin,       gnumeric_dmin, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "dproduct", "rSr",
	  help_dproduct,   gnumeric_dproduct, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "dstdev",   "rSr",
	  help_dstdev,     gnumeric_dstdev, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "dstdevp",  "rSr",
	  help_dstdevp,    gnumeric_dstdevp, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "dsum",     "rSr",
	  help_dsum,       gnumeric_dsum, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "dvar",     "rSr",
	  help_dvar,       gnumeric_dvar, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "dvarp",    "rSr",
	  help_dvarp,      gnumeric_dvarp, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },

/* XL stores in lookup */
	{ "getpivotdata", "rs",
	  help_getpivotdata, gnumeric_getpivotdata, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_SUBSET, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

        {NULL}
};
