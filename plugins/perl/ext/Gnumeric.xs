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

#include <glib.h>
#include <gnome.h>

#include <gnumeric.h>
#include <expr.h>
#include <func.h>

static SV *
value2perl(Value *v)
{
    SV *sv;

    switch (v->type) {
    case VALUE_INTEGER:
	sv = newSViv(v->v.v_int);
	break;
	
    case VALUE_FLOAT:
	sv = newSVnv(v->v.v_float);
	break;
	
    case VALUE_STRING:
	sv = newSVpv(v->v.str->str, strlen(v->v.str->str));
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
    Value *v = g_new (Value, 1);

    if (!v)
	return NULL;

    if (SvIOK(sv)){
	v->type = VALUE_INTEGER;
	v->v.v_int = (int_t) SvIV(sv);
    } else if (SvNOK(sv)) {
	v->type = VALUE_FLOAT;
	v->v.v_float = (float_t) SvNV(sv);
    } else if (SvPOK(sv)) {
	STRLEN size;
	gchar *s,*tmp;

	tmp = SvPV(sv, size);
	
	s = g_malloc (size + 1);
	strncpy(s, tmp, size);
	s[size] = '\0';
	v->type = VALUE_STRING;
	v->v.str = string_get (s);
	g_free (s);
    } else {
	g_free (v);
	return NULL;
    }

    return v;
}

typedef struct {
    FunctionDefinition *fndef;
    SV * codeSV;
} FuncData;

static GList *funclist = NULL;

static int
fndef_compare(FuncData *fdata, FunctionDefinition *fndef)
{
    return (fdata->fndef != fndef);
}

static Value *
marshal_func (FunctionEvalInfo *ei, Value *argv[])
{
    dSP;
    FunctionDefinition *fndef = ei->func_def;
    GList *l;
    int count = strlen(fndef->args), r, i;
    SV * result;
    Value *v;

    l = g_list_find_custom(funclist, fndef, (GCompareFunc) fndef_compare);
    if (!l) {
	error_message_set (ei->error, "Unable to lookup Perl code object.");
	return NULL;
    }

    /* Read the perlcall man page for more information. */
    ENTER;
    SAVETMPS;

    PUSHMARK(sp);
    for (i = 0; i < count; i++) {
	XPUSHs(sv_2mortal(value2perl(argv[i])));
    }
    PUTBACK;

    r = perl_call_sv(((FuncData *)(l->data))->codeSV, G_SCALAR);
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
    FuncData *fdata;
    char **help = NULL;
  CODE:
    fncat = function_get_category ("Perl plugin");

    if (help1) {
	    help = g_new (char *, 1);
            *help = g_strdup (help1);
    }
    fndef = function_add_args (fncat, g_strdup(name), g_strdup(args),
	g_strdup (named_args), help, marshal_func);

    fdata = g_new (FuncData, 1);
    fdata->fndef = fndef;
    fdata->codeSV = newSVsv(subref);
    funclist = g_list_append(funclist, fdata);


