#ifndef GNUMERIC_FUNC_BUILTIN_H
#define GNUMERIC_FUNC_BUILTIN_H

#include <func.h>

GnmValue *gnumeric_sum     (FunctionEvalInfo *ei, GnmExprList const *nodes);
GnmValue *gnumeric_product (FunctionEvalInfo *ei, GnmExprList const *nodes);

void func_builtin_init (void);
void func_builtin_shutdown (void);

#endif /* GNUMERIC_FUNC_BUILTIN_H */
