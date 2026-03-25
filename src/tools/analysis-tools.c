/*
 * analysis-tools.c:
 *
 * Authors:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *   Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 * (C) Copyright 2002, 2004 by Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * Modified 2001 to use range_* functions of mathfunc.h
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
#include <tools/analysis-tools.h>

#include <mathfunc.h>
#include <func.h>
#include <expr.h>
#include <position.h>
#include <value.h>
#include <cell.h>
#include <sheet.h>
#include <ranges.h>
#include <parse-util.h>
#include <style.h>
#include <regression.h>
#include <sheet-style.h>
#include <workbook.h>
#include <collect.h>
#include <gnm-format.h>
#include <sheet-object-cell-comment.h>
#include <workbook-control.h>
#include <command-context.h>
#include <sheet-object-graph.h>
#include <graph.h>
#include <goffice/goffice.h>

#include <string.h>
#include <stdlib.h>
#include <math.h>

GType
gnm_tool_group_by_get_type (void)
{
	static GType etype = 0;
	if (etype == 0) {
		static GEnumValue const values[] = {
			{ GNM_TOOL_GROUPED_BY_ROW,
			  "GNM_TOOL_GROUPED_BY_ROW",
			  "row"
			},
			{ GNM_TOOL_GROUPED_BY_COL,
			  "GNM_TOOL_GROUPED_BY_COL",
			  "col"
			},
			{ GNM_TOOL_GROUPED_BY_AREA,
			  "GNM_TOOL_GROUPED_BY_AREA",
			  "area"
			},
			{ GNM_TOOL_GROUPED_BY_BIN,
			  "GNM_TOOL_GROUPED_BY_BIN",
			  "bin"
			},
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("gnm_tool_group_by_t", values);
	}
	return etype;
}

/******************************************************************/

enum {
	UPDATE_DAO,
	UPDATE_DESCRIPTOR,
	PREPARE_OUTPUT_RANGE,
	LAST_VALIDITY_CHECK,
	FORMAT_OUTPUT_RANGE,
	PERFORM_CALC,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GnmAnalysisTool, gnm_analysis_tool, G_TYPE_OBJECT)

static void
gnm_analysis_tool_init (GnmAnalysisTool *tool)
{
}

static void
gnm_analysis_tool_class_init (GnmAnalysisToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	signals[UPDATE_DAO] = g_signal_new ("update-dao",
		G_TYPE_FROM_CLASS (gobject_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAnalysisToolClass, update_dao),
		NULL, NULL,
		g_cclosure_marshal_generic,
		G_TYPE_BOOLEAN, 1,
		G_TYPE_POINTER);

	signals[UPDATE_DESCRIPTOR] = g_signal_new ("update-descriptor",
		G_TYPE_FROM_CLASS (gobject_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAnalysisToolClass, update_descriptor),
		g_signal_accumulator_first_wins, NULL,
		g_cclosure_marshal_generic,
		G_TYPE_STRING, 1,
		G_TYPE_POINTER);

	signals[PREPARE_OUTPUT_RANGE] = g_signal_new ("prepare-output-range",
		G_TYPE_FROM_CLASS (gobject_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAnalysisToolClass, prepare_output_range),
		NULL, NULL,
		g_cclosure_marshal_generic,
		G_TYPE_BOOLEAN, 2,
		workbook_control_get_type (),
		G_TYPE_POINTER);

	signals[LAST_VALIDITY_CHECK] = g_signal_new ("last-validity-check",
		G_TYPE_FROM_CLASS (gobject_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAnalysisToolClass, last_validity_check),
		NULL, NULL,
		g_cclosure_marshal_generic,
		G_TYPE_BOOLEAN, 2,
		workbook_control_get_type (),
		G_TYPE_POINTER);

	signals[FORMAT_OUTPUT_RANGE] = g_signal_new ("format-output-range",
		G_TYPE_FROM_CLASS (gobject_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAnalysisToolClass, format_output_range),
		NULL, NULL,
		g_cclosure_marshal_generic,
		G_TYPE_BOOLEAN, 2,
		workbook_control_get_type (),
		G_TYPE_POINTER);

	signals[PERFORM_CALC] = g_signal_new ("perform-calc",
		G_TYPE_FROM_CLASS (gobject_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAnalysisToolClass, perform_calc),
		NULL, NULL,
		g_cclosure_marshal_generic,
		G_TYPE_BOOLEAN, 2,
		workbook_control_get_type (),
		G_TYPE_POINTER);
}

/**
 * gnm_analysis_tool_update_dao:
 * @tool: #GnmAnalysisTool
 * @dao: #data_analysis_output_t
 *
 * Adjust the output range size in @dao based on the tool specific
 * input parameters.
 *
 * Returns: %TRUE if an error occurred.
 **/
gboolean
gnm_analysis_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	gboolean error = FALSE;
	g_return_val_if_fail (GNM_IS_ANALYSIS_TOOL (tool), TRUE);
	g_signal_emit (tool, signals[UPDATE_DAO], 0, dao, &error);
	return error;
}

/**
 * gnm_analysis_tool_update_descriptor:
 * @tool: #GnmAnalysisTool
 * @dao: #data_analysis_output_t
 *
 * Retrieve a human-readable string describing the command for undo/redo
 * purposes.
 *
 * Returns: (transfer full): the command descriptor string.
 **/
char *
gnm_analysis_tool_update_descriptor (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	char *result = NULL;
	g_return_val_if_fail (GNM_IS_ANALYSIS_TOOL (tool), NULL);
	g_signal_emit (tool, signals[UPDATE_DESCRIPTOR], 0, dao, &result);
	return result;
}

/**
 * gnm_analysis_tool_prepare_output_range:
 * @wbc: control
 * @tool: #GnmAnalysisTool
 * @dao: #data_analysis_output_t
 *
 * Initialize the output sheet or workbook if necessary.
 *
 * Returns: %TRUE if an error occurred.
 **/
gboolean
gnm_analysis_tool_prepare_output_range (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	gboolean error = FALSE;
	g_return_val_if_fail (GNM_IS_ANALYSIS_TOOL (tool), TRUE);
	g_signal_emit (tool, signals[PREPARE_OUTPUT_RANGE], 0, wbc, dao, &error);
	return error;
}

/**
 * gnm_analysis_tool_last_validity_check:
 * @tool: #GnmAnalysisTool
 * @wbc: control
 * @dao: #data_analysis_output_t
 *
 * Perform a last validation check before the output range is modified.
 *
 * Returns: %TRUE if validation failed.
 **/
gboolean
gnm_analysis_tool_last_validity_check (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	gboolean error = FALSE;
	g_return_val_if_fail (GNM_IS_ANALYSIS_TOOL (tool), TRUE);
	g_signal_emit (tool, signals[LAST_VALIDITY_CHECK], 0, wbc, dao, &error);
	return error;
}

/**
 * gnm_analysis_tool_format_output_range:
 * @tool: #GnmAnalysisTool
 * @wbc: control
 * @dao: #data_analysis_output_t
 *
 * Apply tool-specific formatting (borders, colors, etc.) to the output
 * range in @dao.
 *
 * Returns: %TRUE if an error occurred.
 **/
gboolean
gnm_analysis_tool_format_output_range (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	gboolean error = FALSE;
	g_return_val_if_fail (GNM_IS_ANALYSIS_TOOL (tool), TRUE);
	g_signal_emit (tool, signals[FORMAT_OUTPUT_RANGE], 0, wbc, dao, &error);
	return error;
}

/**
 * gnm_analysis_tool_perform_calc:
 * @tool: #GnmAnalysisTool
 * @wbc: control
 * @dao: #data_analysis_output_t
 *
 * Execute the actual analysis and write the results into the spreadsheet.
 *
 * Returns: %TRUE if the calculation failed.
 **/
gboolean
gnm_analysis_tool_perform_calc (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	gboolean error = FALSE;
	g_return_val_if_fail (GNM_IS_ANALYSIS_TOOL (tool), TRUE);
	g_signal_emit (tool, signals[PERFORM_CALC], 0, wbc, dao, &error);
	return error;
}

/******************************************************************/

G_DEFINE_TYPE (GnmGenericAnalysisTool, gnm_generic_analysis_tool, GNM_ANALYSIS_TOOL_TYPE)

enum {
	GENERIC_PROP_0,
	GENERIC_PROP_LABELS,
	GENERIC_PROP_GROUP_BY
};

