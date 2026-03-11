/*
 * random-generator.c:
 *
 * Authors:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *   Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 * (C) Copyright 2002 by Andreas J. Guelzow  <aguelzow@taliesin.ca>
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
#include <tools/random-generator.h>

#include <gnm-random.h>
#include <rangefunc.h>
#include <value.h>
#include <cell.h>
#include <sheet.h>
#include <ranges.h>
#include <style.h>
#include <sheet-style.h>
#include <workbook.h>
#include <gnm-format.h>
#include <command-context.h>
#include <sheet-object-cell-comment.h>

#include <string.h>
#include <math.h>

#define PROGRESS_START int pro = 0;                   \
	double total = info->n_vars * info->count;    \
	go_cmd_context_progress_set (gcc, 0);         \
	go_cmd_context_progress_message_set (gcc, _("Generating Random Numbers..."))
#define PROGESS_RUN if ((++pro & 2047) == 0) {        \
        go_cmd_context_progress_set (gcc, pro/total); \
	while (gtk_events_pending ())                 \
	gtk_main_iteration_do (FALSE);                \
        }
#define PROGESS_END go_cmd_context_progress_set (gcc, 0); \
	go_cmd_context_progress_message_set (gcc, NULL)


/************* Random Number Generation Tool ******************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

static void
gnm_random_tool_discrete_clear (GnmRandomTool *tool)
{
	gint i;

	if (tool->discrete_values) {
		for (i = 0; i < tool->discrete_n; i++)
			if (tool->discrete_values[i])
				value_release (tool->discrete_values[i]);
		g_free (tool->discrete_values);
		tool->discrete_values = NULL;
	}
	g_free (tool->discrete_cumul_p);
	tool->discrete_cumul_p = NULL;
	tool->discrete_n = 0;
}

static gboolean
tool_random_engine_run_discrete_last_check (G_GNUC_UNUSED data_analysis_output_t *dao,
					    GnmRandomTool *tool)
{
	tools_data_random_t *info = &tool->data;
	discrete_random_tool_t *param = &info->param.discrete;
	GnmValue *range = param->range;
	gnm_float cumprob = 0;
	int j = 0;
	int i;

	gnm_random_tool_discrete_clear (tool);

	tool->discrete_n = range->v_range.cell.b.row - range->v_range.cell.a.row + 1;
	tool->discrete_cumul_p = g_new (gnm_float, tool->discrete_n);
	tool->discrete_values = g_new0 (GnmValue *, tool->discrete_n);

	for (i = range->v_range.cell.a.row;
	     i <= range->v_range.cell.b.row;
	     i++, j++) {
		GnmValue *v;
		gnm_float thisprob;
		GnmCell *cell = sheet_cell_get (range->v_range.cell.a.sheet,
						range->v_range.cell.a.col + 1, i);

		if (cell == NULL ||
		    (v = cell->value) == NULL ||
		    !VALUE_IS_NUMBER (v)) {
			gnm_cmd_context_error_calc (GO_CMD_CONTEXT (info->wbc),
						    _("The probability input range "
						      "contains a non-numeric value.\n"
						      "All probabilities must be "
						      "non-negative numbers."));
			goto random_tool_discrete_out;
		}
		if ((thisprob = value_get_as_float (v)) < 0) {
			gnm_cmd_context_error_calc (GO_CMD_CONTEXT (info->wbc),
						    _("The probability input range "
						      "contains a negative number.\n"
						      "All probabilities must be "
						      "non-negative!"));
			goto random_tool_discrete_out;
		}

		cumprob += thisprob;
		tool->discrete_cumul_p[j] = cumprob;

		cell = sheet_cell_get (range->v_range.cell.a.sheet,
				       range->v_range.cell.a.col, i);

		if (cell == NULL || cell->value == NULL) {
			gnm_cmd_context_error_calc (GO_CMD_CONTEXT (info->wbc),
						    _("None of the values in the value "
						      "range may be empty!"));
			goto random_tool_discrete_out;
		}

		tool->discrete_values[j] = value_dup (cell->value);
	}

	if (cumprob != 0) {
		/* Rescale... */
		for (i = 0; i < tool->discrete_n; i++) {
			tool->discrete_cumul_p[i] /= cumprob;
		}
		return FALSE;
	}
	gnm_cmd_context_error_calc (GO_CMD_CONTEXT (info->wbc),
				    _("The probabilities may not all be 0!"));

 random_tool_discrete_out:
	gnm_random_tool_discrete_clear (tool);
	return TRUE;
}

