/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * analysis-frequency.h:
 *
 * Author:
 *   Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2008 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#ifndef ANALYSIS_FREQUENCY_H
#define ANALYSIS_FREQUENCY_H

#include "gnumeric.h"
#include "numbers.h"
#include "dao.h"
#include "tools.h"
#include "analysis-tools.h"

typedef enum {
	NO_CHART = 0,
	BAR_CHART,
	COLUMN_CHART
} chart_freq_t;


typedef struct {
	analysis_tools_data_generic_t base;
	gboolean   predetermined;
	GnmValue   *bin;
	gnm_float max;
	gnm_float min;
	gint       n;
	gboolean   percentage;
	gboolean   exact;
	chart_freq_t   chart;
} analysis_tools_data_frequency_t;

gboolean analysis_tool_frequency_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
					   analysis_tool_engine_t selector, gpointer result);

#endif
