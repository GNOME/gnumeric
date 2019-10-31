#include <criteria.h>

typedef enum { CRIT_NULL, CRIT_FLOAT, CRIT_WRONGTYPE, CRIT_STRING } CritType;

static CritType
criteria_inspect_values (GnmValue const *x, gnm_float *xr, gnm_float *yr,
			 GnmCriteria *crit, gboolean coerce_to_float)
{
	GnmValue const *y = crit->x;

	if (x == NULL || y == NULL)
		return CRIT_NULL;

	switch (y->v_any.type) {
	case VALUE_BOOLEAN:
		/* If we're searching for a bool -- even one that is
		   from a string search value -- we match only bools.  */
		if (!VALUE_IS_BOOLEAN (x))
			return CRIT_WRONGTYPE;
		*xr = value_get_as_float (x);
		*yr = value_get_as_float (y);
		return CRIT_FLOAT;

	case VALUE_EMPTY:
		return CRIT_WRONGTYPE;

	case VALUE_STRING:
		if (!VALUE_IS_STRING (x))
			return CRIT_WRONGTYPE;
		return CRIT_STRING;

	default:
		g_warning ("This should not happen.  Please report.");
		return CRIT_WRONGTYPE;

	case VALUE_FLOAT: {
		GnmValue *vx;
		*yr = value_get_as_float (y);

		if (VALUE_IS_BOOLEAN (x) || VALUE_IS_ERROR (x))
			return CRIT_WRONGTYPE;
		else if (VALUE_IS_FLOAT (x)) {
			*xr = value_get_as_float (x);
			return CRIT_FLOAT;
		}

		if (!coerce_to_float)
			return CRIT_WRONGTYPE;

		vx = format_match (value_peek_string (x), NULL, crit->date_conv);
		if (VALUE_IS_EMPTY (vx) ||
		    VALUE_IS_BOOLEAN (y) != VALUE_IS_BOOLEAN (vx)) {
			value_release (vx);
			return CRIT_WRONGTYPE;
		}

		*xr = value_get_as_float (vx);
		value_release (vx);
		return CRIT_FLOAT;
	}
	}
}


static gboolean
criteria_test_equal (GnmValue const *x, GnmCriteria *crit)
{
	gnm_float xf, yf;
	GnmValue const *y = crit->x;

	switch (criteria_inspect_values (x, &xf, &yf, crit, TRUE)) {
	default:
		g_assert_not_reached ();
	case CRIT_NULL:
	case CRIT_WRONGTYPE:
		return FALSE;
	case CRIT_FLOAT:
		return xf == yf;
	case CRIT_STRING:
		/* FIXME: _ascii_??? */
		return g_ascii_strcasecmp (value_peek_string (x),
					   value_peek_string (y)) == 0;
	}
}

static gboolean
criteria_test_unequal (GnmValue const *x, GnmCriteria *crit)
{
	gnm_float xf, yf;

	switch (criteria_inspect_values (x, &xf, &yf, crit, FALSE)) {
	default:
		g_assert_not_reached ();
	case CRIT_NULL:
	case CRIT_WRONGTYPE:
		return TRUE;
	case CRIT_FLOAT:
		return xf != yf;
	case CRIT_STRING:
		/* FIXME: _ascii_??? */
		return g_ascii_strcasecmp (value_peek_string (x),
					   value_peek_string (crit->x)) != 0;
	}
}

static gboolean
criteria_test_less (GnmValue const *x, GnmCriteria *crit)
{
	gnm_float xf, yf;
	GnmValue const *y = crit->x;

	switch (criteria_inspect_values (x, &xf, &yf, crit, FALSE)) {
	default:
		g_assert_not_reached ();
	case CRIT_NULL:
	case CRIT_WRONGTYPE:
		return FALSE;
	case CRIT_STRING:
		return go_utf8_collate_casefold (value_peek_string (x),
						 value_peek_string (y)) < 0;
	case CRIT_FLOAT:
		return xf < yf;
	}
}

