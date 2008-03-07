/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_PIVOTTABLE_H_
# define _GNM_PIVOTTABLE_H_

#include "gnumeric.h"

G_BEGIN_DECLS

typedef enum {
	GNM_PIVOT_FIELD_UNASSIGNED	= 0,
	GNM_PIVOT_FIELD_PAGE		= 1,
	GNM_PIVOT_FIELD_ROW		= 2,
	GNM_PIVOT_FIELD_COL		= 3,
	GNM_PIVOT_FIELD_DATA		= 4
} GnmPivotTableFieldType;

struct _GnmPivotTableField {
	GnmPivotTableFieldType type;
	GnmString *name;
};

typedef struct {
	GnmSheetRange src, dst;

	GPtrArray *pages;
	GPtrArray *rows;
	GPtrArray *columns;
	GPtrArray *data;
	GPtrArray *unused;
} GnmPivotTable;

GnmPivotTable  *gnm_pivottable_new    (Sheet *src_sheet, GnmRange const *src,
				       Sheet *dst_sheet, GnmRange const *dst);
void		gnm_pivottable_free   (GnmPivotTable *filter);
#if 0
void		gnm_pivottable_link   (GnmPivotTable *filter);
void		gnm_pivottable_unlink (GnmPivotTable *filter);
#endif

G_END_DECLS

#endif /* _GNM_PIVOTTABLE_H_ */
