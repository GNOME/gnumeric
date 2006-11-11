#include "perl-gnumeric.h"

SV *
value2perl(GnmValue *v)
{
    SV *sv;

    switch (v->type) {
    case VALUE_BOOLEAN:
	sv = newSViv(v->v_bool.val);
	break;

    case VALUE_FLOAT:
	sv = newSVnv(value_get_as_float (v));
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

GnmValue *
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

	s = g_strndup (tmp, size);
	v = value_new_string (s);
	g_free (s);
    }

    return v;
}

GnmValue *
marshal_func (GnmFuncEvalInfo *ei, GnmValue *argv[])
{
    dSP;
    GnmFunc const *func =
	gnm_expr_get_func_def ((GnmExpr const *)ei->func_call);
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
