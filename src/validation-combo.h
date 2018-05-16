#ifndef _GNM_VALIDATION_COMBO_H_
# define _GNM_VALIDATION_COMBO_H_

#include <gnm-cell-combo.h>

G_BEGIN_DECLS

typedef struct {
	GnmCellCombo parent;

	GnmValidation const *validation;
} GnmValidationCombo;

#define GNM_VALIDATION_COMBO_TYPE     (gnm_validation_combo_get_type ())
#define GNM_IS_VALIDATION_COMBO(obj)  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GNM_VALIDATION_COMBO_TYPE))
#define GNM_VALIDATION_COMBO(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_VALIDATION_COMBO_TYPE, GnmValidationCombo))

GType gnm_validation_combo_get_type (void);
SheetObject *gnm_validation_combo_new    (GnmValidation const *v, SheetView *sv);

G_END_DECLS

#endif /* _GNM_VALIDATION_COMBO_H_ */
