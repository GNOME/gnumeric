#ifndef GNM_STYLE_CONDITIONS_H
#define GNM_STYLE_CONDITIONS_H

#include "gnumeric.h"

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

	/* New in Gnumeric 2.0 */
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

	/*NOTE: we should really have a case-independent version of the
	 *  above.  It should be much easier with GNU extensions, but it
	 *  must also be portable!  Maybe this isn't a problem, though;
	 *  More research is needed.*/
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

#endif /* GNM_STYLE_CONDITIONS_H */
