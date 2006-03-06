#ifndef GNUMERIC_COLLECT_H
#define GNUMERIC_COLLECT_H

#include "numbers.h"
#include "gnumeric.h"

typedef enum {
	COLLECT_IGNORE_STRINGS	= 0x01,
	COLLECT_ZERO_STRINGS	= 0x02,
	COLLECT_COERCE_STRINGS	= 0x04,

	COLLECT_IGNORE_BOOLS	= 0x10,
	COLLECT_ZEROONE_BOOLS	= 0x20,

	COLLECT_IGNORE_ERRORS	= 0x100,
	COLLECT_ZERO_ERRORS	= 0x200,

	COLLECT_IGNORE_BLANKS	= 0x1000,
	COLLECT_IGNORE_SUBTOTAL	= 0x2000,
	COLLECT_INFO		= 0x8000,

	COLLECT_NO_INFO_MASK	= 0x7777
} CollectFlags;


typedef int (*float_range_function_t) (gnm_float const *, int, gnm_float *);
typedef int (*float_range_function2_t) (gnm_float const *, gnm_float const *, int, gnm_float *);
typedef int (*string_range_function_t) (GSList *, char**);

gnm_float *collect_floats_value (GnmValue const *val, const GnmEvalPos *ep,
				 CollectFlags flags,
				 int *n, GnmValue **error);

GnmValue *float_range_function (GnmExprList const *exprlist, FunctionEvalInfo *ei,
				float_range_function_t func,
				CollectFlags flags,
				GnmStdError func_error);

GnmValue *float_range_function2 (GnmValue const *val0, GnmValue const *val1,
				 FunctionEvalInfo *ei,
				 float_range_function2_t func,
				 CollectFlags flags,
				 GnmStdError func_error);
GnmValue *string_range_function (GnmExprList const *exprlist, FunctionEvalInfo *ei,
				 string_range_function_t func,
				 CollectFlags flags,
				 GnmStdError func_error);

#endif
