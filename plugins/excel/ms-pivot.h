/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * ms-chart.h: MS Excel Pivot support for Gnumeric
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 2005 Jody Goldberg
 **/

#ifndef GNUMERIC_MS_PIVOT_H
#define GNUMERIC_MS_PIVOT_H

#include <gsf/gsf.h>
#include "ms-container.h"
#include "ms-biff.h"

void excel_read_pivot_caches (ExcelWorkbook *ewb,
			      BiffQuery const *content_query,
			      GsfInfile *parent);

#endif /* GNUMERIC_MS_PIVOT_H */
