#ifndef GNM_EXPR_DERIV_H_
#define GNM_EXPR_DERIV_H_

G_BEGIN_DECLS

#include <gnumeric-fwd.h>
#include <expr.h>
#include <numbers.h>

/* ------------------------------------------------------------------------- */

GType gnm_expr_deriv_info_get_type (void);

GnmExprDeriv *gnm_expr_deriv_info_new (void);
GnmExprDeriv *gnm_expr_deriv_info_ref (GnmExprDeriv *deriv);
void gnm_expr_deriv_info_unref (GnmExprDeriv *deriv);

void gnm_expr_deriv_info_set_var (GnmExprDeriv *deriv, GnmEvalPos const *var);

/* ------------------------------------------------------------------------- */

GnmExpr const *gnm_expr_deriv (GnmExpr const *expr,
			       GnmEvalPos const *ep,
			       GnmExprDeriv *info);


GnmExprTop const *gnm_expr_top_deriv (GnmExprTop const *texpr,
				      GnmEvalPos const *ep,
				      GnmExprDeriv *info);

GnmExprTop const *gnm_expr_cell_deriv (GnmCell *y, GnmCell *x);

gnm_float gnm_expr_cell_deriv_value (GnmCell *y, GnmCell *x);

GnmExpr const *gnm_expr_deriv_chain (GnmExpr const *expr,
				     GnmExpr const *deriv,
				     GnmEvalPos const *ep,
				     GnmExprDeriv *info);

GnmExprList *gnm_expr_deriv_collect (GnmExpr const *expr,
				     GnmEvalPos const *ep,
				     GnmExprDeriv *info);

/* ------------------------------------------------------------------------- */

void gnm_expr_deriv_shutdown_ (void);

/* ------------------------------------------------------------------------- */

G_END_DECLS

#endif
