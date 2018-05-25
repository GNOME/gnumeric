/*
 * analysis-sign-test.c:
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
#include <tools/analysis-sign-test.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <func.h>
#include <numbers.h>

static gboolean
analysis_tool_sign_test_engine_run (data_analysis_output_t *dao,
				      analysis_tools_data_sign_test_t *info)
{
	guint     col;
	GSList *data = info->base.input;
	gboolean first = TRUE;

	GnmExpr const *expr;
	GnmExpr const *expr_neg;
	GnmExpr const *expr_pos;
	GnmExpr const *expr_isnumber;

	GnmFunc *fd_median;
	GnmFunc *fd_if;
	GnmFunc *fd_sum;
	GnmFunc *fd_min;
	GnmFunc *fd_binomdist;
	GnmFunc *fd_isnumber;
	GnmFunc *fd_iferror;

	fd_median = gnm_func_lookup_or_add_placeholder ("MEDIAN");
	gnm_func_inc_usage (fd_median);
	fd_if = gnm_func_lookup_or_add_placeholder ("IF");
	gnm_func_inc_usage (fd_if);
	fd_sum = gnm_func_lookup_or_add_placeholder ("SUM");
	gnm_func_inc_usage (fd_sum);
	fd_min = gnm_func_lookup_or_add_placeholder ("MIN");
	gnm_func_inc_usage (fd_min);
	fd_binomdist = gnm_func_lookup_or_add_placeholder ("BINOMDIST");
	gnm_func_inc_usage (fd_binomdist);
	fd_isnumber = gnm_func_lookup_or_add_placeholder ("ISNUMBER");
	gnm_func_inc_usage (fd_isnumber);
	fd_iferror = gnm_func_lookup_or_add_placeholder ("IFERROR");
	gnm_func_inc_usage (fd_iferror);

	dao_set_italic (dao, 0, 0, 0, 9);
	set_cell_text_col (dao, 0, 0, _("/Sign Test"
					"/Median"
					"/Predicted Median"
					"/Test Statistic"
					"/N"
					"/\xce\xb1"
					"/P(T\xe2\x89\xa4t) one-tailed"
					"/P(T\xe2\x89\xa4t) two-tailed"));

	for (col = 0; data != NULL; data = data->next, col++) {
		GnmValue *val_org = value_dup (data->data);
		GnmExpr const *expr_org;

		/* Note that analysis_tools_write_label may modify val_org */
		dao_set_italic (dao, col + 1, 0, col+1, 0);
		analysis_tools_write_label (val_org, dao, &info->base, col + 1, 0, col + 1);
		expr_org = gnm_expr_new_constant (val_org);

		if (first) {
			dao_set_cell_float (dao, col + 1, 2, info->median);
			dao_set_cell_float (dao, col + 1, 5, info->alpha);
			first = FALSE;
		} else {
			dao_set_cell_expr (dao, col + 1, 2, make_cellref (-1,0));
			dao_set_cell_expr (dao, col + 1, 5, make_cellref (-1,0));
		}

		expr_isnumber = gnm_expr_new_funcall3
			(fd_if, gnm_expr_new_funcall1
			 (fd_isnumber, gnm_expr_copy (expr_org)),
			 gnm_expr_new_constant (value_new_int (1)),
			 gnm_expr_new_constant (value_new_int (0)));

		expr = gnm_expr_new_funcall1
			(fd_median,
			 gnm_expr_copy (expr_org));
		dao_set_cell_expr (dao, col + 1, 1, expr);

		expr_neg = gnm_expr_new_funcall1
			(fd_sum,
			 gnm_expr_new_binary
			 (gnm_expr_copy (expr_isnumber), GNM_EXPR_OP_MULT,
			  gnm_expr_new_funcall2
			  (fd_iferror,
			   gnm_expr_new_funcall3
			   (fd_if, gnm_expr_new_binary (gnm_expr_copy (expr_org),
							GNM_EXPR_OP_LT, make_cellref (0,-1)),
			    gnm_expr_new_constant (value_new_int (1)),
			    gnm_expr_new_constant (value_new_int (0))),
			   gnm_expr_new_constant (value_new_int (0)))));
		expr_pos = gnm_expr_new_funcall1
			(fd_sum,
			 gnm_expr_new_binary
			 (gnm_expr_copy (expr_isnumber), GNM_EXPR_OP_MULT,
			  gnm_expr_new_funcall2
			  (fd_iferror,
			   gnm_expr_new_funcall3
			   (fd_if, gnm_expr_new_binary (gnm_expr_copy (expr_org),
							GNM_EXPR_OP_GT, make_cellref (0,-1)),
			    gnm_expr_new_constant (value_new_int (1)),
			    gnm_expr_new_constant (value_new_int (0))),
			   gnm_expr_new_constant (value_new_int (0)))));
		expr = gnm_expr_new_funcall2
			(fd_min, expr_neg, expr_pos);
		dao_set_cell_array_expr (dao, col + 1, 3, expr);

		expr = gnm_expr_new_funcall1
			(fd_sum, gnm_expr_new_binary
			 (expr_isnumber, GNM_EXPR_OP_MULT,
			  gnm_expr_new_funcall2
			  (fd_iferror, gnm_expr_new_funcall3
			   (fd_if, gnm_expr_new_binary (expr_org,
							GNM_EXPR_OP_NOT_EQUAL, make_cellref (0,-2)),
			    gnm_expr_new_constant (value_new_int (1)),
			    gnm_expr_new_constant (value_new_int (0))),
			   gnm_expr_new_constant (value_new_int (0)))));
		dao_set_cell_array_expr (dao, col + 1, 4, expr);

		expr = gnm_expr_new_funcall4 (fd_binomdist, make_cellref (0,-3), make_cellref (0,-2),
					      gnm_expr_new_constant (value_new_float (0.5)),
					      gnm_expr_new_constant (value_new_bool (TRUE)));
		dao_set_cell_array_expr (dao, col + 1, 6, expr);

		expr = gnm_expr_new_binary (gnm_expr_new_constant (value_new_int (2)),
					    GNM_EXPR_OP_MULT, make_cellref (0,-1));
		dao_set_cell_array_expr (dao, col + 1, 7, expr);
	}

	gnm_func_dec_usage (fd_median);
	gnm_func_dec_usage (fd_if);
	gnm_func_dec_usage (fd_min);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_binomdist);
	gnm_func_dec_usage (fd_isnumber);
	gnm_func_dec_usage (fd_iferror);

	dao_redraw_respan (dao);

	return FALSE;
}

