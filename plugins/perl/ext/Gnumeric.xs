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

static SV *
value2perl(Value *v)
{
    SV *sv;

    switch (v->type) {
    case VALUE_BOOLEAN:
	sv = newSViv(v->v_bool.val);
	break;
	
    case VALUE_INTEGER:
	sv = newSViv(v->v_int.val);
	break;
	
    case VALUE_FLOAT:
	sv = newSVnv(v->v_float.val);
	break;
	
    case VALUE_STRING:
	sv = newSVpv(v->v_str.val->str, strlen(v->v_str.val->str));
	break;
	
    default:
	sv = NULL;
	break;
    }
    return sv;
}

static Value *
perl2value(SV *sv)
{
    Value *v = NULL;

    if (SvIOK(sv)){
	v = value_new_int ((gnum_int) SvIV(sv));
    } else if (SvNOK(sv)) {
	v = value_new_float ((gnum_float) SvNV(sv));
    } else if (SvPOK(sv)) {
	STRLEN size;
	gchar *s,*tmp;

	tmp = SvPV(sv, size);
	
	s = g_malloc (size + 1);
	strncpy(s, tmp, size);
	s[size] = '\0';
	v = value_new_string (s);
	g_free (s);
    }

    return v;
}

static Value *
marshal_func (FunctionEvalInfo *ei, Value *argv[])
{
    dSP;
    FunctionDefinition const *fndef =
	gnm_expr_get_func_def ((GnmExpr const *)ei->func_call);
    GList *l;
    I32 r;
    int i, min, max;
    SV * result;
    Value *v;

    /* Read the perlcall man page for more information. */
    ENTER;
    SAVETMPS;

    PUSHMARK(sp);
    function_def_count_args (fndef, &min, &max);

    for (i = 0; i < max && argv[i] != NULL; i++) {
	XPUSHs(sv_2mortal(value2perl(argv[i])));
    }
    PUTBACK;

    r = perl_call_sv(function_def_get_user_data (fndef), G_SCALAR);
    SPAGAIN;
    if (r != 1)
	croak("uh oh, beter get maco");

    result = POPs;
    v = perl2value(result);

    PUTBACK;
    FREETMPS;
    LEAVE;

    return v;
}

MODULE = Gnumeric  PACKAGE = Gnumeric

void
register_function(name, args, named_args, help1, subref)
  char * name
  char * args
  char * named_args
  char * help1
  SV * subref
  PREINIT:
    FunctionCategory *fncat;
    FunctionDefinition *fndef;
    char const **help = NULL;
  CODE:
    fncat = function_get_category ("Perl plugin");

    if (help1) {
	    help = g_new (char const *, 1);
            *help = g_strdup (help1);
    }
    fndef = function_add_args (fncat, g_strdup(name), g_strdup(args),
			       g_strdup (named_args), help, marshal_func);
    function_def_set_user_data (fndef, newSVsv(subref));
