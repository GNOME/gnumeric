#ifndef GNM_STYLE_CONDITIONS_H
#define GNM_STYLE_CONDITIONS_H

#include "gnumeric.h"

typedef enum {
	/* Cell Value */
	GNM_STYLE_COND_BETWEEN,
	GNM_STYLE_COND_NOT_BETWEEN,
	GNM_STYLE_COND_EQUAL,
	GNM_STYLE_COND_NOT_EQUAL,
	GNM_STYLE_COND_GT,
	GNM_STYLE_COND_LT,
	GNM_STYLE_COND_GTE,
	GNM_STYLE_COND_LTE,

	/* Arbitrary expr evaluated at EvalPos */
	GNM_STYLE_COND_CUSTOM
} GnmStyleCondOp;

typedef struct {
	GnmStyle	*overlay;
	GnmExpr	const   *expr [2];
	GnmStyleCondOp	 op;
} GnmStyleCond;

GnmStyleConditions *gnm_style_conditions_new  (void);
GArray const *gnm_style_conditions_details (GnmStyleConditions const *sc);
void	      gnm_style_conditions_insert  (GnmStyleConditions *sc,
					    GnmStyleCond const *cond,
					    int pos);
GPtrArray    *gnm_style_conditions_overlay (GnmStyleConditions const *sc,
					    GnmStyle const *base);
int	      gnm_style_conditions_eval    (GnmStyleConditions const *sc,
					    GnmEvalPos const *pos);

#endif /* GNM_STYLE_CONDITIONS_H */
