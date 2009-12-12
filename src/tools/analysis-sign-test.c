/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "analysis-sign-test.h"
#include "analysis-tools.h"
#include "value.h"
#include "ranges.h"
#include "expr.h"
#include "func.h"
#include "numbers.h"

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

	GnmFunc *fd_median;
	GnmFunc *fd_if;
	GnmFunc *fd_sum;
	GnmFunc *fd_min;
	GnmFunc *fd_binomdist;

	fd_median = gnm_func_lookup_or_add_placeholder ("MEDIAN", dao->sheet ? dao->sheet->workbook : NULL, FALSE);
	gnm_func_ref (fd_median);
	fd_if = gnm_func_lookup_or_add_placeholder ("IF", dao->sheet ? dao->sheet->workbook : NULL, FALSE);
	gnm_func_ref (fd_if);
	fd_sum = gnm_func_lookup_or_add_placeholder ("SUM", dao->sheet ? dao->sheet->workbook : NULL, FALSE);
	gnm_func_ref (fd_sum);
	fd_min = gnm_func_lookup_or_add_placeholder ("MIN", dao->sheet ? dao->sheet->workbook : NULL, FALSE);
	gnm_func_ref (fd_min);
	fd_binomdist = gnm_func_lookup_or_add_placeholder ("BINOMDIST", dao->sheet ? dao->sheet->workbook : NULL, FALSE);
	gnm_func_ref (fd_binomdist);

	dao_set_italic (dao, 0, 0, 0, 9);
	set_cell_text_col (dao, 0, 0, _("/Sign Test"
					"/Median:"
					"/Predicted Median:"
					"/Test Statistic:"
					"/N:"
					"/\xce\xb1:"
					"/P(T\xe2\x89\xa4t) one-tailed:"
					"/P(T\xe2\x89\xa4t) two-tailed:"));

	for (col = 0; data != NULL; data = data->next, col++) {
		GnmValue *val_org = value_dup (data->data);

		/* Note that analysis_tools_write_label may modify val_org */
		dao_set_italic (dao, col + 1, 0, col+1, 0);
		analysis_tools_write_label (val_org, dao, &info->base, col + 1, 0, col + 1);

		if (first) {
			dao_set_cell_float (dao, col + 1, 2, info->median);
			dao_set_cell_float (dao, col + 1, 5, info->alpha);
			first = FALSE;
		} else {
			dao_set_cell_expr (dao, col + 1, 2, make_cellref (-1,0));
			dao_set_cell_expr (dao, col + 1, 5, make_cellref (-1,0));
		}

		expr = gnm_expr_new_funcall1
			(fd_median,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 1, expr);

		expr_neg = gnm_expr_new_funcall1
			(fd_sum,
			 gnm_expr_new_funcall3
			 (fd_if, gnm_expr_new_binary (gnm_expr_new_constant (value_dup (val_org)), 
						      GNM_EXPR_OP_LT, make_cellref (0,-1)), 
			  gnm_expr_new_constant (value_new_int (1)), 
			  gnm_expr_new_constant (value_new_int (0))));
		expr_pos = gnm_expr_new_funcall1
			(fd_sum,
			 gnm_expr_new_funcall3
			 (fd_if, gnm_expr_new_binary (gnm_expr_new_constant (value_dup (val_org)), 
						      GNM_EXPR_OP_GT, make_cellref (0,-1)), 
			  gnm_expr_new_constant (value_new_int (1)), 
			  gnm_expr_new_constant (value_new_int (0))));
		expr = gnm_expr_new_funcall2
			(fd_min, expr_neg, expr_pos);
		dao_set_cell_array_expr (dao, col + 1, 3, expr);
		
		expr_neg = gnm_expr_new_funcall1
			(fd_sum,
			 gnm_expr_new_funcall3
			 (fd_if, gnm_expr_new_binary (gnm_expr_new_constant (value_dup (val_org)), 
						      GNM_EXPR_OP_LT, make_cellref (0,-2)), 
			  gnm_expr_new_constant (value_new_int (1)), 
			  gnm_expr_new_constant (value_new_int (0))));
		expr_pos = gnm_expr_new_funcall1
			(fd_sum,
			 gnm_expr_new_funcall3
			 (fd_if, gnm_expr_new_binary (gnm_expr_new_constant (val_org), 
						      GNM_EXPR_OP_GT, make_cellref (0,-2)), 
			  gnm_expr_new_constant (value_new_int (1)), 
			  gnm_expr_new_constant (value_new_int (0))));
		expr = gnm_expr_new_funcall2
			(fd_sum, expr_neg, expr_pos);
		dao_set_cell_array_expr (dao, col + 1, 4, expr);

		expr = gnm_expr_new_funcall4 (fd_binomdist, make_cellref (0,-3), make_cellref (0,-2), 
					      gnm_expr_new_constant (value_new_float (0.5)), 
					      gnm_expr_new_constant (value_new_bool (TRUE)));
		dao_set_cell_array_expr (dao, col + 1, 6, expr);

		expr = gnm_expr_new_binary (gnm_expr_new_constant (value_new_int (2)), 
					    GNM_EXPR_OP_MULT, make_cellref (0,-1));
		dao_set_cell_array_expr (dao, col + 1, 7, expr);
		
	}

	gnm_func_unref (fd_median);
	gnm_func_unref (fd_if);
	gnm_func_unref (fd_min);
	gnm_func_unref (fd_sum);
	gnm_func_unref (fd_binomdist);

	dao_redraw_respan (dao);

	return FALSE;
}


gboolean
analysis_tool_sign_test_engine (data_analysis_output_t *dao, gpointer specs,
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




