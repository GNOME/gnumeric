/**
 * gnumeric-chart.c: Gnumeric chart object
 *
 * I'll lurk it down here until it firms up a bit.
 *
 * Author:
 *    Jody Goldberg (jgoldberg@home.com)
 *
 * (C) 1999, 2000 Jody Goldberg
 **/

#include <config.h>
#include "gnumeric-chart.h"

GnumericChart *
gnumeric_chart_new (void)
{
	GnumericChart * res = (GnumericChart *) g_new (GnumericChart, 1);
	res->series = NULL;
	return res;
}

void
gnumeric_chart_destroy (GnumericChart * chart)
{
	if (chart->series)
		g_ptr_array_free (chart->series, FALSE);
	chart->series = (GPtrArray *)0xdeadbeef; /* poison the pointer */
	g_free (chart);
}

