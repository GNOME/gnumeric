#ifndef _GNM_VALIDATION_H_
# define _GNM_VALIDATION_H_

#include <gnumeric.h>
#include <dependent.h>

G_BEGIN_DECLS

typedef enum {
	GNM_VALIDATION_STATUS_VALID,		/* things validate */
	GNM_VALIDATION_STATUS_INVALID_DISCARD,	/* things do not validate and should be discarded */
	GNM_VALIDATION_STATUS_INVALID_EDIT		/* things do not validate and editing should continue */
} ValidationStatus;
typedef enum {
	GNM_VALIDATION_STYLE_NONE,
	GNM_VALIDATION_STYLE_STOP,
	GNM_VALIDATION_STYLE_WARNING,
	GNM_VALIDATION_STYLE_INFO,
	GNM_VALIDATION_STYLE_PARSE_ERROR
} ValidationStyle;
typedef enum {
	GNM_VALIDATION_TYPE_ANY,
	GNM_VALIDATION_TYPE_AS_INT,
	GNM_VALIDATION_TYPE_AS_NUMBER,
	GNM_VALIDATION_TYPE_IN_LIST,
	GNM_VALIDATION_TYPE_AS_DATE,
	GNM_VALIDATION_TYPE_AS_TIME,
	GNM_VALIDATION_TYPE_TEXT_LENGTH,
	GNM_VALIDATION_TYPE_CUSTOM
} ValidationType;
typedef enum {
	GNM_VALIDATION_OP_NONE = -1,
	GNM_VALIDATION_OP_BETWEEN,
	GNM_VALIDATION_OP_NOT_BETWEEN,
	GNM_VALIDATION_OP_EQUAL,
	GNM_VALIDATION_OP_NOT_EQUAL,
	GNM_VALIDATION_OP_GT,
	GNM_VALIDATION_OP_LT,
	GNM_VALIDATION_OP_GTE,
	GNM_VALIDATION_OP_LTE
} ValidationOp;

struct _GnmValidation {
	int               ref_count;

	GOString         *title;
	GOString         *msg;
	GnmDepManaged     deps[2];
	ValidationStyle   style;
	ValidationType	  type;
	ValidationOp	  op;
	gboolean	  allow_blank;
	gboolean	  use_dropdown;
};

GType gnm_validation_type_get_type (void);
#define GNM_VALIDATION_TYPE_TYPE (gnm_validation_type_get_type ())

GType gnm_validation_style_get_type (void);
#define GNM_VALIDATION_STYLE_TYPE (gnm_validation_style_get_type ())

GType gnm_validation_op_get_type (void);
#define GNM_VALIDATION_OP_TYPE (gnm_validation_op_get_type ())


GType gnm_validation_get_type (void);
GnmValidation *gnm_validation_new   (ValidationStyle style,
				     ValidationType type,
				     ValidationOp op,
				     Sheet *sheet,
				     char const *title, char const *msg,
				     GnmExprTop const *texpr0,
				     GnmExprTop const *texpr1,
				     gboolean allow_blank,
				     gboolean use_dropdown);
GnmValidation *gnm_validation_dup_to(GnmValidation *v, Sheet *sheet);
gboolean    gnm_validation_equal    (GnmValidation const *a,
				     GnmValidation const *b,
				     gboolean relax_sheet);

GnmValidation *gnm_validation_ref      (GnmValidation const *v);
void        gnm_validation_unref    (GnmValidation const *v);

void	    gnm_validation_set_expr (GnmValidation *v,
				     GnmExprTop const *texpr, unsigned indx);
GError	   *gnm_validation_is_ok    (GnmValidation const *v);

Sheet      *gnm_validation_get_sheet (GnmValidation const *v);

ValidationStatus gnm_validation_eval (WorkbookControl *wbc,
				      GnmStyle const *mstyle,
				      Sheet *sheet, GnmCellPos const *pos,
				      gboolean *showed_dialog);
ValidationStatus gnm_validation_eval_range (WorkbookControl *wbc,
					    Sheet *sheet,
					    GnmCellPos const *pos,
					    GnmRange const *r,
					    gboolean *showed_dialog);

G_END_DECLS

#endif /* _GNM_VALIDATION_H_ */
