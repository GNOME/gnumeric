#ifndef GNUMERIC_STYLE_CONDITION_H
#define GNUMERIC_STYLE_CONDITION_H

#include "eval.h"
#include "gnumeric.h"

typedef enum {
	SCT_EXPR        = 10,
	SCT_CONSTRAINT  = 20,
	SCT_FLAGS       = 30
} StyleConditionType;

typedef enum {
	SCB_NONE,
	SCB_AND,
	SCB_OR,
	SCB_AND_PAIR,
	SCB_OR_PAIR
} StyleConditionBool;

typedef enum {
	SCO_UNDEFINED = -1,
	SCO_EQUAL = 0,
	SCO_NOT_EQUAL,
	SCO_GREATER,
	SCO_LESS,
	SCO_GREATER_EQUAL,
	SCO_LESS_EQUAL,
	SCO_BOOLEAN_EXPR,
	SCO_LAST
} StyleConditionOperator;

typedef struct {
	StyleConditionOperator  op;
	Dependent               dep;
	Value                  *val;
} StyleConditionExpr;

typedef enum {
	SCC_IS_INT,
	SCC_IS_FLOAT,
	SCC_IS_IN_LIST,
	SCC_IS_DATE,
	SCC_IS_TIME,
	SCC_IS_TEXTLEN,
	SCC_IS_CUSTOM
} StyleConditionConstraint;

typedef enum {
	SCF_ALLOW_BLANK      = 1 << 0,
	SCF_IN_CELL_DROPDOWN = 1 << 1
} StyleConditionFlags;

struct _StyleCondition {
	StyleConditionType type;
	int                ref_count;

	union {
		StyleConditionExpr       expr;
		StyleConditionConstraint constraint;
		StyleConditionFlags      flags;
	} u;
	
	struct _StyleCondition *next;
	StyleConditionBool      next_op;
};

StyleCondition *style_condition_new_expr       (StyleConditionOperator op,
						ExprTree *expr);
StyleCondition *style_condition_new_constraint (StyleConditionConstraint constraint);
StyleCondition *style_condition_new_flags      (StyleConditionFlags flags);

void            style_condition_ref    (StyleCondition *sc);
void            style_condition_unref  (StyleCondition *sc);
void	 	style_condition_link   (StyleCondition *sc, Sheet *sheet);
void	 	style_condition_unlink (StyleCondition *sc);

void            style_condition_dump   (StyleCondition *sc);
StyleCondition *style_condition_chain  (StyleCondition *dst, StyleConditionBool op,
				        StyleCondition *src);
gboolean        style_condition_eval   (StyleCondition *sc, Value *val,
					StyleFormat *format);


#endif /* GNUMERIC_STYLE_CONDITION_H */
