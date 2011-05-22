/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_FUNC_BUILTIN_H_
# define _GNM_FUNC_BUILTIN_H_

#include <func.h>

G_BEGIN_DECLS

GnmValue *gnumeric_if      (GnmFuncEvalInfo *ei, GnmValue const * const *args);
GnmValue *gnumeric_if2     (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv, GnmExprEvalFlags flags);

void func_builtin_init (void);
void func_builtin_shutdown (void);

G_END_DECLS

#endif /* _GNM_FUNC_BUILTIN_H_ */
