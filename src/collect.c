/*
 * collect.c: Helpers to collect ranges of data.
 *
 * Author:
 *   Morten Welinder <terra@diku.dk>
 */

#include "collect.h"
#include "func.h"

/* ------------------------------------------------------------------------- */

typedef struct {
	int alloc_count;
	float_t *data;
	int count;
	CollectFlags flags;
} collect_floats_t;

static Value *
callback_function_collect (const EvalPosition *ep, Value *value, void *closure)
{
	float_t x;
	collect_floats_t *cl = (collect_floats_t *)closure;

	switch (value->type) {
	case VALUE_BOOLEAN:
		if (cl->flags & COLLECT_IGNORE_BOOLS)
			return NULL;
		else if (cl->flags & COLLECT_ZEROONE_BOOLS)
			x = (value->v.v_bool) ? 1. : 0.;
		else
			return value_new_error (ep, gnumeric_err_VALUE);
		break;

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
		if (cl->flags & COLLECT_IGNORE_STRINGS)
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
		cl->data = g_realloc (cl->data, cl->alloc_count * sizeof (float_t));
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
 * float_t.
 */
static float_t *
collect_floats (GList *exprlist, const EvalPosition *ep, CollectFlags flags,
		int *n, Value **error)
{
	Value * err;
	collect_floats_t cl;

	cl.alloc_count = 20;
	cl.data = g_new (float_t, cl.alloc_count);
	cl.count = 0;
	cl.flags = flags;

	err = function_iterate_argument_values (ep, &callback_function_collect,
						&cl, exprlist, TRUE);

	if (err) {
		g_assert (err->type == VALUE_ERROR);
		g_free (cl.data);
		/* Be careful not to make value_terminate into a real value */
		*error = (err != value_terminate())? value_duplicate(err) : err;
		return NULL;
	}

	*n = cl.count;
	return cl.data;
}

/* ------------------------------------------------------------------------- */

Value *
float_range_function (GList *exprlist, FunctionEvalInfo *ei,
		      float_range_function_t func,
		      CollectFlags flags,
		      char const *func_error)
{
	Value *error = NULL;
	float_t *vals, res;
	int n, err;

	vals = collect_floats (exprlist, &ei->pos, flags, &n, &error);
	if (!vals)
		return (error != value_terminate()) ? error : NULL;

	err = func (vals, n, &res);
	g_free (vals);

	if (err)
		return value_new_error (&ei->pos, func_error);
	else
		return value_new_float (res);
}

/* ------------------------------------------------------------------------- */
