/**
 * ms-chart.h: MS Excel Chart support for Gnumeric
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1999-2005 Jody Goldberg
 **/

#ifndef GNM_MS_CHART_H
#define GNM_MS_CHART_H

#include "excel.h"
#include "ms-excel-read.h"
#include "ms-container.h"

gboolean ms_excel_chart_read     (BiffQuery *q, MSContainer *container,
				  SheetObject *sog, Sheet *full_page);
gboolean ms_excel_chart_read_BOF (BiffQuery *q, MSContainer *container,
				  SheetObject *sog);

void ms_excel_chart_write	   (ExcelWriteState *ewb, SheetObject *so);
void ms_excel_chart_extract_styles (ExcelWriteState *ewb, SheetObject *so);

#endif /* GNM_MS_CHART_H */
