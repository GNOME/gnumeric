#ifndef GNUMERIC_FUNC_BUILTIN_H
#define GNUMERIC_FUNC_BUILTIN_H

#include <func.h>

GnmValue *gnumeric_sum     (FunctionEvalInfo *ei, int argc, const GnmExprConstPtr *argv);
GnmValue *gnumeric_product (FunctionEvalInfo *ei, int argc, const GnmExprConstPtr *argv);

void func_builtin_init (void);
void func_builtin_shutdown (void);

#endif /* GNUMERIC_FUNC_BUILTIN_H */
