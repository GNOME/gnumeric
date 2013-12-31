#ifndef GNM_SF_DPQ_H_
#define GNM_SF_DPQ_H_

#include <numbers.h>

/* ------------------------------------------------------------------------- */

typedef gnm_float (*GnmPFunc) (gnm_float x, const gnm_float shape[],
			       gboolean lower_tail, gboolean log_p);
typedef gnm_float (*GnmDPFunc) (gnm_float x, const gnm_float shape[],
				gboolean log_p);

gnm_float pfuncinverter (gnm_float p, const gnm_float shape[],
			 gboolean lower_tail, gboolean log_p,
			 gnm_float xlow, gnm_float xhigh, gnm_float x0,
			 GnmPFunc pfunc, GnmDPFunc dpfunc_dx);
gnm_float discpfuncinverter (gnm_float p, const gnm_float shape[],
			     gboolean lower_tail, gboolean log_p,
			     gnm_float xlow, gnm_float xhigh, gnm_float x0,
			     GnmPFunc pfunc);

/* ------------------------------------------------------------------------- */

/* The normal distribution.  */
gnm_float dnorm (gnm_float x, gnm_float mu, gnm_float sigma, gboolean give_log);
gnm_float pnorm2 (gnm_float x1, gnm_float x2);

/* The log-normal distribution.  */
gnm_float dlnorm (gnm_float x, gnm_float logmean, gnm_float logsd, gboolean give_log);
gnm_float plnorm (gnm_float x, gnm_float logmean, gnm_float logsd, gboolean lower_tail, gboolean log_p);
gnm_float qlnorm (gnm_float p, gnm_float logmean, gnm_float logsd, gboolean lower_tail, gboolean log_p);

/* ------------------------------------------------------------------------- */

#endif
