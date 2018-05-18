#ifndef GNM_EXPR_DERIV_H_
#define GNM_EXPR_DERIV_H_

G_BEGIN_DECLS

#include <expr.h>
#include <numbers.h>

/* ------------------------------------------------------------------------- */

typedef struct GnmExprDeriv_ GnmExprDeriv;

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

/* ------------------------------------------------------------------------- */

GnmExprList *gnm_expr_deriv_collect (GnmExpr const *expr,
				     GnmEvalPos const *ep,
				     GnmExprDeriv *info);

typedef GnmExpr const * (*GnmExprDerivHandler) (GnmExpr const *expr,
						GnmEvalPos const *ep,
						GnmExprDeriv *info,
						gpointer user);
typedef enum {
	GNM_EXPR_DERIV_NO_CHAIN = 0x0,
	GNM_EXPR_DERIV_CHAIN = 0x1
} GnmExprDerivFlags;

void gnm_expr_deriv_install_handler (GnmFunc *func, GnmExprDerivHandler h,
				     GnmExprDerivFlags flags,
				     gpointer data, GDestroyNotify notify);
void gnm_expr_deriv_uninstall_handler (GnmFunc *func);
void _gnm_expr_deriv_shutdown (void);

/* ------------------------------------------------------------------------- */

G_END_DECLS

#endif
