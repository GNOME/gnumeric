/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * analysis-chi-squared.h:
 *
 * Author:
 *   Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2009 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
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


#ifndef ANALYSIS_CHI_SQUARED_H
#define ANALYSIS_CHI_SQUARED_H

#include "gnumeric.h"
#include "numbers.h"
#include "dao.h"
#include "tools.h"
#include "analysis-tools.h"
#include "sheet.h"

typedef struct {
	WorkbookControl *wbc;
	GnmValue        *input;
	gboolean         labels;
	gboolean         independence;
	gnm_float        alpha;
	gint             n_c;
	gint             n_r;
} analysis_tools_data_chi_squared_t;

gboolean analysis_tool_chi_squared_engine (GOCmdContext *gcc,
					   data_analysis_output_t *dao,
					   gpointer specs,
					   analysis_tool_engine_t selector,
					   gpointer result);


#endif
