/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dao.h:
 *
 * Authors:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *   Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 * (C) Copyright 2001, 2002 by Andreas J. Guelzow  <aguelzow@taliesin.ca>
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

#ifndef GNUMERIC_DAO_H
#define GNUMERIC_DAO_H

#include "gnumeric.h"
#include "numbers.h"
#include <widgets/gnumeric-expr-entry.h>
#include <glade/glade.h>

typedef enum {
        NewSheetOutput, NewWorkbookOutput, RangeOutput, InPlaceOutput
} data_analysis_output_type_t;

typedef struct {
        data_analysis_output_type_t type;
        Sheet                       *sheet;
        int                         start_col, cols;
        int                         start_row, rows;
	int                         offset_col, offset_row;
	gboolean                    autofit_flag : 1;
	gboolean                    clear_outputrange : 1;
	gboolean                    retain_format : 1;
	gboolean                    retain_comments : 1;
	WorkbookControl             *wbc;
} data_analysis_output_t;

data_analysis_output_t *dao_init (data_analysis_output_t *dao, 
				  data_analysis_output_type_t type);

void dao_autofit_columns      (data_analysis_output_t *dao);
void dao_autofit_these_columns (data_analysis_output_t *dao, int from_col, int to_col);
void dao_set_bold             (data_analysis_output_t *dao, int col1, int row1,
			       int col2, int row2);
void dao_set_italic           (data_analysis_output_t *dao, int col1, int row1,
			       int col2, int row2);
void dao_set_underlined       (data_analysis_output_t *dao, int col1, int row1,
			       int col2, int row2);
void dao_set_percent          (data_analysis_output_t *dao, int col1, int row1,
			       int col2, int row2);

void dao_set_cell             (data_analysis_output_t *dao, int col, int row, char const *text);
void dao_set_cell_printf      (data_analysis_output_t *dao,
			       int col, int row, char const *fmt, ...)
                           G_GNUC_PRINTF (4, 5);
void dao_set_cell_value       (data_analysis_output_t *dao, int col, int row, Value *v);
void dao_set_cell_float       (data_analysis_output_t *dao,
			       int col, int row, gnum_float v);
void dao_set_cell_int         (data_analysis_output_t *dao,
			       int col, int row, int v);
void dao_set_cell_na          (data_analysis_output_t *dao,
			       int col, int row);
void dao_set_cell_float_na    (data_analysis_output_t *dao, int col, int row, gnum_float v, 
			   gboolean is_valid);
void dao_set_cell_comment (data_analysis_output_t *dao, int col, int row,
			   const char *comment);

void dao_prepare_output       (WorkbookControl *wbc,  
			       data_analysis_output_t *dao, char const *name);
gboolean dao_format_output    (data_analysis_output_t *dao, char const *cmd);
char *dao_range_name      (data_analysis_output_t *dao);
char *dao_command_descriptor (data_analysis_output_t *dao, char const *format, gpointer result);
void dao_adjust           (data_analysis_output_t *dao, gint cols, gint rows);

ColRowStateList *dao_get_colrow_state_list (data_analysis_output_t *dao, gboolean is_cols);
void dao_set_colrow_state_list (data_analysis_output_t *dao, gboolean is_cols, 
				ColRowStateList *list);
void dao_write_header (data_analysis_output_t *dao, gchar *toolname,
		       gchar *title, Sheet *sheet);
char *dao_find_name (Sheet *sheet, int col, int row);
void dao_append_date (GString *buf);

#endif
