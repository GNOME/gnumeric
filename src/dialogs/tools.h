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
#include "tools/dao.h"

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



typedef enum {
	TOOL_CORRELATION = 1,
	TOOL_COVARIANCE = 2, 
	TOOL_RANK_PERCENTILE = 3,
	TOOL_HISTOGRAM = 5,  
	TOOL_FOURIER = 6,   
	TOOL_GENERIC = 10,  
	TOOL_DESC_STATS = 11,
	TOOL_TTEST = 12,
	TOOL_SAMPLING = 13,
	TOOL_AVERAGE = 14,
	TOOL_REGRESSION = 15,
	TOOL_ANOVA_SINGLE = 16,
	TOOL_ANOVA_TWO_FACTOR = 17,
	TOOL_FTEST = 18,
	TOOL_RANDOM = 19,
	TOOL_EXP_SMOOTHING = 20,
	TOOL_ADVANCED_FILTER = 21
} ToolType;

#define GENERIC_TOOL_STATE     ToolType  const type;\
	GladeXML  *gui;\
	GtkWidget *dialog;\
	GnumericExprEntry *input_entry;\
	GnumericExprEntry *input_entry_2;\
	GnumericExprEntry *output_entry;\
        GtkWidget *clear_outputrange_button;\
        GtkWidget *retain_format_button;\
        GtkWidget *retain_comments_button;\
	GtkWidget *ok_button;\
	GtkWidget *cancel_button;\
	GtkWidget *apply_button;\
	GtkWidget *help_button;\
	const char *help_link;\
	char *input_var1_str;\
	char *input_var2_str;\
	GtkWidget *new_sheet;\
	GtkWidget *new_workbook;\
	GtkWidget *output_range;\
	Sheet	  *sheet;\
	Workbook  *wb;\
	WorkbookControlGUI  *wbcg;\
	GtkAccelGroup *accel;\
	GtkWidget *warning_dialog;\
	GtkWidget *warning;

typedef struct {
	GENERIC_TOOL_STATE
} GenericToolState;

void tool_load_selection (GenericToolState *state, gboolean allow_multiple);
gboolean tool_destroy (GtkObject *w, GenericToolState  *state);
void dialog_tool_init_buttons (GenericToolState *state, GCallback ok_function);
void error_in_entry (GenericToolState *state, GtkWidget *entry, const char *err_str);



#endif
