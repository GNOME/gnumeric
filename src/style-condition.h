#ifndef GNUMERIC_STYLE_VALIDATION_H
#define GNUMERIC_STYLE_VALIDATION_H

#include "eval.h"
#include "gnumeric.h"

/*
 * FIXME: Where do we put this?
 */
typedef enum _ValidationStyle {
	VALIDATION_STYLE_NONE,
	VALIDATION_STYLE_STOP,
	VALIDATION_STYLE_ERROR,
	VALIDATION_STYLE_WARNING
} ValidationStyle;

typedef enum _StyleConditionOp {
	STYLE_CONDITION_EQUAL,
	STYLE_CONDITION_NOT_EQUAL,
	STYLE_CONDITION_GREATER,
	STYLE_CONDITION_LESS,
	STYLE_CONDITION_GREATER_EQUAL,
	STYLE_CONDITION_LESS_EQUAL,
	
	STYLE_CONDITION_IS_INT,
	STYLE_CONDITION_IS_FLOAT,
	STYLE_CONDITION_IS_IN_LIST,
	STYLE_CONDITION_IS_DATE,
	STYLE_CONDITION_IS_TIME,
	STYLE_CONDITION_IS_TEXTLEN
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
gboolean        style_condition_eval  (StyleCondition *sc, Value *val, StyleFormat *format);

#endif /* GNUMERIC_STYLE_VALIDATION_H */
