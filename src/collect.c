/*
 * collect.c: Helpers to collect ranges of data.
 *
 * Authors:
 *   Morten Welinder <terra@diku.dk>
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
 */

#include <config.h>
#include "collect.h"
#include "func.h"
#include "datetime.h"

/* ------------------------------------------------------------------------- */

typedef struct {
	int alloc_count;
	gnum_float *data;
	int count;
	CollectFlags flags;
} collect_floats_t;

static Value *
callback_function_collect (const EvalPos *ep, Value *value, void *closure)
{
	gnum_float x;
	collect_floats_t *cl = (collect_floats_t *)closure;

	if (value == NULL) {
		if (cl->flags & COLLECT_IGNORE_BLANKS)
			return NULL;
		x = 0.;
	} else switch (value->type) {
	case VALUE_EMPTY:
		if (cl->flags & COLLECT_IGNORE_BLANKS)
			return NULL;
		x = 0.;
		break;

	case VALUE_BOOLEAN:
		if (cl->flags & COLLECT_IGNORE_BOOLS)
			return NULL;
		else if (cl->flags & COLLECT_ZEROONE_BOOLS)
			x = (value->v_bool.val) ? 1. : 0.;
		else
			return value_new_error (ep, gnumeric_err_VALUE);
		break;

	case VALUE_CELLRANGE :
	case VALUE_ARRAY :
		/* Ranges and arrays are not singleton values treat as errors */

	case VALUE_ERROR:
		if (cl->flags & COLLECT_IGNORE_ERRORS)
			return NULL;
		else if (cl->flags & COLLECT_ZERO_ERRORS)
			x = 0.;
		else
			return value_new_error (ep, gnumeric_err_VALUE);
		break;

	case VALUE_INTEGER:
	case VALUE_FLOAT:
		x = value_get_as_float (value);
		break;

	case VALUE_STRING:
	        if (cl->flags & COLLECT_DATES) {
		        x = datetime_value_to_serial (value);
			if (x == 0)
			        return value_new_error (ep, gnumeric_err_VALUE);
		} else if (cl->flags & COLLECT_IGNORE_STRINGS)
			return NULL;
		else if (cl->flags & COLLECT_ZERO_STRINGS)
			x = 0;
		else
			return value_new_error (ep, gnumeric_err_VALUE);
		break;

	default:
		g_warning ("Trouble in callback_function_collect. (%d)",
			   value->type);
		return NULL;
	}

	if (cl->count == cl->alloc_count) {
		cl->alloc_count *= 2;
		cl->data = g_realloc (cl->data, cl->alloc_count * sizeof (gnum_float));
	}

	cl->data[cl->count++] = x;
	return NULL;
}

/*
 * collect_floats:
 *
 * exprlist:       List of expressions to evaluate.
 * cr:             Current location (for resolving relative cells).
 * flags:          COLLECT_IGNORE_STRINGS: silently ignore strings.
 *                 COLLECT_ZERO_STRINGS: count strings as 0.
 *                   (Alternative: return #VALUE!.)
 *                 COLLECT_DATES: count strings as dates.
 *                 COLLECT_IGNORE_BOOLS: silently ignore bools.
 *                 COLLECT_ZEROONE_BOOLS: count FALSE as 0, TRUE as 1.
 *                   (Alternative: return #VALUE!.)
 * n:              Output parameter for number of floats.
 *
 * Return value:
 *   NULL in case of strict and a blank.
 *   A copy of the error in the case of strict and an error.
 *   Non-NULL in case of success.  Then n will be set.
 *
 * Evaluate a list of expressions and return the result as an array of
 * gnum_float.
 */
static gnum_float *
collect_floats (GList *exprlist, const EvalPos *ep, CollectFlags flags,
		int *n, Value **error)
{
	Value * err;
	collect_floats_t cl;

	cl.alloc_count = 20;
	cl.data = g_new (gnum_float, cl.alloc_count);
	cl.count = 0;
	cl.flags = flags;

	err = function_iterate_argument_values (ep, &callback_function_collect,
		&cl, exprlist,
		TRUE, flags&COLLECT_IGNORE_BLANKS);

	if (err) {
		g_assert (err->type == VALUE_ERROR);
		g_free (cl.data);
		*error = err;
		return NULL;
	}

	*n = cl.count;
	return cl.data;
}

/* ------------------------------------------------------------------------- */
/* Like collect_floats, but takes a value instead of an expression list.
   Presumably most useful when the value is an array.  */

gnum_float *
collect_floats_value (const Value *val, const EvalPos *ep,
		      CollectFlags flags, int *n, Value **error)
{
	GList *exprlist;
	ExprTree *expr_val;
	gnum_float *res;

	expr_val = expr_tree_new_constant (value_duplicate (val));
	exprlist = g_list_prepend (NULL, expr_val);

	res = collect_floats (exprlist, ep, flags, n, error);

	expr_tree_unref (expr_val);
	g_list_free (exprlist);

	return res;
}


/* ------------------------------------------------------------------------- */

Value *
float_range_function (GList *exprlist, FunctionEvalInfo *ei,
		      float_range_function_t func,
		      CollectFlags flags,
		      char const *func_error)
{
	Value *error = NULL;
	gnum_float *vals, res;
	int n, err;

	vals = collect_floats (exprlist, ei->pos, flags, &n, &error);
	if (!vals)
		return (error != value_terminate ()) ? error : NULL;

	err = func (vals, n, &res);
	g_free (vals);

	if (err)
		return value_new_error (ei->pos, func_error);
	else
		return value_new_float (res);
}

/* ------------------------------------------------------------------------- */

Value *
float_range_function2 (Value *val0, Value *val1, FunctionEvalInfo *ei,
		       float_range_function2_t func,
		       CollectFlags flags,
		       char const *func_error)
{
	gnum_float *vals0, *vals1;
	int n0, n1;
	Value *error = NULL;
	Value *res;

	vals0 = collect_floats_value (val0, ei->pos,
				      COLLECT_IGNORE_STRINGS | COLLECT_IGNORE_BOOLS | COLLECT_IGNORE_BLANKS,
				      &n0, &error);
	if (error)
		return error;

	vals1 = collect_floats_value (val1, ei->pos,
				      COLLECT_IGNORE_STRINGS | COLLECT_IGNORE_BOOLS | COLLECT_IGNORE_BLANKS,
				      &n1, &error);
	if (error) {
		g_free (vals0);
		return error;
	}

	if (n0 != n1 || n0 == 0)
		res = value_new_error (ei->pos, func_error);
	else {
		gnum_float fres;

		if (func (vals0, vals1, n0, &fres))
			res = value_new_error (ei->pos, func_error);
		else
			res = value_new_float (fres);
	}

	g_free (vals0);
	g_free (vals1);
	return res;
}

/* ------------------------------------------------------------------------- */
