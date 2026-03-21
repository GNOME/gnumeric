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
#include <sheet.h>


G_DEFINE_TYPE (GnmAutoExpressionTool, gnm_auto_expression_tool, GNM_TYPE_GENERIC_ANALYSIS_TOOL)

enum {
	AUTO_EXPRESSION_PROP_0,
	AUTO_EXPRESSION_PROP_MULTIPLE,
	AUTO_EXPRESSION_PROP_BELOW,
	AUTO_EXPRESSION_PROP_FUNC
};

static void
gnm_auto_expression_tool_set_property (GObject *object, guint property_id,
				       GValue const *value, GParamSpec *pspec)
{
	GnmAutoExpressionTool *tool = GNM_AUTO_EXPRESSION_TOOL (object);

	switch (property_id) {
	case AUTO_EXPRESSION_PROP_MULTIPLE:
		tool->multiple = g_value_get_boolean (value);
		break;
	case AUTO_EXPRESSION_PROP_BELOW:
		tool->below = g_value_get_boolean (value);
		break;
	case AUTO_EXPRESSION_PROP_FUNC: {
		const char *name = g_value_get_string (value);
		if (tool->func) {
			gnm_func_dec_usage (tool->func);
			g_object_unref (tool->func);
		}
		tool->func = name
			? g_object_ref (gnm_func_get_and_use (name))
			: NULL;
		break;
	}
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_auto_expression_tool_get_property (GObject *object, guint property_id,
				       GValue *value, GParamSpec *pspec)
{
	GnmAutoExpressionTool *tool = GNM_AUTO_EXPRESSION_TOOL (object);

	switch (property_id) {
	case AUTO_EXPRESSION_PROP_MULTIPLE:
		g_value_set_boolean (value, tool->multiple);
		break;
	case AUTO_EXPRESSION_PROP_BELOW:
		g_value_set_boolean (value, tool->below);
		break;
	case AUTO_EXPRESSION_PROP_FUNC:
		g_value_set_string (value,
				    tool->func
				    ? gnm_func_get_name (tool->func, FALSE)
				    : NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_auto_expression_tool_init (G_GNUC_UNUSED GnmAutoExpressionTool *tool)
{
	tool->multiple = FALSE;
	tool->below = FALSE;
	tool->func = NULL;
}

static void
gnm_auto_expression_tool_finalize (GObject *obj)
{
	GnmAutoExpressionTool *tool = GNM_AUTO_EXPRESSION_TOOL (obj);
	if (tool->func) {
		gnm_func_dec_usage (tool->func);
		g_object_unref (tool->func);
		tool->func = NULL;
	}
	G_OBJECT_CLASS (gnm_auto_expression_tool_parent_class)->finalize (obj);
}

static gboolean
gnm_auto_expression_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmAutoExpressionTool *atool = GNM_AUTO_EXPRESSION_TOOL (tool);
	GnmGenericAnalysisTool *gtool = &atool->parent;

	if (!atool->func)
		return TRUE;

	analysis_tool_prepare_input_range (gtool);
	if (!analysis_tool_check_input_homogeneity (gtool)) {
		gtool->base.err = gtool->base.group_by + 1;
		return TRUE;
	}
	dao_adjust (dao, 1 + g_slist_length (gtool->base.input),
		    1 + g_slist_length (gtool->base.input));
	return FALSE;
}

static char *
gnm_auto_expression_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Auto Expression (%s)"));
}

static gboolean
gnm_auto_expression_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	dao_prepare_output (wbc, dao, _("Auto Expression"));
	return FALSE;
}

static gboolean
gnm_auto_expression_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	return dao_format_output (wbc, dao, _("Auto Expression"));
}

static gboolean
gnm_auto_expression_tool_perform_calc (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	GnmAutoExpressionTool *atool = GNM_AUTO_EXPRESSION_TOOL (tool);
	GnmGenericAnalysisTool *gtool = &atool->parent;
	guint     col;
	GSList *data = gtool->base.input;

	// FIXME: below/side is just a variant of "group-by" and don't
	// really work for a new sheet.
	if (atool->below) {
		for (col = 0; data != NULL; data = data->next, col++)
			dao_set_cell_expr
				(dao, col, 0,
				 gnm_expr_new_funcall1
				 (atool->func,
				  gnm_expr_new_constant (value_dup (data->data))));

		if (atool->multiple)
			dao_set_cell_expr
				(dao, col, 0,
				 gnm_expr_new_funcall1
				 (atool->func,
				  make_rangeref (- col, 0, -1, 0)));
	} else {
		for (col = 0; data != NULL; data = data->next, col++)
			dao_set_cell_expr
				(dao, 0, col,
				 gnm_expr_new_funcall1
				 (atool->func,
				  gnm_expr_new_constant (value_dup (data->data))));
		if (atool->multiple)
			dao_set_cell_expr
				(dao, 0, col,
				 gnm_expr_new_funcall1
				 (atool->func,
				  make_rangeref (0, - col, 0, -1)));
	}
	dao_redraw_respan (dao);

	return FALSE;
}

static void
gnm_auto_expression_tool_class_init (GnmAutoExpressionToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->set_property = gnm_auto_expression_tool_set_property;
	gobject_class->get_property = gnm_auto_expression_tool_get_property;

	gobject_class->finalize = gnm_auto_expression_tool_finalize;
	at_class->update_dao = gnm_auto_expression_tool_update_dao;
	at_class->update_descriptor = gnm_auto_expression_tool_update_descriptor;
	at_class->prepare_output_range = gnm_auto_expression_tool_prepare_output_range;
	at_class->format_output_range = gnm_auto_expression_tool_format_output_range;
	at_class->perform_calc = gnm_auto_expression_tool_perform_calc;

	g_object_class_install_property (gobject_class,
		AUTO_EXPRESSION_PROP_MULTIPLE,
		g_param_spec_boolean ("multiple", NULL, NULL,
			FALSE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		AUTO_EXPRESSION_PROP_BELOW,
		g_param_spec_boolean ("below", NULL, NULL,
			FALSE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		AUTO_EXPRESSION_PROP_FUNC,
		g_param_spec_string ("function", NULL, NULL, NULL,
				     GSF_PARAM_STATIC | G_PARAM_READWRITE));
}

GnmAnalysisTool *
gnm_auto_expression_tool_new (void)
{
	return g_object_new (GNM_TYPE_AUTO_EXPRESSION_TOOL, NULL);
}