static gboolean
criteria_test_greater (GnmValue const *x, GnmCriteria *crit)
{
	gnm_float xf, yf;
	GnmValue const *y = crit->x;

	switch (criteria_inspect_values (x, &xf, &yf, crit, FALSE)) {
	default:
		g_assert_not_reached ();
	case CRIT_NULL:
	case CRIT_WRONGTYPE:
		return FALSE;
	case CRIT_STRING:
		return go_utf8_collate_casefold (value_peek_string (x),
						 value_peek_string (y)) > 0;
	case CRIT_FLOAT:
		return xf > yf;
	}
}

static gboolean
criteria_test_less_or_equal (GnmValue const *x, GnmCriteria *crit)
{
	gnm_float xf, yf;
	GnmValue const *y = crit->x;

	switch (criteria_inspect_values (x, &xf, &yf, crit, FALSE)) {
	default:
		g_assert_not_reached ();
	case CRIT_NULL:
	case CRIT_WRONGTYPE:
		return FALSE;
	case CRIT_STRING:
		return go_utf8_collate_casefold (value_peek_string (x),
						 value_peek_string (y)) <= 0;
	case CRIT_FLOAT:
		return xf <= yf;
	}
}

static gboolean
criteria_test_greater_or_equal (GnmValue const *x, GnmCriteria *crit)
{
	gnm_float xf, yf;
	GnmValue const *y = crit->x;

	switch (criteria_inspect_values (x, &xf, &yf, crit, FALSE)) {
	default:
		g_assert_not_reached ();
	case CRIT_NULL:
	case CRIT_WRONGTYPE:
		return FALSE;
	case CRIT_STRING:
		return go_utf8_collate_casefold (value_peek_string (x),
						 value_peek_string (y)) >= 0;
	case CRIT_FLOAT:
		return xf >= yf;
	}
}

static gboolean
criteria_test_match (GnmValue const *x, GnmCriteria *crit)
{
	if (!crit->has_rx)
		return FALSE;

	// Only strings are matched
	if (!VALUE_IS_STRING (x))
		return FALSE;

	return go_regexec (&crit->rx, value_peek_string (x), 0, NULL, 0) ==
		GO_REG_OK;
}

static gboolean
criteria_test_empty (GnmValue const *x, GnmCriteria *crit)
{
	return VALUE_IS_EMPTY (x);
}

static gboolean
criteria_test_blank (GnmValue const *x, GnmCriteria *crit)
{
	if (VALUE_IS_EMPTY (x))
		return TRUE;
	if (!VALUE_IS_STRING (x))
		return FALSE;
	return *value_peek_string (x) == 0;
}

static gboolean
criteria_test_nonempty (GnmValue const *x, GnmCriteria *crit)
{
	return !VALUE_IS_EMPTY (x);
}

static gboolean
criteria_test_nothing (GnmValue const *x, GnmCriteria *crit)
{
	return FALSE;
}

/*
 * Finds a column index of a field.
 */
int
find_column_of_field (GnmEvalPos const *ep,
		      GnmValue const *database, GnmValue const *field)
{
        Sheet *sheet;
        GnmCell  *cell;
	gchar *field_name;
	int   begin_col, end_col, row, n, column;
	int   offset;

	// I'm not certain we should demand this, but the code clearly wants
	// it.
	if (!VALUE_IS_CELLRANGE (database))
		return -1;

	offset = database->v_range.cell.a.col;

	if (VALUE_IS_FLOAT (field))
		return value_get_as_int (field) + offset - 1;

	if (!VALUE_IS_STRING (field))
		return -1;

	sheet = eval_sheet (database->v_range.cell.a.sheet, ep->sheet);
	field_name = value_get_as_string (field);
	column = -1;

	/* find the column that is labeled after `field_name' */
	begin_col = database->v_range.cell.a.col;
	end_col = database->v_range.cell.b.col;
	row = database->v_range.cell.a.row;

	for (n = begin_col; n <= end_col; n++) {
		char const *txt;
		gboolean match;

		cell = sheet_cell_get (sheet, n, row);
		if (cell == NULL)
			continue;
		gnm_cell_eval (cell);

		txt = cell->value
			? value_peek_string (cell->value)
			: "";
		match = (g_ascii_strcasecmp (field_name, txt) == 0);
		if (match) {
			column = n;
			break;
		}
	}

	g_free (field_name);
	return column;
}

void
gnm_criteria_unref (GnmCriteria *criteria)
{
	if (!criteria || criteria->ref_count-- > 1)
		return;
	value_release (criteria->x);
	if (criteria->has_rx)
		go_regfree (&criteria->rx);
	g_free (criteria);
}

