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

/* Reads charts */
gboolean ms_excel_chart (BiffQuery *q, MSContainer *container,
			    MsBiffVersion ver, GObject *so);

/* A wrapper which reads and checks the BOF record then calls ms_excel_chart */
gboolean ms_excel_read_chart (BiffQuery *q, MSContainer *container, GObject *so);

#endif /* GNUMERIC_MS_CHART_H */
