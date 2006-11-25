/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNM_VALIDATION_COMBO_H
#define GNM_VALIDATION_COMBO_H

#include "sheet-object-impl.h"

typedef struct {
	SheetObject parent;

	GnmValidation const *validation;
	SheetView	*sv;
} GnmValidationCombo;

#define GNM_VALIDATION_COMBO_TYPE     (gnm_validation_combo_get_type ())
#define GNM_VALIDATION_COMBO(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_VALIDATION_COMBO_TYPE, GnmValidationCombo))

GType gnm_validation_combo_get_type (void);
SheetObject *gnm_validation_combo_new    (GnmValidation const *v, SheetView *sv);

#endif /* GNM_VALIDATION_COMBO_H */