static GnmCriteria *
gnm_criteria_ref (GnmCriteria *criteria)
{
	criteria->ref_count++;
	return criteria;
}

GType
gnm_criteria_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmCriteria",
			 (GBoxedCopyFunc)gnm_criteria_ref,
			 (GBoxedFreeFunc)gnm_criteria_unref);
	}
	return t;
}

/**
 * free_criterias:
 * @criterias: (element-type GnmCriteria) (transfer full): the criteria to be
 * freed.
 * Frees the allocated memory.
 */
void
free_criterias (GSList *criterias)
{
        GSList *list = criterias;

        while (criterias != NULL) {
		GnmDBCriteria *criteria = criterias->data;
		g_slist_free_full (criteria->conditions,
				      (GFreeFunc)gnm_criteria_unref);
		g_free (criteria);
		criterias = criterias->next;
	}
	g_slist_free (list);
}

/**
 * parse_criteria:
 * @crit_val: #GnmValue
 * @date_conv: #GODateConventions
 *
 * Returns: (transfer full): GnmCriteria which caller must free.
 *
 * ">=value"
 * "<=value"
 * "<>value"
 * "<value"
 * ">value"
 * "=value"
 * "pattern"
 **/
GnmCriteria *
parse_criteria (GnmValue const *crit_val, GODateConventions const *date_conv,
		gboolean anchor_end)
{
	int len;
	char const *criteria;
	GnmCriteria *res = g_new0 (GnmCriteria, 1);
	GnmValue *empty;

	res->iter_flags = CELL_ITER_IGNORE_BLANK;
	res->date_conv = date_conv;
	res->ref_count = 1;

	if (VALUE_IS_NUMBER (crit_val)) {
		res->fun = criteria_test_equal;
		res->x = value_dup (crit_val);
		return res;
	}

	if (VALUE_IS_EMPTY (crit_val)) {
		// Empty value
		res->fun = criteria_test_nothing;
		res->x = value_new_empty ();
		return res;
	}

	criteria = value_peek_string (crit_val);
	if (*criteria == 0) {
		res->fun = criteria_test_blank;
		len = 0;
	} else if (strncmp (criteria, "<=", 2) == 0) {
		res->fun = criteria_test_less_or_equal;
		len = 2;
	} else if (strncmp (criteria, ">=", 2) == 0) {
		res->fun = criteria_test_greater_or_equal;
		len = 2;
	} else if (strncmp (criteria, "<>", 2) == 0) {
		/* "<>" by itself is special: */
		res->fun = (criteria[2] == 0) ? criteria_test_nonempty : criteria_test_unequal;
		len = 2;
	} else if (*criteria == '<') {
		res->fun = criteria_test_less;
		len = 1;
	} else if (*criteria == '=') {
		/* "=" by itself is special: */
		res->fun = (criteria[1] == 0) ? criteria_test_empty : criteria_test_equal;
		len = 1;
	} else if (*criteria == '>') {
		res->fun = criteria_test_greater;
		len = 1;
	} else {
		res->fun = criteria_test_match;
		res->has_rx = (gnm_regcomp_XL (&res->rx, criteria, GO_REG_ICASE, TRUE, anchor_end) == GO_REG_OK);
		len = 0;
	}

	res->x = format_match_number (criteria + len, NULL, date_conv);
	if (res->x == NULL)
		res->x = value_new_string (criteria + len);
	else if (len == 0 && VALUE_IS_NUMBER (res->x))
		res->fun = criteria_test_equal;

	empty = value_new_empty ();
	if (res->fun (empty, res))
		res->iter_flags &= ~CELL_ITER_IGNORE_BLANK;
	value_release (empty);

	return res;
}


