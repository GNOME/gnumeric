/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_SHEET_FILTER_COMBO_H_
# define _GNM_SHEET_FILTER_COMBO_H_

#include "sheet-object-impl.h"

G_BEGIN_DECLS

typedef struct {
	SheetObject parent;

	GnmFilterCondition *cond;
	GnmFilter   	   *filter;
} GnmFilterCombo;

#define GNM_FILTER_COMBO_TYPE     (gnm_filter_combo_get_type ())
#define GNM_FILTER_COMBO(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_FILTER_COMBO_TYPE, GnmFilterCombo))
#define IS_GNM_FILTER_COMBO(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_FILTER_COMBO_TYPE))

GType gnm_filter_combo_get_type (void);
void  gnm_filter_combo_apply    (GnmFilterCombo *fcombo, Sheet *target_sheet);

G_END_DECLS

#endif /* _GNM_SHEET_FILTER_COMBO_H_ */
