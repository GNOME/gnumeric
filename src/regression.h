#ifndef _GNM_REGRESSION_H_
# define _GNM_REGRESSION_H_

#include <numbers.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

#define gnm_regression_stat_t GNM_SUFFIX(go_regression_stat_t)
#define gnm_regression_stat_new GNM_SUFFIX(go_regression_stat_new)
#define gnm_regression_stat_destroy GNM_SUFFIX(go_regression_stat_destroy)
#define gnm_linear_regression GNM_SUFFIX(go_linear_regression)
#define gnm_linear_regression_leverage GNM_SUFFIX(go_linear_regression_leverage)
#define gnm_exponential_regression GNM_SUFFIX(go_exponential_regression)
#define gnm_logarithmic_regression GNM_SUFFIX(go_logarithmic_regression)
#define gnm_logarithmic_fit GNM_SUFFIX(go_logarithmic_fit)
#define GnmRegressionFunction GNM_SUFFIX(GORegressionFunction)
#define gnm_non_linear_regression GNM_SUFFIX(go_non_linear_regression)
#define gnm_matrix_invert GNM_SUFFIX(go_matrix_invert)
#define gnm_matrix_pseudo_inverse GNM_SUFFIX(go_matrix_pseudo_inverse)
#define gnm_matrix_determinant GNM_SUFFIX(go_matrix_determinant)

G_END_DECLS

#endif /* _GNM_REGRESSION_H_ */
