#ifndef GNUMERIC_CHART_H
#define GNUMERIC_CHART_H

#include <glib.h>

/**
 * gnumeric-chart.h: Gnumeric Charts
 *
 * Author:
 *    Jody Goldberg (jgoldberg@home.com)
 **/

typedef struct _GnumericChartSeries
{
} GnumericChartSeries;

typedef struct _GnumericChart
{
	GPtrArray  *series;
} GnumericChart;

extern void gnumeric_chart_destroy (GnumericChart * chart);

#endif /* GNUMERIC_CHART_H */
