/*
 * analysis-anova.c:
 *
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <tools/analysis-anova.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <func.h>
#include <sheet.h>
#include <numbers.h>
#include <mstyle.h>
#include <style-border.h>
#include <style-color.h>


static gboolean
analysis_tool_anova_two_factor_prepare_input_range (
                       analysis_tools_data_anova_two_factor_t *info)
{
	info->rows = info->input->v_range.cell.b.row - info->input->v_range.cell.a.row +
		(info->labels ? 0 : 1);
	info->n_r = info->rows/info->replication;
	info->n_c = info->input->v_range.cell.b.col - info->input->v_range.cell.a.col +
		(info->labels ? 0 : 1);

	/* Check that correct number of rows per sample */
	if (info->rows % info->replication != 0) {
		info->err = analysis_tools_replication_invalid;
		return TRUE;
	}

	/* Check that at least two columns of data are given */
	if (info->n_c < 2) {
			info->err = analysis_tools_too_few_cols;
			return TRUE;
	}
	/* Check that at least two data rows of data are given */
	if (info->n_r < 2) {
		info->err = analysis_tools_too_few_rows;
			return TRUE;
	}

	return FALSE;
}

/************* ANOVA: Two-Factor Without Replication Tool ****************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

static gboolean
analysis_tool_anova_two_factor_no_rep_engine_run (data_analysis_output_t *dao,
						  analysis_tools_data_anova_two_factor_t *info)
{
	int        i, r;
	GnmExpr const *expr_check;
	GnmExpr const *expr_region;

	GnmFunc *fd_index;
	GnmFunc *fd_offset;
	GnmFunc *fd_count;
	GnmFunc *fd_sum;
	GnmFunc *fd_sumsq;
	GnmFunc *fd_average;
	GnmFunc *fd_var;
	GnmFunc *fd_if;
	GnmFunc *fd_fdist;
	GnmFunc *fd_finv;

	fd_index = gnm_func_lookup_or_add_placeholder ("INDEX");
	gnm_func_inc_usage (fd_index);
	fd_offset = gnm_func_lookup_or_add_placeholder ("OFFSET");
	gnm_func_inc_usage (fd_offset);
	fd_count = gnm_func_lookup_or_add_placeholder ("COUNT");
	gnm_func_inc_usage (fd_count);
	fd_sum = gnm_func_lookup_or_add_placeholder ("SUM");
	gnm_func_inc_usage (fd_sum);
	fd_sumsq = gnm_func_lookup_or_add_placeholder ("SUMSQ");
	gnm_func_inc_usage (fd_sumsq);
	fd_average = gnm_func_lookup_or_add_placeholder ("AVERAGE");
	gnm_func_inc_usage (fd_average);
	fd_var = gnm_func_lookup_or_add_placeholder ("VAR");
	gnm_func_inc_usage (fd_var);
	fd_if = gnm_func_lookup_or_add_placeholder ("IF");
	gnm_func_inc_usage (fd_if);
	fd_fdist = gnm_func_lookup_or_add_placeholder ("FDIST");
	gnm_func_inc_usage (fd_fdist);
	fd_finv = gnm_func_lookup_or_add_placeholder ("FINV");
	gnm_func_inc_usage (fd_finv);

	dao_set_merge (dao, 0, 0, 4, 0);
	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell (dao, 0, 0, _("ANOVA: Two-Factor Without Replication"));
	dao_set_italic (dao, 0, 2, 4, 2);
	set_cell_text_row (dao, 0, 2, _("/Summary"
					"/Count"
					"/Sum"
					"/Average"
					"/Variance"));
	r = 3;

	for (i = 1; i <= info->n_r; i++, r++) {
		GnmExpr const *expr_source;
		dao_set_italic (dao, 0, r, 0, r);
		if (info->labels) {
			GnmExpr const *expr_label;
			expr_label = gnm_expr_new_funcall3
				(fd_index,
				 gnm_expr_new_constant (value_dup (info->input)),
				 gnm_expr_new_constant (value_new_int (i+1)),
				 gnm_expr_new_constant (value_new_int (1)));
			dao_set_cell_expr (dao, 0, r, expr_label);
			expr_source =  gnm_expr_new_funcall5
				(fd_offset,
				 gnm_expr_new_constant (value_dup (info->input)),
				 gnm_expr_new_constant (value_new_int (i)),
				 gnm_expr_new_constant (value_new_int (1)),
				 gnm_expr_new_constant (value_new_int (1)),
				 gnm_expr_new_constant (value_new_int (info->n_c)));
		} else {
			dao_set_cell_printf (dao, 0, r, _("Row %i"), i);
			expr_source =  gnm_expr_new_funcall4
				(fd_offset,
				 gnm_expr_new_constant (value_dup (info->input)),
				 gnm_expr_new_constant (value_new_int (i-1)),
				 gnm_expr_new_constant (value_new_int (0)),
				 gnm_expr_new_constant (value_new_int (1)));
		}
		dao_set_cell_expr (dao, 1, r, gnm_expr_new_funcall1
				   (fd_count, gnm_expr_copy (expr_source)));
		dao_set_cell_expr (dao, 2, r, gnm_expr_new_funcall1
				   (fd_sum, gnm_expr_copy (expr_source)));
		dao_set_cell_expr (dao, 3, r, gnm_expr_new_funcall1
				   (fd_average, gnm_expr_copy (expr_source)));
		dao_set_cell_expr (dao, 4, r, gnm_expr_new_funcall1
				   (fd_var, expr_source));
	}

	r++;

	for (i = 1; i <= info->n_c; i++, r++) {
		GnmExpr const *expr_source;
		dao_set_italic (dao, 0, r, 0, r);
		if (info->labels) {
			GnmExpr const *expr_label;
			expr_label = gnm_expr_new_funcall3
				(fd_index,
				 gnm_expr_new_constant (value_dup (info->input)),
				 gnm_expr_new_constant (value_new_int (1)),
				 gnm_expr_new_constant (value_new_int (i+1)));
			dao_set_cell_expr (dao, 0, r, expr_label);
			expr_source =  gnm_expr_new_funcall5
				(fd_offset,
				 gnm_expr_new_constant (value_dup (info->input)),
				 gnm_expr_new_constant (value_new_int (1)),
				 gnm_expr_new_constant (value_new_int (i)),
				 gnm_expr_new_constant (value_new_int (info->n_r)),
				 gnm_expr_new_constant (value_new_int (1)));
		} else {
			dao_set_cell_printf (dao, 0, r, _("Column %i"), i);
			expr_source =  gnm_expr_new_funcall5
				(fd_offset,
				 gnm_expr_new_constant (value_dup (info->input)),
				 gnm_expr_new_constant (value_new_int (0)),
				 gnm_expr_new_constant (value_new_int (i-1)),
				 gnm_expr_new_constant (value_new_int (info->n_r)),
				 gnm_expr_new_constant (value_new_int (1)));
		}
		dao_set_cell_expr (dao, 1, r, gnm_expr_new_funcall1
				   (fd_count, gnm_expr_copy (expr_source)));
		dao_set_cell_expr (dao, 2, r, gnm_expr_new_funcall1
				   (fd_sum, gnm_expr_copy (expr_source)));
		dao_set_cell_expr (dao, 3, r, gnm_expr_new_funcall1
				   (fd_average, gnm_expr_copy (expr_source)));
		dao_set_cell_expr (dao, 4, r, gnm_expr_new_funcall1
				   (fd_var, expr_source));
	}

	r += 2;

	dao_set_merge (dao, 0, r, 6, r);
	dao_set_italic (dao, 0, r, 6, r);

	if (info->labels)
		expr_region = gnm_expr_new_funcall5
			(fd_offset,
			 gnm_expr_new_constant (value_dup (info->input)),
			 gnm_expr_new_constant (value_new_int (1)),
			 gnm_expr_new_constant (value_new_int (1)),
			 gnm_expr_new_constant (value_new_int (info->n_r)),
			 gnm_expr_new_constant (value_new_int (info->n_c)));
	else
		expr_region = gnm_expr_new_constant (value_dup (info->input));

	expr_check = gnm_expr_new_funcall3
		(fd_if,
		 gnm_expr_new_binary
		 (gnm_expr_new_funcall1
		  (fd_count, gnm_expr_copy (expr_region)),
		  GNM_EXPR_OP_EQUAL,
		  gnm_expr_new_constant (value_new_int (info->n_r*info->n_c))),
		 gnm_expr_new_constant (value_new_int (1)),
		 gnm_expr_new_constant (value_new_int (-1)));
	dao_set_cell_expr (dao, 0, r, expr_check);
	dao_set_format (dao, 0, r, 0, r,
			_("\"ANOVA\";[Red]\"Invalid ANOVA: Missing Observations\""));
	dao_set_align (dao, 0, r, 0, r, GNM_HALIGN_LEFT, GNM_VALIGN_BOTTOM);

	r++;
	dao_set_italic (dao, 0, r, 0, r + 4);
	set_cell_text_col (dao, 0, r, _("/Source of Variation"
					"/Rows"
					"/Columns"
					"/Error"
					"/Total"));
	dao_set_italic (dao, 1, r, 6, r);
	dao_set_border (dao, 0, r, 6, r, MSTYLE_BORDER_BOTTOM, GNM_STYLE_BORDER_THIN,
			style_color_black (), GNM_STYLE_BORDER_HORIZONTAL);
	dao_set_border (dao, 0, r+3, 6, r+3, MSTYLE_BORDER_BOTTOM, GNM_STYLE_BORDER_THIN,
			style_color_black (), GNM_STYLE_BORDER_HORIZONTAL);
	set_cell_text_row (dao, 1, r, _("/SS"
					"/df"
					"/MS"
					"/F"
					"/P-value"
					"/F critical"));

	dao->offset_col += 1;
	dao->offset_row += r + 1;

	if (dao_cell_is_visible (dao, 5, 2)) {
		char *cc;
		GnmExprList *args;

		GnmExpr const *expr_ms;
		GnmExpr const *expr_total;
		GnmExpr const *expr_a;
		GnmExpr const *expr_b;
		GnmExpr const *expr_t;
		GnmExpr const *expr_cf;

		expr_t = gnm_expr_new_funcall1 (fd_sumsq, gnm_expr_copy (expr_region));
		expr_cf = gnm_expr_new_binary
			(gnm_expr_new_binary
			 (gnm_expr_new_funcall1 (fd_sum, gnm_expr_copy (expr_region)),
			  GNM_EXPR_OP_EXP,
			  gnm_expr_new_constant (value_new_int (2))),
			 GNM_EXPR_OP_DIV,
			 gnm_expr_new_funcall1 (fd_count, gnm_expr_copy (expr_region)));

		args = NULL;
		for (i = 1; i <= info->n_r; i++) {
			GnmExpr const *expr;
			expr = gnm_expr_new_funcall1
				(fd_sum,
				 gnm_expr_new_funcall5
				 (fd_offset,
				  gnm_expr_new_constant (value_dup (info->input)),
				  gnm_expr_new_constant (value_new_int
							 ((info->labels)?i:(i-1))),
				  gnm_expr_new_constant (value_new_int
							 ((info->labels)?1:0)),
				  gnm_expr_new_constant (value_new_int (1)),
				  gnm_expr_new_constant (value_new_int (info->n_c))));
			args = gnm_expr_list_prepend (args, expr);
		}
		expr_a =  gnm_expr_new_binary
			(gnm_expr_new_funcall (fd_sumsq, args), GNM_EXPR_OP_DIV,
			 gnm_expr_new_constant (value_new_int (info->n_c)));

		args = NULL;
		for (i = 1; i <= info->n_c; i++) {
			GnmExpr const *expr;
			expr = gnm_expr_new_funcall1
				(fd_sum,
				 gnm_expr_new_funcall5
				 (fd_offset,
				  gnm_expr_new_constant (value_dup (info->input)),
				  gnm_expr_new_constant (value_new_int
							 ((info->labels)?1:0)),
				  gnm_expr_new_constant (value_new_int
							 ((info->labels)?i:(i-1))),
				  gnm_expr_new_constant (value_new_int (info->n_r)),
				  gnm_expr_new_constant (value_new_int (1))));
			args = gnm_expr_list_prepend (args, expr);
		}
		expr_b =  gnm_expr_new_binary
			(gnm_expr_new_funcall (fd_sumsq, args), GNM_EXPR_OP_DIV,
			 gnm_expr_new_constant (value_new_int (info->n_r)));

		dao_set_cell_expr (dao, 0, 0, gnm_expr_new_binary
				   (gnm_expr_copy (expr_a), GNM_EXPR_OP_SUB,
				    gnm_expr_copy (expr_cf)));
		dao_set_cell_expr (dao, 0, 1, gnm_expr_new_binary
				   (gnm_expr_copy (expr_b), GNM_EXPR_OP_SUB,
				    gnm_expr_copy (expr_cf)));
		dao_set_cell_expr (dao, 0, 2, gnm_expr_new_binary
				   (gnm_expr_new_binary
				   (expr_t, GNM_EXPR_OP_ADD, expr_cf),
				   GNM_EXPR_OP_SUB ,
				    gnm_expr_new_binary
				   (expr_a, GNM_EXPR_OP_ADD, expr_b)));
		expr_total = gnm_expr_new_funcall1
			(fd_sum,  make_rangeref (0, -3, 0, -1));
		dao_set_cell_expr (dao, 0, 3, gnm_expr_copy (expr_total));
		dao_set_cell_int (dao, 1, 0, info->n_r - 1);
		dao_set_cell_int (dao, 1, 1, info->n_c - 1);
		dao_set_cell_expr (dao, 1, 2, gnm_expr_new_binary
				   (make_cellref (0,-1), GNM_EXPR_OP_MULT,
				    make_cellref (0,-2)));
		dao_set_cell_expr (dao, 1, 3, expr_total);

		expr_ms = gnm_expr_new_binary (make_cellref (-2,0), GNM_EXPR_OP_DIV,
					       make_cellref (-1,0));
		dao_set_cell_expr (dao, 2, 0, gnm_expr_copy (expr_ms));
		dao_set_cell_expr (dao, 2, 1, gnm_expr_copy (expr_ms));
		dao_set_cell_expr (dao, 2, 2, expr_ms);

		dao_set_cell_expr (dao, 3, 0,  gnm_expr_new_binary
				   (make_cellref (-1,0), GNM_EXPR_OP_DIV,
				    make_cellref (-1,2)));
		dao_set_cell_expr (dao, 3, 1,  gnm_expr_new_binary
				   (make_cellref (-1,0), GNM_EXPR_OP_DIV,
				    make_cellref (-1,1)));
		dao_set_cell_expr
			(dao, 4, 0,
			 gnm_expr_new_funcall3
			 (fd_fdist,
			  make_cellref (-1, 0),
			  make_cellref (-3, 0),
			  make_cellref (-3, 2)));
		dao_set_cell_expr
			(dao, 4, 1,
			 gnm_expr_new_funcall3
			 (fd_fdist,
			  make_cellref (-1, 0),
			  make_cellref (-3, 0),
			  make_cellref (-3, 1)));
		dao_set_cell_expr
			(dao, 5, 0,
			 gnm_expr_new_funcall3
			 (fd_finv,
			  gnm_expr_new_constant (value_new_float (info->alpha)),
			  make_cellref (-4, 0),
			  make_cellref (-4, 2)));
		dao_set_cell_expr
			(dao, 5, 1,
			 gnm_expr_new_funcall3
			 (fd_finv,
			  gnm_expr_new_constant (value_new_float (info->alpha)),
			  make_cellref (-4, 0),
			  make_cellref (-4, 1)));
		cc = g_strdup_printf ("%s = %.2" GNM_FORMAT_f, "\xce\xb1", info->alpha);
		dao_set_cell_comment (dao, 5, 0, cc);
		dao_set_cell_comment (dao, 5, 1, cc);
		g_free (cc);
	} else
		dao_set_cell (dao, 0, 0, _("Insufficient space available for ANOVA table."));

	gnm_func_dec_usage (fd_index);
	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_offset);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_sumsq);
	gnm_func_dec_usage (fd_average);
	gnm_func_dec_usage (fd_var);
	gnm_func_dec_usage (fd_if);
	gnm_func_dec_usage (fd_finv);
	gnm_func_dec_usage (fd_fdist);

	gnm_expr_free (expr_region);

	dao_redraw_respan (dao);

	return FALSE;
}


/************* ANOVA: Two-Factor With Replication Tool *******************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/


static gboolean
analysis_tool_anova_two_factor_engine_run (data_analysis_output_t *dao,
					   analysis_tools_data_anova_two_factor_t *info)
{

	int        i, k, r;
	GnmExpr const *expr_check;
	GnmExpr const *expr_source;
	GnmExpr const *expr_total_count;

	GnmFunc *fd_index;
	GnmFunc *fd_offset;
	GnmFunc *fd_count;
	GnmFunc *fd_sum;
	GnmFunc *fd_sumsq;
	GnmFunc *fd_average;
	GnmFunc *fd_var;
	GnmFunc *fd_if;
	GnmFunc *fd_fdist;
	GnmFunc *fd_finv;

	fd_index = gnm_func_lookup_or_add_placeholder ("INDEX");
	gnm_func_inc_usage (fd_index);
	fd_offset = gnm_func_lookup_or_add_placeholder ("OFFSET");
	gnm_func_inc_usage (fd_offset);
	fd_count = gnm_func_lookup_or_add_placeholder ("COUNT");
	gnm_func_inc_usage (fd_count);
	fd_sum = gnm_func_lookup_or_add_placeholder ("SUM");
	gnm_func_inc_usage (fd_sum);
	fd_sumsq = gnm_func_lookup_or_add_placeholder ("SUMSQ");
	gnm_func_inc_usage (fd_sumsq);
	fd_average = gnm_func_lookup_or_add_placeholder ("AVERAGE");
	gnm_func_inc_usage (fd_average);
	fd_var = gnm_func_lookup_or_add_placeholder ("VAR");
	gnm_func_inc_usage (fd_var);
	fd_if = gnm_func_lookup_or_add_placeholder ("IF");
	gnm_func_inc_usage (fd_if);
	fd_fdist = gnm_func_lookup_or_add_placeholder ("FDIST");
	gnm_func_inc_usage (fd_fdist);
	fd_finv = gnm_func_lookup_or_add_placeholder ("FINV");
	gnm_func_inc_usage (fd_finv);

	dao_set_merge (dao, 0, 0, 4, 0);
	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell (dao, 0, 0, _("ANOVA: Two-Factor Fixed Effects With Replication"));
	dao_set_italic (dao, 0, 2, info->n_c + 1, 2);
	dao_set_cell (dao, 0, 2, _("Summary"));

	for (k = 1; k <= info->n_c; k++) {
		if (info->labels) {
			GnmExpr const *expr_label;
			expr_label = gnm_expr_new_funcall3
				(fd_index,
				 gnm_expr_new_constant (value_dup (info->input)),
				 gnm_expr_new_constant (value_new_int (1)),
				 gnm_expr_new_constant (value_new_int (k+1)));
			dao_set_cell_expr (dao, k, 2, expr_label);
		} else
		/*xgettext: this is a label for the first, second,... level of factor B in an ANOVA*/
			dao_set_cell_printf (dao, k, 2, _("B, Level %i"), k);
	}
	dao_set_cell (dao, info->n_c + 1, 2, _("Subtotal"));

	r = 3;
	for (i = 1; i <= info->n_r; i++, r += 6) {
		int level_start =  (i-1)*info->replication + ((info->labels) ? 1 : 0);

		dao_set_italic (dao, 0, r, 0, r+4);
		if (info->labels) {
			GnmExpr const *expr_label;
			expr_label = gnm_expr_new_funcall3
				(fd_index,
				 gnm_expr_new_constant (value_dup (info->input)),
				 gnm_expr_new_constant (value_new_int (level_start + 1)),
				 gnm_expr_new_constant (value_new_int (1)));
			dao_set_cell_expr (dao, 0, r, expr_label);
		} else
		/*xgettext: this is a label for the first, second,... level of factor A in an ANOVA*/
			dao_set_cell_printf (dao, 0, r, _("A, Level %i"), i);
		set_cell_text_col (dao, 0, r + 1, _("/Count"
						    "/Sum"
						    "/Average"
						    "/Variance"));
		for (k = 1; k <= info->n_c; k++) {
			expr_source =  gnm_expr_new_funcall5
				(fd_offset,
				 gnm_expr_new_constant (value_dup (info->input)),
				 gnm_expr_new_constant (value_new_int (level_start)),
				 gnm_expr_new_constant (value_new_int ((info->labels) ? k : (k - 1))),
				 gnm_expr_new_constant (value_new_int (info->replication)),
				 gnm_expr_new_constant (value_new_int (1)));
			dao_set_cell_expr (dao, k, r + 1, gnm_expr_new_funcall1
					   (fd_count, gnm_expr_copy (expr_source)));
			dao_set_cell_expr (dao, k, r + 2, gnm_expr_new_funcall1
					   (fd_sum, gnm_expr_copy (expr_source)));
			dao_set_cell_expr (dao, k, r + 3, gnm_expr_new_funcall1
					   (fd_average, gnm_expr_copy (expr_source)));
			dao_set_cell_expr (dao, k, r + 4, gnm_expr_new_funcall1
					   (fd_var, expr_source));
		}

		expr_source =  gnm_expr_new_funcall5
			(fd_offset,
			 gnm_expr_new_constant (value_dup (info->input)),
			 gnm_expr_new_constant (value_new_int (level_start)),
			 gnm_expr_new_constant (value_new_int ((info->labels) ? 1 : 0)),
			 gnm_expr_new_constant (value_new_int (info->replication)),
			 gnm_expr_new_constant (value_new_int (info->n_c)));
		dao_set_cell_expr (dao, k, r + 1, gnm_expr_new_funcall1
				   (fd_count, gnm_expr_copy (expr_source)));
		dao_set_cell_expr (dao, k, r + 2, gnm_expr_new_funcall1
				   (fd_sum, gnm_expr_copy (expr_source)));
		dao_set_cell_expr (dao, k, r + 3, gnm_expr_new_funcall1
				   (fd_average, gnm_expr_copy (expr_source)));
		dao_set_cell_expr (dao, k, r + 4, gnm_expr_new_funcall1
				   (fd_var, expr_source));
	}

	dao_set_italic (dao, 0, r, 0, r+4);
	dao_set_cell (dao, 0, r, _("Subtotal"));
	set_cell_text_col (dao, 0, r + 1, _("/Count"
					    "/Sum"
					    "/Average"
					    "/Variance"));

	for (k = 1; k <= info->n_c; k++) {
		expr_source =  gnm_expr_new_funcall5
			(fd_offset,
			 gnm_expr_new_constant (value_dup (info->input)),
			 gnm_expr_new_constant (value_new_int ((info->labels) ? 1 : 0)),
			 gnm_expr_new_constant (value_new_int ((info->labels) ? k : (k - 1))),
			 gnm_expr_new_constant (value_new_int (info->replication * info->n_r)),
			 gnm_expr_new_constant (value_new_int (1)));
		dao_set_cell_expr (dao, k, r + 1, gnm_expr_new_funcall1
				   (fd_count, gnm_expr_copy (expr_source)));
		dao_set_cell_expr (dao, k, r + 2, gnm_expr_new_funcall1
				   (fd_sum, gnm_expr_copy (expr_source)));
		dao_set_cell_expr (dao, k, r + 3, gnm_expr_new_funcall1
				   (fd_average, gnm_expr_copy (expr_source)));
		dao_set_cell_expr (dao, k, r + 4, gnm_expr_new_funcall1
				   (fd_var, expr_source));
	}

	dao_set_italic (dao, info->n_c + 1, r, info->n_c + 1, r);
	dao_set_cell (dao, info->n_c + 1, r, _("Total"));

	expr_source =  gnm_expr_new_funcall5
		(fd_offset,
		 gnm_expr_new_constant (value_dup (info->input)),
		 gnm_expr_new_constant (value_new_int ((info->labels) ? 1 : 0)),
		 gnm_expr_new_constant (value_new_int ((info->labels) ? 1 : 0)),
		 gnm_expr_new_constant (value_new_int (info->replication * info->n_r)),
		 gnm_expr_new_constant (value_new_int (info->n_c)));
	expr_total_count = gnm_expr_new_funcall1 (fd_count, gnm_expr_copy (expr_source));
	dao_set_cell_expr (dao, info->n_c + 1, r + 1,  gnm_expr_copy (expr_total_count));
	dao_set_cell_expr (dao, info->n_c + 1, r + 2, gnm_expr_new_funcall1
			   (fd_sum, gnm_expr_copy (expr_source)));
	dao_set_cell_expr (dao, info->n_c + 1, r + 3, gnm_expr_new_funcall1
			   (fd_average, gnm_expr_copy (expr_source)));
	dao_set_cell_expr (dao, info->n_c + 1, r + 4, gnm_expr_new_funcall1
			   (fd_var, gnm_expr_copy (expr_source)));

	r += 7;

	dao_set_merge (dao, 0, r, 6, r);
	dao_set_italic (dao, 0, r, 6, r);

	expr_check = gnm_expr_new_funcall3
		(fd_if,
		 gnm_expr_new_binary
		 (gnm_expr_copy (expr_total_count),
		  GNM_EXPR_OP_EQUAL,
		  gnm_expr_new_constant (value_new_int (info->n_r*info->n_c*info->replication))),
		 gnm_expr_new_constant (value_new_int (1)),
		 gnm_expr_new_constant (value_new_int (-1)));
	dao_set_cell_expr (dao, 0, r, expr_check);
	dao_set_format (dao, 0, r, 0, r,
			_("\"ANOVA\";[Red]\"Invalid ANOVA: Missing Observations\""));
	dao_set_align (dao, 0, r, 0, r, GNM_HALIGN_LEFT, GNM_VALIGN_BOTTOM);

	r++;
	dao_set_italic (dao, 0, r, 0, r + 5);
	set_cell_text_col (dao, 0, r, _("/Source of Variation"
					"/Factor A"
					"/Factor B"
					"/Interaction"
					"/Error"
					"/Total"));
	dao_set_italic (dao, 1, r, 6, r);
	dao_set_border (dao, 0, r, 6, r, MSTYLE_BORDER_BOTTOM, GNM_STYLE_BORDER_THIN,
			style_color_black (), GNM_STYLE_BORDER_HORIZONTAL);
	dao_set_border (dao, 0, r+4, 6, r+4, MSTYLE_BORDER_BOTTOM, GNM_STYLE_BORDER_THIN,
			style_color_black (), GNM_STYLE_BORDER_HORIZONTAL);
	set_cell_text_row (dao, 1, r, _("/SS"
					"/df"
					"/MS"
					"/F"
					"/P-value"
					"/F critical"));

	dao->offset_col += 1;
	dao->offset_row += r + 1;

	if (dao_cell_is_visible (dao, 5, 2)) {
		char *cc;
		GnmExprList *args;

		GnmExpr const *expr_ms;
		GnmExpr const *expr_total;
		GnmExpr const *expr_a;
		GnmExpr const *expr_b;
		GnmExpr const *expr_t;
		GnmExpr const *expr_s;
		GnmExpr const *expr_cf;

		expr_t = gnm_expr_new_funcall1 (fd_sumsq, gnm_expr_copy (expr_source));
		expr_cf = gnm_expr_new_binary
			(gnm_expr_new_binary
			 (gnm_expr_new_funcall1 (fd_sum, gnm_expr_copy (expr_source)),
			  GNM_EXPR_OP_EXP,
			  gnm_expr_new_constant (value_new_int (2))),
			 GNM_EXPR_OP_DIV,
			 gnm_expr_copy (expr_total_count));

		args = NULL;
		for (i = 1; i <= info->n_r; i++) {
			GnmExpr const *expr;
			int level_start =  (i-1)*info->replication + 1;

			expr = gnm_expr_new_funcall1
				(fd_sum,
				 gnm_expr_new_funcall5
				 (fd_offset,
				  gnm_expr_new_constant (value_dup (info->input)),
				  gnm_expr_new_constant (value_new_int
							 ((info->labels)?level_start:(level_start-1))),
				  gnm_expr_new_constant (value_new_int
							 ((info->labels)?1:0)),
				  gnm_expr_new_constant (value_new_int (info->replication)),
				  gnm_expr_new_constant (value_new_int (info->n_c))));
			args = gnm_expr_list_prepend (args, expr);
		}
		expr_a =  gnm_expr_new_binary
			(gnm_expr_new_funcall (fd_sumsq, args), GNM_EXPR_OP_DIV,
			 gnm_expr_new_constant (value_new_int (info->n_c * info->replication)));

		args = NULL;
		for (k = 1; k <= info->n_c; k++) {
			GnmExpr const *expr;
			expr = gnm_expr_new_funcall1
				(fd_sum,
				 gnm_expr_new_funcall5
				 (fd_offset,
				  gnm_expr_new_constant (value_dup (info->input)),
				  gnm_expr_new_constant (value_new_int
							 ((info->labels)?1:0)),
				  gnm_expr_new_constant (value_new_int
							 ((info->labels)?k:(k-1))),
				  gnm_expr_new_constant (value_new_int (info->n_r * info->replication)),
				  gnm_expr_new_constant (value_new_int (1))));
			args = gnm_expr_list_prepend (args, expr);
		}
		expr_b =  gnm_expr_new_binary
			(gnm_expr_new_funcall (fd_sumsq, args), GNM_EXPR_OP_DIV,
			 gnm_expr_new_constant (value_new_int (info->n_r * info->replication)));

		args = NULL;
		for (i = 1; i <= info->n_r; i++) {
			int level_start =  (i-1)*info->replication + 1;
			for (k = 1; k <= info->n_c; k++) {
				GnmExpr const *expr;
				expr = gnm_expr_new_funcall1
					(fd_sum,
					 gnm_expr_new_funcall5
					 (fd_offset,
					  gnm_expr_new_constant (value_dup (info->input)),
					  gnm_expr_new_constant
					  (value_new_int ((info->labels)?level_start:level_start-1)),
					  gnm_expr_new_constant (value_new_int
								 ((info->labels)?k:(k-1))),
					  gnm_expr_new_constant (value_new_int (info->replication)),
					  gnm_expr_new_constant (value_new_int (1))));
				args = gnm_expr_list_prepend (args, expr);
			}
		}
		expr_s =  gnm_expr_new_binary
			(gnm_expr_new_funcall (fd_sumsq, args), GNM_EXPR_OP_DIV,
			 gnm_expr_new_constant (value_new_int (info->replication)));

		dao_set_cell_expr (dao, 0, 0, gnm_expr_new_binary
				   (gnm_expr_copy (expr_a), GNM_EXPR_OP_SUB,
				    gnm_expr_copy (expr_cf)));
		dao_set_cell_expr (dao, 0, 1, gnm_expr_new_binary
				   (gnm_expr_copy (expr_b), GNM_EXPR_OP_SUB,
				    gnm_expr_copy (expr_cf)));
		dao_set_cell_expr (dao, 0, 2, gnm_expr_new_binary
				   (gnm_expr_new_binary
				    (gnm_expr_copy (expr_s), GNM_EXPR_OP_ADD, expr_cf),
				   GNM_EXPR_OP_SUB ,
				    gnm_expr_new_binary
				   (expr_a, GNM_EXPR_OP_ADD, expr_b)));
		dao_set_cell_expr (dao, 0, 3, gnm_expr_new_binary (expr_t, GNM_EXPR_OP_SUB, expr_s));
		expr_total = gnm_expr_new_funcall1
			(fd_sum,  make_rangeref (0, -4, 0, -1));
		dao_set_cell_expr (dao, 0, 4, gnm_expr_copy (expr_total));
		dao_set_cell_int (dao, 1, 0, info->n_r - 1);
		dao_set_cell_int (dao, 1, 1, info->n_c - 1);
		dao_set_cell_expr (dao, 1, 2, gnm_expr_new_binary
				   (make_cellref (0,-1), GNM_EXPR_OP_MULT,
				    make_cellref (0,-2)));
		dao_set_cell_int (dao, 1, 3, info->n_c*info->n_r*(info->replication - 1));
		dao_set_cell_expr (dao, 1, 4, expr_total);

		expr_ms = gnm_expr_new_binary (make_cellref (-2,0), GNM_EXPR_OP_DIV,
					       make_cellref (-1,0));
		dao_set_cell_expr (dao, 2, 0, gnm_expr_copy (expr_ms));
		dao_set_cell_expr (dao, 2, 1, gnm_expr_copy (expr_ms));
		dao_set_cell_expr (dao, 2, 2, gnm_expr_copy (expr_ms));
		dao_set_cell_expr (dao, 2, 3, expr_ms);

		dao_set_cell_expr (dao, 3, 0,  gnm_expr_new_binary
				   (make_cellref (-1,0), GNM_EXPR_OP_DIV,
				    make_cellref (-1,3)));
		dao_set_cell_expr (dao, 3, 1,  gnm_expr_new_binary
				   (make_cellref (-1,0), GNM_EXPR_OP_DIV,
				    make_cellref (-1,2)));
		dao_set_cell_expr (dao, 3, 2,  gnm_expr_new_binary
				   (make_cellref (-1,0), GNM_EXPR_OP_DIV,
				    make_cellref (-1,1)));
		dao_set_cell_expr
			(dao, 4, 0,
			 gnm_expr_new_funcall3
			 (fd_fdist,
			  make_cellref (-1, 0),
			  make_cellref (-3, 0),
			  make_cellref (-3, 3)));
		dao_set_cell_expr
			(dao, 4, 1,
			 gnm_expr_new_funcall3
			 (fd_fdist,
			  make_cellref (-1, 0),
			  make_cellref (-3, 0),
			  make_cellref (-3, 2)));
		dao_set_cell_expr
			(dao, 4, 2,
			 gnm_expr_new_funcall3
			 (fd_fdist,
			  make_cellref (-1, 0),
			  make_cellref (-3, 0),
			  make_cellref (-3, 1)));
		dao_set_cell_expr
			(dao, 5, 0,
			 gnm_expr_new_funcall3
			 (fd_finv,
			  gnm_expr_new_constant (value_new_float (info->alpha)),
			  make_cellref (-4, 0),
			  make_cellref (-4, 3)));
		dao_set_cell_expr
			(dao, 5, 1,
			 gnm_expr_new_funcall3
			 (fd_finv,
			  gnm_expr_new_constant (value_new_float (info->alpha)),
			  make_cellref (-4, 0),
			  make_cellref (-4, 2)));
		dao_set_cell_expr
			(dao, 5, 2,
			 gnm_expr_new_funcall3
			 (fd_finv,
			  gnm_expr_new_constant (value_new_float (info->alpha)),
			  make_cellref (-4, 0),
			  make_cellref (-4, 1)));
		cc = g_strdup_printf ("%s = %.2" GNM_FORMAT_f, "\xce\xb1", info->alpha);
		dao_set_cell_comment (dao, 5, 0, cc);
		dao_set_cell_comment (dao, 5, 1, cc);
		dao_set_cell_comment (dao, 5, 2, cc);
		g_free (cc);
	} else
		dao_set_cell (dao, 0, 0, _("Insufficient space available for ANOVA table."));

	gnm_func_dec_usage (fd_index);
	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_offset);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_sumsq);
	gnm_func_dec_usage (fd_average);
	gnm_func_dec_usage (fd_var);
	gnm_func_dec_usage (fd_if);
	gnm_func_dec_usage (fd_finv);
	gnm_func_dec_usage (fd_fdist);

	gnm_expr_free (expr_source);
	gnm_expr_free (expr_total_count);

	dao_redraw_respan (dao);

	return FALSE;
}

static gboolean
analysis_tool_anova_two_factor_engine_clean (G_GNUC_UNUSED data_analysis_output_t *dao,
					     gpointer specs)
{
	analysis_tools_data_anova_two_factor_t *info = specs;

	value_release (info->input);
	info->input = NULL;

	return FALSE;
}

gboolean
analysis_tool_anova_two_factor_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
				   analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_anova_two_factor_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (
				dao, (info->replication == 1) ?
				_("Two Factor ANOVA (%s), no replication") :
				_("Two Factor ANOVA (%s),  with replication") , result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		if (analysis_tool_anova_two_factor_prepare_input_range (info))
			return TRUE;
		if (info->replication == 1)
			dao_adjust (dao, 7, info->n_c + info->n_r + 12);
		else
			dao_adjust (dao, MAX (2 + info->n_c, 7), info->n_r * 6 + 18);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_anova_two_factor_engine_clean (dao, specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("ANOVA"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Two Factor ANOVA"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		if (info->replication == 1)
			return analysis_tool_anova_two_factor_no_rep_engine_run (dao, info);
		else
			return analysis_tool_anova_two_factor_engine_run (dao, info);
	}
	return TRUE;  /* We shouldn't get here */
}

