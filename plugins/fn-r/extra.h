#ifndef GNM_FN_R_EXTRA_H
#define GNM_FN_R_EXTRA_H

#include <numbers.h>
#include <glib.h>

gnm_float qcauchy (gnm_float p, gnm_float location, gnm_float scale, gboolean lower_tail, gboolean log_p);

/* The skew-normal distribution.  */
gnm_float dsnorm (gnm_float x, gnm_float shape, gnm_float location, gnm_float scale, gboolean give_log);
gnm_float psnorm (gnm_float x, gnm_float shape, gnm_float location, gnm_float scale, gboolean lower_tail, gboolean log_p);
gnm_float qsnorm (gnm_float p, gnm_float shape, gnm_float location, gnm_float scale, gboolean lower_tail, gboolean log_p);

/* The skew-t distribution.  */
gnm_float dst (gnm_float x, gnm_float n, gnm_float shape, gboolean give_log);
gnm_float pst (gnm_float x, gnm_float n, gnm_float shape, gboolean lower_tail, gboolean log_p);
gnm_float qst (gnm_float p, gnm_float n, gnm_float shape, gboolean lower_tail, gboolean log_p);

/* The Gumbel distribution */
gnm_float dgumbel (gnm_float x, gnm_float mu, gnm_float beta, gboolean give_log);
gnm_float pgumbel (gnm_float x, gnm_float mu, gnm_float beta, gboolean lower_tail, gboolean log_p);
gnm_float qgumbel (gnm_float p, gnm_float mu, gnm_float beta, gboolean lower_tail, gboolean log_p);

#endif
