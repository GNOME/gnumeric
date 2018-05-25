#include <gnumeric-config.h>
#include "perl-gnumeric.h"

SV *
value2perl(const GnmValue *v)
{
	SV *sv;

	switch (v->v_any.type) {
	case VALUE_BOOLEAN:
		sv = newSViv(value_get_as_int (v));
		break;

	case VALUE_FLOAT:
		sv = newSVnv(value_get_as_float (v));
		break;

	case VALUE_STRING: {
		const char *s = value_peek_string (v);
		sv = newSVpv(s,strlen(s));
		break;
	}

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
	gchar *tmp;

	tmp = SvPV(sv, size);
	v = value_new_string_nocopy (g_strndup (tmp, size));
    }

    return v;
}

#if 0
GnmValue *
marshal_func (GnmFuncEvalInfo *ei, GnmValue *argv[])
{
    dSP;
    GnmFunc const *func = gnm_eval_info_get_func (ei);
    I32 r;
    int i, min, max;
    SV * result;
    GnmValue *v;

    /* Read the perlcall man page for more information. */
    ENTER;
    SAVETMPS;

    PUSHMARK(sp);
    gnm_func_count_args (func, &min, &max);

    for (i = 0; i < max && argv[i] != NULL; i++) {
	XPUSHs(sv_2mortal(value2perl(argv[i])));
    }
    PUTBACK;

    /* gnm_func_get_user_data doesn't exist anymore and in any case
       it was never set to a value sane for this. */
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
#endif
