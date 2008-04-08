/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_COLLECT_H_
# define _GNM_COLLECT_H_

#include "numbers.h"
#include "gnumeric.h"

G_BEGIN_DECLS

typedef enum {
	COLLECT_IGNORE_STRINGS	= 0x01,
	COLLECT_ZERO_STRINGS	= 0x02,
	COLLECT_COERCE_STRINGS	= 0x04,

	COLLECT_IGNORE_BOOLS	= 0x10,
	COLLECT_ZEROONE_BOOLS	= 0x20,

	COLLECT_IGNORE_ERRORS	= 0x100,

	COLLECT_IGNORE_BLANKS	= 0x1000,
	COLLECT_IGNORE_SUBTOTAL	= 0x2000,

	/* Not for general usage.  */
	COLLECT_INFO		= 0x1000000
} CollectFlags;

typedef int (*float_range_function_t) (gnm_float const *, int, gnm_float *);
typedef int (*float_range_function2_t) (gnm_float const *, gnm_float const *, int, gnm_float *);
typedef int (*string_range_function_t) (GPtrArray *, char**);

/*gnm_float *collect_floats (int argc, GnmExprConstPtr const *argv,
				GnmEvalPos const *ep, CollectFlags flags,
				int *n, GnmValue **error, GSList **info);*/

gnm_float *collect_floats_value (GnmValue const *val,
				 GnmEvalPos const *ep,
				 CollectFlags flags,
				 int *n, GnmValue **error);

gnm_float *collect_floats_value_with_info (GnmValue const *val, GnmEvalPos const *ep,
				CollectFlags flags, int *n, GSList **info,
				GnmValue **error);

GnmValue *float_range_function (int argc, GnmExprConstPtr const *argv,
				GnmFuncEvalInfo *ei,
				float_range_function_t func,
				CollectFlags flags,
				GnmStdError func_error);

GnmValue *float_range_function2 (GnmValue const *val0, GnmValue const *val1,
				 GnmFuncEvalInfo *ei,
				 float_range_function2_t func,
				 CollectFlags flags,
				 GnmStdError func_error);

GnmValue *string_range_function (int argc, GnmExprConstPtr const *argv,
				 GnmFuncEvalInfo *ei,
				 string_range_function_t func,
				 CollectFlags flags,
				 GnmStdError func_error);

GSList *gnm_slist_sort_merge (GSList * list_1, GSList * list_2);

void gnm_strip_missing (GArray * data, GSList *missing);


G_END_DECLS

#endif /* _GNM_COLLECT_H_ */
