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
			lower_tail = !lower_tail, p = -gnm_expm1 (p);
		else
			p = gnm_exp (p);
	}
	if (lower_tail) scale = -scale;
	return location + scale / gnm_tan(M_PIgnum * p);
}

/* ------------------------------------------------------------------------- */

static gnm_float
phyper1 (gnm_float x, const gnm_float shape[],
	 gboolean lower_tail, gboolean log_p)
{
	return phyper (x, shape[0], shape[1], shape[2], lower_tail, log_p);
}

gnm_float
qhyper (gnm_float p, gnm_float NR, gnm_float NB, gnm_float n,
	gboolean lower_tail, gboolean log_p)
{
	gnm_float y, shape[3];
	gnm_float N = NR + NB;

	if (gnm_isnan (p) || gnm_isnan (N) || gnm_isnan (n))
		return p + N + n;
	if(!gnm_finite (p) || !gnm_finite (N) ||
	   NR < 0 || NB < 0 || n < 0 || n > N)
		ML_ERR_return_NAN;

	shape[0] = NR;
	shape[1] = NB;
	shape[2] = n;

	if (N > 2) {
		gnm_float mu = n * NR / N;
		gnm_float sigma =
			gnm_sqrt (NR * NB * n * (N - n) / (N * N * (N - 1)));
		gnm_float sigma_gamma =
			(N - 2 * NR) * (N - 2 * n) / ((N - 2) * N);
		gnm_float z = qnorm (p, 0., 1., lower_tail, log_p);
		y = mu + sigma * z + sigma_gamma * (z * z - 1) / 6;
	} else
		y = 0;

	return discpfuncinverter (p, shape, lower_tail, log_p,
				  MAX (0, n - NB), MIN (n, NR), y,
				  phyper1);
}

/* ------------------------------------------------------------------------- */
