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

    if (SvIOK(sv))
	v = value_new_int (SvIV(sv));
    else if (SvNOK(sv))
	v = value_new_float ((gnum_float) SvNV(sv));
    else if (SvPOK(sv)) {
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
    GnmFunc const *func =
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
    function_def_count_args (func, &min, &max);

    for (i = 0; i < max && argv[i] != NULL; i++) {
	XPUSHs(sv_2mortal(value2perl(argv[i])));
    }
    PUTBACK;

    r = perl_call_sv (gnm_func_get_user_data (func), G_SCALAR);
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
PROTOTYPES: ENABLE

void
register_function(name, args, named_args, help, subref)
    char *name
    char *args
    char *named_args
    char *help
    SV *subref

 PREINIT:
    FunctionCategory *fncat;
    GnmFuncDescriptor desc;
    GnmFunc *func;

 CODE:
    fncat = function_get_category ("Perl plugin");
    desc.fn_name     = name;
    desc.args	     = args;;
    desc.arg_names   = named_args;
    desc.help	     = (char const **)&help;
    desc.fn_args     = &marshal_func;
    desc.fn_nodes    = NULL;
    desc.linker	     = NULL;
    desc.unlinker    = NULL;
    desc.flags	     = 0;
    desc.impl_status = GNM_FUNC_IMPL_STATUS_NOT_IN_EXCEL;
    desc.test_status = GNM_FUNC_TEST_STATUS_UNKNOWN;

    func = gnm_func_add (fncat, &desc);
    gnm_func_set_user_data (func, newSVsv (subref));

