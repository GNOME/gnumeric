#ifndef GNUMERIC_COLLECT_H
#define GNUMERIC_COLLECT_H

#include "numbers.h"
#include "expr.h"

typedef enum {
	COLLECT_IGNORE_STRINGS = 0x01,
	COLLECT_ZERO_STRINGS = 0x02,

	COLLECT_IGNORE_BOOLS = 0x10,
	COLLECT_ZEROONE_BOOLS = 0x20,
} CollectFlags;


float_t *collect_floats_1 (ExprTree *expr, const EvalPosition *cr,
			   CollectFlags flags,
			   int *n, ErrorMessage *error);
float_t *collect_floats   (GList *exprlist, const EvalPosition *cr,
			   CollectFlags flags,
			   int *n, ErrorMessage *error);

typedef int (*float_range_function_t) (const float_t *, int, float_t *);

Value *float_range_function (GList *exprlist, FunctionEvalInfo *ei,
			     float_range_function_t func,
			     CollectFlags flags,
			     char *func_error, ErrorMessage *error);

#endif
