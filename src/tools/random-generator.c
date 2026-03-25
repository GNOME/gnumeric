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

#define PROGRESS_START int pro = 0;					\
	double total = rtool->n_vars * rtool->count;			\
	go_cmd_context_progress_set (gcc, 0);				\
	go_cmd_context_progress_message_set (gcc, _("Generating Random Numbers..."))
#define PROGESS_RUN if ((++pro & 2047) == 0) {        \
        go_cmd_context_progress_set (gcc, pro/total); \
	while (gtk_events_pending ())                 \
	gtk_main_iteration_do (FALSE);                \
        }
#define PROGESS_END go_cmd_context_progress_set (gcc, 0);	\
	go_cmd_context_progress_message_set (gcc, NULL)


/************* Random Number Generation Tool ******************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

static void
gnm_random_tool_discrete_clear (GnmRandomTool *rtool)
{
	gint i;

	if (rtool->discrete_values) {
		for (i = 0; i < rtool->discrete_n; i++)
			value_release (rtool->discrete_values[i]);
		g_free (rtool->discrete_values);
		rtool->discrete_values = NULL;
	}
	g_free (rtool->discrete_cumul_p);
	rtool->discrete_cumul_p = NULL;
	rtool->discrete_n = 0;
}

static gboolean
tool_random_engine_run_discrete_last_check (GnmRandomTool *rtool,
					    WorkbookControl *wbc,
					    G_GNUC_UNUSED data_analysis_output_t *dao)
{
	discrete_random_tool_t *param = &rtool->param.discrete;
	GnmValue *range = param->range;
	gnm_float cumprob = 0;
	int j = 0;
	int i;

	gnm_random_tool_discrete_clear (rtool);

	rtool->discrete_n = range->v_range.cell.b.row - range->v_range.cell.a.row + 1;
	rtool->discrete_cumul_p = g_new (gnm_float, rtool->discrete_n);
	rtool->discrete_values = g_new0 (GnmValue *, rtool->discrete_n);

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
			gnm_cmd_context_error_calc (GO_CMD_CONTEXT (wbc),
						    _("The probability input range "
						      "contains a non-numeric value.\n"
						      "All probabilities must be "
						      "non-negative numbers."));
			goto random_tool_discrete_out;
		}
		if ((thisprob = value_get_as_float (v)) < 0) {
			gnm_cmd_context_error_calc (GO_CMD_CONTEXT (wbc),
						    _("The probability input range "
						      "contains a negative number.\n"
						      "All probabilities must be "
						      "non-negative!"));
			goto random_tool_discrete_out;
		}

		cumprob += thisprob;
		rtool->discrete_cumul_p[j] = cumprob;

		cell = sheet_cell_get (range->v_range.cell.a.sheet,
				       range->v_range.cell.a.col, i);

		if (cell == NULL || cell->value == NULL) {
			gnm_cmd_context_error_calc (GO_CMD_CONTEXT (wbc),
						    _("None of the values in the value "
						      "range may be empty!"));
			goto random_tool_discrete_out;
		}

		rtool->discrete_values[j] = value_dup (cell->value);
	}

	if (cumprob != 0) {
		/* Rescale... */
		for (i = 0; i < rtool->discrete_n; i++) {
			rtool->discrete_cumul_p[i] /= cumprob;
		}
		return FALSE;
	}
	gnm_cmd_context_error_calc (GO_CMD_CONTEXT (wbc),
				    _("The probabilities may not all be 0!"));

 random_tool_discrete_out:
	gnm_random_tool_discrete_clear (rtool);
	return TRUE;
}

