/*
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
#ifndef GNUMERIC_TOOLS_H
#define GNUMERIC_TOOLS_H

#include "gnumeric.h"
#include "numbers.h"
#include "dao.h"

typedef enum {
	TOOL_ENGINE_UPDATE_DAO = 0,
	TOOL_ENGINE_UPDATE_DESCRIPTOR,
	TOOL_ENGINE_PREPARE_OUTPUT_RANGE,
	TOOL_ENGINE_LAST_VALIDITY_CHECK,
	TOOL_ENGINE_FORMAT_OUTPUT_RANGE,
	TOOL_ENGINE_PERFORM_CALC,
	TOOL_ENGINE_CLEAN_UP
} analysis_tool_engine_t;

typedef gboolean (* analysis_tool_engine) (data_analysis_output_t *dao, gpointer specs, 
					   analysis_tool_engine_t selector, gpointer result);


typedef enum {
	GROUPED_BY_ROW = 0,
	GROUPED_BY_COL = 1,
	GROUPED_BY_AREA = 2,
	GROUPED_BY_BIN = 3
} group_by_t;


#endif
