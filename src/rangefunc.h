#ifndef GNUMERIC_RANGEFUNC_H
#define GNUMERIC_RANGEFUNC_H

#include "numbers.h"

int range_count (const gnm_float *xs, int n, gnm_float *res);

int range_sum (const gnm_float *xs, int n, gnm_float *res);
int range_product (const gnm_float *xs, int n, gnm_float *res);
int range_multinomial (const gnm_float *xs, int n, gnm_float *res);

int range_sumsq (const gnm_float *xs, int n, gnm_float *res);
int range_avedev (const gnm_float *xs, int n, gnm_float *res);

int range_average (const gnm_float *xs, int n, gnm_float *res);
int range_harmonic_mean (const gnm_float *xs, int n, gnm_float *res);
int range_geometric_mean (const gnm_float *xs, int n, gnm_float *res);

int range_min (const gnm_float *xs, int n, gnm_float *res);
int range_max (const gnm_float *xs, int n, gnm_float *res);
int range_minabs (const gnm_float *xs, int n, gnm_float *res);
int range_maxabs (const gnm_float *xs, int n, gnm_float *res);

int range_devsq (const gnm_float *xs, int n, gnm_float *res);
int range_var_pop (const gnm_float *xs, int n, gnm_float *res);
int range_var_est (const gnm_float *xs, int n, gnm_float *res);
int range_stddev_pop (const gnm_float *xs, int n, gnm_float *res);
int range_stddev_est (const gnm_float *xs, int n, gnm_float *res);
int range_skew_pop (const gnm_float *xs, int n, gnm_float *res);
int range_skew_est (const gnm_float *xs, int n, gnm_float *res);
int range_kurtosis_m3_pop (const gnm_float *xs, int n, gnm_float *res);
int range_kurtosis_m3_est (const gnm_float *xs, int n, gnm_float *res);

int range_covar (const gnm_float *xs, const gnm_float *ys, int n, gnm_float *res);
int range_correl_pop (const gnm_float *xs, const gnm_float *ys, int n, gnm_float *res);
int range_correl_est (const gnm_float *xs, const gnm_float *ys, int n, gnm_float *res);
int range_rsq_pop (const gnm_float *xs, const gnm_float *ys, int n, gnm_float *res);
int range_rsq_est (const gnm_float *xs, const gnm_float *ys, int n, gnm_float *res);

int range_mode (const gnm_float *xs, int n, gnm_float *res);

int range_fractile_inter (const gnm_float *xs, int n, gnm_float *res, gnm_float f);
int range_fractile_inter_nonconst (gnm_float *xs, int n, gnm_float *res, gnm_float f);
int range_median_inter (const gnm_float *xs, int n, gnm_float *res);
int range_median_inter_nonconst (gnm_float *xs, int n, gnm_float *res);

int range_min_k (const gnm_float *xs, int n, gnm_float *res, int k);
int range_min_k_nonconst (gnm_float *xs, int n, gnm_float *res, int k);

#endif
