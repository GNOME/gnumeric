/*
 * fn-database.c:  Built in database functions and functions registration
 *
 * Author:
 *  Jukka-Pekka Iivonen (iivonen@iki.fi)
 */
#include <config.h>
#include <gnome.h>
#include "math.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"
#include "sheet.h"


/* Type definitions */

typedef (*condition_test_fun_t) (float_t x, float_t y);

typedef struct {
        condition_test_fun_t fun;
        float_t              x;
} condition_t;

typedef struct {
        int    column;
        GSList *conditions;
} database_criteria_t;


/* Callback functions */

static int
test_equal(float_t x, float_t y)
{
        if (x == y)
	        return 1;
	else
	        return 0;
}

static int
test_unequal(float_t x, float_t y)
{
        if (x != y)
	        return 1;
	else
	        return 0;
}

static int
test_less(float_t x, float_t y)
{
        if (x < y)
	        return 1;
	else
	        return 0;
}

static int
test_greater(float_t x, float_t y)
{
        if (x > y)
	        return 1;
	else
	        return 0;
}

static int
test_less_or_equal(float_t x, float_t y)
{
        if (x <= y)
	        return 1;
	else
	        return 0;
}

static int
test_greater_or_equal(float_t x, float_t y)
{
        if (x >= y)
	        return 1;
	else
	        return 0;
}


/* Finds a column index of a field. 
 */
static int
find_column_of_field(Value *database, Value *field)
{
        Sheet *sheet;
        Cell  *cell;
	gchar *field_name;
	int   begin_col, end_col, row, n, column;
	int   offset;

	offset = database->v.cell_range.cell_b.col -
	  database->v.cell_range.cell_a.col;

	if (field->type == VALUE_INTEGER) 
	        return value_get_as_int (field) + offset - 1;

	if (field->type != VALUE_STRING)
	        return -1;

	sheet = (Sheet *) database->v.cell_range.cell_a.sheet;
	field_name = value_string(field);
	column = -1;

	/* find the column that is labeled with `field_name' */
	begin_col = database->v.cell_range.cell_a.col;
	end_col = database->v.cell_range.cell_b.col;
	row = database->v.cell_range.cell_a.row;

	for (n=begin_col; n<=end_col; n++) {
	        cell = sheet_cell_get(sheet, n, row);
		if (cell == NULL)
		        continue;
		if (strcmp(field_name, cell_get_text(cell)) == 0) {
		        column = n;
			break;
		}
	}

	return column;
}

/* Frees the allocated memory.
 */
static void
free_criterias(GSList *criterias)
{
        GSList *list = criterias;

        while (criterias != NULL) {
	        database_criteria_t *criteria = criterias->data;

		g_slist_free(criteria->conditions);
	        criterias = criterias->next;
	}
	g_slist_free(list);
}

/* Parses the criteria cell range.
 */
static GSList *
parse_criteria(Value *database, Value *criteria)
{
	Sheet               *sheet;
	database_criteria_t *new_criteria;
	GSList              *criterias;
	GSList              *conditions;
	Cell                *cell;

        int   i, j;
	int   b_col, b_row, e_col, e_row;
	int   field_ind;

	sheet = (Sheet *) database->v.cell_range.cell_a.sheet;
	b_col = criteria->v.cell_range.cell_a.col;
	b_row = criteria->v.cell_range.cell_a.row;
	e_col = criteria->v.cell_range.cell_b.col;
	e_row = criteria->v.cell_range.cell_b.row;

	conditions = NULL;
	criterias = NULL;

	for (i=b_col; i<=e_col; i++) {
	        cell = sheet_cell_get(sheet, i, b_row);
		if (cell == NULL || cell->value == NULL)
		        continue;
		new_criteria = g_new(database_criteria_t, 1);
	        field_ind = find_column_of_field(database, cell->value);
		if (field_ind == -1) {
		        free_criterias(criterias);
		        return NULL;
		}
		new_criteria->column = field_ind;
		conditions = NULL;

	        for (j=b_row+1; j<=e_row; j++) {
		        condition_t *cond;
		        int         len;
			gchar       *cell_str;

			cell = sheet_cell_get(sheet, i, j);
			if (cell == NULL || cell->value == NULL)
			       continue;
			cell_str = cell_get_text(cell);
			cond = g_new(condition_t, 1);
		        if (strncmp(cell_str, "<=", 2) == 0) {
			       cond->fun =
				 (condition_test_fun_t) test_less_or_equal;
			       len=2;
			} else if (strncmp(cell_str, ">=", 2) == 0) {
			       cond->fun =
				 (condition_test_fun_t) test_greater_or_equal;
			       len=2;
			} else if (strncmp(cell_str, "<>", 2) == 0) {
			       cond->fun =
				 (condition_test_fun_t) test_unequal;
			       len=2;
			} else if (*cell_str == '<') {
			       cond->fun = (condition_test_fun_t) test_less;
			       len=1;
			} else if (*cell_str == '=') {
			       cond->fun = (condition_test_fun_t) test_equal;
			       len=1;
			} else if (*cell_str == '>') {
			       cond->fun = (condition_test_fun_t) test_greater;
			       len=1;
			} else {
			       free_criterias(criterias);
			       g_free(cond);
			       g_slist_free(conditions);
			       return NULL;
			}

			cond->x = atof(cell_str+len);
			conditions = g_slist_append(conditions, cond);
		}
		new_criteria->conditions = conditions;
		criterias = g_slist_append(criterias, new_criteria);
	}
	return criterias;
}

