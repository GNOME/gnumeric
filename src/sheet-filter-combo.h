/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNM_SHEET_FILTER_COMBO_H
#define GNM_SHEET_FILTER_COMBO_H

#include "sheet-object-impl.h"

typedef struct {
	SheetObject parent;

	GnmFilterCondition *cond;
	GnmFilter   	   *filter;
} GnmFilterCombo;

#define GNM_FILTER_COMBO_TYPE     (gnm_filter_combo_get_type ())
#define GNM_FILTER_COMBO(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_FILTER_COMBO_TYPE, GnmFilterCombo))

GType gnm_filter_combo_get_type (void);

#endif /* GNM_SHEET_FILTER_COMBO_H */
