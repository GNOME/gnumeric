#ifndef GNUMERIC_FUNC_UTIL_H
#define GNUMERIC_FUNC_UTIL_H

#include "numbers.h"

typedef struct {
	int N;
	gnum_float M, Q, sum;
        gboolean afun_flag;
} stat_closure_t;

void setup_stat_closure     (stat_closure_t *cl);
Value *callback_function_stat (EvalPos const *ep, Value *value,
			       void *closure);


#endif /* GNUMERIC_FUNC_UTIL_H */