static gboolean
analysis_tool_sign_test_two_engine_run (data_analysis_output_t *dao,
				      analysis_tools_data_sign_test_two_t *info)
{
	GnmValue *val_1;
	GnmValue *val_2;

	GnmExpr const *expr_1;
	GnmExpr const *expr_2;

	GnmExpr const *expr;
	GnmExpr const *expr_diff;
	GnmExpr const *expr_neg;
	GnmExpr const *expr_pos;
	GnmExpr const *expr_isnumber_1;
	GnmExpr const *expr_isnumber_2;

	GnmFunc *fd_median;
	GnmFunc *fd_if;
	GnmFunc *fd_sum;
	GnmFunc *fd_min;
	GnmFunc *fd_binomdist;
	GnmFunc *fd_isnumber;
	GnmFunc *fd_iferror;

	fd_median = gnm_func_lookup_or_add_placeholder ("MEDIAN");
	gnm_func_inc_usage (fd_median);
	fd_if = gnm_func_lookup_or_add_placeholder ("IF");
	gnm_func_inc_usage (fd_if);
	fd_sum = gnm_func_lookup_or_add_placeholder ("SUM");
	gnm_func_inc_usage (fd_sum);
	fd_min = gnm_func_lookup_or_add_placeholder ("MIN");
	gnm_func_inc_usage (fd_min);
	fd_binomdist = gnm_func_lookup_or_add_placeholder ("BINOMDIST");
	gnm_func_inc_usage (fd_binomdist);
	fd_isnumber = gnm_func_lookup_or_add_placeholder ("ISNUMBER");
	gnm_func_inc_usage (fd_isnumber);
	fd_iferror = gnm_func_lookup_or_add_placeholder ("IFERROR");
	gnm_func_inc_usage (fd_iferror);

	dao_set_italic (dao, 0, 0, 0, 9);
	set_cell_text_col (dao, 0, 0, _("/Sign Test"
					"/Median"
					"/Predicted Difference"
					"/Test Statistic"
					"/N"
					"/\xce\xb1"
					"/P(T\xe2\x89\xa4t) one-tailed"
					"/P(T\xe2\x89\xa4t) two-tailed"));

	val_1 = value_dup (info->base.range_1);
	val_2 = value_dup (info->base.range_2);

	/* Labels */
	dao_set_italic (dao, 1, 0, 2, 0);
	analysis_tools_write_label_ftest (val_1, dao, 1, 0,
					  info->base.labels, 1);
	analysis_tools_write_label_ftest (val_2, dao, 2, 0,
					  info->base.labels, 2);

	expr_1 = gnm_expr_new_constant (value_dup (val_1));
	expr_2 = gnm_expr_new_constant (value_dup (val_2));

	dao_set_cell_float (dao, 1, 2, info->median);
	dao_set_cell_float (dao, 1, 5, info->base.alpha);

	expr = gnm_expr_new_funcall1
		(fd_median,
		 gnm_expr_copy (expr_1));
	dao_set_cell_expr (dao, 1, 1, expr);

	expr = gnm_expr_new_funcall1
		(fd_median,
		 gnm_expr_copy (expr_2));
	dao_set_cell_expr (dao, 2, 1, expr);

	expr_diff = gnm_expr_new_binary (gnm_expr_copy (expr_1),
					 GNM_EXPR_OP_SUB,
					 gnm_expr_copy (expr_2));

	expr_isnumber_1 = gnm_expr_new_funcall3
		(fd_if, gnm_expr_new_funcall1
		 (fd_isnumber, expr_1),
		 gnm_expr_new_constant (value_new_int (1)),
		 gnm_expr_new_constant (value_new_int (0)));
	expr_isnumber_2 = gnm_expr_new_funcall3
		(fd_if, gnm_expr_new_funcall1
		 (fd_isnumber, expr_2),
		 gnm_expr_new_constant (value_new_int (1)),
		 gnm_expr_new_constant (value_new_int (0)));

	expr_neg = gnm_expr_new_funcall1
		(fd_sum,
		 gnm_expr_new_binary
		 (gnm_expr_copy (expr_isnumber_1), GNM_EXPR_OP_MULT,
		  gnm_expr_new_binary
		  (gnm_expr_copy (expr_isnumber_1), GNM_EXPR_OP_MULT,
		   gnm_expr_new_funcall2
		   (fd_iferror,
		    gnm_expr_new_funcall3
		    (fd_if, gnm_expr_new_binary (gnm_expr_copy (expr_diff),
						 GNM_EXPR_OP_LT, make_cellref (0,-1)),
		     gnm_expr_new_constant (value_new_int (1)),
		     gnm_expr_new_constant (value_new_int (0))),
		    gnm_expr_new_constant (value_new_int (0))))));
	expr_pos = gnm_expr_new_funcall1
		(fd_sum,
		 gnm_expr_new_binary
		 (gnm_expr_copy (expr_isnumber_1), GNM_EXPR_OP_MULT,
		  gnm_expr_new_binary
		  (gnm_expr_copy (expr_isnumber_1), GNM_EXPR_OP_MULT,
		   gnm_expr_new_funcall2
		   (fd_iferror,
		    gnm_expr_new_funcall3
		    (fd_if, gnm_expr_new_binary (gnm_expr_copy (expr_diff),
						 GNM_EXPR_OP_GT, make_cellref (0,-1)),
		     gnm_expr_new_constant (value_new_int (1)),
		     gnm_expr_new_constant (value_new_int (0))),
		    gnm_expr_new_constant (value_new_int (0))))));
	expr = gnm_expr_new_funcall2
		(fd_min, expr_neg, expr_pos);
	dao_set_cell_array_expr (dao, 1, 3, expr);

	expr = gnm_expr_new_funcall1
		(fd_sum, gnm_expr_new_binary
		 (expr_isnumber_1, GNM_EXPR_OP_MULT,
		  gnm_expr_new_binary
		  (expr_isnumber_2, GNM_EXPR_OP_MULT,
		   gnm_expr_new_funcall2
		   (fd_iferror, gnm_expr_new_funcall3
		    (fd_if, gnm_expr_new_binary (expr_diff,
						 GNM_EXPR_OP_NOT_EQUAL, make_cellref (0,-2)),
		     gnm_expr_new_constant (value_new_int (1)),
		     gnm_expr_new_constant (value_new_int (0))),
		    gnm_expr_new_constant (value_new_int (0))))));
	dao_set_cell_array_expr (dao, 1, 4, expr);

	expr = gnm_expr_new_funcall4 (fd_binomdist, make_cellref (0,-3), make_cellref (0,-2),
				      gnm_expr_new_constant (value_new_float (0.5)),
				      gnm_expr_new_constant (value_new_bool (TRUE)));
	dao_set_cell_array_expr (dao, 1, 6,
				 gnm_expr_new_funcall2
				 (fd_min,
				  gnm_expr_copy (expr),
				  gnm_expr_new_binary
				  (gnm_expr_new_constant (value_new_int (1)),
				   GNM_EXPR_OP_SUB,
				   expr)));

	expr = gnm_expr_new_binary (gnm_expr_new_constant (value_new_int (2)),
				    GNM_EXPR_OP_MULT, make_cellref (0,-1));
	dao_set_cell_array_expr (dao, 1, 7, expr);

	gnm_func_dec_usage (fd_median);
	gnm_func_dec_usage (fd_if);
	gnm_func_dec_usage (fd_min);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_binomdist);
	gnm_func_dec_usage (fd_isnumber);
	gnm_func_dec_usage (fd_iferror);

	value_release (val_1);
	value_release (val_2);

	dao_redraw_respan (dao);

	return FALSE;
}

gboolean
analysis_tool_sign_test_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
			      analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_sign_test_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor
			(dao, _("Sign Test (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		prepare_input_range (&info->base.input, info->base.group_by);
		dao_adjust (dao, 1 + g_slist_length (info->base.input), 8);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Sign Test"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Sign Test"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_sign_test_engine_run (dao, specs);
	}
	return TRUE;
}

gboolean
analysis_tool_sign_test_two_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
			      analysis_tool_engine_t selector, gpointer result)
{
	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor
			(dao, _("Sign Test (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		dao_adjust (dao, 3, 8);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_b_clean (specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Sign Test"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Sign Test"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_sign_test_two_engine_run (dao, specs);
	}
	return TRUE;  /* We shouldn't get here */
}