static GSList *
parse_criteria_range (Sheet *sheet, int b_col, int b_row, int e_col, int e_row,
		      int   *field_ind, gboolean anchor_end)
{
	GSList *criterias = NULL;
	GODateConventions const *date_conv = sheet_date_conv (sheet);
        int i, j;

	for (i = b_row; i <= e_row; i++) {
		GnmDBCriteria *new_criteria = g_new (GnmDBCriteria, 1);
		GSList *conditions = NULL;

		for (j = b_col; j <= e_col; j++) {
			GnmCriteria *cond;
			GnmCell	*cell = sheet_cell_get (sheet, j, i);
			if (cell != NULL)
				gnm_cell_eval (cell);
			if (gnm_cell_is_empty (cell))
				continue;

			cond = parse_criteria (cell->value, date_conv,
					       anchor_end);
			cond->column = (field_ind != NULL)
				? field_ind[j - b_col]
				: j - b_col;
			conditions = g_slist_prepend (conditions, cond);
		}

		new_criteria->conditions = g_slist_reverse (conditions);
		criterias = g_slist_prepend (criterias, new_criteria);
	}

	return g_slist_reverse (criterias);
}

/**
 * parse_database_criteria:
 * @ep: #GnmEvalPos
 * @database: #GnmValue
 * @criteria: #GnmValue
 *
 * Parses the criteria cell range.
 * Returns: (element-type GnmDBCriteria) (transfer full):
 */
GSList *
parse_database_criteria (GnmEvalPos const *ep, GnmValue const *database, GnmValue const *criteria)
{
	Sheet	*sheet;
	GnmCell	*cell;
        int   i;
	int   b_col, b_row, e_col, e_row;
	int   *field_ind;
	GSList *res;

	g_return_val_if_fail (VALUE_IS_CELLRANGE (criteria), NULL);

	sheet = eval_sheet (criteria->v_range.cell.a.sheet, ep->sheet);
	b_col = criteria->v_range.cell.a.col;
	b_row = criteria->v_range.cell.a.row;
	e_col = criteria->v_range.cell.b.col;
	e_row = criteria->v_range.cell.b.row;

	if (e_col < b_col) {
		int tmp = b_col;
		b_col = e_col;
		e_col = tmp;
	}

	/* Find the index numbers for the columns of criterias */
	field_ind = g_new (int, e_col - b_col + 1);
	for (i = b_col; i <= e_col; i++) {
		cell = sheet_cell_get (sheet, i, b_row);
		if (cell == NULL)
			continue;
		gnm_cell_eval (cell);
		if (gnm_cell_is_empty (cell))
			continue;
		field_ind[i - b_col] =
			find_column_of_field (ep, database, cell->value);
		if (field_ind[i - b_col] == -1) {
			g_free (field_ind);
			return NULL;
		}
	}

	res = parse_criteria_range (sheet, b_col, b_row + 1,
				    e_col, e_row, field_ind,
				    FALSE);
	g_free (field_ind);
	return res;
}

/**
 * find_rows_that_match:
 * @sheet: #Sheet
 * @first_col: first column.
 * @first_row: first row.
 * @last_col: last column.
 * @last_row: last row.
 * @criterias: (element-type GnmDBCriteria): the criteria to use.
 * @unique_only:
 *
 * Finds the rows from the given database that match the criteria.
 * Returns: (element-type int) (transfer full): the list of matching rows.
 **/
GSList *
find_rows_that_match (Sheet *sheet, int first_col, int first_row,
		      int last_col, int last_row,
		      GSList *criterias, gboolean unique_only)
{
	GSList	     *rows = NULL;
	GSList const *crit_ptr, *cond_ptr;
	int        row;
	gboolean   add_flag;
	char const *t1, *t2;
	GnmCell   *test_cell;
	GnmValue const *empty = value_new_empty ();

	for (row = first_row; row <= last_row; row++) {
		add_flag = TRUE;
		for (crit_ptr = criterias; crit_ptr; crit_ptr = crit_ptr->next) {
			GnmDBCriteria const *crit = crit_ptr->data;
			add_flag = TRUE;
			for (cond_ptr = crit->conditions;
			     cond_ptr != NULL ; cond_ptr = cond_ptr->next) {
				GnmCriteria *cond = cond_ptr->data;
				test_cell = sheet_cell_get (sheet, cond->column, row);
				if (test_cell != NULL)
					gnm_cell_eval (test_cell);
				if (!cond->fun (test_cell ? test_cell->value : empty, cond)) {
					add_flag = FALSE;
					break;
				}
			}

			if (add_flag)
				break;
		}
		if (add_flag) {
			if (unique_only) {
				GSList *c;
				GnmCell   *cell;
				gint    i;

				for (c = rows; c != NULL; c = c->next) {
					int trow = GPOINTER_TO_INT (c->data);
					for (i = first_col; i <= last_col; i++) {
						test_cell = sheet_cell_get (sheet, i, trow);
						cell = sheet_cell_get (sheet, i, row);

						/* FIXME: this is probably not right, but crashing is more wrong.  */
						if (test_cell == NULL || cell == NULL)
							continue;

						t1 = cell->value
							? value_peek_string (cell->value)
							: "";
						t2 = test_cell->value
							? value_peek_string (test_cell->value)
							: "";
						if (strcmp (t1, t2) != 0)
							goto row_ok;
					}
					goto filter_row;
row_ok:
					;
				}
			}
			rows = g_slist_prepend (rows, GINT_TO_POINTER (row));
filter_row:
			;
		}
	}

	return g_slist_reverse (rows);
}

