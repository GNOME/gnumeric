#ifndef GNUMERIC_FUNC_BUILTIN_H
#define GNUMERIC_FUNC_BUILTIN_H

#include <func.h>

Value *gnumeric_sum (FunctionEvalInfo *ei, GnmExprList *nodes);
Value *gnumeric_product (FunctionEvalInfo *ei, GnmExprList *nodes);

void func_builtin_init (void);
void func_builtin_shutdown (void);

#endif
