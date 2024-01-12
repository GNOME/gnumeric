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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef GNUMERIC_DAO_H
#define GNUMERIC_DAO_H

#include <gnumeric.h>
#include <numbers.h>
#include <style.h>
#include <goffice/goffice.h>
#include <style-color.h>
#include <style-border.h>
#include <graph.h>

typedef enum {
        NewSheetOutput, NewWorkbookOutput, RangeOutput, InPlaceOutput
} data_analysis_output_type_t;

typedef struct {
        data_analysis_output_type_t type;
        Sheet                       *sheet;
        int                         start_col, cols;
        int                         start_row, rows;
	int                         offset_col, offset_row;
	gboolean                    autofit_flag;
	gboolean                    autofit_noshrink;
	gboolean                    clear_outputrange;
	gboolean                    retain_format;
	gboolean                    retain_comments;
	gboolean                    put_formulas;
	WorkbookControl             *wbc;
	GSList                      *sos;
	gboolean                    omit_so;
	gboolean                    use_gfree;
} data_analysis_output_t;

data_analysis_output_t *dao_init (data_analysis_output_t *dao,
				  data_analysis_output_type_t type);
data_analysis_output_t *dao_init_new_sheet (data_analysis_output_t *dao);
data_analysis_output_t *dao_load_from_value (data_analysis_output_t *dao,
					     GnmValue *output_range);
void dao_free (data_analysis_output_t *dao);

void dao_autofit_columns      (data_analysis_output_t *dao);
void dao_autofit_these_columns (data_analysis_output_t *dao, int from_col, int to_col);

void dao_autofit_rows      (data_analysis_output_t *dao);
void dao_autofit_these_rows (data_analysis_output_t *dao, int from_row, int to_row);

gboolean dao_cell_is_visible      (data_analysis_output_t *dao, int col, int row);
void dao_set_bold             (data_analysis_output_t *dao, int col1, int row1,
			       int col2, int row2);
void dao_set_italic           (data_analysis_output_t *dao, int col1, int row1,
			       int col2, int row2);
void dao_set_percent          (data_analysis_output_t *dao, int col1, int row1,
			       int col2, int row2);
void dao_set_date             (data_analysis_output_t *dao, int col1, int row1,
			       int col2, int row2);
void dao_set_format             (data_analysis_output_t *dao, int col1, int row1,
				 int col2, int row2, char const *format);
void dao_set_merge          (data_analysis_output_t *dao, int col1, int row1,
			       int col2, int row2);

void dao_set_colors (data_analysis_output_t *dao, int col1, int row1,
		     int col2, int row2,
		     GnmColor *fore, GnmColor *back);
void dao_set_align (data_analysis_output_t *dao, int col1, int row1,
		    int col2, int row2,
		    GnmHAlign align_h, GnmVAlign align_v);
void dao_set_border (data_analysis_output_t *dao, int col1,
		     int row1, int col2, int row2,
		     GnmStyleElement elem,GnmStyleBorderType border,
		     GnmColor *color,
		     GnmStyleBorderOrientation orientation);
void dao_set_cell             (data_analysis_output_t *dao, int col, int row,
			       char const *text);
void dao_set_cell_printf      (data_analysis_output_t *dao,
			       int col, int row, char const *fmt, ...)
                           G_GNUC_PRINTF (4, 5);
void dao_set_cell_value       (data_analysis_output_t *dao, int col, int row,
			       GnmValue *v);
void dao_set_cell_expr       (data_analysis_output_t *dao, int col, int row,
			       GnmExpr const *expr);
void dao_set_cell_array_expr (data_analysis_output_t *dao, int col, int row,
			      GnmExpr const *expr);
void dao_set_array_expr (data_analysis_output_t *dao,
			 int col, int row,  int cols, int rows,
			 GnmExpr const *expr);
void dao_set_cell_float       (data_analysis_output_t *dao,
			       int col, int row, gnm_float v);
void dao_set_cell_int         (data_analysis_output_t *dao,
			       int col, int row, int v);
void dao_set_cell_na          (data_analysis_output_t *dao,
			       int col, int row);
void dao_set_cell_float_na    (data_analysis_output_t *dao, int col, int row,
			       gnm_float v,
			   gboolean is_valid);
void dao_set_cell_comment (data_analysis_output_t *dao, int col, int row,
			   char const *comment);
void dao_set_sheet_object (data_analysis_output_t *dao, int col, int row, SheetObject* so);

void dao_prepare_output       (WorkbookControl *wbc,
			       data_analysis_output_t *dao, char const *name);
gboolean dao_format_output    (data_analysis_output_t *dao, char const *cmd);
char *dao_command_descriptor (data_analysis_output_t *dao, char const *format,
			      gpointer result);
void dao_adjust           (data_analysis_output_t *dao, gint cols, gint rows);

ColRowStateList *dao_get_colrow_state_list (data_analysis_output_t *dao,
					    gboolean is_cols);
void dao_set_colrow_state_list (data_analysis_output_t *dao, gboolean is_cols,
				ColRowStateList *list);
void dao_write_header (data_analysis_output_t *dao, const gchar *toolname,
		       const gchar *title, Sheet *sheet);
char *dao_find_name (Sheet *sheet, int col, int row);
void dao_append_date (GString *buf);

gboolean dao_put_formulas (data_analysis_output_t *dao);

void dao_redraw_respan (data_analysis_output_t *dao);

GnmExpr const *dao_get_cellref  (data_analysis_output_t *dao, int x, int y);
GnmExpr const *dao_get_rangeref (data_analysis_output_t *dao, int ax, int ay,  int bx, int by);

GOData *dao_go_data_vector (data_analysis_output_t *dao, int ax, int ay,  int bx, int by);


/* The following 2 functions are intended solely for undo support */
/* of sheet objects normally created by the dao. */

GSList *dao_surrender_so (data_analysis_output_t *dao);
void    dao_set_omit_so (data_analysis_output_t *dao, gboolean omit);

#endif
