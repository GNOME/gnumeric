#ifndef GNUMERIC_MS_CHART_H
#define GNUMERIC_MS_CHART_H

/**
 * ms-chart.h: MS Excel Chart support for Gnumeric
 *
 * Author:
 *    Jody Goldberg (jgoldberg@home.com)
 **/

#include <ms-excel-read.h>

extern void ms_excel_read_chart (ExcelWorkbook * wb, BiffQuery * q);

#endif /* GNUMERIC_MS_CHART_H */
