/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNUMERIC_PIVOTTABLE_H
#define GNUMERIC_PIVOTTABLE_H

#include "gnumeric.h"

typedef enum {
	GNM_PIVOT_FIELD_UNASSIGNED	= 0,
	GNM_PIVOT_FIELD_PAGE		= 1,
	GNM_PIVOT_FIELD_ROW		= 2,
	GNM_PIVOT_FIELD_COL		= 3,
	GNM_PIVOT_FIELD_DATA		= 4
} GnmPivotTableFieldType;

struct _GnmPivotTableField {
	GnmPivotTableFieldType type;
	String *name;
};

typedef struct {
	GlobalRange src, dst;

	GPtrArray *pages;
	GPtrArray *rows;
	GPtrArray *columns;
	GPtrArray *data;
	GPtrArray *unused;
} GnmPivotTable;

GnmPivotTable  *gnm_pivottable_new    (Sheet *src_sheet, Range const *src,
				       Sheet *dst_sheet, Range const *dst);
void		gnm_pivottable_free   (GnmPivotTable *filter);
void		gnm_pivottable_link   (GnmPivotTable *filter);
void		gnm_pivottable_unlink (GnmPivotTable *filter);

#endif /* GNUMERIC_PIVOTTABLE_H */
