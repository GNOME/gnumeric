/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_REGRESSION_H_
# define _GNM_REGRESSION_H_

#include "numbers.h"
#include <goffice/math/go-regression.h>

G_BEGIN_DECLS

#ifdef WITH_LONG_DOUBLE
#	define gnm_regression_stat_t go_regression_stat_tl
#	define gnm_regression_stat_new go_regression_stat_newl
#	define gnm_regression_stat_destroy go_regression_stat_destroyl
#	define gnm_linear_regression go_linear_regressionl
#	define gnm_exponential_regression go_exponential_regressionl
#	define gnm_logarithmic_regression go_logarithmic_regressionl
#	define gnm_logarithmic_fit go_logarithmic_fitl
#	define GnmRegressionFunction GORegressionFunctionl
#	define gnm_non_linear_regression go_non_linear_regressionl
#	define gnm_matrix_invert go_matrix_invertl
#	define gnm_matrix_determinant go_matrix_determinantl
#else
#	define gnm_regression_stat_t go_regression_stat_t
#	define gnm_regression_stat_new go_regression_stat_new
#	define gnm_regression_stat_destroy go_regression_stat_destroy
#	define gnm_linear_regression go_linear_regression
#	define gnm_exponential_regression go_exponential_regression
#	define gnm_logarithmic_regression go_logarithmic_regression
#	define gnm_logarithmic_fit go_logarithmic_fit
#	define GnmRegressionFunction GORegressionFunction
#	define gnm_non_linear_regression go_non_linear_regression
#	define gnm_matrix_invert go_matrix_invert
#	define gnm_matrix_determinant go_matrix_determinant
#endif

G_END_DECLS

#endif /* _GNM_REGRESSION_H_ */