static gboolean
tool_random_engine_run_discrete (GOCmdContext *gcc, data_analysis_output_t *dao,
				 GnmRandomTool *tool)
{
	tools_data_random_t *info = &tool->data;
	gint i;

	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		int k;
		for (k = 0; k < info->count; k++) {
			int j;
			gnm_float x = random_01 ();

			for (j = 0; tool->discrete_cumul_p[j] < x; j++)
				;

			dao_set_cell_value (dao, i, k,
					    value_dup (tool->discrete_values[j]));
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_uniform (GOCmdContext *gcc, data_analysis_output_t *dao,
				tools_data_random_t *info,
				uniform_random_tool_t *param)
{
	int i, n;
	gnm_float range = param->upper_limit - param->lower_limit;

	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = range * random_01 () + param->lower_limit;
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_uniform_int (GOCmdContext *gcc, data_analysis_output_t *dao,
				    tools_data_random_t *info,
				    uniform_random_tool_t *param)
{
	int        i, n;
	gnm_float lower = gnm_floor (param->lower_limit);
	gnm_float upper = gnm_floor (param->upper_limit);

	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v = gnm_random_uniform_integer (lower, upper);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}

	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_normal (GOCmdContext *gcc, data_analysis_output_t *dao,
			       tools_data_random_t *info,
			       normal_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = param->stdev * random_normal () + param->mean;
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_bernoulli (GOCmdContext *gcc, data_analysis_output_t *dao,
				  tools_data_random_t *info,
				  bernoulli_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float tmp = random_bernoulli (param->p);
			dao_set_cell_int (dao, i, n, (int)tmp);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_beta (GOCmdContext *gcc, data_analysis_output_t *dao,
			     tools_data_random_t *info,
			     beta_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float tmp = random_beta (param->a, param->b);
			dao_set_cell_float (dao, i, n, tmp);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_binomial (GOCmdContext *gcc, data_analysis_output_t *dao,
				 tools_data_random_t *info,
				 binomial_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_binomial (param->p,
					     param->trials);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_negbinom (GOCmdContext *gcc, data_analysis_output_t *dao,
				 tools_data_random_t *info,
				 negbinom_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_negbinom (param->p,
					     param->f);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_poisson (GOCmdContext *gcc, data_analysis_output_t *dao,
				tools_data_random_t *info,
				poisson_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_poisson (param->lambda);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_exponential (GOCmdContext *gcc, data_analysis_output_t *dao,
				    tools_data_random_t *info,
				    exponential_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_exponential (param->b);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_exppow (GOCmdContext *gcc, data_analysis_output_t *dao,
			       tools_data_random_t *info,
			       exppow_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_exppow (param->a, param->b);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_cauchy (GOCmdContext *gcc, data_analysis_output_t *dao,
			       tools_data_random_t *info,
			       cauchy_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_cauchy (param->a);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_chisq (GOCmdContext *gcc, data_analysis_output_t *dao,
			      tools_data_random_t *info,
			      chisq_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_chisq (param->nu);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_pareto (GOCmdContext *gcc, data_analysis_output_t *dao,
			       tools_data_random_t *info,
			       pareto_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_pareto (param->a, param->b);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_rayleigh (GOCmdContext *gcc, data_analysis_output_t *dao,
				 tools_data_random_t *info,
				 rayleigh_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_rayleigh (param->sigma);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_rayleigh_tail (GOCmdContext *gcc,
				      data_analysis_output_t *dao,
				      tools_data_random_t *info,
				      rayleigh_tail_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_rayleigh_tail (param->a, param->sigma);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_levy (GOCmdContext *gcc, data_analysis_output_t *dao,
			     tools_data_random_t *info,
			     levy_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_levy (param->c, param->alpha);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_fdist (GOCmdContext *gcc, data_analysis_output_t *dao,
			      tools_data_random_t *info,
			      fdist_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_fdist (param->nu1, param->nu2);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_lognormal (GOCmdContext *gcc, data_analysis_output_t *dao,
				  tools_data_random_t *info,
				  lognormal_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_lognormal (param->zeta, param->sigma);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_logarithmic (GOCmdContext *gcc, data_analysis_output_t *dao,
				    tools_data_random_t *info,
				    logarithmic_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_logarithmic (param->p);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_logistic (GOCmdContext *gcc, data_analysis_output_t *dao,
				 tools_data_random_t *info,
				 logistic_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_logistic (param->a);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_tdist (GOCmdContext *gcc, data_analysis_output_t *dao,
			      tools_data_random_t *info,
			      tdist_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_tdist (param->nu);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_gamma (GOCmdContext *gcc, data_analysis_output_t *dao,
			      tools_data_random_t *info,
			      gamma_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_gamma (param->a, param->b);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_geometric (GOCmdContext *gcc, data_analysis_output_t *dao,
				  tools_data_random_t *info,
				  geometric_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_geometric (param->p);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_weibull (GOCmdContext *gcc, data_analysis_output_t *dao,
				tools_data_random_t *info,
				weibull_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_weibull (param->a, param->b);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_laplace (GOCmdContext *gcc, data_analysis_output_t *dao,
				tools_data_random_t *info,
				laplace_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_laplace (param->a);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_gaussian_tail (GOCmdContext *gcc,
				      data_analysis_output_t *dao,
				      tools_data_random_t *info,
				      gaussian_tail_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_gaussian_tail (param->a, param->sigma);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_landau (GOCmdContext *gcc, data_analysis_output_t *dao,
			       tools_data_random_t *info)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_landau ();
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_gumbel1 (GOCmdContext *gcc, data_analysis_output_t *dao,
				tools_data_random_t *info,
				gumbel_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_gumbel1 (param->a, param->b);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_gumbel2 (GOCmdContext *gcc, data_analysis_output_t *dao,
				tools_data_random_t *info,
				gumbel_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnm_float v;
			v = random_gumbel2 (param->a, param->b);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

G_DEFINE_TYPE (GnmRandomTool, gnm_random_tool, GNM_TYPE_ANALYSIS_TOOL)

static void
gnm_random_tool_init (GnmRandomTool *tool)
{
	tool->data.param.discrete.range = NULL;
	tool->data.wbc = NULL;
	tool->data.n_vars = 1;
	tool->data.count = 1;
	tool->data.distribution = UniformDistribution;

	tool->discrete_n = 0;
	tool->discrete_values = NULL;
	tool->discrete_cumul_p = NULL;
}

static void
gnm_random_tool_finalize (GObject *obj)
{
	GnmRandomTool *tool = GNM_RANDOM_TOOL (obj);
	if (tool->data.distribution == DiscreteDistribution &&
	    tool->data.param.discrete.range != NULL) {
		value_release (tool->data.param.discrete.range);
		tool->data.param.discrete.range = NULL;
	}
	gnm_random_tool_discrete_clear (tool);
	G_OBJECT_CLASS (gnm_random_tool_parent_class)->finalize (obj);
}

static gboolean
gnm_random_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	tools_data_random_t *info = &GNM_RANDOM_TOOL (tool)->data;
	dao_adjust (dao, info->n_vars, info->count);
	return FALSE;
}

static char *
gnm_random_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Random Numbers (%s)"));
}

static gboolean
gnm_random_tool_last_validity_check (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmRandomTool *rtool = GNM_RANDOM_TOOL (tool);
	if (rtool->data.distribution == DiscreteDistribution)
		return tool_random_engine_run_discrete_last_check (dao, rtool);
	return FALSE;
}

static gboolean
gnm_random_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("Random Numbers"));
	return FALSE;
}

static gboolean
gnm_random_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("Random Numbers"));
}

static gboolean
gnm_random_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmRandomTool *rtool = GNM_RANDOM_TOOL (tool);
	tools_data_random_t *info = &rtool->data;
	GOCmdContext *gcc = GO_CMD_CONTEXT (info->wbc);

	switch (info->distribution) {
	case DiscreteDistribution:
		return tool_random_engine_run_discrete (gcc, dao, rtool);
	case NormalDistribution:
		return tool_random_engine_run_normal
			(gcc, dao, info, &info->param.normal);
	case BernoulliDistribution:
		return tool_random_engine_run_bernoulli
			(gcc, dao, info, &info->param.bernoulli);
	case BetaDistribution:
		return tool_random_engine_run_beta
			(gcc, dao, info, &info->param.beta);
	case UniformDistribution:
		return tool_random_engine_run_uniform
			(gcc, dao, info, &info->param.uniform);
	case UniformIntDistribution:
		return tool_random_engine_run_uniform_int
			(gcc, dao, info, &info->param.uniform);
	case PoissonDistribution:
		return tool_random_engine_run_poisson
			(gcc, dao, info, &info->param.poisson);
	case ExponentialDistribution:
		return tool_random_engine_run_exponential
			(gcc, dao, info, &info->param.exponential);
	case ExponentialPowerDistribution:
		return tool_random_engine_run_exppow
			(gcc, dao, info, &info->param.exppow);
	case CauchyDistribution:
		return tool_random_engine_run_cauchy
			(gcc, dao, info, &info->param.cauchy);
	case ChisqDistribution:
		return tool_random_engine_run_chisq
			(gcc, dao, info, &info->param.chisq);
	case ParetoDistribution:
		return tool_random_engine_run_pareto
			(gcc, dao, info, &info->param.pareto);
	case LognormalDistribution:
		return tool_random_engine_run_lognormal
			(gcc, dao, info, &info->param.lognormal);
	case RayleighDistribution:
		return tool_random_engine_run_rayleigh
			(gcc, dao, info, &info->param.rayleigh);
	case RayleighTailDistribution:
		return tool_random_engine_run_rayleigh_tail
			(gcc, dao, info, &info->param.rayleigh_tail);
	case LevyDistribution:
		return tool_random_engine_run_levy
			(gcc, dao, info, &info->param.levy);
	case FdistDistribution:
		return tool_random_engine_run_fdist
			(gcc, dao, info, &info->param.fdist);
	case TdistDistribution:
		return tool_random_engine_run_tdist
			(gcc, dao, info, &info->param.tdist);
	case GammaDistribution:
		return tool_random_engine_run_gamma
			(gcc, dao, info, &info->param.gamma);
	case GeometricDistribution:
		return tool_random_engine_run_geometric
			(gcc, dao, info, &info->param.geometric);
	case WeibullDistribution:
		return tool_random_engine_run_weibull
			(gcc, dao, info, &info->param.weibull);
	case LaplaceDistribution:
		return tool_random_engine_run_laplace
			(gcc, dao, info, &info->param.laplace);
	case GaussianTailDistribution:
		return tool_random_engine_run_gaussian_tail
			(gcc, dao, info, &info->param.gaussian_tail);
	case LandauDistribution:
		return tool_random_engine_run_landau
			(gcc, dao, info);
	case LogarithmicDistribution:
		return tool_random_engine_run_logarithmic
			(gcc, dao, info, &info->param.logarithmic);
	case LogisticDistribution:
		return tool_random_engine_run_logistic
			(gcc, dao, info, &info->param.logistic);
	case Gumbel1Distribution:
		return tool_random_engine_run_gumbel1
			(gcc, dao, info, &info->param.gumbel);
	case Gumbel2Distribution:
		return tool_random_engine_run_gumbel2
			(gcc, dao, info, &info->param.gumbel);
	case BinomialDistribution:
		return tool_random_engine_run_binomial
			(gcc, dao, info, &info->param.binomial);
	case NegativeBinomialDistribution:
		return tool_random_engine_run_negbinom
			(gcc, dao, info, &info->param.negbinom);
	}
	return FALSE;
}

static void
gnm_random_tool_class_init (GnmRandomToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->finalize = gnm_random_tool_finalize;
	at_class->update_dao = gnm_random_tool_update_dao;
	at_class->update_descriptor = gnm_random_tool_update_descriptor;
	at_class->last_validity_check = gnm_random_tool_last_validity_check;
	at_class->prepare_output_range = gnm_random_tool_prepare_output_range;
	at_class->format_output_range = gnm_random_tool_format_output_range;
	at_class->perform_calc = gnm_random_tool_perform_calc;
}

GnmAnalysisTool *
gnm_random_tool_new (void)
{
	return g_object_new (GNM_TYPE_RANDOM_TOOL, NULL);
}


