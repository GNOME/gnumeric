/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_FUNC_BUILTIN_H_
# define _GNM_FUNC_BUILTIN_H_

#include <func.h>

G_BEGIN_DECLS

GnmValue *gnumeric_sum     (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv);
GnmValue *gnumeric_product (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv);

void func_builtin_init (void);
void func_builtin_shutdown (void);

G_END_DECLS

#endif /* _GNM_FUNC_BUILTIN_H_ */