static void
gnm_generic_analysis_tool_set_property (GObject *object, guint property_id,
					GValue const *value, GParamSpec *pspec)
{
	GnmGenericAnalysisTool *tool = GNM_GENERIC_ANALYSIS_TOOL (object);

	switch (property_id) {
	case GENERIC_PROP_LABELS:
		tool->base.labels = g_value_get_boolean (value);
		break;
	case GENERIC_PROP_GROUP_BY:
		tool->base.group_by = g_value_get_enum (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_generic_analysis_tool_get_property (GObject *object, guint property_id,
					GValue *value, GParamSpec *pspec)
{
	GnmGenericAnalysisTool *tool = GNM_GENERIC_ANALYSIS_TOOL (object);

	switch (property_id) {
	case GENERIC_PROP_LABELS:
		g_value_set_boolean (value, tool->base.labels);
		break;
	case GENERIC_PROP_GROUP_BY:
		g_value_set_enum (value, tool->base.group_by);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_generic_analysis_tool_init (GnmGenericAnalysisTool *tool)
{
	tool->base.err = analysis_tools_noerr;
	tool->base.input = NULL;
	tool->base.group_by = GNM_TOOL_GROUPED_BY_COL;
	tool->base.labels = FALSE;
}

static void
gnm_generic_analysis_tool_finalize (GObject *obj)
{
	GnmGenericAnalysisTool *tool = GNM_GENERIC_ANALYSIS_TOOL (obj);
	range_list_destroy (tool->base.input);
	tool->base.input = NULL;
	G_OBJECT_CLASS (gnm_generic_analysis_tool_parent_class)->finalize (obj);
}

static void
gnm_generic_analysis_tool_class_init (GnmGenericAnalysisToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->set_property = gnm_generic_analysis_tool_set_property;
	gobject_class->get_property = gnm_generic_analysis_tool_get_property;
	gobject_class->finalize = gnm_generic_analysis_tool_finalize;

	g_object_class_install_property (gobject_class,
		GENERIC_PROP_LABELS,
		g_param_spec_boolean ("labels", NULL, NULL,
			FALSE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		GENERIC_PROP_GROUP_BY,
		g_param_spec_enum ("group-by", NULL, NULL,
			GNM_TOOL_GROUP_BY_TYPE, GNM_TOOL_GROUPED_BY_COL,
			G_PARAM_READWRITE));
}

/********************************************************************/

/************* Correlation Tool *******************************************
 *
 * The correlation tool calculates the correlation coefficient of two
 * data sets.  The two data sets can be grouped by rows or by columns.
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

struct _GnmCorrelationTool {
	GnmGenericAnalysisTool parent;
};

G_DEFINE_TYPE (GnmCorrelationTool, gnm_correlation_tool, GNM_TYPE_GENERIC_ANALYSIS_TOOL)

static void
gnm_correlation_tool_init (G_GNUC_UNUSED GnmCorrelationTool *tool)
{
}

static gboolean
gnm_correlation_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmGenericAnalysisTool *gtool = GNM_GENERIC_ANALYSIS_TOOL (tool);
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
gnm_correlation_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Correlation (%s)"));
}

static gboolean
gnm_correlation_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	dao_prepare_output (wbc, dao, _("Correlation"));
	return FALSE;
}

static gboolean
gnm_correlation_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	return dao_format_output (wbc, dao, _("Correlation"));
}

static gboolean
gnm_correlation_tool_perform_calc (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	GnmGenericAnalysisTool *gtool = GNM_GENERIC_ANALYSIS_TOOL (tool);
	return analysis_tool_table (gtool, dao, _("Correlations"), "CORREL", FALSE);
}

static void
gnm_correlation_tool_class_init (GnmCorrelationToolClass *klass)
{
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	at_class->update_dao = gnm_correlation_tool_update_dao;
	at_class->update_descriptor = gnm_correlation_tool_update_descriptor;
	at_class->prepare_output_range = gnm_correlation_tool_prepare_output_range;
	at_class->format_output_range = gnm_correlation_tool_format_output_range;
	at_class->perform_calc = gnm_correlation_tool_perform_calc;
}

GnmAnalysisTool *
gnm_correlation_tool_new (void)
{
	return g_object_new (GNM_TYPE_CORRELATION_TOOL, NULL);
}

/********************************************************************/

/************* Covariance Tool ********************************************
 *
 * The covariance tool calculates the covariance of two data sets.
 * The two data sets can be grouped by rows or by columns.  The
 * results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

struct _GnmCovarianceTool {
	GnmGenericAnalysisTool parent;
};

G_DEFINE_TYPE (GnmCovarianceTool, gnm_covariance_tool, GNM_TYPE_GENERIC_ANALYSIS_TOOL)

static void
gnm_covariance_tool_init (G_GNUC_UNUSED GnmCovarianceTool *tool)
{
}

static gboolean
gnm_covariance_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmGenericAnalysisTool *gtool = GNM_GENERIC_ANALYSIS_TOOL (tool);
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
gnm_covariance_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Covariance (%s)"));
}

static gboolean
gnm_covariance_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	dao_prepare_output (wbc, dao, _("Covariance"));
	return FALSE;
}

static gboolean
gnm_covariance_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	return dao_format_output (wbc, dao, _("Covariance"));
}

static gboolean
gnm_covariance_tool_perform_calc (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	GnmGenericAnalysisTool *gtool = GNM_GENERIC_ANALYSIS_TOOL (tool);
	return analysis_tool_table (gtool, dao, _("Covariances"), "COVAR", FALSE);
}

static void
gnm_covariance_tool_class_init (GnmCovarianceToolClass *klass)
{
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	at_class->update_dao = gnm_covariance_tool_update_dao;
	at_class->update_descriptor = gnm_covariance_tool_update_descriptor;
	at_class->prepare_output_range = gnm_covariance_tool_prepare_output_range;
	at_class->format_output_range = gnm_covariance_tool_format_output_range;
	at_class->perform_calc = gnm_covariance_tool_perform_calc;
}

GnmAnalysisTool *
gnm_covariance_tool_new (void)
{
	return g_object_new (GNM_TYPE_COVARIANCE_TOOL, NULL);
}

/********************************************************************/

/************* Descriptive Statistics Tool *******************************
 *
 * Descriptive Statistics Tool calculates some useful statistical
 * information such as the mean, standard deviation, sample variance,
 * skewness, kurtosis, and standard error about the given variables.
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

static void
summary_statistics (GnmDescriptiveTool *dtool, data_analysis_output_t *dao)
{
	GnmGenericAnalysisTool *gtool = &dtool->parent;
	guint     col;
	GSList *data = gtool->base.input;
	GnmFunc *fd_mean = gnm_func_get_and_use ("AVERAGE");
	GnmFunc *fd_median = gnm_func_get_and_use (dtool->use_ssmedian ? "SSMEDIAN" : "MEDIAN");
	GnmFunc *fd_mode = gnm_func_get_and_use ("MODE");
	GnmFunc *fd_stdev = gnm_func_get_and_use ("STDEV");
	GnmFunc *fd_var = gnm_func_get_and_use ("VAR");
	GnmFunc *fd_kurt = gnm_func_get_and_use ("KURT");
	GnmFunc *fd_skew = gnm_func_get_and_use ("SKEW");
	GnmFunc *fd_min = gnm_func_get_and_use ("MIN");
	GnmFunc *fd_max = gnm_func_get_and_use ("MAX");
	GnmFunc *fd_sum = gnm_func_get_and_use ("SUM");
	GnmFunc *fd_count = gnm_func_get_and_use ("COUNT");
	GnmFunc *fd_sqrt = gnm_func_get_and_use ("SQRT");

        dao_set_cell (dao, 0, 0, NULL);

	dao_set_italic (dao, 0, 1, 0, 13);
	/*
	 * Note to translators: in the following string and others like it,
	 * the "/" is a separator character that can be changed to anything
	 * if the translation needs the slash; just use, say, "|" instead.
	 *
	 * The items are bundled like this to increase translation context.
	 */
        set_cell_text_col (dao, 0, 1, _("/Mean"
					"/Standard Error"
					"/Median"
					"/Mode"
					"/Standard Deviation"
					"/Sample Variance"
					"/Kurtosis"
					"/Skewness"
					"/Range"
					"/Minimum"
					"/Maximum"
					"/Sum"
					"/Count"));

	for (col = 0; data != NULL; data = data->next, col++) {
		GnmExpr const *expr;
		GnmExpr const *expr_min;
		GnmExpr const *expr_max;
		GnmExpr const *expr_var;
		GnmExpr const *expr_count;
		GnmValue *val_org = value_dup (data->data);

		dao_set_italic (dao, col + 1, 0, col+1, 0);
		/* Note that analysis_tools_write_label may modify val_org */
		analysis_tools_write_label (gtool, val_org, dao,
					    col + 1, 0, col + 1);

	        /* Mean */
		expr = gnm_expr_new_funcall1
			(fd_mean,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 1, expr);

		/* Standard Deviation */
		expr = gnm_expr_new_funcall1
			(fd_stdev,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 5, expr);

		/* Sample Variance */
		expr_var = gnm_expr_new_funcall1
			(fd_var,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 6, gnm_expr_copy (expr_var));

		/* Median */
		expr = gnm_expr_new_funcall1
			(fd_median,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 3, expr);

		/* Mode */
		expr = gnm_expr_new_funcall1
			(fd_mode,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 4, expr);

		/* Kurtosis */
		expr = gnm_expr_new_funcall1
			(fd_kurt,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 7, expr);

		/* Skewness */
		expr = gnm_expr_new_funcall1
			(fd_skew,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 8, expr);

		/* Minimum */
		expr_min = gnm_expr_new_funcall1
			(fd_min,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 10, gnm_expr_copy (expr_min));

		/* Maximum */
		expr_max = gnm_expr_new_funcall1
			(fd_max,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 11, gnm_expr_copy (expr_max));

		/* Range */
		expr = gnm_expr_new_binary (expr_max, GNM_EXPR_OP_SUB, expr_min);
		dao_set_cell_expr (dao, col + 1, 9, expr);

		/* Sum */
		expr = gnm_expr_new_funcall1
			(fd_sum,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 12, expr);

		/* Count */
		expr_count = gnm_expr_new_funcall1
			(fd_count,
			 gnm_expr_new_constant (val_org));
		dao_set_cell_expr (dao, col + 1, 13, gnm_expr_copy (expr_count));

		/* Standard Error */
		expr = gnm_expr_new_funcall1
			(fd_sqrt,
			 gnm_expr_new_binary (expr_var,
					      GNM_EXPR_OP_DIV,
					      expr_count));
		dao_set_cell_expr (dao, col + 1, 2, expr);
	}

	gnm_func_dec_usage (fd_mean);
	gnm_func_dec_usage (fd_median);
	gnm_func_dec_usage (fd_mode);
	gnm_func_dec_usage (fd_stdev);
	gnm_func_dec_usage (fd_var);
	gnm_func_dec_usage (fd_kurt);
	gnm_func_dec_usage (fd_skew);
	gnm_func_dec_usage (fd_min);
	gnm_func_dec_usage (fd_max);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_sqrt);
}

static void
confidence_level (GnmDescriptiveTool *dtool, data_analysis_output_t *dao)
{
	GnmGenericAnalysisTool *gtool = &dtool->parent;
        guint col;
	char *buffer;
	char *format;
	GSList *data = gtool->base.input;

	format = g_strdup_printf (_("/%%%s%%%% CI for the Mean from"
				    "/to"), GNM_FORMAT_g);
	buffer = g_strdup_printf (format, dtool->c_level * 100);
	g_free (format);
	dao_set_italic (dao, 0, 1, 0, 2);
	set_cell_text_col (dao, 0, 1, buffer);
        g_free (buffer);

        dao_set_cell (dao, 0, 0, NULL);

	GnmFunc *fd_mean = gnm_func_get_and_use ("AVERAGE");
	GnmFunc *fd_var = gnm_func_get_and_use ("VAR");
	GnmFunc *fd_count = gnm_func_get_and_use ("COUNT");
	GnmFunc *fd_tinv = gnm_func_get_and_use ("TINV");
	GnmFunc *fd_sqrt = gnm_func_get_and_use ("SQRT");

	for (col = 0; data != NULL; data = data->next, col++) {
		GnmExpr const *expr;
		GnmExpr const *expr_mean;
		GnmExpr const *expr_var;
		GnmExpr const *expr_count;
		GnmValue *val_org = value_dup (data->data);

		dao_set_italic (dao, col+1, 0, col+1, 0);
		/* Note that analysis_tools_write_label may modify val_org */
		analysis_tools_write_label (gtool, val_org, dao, col + 1, 0, col + 1);

		expr_mean = gnm_expr_new_funcall1
			(fd_mean,
			 gnm_expr_new_constant (value_dup (val_org)));

		expr_var = gnm_expr_new_funcall1
			(fd_var,
			 gnm_expr_new_constant (value_dup (val_org)));

		expr_count = gnm_expr_new_funcall1
			(fd_count,
			 gnm_expr_new_constant (val_org));

		expr = gnm_expr_new_binary
			(gnm_expr_new_funcall2
			 (fd_tinv,
			  gnm_expr_new_constant (value_new_float (1 - dtool->c_level)),
			  gnm_expr_new_binary
			  (gnm_expr_copy (expr_count),
			   GNM_EXPR_OP_SUB,
			   gnm_expr_new_constant (value_new_int (1)))),
			 GNM_EXPR_OP_MULT,
			 gnm_expr_new_funcall1
			 (fd_sqrt,
			  gnm_expr_new_binary (expr_var,
					       GNM_EXPR_OP_DIV,
					       expr_count)));

		dao_set_cell_expr (dao, col + 1, 1,
				   gnm_expr_new_binary
				   (gnm_expr_copy (expr_mean),
				    GNM_EXPR_OP_SUB,
				    gnm_expr_copy (expr)));
		dao_set_cell_expr (dao, col + 1, 2,
				   gnm_expr_new_binary (expr_mean,
							GNM_EXPR_OP_ADD,
							expr));
	}

	gnm_func_dec_usage (fd_mean);
	gnm_func_dec_usage (fd_var);
	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_tinv);
	gnm_func_dec_usage (fd_sqrt);
}

static void
kth_smallest_largest (GnmDescriptiveTool *dtool, data_analysis_output_t *dao,
		      char const* func, char const* label, int k)
{
	GnmGenericAnalysisTool *gtool = &dtool->parent;
        guint col;
	GSList *data = gtool->base.input;
	GnmFunc *fd = gnm_func_get_and_use (func);

	dao_set_italic (dao, 0, 1, 0, 1);
        dao_set_cell_printf (dao, 0, 1, label, k);

        dao_set_cell (dao, 0, 0, NULL);

	for (col = 0; data != NULL; data = data->next, col++) {
		GnmExpr const *expr = NULL;
		GnmValue *val = value_dup (data->data);

		dao_set_italic (dao, col + 1, 0, col + 1, 0);
		analysis_tools_write_label (gtool, val, dao, col + 1, 0, col + 1);

		expr = gnm_expr_new_funcall2
			(fd,
			 gnm_expr_new_constant (val),
			 gnm_expr_new_constant (value_new_int (k)));

		dao_set_cell_expr (dao, col + 1, 1, expr);
	}

	gnm_func_dec_usage (fd);
}


G_DEFINE_TYPE (GnmDescriptiveTool, gnm_descriptive_tool, GNM_TYPE_GENERIC_ANALYSIS_TOOL)

enum {
	DESCRIPTIVE_PROP_0,
	DESCRIPTIVE_PROP_DO_SUMMARY_STATISTICS,
	DESCRIPTIVE_PROP_DO_CONFIDENCE_LEVEL,
	DESCRIPTIVE_PROP_DO_KTH_LARGEST,
	DESCRIPTIVE_PROP_DO_KTH_SMALLEST,
	DESCRIPTIVE_PROP_USE_SSMEDIAN,
	DESCRIPTIVE_PROP_K_SMALLEST,
	DESCRIPTIVE_PROP_K_LARGEST,
	DESCRIPTIVE_PROP_CONFIDENCE_LEVEL
};

static void
gnm_descriptive_tool_set_property (GObject *object, guint property_id,
				   GValue const *value, GParamSpec *pspec)
{
	GnmDescriptiveTool *dtool = GNM_DESCRIPTIVE_TOOL (object);

	switch (property_id) {
	case DESCRIPTIVE_PROP_DO_SUMMARY_STATISTICS:
		dtool->summary_statistics = g_value_get_boolean (value);
		break;
	case DESCRIPTIVE_PROP_DO_CONFIDENCE_LEVEL:
		dtool->confidence_level = g_value_get_boolean (value);
		break;
	case DESCRIPTIVE_PROP_DO_KTH_LARGEST:
		dtool->kth_largest = g_value_get_boolean (value);
		break;
	case DESCRIPTIVE_PROP_DO_KTH_SMALLEST:
		dtool->kth_smallest = g_value_get_boolean (value);
		break;
	case DESCRIPTIVE_PROP_USE_SSMEDIAN:
		dtool->use_ssmedian = g_value_get_boolean (value);
		break;
	case DESCRIPTIVE_PROP_K_SMALLEST:
		dtool->k_smallest = g_value_get_int (value);
		break;
	case DESCRIPTIVE_PROP_K_LARGEST:
		dtool->k_largest = g_value_get_int (value);
		break;
	case DESCRIPTIVE_PROP_CONFIDENCE_LEVEL:
		dtool->c_level = g_value_get_double (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_descriptive_tool_get_property (GObject *object, guint property_id,
				   GValue *value, GParamSpec *pspec)
{
	GnmDescriptiveTool *dtool = GNM_DESCRIPTIVE_TOOL (object);

	switch (property_id) {
	case DESCRIPTIVE_PROP_DO_SUMMARY_STATISTICS:
		g_value_set_boolean (value, dtool->summary_statistics);
		break;
	case DESCRIPTIVE_PROP_DO_CONFIDENCE_LEVEL:
		g_value_set_boolean (value, dtool->confidence_level);
		break;
	case DESCRIPTIVE_PROP_DO_KTH_LARGEST:
		g_value_set_boolean (value, dtool->kth_largest);
		break;
	case DESCRIPTIVE_PROP_DO_KTH_SMALLEST:
		g_value_set_boolean (value, dtool->kth_smallest);
		break;
	case DESCRIPTIVE_PROP_USE_SSMEDIAN:
		g_value_set_boolean (value, dtool->use_ssmedian);
		break;
	case DESCRIPTIVE_PROP_K_SMALLEST:
		g_value_set_int (value, dtool->k_smallest);
		break;
	case DESCRIPTIVE_PROP_K_LARGEST:
		g_value_set_int (value, dtool->k_largest);
		break;
	case DESCRIPTIVE_PROP_CONFIDENCE_LEVEL:
		g_value_set_double (value, dtool->c_level);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_descriptive_tool_init (GnmDescriptiveTool *tool)
{
	tool->summary_statistics = TRUE;
	tool->confidence_level = TRUE;
	tool->kth_largest = TRUE;
	tool->kth_smallest = TRUE;
	tool->use_ssmedian = FALSE;
	tool->k_largest = 1;
	tool->k_smallest = 1;
	tool->c_level = 0.95;
}

static gboolean
gnm_descriptive_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmGenericAnalysisTool *gtool = GNM_GENERIC_ANALYSIS_TOOL (tool);
	analysis_tool_prepare_input_range (gtool);
	if (!analysis_tool_check_input_homogeneity (gtool)) {
		gtool->base.err = gtool->base.group_by + 1;
		return TRUE;
	}
	dao_adjust (dao, 2 + g_slist_length (gtool->base.input), 16 + 4 + 4 + 4);
	return FALSE;
}

static char *
gnm_descriptive_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Descriptive Statistics (%s)"));
}

static gboolean
gnm_descriptive_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	dao_prepare_output (wbc, dao, _("Descriptive Statistics"));
	return FALSE;
}

static gboolean
gnm_descriptive_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	return dao_format_output (wbc, dao, _("Descriptive Statistics"));
}

static gboolean
gnm_descriptive_tool_perform_calc (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	GnmDescriptiveTool *dtool = GNM_DESCRIPTIVE_TOOL (tool);

        if (dtool->summary_statistics) {
                summary_statistics (dtool, dao);
		dao->offset_row += 16;
		if (dao->rows <= dao->offset_row)
			goto finish_descriptive_tool;
	}
        if (dtool->confidence_level) {
                confidence_level (dtool, dao);
		dao->offset_row += 4;
		if (dao->rows <= dao->offset_row)
			goto finish_descriptive_tool;
	}
        if (dtool->kth_largest) {
		kth_smallest_largest (dtool, dao, "LARGE", _("Largest (%d)"),
				      dtool->k_largest);
		dao->offset_row += 4;
		if (dao->rows <= dao->offset_row)
			goto finish_descriptive_tool;
	}
        if (dtool->kth_smallest)
                kth_smallest_largest (dtool, dao, "SMALL", _("Smallest (%d)"),
				      dtool->k_smallest);

 finish_descriptive_tool:

	dao_redraw_respan (dao);
	return 0;
}

static void
gnm_descriptive_tool_class_init (GnmDescriptiveToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->set_property = gnm_descriptive_tool_set_property;
	gobject_class->get_property = gnm_descriptive_tool_get_property;

	at_class->update_dao = gnm_descriptive_tool_update_dao;
	at_class->update_descriptor = gnm_descriptive_tool_update_descriptor;
	at_class->prepare_output_range = gnm_descriptive_tool_prepare_output_range;
	at_class->format_output_range = gnm_descriptive_tool_format_output_range;
	at_class->perform_calc = gnm_descriptive_tool_perform_calc;

	g_object_class_install_property (gobject_class,
		DESCRIPTIVE_PROP_DO_SUMMARY_STATISTICS,
		g_param_spec_boolean ("do-summary-statistics", NULL, NULL,
				      TRUE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		DESCRIPTIVE_PROP_DO_CONFIDENCE_LEVEL,
		g_param_spec_boolean ("do-confidence-level", NULL, NULL,
				      TRUE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		DESCRIPTIVE_PROP_DO_KTH_LARGEST,
		g_param_spec_boolean ("do-kth-largest", NULL, NULL,
				      TRUE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		DESCRIPTIVE_PROP_DO_KTH_SMALLEST,
		g_param_spec_boolean ("do-kth-smallest", NULL, NULL,
				      TRUE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		DESCRIPTIVE_PROP_USE_SSMEDIAN,
		g_param_spec_boolean ("use-ssmedian", NULL, NULL,
				      FALSE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		DESCRIPTIVE_PROP_K_SMALLEST,
		g_param_spec_int ("k-smallest", NULL, NULL,
				  1, G_MAXINT, 1, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		DESCRIPTIVE_PROP_K_LARGEST,
		g_param_spec_int ("k-largest", NULL, NULL,
				  1, G_MAXINT, 1, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		DESCRIPTIVE_PROP_CONFIDENCE_LEVEL,
		g_param_spec_double ("confidence-level", NULL, NULL,
				     0.0, 1.0, 0.95, G_PARAM_READWRITE));
}

GnmAnalysisTool *
gnm_descriptive_tool_new (void)
{
	return g_object_new (GNM_TYPE_DESCRIPTIVE_TOOL, NULL);
}

/********************************************************************/

/************* Anova: Single Factor Tool **********************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

G_DEFINE_TYPE (GnmAnovaSingleTool, gnm_anova_single_tool, GNM_TYPE_GENERIC_ANALYSIS_TOOL)

enum {
	ANOVA_SINGLE_PROP_0,
	ANOVA_SINGLE_PROP_ALPHA
};

static void
gnm_anova_single_tool_set_property (GObject *object, guint property_id,
				    GValue const *value, GParamSpec *pspec)
{
	GnmAnovaSingleTool *tool = GNM_ANOVA_SINGLE_TOOL (object);

	switch (property_id) {
	case ANOVA_SINGLE_PROP_ALPHA:
		tool->alpha = g_value_get_double (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_anova_single_tool_get_property (GObject *object, guint property_id,
				    GValue *value, GParamSpec *pspec)
{
	GnmAnovaSingleTool *tool = GNM_ANOVA_SINGLE_TOOL (object);

	switch (property_id) {
	case ANOVA_SINGLE_PROP_ALPHA:
		g_value_set_double (value, tool->alpha);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_anova_single_tool_init (GnmAnovaSingleTool *tool)
{
	tool->alpha = 0.05;
}

static gboolean
gnm_anova_single_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmGenericAnalysisTool *gtool = GNM_GENERIC_ANALYSIS_TOOL (tool);
	analysis_tool_prepare_input_range (gtool);
	if (!analysis_tool_check_input_homogeneity (gtool)) {
		gtool->base.err = gtool->base.group_by + 1;
		return TRUE;
	}
	dao_adjust (dao, 7, 12);
	return FALSE;
}

static char *
gnm_anova_single_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Single Factor ANOVA (%s)"));
}

static gboolean
gnm_anova_single_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	dao_prepare_output (wbc, dao, _("Single Factor ANOVA"));
	return FALSE;
}

static gboolean
gnm_anova_single_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	return dao_format_output (wbc, dao, _("Single Factor ANOVA"));
}

static gboolean
gnm_anova_single_tool_perform_calc (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	GnmAnovaSingleTool *atool = GNM_ANOVA_SINGLE_TOOL (tool);
	GnmGenericAnalysisTool *gtool = &atool->parent;
	GSList *inputdata = gtool->base.input;

	guint index;

	dao_set_italic (dao, 0, 0, 0, 2);
	dao_set_cell (dao, 0, 0, _("Anova: Single Factor"));
	dao_set_cell (dao, 0, 2, _("SUMMARY"));

	dao_set_italic (dao, 0, 3, 4, 3);
	set_cell_text_row (dao, 0, 3, _("/Groups"
					"/Count"
					"/Sum"
					"/Average"
					"/Variance"));

	GnmFunc *fd_mean = gnm_func_get_and_use ("AVERAGE");
	GnmFunc *fd_var = gnm_func_get_and_use ("VAR");
	GnmFunc *fd_sum = gnm_func_get_and_use ("SUM");
	GnmFunc *fd_count = gnm_func_get_and_use ("COUNT");
	GnmFunc *fd_devsq = gnm_func_get_and_use ("DEVSQ");

	dao->offset_row += 4;
	if (dao->rows <= dao->offset_row)
		goto finish_anova_single_factor_tool;

	/* SUMMARY */

	for (index = 0; inputdata != NULL;
	     inputdata = inputdata->next, index++) {
		GnmValue *val_org = value_dup (inputdata->data);

		/* Label */
		dao_set_italic (dao, 0, index, 0, index);
		analysis_tools_write_label (gtool, val_org, dao, 0, index, index + 1);

		/* Count */
		dao_set_cell_expr
			(dao, 1, index,
			 gnm_expr_new_funcall1
			 (fd_count,
			  gnm_expr_new_constant (value_dup (val_org))));

		/* Sum */
		dao_set_cell_expr
			(dao, 2, index,
			 gnm_expr_new_funcall1
			 (fd_sum,
			  gnm_expr_new_constant (value_dup (val_org))));

		/* Average */
		dao_set_cell_expr
			(dao, 3, index,
			 gnm_expr_new_funcall1
			 (fd_mean,
			  gnm_expr_new_constant (value_dup (val_org))));

		/* Variance */
		dao_set_cell_expr
			(dao, 4, index,
			 gnm_expr_new_funcall1
			 (fd_var,
			  gnm_expr_new_constant (val_org)));

	}

	dao->offset_row += index + 2;
	if (dao->rows <= dao->offset_row)
		goto finish_anova_single_factor_tool;


	dao_set_italic (dao, 0, 0, 0, 4);
	set_cell_text_col (dao, 0, 0, _("/ANOVA"
					"/Source of Variation"
					"/Between Groups"
					"/Within Groups"
					"/Total"));
	dao_set_italic (dao, 1, 1, 6, 1);
	set_cell_text_row (dao, 1, 1, _("/SS"
					"/df"
					"/MS"
					"/F"
					"/P-value"
					"/F critical"));

	/* ANOVA */
	{
		GnmExprList *sum_wdof_args = NULL;
		GnmExprList *sum_tdof_args = NULL;
		GnmExprList *arg_ss_total = NULL;
		GnmExprList *arg_ss_within = NULL;

		GnmExpr const *expr_wdof = NULL;
		GnmExpr const *expr_ss_total = NULL;
		GnmExpr const *expr_ss_within = NULL;

		for (inputdata = gtool->base.input; inputdata != NULL;
		     inputdata = inputdata->next) {
			GnmValue *val_org = value_dup (inputdata->data);
			GnmExpr const *expr_one;
			GnmExpr const *expr_count_one;

			analysis_tools_remove_label (val_org,
						     gtool->base.labels,
						     gtool->base.group_by);
			expr_one = gnm_expr_new_constant (value_dup (val_org));

			arg_ss_total =  gnm_expr_list_append
				(arg_ss_total,
				 gnm_expr_new_constant (val_org));

			arg_ss_within = gnm_expr_list_append
				(arg_ss_within,
				 gnm_expr_new_funcall1
				 (fd_devsq, gnm_expr_copy (expr_one)));

			expr_count_one =
				gnm_expr_new_funcall1 (fd_count, expr_one);

			sum_wdof_args = gnm_expr_list_append
				(sum_wdof_args,
				 gnm_expr_new_binary (
					 gnm_expr_copy (expr_count_one),
					 GNM_EXPR_OP_SUB,
					 gnm_expr_new_constant
					 (value_new_int (1))));
			sum_tdof_args = gnm_expr_list_append
				(sum_tdof_args,
				 expr_count_one);
		}

		expr_ss_total = gnm_expr_new_funcall
			(fd_devsq, arg_ss_total);
		expr_ss_within = gnm_expr_new_funcall
			(fd_sum, arg_ss_within);

		{
			/* SS between groups */
			GnmExpr const *expr_ss_between;

			if (dao_cell_is_visible (dao, 1,4)) {
				expr_ss_between = gnm_expr_new_binary
					(make_cellref (0, 2),
					 GNM_EXPR_OP_SUB,
					 make_cellref (0, 1));

			} else {
				expr_ss_between = gnm_expr_new_binary
					(gnm_expr_copy (expr_ss_total),
					 GNM_EXPR_OP_SUB,
					 gnm_expr_copy (expr_ss_within));
			}
			dao_set_cell_expr (dao, 1, 2, expr_ss_between);
		}
		{
			/* SS within groups */
			dao_set_cell_expr (dao, 1, 3, gnm_expr_copy (expr_ss_within));
		}
		{
			/* SS total groups */
			dao_set_cell_expr (dao, 1, 4, expr_ss_total);
		}
		{
			/* Between groups degrees of freedom */
			dao_set_cell_int (dao, 2, 2,
					  g_slist_length (gtool->base.input) - 1);
		}
		{
			/* Within groups degrees of freedom */
			expr_wdof = gnm_expr_new_funcall (fd_sum, sum_wdof_args);
			dao_set_cell_expr (dao, 2, 3, gnm_expr_copy (expr_wdof));
		}
		{
			/* Total degrees of freedom */
			GnmExpr const *expr_tdof =
				gnm_expr_new_binary
				(gnm_expr_new_funcall (fd_sum, sum_tdof_args),
				 GNM_EXPR_OP_SUB,
				 gnm_expr_new_constant (value_new_int (1)));
			dao_set_cell_expr (dao, 2, 4, expr_tdof);
		}
		{
			/* MS values */
			GnmExpr const *expr_ms =
				gnm_expr_new_binary
				(make_cellref (-2, 0),
				 GNM_EXPR_OP_DIV,
				 make_cellref (-1, 0));
			dao_set_cell_expr (dao, 3, 2, gnm_expr_copy (expr_ms));
			dao_set_cell_expr (dao, 3, 3, expr_ms);
		}
		{
			/* Observed F */
			GnmExpr const *expr_denom;
			GnmExpr const *expr_f;

			if (dao_cell_is_visible (dao, 3, 3)) {
				expr_denom = make_cellref (-1, 1);
				gnm_expr_free (expr_ss_within);
			} else {
				expr_denom = gnm_expr_new_binary
					(expr_ss_within,
					 GNM_EXPR_OP_DIV,
					 gnm_expr_copy (expr_wdof));
			}

			expr_f = gnm_expr_new_binary
				(make_cellref (-1, 0),
				 GNM_EXPR_OP_DIV,
				 expr_denom);
			dao_set_cell_expr(dao, 4, 2, expr_f);
		}
		{
			/* P value */
			GnmFunc *fd_fdist;
			const GnmExpr *arg1;
			const GnmExpr *arg2;
			const GnmExpr *arg3;

			arg1 = make_cellref (-1, 0);
			arg2 = make_cellref (-3, 0);

			if (dao_cell_is_visible (dao, 2, 3)) {
				arg3 = make_cellref (-3, 1);
			} else {
				arg3 = gnm_expr_copy (expr_wdof);
			}

			fd_fdist = gnm_func_get_and_use ("FDIST");

			dao_set_cell_expr
				(dao, 5, 2,
				 gnm_expr_new_funcall3
				 (fd_fdist,
				  arg1, arg2, arg3));
			if (fd_fdist)
				gnm_func_dec_usage (fd_fdist);
		}
		{
			/* Critical F*/
			GnmFunc *fd_finv;
			const GnmExpr *arg3;

			if (dao_cell_is_visible (dao, 2, 3)) {
				arg3 = make_cellref (-4, 1);
				gnm_expr_free (expr_wdof);
			} else
				arg3 = expr_wdof;

			fd_finv = gnm_func_get_and_use ("FINV");

			dao_set_cell_expr
				(dao, 6, 2,
				 gnm_expr_new_funcall3
				 (fd_finv,
				  gnm_expr_new_constant
				  (value_new_float (atool->alpha)),
				  make_cellref (-4, 0),
				  arg3));
			gnm_func_dec_usage (fd_finv);
		}
	}

finish_anova_single_factor_tool:

	gnm_func_dec_usage (fd_mean);
	gnm_func_dec_usage (fd_var);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_devsq);

	dao->offset_row = 0;
	dao->offset_col = 0;

	dao_redraw_respan (dao);
        return FALSE;
}


static void
gnm_anova_single_tool_class_init (GnmAnovaSingleToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->set_property = gnm_anova_single_tool_set_property;
	gobject_class->get_property = gnm_anova_single_tool_get_property;

	at_class->update_dao = gnm_anova_single_tool_update_dao;
	at_class->update_descriptor = gnm_anova_single_tool_update_descriptor;
	at_class->prepare_output_range = gnm_anova_single_tool_prepare_output_range;
	at_class->format_output_range = gnm_anova_single_tool_format_output_range;
	at_class->perform_calc = gnm_anova_single_tool_perform_calc;

	g_object_class_install_property (gobject_class,
		ANOVA_SINGLE_PROP_ALPHA,
		g_param_spec_double ("alpha", NULL, NULL,
			0.0, 1.0, 0.05, G_PARAM_READWRITE));
}

GnmAnalysisTool *
gnm_anova_single_tool_new (void)
{
	return g_object_new (GNM_TYPE_ANOVA_SINGLE_TOOL, NULL);
}

/********************************************************************/

/************* Moving Average Tool *****************************************
 *
 * The moving average tool calculates moving averages of given data
 * set.  The results are given in a table which can be printed out in
 * a new sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

static GnmExpr const *
analysis_tool_moving_average_funcall5 (GnmFunc *fd, GnmExpr const *ex, int y, int x, int dy, int dx)
{
	GnmExprList *list;
	list = gnm_expr_list_prepend (NULL, gnm_expr_new_constant (value_new_int (dx)));
	list = gnm_expr_list_prepend (list, gnm_expr_new_constant (value_new_int (dy)));
	list = gnm_expr_list_prepend (list, gnm_expr_new_constant (value_new_int (x)));
	list = gnm_expr_list_prepend (list, gnm_expr_new_constant (value_new_int (y)));
	list = gnm_expr_list_prepend (list, gnm_expr_copy (ex));

	return gnm_expr_new_funcall (fd, list);
}

static GnmExpr const *
analysis_tool_moving_average_weighted_av (GnmFunc *fd_sum, GnmFunc *fd_in, GnmExpr const *ex,
					  int y, int x, int dy, int dx, const int *w)
{
	GnmExprList *list = NULL;

	while (*w != 0) {
		list = gnm_expr_list_prepend
			(list, gnm_expr_new_binary
			 (gnm_expr_new_constant (value_new_int (*w)),
			  GNM_EXPR_OP_MULT,
			  gnm_expr_new_funcall3 (fd_in, gnm_expr_copy (ex),
						 gnm_expr_new_constant (value_new_int (y)),
						 gnm_expr_new_constant (value_new_int (x)))));
		w++;
		x += dx;
		y += dy;
	}

	return gnm_expr_new_funcall (fd_sum, list);
}

G_DEFINE_TYPE (GnmMovingAverageTool, gnm_moving_average_tool, GNM_TYPE_GENERIC_ANALYSIS_TOOL)

enum {
	MOVING_AVERAGE_PROP_0,
	MOVING_AVERAGE_PROP_INTERVAL,
	MOVING_AVERAGE_PROP_STD_ERROR_FLAG,
	MOVING_AVERAGE_PROP_DF,
	MOVING_AVERAGE_PROP_OFFSET,
	MOVING_AVERAGE_PROP_SHOW_GRAPH,
	MOVING_AVERAGE_PROP_MA_TYPE
};

static void
gnm_moving_average_tool_set_property (GObject *object, guint property_id,
				      GValue const *value, GParamSpec *pspec)
{
	GnmMovingAverageTool *tool = GNM_MOVING_AVERAGE_TOOL (object);

	switch (property_id) {
	case MOVING_AVERAGE_PROP_INTERVAL:
		tool->interval = g_value_get_int (value);
		break;
	case MOVING_AVERAGE_PROP_STD_ERROR_FLAG:
		tool->std_error_flag = g_value_get_int (value);
		break;
	case MOVING_AVERAGE_PROP_DF:
		tool->df = g_value_get_int (value);
		break;
	case MOVING_AVERAGE_PROP_OFFSET:
		tool->offset = g_value_get_int (value);
		break;
	case MOVING_AVERAGE_PROP_SHOW_GRAPH:
		tool->show_graph = g_value_get_boolean (value);
		break;
	case MOVING_AVERAGE_PROP_MA_TYPE:
		tool->ma_type = g_value_get_int (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_moving_average_tool_get_property (GObject *object, guint property_id,
				      GValue *value, GParamSpec *pspec)
{
	GnmMovingAverageTool *tool = GNM_MOVING_AVERAGE_TOOL (object);

	switch (property_id) {
	case MOVING_AVERAGE_PROP_INTERVAL:
		g_value_set_int (value, tool->interval);
		break;
	case MOVING_AVERAGE_PROP_STD_ERROR_FLAG:
		g_value_set_int (value, tool->std_error_flag);
		break;
	case MOVING_AVERAGE_PROP_DF:
		g_value_set_int (value, tool->df);
		break;
	case MOVING_AVERAGE_PROP_OFFSET:
		g_value_set_int (value, tool->offset);
		break;
	case MOVING_AVERAGE_PROP_SHOW_GRAPH:
		g_value_set_boolean (value, tool->show_graph);
		break;
	case MOVING_AVERAGE_PROP_MA_TYPE:
		g_value_set_int (value, tool->ma_type);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_moving_average_tool_init (GnmMovingAverageTool *tool)
{
	tool->interval = 1;
	tool->std_error_flag = 0;
	tool->df = 0;
	tool->offset = 0;
	tool->show_graph = FALSE;
	tool->ma_type = 0;
}

static gboolean
gnm_moving_average_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmGenericAnalysisTool *gtool = GNM_GENERIC_ANALYSIS_TOOL (tool);
	analysis_tool_prepare_input_range (gtool);
	if (!analysis_tool_check_input_homogeneity (gtool)) {
		gtool->base.err = gtool->base.group_by + 1;
		return TRUE;
	}
	dao_adjust (dao, 1, analysis_tool_calc_length (gtool));
	return FALSE;
}

static char *
gnm_moving_average_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Moving Average (%s)"));
}

static gboolean
gnm_moving_average_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	dao_prepare_output (wbc, dao, _("Moving Average"));
	return FALSE;
}

static gboolean
gnm_moving_average_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	return dao_format_output (wbc, dao, _("Moving Average"));
}

static gboolean
gnm_moving_average_tool_perform_calc (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	GnmMovingAverageTool *mtool = GNM_MOVING_AVERAGE_TOOL (tool);
	GnmGenericAnalysisTool *gtool = &mtool->parent;
	GnmFunc *fd_index = NULL;
	GnmFunc *fd_average;
	GnmFunc *fd_offset;
	GnmFunc *fd_sqrt = NULL;
	GnmFunc *fd_sumxmy2 = NULL;
	GnmFunc *fd_sum = NULL;
	GSList *l;
	gint col = 0;
	gint source;
	SheetObject *so = NULL;
	GogPlot	     *plot = NULL;

	if (gtool->base.labels || mtool->ma_type == moving_average_type_wma
	    || mtool->ma_type== moving_average_type_spencer_ma) {
		fd_index = gnm_func_get_and_use ("INDEX");
	}
	if (mtool->std_error_flag) {
		fd_sqrt = gnm_func_get_and_use ("SQRT");
		fd_sumxmy2 = gnm_func_get_and_use ("SUMXMY2");
	}
	if (moving_average_type_wma == mtool->ma_type || moving_average_type_spencer_ma == mtool->ma_type) {
		fd_sum = gnm_func_get_and_use ("SUM");
	}
	fd_average = gnm_func_get_and_use ("AVERAGE");
	fd_offset = gnm_func_get_and_use ("OFFSET");

	if (mtool->show_graph) {
		GogGraph     *graph;
		GogChart     *chart;

		graph = g_object_new (GOG_TYPE_GRAPH, NULL);
		chart = GOG_CHART (gog_object_add_by_name (GOG_OBJECT (graph), "Chart", NULL));
		plot = gog_plot_new_by_name ("GogLinePlot");
		gog_object_add_by_name (GOG_OBJECT (chart), "Plot", GOG_OBJECT (plot));
		so = sheet_object_graph_new (graph);
		g_object_unref (graph);
	}

	for (l = gtool->base.input, source = 1; l; l = l->next, col++, source++) {
		GnmValue *val = value_dup ((GnmValue *)l->data);
		GnmValue *val_c = NULL;
		GnmExpr const *expr_title = NULL;
		GnmExpr const *expr_input = NULL;
		char const *format = NULL;
		gint height;
		gint  x = 0;
		gint  y = 0;
		gint  *mover;
		guint *delta_mover;
		guint delta_x = 1;
		guint delta_y = 1;
		gint row, base;
		Sheet *sheet;
		GnmEvalPos ep;

		eval_pos_init_sheet (&ep, val->v_range.cell.a.sheet);

		if (gtool->base.labels) {
			val_c = value_dup (val);
			switch (gtool->base.group_by) {
			case GNM_TOOL_GROUPED_BY_ROW:
				val->v_range.cell.a.col++;
				break;
			default:
				val->v_range.cell.a.row++;
				break;
			}
			expr_title = gnm_expr_new_funcall1 (fd_index,
							    gnm_expr_new_constant (val_c));

			dao_set_italic (dao, col, 0, col, 0);
			dao_set_cell_expr (dao, col, 0, expr_title);
		} else {
			switch (gtool->base.group_by) {
			case GNM_TOOL_GROUPED_BY_ROW:
				format = _("Row %d");
				break;
			default:
				format = _("Column %d");
				break;
			}
			dao_set_cell_printf (dao, col, 0, format, source);
		}

		switch (gtool->base.group_by) {
		case GNM_TOOL_GROUPED_BY_ROW:
			height = value_area_get_width (val, &ep);
			mover = &x;
			delta_mover = &delta_x;
			break;
		default:
			height = value_area_get_height (val, &ep);
			mover = &y;
			delta_mover = &delta_y;
			break;
		}

		sheet = val->v_range.cell.a.sheet;
		expr_input = gnm_expr_new_constant (val);

		if  (plot != NULL) {
			GogSeries    *series;

			series = gog_plot_new_series (plot);
			gog_series_set_dim (series, 1,
					    gnm_go_data_vector_new_expr (sheet,
									 gnm_expr_top_new (gnm_expr_copy (expr_input))),
					    NULL);

			series = gog_plot_new_series (plot);
			gog_series_set_dim (series, 1,
					    dao_go_data_vector (dao, col, 1, col, height),
					    NULL);
		}

		switch (mtool->ma_type) {
		case moving_average_type_central_sma:
		{
			GnmExpr const *expr_offset_last = NULL;
			GnmExpr const *expr_offset = NULL;
			*delta_mover = mtool->interval;
			(*mover) = 1 - mtool->interval + mtool->offset;
			for (row = 1; row <= height; row++, (*mover)++) {
				expr_offset_last = expr_offset;
				expr_offset = NULL;
				if ((*mover >= 0) && (*mover < height - mtool->interval + 1)) {
					expr_offset = gnm_expr_new_funcall1
						(fd_average, analysis_tool_moving_average_funcall5
						 (fd_offset,expr_input, y, x, delta_y, delta_x));

					if (expr_offset_last == NULL)
						dao_set_cell_na (dao, col, row);
					else
						dao_set_cell_expr (dao, col, row,
								   gnm_expr_new_funcall2 (fd_average, expr_offset_last,
											  gnm_expr_copy (expr_offset)));
				} else {
					if (expr_offset_last != NULL) {
						gnm_expr_free (expr_offset_last);
						expr_offset_last = NULL;
					}
					dao_set_cell_na (dao, col, row);
				}
			}
			base = mtool->interval - mtool->offset;
		}
		break;
		case moving_average_type_cma:
			for (row = 1; row <= height; row++) {
				GnmExpr const *expr_offset;

				*delta_mover = row;

				expr_offset = analysis_tool_moving_average_funcall5
					 (fd_offset, expr_input, y, x, delta_y, delta_x);

				dao_set_cell_expr (dao, col, row,
						   gnm_expr_new_funcall1 (fd_average, expr_offset));
			}
			base = 0;
			break;
		case moving_average_type_wma:
		{
			GnmExpr const *expr_divisor = gnm_expr_new_constant
				(value_new_int ((mtool->interval * (mtool->interval + 1))/2));
			int *w = g_new (int, (mtool->interval + 1));
			int i;

			for (i = 0; i < mtool->interval; i++)
				w[i] = i+1;
			w[mtool->interval] = 0;

			delta_x = 0;
			delta_y= 0;
			(*delta_mover) = 1;
			(*mover) = 1 - mtool->interval;
			for (row = 1; row <= height; row++, (*mover)++) {
				if ((*mover >= 0) && (*mover < height - mtool->interval + 1)) {
					GnmExpr const *expr_sum;

					expr_sum = analysis_tool_moving_average_weighted_av
						(fd_sum, fd_index, expr_input, y+1, x+1, delta_y, delta_x, w);

					dao_set_cell_expr (dao, col, row,
							   gnm_expr_new_binary
							   (expr_sum,
							    GNM_EXPR_OP_DIV,
							    gnm_expr_copy (expr_divisor)));
				} else
					dao_set_cell_na (dao, col, row);
			}
			g_free (w);
			gnm_expr_free (expr_divisor);
			base =  mtool->interval - 1;
			delta_x = 1;
			delta_y= 1;
		}
		break;
		case moving_average_type_spencer_ma:
		{
			GnmExpr const *expr_divisor = gnm_expr_new_constant
				(value_new_int (-3-6-5+3+21+45+67+74+67+46+21+3-5-6-3));
			static const int w[] = {-3, -6, -5, 3, 21, 45, 67, 74, 67, 46, 21, 3, -5, -6, -3, 0};

			delta_x = 0;
			delta_y= 0;
			(*delta_mover) = 1;
			(*mover) = 1 - mtool->interval + mtool->offset;
			for (row = 1; row <= height; row++, (*mover)++) {
				if ((*mover >= 0) && (*mover < height - mtool->interval + 1)) {
					GnmExpr const *expr_sum;

					expr_sum = analysis_tool_moving_average_weighted_av
						(fd_sum, fd_index, expr_input, y+1, x+1, delta_y, delta_x, w);

					dao_set_cell_expr (dao, col, row,
							   gnm_expr_new_binary
							   (expr_sum,
							    GNM_EXPR_OP_DIV,
							    gnm_expr_copy (expr_divisor)));
				} else
					dao_set_cell_na (dao, col, row);
			}
			gnm_expr_free (expr_divisor);
			base =  mtool->interval - mtool->offset - 1;
			delta_x = 1;
			delta_y= 1;
		}
		break;
		default:
			(*delta_mover) = mtool->interval;
			(*mover) = 1 - mtool->interval + mtool->offset;
			for (row = 1; row <= height; row++, (*mover)++) {
				if ((*mover >= 0) && (*mover < height - mtool->interval + 1)) {
					GnmExpr const *expr_offset;

					expr_offset = analysis_tool_moving_average_funcall5
						(fd_offset, expr_input, y, x, delta_y, delta_x);
					dao_set_cell_expr (dao, col, row,
							   gnm_expr_new_funcall1 (fd_average, expr_offset));
				} else
					dao_set_cell_na (dao, col, row);
			}
			base =  mtool->interval - mtool->offset - 1;
			break;
		}

		if (mtool->std_error_flag) {
			col++;
			dao_set_italic (dao, col, 0, col, 0);
			dao_set_cell (dao, col, 0, _("Standard Error"));

			(*mover) = base;
			for (row = 1; row <= height; row++) {
				if (row > base && row <= height - mtool->offset && (row - base - mtool->df) > 0) {
					GnmExpr const *expr_offset;

					if (gtool->base.group_by == GNM_TOOL_GROUPED_BY_ROW)
						delta_x = row - base;
					else
						delta_y = row - base;

					expr_offset = analysis_tool_moving_average_funcall5
						(fd_offset, expr_input, y, x, delta_y, delta_x);
					dao_set_cell_expr (dao, col, row,
							   gnm_expr_new_funcall1
							   (fd_sqrt,
							    gnm_expr_new_binary
							    (gnm_expr_new_funcall2
							     (fd_sumxmy2,
							      expr_offset,
							      make_rangeref (-1, - row + base + 1, -1, 0)),
							     GNM_EXPR_OP_DIV,
							     gnm_expr_new_constant (value_new_int
										    (row - base - mtool->df)))));
				} else
					dao_set_cell_na (dao, col, row);
			}
		}

		gnm_expr_free (expr_input);
	}

	if (so != NULL)
		dao_set_sheet_object (dao, 0, 1, so);

	if (fd_index != NULL)
		gnm_func_dec_usage (fd_index);
	if (fd_sqrt != NULL)
		gnm_func_dec_usage (fd_sqrt);
	if (fd_sumxmy2 != NULL)
		gnm_func_dec_usage (fd_sumxmy2);
	if (fd_sum != NULL)
		gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_average);
	gnm_func_dec_usage (fd_offset);

	dao_redraw_respan (dao);

	return FALSE;
}

static void
gnm_moving_average_tool_class_init (GnmMovingAverageToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->set_property = gnm_moving_average_tool_set_property;
	gobject_class->get_property = gnm_moving_average_tool_get_property;

	at_class->update_dao = gnm_moving_average_tool_update_dao;
	at_class->update_descriptor = gnm_moving_average_tool_update_descriptor;
	at_class->prepare_output_range = gnm_moving_average_tool_prepare_output_range;
	at_class->format_output_range = gnm_moving_average_tool_format_output_range;
	at_class->perform_calc = gnm_moving_average_tool_perform_calc;

	g_object_class_install_property (gobject_class,
		MOVING_AVERAGE_PROP_INTERVAL,
		g_param_spec_int ("interval", NULL, NULL,
			1, G_MAXINT, 1, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		MOVING_AVERAGE_PROP_STD_ERROR_FLAG,
		g_param_spec_int ("std-error-flag", NULL, NULL,
			0, 1, 0, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		MOVING_AVERAGE_PROP_DF,
		g_param_spec_int ("df", NULL, NULL,
			0, G_MAXINT, 0, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		MOVING_AVERAGE_PROP_OFFSET,
		g_param_spec_int ("offset", NULL, NULL,
			0, G_MAXINT, 0, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		MOVING_AVERAGE_PROP_SHOW_GRAPH,
		g_param_spec_boolean ("show-graph", NULL, NULL,
			FALSE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		MOVING_AVERAGE_PROP_MA_TYPE,
		g_param_spec_int ("ma-type", NULL, NULL,
			0, 10, 0, G_PARAM_READWRITE));
}

GnmAnalysisTool *
gnm_moving_average_tool_new (void)
{
	return g_object_new (GNM_TYPE_MOVING_AVERAGE_TOOL, NULL);
}

/********************************************************************/

/************* Fourier Analysis Tool **************************************
 *
 * This tool performes a fast fourier transform calculating the fourier
 * transform as defined in Weaver: Theory of dis and cont Fouriere Analysis
 *
 *
 **/


G_DEFINE_TYPE (GnmFourierTool, gnm_fourier_tool, GNM_TYPE_GENERIC_ANALYSIS_TOOL)

enum {
	FOURIER_PROP_0,
	FOURIER_PROP_INVERSE
};

static void
gnm_fourier_tool_set_property (GObject *object, guint property_id,
			       GValue const *value, GParamSpec *pspec)
{
	GnmFourierTool *tool = GNM_FOURIER_TOOL (object);

	switch (property_id) {
	case FOURIER_PROP_INVERSE:
		tool->inverse = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_fourier_tool_get_property (GObject *object, guint property_id,
			       GValue *value, GParamSpec *pspec)
{
	GnmFourierTool *tool = GNM_FOURIER_TOOL (object);

	switch (property_id) {
	case FOURIER_PROP_INVERSE:
		g_value_set_boolean (value, tool->inverse);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_fourier_tool_init (GnmFourierTool *tool)
{
	tool->inverse = FALSE;
}

static gboolean
gnm_fourier_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmGenericAnalysisTool *gtool = GNM_GENERIC_ANALYSIS_TOOL (tool);
	analysis_tool_prepare_input_range (gtool);
	if (!analysis_tool_check_input_homogeneity (gtool)) {
		gtool->base.err = gtool->base.group_by + 1;
		return TRUE;
	}
	dao_adjust (dao, 1, analysis_tool_calc_length (gtool));
	return FALSE;
}

static char *
gnm_fourier_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Fourier Series (%s)"));
}

static gboolean
gnm_fourier_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	dao_prepare_output (wbc, dao, _("Fourier Series"));
	return FALSE;
}

static gboolean
gnm_fourier_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	return dao_format_output (wbc, dao, _("Fourier Series"));
}

static gboolean
gnm_fourier_tool_perform_calc (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	GnmFourierTool *ftool = GNM_FOURIER_TOOL (tool);
	GnmGenericAnalysisTool *gtool = &ftool->parent;
	GSList *data = gtool->base.input;
	int col = 0;
	GnmFunc *fd_fourier = gnm_func_get_and_use ("FOURIER");

	dao_set_merge (dao, 0, 0, 1, 0);
	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell (dao, 0, 0, ftool->inverse ? _("Inverse Fourier Transform")
		      : _("Fourier Transform"));

	for (; data; data = data->next, col++) {
		GnmValue *val_org = value_dup (data->data);
		GnmExpr const *expr_fourier;
		int rows, n;

		dao_set_italic (dao, 0, 1, 1, 2);
		set_cell_text_row (dao, 0, 2, _("/Real"
						"/Imaginary"));
		dao_set_merge (dao, 0, 1, 1, 1);
		analysis_tools_write_label (gtool, val_org, dao, 0, 1, col + 1);

		n = (val_org->v_range.cell.b.row - val_org->v_range.cell.a.row + 1) *
			(val_org->v_range.cell.b.col - val_org->v_range.cell.a.col + 1);
		rows = 1;
		while (rows < n)
			rows *= 2;

		expr_fourier = gnm_expr_new_funcall3
			(fd_fourier,
			 gnm_expr_new_constant (val_org),
			 gnm_expr_new_constant (value_new_bool (ftool->inverse)),
			 gnm_expr_new_constant (value_new_bool (TRUE)));

		dao_set_array_expr (dao, 0, 3, 2, rows, expr_fourier);

		dao->offset_col += 2;
	}

	gnm_func_dec_usage (fd_fourier);

	dao_redraw_respan (dao);

	return FALSE;
}

static void
gnm_fourier_tool_class_init (GnmFourierToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->set_property = gnm_fourier_tool_set_property;
	gobject_class->get_property = gnm_fourier_tool_get_property;

	at_class->update_dao = gnm_fourier_tool_update_dao;
	at_class->update_descriptor = gnm_fourier_tool_update_descriptor;
	at_class->prepare_output_range = gnm_fourier_tool_prepare_output_range;
	at_class->format_output_range = gnm_fourier_tool_format_output_range;
	at_class->perform_calc = gnm_fourier_tool_perform_calc;

	g_object_class_install_property (gobject_class,
		FOURIER_PROP_INVERSE,
		g_param_spec_boolean ("inverse", NULL, NULL,
			FALSE, G_PARAM_READWRITE));
}

GnmAnalysisTool *
gnm_fourier_tool_new (void)
{
	return g_object_new (GNM_TYPE_FOURIER_TOOL, NULL);
}

/********************************************************************/

/************* Sampling Tool *********************************************
 *
 * Sampling tool takes a sample from a given data set.  Sample can be
 * a random sample where a given number of data points are selected
 * randomly from the data set.  The sample can also be a periodic
 * sample where, for example, every fourth data element is selected to
 * the sample.  The results are given in a table which can be printed
 * out in a new sheet, in a new workbook, or simply into an existing
 * sheet.
 *
 **/

G_DEFINE_TYPE (GnmSamplingTool, gnm_sampling_tool, GNM_TYPE_GENERIC_ANALYSIS_TOOL)

enum {
	SAMPLING_PROP_0,
	SAMPLING_PROP_PERIODIC,
	SAMPLING_PROP_ROW_MAJOR,
	SAMPLING_PROP_OFFSET,
	SAMPLING_PROP_SIZE,
	SAMPLING_PROP_PERIOD,
	SAMPLING_PROP_NUMBER
};

static void
gnm_sampling_tool_set_property (GObject *object, guint property_id,
				GValue const *value, GParamSpec *pspec)
{
	GnmSamplingTool *tool = GNM_SAMPLING_TOOL (object);

	switch (property_id) {
	case SAMPLING_PROP_PERIODIC:
		tool->periodic = g_value_get_boolean (value);
		break;
	case SAMPLING_PROP_ROW_MAJOR:
		tool->row_major = g_value_get_boolean (value);
		break;
	case SAMPLING_PROP_OFFSET:
		tool->offset = g_value_get_uint (value);
		break;
	case SAMPLING_PROP_SIZE:
		tool->size = g_value_get_uint (value);
		break;
	case SAMPLING_PROP_PERIOD:
		tool->period = g_value_get_uint (value);
		break;
	case SAMPLING_PROP_NUMBER:
		tool->number = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_sampling_tool_get_property (GObject *object, guint property_id,
				GValue *value, GParamSpec *pspec)
{
	GnmSamplingTool *tool = GNM_SAMPLING_TOOL (object);

	switch (property_id) {
	case SAMPLING_PROP_PERIODIC:
		g_value_set_boolean (value, tool->periodic);
		break;
	case SAMPLING_PROP_ROW_MAJOR:
		g_value_set_boolean (value, tool->row_major);
		break;
	case SAMPLING_PROP_OFFSET:
		g_value_set_uint (value, tool->offset);
		break;
	case SAMPLING_PROP_SIZE:
		g_value_set_uint (value, tool->size);
		break;
	case SAMPLING_PROP_PERIOD:
		g_value_set_uint (value, tool->period);
		break;
	case SAMPLING_PROP_NUMBER:
		g_value_set_uint (value, tool->number);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_sampling_tool_init (GnmSamplingTool *tool)
{
	tool->periodic = FALSE;
	tool->row_major = FALSE;
	tool->offset = 0;
	tool->size = 0;
	tool->period = 0;
	tool->number = 0;
}

static gboolean
gnm_sampling_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmGenericAnalysisTool *gtool = GNM_GENERIC_ANALYSIS_TOOL (tool);
	analysis_tool_prepare_input_range (gtool);
	if (!analysis_tool_check_input_homogeneity (gtool)) {
		gtool->base.err = gtool->base.group_by + 1;
		return TRUE;
	}
	dao_adjust (dao, 1, analysis_tool_calc_length (gtool));
	return FALSE;
}

static char *
gnm_sampling_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Sampling (%s)"));
}

static gboolean
gnm_sampling_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	dao_prepare_output (wbc, dao, _("Sampling"));
	return FALSE;
}

static gboolean
gnm_sampling_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	return dao_format_output (wbc, dao, _("Sampling"));
}

static gboolean
gnm_sampling_tool_perform_calc (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	GnmSamplingTool *stool = GNM_SAMPLING_TOOL (tool);
	GnmGenericAnalysisTool *gtool = &stool->parent;
	GSList *l;
	gint col = 0;
	guint ct;
	GnmFunc *fd_index = NULL;
	GnmFunc *fd_randdiscrete = NULL;
	gint source;

	if (gtool->base.labels || stool->periodic) {
		fd_index = gnm_func_get_and_use ("INDEX");
	}
	if (!stool->periodic) {
		fd_randdiscrete = gnm_func_get_and_use ("RANDDISCRETE");
	}

	for (l = gtool->base.input, source = 1; l; l = l->next, source++) {
		GnmValue *val = value_dup ((GnmValue *)l->data);
		GnmValue *val_c = NULL;
		GnmExpr const *expr_title = NULL;
		GnmExpr const *expr_input = NULL;
		char const *format = NULL;
		guint offset = stool->periodic ? ((stool->offset == 0) ? stool->period : stool->offset): 0;
		GnmEvalPos ep;

		eval_pos_init_sheet (&ep, val->v_range.cell.a.sheet);

		dao_set_italic (dao, col, 0, col + stool->number - 1, 0);

		if (gtool->base.labels) {
			val_c = value_dup (val);
			switch (gtool->base.group_by) {
			case GNM_TOOL_GROUPED_BY_ROW:
				val->v_range.cell.a.col++;
				break;
			case GNM_TOOL_GROUPED_BY_COL:
				val->v_range.cell.a.row++;
				break;
			default:
				offset++;
				break;
			}
			expr_title = gnm_expr_new_funcall1 (fd_index,
							    gnm_expr_new_constant (val_c));
			for (ct = 0; ct < stool->number; ct++)
				dao_set_cell_expr (dao, col+ct, 0, gnm_expr_copy (expr_title));
			gnm_expr_free (expr_title);
		} else {
			switch (gtool->base.group_by) {
			case GNM_TOOL_GROUPED_BY_ROW:
				format = _("Row %d");
				break;
			case GNM_TOOL_GROUPED_BY_COL:
				format = _("Column %d");
				break;
			default:
				format = _("Area %d");
				break;
			}
			for (ct = 0; ct < stool->number; ct++)
				dao_set_cell_printf (dao, col+ct, 0, format, source);
		}

		expr_input = gnm_expr_new_constant (value_dup (val));


		if (stool->periodic) {
			guint i;
			gint height = value_area_get_height (val, &ep);
			gint width = value_area_get_width (val, &ep);
			GnmExpr const *expr_period;

			for (i=0; i < stool->size; i++, offset += stool->period) {
				gint x_offset;
				gint y_offset;

				if (stool->row_major) {
					y_offset = (offset - 1)/width + 1;
					x_offset = offset - (y_offset - 1) * width;
				} else {
					x_offset = (offset - 1)/height + 1;
					y_offset = offset - (x_offset - 1) * height;
				}

				expr_period = gnm_expr_new_funcall3
					(fd_index, gnm_expr_copy (expr_input),
					 gnm_expr_new_constant (value_new_int (y_offset)),
					 gnm_expr_new_constant (value_new_int (x_offset)));

				for (ct = 0; ct < stool->number; ct += 2)
					dao_set_cell_expr (dao, col + ct, i + 1,
							   gnm_expr_copy (expr_period));
				gnm_expr_free (expr_period);

				if (stool->number > 1) {
					if (!stool->row_major) {
						y_offset = (offset - 1)/width + 1;
						x_offset = offset - (y_offset - 1) * width;
					} else {
						x_offset = (offset - 1)/height + 1;
						y_offset = offset - (x_offset - 1) * height;
					}

					expr_period = gnm_expr_new_funcall3
						(fd_index, gnm_expr_copy (expr_input),
						 gnm_expr_new_constant (value_new_int (y_offset)),
						 gnm_expr_new_constant (value_new_int (x_offset)));

					for (ct = 1; ct < stool->number; ct += 2)
						dao_set_cell_expr (dao, col + ct, i + 1,
								   gnm_expr_copy (expr_period));
					gnm_expr_free (expr_period);

				}
			}
			col += stool->number;
		} else {
			GnmExpr const *expr_random;
			guint i;

			expr_random = gnm_expr_new_funcall1 (fd_randdiscrete,
							     gnm_expr_copy (expr_input));

			for (ct = 0; ct < stool->number; ct++, col++)
				for (i=0; i < stool->size; i++)
					dao_set_cell_expr (dao, col, i + 1,
							   gnm_expr_copy (expr_random));
			gnm_expr_free (expr_random);
		}

		value_release (val);
		gnm_expr_free (expr_input);

	}

	if (fd_index != NULL)
		gnm_func_dec_usage (fd_index);
	if (fd_randdiscrete != NULL)
		gnm_func_dec_usage (fd_randdiscrete);

	dao_redraw_respan (dao);

	return FALSE;
}

static void
gnm_sampling_tool_class_init (GnmSamplingToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->set_property = gnm_sampling_tool_set_property;
	gobject_class->get_property = gnm_sampling_tool_get_property;

	at_class->update_dao = gnm_sampling_tool_update_dao;
	at_class->update_descriptor = gnm_sampling_tool_update_descriptor;
	at_class->prepare_output_range = gnm_sampling_tool_prepare_output_range;
	at_class->format_output_range = gnm_sampling_tool_format_output_range;
	at_class->perform_calc = gnm_sampling_tool_perform_calc;

	g_object_class_install_property (gobject_class,
		SAMPLING_PROP_PERIODIC,
		g_param_spec_boolean ("periodic", NULL, NULL,
			FALSE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		SAMPLING_PROP_ROW_MAJOR,
		g_param_spec_boolean ("row-major", NULL, NULL,
			FALSE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		SAMPLING_PROP_OFFSET,
		g_param_spec_uint ("offset", NULL, NULL,
			0, G_MAXUINT, 0, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		SAMPLING_PROP_SIZE,
		g_param_spec_uint ("size", NULL, NULL,
			0, G_MAXUINT, 0, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		SAMPLING_PROP_PERIOD,
		g_param_spec_uint ("period", NULL, NULL,
			0, G_MAXUINT, 0, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		SAMPLING_PROP_NUMBER,
		g_param_spec_uint ("number", NULL, NULL,
			0, G_MAXUINT, 0, G_PARAM_READWRITE));
}

GnmAnalysisTool *
gnm_sampling_tool_new (void)
{
	return g_object_new (GNM_TYPE_SAMPLING_TOOL, NULL);
}

/********************************************************************/

/************* Rank and Percentile Tool ************************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

G_DEFINE_TYPE (GnmRankingTool, gnm_ranking_tool, GNM_TYPE_GENERIC_ANALYSIS_TOOL)

enum {
	RANKING_PROP_0,
	RANKING_PROP_AV_TIES
};

static void
gnm_ranking_tool_set_property (GObject *object, guint property_id,
			       GValue const *value, GParamSpec *pspec)
{
	GnmRankingTool *tool = GNM_RANKING_TOOL (object);

	switch (property_id) {
	case RANKING_PROP_AV_TIES:
		tool->av_ties = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_ranking_tool_get_property (GObject *object, guint property_id,
			       GValue *value, GParamSpec *pspec)
{
	GnmRankingTool *tool = GNM_RANKING_TOOL (object);

	switch (property_id) {
	case RANKING_PROP_AV_TIES:
		g_value_set_boolean (value, tool->av_ties);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_ranking_tool_init (GnmRankingTool *tool)
{
	tool->av_ties = FALSE;
}

static gboolean
gnm_ranking_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmGenericAnalysisTool *gtool = GNM_GENERIC_ANALYSIS_TOOL (tool);
	analysis_tool_prepare_input_range (gtool);
	if (!analysis_tool_check_input_homogeneity (gtool)) {
		gtool->base.err = gtool->base.group_by + 1;
		return TRUE;
	}
	dao_adjust (dao, 3 * g_slist_length (gtool->base.input), analysis_tool_calc_length (gtool));
	return FALSE;
}

static char *
gnm_ranking_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Ranks (%s)"));
}

static gboolean
gnm_ranking_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	dao_prepare_output (wbc, dao, _("Ranks"));
	return FALSE;
}

static gboolean
gnm_ranking_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	return dao_format_output (wbc, dao, _("Ranks"));
}

static gboolean
gnm_ranking_tool_perform_calc (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	GnmRankingTool *rtool = GNM_RANKING_TOOL (tool);
	GnmGenericAnalysisTool *gtool = &rtool->parent;
	GSList *data = gtool->base.input;
	int col = 0;
	GnmFunc *fd_large = gnm_func_get_and_use ("LARGE");
	GnmFunc *fd_row = gnm_func_get_and_use ("ROW");
	GnmFunc *fd_rank = gnm_func_get_and_use ("RANK");
	GnmFunc *fd_match = gnm_func_get_and_use ("MATCH");
	GnmFunc *fd_percentrank = gnm_func_get_and_use ("PERCENTRANK");

	dao_set_merge (dao, 0, 0, 1, 0);
	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell (dao, 0, 0, _("Ranks & Percentiles"));

	for (; data; data = data->next, col++) {
		GnmValue *val_org = value_dup (data->data);
		GnmExpr const *expr_large;
		GnmExpr const *expr_rank;
		GnmExpr const *expr_position;
		GnmExpr const *expr_percentile;
		int rows, i;

		dao_set_italic (dao, 0, 1, 3, 1);
		dao_set_cell (dao, 0, 1, _("Point"));
		dao_set_cell (dao, 2, 1, _("Rank"));
		dao_set_cell (dao, 3, 1, _("Percentile Rank"));
		analysis_tools_write_label (gtool, val_org, dao, 1, 1, col + 1);

		rows = (val_org->v_range.cell.b.row - val_org->v_range.cell.a.row + 1) *
			(val_org->v_range.cell.b.col - val_org->v_range.cell.a.col + 1);

		expr_large = gnm_expr_new_funcall2
			(fd_large, gnm_expr_new_constant (value_dup (val_org)),
			 gnm_expr_new_binary (gnm_expr_new_binary
					      (gnm_expr_new_funcall (fd_row, NULL),
					       GNM_EXPR_OP_SUB,
					       gnm_expr_new_funcall1
					       (fd_row, dao_get_cellref (dao, 1, 2))),
					      GNM_EXPR_OP_ADD,
					      gnm_expr_new_constant (value_new_int (1))));
		dao_set_array_expr (dao, 1, 2, 1, rows, gnm_expr_copy (expr_large));

		/* If there are ties the following will only give us the first occurrence... */
		expr_position = gnm_expr_new_funcall3 (fd_match, expr_large,
						       gnm_expr_new_constant (value_dup (val_org)),
						       gnm_expr_new_constant (value_new_int (0)));

		dao_set_array_expr (dao, 0, 2, 1, rows, expr_position);

		expr_rank = gnm_expr_new_funcall2 (fd_rank,
						   make_cellref (-1,0),
						   gnm_expr_new_constant (value_dup (val_org)));
		if (rtool->av_ties) {
			GnmExpr const *expr_rank_lower;
			GnmExpr const *expr_rows_p_one;
			GnmExpr const *expr_rows;
			GnmFunc *fd_count = gnm_func_get_and_use ("COUNT");

			expr_rows = gnm_expr_new_funcall1
				(fd_count, gnm_expr_new_constant (value_dup (val_org)));
			expr_rows_p_one = gnm_expr_new_binary
				(expr_rows,
				 GNM_EXPR_OP_ADD,
				 gnm_expr_new_constant (value_new_int (1)));
			expr_rank_lower = gnm_expr_new_funcall3
				(fd_rank,
				 make_cellref (-1,0),
				 gnm_expr_new_constant (value_dup (val_org)),
				 gnm_expr_new_constant (value_new_int (1)));
			expr_rank = gnm_expr_new_binary
				(gnm_expr_new_binary
				 (gnm_expr_new_binary (expr_rank, GNM_EXPR_OP_SUB, expr_rank_lower),
				  GNM_EXPR_OP_ADD, expr_rows_p_one),
				 GNM_EXPR_OP_DIV,
				 gnm_expr_new_constant (value_new_int (2)));

			gnm_func_dec_usage (fd_count);
		}
		expr_percentile = gnm_expr_new_funcall3 (fd_percentrank,
							 gnm_expr_new_constant (value_dup (val_org)),
							 make_cellref (-2,0),
							 gnm_expr_new_constant (value_new_int (10)));

		dao_set_format_percent (dao, 3, 2, 3, 1 + rows);
		for (i = 2; i < rows + 2; i++) {
			dao_set_cell_expr ( dao, 2, i, gnm_expr_copy (expr_rank));
			dao_set_cell_expr ( dao, 3, i, gnm_expr_copy (expr_percentile));
		}


		dao->offset_col += 4;
		value_release (val_org);
		gnm_expr_free (expr_rank);
		gnm_expr_free (expr_percentile);
	}

	gnm_func_dec_usage (fd_large);
	gnm_func_dec_usage (fd_row);
	gnm_func_dec_usage (fd_rank);
	gnm_func_dec_usage (fd_match);
	gnm_func_dec_usage (fd_percentrank);

	dao_redraw_respan (dao);

	return FALSE;
}

static void
gnm_ranking_tool_class_init (GnmRankingToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->set_property = gnm_ranking_tool_set_property;
	gobject_class->get_property = gnm_ranking_tool_get_property;

	at_class->update_dao = gnm_ranking_tool_update_dao;
	at_class->update_descriptor = gnm_ranking_tool_update_descriptor;
	at_class->prepare_output_range = gnm_ranking_tool_prepare_output_range;
	at_class->format_output_range = gnm_ranking_tool_format_output_range;
	at_class->perform_calc = gnm_ranking_tool_perform_calc;

	g_object_class_install_property (gobject_class,
		RANKING_PROP_AV_TIES,
		g_param_spec_boolean ("av-ties", NULL, NULL,
			FALSE, G_PARAM_READWRITE));
}

GnmAnalysisTool *
gnm_ranking_tool_new (void)
{
	return g_object_new (GNM_TYPE_RANKING_TOOL, NULL);
}

/********************************************************************/

G_DEFINE_TYPE (GnmGenericBAnalysisTool, gnm_generic_b_analysis_tool, GNM_ANALYSIS_TOOL_TYPE)

static void
gnm_generic_b_analysis_tool_init (GnmGenericBAnalysisTool *tool)
{
	tool->base.err = analysis_tools_noerr;
	tool->base.range_1 = NULL;
	tool->base.range_2 = NULL;
	tool->base.labels = FALSE;
	tool->base.alpha = 0.05;
}

enum {
	GENERIC_B_PROP_0,
	GENERIC_B_PROP_LABELS,
	GENERIC_B_PROP_ALPHA
};

static void
gnm_generic_b_analysis_tool_set_property (GObject      *obj,
					  guint         property_id,
					  GValue const *value,
					  GParamSpec   *pspec)
{
	GnmGenericBAnalysisTool *tool = GNM_GENERIC_B_ANALYSIS_TOOL (obj);

	switch (property_id) {
	case GENERIC_B_PROP_LABELS:
		tool->base.labels = g_value_get_boolean (value);
		break;
	case GENERIC_B_PROP_ALPHA:
		tool->base.alpha = g_value_get_double (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
		break;
	}
}

static void
gnm_generic_b_analysis_tool_get_property (GObject    *obj,
					  guint       property_id,
					  GValue     *value,
					  GParamSpec *pspec)
{
	GnmGenericBAnalysisTool *tool = GNM_GENERIC_B_ANALYSIS_TOOL (obj);

	switch (property_id) {
	case GENERIC_B_PROP_LABELS:
		g_value_set_boolean (value, tool->base.labels);
		break;
	case GENERIC_B_PROP_ALPHA:
		g_value_set_double (value, tool->base.alpha);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
		break;
	}
}

static void
gnm_generic_b_analysis_tool_finalize (GObject *obj)
{
	GnmGenericBAnalysisTool *tool = GNM_GENERIC_B_ANALYSIS_TOOL (obj);
	value_release (tool->base.range_1);
	value_release (tool->base.range_2);
	G_OBJECT_CLASS (gnm_generic_b_analysis_tool_parent_class)->finalize (obj);
}

static void
gnm_generic_b_analysis_tool_class_init (GnmGenericBAnalysisToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->set_property = gnm_generic_b_analysis_tool_set_property;
	gobject_class->get_property = gnm_generic_b_analysis_tool_get_property;
	gobject_class->finalize = gnm_generic_b_analysis_tool_finalize;

	g_object_class_install_property (gobject_class,
		GENERIC_B_PROP_LABELS,
		g_param_spec_boolean ("labels", NULL, NULL,
				      FALSE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		GENERIC_B_PROP_ALPHA,
		g_param_spec_double ("alpha", NULL, NULL,
				     0.0, 1.0, 0.05, G_PARAM_READWRITE));
}

/********************************************************************/

/**
 * make_cellref:
 * @dx: relative column offset
 * @dy: relative row offset
 *
 * Returns: (transfer full): a new relative cell reference expression.
 **/
const GnmExpr *
make_cellref (int dx, int dy)
{
	GnmCellRef r;
	r.sheet = NULL;
	r.col = dx;
	r.col_relative = TRUE;
	r.row = dy;
	r.row_relative = TRUE;
	return gnm_expr_new_cellref (&r);
}

/**
 * make_rangeref:
 * @dx0: start relative column offset
 * @dy0: start relative row offset
 * @dx1: end relative column offset
 * @dy1: end relative row offset
 *
 * Returns: (transfer full): a new relative range reference expression.
 **/
const GnmExpr *
make_rangeref (int dx0, int dy0, int dx1, int dy1)
{
	GnmCellRef a, b;
	GnmValue *val;

	a.sheet = NULL;
	a.col = dx0;
	a.col_relative = TRUE;
	a.row = dy0;
	a.row_relative = TRUE;
	b.sheet = NULL;
	b.col = dx1;
	b.col_relative = TRUE;
	b.row = dy1;
	b.row_relative = TRUE;

	val = value_new_cellrange_unsafe (&a, &b);
	return gnm_expr_new_constant (val);
}


/**
 * analysis_tools_adjust_areas:
 * @range:
 *
 * In-place updates a cell range to be absolute.  This is needed so it can
 * be used in expressions without changing meaning.
 */
void
analysis_tools_adjust_areas (GnmValue *range)
{
	if (range == NULL || !VALUE_IS_CELLRANGE (range)) {
		return;
	}

	range->v_range.cell.a.col_relative = FALSE;
	range->v_range.cell.a.row_relative = FALSE;
	range->v_range.cell.b.col_relative = FALSE;
	range->v_range.cell.b.row_relative = FALSE;
}

/*
 *  analysis_tools_remove_label:
 *
 */
void
analysis_tools_remove_label (GnmValue *val,
			     gboolean labels, gnm_tool_group_by_t group_by)
{
	if (labels) {
		switch (group_by) {
		case GNM_TOOL_GROUPED_BY_ROW:
			val->v_range.cell.a.col++;
			break;
		case GNM_TOOL_GROUPED_BY_COL:
		case GNM_TOOL_GROUPED_BY_BIN:
		case GNM_TOOL_GROUPED_BY_AREA:
		default:
			val->v_range.cell.a.row++;
			break;
		}
	}
}



/**
 * analysis_tools_write_label:
 * @gtool: #GnmGenericAnalysisTool
 * @val: range to extract label from
 * @dao: data_analysis_output_t, where to write to
 * @x: output col number
 * @y: output row number
 * @i: default col/row number
 */
void
analysis_tools_write_label (GnmGenericAnalysisTool *gtool,
			    GnmValue *val, data_analysis_output_t *dao,
			    int x, int y, int i)
{
	char const *format = NULL;

	if (gtool->base.labels) {
		GnmValue *label = value_dup (val);

		label->v_range.cell.b = label->v_range.cell.a;
		dao_set_cell_expr (dao, x, y, gnm_expr_new_constant (label));
		analysis_tools_remove_label (val, gtool->base.labels, gtool->base.group_by);
	} else {
		switch (gtool->base.group_by) {
		case GNM_TOOL_GROUPED_BY_ROW:
			format = _("Row %i");
			break;
		case GNM_TOOL_GROUPED_BY_COL:
			format = _("Column %i");
			break;
		case GNM_TOOL_GROUPED_BY_BIN:
			format = _("Bin %i");
			break;
		case GNM_TOOL_GROUPED_BY_AREA:
		default:
			format = _("Area %i");
			break;
		}

		dao_set_cell_printf (dao, x, y, format, i);
	}
}

/**
 * analysis_tools_write_variable_label:
 * @val: (inout): Range to extract label from
 * @dao: #data_analysis_output_t
 * @x: output col number
 * @y: output row number
 * @labels: boolean
 * @i: default col/row number
 *
 * Writes a variable (data series) label and advances @val to the next
 * row or column.
 **/
void
analysis_tools_write_variable_label (GnmValue *val, data_analysis_output_t *dao,
				     int x, int y, gboolean labels, int i)
{
	analysis_tools_adjust_areas (val);

	if (labels) {
		GnmValue *label = value_dup (val);

		label->v_range.cell.b = label->v_range.cell.a;
		dao_set_cell_expr (dao, x, y, gnm_expr_new_constant (label));

		if ((val->v_range.cell.b.col - val->v_range.cell.a.col) <
		    (val->v_range.cell.b.row - val->v_range.cell.a.row))
			val->v_range.cell.a.row++;
		else
			val->v_range.cell.a.col++;
	} else {
		dao_set_cell_printf (dao, x, y,  _("Variable %i"), i);
	}
}



static void
cb_cut_into_cols (gpointer data, gpointer user_data)
{
	GnmValue *range = (GnmValue *)data;
	GnmValue *col_value;
	GSList **list_of_units = (GSList **) user_data;
	gint col;

	if (range == NULL) {
		return;
	}
	if (!VALUE_IS_CELLRANGE (range) ||
	    (range->v_range.cell.b.sheet != NULL &&
	     range->v_range.cell.b.sheet != range->v_range.cell.a.sheet)) {
		value_release (range);
		return;
	}

	analysis_tools_adjust_areas (data);

	if (range->v_range.cell.a.col == range->v_range.cell.b.col) {
		*list_of_units = g_slist_prepend (*list_of_units, range);
		return;
	}

	for (col = range->v_range.cell.a.col; col <= range->v_range.cell.b.col; col++) {
		col_value = value_dup (range);
		col_value->v_range.cell.a.col = col;
		col_value->v_range.cell.b.col = col;
		*list_of_units = g_slist_prepend (*list_of_units, col_value);
	}
	value_release (range);
	return;
}

/*
 *  cb_cut_into_rows:
 *  @data:
 *  @user_data:
 *
 */
static void
cb_cut_into_rows (gpointer data, gpointer user_data)
{
	GnmValue *range = (GnmValue *)data;
	GnmValue *row_value;
	GSList **list_of_units = (GSList **) user_data;
	gint row;

	if (range == NULL) {
		return;
	}
	if (!VALUE_IS_CELLRANGE (range) ||
	    (range->v_range.cell.b.sheet != NULL &&
	     range->v_range.cell.b.sheet != range->v_range.cell.a.sheet)) {
		value_release (range);
		return;
	}

	analysis_tools_adjust_areas (data);

	if (range->v_range.cell.a.row == range->v_range.cell.b.row) {
		*list_of_units = g_slist_prepend (*list_of_units, range);
		return;
	}

	for (row = range->v_range.cell.a.row; row <= range->v_range.cell.b.row; row++) {
		row_value = value_dup (range);
		row_value->v_range.cell.a.row = row;
		row_value->v_range.cell.b.row = row;
		*list_of_units = g_slist_prepend (*list_of_units, row_value);
	}
	value_release (range);
	return;
}


/**
 *  analysis_tool_prepare_input_range_full:
 *  @input_range: (inout) (element-type GnmRange) (transfer full):
 *  @group_by:
 */
void
analysis_tool_prepare_input_range_full (GSList **input_range, gnm_tool_group_by_t group_by)
{
	GSList *input_by_units = NULL;

	switch (group_by) {
	case GNM_TOOL_GROUPED_BY_ROW:
		g_slist_foreach (*input_range, cb_cut_into_rows, &input_by_units);
		g_slist_free (*input_range);
		*input_range = g_slist_reverse (input_by_units);
		return;
	case GNM_TOOL_GROUPED_BY_COL:
		g_slist_foreach (*input_range, cb_cut_into_cols, &input_by_units);
		g_slist_free (*input_range);
		*input_range = g_slist_reverse (input_by_units);
		return;
	case GNM_TOOL_GROUPED_BY_AREA:
	default:
		g_slist_foreach (*input_range, (GFunc)analysis_tools_adjust_areas, NULL);
		return;
	}
}

/**
 *  analysis_tool_prepare_input_range:
 *  @gtool: #GnmGenericAnalysisTool
 */
void
analysis_tool_prepare_input_range (GnmGenericAnalysisTool *gtool)
{
	analysis_tool_prepare_input_range_full (&gtool->base.input, gtool->base.group_by);
}

static size_t
calc_size (GnmValue const *range)
{
	// This feels half-baked.  What about relative positions?
	return ((range->v_range.cell.b.col - range->v_range.cell.a.col + 1) *
		(range->v_range.cell.b.row - range->v_range.cell.a.row + 1));
}

/**
 * analysis_tool_check_input_homogeneity:
 * @gtool: #GnmGenericAnalysisTool
 *
 * Check that all elements have the same size.
 *
 * Returns: %TRUE if sizes are uniform.
 */
gboolean
analysis_tool_check_input_homogeneity (GnmGenericAnalysisTool *gtool)
{
	size_t s0 = -1;
	for (GSList *l = gtool->base.input; l; l = l->next) {
		GnmValue const *v = l->data;
		if (!VALUE_IS_CELLRANGE (v))
			return FALSE;

		size_t s = calc_size (v);
		if (s0 == (size_t)-1)
			s0 = s;
		else if (s != s0)
			return FALSE;
	}

	return TRUE;
}


/***** Some general routines ***********************************************/

/**
 * set_cell_text_col:
 * @dao: #data_analysis_output_t
 * @col: col
 * @row: row
 * @text: text
 *
 * Set a column of text from a string like "/first/second/third" or "|foo|bar|baz".
 **/
void
set_cell_text_col (data_analysis_output_t *dao, int col, int row, const char *text)
{
	gboolean leave = FALSE;
	char *copy, *orig_copy;
	char sep = *text;
	if (sep == 0) return;

	copy = orig_copy = g_strdup (text + 1);
	while (!leave) {
		char *p = copy;
		while (*copy && *copy != sep)
			copy++;
		if (*copy)
			*copy++ = 0;
		else
			leave = TRUE;
		dao_set_cell_value (dao, col, row++, value_new_string (p));
	}
	g_free (orig_copy);
}


/**
 * set_cell_text_row:
 * @dao: #data_analysis_output_t
 * @col: col
 * @row: row
 * @text: text
 *
 * Set a row of text from a string like "/first/second/third" or "|foo|bar|baz".
 **/
void
set_cell_text_row (data_analysis_output_t *dao, int col, int row, const char *text)
{
	gboolean leave = 0;
	char *copy, *orig_copy;
	char sep = *text;
	if (sep == 0) return;

	copy = orig_copy = g_strdup (text + 1);
	while (!leave) {
		char *p = copy;
		while (*copy && *copy != sep)
			copy++;
		if (*copy)
			*copy++ = 0;
		else
			leave = TRUE;
		dao_set_cell_value (dao, col++, row, value_new_string (p));
	}
	g_free (orig_copy);
}

/**
 * analysis_tool_calc_length:
 * @gtool: #GnmGenericAnalysisTool
 *
 * Returns: the calculated length for the analysis tool.
 **/
int analysis_tool_calc_length (GnmGenericAnalysisTool *gtool)
{
	int           result = 1;
	GSList        *dataset;

	for (dataset = gtool->base.input; dataset; dataset = dataset->next) {
		GnmValue    *current = dataset->data;
		int      given_length;

		if (gtool->base.group_by == GNM_TOOL_GROUPED_BY_AREA) {
			given_length = (current->v_range.cell.b.row - current->v_range.cell.a.row + 1) *
				(current->v_range.cell.b.col - current->v_range.cell.a.col + 1);
		} else
			given_length = (gtool->base.group_by == GNM_TOOL_GROUPED_BY_COL) ?
				(current->v_range.cell.b.row - current->v_range.cell.a.row + 1) :
				(current->v_range.cell.b.col - current->v_range.cell.a.col + 1);
		if (given_length > result)
			result = given_length;
	}
	if (gtool->base.labels)
		result--;
	return result;
}


/**
 * analysis_tool_table:
 * @gtool: #GnmGenericAnalysisTool
 * @dao: data_analysis_output_t, where to write to
 * @title: title of the table
 * @functionname: name of the function
 * @full_table: boolean
 *
 * Returns: %TRUE if there is an error.
 **/
gboolean
analysis_tool_table (GnmGenericAnalysisTool *gtool, data_analysis_output_t *dao,
		     gchar const *title, gchar const *functionname,
		     gboolean full_table)
{
	GSList *inputdata, *inputexpr = NULL;
	guint col, row;
	GnmFunc *fd = gnm_func_get_and_use (functionname);

	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell_printf (dao, 0, 0, "%s", title);

	for (col = 1, inputdata = gtool->base.input; inputdata != NULL;
	     inputdata = inputdata->next, col++) {
		GnmValue *val = NULL;

		val = value_dup (inputdata->data);

		/* Label */
		dao_set_italic (dao, col, 0, col, 0);
		analysis_tools_write_label (gtool, val, dao, col, 0, col);

		inputexpr = g_slist_prepend (inputexpr,
					     (gpointer) gnm_expr_new_constant (val));
	}
	inputexpr = g_slist_reverse (inputexpr);

	for (row = 1, inputdata = gtool->base.input; inputdata != NULL;
	     inputdata = inputdata->next, row++) {
		GnmValue *val = value_dup (inputdata->data);
		GSList *colexprlist;

		/* Label */
		dao_set_italic (dao, 0, row, 0, row);
		analysis_tools_write_label (gtool, val, dao, 0, row, row);

		for (col = 1, colexprlist = inputexpr; colexprlist != NULL;
		     colexprlist = colexprlist->next, col++) {
			GnmExpr const *colexpr = colexprlist->data;

			if ((!full_table) && (col < row))
				continue;

			dao_set_cell_expr
				(dao, row, col,
				 gnm_expr_new_funcall2
				 (fd,
				  gnm_expr_new_constant (value_dup (val)),
				  gnm_expr_copy (colexpr)));
		}

		value_release (val);
	}

	g_slist_free_full (inputexpr, (GDestroyNotify)gnm_expr_free);
	if (fd) gnm_func_dec_usage (fd);

	dao_redraw_respan (dao);
	return FALSE;
}


gint
analysis_tools_calculate_xdim (GnmValue const *input, gnm_tool_group_by_t  group_by)
{
		GnmRange r;

		g_return_val_if_fail (input != NULL, 0);

		if (NULL == range_init_value (&r, input))
			return 0;

		if (group_by == GNM_TOOL_GROUPED_BY_ROW)
			return range_height (&r);

		return range_width (&r);
}

gint
analysis_tools_calculate_n_obs (GnmValue const *input, gnm_tool_group_by_t  group_by)
{
		GnmRange r;

		g_return_val_if_fail (input != NULL, 0);

		if (NULL == range_init_value (&r, input))
			return 0;

		if (group_by == GNM_TOOL_GROUPED_BY_ROW)
			return range_width (&r);

		return range_height (&r);
}