/* Finds the cells from the given column that match the criteria.
 */
static GSList *
find_cells_that_match(Value *database, int field, GSList *criterias)
{
        Sheet  *sheet;
	GSList *current, *conditions, *cells;
        int    row, first_row, last_row, add_flag;
	sheet = (Sheet *) database->v.cell_range.cell_a.sheet;
	last_row = database->v.cell_range.cell_b.row;
	cells = NULL;
	first_row = database->v.cell_range.cell_a.row + 1;

	for (row=first_row; row<=last_row; row++) {
	       GSList *new_item;
	       Cell   *cell, *test_cell;

	       cell = sheet_cell_get(sheet, field, row);
	       if (cell == NULL || cell->value == NULL)
		       continue;
	       current = criterias;
	       add_flag = 0;
	       for (current = criterias; current != NULL;
		    current=current->next) {
		       database_criteria_t *current_criteria;
		       float_t y;

		       current_criteria = current->data;
		       test_cell = sheet_cell_get(sheet, 
						  current_criteria->column,
						  row);
		       if (test_cell == NULL || test_cell->value == NULL)
			       continue;
		       y = value_get_as_double(test_cell->value);
		       conditions = current_criteria->conditions;
		       add_flag = 0;
		       while (conditions != NULL) {
			       condition_t *cond = conditions->data;

			       if (cond->fun(y, cond->x)) {
				       add_flag = 1;
				       break;
			       }
			       conditions = conditions->next;
		       }
		       if (add_flag == 0)
			       break;
	       }
	       if (add_flag) {
		       cells = g_slist_append(cells, cell);
	       }
	}
	return cells;
}


static char *help_daverage = {
        N_("@FUNCTION=DAVERAGE\n"
           "@SYNTAX=DAVERAGE(database,field,criteria)\n"

           "@DESCRIPTION="
           "DAVERAGE function returns the average of the values in a list "
	   "or database that match conditions specified. "
	   "\n"
	   "@database is a range of cells in which rows of related "
	   "information are records and columns of data are fields. "
	   "The first row of a database contains labels for each column. "
	   "\n"
	   "@field specifies which column is used in the function. If @field "
	   "is an integer, i.e. 2, the second column is used. Field can "
	   "also be the label of a column. "
	   "\n"
	   "@criteria is the range of cells which contains the specified "
	   "conditions. The first row of a criteria should contain the labels "
	   "of the fields for which the criterias are for. Cells below the "
	   "label specify coditions, for example, ``>3'' or ``<9''. "
           "\n"
           "@SEEALSO=DCOUNT")
};

