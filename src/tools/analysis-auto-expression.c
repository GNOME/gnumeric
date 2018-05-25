/*
 * analysis-auto-expression.c:
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
#include <tools/analysis-auto-expression.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <func.h>
#include <numbers.h>

static gboolean
analysis_tool_auto_expression_engine_run (data_analysis_output_t *dao,
				      analysis_tools_data_auto_expression_t *info)
{
	guint     col;
	GSList *data = info->base.input;

	if (info->below) {
		for (col = 0; data != NULL; data = data->next, col++)
			dao_set_cell_expr
				(dao, col, 0,
				 gnm_expr_new_funcall1
				 (info->func,
				  gnm_expr_new_constant (value_dup (data->data))));

		if (info->multiple)
			dao_set_cell_expr
				(dao, col, 0,
				 gnm_expr_new_funcall1
				 (info->func,
				  make_rangeref (- col, 0, -1, 0)));
	} else {
		for (col = 0; data != NULL; data = data->next, col++)
			dao_set_cell_expr
				(dao, 0, col,
				 gnm_expr_new_funcall1
				 (info->func,
				  gnm_expr_new_constant (value_dup (data->data))));

		if (info->multiple)
			dao_set_cell_expr
				(dao, 0, col,
				 gnm_expr_new_funcall1
				 (info->func,
				  make_rangeref (0, - col, 0, -1)));
	}
	dao_redraw_respan (dao);

	return FALSE;
}

static gboolean
analysis_tool_auto_expression_engine_clean (gpointer specs)
{
	analysis_tools_data_auto_expression_t *info = specs;

	gnm_func_dec_usage (info->func);
	info->func = NULL;

	return analysis_tool_generic_clean (specs);
}

gboolean
analysis_tool_auto_expression_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
			      analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_auto_expression_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor
			(dao, _("Auto Expression (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		prepare_input_range (&info->base.input, info->base.group_by);
		if (info->below)
			dao_adjust (dao,
				    (info->multiple ? 1 : 0)  + g_slist_length (info->base.input),
				    1);
		else
			dao_adjust (dao, 1,
				    (info->multiple ? 1 : 0)  + g_slist_length (info->base.input));
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_auto_expression_engine_clean (specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Auto Expression"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Auto Expression"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_auto_expression_engine_run (dao, specs);
	}
	return TRUE;
}

