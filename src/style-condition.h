#ifndef GNUMERIC_STYLE_VALIDATION_H
#define GNUMERIC_STYLE_VALIDATION_H

#include "eval.h"
#include "gnumeric.h"

/*
 * FIXME: Where do we put this?
 */
typedef enum _ValidationStyle {
	STYLE_NONE,
	STYLE_STOP,
	STYLE_ERROR,
	STYLE_WARNING
} ValidationStyle;

typedef enum _StyleConditionOp {
	EQUAL,
	NOT_EQUAL,
	LESS,
	GREATER,
	LESS_EQUAL,
	GREATER_EQUAL
} StyleConditionOp;

struct _StyleCondition {
	int                     ref_count;
	StyleConditionOp        op;
	Dependent               dep;
	Value                  *val;

	struct _StyleCondition *next;
};

StyleCondition *style_condition_new   (Sheet *sheet, StyleConditionOp op, ExprTree *expr);
void            style_condition_ref   (StyleCondition *sc);
void            style_condition_unref (StyleCondition *sc);

StyleCondition *style_condition_chain (StyleCondition *dst, StyleCondition *src);
gboolean        style_condition_eval  (StyleCondition *sc, Value *val);

#endif /* GNUMERIC_STYLE_VALIDATION_H */
