#ifndef GNUMERIC_MS_CHART_H
#define GNUMERIC_MS_CHART_H

/**
 * ms-chart.h: MS Excel Chart support for Gnumeric
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1999-2001 Jody Goldberg
 **/

#include "ms-excel-read.h"
#include "ms-container.h"

gboolean ms_excel_read_chart     (BiffQuery *q, MSContainer *container,
				  MsBiffVersion ver, SheetObject *sog);
gboolean ms_excel_read_chart_BOF (BiffQuery *q, MSContainer *container,
				  SheetObject *sog);

void	 ms_excel_write_chart	(ExcelWriteState *ewb, SheetObject *so);

#endif /* GNUMERIC_MS_CHART_H */
