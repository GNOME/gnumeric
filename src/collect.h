#ifndef _GNM_COLLECT_H_
# define _GNM_COLLECT_H_

#include <numbers.h>
#include <gnumeric.h>

G_BEGIN_DECLS

typedef enum {
	COLLECT_IGNORE_STRINGS	= 0x01,
	COLLECT_ZERO_STRINGS	= 0x02,
	COLLECT_COERCE_STRINGS	= 0x04,

	COLLECT_IGNORE_BOOLS	= 0x10,
	COLLECT_ZEROONE_BOOLS	= 0x20,

	COLLECT_IGNORE_ERRORS	= 0x100,
	COLLECT_ZERO_ERRORS	= 0x200,

	COLLECT_IGNORE_BLANKS	= 0x1000,
	COLLECT_ZERO_BLANKS	= 0x2000,

	COLLECT_IGNORE_SUBTOTAL	= 0x4000,

	COLLECT_SORT            = 0x10000,
	COLLECT_ORDER_IRRELEVANT = 0x20000,

	/* Not for general usage.  */
	COLLECT_INFO		= 0x1000000
} CollectFlags;

typedef int (*float_range_function_t) (gnm_float const *xs, int n, gnm_float *res);
typedef int (*float_range_function2_t) (gnm_float const *xs, gnm_float const *ys, int n, gnm_float *res);
typedef int (*float_range_function2d_t) (gnm_float const *xs, gnm_float const *ys, int n, gnm_float *res, gpointer data);
typedef int (*string_range_function_t) (GPtrArray *xs, char**res, gpointer user);

gnm_float *collect_floats_value (GnmValue const *val,
				 GnmEvalPos const *ep,
				 CollectFlags flags,
				 int *n, GnmValue **error);
gnm_float *collect_floats (int argc, GnmExprConstPtr const *argv,
			   GnmEvalPos const *ep, CollectFlags flags,
			   int *n, GnmValue **error, GSList **info,
			   gboolean *constp);

gnm_float *collect_floats_value_with_info (GnmValue const *val, GnmEvalPos const *ep,
				CollectFlags flags, int *n, GSList **info,
				GnmValue **error);

GnmValue *collect_float_pairs (GnmValue const *v0, GnmValue const *v1,
			       GnmEvalPos const *ep, CollectFlags flags,
			       gnm_float **xs0, gnm_float **xs1, int *n,
			       gboolean *constp);

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

GnmValue *float_range_function2d (GnmValue const *val0, GnmValue const *val1,
				  GnmFuncEvalInfo *ei,
				  float_range_function2d_t func,
				  CollectFlags flags,
				  GnmStdError func_error,
				  gpointer data);

GnmValue *string_range_function (int argc, GnmExprConstPtr const *argv,
				 GnmFuncEvalInfo *ei,
				 string_range_function_t func,
				 gpointer user,
				 CollectFlags flags,
				 GnmStdError func_error);

GSList *gnm_slist_sort_merge (GSList * list_1, GSList * list_2);

void gnm_strip_missing (gnm_float* data, int *n, GSList *missing);


G_END_DECLS

#endif /* _GNM_COLLECT_H_ */
