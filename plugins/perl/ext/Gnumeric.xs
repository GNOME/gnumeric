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
value2perl(GnmValue *v)
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

static GnmValue *
perl2value(SV *sv)
{
    GnmValue *v = NULL;

    if (SvIOK(sv))
	v = value_new_int (SvIV(sv));
    else if (SvNOK(sv))
	v = value_new_float ((gnm_float) SvNV(sv));
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

static GnmValue *
marshal_func (FunctionEvalInfo *ei, GnmValue *argv[])
{
    dSP;
    GnmFunc const *func =
	gnm_expr_get_func_def ((GnmExpr const *)ei->func_call);
    GList *l;
    I32 r;
    int i, min, max;
    SV * result;
    GnmValue *v;

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
register_function(name, arg_spec, arg_names, help, subref)
    char *name
    char *arg_spec
    char *arg_names
    char *help
    SV *subref

 PREINIT:
    GnmFuncGroup *fncat;
    GnmFuncDescriptor desc;
    GnmFunc *func;

 CODE:
    fncat = gnm_func_group_fetch ("Perl plugin");
    desc.name     = name;
    desc.arg_spec     = arg_spec;
    desc.arg_names   = arg_names;
    desc.help	     = (char const **)&help;
    desc.fn_args     = &marshal_func;
    desc.fn_nodes    = NULL;
    desc.linker	     = NULL;
    desc.unlinker    = NULL;
    desc.flags	     = 0;
    desc.impl_status = GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC;
    desc.test_status = GNM_FUNC_TEST_STATUS_UNKNOWN;

    func = gnm_func_add (fncat, &desc);
    gnm_func_set_user_data (func, newSVsv (subref));

