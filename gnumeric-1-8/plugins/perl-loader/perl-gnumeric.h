#ifndef _PERL_GNUMERIC_H
#define _PERL_GNUMERIC_H

/*
 * Interface to Gnumeric internal functions.
 */

#ifdef __cplusplus
extern "C" {
#endif
#include "EXTERN.h"
#include "perl.h"
#undef _
#undef dirty
#include "XSUB.h"
#ifdef __cplusplus
}
#endif

#ifdef PerlIO
typedef int SysRet;
typedef PerlIO * InputStream;
typedef PerlIO * OutputStream;
#else
typedef int SysRet;
typedef FILE * InputStream;
typedef FILE * OutputStream;
#endif

#include <gnumeric.h>
#include <expr.h>
#include <func.h>
#include <value.h>
#include <str.h>

SV* value2perl(GnmValue *v);
GnmValue* perl2value(SV *sv);
GnmValue* marshal_func (GnmFuncEvalInfo *ei, GnmValue *argv[]);

#endif /* _PERL_GNUMERIC_H */
