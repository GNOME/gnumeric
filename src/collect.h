#ifndef GNUMERIC_COLLECT_H
#define GNUMERIC_COLLECT_H

#include "numbers.h"
#include "gnumeric.h"

typedef enum {
	COLLECT_IGNORE_STRINGS	= 0x01,
	COLLECT_ZERO_STRINGS	= 0x02,

	COLLECT_IGNORE_BOOLS	= 0x10,
	COLLECT_ZEROONE_BOOLS	= 0x20,

	COLLECT_IGNORE_ERRORS	= 0x100,
	COLLECT_ZERO_ERRORS	= 0x200,

	COLLECT_IGNORE_BLANKS	= 0x1000,
	COLLECT_IGNORE_SUBTOTAL	= 0x2000,
	COLLECT_DATES		= 0x4000,
	COLLECT_INFO		= 0x8000,

	COLLECT_NO_INFO_MASK	= 0x7777
} CollectFlags;


typedef int (*float_range_function_t) (const gnm_float *, int, gnm_float *);
typedef int (*float_range_function2_t) (const gnm_float *, const gnm_float *, int, gnm_float *);
typedef int (*string_range_function_t) (GSList *, char**);

gnm_float *collect_floats_value (const Value *val, const EvalPos *ep,
				  CollectFlags flags,
				  int *n, Value **error);

Value *float_range_function (GnmExprList *exprlist, FunctionEvalInfo *ei,
			     float_range_function_t func,
			     CollectFlags flags,
			     GnmStdError func_error);

Value *float_range_function2 (Value *val0, Value *val1, FunctionEvalInfo *ei,
			      float_range_function2_t func,
			      CollectFlags flags,
			      GnmStdError func_error);
Value *string_range_function (GnmExprList *exprlist, FunctionEvalInfo *ei,
			      string_range_function_t func,
			      CollectFlags flags,
			      GnmStdError func_error);

#endif
