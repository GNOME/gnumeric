#ifndef GNUMERIC_MS_CHART_H
#define GNUMERIC_MS_CHART_H

/**
 * ms-chart.h: MS Excel Chart support for Gnumeric
 *
 * Author:
 *    Jody Goldberg (jgoldberg@home.com)
 *
 * (C) 1999, 2000 Jody Goldberg
 **/

#include "ms-excel-read.h"
#include "ms-container.h"

/* Reads charts */
extern void ms_excel_chart (BiffQuery *q, MSContainer *container,
			    MsBiffBofData *bof);

/* A wrapper which reads and checks the BOF record then calls ms_excel_chart */
extern void ms_excel_read_chart (BiffQuery *q, MSContainer *container);

extern void ms_excel_biff_dimensions (BiffQuery *q, ExcelWorkbook *wb);

#endif /* GNUMERIC_MS_CHART_H */
