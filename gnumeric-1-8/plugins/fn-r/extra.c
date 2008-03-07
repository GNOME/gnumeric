#include <gnumeric-config.h>
#include "gnumeric.h"
#include <mathfunc.h>
#include "extra.h"

#define ML_ERR_return_NAN { return gnm_nan; }

/* ------------------------------------------------------------------------- */
/* --- BEGIN MAGIC R SOURCE MARKER --- */

#define R_Q_P01_check(p)			\
    if ((log_p	&& p > 0) ||			\
	(!log_p && (p < 0 || p > 1)) )		\
	ML_ERR_return_NAN


/* ------------------------------------------------------------------------ */
/* --- END MAGIC R SOURCE MARKER --- */

gnm_float
qcauchy (gnm_float p, gnm_float location, gnm_float scale,
	 gboolean lower_tail, gboolean log_p)
{
	if (gnm_isnan(p) || gnm_isnan(location) || gnm_isnan(scale))
		return p + location + scale;

	R_Q_P01_check(p);
	if (scale < 0 || !gnm_finite(scale)) ML_ERR_return_NAN;

	if (log_p) {
		if (p > -1)
			/* The "0" here is important for the p=0 case:  */
			lower_tail = !lower_tail, p = 0 - gnm_expm1 (p);
		else
			p = gnm_exp (p);
	}
	if (lower_tail) scale = -scale;
	return location + scale / gnm_tan(M_PIgnum * p);
}

/* ------------------------------------------------------------------------- */
