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

static int
callback_function_collect (const EvalPosition *ep, Value *value,
			   ErrorMessage *error, void *closure)
{
	float_t x;
	collect_floats_t *cl = (collect_floats_t *)closure;

	switch (value->type) {
	case VALUE_INTEGER:
	case VALUE_FLOAT:
		x = value_get_as_float (value);
		break;

	case VALUE_STRING:
		if (cl->flags & COLLECT_IGNORE_STRINGS)
			return TRUE;
		else if (cl->flags & COLLECT_ZERO_STRINGS)
			x = 0;
		else {
			error_message_set (error, gnumeric_err_VALUE);
			return FALSE;
		}
		break;

	default:
		g_warning ("Trouble in callback_function_collect.");
		return TRUE;
	}

	if (cl->count == cl->alloc_count) {
		cl->alloc_count *= 2;
		cl->data = g_realloc (cl->data, cl->alloc_count * sizeof (float_t));
	}

	cl->data[cl->count++] = x;
	return TRUE;
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
 * error_string:   Location to store error string.
 *
 * Return value:
 *   NULL in case of error.  Then error_string will be set.
 *   Non-NULL in case of success.  Then n will be set.
 *
 * Evaluate a list of expressions and return the result as an array of
 * float_t.
 */
float_t *
collect_floats (GList *exprlist, const EvalPosition *ep, CollectFlags flags,
		int *n, ErrorMessage *error)
{
	collect_floats_t cl;

	cl.alloc_count = 20;
	cl.data = g_new (float_t, cl.alloc_count);
	cl.count = 0;
	cl.flags = flags;

	if (function_iterate_argument_values (ep, callback_function_collect,
					      &cl, exprlist,
					      error, TRUE)) {
		*n = cl.count;
		return cl.data;
	} else {
		g_free (cl.data);
		return NULL;
	}
}

/* ------------------------------------------------------------------------- */

/*
 * Single-expression version of collect_floats, which see.
 */
float_t *
collect_floats_1 (ExprTree *expr, const EvalPosition *ep, CollectFlags flags,
		  int *n, ErrorMessage *error)
{
	GList *l;
	float_t *res;

	l = g_list_prepend (NULL, expr);
	res = collect_floats (l, ep, flags, n, error);
	g_list_free_1 (l);

	return res;
}

/* ------------------------------------------------------------------------- */

Value *
float_range_function (GList *exprlist, FunctionEvalInfo *ei,
		      float_range_function_t func,
		      CollectFlags flags,
		      char *func_error, ErrorMessage *error)
{
	float_t *vals, res;
	int n, err;


	vals = collect_floats (exprlist, &ei->pos, flags, &n, error);
	if (!vals)
		return NULL;

	err = func (vals, n, &res);
	g_free (vals);

	if (err)
		return function_error (ei, func_error);
	else
		return value_new_float (res);
}

/* ------------------------------------------------------------------------- */
