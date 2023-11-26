#ifndef _GNM_RANGEFUNC_H_
# define _GNM_RANGEFUNC_H_

#include <numbers.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

#define gnm_range_sum GNM_SUFFIX(go_range_sum)
#define gnm_range_sumsq GNM_SUFFIX(go_range_sumsq)
#define gnm_range_average GNM_SUFFIX(go_range_average)
#define gnm_range_min GNM_SUFFIX(go_range_min)
#define gnm_range_max GNM_SUFFIX(go_range_max)
#define gnm_range_maxabs GNM_SUFFIX(go_range_maxabs)
#define gnm_range_devsq GNM_SUFFIX(go_range_devsq)
#define gnm_range_fractile_inter_sorted GNM_SUFFIX(go_range_fractile_inter_sorted)
#define gnm_range_median_inter GNM_SUFFIX(go_range_median_inter)
#define gnm_range_median_inter_sorted GNM_SUFFIX(go_range_median_inter_sorted)
#define gnm_range_increasing GNM_SUFFIX(go_range_increasing)
#define gnm_range_sort GNM_SUFFIX(go_range_sort)

int gnm_range_count		(gnm_float const *xs, int n, gnm_float *res);

int gnm_range_product	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_multinomial	(gnm_float const *xs, int n, gnm_float *res);

int gnm_range_avedev	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_hypot		(gnm_float const *xs, int n, gnm_float *res);

int gnm_range_harmonic_mean	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_geometric_mean (gnm_float const *xs, int n, gnm_float *res);

int gnm_range_var_pop	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_var_est	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_stddev_pop	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_stddev_est	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_skew_pop	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_skew_est	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_kurtosis_m3_pop (gnm_float const *xs, int n, gnm_float *res);
int gnm_range_kurtosis_m3_est (gnm_float const *xs, int n, gnm_float *res);

int gnm_range_covar_pop		(gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res);
int gnm_range_covar_est		(gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res);
int gnm_range_correl_pop	(gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res);
int gnm_range_rsq_pop	(gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res);

int gnm_range_mode	(gnm_float const *xs, int n, gnm_float *res);

int gnm_range_adtest    (gnm_float const *xs, int n, gnm_float *p,
			 gnm_float *statistics);

G_END_DECLS

#endif /* _GNM_RANGEFUNC_H_ */
