#ifndef GNUMERIC_VALIDATION_H
#define GNUMERIC_VALIDATION_H

#include "gnumeric.h"
#include "str.h"

typedef enum {
	VALIDATION_STATUS_VALID,		/* things validate */
	VALIDATION_STATUS_INVALID_DISCARD,	/* things do not validate and should be discarded */
	VALIDATION_STATUS_INVALID_EDIT		/* things do not validate and editing should continue */
} ValidationStatus;
typedef enum {
	VALIDATION_STYLE_NONE,
	VALIDATION_STYLE_STOP,
	VALIDATION_STYLE_WARNING,
	VALIDATION_STYLE_INFO
} ValidationStyle;
typedef enum {
	VALIDATION_TYPE_ANY,
	VALIDATION_TYPE_AS_INT,
	VALIDATION_TYPE_AS_NUMBER,
	VALIDATION_TYPE_IN_LIST,
	VALIDATION_TYPE_AS_DATE,
	VALIDATION_TYPE_AS_TIME,
	VALIDATION_TYPE_TEXT_LENGTH,
	VALIDATION_TYPE_CUSTOM
} ValidationType;
typedef enum {
	VALIDATION_OP_NONE = -1,
	VALIDATION_OP_BETWEEN,
	VALIDATION_OP_NOT_BETWEEN,
	VALIDATION_OP_EQUAL,
	VALIDATION_OP_NOT_EQUAL,
	VALIDATION_OP_GT,
	VALIDATION_OP_LT,
	VALIDATION_OP_GTE,
	VALIDATION_OP_LTE
} ValidationOp;

struct _Validation {
	int              ref_count;

	String          *title;
	String          *msg;
	GnmExpr	const *expr [2];
	ValidationStyle  style;
	ValidationType	 type;
	ValidationOp	 op;
	gboolean	 allow_blank : 1;
	gboolean	 use_dropdown : 1;
};

Validation *validation_new   (ValidationStyle style,
			      ValidationType  type,
			      ValidationOp    op,
			      char const *title, char const *msg,
			      GnmExpr const *expr0, GnmExpr const *expr1,
			      gboolean allow_blank, gboolean use_dropdown);

void        validation_ref    (Validation *v);
void        validation_unref  (Validation *v);
ValidationStatus validation_eval (WorkbookControl *wbc, MStyle const *mstyle,
				  Sheet *sheet, CellPos const *pos);

#endif /* GNUMERIC_VALIDATION_H */