/****************************************************************************/

/**
 * gnm_ifs_func:
 * @data: (element-type GnmValue):
 * @crits: (element-type GnmCriteria): criteria
 * @vals:
 * @fun: (scope call): function to evaluate on filtered data
 * @err: error value in case @fun fails.
 * @ep: evaluation position
 * @flags: #CollectFlags flags describing the collection and interpretation
 * of values from @data.
 *
 * This implements a Gnumeric sheet database function of the "*IFS" type
 * This function collects the arguments and uses @fun to do
 * the actual computation.
 *
 * Returns: (transfer full): Function result or error value.
 */
GnmValue *
gnm_ifs_func (GPtrArray *data, GPtrArray *crits, GnmValue const *vals,
	      float_range_function_t fun, GnmStdError err,
	      GnmEvalPos const *ep, CollectFlags flags)
{
	int sx, sy, x, y;
	unsigned ui, N = 0, nalloc = 0;
	gnm_float *xs = NULL;
	GnmValue *res = NULL;
	gnm_float fres;

	g_return_val_if_fail (data->len == crits->len, NULL);

	if (flags & ~(COLLECT_IGNORE_STRINGS |
		      COLLECT_IGNORE_BOOLS |
		      COLLECT_IGNORE_BLANKS |
		      COLLECT_IGNORE_ERRORS)) {
		g_warning ("unsupported flags in gnm_ifs_func %x", flags);
	}

	sx = value_area_get_width (vals, ep);
	sy = value_area_get_height (vals, ep);
	for (ui = 0; ui < data->len; ui++) {
		GnmValue const *datai = g_ptr_array_index (data, ui);
		if (value_area_get_width (datai, ep) != sx ||
		    value_area_get_height (datai, ep) != sy)
			return value_new_error_VALUE (ep);
	}

	for (y = 0; y < sy; y++) {
		for (x = 0; x < sx; x++) {
			GnmValue const *v;
			gboolean match = TRUE;

			for (ui = 0; match && ui < crits->len; ui++) {
				GnmCriteria *crit = g_ptr_array_index (crits, ui);
				GnmValue const *datai = g_ptr_array_index (data, ui);
				v = value_area_get_x_y (datai, x, y, ep);

				match = crit->fun (v, crit);
			}
			if (!match)
				continue;

			// Match.  Maybe collect the data point.

			v = value_area_get_x_y (vals, x, y, ep);
			if ((flags & COLLECT_IGNORE_STRINGS) && VALUE_IS_STRING (v))
				continue;
			if ((flags & COLLECT_IGNORE_BOOLS) && VALUE_IS_BOOLEAN (v))
				continue;
			if ((flags & COLLECT_IGNORE_BLANKS) && VALUE_IS_EMPTY (v))
				continue;
			if ((flags & COLLECT_IGNORE_ERRORS) && VALUE_IS_ERROR (v))
				continue;

			if (VALUE_IS_ERROR (v)) {
				res = value_dup (v);
				goto out;
			}

			if (N >= nalloc) {
				nalloc = (2 * nalloc) + 100;
				xs = g_renew (gnm_float, xs, nalloc);
			}
			xs[N++] = value_get_as_float (v);
		}
	}

	if (fun (xs, N, &fres)) {
		res = value_new_error_std (ep, err);
	} else
		res = value_new_float (fres);

out:
	g_free (xs);
	return res;
}

/****************************************************************************/
