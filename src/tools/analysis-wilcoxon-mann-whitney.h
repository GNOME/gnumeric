/*
 * analysis-wilcoxon-mann-whitney.h:
 *
 * Author:
 *   Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2010 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */


#ifndef ANALYSIS_WILCOXON_MANN_WHITNEY_H
#define ANALYSIS_WILCOXON_MANN_WHITNEY_H

#include <gnumeric.h>
#include <numbers.h>
#include <tools/dao.h>
#include <tools/tools.h>
#include <tools/analysis-tools.h>
#include <sheet.h>

gboolean
analysis_tool_wilcoxon_mann_whitney_engine (GOCmdContext *gcc,
					    data_analysis_output_t *dao,
					    gpointer specs,
					    analysis_tool_engine_t selector,
					    gpointer result);


#endif
