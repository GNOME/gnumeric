#ifndef GNUMERIC_RANGEFUNC_H
#define GNUMERIC_RANGEFUNC_H

#include "numbers.h"

int range_sum (const gnum_float *xs, int n, gnum_float *res);
int range_product (const gnum_float *xs, int n, gnum_float *res);
int range_multinomial (const gnum_float *xs, int n, gnum_float *res);

int range_sumsq (const gnum_float *xs, int n, gnum_float *res);
int range_avedev (const gnum_float *xs, int n, gnum_float *res);

int range_average (const gnum_float *xs, int n, gnum_float *res);
int range_harmonic_mean (const gnum_float *xs, int n, gnum_float *res);
int range_geometric_mean (const gnum_float *xs, int n, gnum_float *res);

int range_min (const gnum_float *xs, int n, gnum_float *res);
int range_max (const gnum_float *xs, int n, gnum_float *res);

int range_devsq (const gnum_float *xs, int n, gnum_float *res);
int range_var_pop (const gnum_float *xs, int n, gnum_float *res);
int range_var_est (const gnum_float *xs, int n, gnum_float *res);
int range_stddev_pop (const gnum_float *xs, int n, gnum_float *res);
int range_stddev_est (const gnum_float *xs, int n, gnum_float *res);
int range_skew_pop (const gnum_float *xs, int n, gnum_float *res);
int range_skew_est (const gnum_float *xs, int n, gnum_float *res);
int range_kurtosis_m3_pop (const gnum_float *xs, int n, gnum_float *res);
int range_kurtosis_m3_est (const gnum_float *xs, int n, gnum_float *res);

int range_covar (const gnum_float *xs, const gnum_float *ys, int n, gnum_float *res);
int range_correl_pop (const gnum_float *xs, const gnum_float *ys, int n, gnum_float *res);
int range_correl_est (const gnum_float *xs, const gnum_float *ys, int n, gnum_float *res);
int range_rsq_pop (const gnum_float *xs, const gnum_float *ys, int n, gnum_float *res);
int range_rsq_est (const gnum_float *xs, const gnum_float *ys, int n, gnum_float *res);

int range_mode (const gnum_float *xs, int n, gnum_float *res);

int range_fractile_inter (const gnum_float *xs, int n, gnum_float *res, gnum_float f);
int range_fractile_inter_nonconst (gnum_float *xs, int n, gnum_float *res, gnum_float f);
int range_median_inter (const gnum_float *xs, int n, gnum_float *res);
int range_median_inter_nonconst (gnum_float *xs, int n, gnum_float *res);

int range_min_k (const gnum_float *xs, int n, gnum_float *res, int k);
int range_min_k_nonconst (gnum_float *xs, int n, gnum_float *res, int k);

#endif