static Value *
gnumeric_daverage (struct FunctionDefinition *i,
		   Value *argv [], char **error_string)
{
        Value       *database, *criteria;
	GSList      *criterias;
	GSList      *cells, *current;
	int         field;
	float_t     sum;
	int         count;

	database = argv[0];
	criteria = argv[2];

	field = find_column_of_field(database, argv[1]);
	if (field < 0) {
		*error_string = _("#NUM!");
		return NULL;
	}
	criterias = parse_criteria(database, criteria);
	if (criterias == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	cells = find_cells_that_match(database, field, criterias);

	current = cells;
	count = 0;
	sum = 0;

	while (current != NULL) {
	        Cell *cell = current->data;

	        count++;
		sum += value_get_as_double(cell->value);
		current = g_slist_next(current);
	}

	g_slist_free(cells);
	free_criterias(criterias);

        return value_float (sum / count);
}

static char *help_dcount = {
        N_("@FUNCTION=DCOUNT\n"
           "@SYNTAX=DCOUNT(database,field,criteria)\n"

           "@DESCRIPTION="
           "DCOUNT function counts the cells that contain numbers in a "
	   "database that match conditions specified. "
	   "\n"
	   "@database is a range of cells in which rows of related "
	   "information are records and columns of data are fields. "
	   "The first row of a database contains labels for each column. "
	   "\n"
	   "@field specifies which column is used in the function. If @field "
	   "is an integer, i.e. 2, the second column is used. Field can "
	   "also be the label of a column. "
	   "\n"
	   "@criteria is the range of cells which contains the specified "
	   "conditions. The first row of a criteria should contain the labels "
	   "of the fields for which the criterias are for. Cells below the "
	   "label specify coditions, for example, ``>3'' or ``<9''. "
           "\n"
           "@SEEALSO=DAVERAGE")
};

static Value *
gnumeric_dcount (struct FunctionDefinition *i,
		 Value *argv [], char **error_string)
{
        Value       *database, *criteria;
	GSList      *criterias;
	GSList      *cells, *current;
	int         field;
	int         count;

	database = argv[0];
	criteria = argv[2];

	field = find_column_of_field(database, argv[1]);
	if (field < 0) {
		*error_string = _("#NUM!");
		return NULL;
	}
	criterias = parse_criteria(database, criteria);
	if (criterias == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	cells = find_cells_that_match(database, field, criterias);

	current = cells;
	count = 0;

	while (current != NULL) {
	        count++;
		current = g_slist_next(current);
	}

	g_slist_free(cells);
	free_criterias(criterias);

        return value_int (count);
}

static char *help_dget = {
        N_("@FUNCTION=DGET\n"
           "@SYNTAX=DGET(database,field,criteria)\n"

           "@DESCRIPTION="
           "DGET function returns a single value from a column that "
	   "match conditions specified. "
	   "\n"
	   "@database is a range of cells in which rows of related "
	   "information are records and columns of data are fields. "
	   "The first row of a database contains labels for each column. "
	   "\n"
	   "@field specifies which column is used in the function. If @field "
	   "is an integer, i.e. 2, the second column is used. Field can "
	   "also be the label of a column. "
	   "\n"
	   "@criteria is the range of cells which contains the specified "
	   "conditions. The first row of a criteria should contain the labels "
	   "of the fields for which the criterias are for. Cells below the "
	   "label specify coditions, for example, ``>3'' or ``<9''. "
           "\n"
	   "If none of the items match the conditions, DGET returns #VALUE! "
	   "error. "
	   "If more than one items match the conditions, DGET returns #NUM! "
	   "error. "
	   "\n"
           "@SEEALSO=DCOUNT")
};

static Value *
gnumeric_dget (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
        Value       *database, *criteria;
	GSList      *criterias;
	GSList      *cells, *current;
	int         field;
	Cell        *cell = NULL;
	int         count;

	database = argv[0];
	criteria = argv[2];

	field = find_column_of_field(database, argv[1]);

	if (field < 0) {
		*error_string = _("#NUM!");
		return NULL;
	}

	criterias = parse_criteria(database, criteria);

	if (criterias == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}

	cells = find_cells_that_match(database, field, criterias);

	current = cells;
	count = 0;

	while (current != NULL) {
	        cell = current->data;
	        count++;
		current = g_slist_next(current);
	}

	g_slist_free(cells);
	free_criterias(criterias);

	if (count == 0) {
		*error_string = _("#VALUE!");
		return NULL;
	}
	if (count > 1) {
		*error_string = _("#NUM!");
		return NULL;
	}

        return value_float (value_get_as_double(cell->value));
}

static char *help_dmax = {
        N_("@FUNCTION=DMAX\n"
           "@SYNTAX=DMAX(database,field,criteria)\n"

           "@DESCRIPTION="
           "DMAX function returns the largest number in a column that "
	   "match conditions specified. "
	   "\n"
	   "@database is a range of cells in which rows of related "
	   "information are records and columns of data are fields. "
	   "The first row of a database contains labels for each column. "
	   "\n"
	   "@field specifies which column is used in the function. If @field "
	   "is an integer, i.e. 2, the second column is used. Field can "
	   "also be the label of a column. "
	   "\n"
	   "@criteria is the range of cells which contains the specified "
	   "conditions. The first row of a criteria should contain the labels "
	   "of the fields for which the criterias are for. Cells below the "
	   "label specify coditions, for example, ``>3'' or ``<9''. "
           "\n"
           "@SEEALSO=DMIN")
};

static Value *
gnumeric_dmax (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
        Value       *database, *criteria;
	GSList      *criterias;
	GSList      *cells, *current;
	Cell        *cell;
	int         field;
	float_t     max;

	database = argv[0];
	criteria = argv[2];

	field = find_column_of_field(database, argv[1]);
	if (field < 0) {
		*error_string = _("#NUM!");
		return NULL;
	}
	criterias = parse_criteria(database, criteria);
	if (criterias == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	cells = find_cells_that_match(database, field, criterias);
	if (cells == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	current = cells;
	cell = current->data;
	max = value_get_as_double (cell->value);

	while (current != NULL) {
	        float_t v;

	        cell = current->data;
		v = value_get_as_double (cell->value);
		if (max < v)
		        max = v;
		current = g_slist_next(current);
	}

	g_slist_free(cells);
	free_criterias(criterias);

        return value_float (max);
}

static char *help_dmin = {
        N_("@FUNCTION=DMIN\n"
           "@SYNTAX=DMIN(database,field,criteria)\n"

           "@DESCRIPTION="
           "DMIN function returns the smallest number in a column that "
	   "match conditions specified. "
	   "\n"
	   "@database is a range of cells in which rows of related "
	   "information are records and columns of data are fields. "
	   "The first row of a database contains labels for each column. "
	   "\n"
	   "@field specifies which column is used in the function. If @field "
	   "is an integer, i.e. 2, the second column is used. Field can "
	   "also be the label of a column. "
	   "\n"
	   "@criteria is the range of cells which contains the specified "
	   "conditions. The first row of a criteria should contain the labels "
	   "of the fields for which the criterias are for. Cells below the "
	   "label specify coditions, for example, ``>3'' or ``<9''. "
           "\n"
           "@SEEALSO=DMAX")
};

static Value *
gnumeric_dmin (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
        Value       *database, *criteria;
	GSList      *criterias;
	GSList      *cells, *current;
	Cell        *cell;
	int         field;
	float_t     min;

	database = argv[0];
	criteria = argv[2];

	field = find_column_of_field(database, argv[1]);
	if (field < 0) {
		*error_string = _("#NUM!");
		return NULL;
	}
	criterias = parse_criteria(database, criteria);
	if (criterias == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	cells = find_cells_that_match(database, field, criterias);
	if (cells == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	current = cells;
	cell = current->data;
	min = value_get_as_double (cell->value);

	while (current != NULL) {
	        float_t v;

	        cell = current->data;
		v = value_get_as_double (cell->value);
		if (min > v)
		        min = v;
		current = g_slist_next(current);
	}

	g_slist_free(cells);
	free_criterias(criterias);

        return value_float (min);
}

static char *help_dproduct = {
        N_("@FUNCTION=DPRODUCT\n"
           "@SYNTAX=DPRODUCT(database,field,criteria)\n"

           "@DESCRIPTION="
           "DPRODUCT function returns the product of numbers in a column "
	   "that match conditions specified. "
	   "\n"
	   "@database is a range of cells in which rows of related "
	   "information are records and columns of data are fields. "
	   "The first row of a database contains labels for each column. "
	   "\n"
	   "@field specifies which column is used in the function. If @field "
	   "is an integer, i.e. 2, the second column is used. Field can "
	   "also be the label of a column. "
	   "\n"
	   "@criteria is the range of cells which contains the specified "
	   "conditions. The first row of a criteria should contain the labels "
	   "of the fields for which the criterias are for. Cells below the "
	   "label specify coditions, for example, ``>3'' or ``<9''. "
           "\n"
           "@SEEALSO=DSUM")
};

static Value *
gnumeric_dproduct (struct FunctionDefinition *i,
		   Value *argv [], char **error_string)
{
        Value       *database, *criteria;
	GSList      *criterias;
	GSList      *cells, *current;
	Cell        *cell;
	int         field;
	float_t     product;

	database = argv[0];
	criteria = argv[2];

	field = find_column_of_field(database, argv[1]);
	if (field < 0) {
		*error_string = _("#NUM!");
		return NULL;
	}
	criterias = parse_criteria(database, criteria);
	if (criterias == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	cells = find_cells_that_match(database, field, criterias);
	if (cells == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	current = cells;
	product = 1;
	cell = current->data;

	while (current != NULL) {
	        float_t v;

	        cell = current->data;
		v = value_get_as_double (cell->value);
		product *= v;
		current = g_slist_next(current);
	}

	g_slist_free(cells);
	free_criterias(criterias);

        return value_float (product);
}

static char *help_dstdev = {
        N_("@FUNCTION=DSTDEV\n"
           "@SYNTAX=DSTDEV(database,field,criteria)\n"

           "@DESCRIPTION="
           "DSTDEV function returns the estimate of the standard deviation "
	   "of a population based on a sample. The populations consists of "
	   "numbers that match conditions specified. "
	   "\n"
	   "@database is a range of cells in which rows of related "
	   "information are records and columns of data are fields. "
	   "The first row of a database contains labels for each column. "
	   "\n"
	   "@field specifies which column is used in the function. If @field "
	   "is an integer, i.e. 2, the second column is used. Field can "
	   "also be the label of a column. "
	   "\n"
	   "@criteria is the range of cells which contains the specified "
	   "conditions. The first row of a criteria should contain the labels "
	   "of the fields for which the criterias are for. Cells below the "
	   "label specify coditions, for example, ``>3'' or ``<9''. "
           "\n"
           "@SEEALSO=DSTDEVP")
};

static Value *
gnumeric_dstdev (struct FunctionDefinition *i,
		 Value *argv [], char **error_string)
{
        Value          *database, *criteria;
	GSList         *criterias;
	GSList         *cells, *current;
	Cell           *cell;
	int            field;
	stat_closure_t p;

	database = argv[0];
	criteria = argv[2];

	field = find_column_of_field(database, argv[1]);
	if (field < 0) {
		*error_string = _("#NUM!");
		return NULL;
	}
	criterias = parse_criteria(database, criteria);
	if (criterias == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	cells = find_cells_that_match(database, field, criterias);
	if (cells == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	current = cells;
	setup_stat_closure (&p);
	cell = current->data;

	while (current != NULL) {
	        char *error_str;
	        cell = current->data;
		callback_function_stat (NULL, cell->value, &error_str, &p);
		current = g_slist_next(current);
	}

	g_slist_free(cells);
	free_criterias(criterias);

	if (p.N - 1 == 0) {
		*error_string = _("#NUM!");
		return NULL;
	}

        return value_float (sqrt(p.Q / (p.N - 1)));
}

static char *help_dstdevp = {
        N_("@FUNCTION=DSTDEVP\n"
           "@SYNTAX=DSTDEVP(database,field,criteria)\n"

           "@DESCRIPTION="
           "DSTDEVP function returns the standard deviation of a population "
	   "based on the entire populations. The populations consists of "
	   "numbers that match conditions specified. "
	   "\n"
	   "@database is a range of cells in which rows of related "
	   "information are records and columns of data are fields. "
	   "The first row of a database contains labels for each column. "
	   "\n"
	   "@field specifies which column is used in the function. If @field "
	   "is an integer, i.e. 2, the second column is used. Field can "
	   "also be the label of a column. "
	   "\n"
	   "@criteria is the range of cells which contains the specified "
	   "conditions. The first row of a criteria should contain the labels "
	   "of the fields for which the criterias are for. Cells below the "
	   "label specify coditions, for example, ``>3'' or ``<9''. "
           "\n"
           "@SEEALSO=DSTDEV")
};

static Value *
gnumeric_dstdevp (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
        Value          *database, *criteria;
	GSList         *criterias;
	GSList         *cells, *current;
	Cell           *cell;
	int            field;
	stat_closure_t p;

	database = argv[0];
	criteria = argv[2];

	field = find_column_of_field(database, argv[1]);
	if (field < 0) {
		*error_string = _("#NUM!");
		return NULL;
	}
	criterias = parse_criteria(database, criteria);
	if (criterias == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	cells = find_cells_that_match(database, field, criterias);
	if (cells == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	current = cells;
	setup_stat_closure (&p);
	cell = current->data;

	while (current != NULL) {
	        char *error_str;
	        cell = current->data;
		callback_function_stat (NULL, cell->value, &error_str, &p);
		current = g_slist_next(current);
	}

	g_slist_free(cells);
	free_criterias(criterias);

	if (p.N == 0) {
		*error_string = _("#NUM!");
		return NULL;
	}

        return value_float (sqrt(p.Q / p.N));
}

static char *help_dsum = {
        N_("@FUNCTION=DSUM\n"
           "@SYNTAX=DSUM(database,field,criteria)\n"

           "@DESCRIPTION="
           "DSUM function returns the sum of numbers in a column "
	   "that match conditions specified. "
	   "\n"
	   "@database is a range of cells in which rows of related "
	   "information are records and columns of data are fields. "
	   "The first row of a database contains labels for each column. "
	   "\n"
	   "@field specifies which column is used in the function. If @field "
	   "is an integer, i.e. 2, the second column is used. Field can "
	   "also be the label of a column. "
	   "\n"
	   "@criteria is the range of cells which contains the specified "
	   "conditions. The first row of a criteria should contain the labels "
	   "of the fields for which the criterias are for. Cells below the "
	   "label specify coditions, for example, ``>3'' or ``<9''. "
           "\n"
           "@SEEALSO=DPRODUCT")
};

static Value *
gnumeric_dsum (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
        Value       *database, *criteria;
	GSList      *criterias;
	GSList      *cells, *current;
	Cell        *cell;
	int         field;
	float_t     sum;

	database = argv[0];
	criteria = argv[2];

	field = find_column_of_field(database, argv[1]);
	if (field < 0) {
		*error_string = _("#NUM!");
		return NULL;
	}
	criterias = parse_criteria(database, criteria);
	if (criterias == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	cells = find_cells_that_match(database, field, criterias);
	if (cells == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	current = cells;
	sum = 0;
	cell = current->data;

	while (current != NULL) {
	        float_t v;

	        cell = current->data;
		v = value_get_as_double (cell->value);
		sum += v;
		current = g_slist_next(current);
	}

	g_slist_free(cells);
	free_criterias(criterias);

        return value_float (sum);
}

static char *help_dvar = {
        N_("@FUNCTION=DVAR\n"
           "@SYNTAX=DVAR(database,field,criteria)\n"

           "@DESCRIPTION="
           "DVAR function returns the estimate of variance of a population "
	   "based on a sample. The populations consists of numbers "
	   "that match conditions specified. "
	   "\n"
	   "@database is a range of cells in which rows of related "
	   "information are records and columns of data are fields. "
	   "The first row of a database contains labels for each column. "
	   "\n"
	   "@field specifies which column is used in the function. If @field "
	   "is an integer, i.e. 2, the second column is used. Field can "
	   "also be the label of a column. "
	   "\n"
	   "@criteria is the range of cells which contains the specified "
	   "conditions. The first row of a criteria should contain the labels "
	   "of the fields for which the criterias are for. Cells below the "
	   "label specify coditions, for example, ``>3'' or ``<9''. "
           "\n"
           "@SEEALSO=DVARP")
};

static Value *
gnumeric_dvar (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
        Value          *database, *criteria;
	GSList         *criterias;
	GSList         *cells, *current;
	Cell           *cell;
	int            field;
	stat_closure_t p;

	database = argv[0];
	criteria = argv[2];

	field = find_column_of_field(database, argv[1]);
	if (field < 0) {
		*error_string = _("#NUM!");
		return NULL;
	}
	criterias = parse_criteria(database, criteria);
	if (criterias == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	cells = find_cells_that_match(database, field, criterias);
	if (cells == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	current = cells;
	setup_stat_closure (&p);
	cell = current->data;

	while (current != NULL) {
	        char *error_str;
	        cell = current->data;
		callback_function_stat (NULL, cell->value, &error_str, &p);
		current = g_slist_next(current);
	}

	g_slist_free(cells);
	free_criterias(criterias);

	if (p.N - 1 == 0) {
		*error_string = _("#NUM!");
		return NULL;
	}

        return value_float (p.Q / (p.N - 1));
}

static char *help_dvarp = {
        N_("@FUNCTION=DVARP\n"
           "@SYNTAX=DVARP(database,field,criteria)\n"

           "@DESCRIPTION="
           "DVARP function returns the variance of a population based "
	   "on the entire populations. The populations consists of numbers "
	   "that match conditions specified. "
	   "\n"
	   "@database is a range of cells in which rows of related "
	   "information are records and columns of data are fields. "
	   "The first row of a database contains labels for each column. "
	   "\n"
	   "@field specifies which column is used in the function. If @field "
	   "is an integer, i.e. 2, the second column is used. Field can "
	   "also be the label of a column. "
	   "\n"
	   "@criteria is the range of cells which contains the specified "
	   "conditions. The first row of a criteria should contain the labels "
	   "of the fields for which the criterias are for. Cells below the "
	   "label specify coditions, for example, ``>3'' or ``<9''. "
           "\n"
           "@SEEALSO=DVAR")
};

static Value *
gnumeric_dvarp (struct FunctionDefinition *i,
		Value *argv [], char **error_string)
{
        Value          *database, *criteria;
	GSList         *criterias;
	GSList         *cells, *current;
	Cell           *cell;
	int            field;
	stat_closure_t p;

	database = argv[0];
	criteria = argv[2];

	field = find_column_of_field(database, argv[1]);
	if (field < 0) {
		*error_string = _("#NUM!");
		return NULL;
	}
	criterias = parse_criteria(database, criteria);
	if (criterias == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	cells = find_cells_that_match(database, field, criterias);
	if (cells == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	current = cells;
	setup_stat_closure (&p);
	cell = current->data;

	while (current != NULL) {
	        char *error_str;
	        cell = current->data;
		callback_function_stat (NULL, cell->value, &error_str, &p);
		current = g_slist_next(current);
	}

	g_slist_free(cells);
	free_criterias(criterias);

	if (p.N == 0) {
		*error_string = _("#NUM!");
		return NULL;
	}

        return value_float (p.Q / p.N);
}


FunctionDefinition database_functions [] = {
	{ "daverage", "r?r", "database,field,criteria", &help_daverage,
	  NULL, gnumeric_daverage },
	{ "dcount",   "r?r", "database,field,criteria", &help_dcount,
	  NULL, gnumeric_dcount },
	{ "dget",     "r?r", "database,field,criteria", &help_dget,
	  NULL, gnumeric_dget },
	{ "dmax",     "r?r", "database,field,criteria", &help_dmax,
	  NULL, gnumeric_dmax },
	{ "dmin",     "r?r", "database,field,criteria", &help_dmin,
	  NULL, gnumeric_dmin },
	{ "dproduct", "r?r", "database,field,criteria", &help_dproduct,
	  NULL, gnumeric_dproduct },
	{ "dstdev",   "r?r", "database,field,criteria", &help_dstdev,
	  NULL, gnumeric_dstdev },
	{ "dstdevp",  "r?r", "database,field,criteria", &help_dstdevp,
	  NULL, gnumeric_dstdevp },
	{ "dsum",     "r?r", "database,field,criteria", &help_dsum,
	  NULL, gnumeric_dsum },
	{ "dvar",     "r?r", "database,field,criteria", &help_dvar,
	  NULL, gnumeric_dvar },
	{ "dvarp",    "r?r", "database,field,criteria", &help_dvarp,
	  NULL, gnumeric_dvarp },
	{ NULL, NULL },
};

