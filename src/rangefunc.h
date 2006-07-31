#ifndef GNUMERIC_RANGEFUNC_H
#define GNUMERIC_RANGEFUNC_H

#include "numbers.h"

int range_count		(gnm_float const *xs, int n, gnm_float *res);

int range_sum		(gnm_float const *xs, int n, gnm_float *res);
int range_product	(gnm_float const *xs, int n, gnm_float *res);
int range_multinomial	(gnm_float const *xs, int n, gnm_float *res);

int range_sumsq		(gnm_float const *xs, int n, gnm_float *res);
int range_avedev	(gnm_float const *xs, int n, gnm_float *res);
int range_hypot		(gnm_float const *xs, int n, gnm_float *res);

int range_average	(gnm_float const *xs, int n, gnm_float *res);
int range_harmonic_mean	(gnm_float const *xs, int n, gnm_float *res);
int range_geometric_mean (gnm_float const *xs, int n, gnm_float *res);

int range_min		(gnm_float const *xs, int n, gnm_float *res);
int range_max		(gnm_float const *xs, int n, gnm_float *res);
int range_minabs	(gnm_float const *xs, int n, gnm_float *res);
int range_maxabs	(gnm_float const *xs, int n, gnm_float *res);

int range_devsq		(gnm_float const *xs, int n, gnm_float *res);
int range_var_pop	(gnm_float const *xs, int n, gnm_float *res);
int range_var_est	(gnm_float const *xs, int n, gnm_float *res);
int range_stddev_pop	(gnm_float const *xs, int n, gnm_float *res);
int range_stddev_est	(gnm_float const *xs, int n, gnm_float *res);
int range_skew_pop	(gnm_float const *xs, int n, gnm_float *res);
int range_skew_est	(gnm_float const *xs, int n, gnm_float *res);
int range_kurtosis_m3_pop (gnm_float const *xs, int n, gnm_float *res);
int range_kurtosis_m3_est (gnm_float const *xs, int n, gnm_float *res);

int range_covar		(gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res);
int range_correl_pop	(gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res);
int range_correl_est	(gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res);
int range_rsq_pop	(gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res);
int range_rsq_est	(gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res);

int range_mode	(gnm_float const *xs, int n, gnm_float *res);

int range_fractile_inter	  (gnm_float const *xs, int n, gnm_float *res, gnm_float f);
int range_fractile_inter_nonconst (gnm_float       *xs, int n, gnm_float *res, gnm_float f);
int range_median_inter		(gnm_float const *xs, int n, gnm_float *res);
int range_median_inter_nonconst (gnm_float       *xs, int n, gnm_float *res);

int range_min_k		 (gnm_float const *xs, int n, gnm_float *res, int k);
int range_min_k_nonconst (gnm_float       *xs, int n, gnm_float *res, int k);

#endif
