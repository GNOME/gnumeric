#ifndef GNM_EXPR_DERIV_H_
#define GNM_EXPR_DERIV_H_

G_BEGIN_DECLS

#include "expr.h"
#include "numbers.h"

/* ------------------------------------------------------------------------- */

typedef struct GnmExprDeriv_ GnmExprDeriv;

GnmExprDeriv *gnm_expr_deriv_info_new (void);
void gnm_expr_deriv_info_free (GnmExprDeriv *deriv);

void gnm_expr_deriv_info_set_var (GnmExprDeriv *deriv, GnmEvalPos const *var);

/* ------------------------------------------------------------------------- */

GnmExprTop const *gnm_expr_top_deriv (GnmExprTop const *texpr,
				      GnmEvalPos const *ep,
				      GnmExprDeriv *info);

GnmExprTop const *gnm_expr_cell_deriv (GnmCell *y, GnmCell *x);

gnm_float gnm_expr_cell_deriv_value (GnmCell *y, GnmCell *x);

/* ------------------------------------------------------------------------- */

G_END_DECLS

#endif
