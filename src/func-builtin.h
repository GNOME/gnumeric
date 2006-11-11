#ifndef GNUMERIC_FUNC_BUILTIN_H
#define GNUMERIC_FUNC_BUILTIN_H

#include <func.h>

GnmValue *gnumeric_sum     (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv);
GnmValue *gnumeric_product (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv);

void func_builtin_init (void);
void func_builtin_shutdown (void);

#endif /* GNUMERIC_FUNC_BUILTIN_H */
