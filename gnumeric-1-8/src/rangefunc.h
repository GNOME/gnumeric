/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_RANGEFUNC_H_
# define _GNM_RANGEFUNC_H_

#include "numbers.h"
#include <goffice/math/go-rangefunc.h>

G_BEGIN_DECLS

#ifdef WITH_LONG_DOUBLE
#	define gnm_range_sum go_range_suml
#	define gnm_range_sumsq go_range_sumsql
#	define gnm_range_average go_range_averagel
#	define gnm_range_min go_range_minl
#	define gnm_range_max go_range_maxl
#	define gnm_range_maxabs go_range_maxabsl
#	define gnm_range_devsq go_range_devsql
#	define gnm_range_fractile_inter go_range_fractile_interl
#	define gnm_range_fractile_inter_nonconst go_range_fractile_inter_nonconstl
#	define gnm_range_median_inter go_range_median_interl
#	define gnm_range_median_inter_nonconst go_range_median_inter_nonconstl
#else
#	define gnm_range_sum go_range_sum
#	define gnm_range_sumsq go_range_sumsq
#	define gnm_range_average go_range_average
#	define gnm_range_min go_range_min
#	define gnm_range_max go_range_max
#	define gnm_range_maxabs go_range_maxabs
#	define gnm_range_devsq go_range_devsq
#	define gnm_range_fractile_inter go_range_fractile_inter
#	define gnm_range_fractile_inter_nonconst go_range_fractile_inter_nonconst
#	define gnm_range_median_inter go_range_median_inter
#	define gnm_range_median_inter_nonconst go_range_median_inter_nonconst
#endif

int gnm_range_count		(gnm_float const *xs, int n, gnm_float *res);

int gnm_range_product	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_multinomial	(gnm_float const *xs, int n, gnm_float *res);

int gnm_range_avedev	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_hypot		(gnm_float const *xs, int n, gnm_float *res);

int gnm_range_harmonic_mean	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_geometric_mean (gnm_float const *xs, int n, gnm_float *res);

int gnm_range_minabs	(gnm_float const *xs, int n, gnm_float *res);

int gnm_range_var_pop	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_var_est	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_stddev_pop	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_stddev_est	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_skew_pop	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_skew_est	(gnm_float const *xs, int n, gnm_float *res);
int gnm_range_kurtosis_m3_pop (gnm_float const *xs, int n, gnm_float *res);
int gnm_range_kurtosis_m3_est (gnm_float const *xs, int n, gnm_float *res);

int gnm_range_covar		(gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res);
int gnm_range_correl_pop	(gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res);
int gnm_range_correl_est	(gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res);
int gnm_range_rsq_pop	(gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res);
int gnm_range_rsq_est	(gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res);

int gnm_range_mode	(gnm_float const *xs, int n, gnm_float *res);

int gnm_range_min_k_nonconst (gnm_float       *xs, int n, gnm_float *res, int k);

G_END_DECLS

#endif /* _GNM_RANGEFUNC_H_ */