static gboolean
tool_random_engine_run_discrete (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao)
{
	gint i;

	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		int k;
		for (k = 0; k < rtool->count; k++) {
			int j;
			gnm_float x = random_01 ();

			for (j = 0; rtool->discrete_cumul_p[j] < x; j++)
				;

			dao_set_cell_value (dao, i, k,
					    value_dup (rtool->discrete_values[j]));
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_uniform (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				uniform_random_tool_t *param)
{
	int i, n;
	gnm_float range = param->upper_limit - param->lower_limit;

	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_uniform_int (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				    uniform_random_tool_t *param)
{
	int        i, n;
	gnm_float lower = gnm_floor (param->lower_limit);
	gnm_float upper = gnm_floor (param->upper_limit);

	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
			gnm_float v = gnm_random_uniform_integer (lower, upper);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}

	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_normal (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
			       normal_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_bernoulli (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				  bernoulli_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
			gnm_float tmp = random_bernoulli (param->p);
			dao_set_cell_int (dao, i, n, (int)tmp);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_beta (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
			     beta_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
			gnm_float tmp = random_beta (param->a, param->b);
			dao_set_cell_float (dao, i, n, tmp);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

static gboolean
tool_random_engine_run_binomial (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				 binomial_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_negbinom (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				 negbinom_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_poisson (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				poisson_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_exponential (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				    exponential_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_exppow (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
			       exppow_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_cauchy (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
			       cauchy_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_chisq (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
			      chisq_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_pareto (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
			       pareto_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_rayleigh (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				 rayleigh_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_rayleigh_tail (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				      rayleigh_tail_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_levy (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
			     levy_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_fdist (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
			      fdist_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_lognormal (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				  lognormal_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_logarithmic (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				    logarithmic_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_logistic (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				 logistic_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_tdist (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
			      tdist_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_gamma (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
			      gamma_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_geometric (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				  geometric_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_weibull (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				weibull_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_laplace (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				laplace_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_gaussian_tail (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				      gaussian_tail_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_landau (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_gumbel1 (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				gumbel_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
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
tool_random_engine_run_gumbel2 (GnmRandomTool *rtool, GOCmdContext *gcc, data_analysis_output_t *dao,
				gumbel_random_tool_t *param)
{
	int i, n;
	PROGRESS_START;
	for (i = 0; i < rtool->n_vars; i++) {
		for (n = 0; n < rtool->count; n++) {
			gnm_float v;
			v = random_gumbel2 (param->a, param->b);
			dao_set_cell_float (dao, i, n, v);
			PROGESS_RUN;
		}
	}
	PROGESS_END;
	return FALSE;
}

G_DEFINE_TYPE (GnmRandomTool, gnm_random_tool, GNM_ANALYSIS_TOOL_TYPE)

static void
gnm_random_tool_init (GnmRandomTool *rtool)
{
	rtool->param.discrete.range = NULL;
	rtool->n_vars = 1;
	rtool->count = 1;
	rtool->distribution = UniformDistribution;

	rtool->discrete_n = 0;
	rtool->discrete_values = NULL;
	rtool->discrete_cumul_p = NULL;
}

static void
gnm_random_tool_finalize (GObject *obj)
{
	GnmRandomTool *rtool = GNM_RANDOM_TOOL (obj);
	switch (rtool->distribution) {
	case DiscreteDistribution:
		value_release (rtool->param.discrete.range);
		rtool->param.discrete.range = NULL;
		break;
	default:
		break;
	}
	gnm_random_tool_discrete_clear (rtool);
	G_OBJECT_CLASS (gnm_random_tool_parent_class)->finalize (obj);
}

static gboolean
gnm_random_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmRandomTool *rtool = GNM_RANDOM_TOOL (tool);
	dao_adjust (dao, rtool->n_vars, rtool->count);
	return FALSE;
}

static char *
gnm_random_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Random Numbers (%s)"));
}

static gboolean
gnm_random_tool_last_validity_check (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	GnmRandomTool *rtool = GNM_RANDOM_TOOL (tool);
	switch (rtool->distribution) {
	case DiscreteDistribution:
		return tool_random_engine_run_discrete_last_check (rtool, wbc, dao);
	default:
		return FALSE;
	}
}

static gboolean
gnm_random_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	dao_prepare_output (wbc, dao, _("Random Numbers"));
	return FALSE;
}

static gboolean
gnm_random_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	return dao_format_output (wbc, dao, _("Random Numbers"));
}

static gboolean
gnm_random_tool_perform_calc (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	GnmRandomTool *rtool = GNM_RANDOM_TOOL (tool);
	GOCmdContext *gcc = GO_CMD_CONTEXT (wbc);

	switch (rtool->distribution) {
	case DiscreteDistribution:
		return tool_random_engine_run_discrete (rtool, gcc, dao);
	case NormalDistribution:
		return tool_random_engine_run_normal
			(rtool, gcc, dao, &rtool->param.normal);
	case BernoulliDistribution:
		return tool_random_engine_run_bernoulli
			(rtool, gcc, dao, &rtool->param.bernoulli);
	case BetaDistribution:
		return tool_random_engine_run_beta
			(rtool, gcc, dao, &rtool->param.beta);
	case UniformDistribution:
		return tool_random_engine_run_uniform
			(rtool, gcc, dao, &rtool->param.uniform);
	case UniformIntDistribution:
		return tool_random_engine_run_uniform_int
			(rtool, gcc, dao, &rtool->param.uniform);
	case PoissonDistribution:
		return tool_random_engine_run_poisson
			(rtool, gcc, dao, &rtool->param.poisson);
	case ExponentialDistribution:
		return tool_random_engine_run_exponential
			(rtool, gcc, dao, &rtool->param.exponential);
	case ExponentialPowerDistribution:
		return tool_random_engine_run_exppow
			(rtool, gcc, dao, &rtool->param.exppow);
	case CauchyDistribution:
		return tool_random_engine_run_cauchy
			(rtool, gcc, dao, &rtool->param.cauchy);
	case ChisqDistribution:
		return tool_random_engine_run_chisq
			(rtool, gcc, dao, &rtool->param.chisq);
	case ParetoDistribution:
		return tool_random_engine_run_pareto
			(rtool, gcc, dao, &rtool->param.pareto);
	case LognormalDistribution:
		return tool_random_engine_run_lognormal
			(rtool, gcc, dao, &rtool->param.lognormal);
	case RayleighDistribution:
		return tool_random_engine_run_rayleigh
			(rtool, gcc, dao, &rtool->param.rayleigh);
	case RayleighTailDistribution:
		return tool_random_engine_run_rayleigh_tail
			(rtool, gcc, dao, &rtool->param.rayleigh_tail);
	case LevyDistribution:
		return tool_random_engine_run_levy
			(rtool, gcc, dao, &rtool->param.levy);
	case FdistDistribution:
		return tool_random_engine_run_fdist
			(rtool, gcc, dao, &rtool->param.fdist);
	case TdistDistribution:
		return tool_random_engine_run_tdist
			(rtool, gcc, dao, &rtool->param.tdist);
	case GammaDistribution:
		return tool_random_engine_run_gamma
			(rtool, gcc, dao, &rtool->param.gamma);
	case GeometricDistribution:
		return tool_random_engine_run_geometric
			(rtool, gcc, dao, &rtool->param.geometric);
	case WeibullDistribution:
		return tool_random_engine_run_weibull
			(rtool, gcc, dao, &rtool->param.weibull);
	case LaplaceDistribution:
		return tool_random_engine_run_laplace
			(rtool, gcc, dao, &rtool->param.laplace);
	case GaussianTailDistribution:
		return tool_random_engine_run_gaussian_tail
			(rtool, gcc, dao, &rtool->param.gaussian_tail);
	case LandauDistribution:
		return tool_random_engine_run_landau
			(rtool, gcc, dao);
	case LogarithmicDistribution:
		return tool_random_engine_run_logarithmic
			(rtool, gcc, dao, &rtool->param.logarithmic);
	case LogisticDistribution:
		return tool_random_engine_run_logistic
			(rtool, gcc, dao, &rtool->param.logistic);
	case Gumbel1Distribution:
		return tool_random_engine_run_gumbel1
			(rtool, gcc, dao, &rtool->param.gumbel);
	case Gumbel2Distribution:
		return tool_random_engine_run_gumbel2
			(rtool, gcc, dao, &rtool->param.gumbel);
	case BinomialDistribution:
		return tool_random_engine_run_binomial
			(rtool, gcc, dao, &rtool->param.binomial);
	case NegativeBinomialDistribution:
		return tool_random_engine_run_negbinom
			(rtool, gcc, dao, &rtool->param.negbinom);
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
