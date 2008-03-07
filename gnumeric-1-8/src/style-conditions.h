/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_STYLE_CONDITIONS_H_
# define _GNM_STYLE_CONDITIONS_H_

#include "gnumeric.h"

G_BEGIN_DECLS

/* This is persisted directly in .gnumeric files, DO NOT REMOVE OR REORDER */
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
	GNM_STYLE_COND_CUSTOM,

	/* New in Gnumeric 1.8 */
	GNM_STYLE_COND_CONTAINS_STR		= 0x10,
	GNM_STYLE_COND_NOT_CONTAINS_STR,
	GNM_STYLE_COND_BEGINS_WITH_STR,
	GNM_STYLE_COND_NOT_BEGINS_WITH_STR,
	GNM_STYLE_COND_ENDS_WITH_STR,
	GNM_STYLE_COND_NOT_ENDS_WITH_STR,

	GNM_STYLE_COND_CONTAINS_ERR,
	GNM_STYLE_COND_NOT_CONTAINS_ERR,

	GNM_STYLE_COND_CONTAINS_BLANKS,
	GNM_STYLE_COND_NOT_CONTAINS_BLANKS
} GnmStyleCondOp;

typedef struct {
	GnmStyle	 *overlay;
	GnmExprTop const *texpr[2];
	GnmStyleCondOp	  op;
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

gboolean      gnm_style_cond_is_valid (GnmStyleCond const *cond);

G_END_DECLS

#endif /* _GNM_STYLE_CONDITIONS_H_ */
