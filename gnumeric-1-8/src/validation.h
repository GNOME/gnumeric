/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_VALIDATION_H_
# define _GNM_VALIDATION_H_

#include "gnumeric.h"
#include "str.h"

G_BEGIN_DECLS

typedef enum {
	VALIDATION_STATUS_VALID,		/* things validate */
	VALIDATION_STATUS_INVALID_DISCARD,	/* things do not validate and should be discarded */
	VALIDATION_STATUS_INVALID_EDIT		/* things do not validate and editing should continue */
} ValidationStatus;
typedef enum {
	VALIDATION_STYLE_NONE,
	VALIDATION_STYLE_STOP,
	VALIDATION_STYLE_WARNING,
	VALIDATION_STYLE_INFO,
	VALIDATION_STYLE_PARSE_ERROR
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

struct _GnmValidation {
	int               ref_count;

	GnmString        *title;
	GnmString        *msg;
	GnmExprTop const *texpr[2];
	ValidationStyle   style;
	ValidationType	  type;
	ValidationOp	  op;
	gboolean	  allow_blank;
	gboolean	  use_dropdown;
};

GnmValidation *validation_new   (ValidationStyle style,
				 ValidationType type,
				 ValidationOp op,
				 char const *title, char const *msg,
				 GnmExprTop const *texpr0,
				 GnmExprTop const *texpr1,
				 gboolean allow_blank, gboolean use_dropdown);

void        validation_ref      (GnmValidation const *v);
void        validation_unref    (GnmValidation const *v);
void	    validation_set_expr (GnmValidation *v,
				 GnmExprTop const *texpr, unsigned indx);
GError	   *validation_is_ok    (GnmValidation const *v);
ValidationStatus validation_eval (WorkbookControl *wbc, GnmStyle const *mstyle,
				  Sheet *sheet, GnmCellPos const *pos,
				  gboolean *showed_dialog);

G_END_DECLS

#endif /* _GNM_VALIDATION_H_ */
