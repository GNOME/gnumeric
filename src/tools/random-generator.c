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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "random-generator.h"

#include "mathfunc.h"
#include "rangefunc.h"
#include "parse-util.h"
#include "tools.h"
#include "value.h"
#include "cell.h"
#include "sheet.h"
#include "ranges.h"
#include "style.h"
#include "sheet-style.h"
#include "workbook.h"
#include "format.h"
#include "gui-util.h"
#include "sheet-object-cell-comment.h"

#include <libgnome/gnome-i18n.h>
#include <gtk/gtk.h>
#include <string.h>
#include <math.h>


/************* Random Number Generation Tool ******************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

typedef struct {
	gint n;
	Value **values;
	gnum_float *cumul_p;
} discrete_random_tool_local_t;

static void
tool_random_engine_run_discrete_clear_continuity (discrete_random_tool_local_t **continuity)
{
	discrete_random_tool_local_t *data = *continuity;
	gint i;

	for (i = 0; i < data->n; i++)
		if (data->values[i])
			value_release (data->values[i]);
	g_free (data->cumul_p);
	g_free (data->values);
	g_free (data);
	*continuity = NULL;
}

static gboolean
tool_random_engine_run_discrete_last_check (data_analysis_output_t *dao, 
					    tools_data_random_t *info,
					    discrete_random_tool_t *param,
					    discrete_random_tool_local_t **continuity)
{
	discrete_random_tool_local_t *data;
	Value *range = param->range;
	gnum_float cumprob = 0;
	int j = 0;
	int i;

	data = *continuity = g_new0 (discrete_random_tool_local_t, 1);
	data->n = range->v_range.cell.b.row - range->v_range.cell.a.row + 1;
	data->cumul_p = g_new (gnum_float, data->n);
	data->values = g_new0 (Value *, data->n);

	for (i = range->v_range.cell.a.row;
	     i <= range->v_range.cell.b.row;
	     i++, j++) {
		Value *v;
		gnum_float thisprob;
		Cell *cell = sheet_cell_get (range->v_range.cell.a.sheet,
					     range->v_range.cell.a.col + 1, i);
		
		if (cell == NULL ||
		    (v = cell->value) == NULL ||
		    !VALUE_IS_NUMBER (v)) {
			gnumeric_notice (info->wbcg, GTK_MESSAGE_ERROR,
					 _("The probability input range "
					   "contains a non-numeric value.\n"
					   "All probabilities must be "
					   "non-negative numbers."));
			goto random_tool_discrete_out;
		}
		if ((thisprob = value_get_as_float (v)) < 0) {
			gnumeric_notice (info->wbcg, GTK_MESSAGE_ERROR,
					 _("The probability input range "
					   "contains a negative number.\n"
					   "All probabilities must be "
					   "non-negative!"));
			goto random_tool_discrete_out;
		}
		
		cumprob += thisprob;
		data->cumul_p[j] = cumprob;
		
		cell = sheet_cell_get (range->v_range.cell.a.sheet,
				       range->v_range.cell.a.col, i);
		
		if (cell == NULL || cell->value == NULL) {
			gnumeric_notice (info->wbcg, GTK_MESSAGE_ERROR,
					 _("None of the values in the value "
					   "range may be empty!"));
			goto random_tool_discrete_out;
		}
		
		data->values[j] = value_duplicate (cell->value);
	}
	
	if (cumprob != 0) {
		/* Rescale... */
		for (i = 0; i < data->n; i++) {
			data->cumul_p[i] /= cumprob;
		}
		return FALSE;
	}
	gnumeric_notice (info->wbcg, GTK_MESSAGE_ERROR,
			 _("The probabilities may not all be 0!"));

 random_tool_discrete_out:
	tool_random_engine_run_discrete_clear_continuity (continuity);	
	return TRUE;
}

static gboolean
tool_random_engine_run_discrete (data_analysis_output_t *dao, 
				 tools_data_random_t *info,
				 discrete_random_tool_t *param,
				 discrete_random_tool_local_t **continuity)
{
	gint i;
	discrete_random_tool_local_t *data = *continuity;

	for (i = 0; i < info->n_vars; i++) {
		int k;
		for (k = 0; k < info->count; k++) {
			int j;
			gnum_float x = random_01 ();
			
			for (j = 0; data->cumul_p[j] < x; j++)
				;
			
			dao_set_cell_value (dao, i, k,
					    value_duplicate (data->values[j]));
		}
	}
	tool_random_engine_run_discrete_clear_continuity (continuity);
	return FALSE;
}

static gboolean
tool_random_engine_run_uniform (data_analysis_output_t *dao, 
				tools_data_random_t *info,
				uniform_random_tool_t *param)
{
	int i, n;
	gnum_float range = param->upper_limit - param->lower_limit;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = range * random_01 () + param->lower_limit;
			dao_set_cell_float (dao, i, n, v);
		}
	}
	
	return FALSE;
}

static gboolean
tool_random_engine_run_normal (data_analysis_output_t *dao, 
			       tools_data_random_t *info,
			       normal_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = param->stdev * random_normal () + param->mean;
			dao_set_cell_float (dao, i, n, v);
		}
	}
	return FALSE;
}

static gboolean
tool_random_engine_run_bernoulli (data_analysis_output_t *dao, 
				  tools_data_random_t *info,
				  bernoulli_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float tmp = random_bernoulli (param->p);
			dao_set_cell_int (dao, i, n, (int)tmp);
		}
	}
	return FALSE;
}

static gboolean
tool_random_engine_run_beta (data_analysis_output_t *dao, 
			     tools_data_random_t *info,
			     beta_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float tmp = random_beta (param->a, param->b);
			dao_set_cell_float (dao, i, n, tmp);
		}
	}
	return FALSE;
}

static gboolean
tool_random_engine_run_binomial (data_analysis_output_t *dao, 
				 tools_data_random_t *info,
				 binomial_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_binomial (param->p,
					     param->trials);
			dao_set_cell_float (dao, i, n, v);
		}
	}	
	return FALSE;
}

static gboolean
tool_random_engine_run_negbinom (data_analysis_output_t *dao, 
				 tools_data_random_t *info,
				 negbinom_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_negbinom (param->p,
					     param->f);
			dao_set_cell_float (dao, i, n, v);
		}
	}
	return FALSE;
}

static gboolean
tool_random_engine_run_poisson (data_analysis_output_t *dao, 
				tools_data_random_t *info,
				poisson_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_poisson (param->lambda);
			dao_set_cell_float (dao, i, n, v);
		}
	}
	return FALSE;
}

static gboolean
tool_random_engine_run_exponential (data_analysis_output_t *dao, 
				    tools_data_random_t *info,
				    exponential_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_exponential (param->b);
			dao_set_cell_float (dao, i, n, v);
		}
	}	
	return FALSE;
}

static gboolean
tool_random_engine_run_cauchy (data_analysis_output_t *dao, 
			       tools_data_random_t *info,
			       cauchy_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_cauchy (param->a);
			dao_set_cell_float (dao, i, n, v);
		}
	}	
	return FALSE;
}

static gboolean
tool_random_engine_run_chisq (data_analysis_output_t *dao, 
			      tools_data_random_t *info,
			      chisq_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_chisq (param->nu);
			dao_set_cell_float (dao, i, n, v);
		}
	}	
	return FALSE;
}

static gboolean
tool_random_engine_run_pareto (data_analysis_output_t *dao, 
			       tools_data_random_t *info,
			       pareto_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_pareto (param->a, param->b);
			dao_set_cell_float (dao, i, n, v);
		}
	}	
	return FALSE;
}

static gboolean
tool_random_engine_run_rayleigh (data_analysis_output_t *dao, 
				 tools_data_random_t *info,
				 rayleigh_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_rayleigh (param->sigma);
			dao_set_cell_float (dao, i, n, v);
		}
	}	
	return FALSE;
}

static gboolean
tool_random_engine_run_fdist (data_analysis_output_t *dao, 
			      tools_data_random_t *info,
			      fdist_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_fdist (param->nu1, param->nu2);
			dao_set_cell_float (dao, i, n, v);
		}
	}	
	return FALSE;
}

static gboolean
tool_random_engine_run_lognormal (data_analysis_output_t *dao, 
				  tools_data_random_t *info,
				  lognormal_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_lognormal (param->zeta, param->sigma);
			dao_set_cell_float (dao, i, n, v);
		}
	}	
	return FALSE;
}

static gboolean
tool_random_engine_run_logarithmic (data_analysis_output_t *dao, 
				    tools_data_random_t *info,
				    logarithmic_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_logarithmic (param->p);
			dao_set_cell_float (dao, i, n, v);
		}
	}	
	return FALSE;
}

static gboolean
tool_random_engine_run_logistic (data_analysis_output_t *dao, 
				 tools_data_random_t *info,
				 logistic_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_logistic (param->a);
			dao_set_cell_float (dao, i, n, v);
		}
	}	
	return FALSE;
}

static gboolean
tool_random_engine_run_tdist (data_analysis_output_t *dao, 
			      tools_data_random_t *info,
			      tdist_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_tdist (param->nu);
			dao_set_cell_float (dao, i, n, v);
		}
	}	
	return FALSE;
}

static gboolean
tool_random_engine_run_gamma (data_analysis_output_t *dao, 
			      tools_data_random_t *info,
			      gamma_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_gamma (param->a, param->b);
			dao_set_cell_float (dao, i, n, v);
		}
	}	
	return FALSE;
}

static gboolean
tool_random_engine_run_geometric (data_analysis_output_t *dao, 
				  tools_data_random_t *info,
				  geometric_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_geometric (param->p);
			dao_set_cell_float (dao, i, n, v);
		}
	}	
	return FALSE;
}

static gboolean
tool_random_engine_run_weibull (data_analysis_output_t *dao, 
				tools_data_random_t *info,
				weibull_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_weibull (param->a, param->b);
			dao_set_cell_float (dao, i, n, v);
		}
	}	
	return FALSE;
}

static gboolean
tool_random_engine_run_laplace (data_analysis_output_t *dao, 
				tools_data_random_t *info,
				laplace_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_laplace (param->a);
			dao_set_cell_float (dao, i, n, v);
		}
	}	
	return FALSE;
}

static gboolean
tool_random_engine_run_gumbel1 (data_analysis_output_t *dao, 
				tools_data_random_t *info,
				gumbel_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_gumbel1 (param->a, param->b);
			dao_set_cell_float (dao, i, n, v);
		}
	}	
	return FALSE;
}

static gboolean
tool_random_engine_run_gumbel2 (data_analysis_output_t *dao, 
				tools_data_random_t *info,
				gumbel_random_tool_t *param)
{
	int i, n;
	for (i = 0; i < info->n_vars; i++) {
		for (n = 0; n < info->count; n++) {
			gnum_float v;
			v = random_gumbel2 (param->a, param->b);
			dao_set_cell_float (dao, i, n, v);
		}
	}	
	return FALSE;
}

gboolean 
tool_random_engine (data_analysis_output_t *dao, gpointer specs, 
		    analysis_tool_engine_t selector, gpointer result)
{
	tools_data_random_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Random Numbers (%s)"),
						result) == NULL);
	case TOOL_ENGINE_UPDATE_DAO: 
		dao_adjust (dao, info->n_vars, info->count);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		if (info->distribution == DiscreteDistribution && 
		    info->param.discrete.range != NULL) {
			value_release (info->param.discrete.range);
			info->param.discrete.range = NULL;
		}
		return FALSE;
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		if (info->distribution == DiscreteDistribution)
			return tool_random_engine_run_discrete_last_check 
				(dao, specs, &info->param.discrete, result);
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Random Numbers"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Random Numbers"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		switch (info->distribution) {
		case DiscreteDistribution:
			return tool_random_engine_run_discrete 
				(dao, specs, &info->param.discrete, result);
		case NormalDistribution: 
			return tool_random_engine_run_normal
			        (dao, specs, &info->param.normal);
		case BernoulliDistribution:
			return tool_random_engine_run_bernoulli 
				(dao, specs, &info->param.bernoulli);
		case BetaDistribution:
			return tool_random_engine_run_beta
				(dao, specs, &info->param.beta);
		case UniformDistribution:
			return tool_random_engine_run_uniform 
			        (dao, specs, &info->param.uniform);
		case PoissonDistribution:
			return tool_random_engine_run_poisson
			        (dao, specs, &info->param.poisson);
		case ExponentialDistribution:
			return tool_random_engine_run_exponential 
				(dao, specs, &info->param.exponential);
		case CauchyDistribution:
			return tool_random_engine_run_cauchy 
				(dao, specs, &info->param.cauchy);
		case ChisqDistribution:
			return tool_random_engine_run_chisq
				(dao, specs, &info->param.chisq);
		case ParetoDistribution:
			return tool_random_engine_run_pareto
				(dao, specs, &info->param.pareto);
		case LognormalDistribution:
			return tool_random_engine_run_lognormal
				(dao, specs, &info->param.lognormal);
		case RayleighDistribution:
			return tool_random_engine_run_rayleigh
				(dao, specs, &info->param.rayleigh);
		case FdistDistribution:
			return tool_random_engine_run_fdist
				(dao, specs, &info->param.fdist);
		case TdistDistribution:
			return tool_random_engine_run_tdist
				(dao, specs, &info->param.tdist);
		case GammaDistribution:
			return tool_random_engine_run_gamma
				(dao, specs, &info->param.gamma);
		case GeometricDistribution:
			return tool_random_engine_run_geometric
				(dao, specs, &info->param.geometric);
		case WeibullDistribution:
			return tool_random_engine_run_weibull
				(dao, specs, &info->param.weibull);
		case LaplaceDistribution:
			return tool_random_engine_run_laplace
				(dao, specs, &info->param.laplace);
		case LogarithmicDistribution:
			return tool_random_engine_run_logarithmic
				(dao, specs, &info->param.logarithmic);
		case LogisticDistribution:
			return tool_random_engine_run_logistic
				(dao, specs, &info->param.logistic);
		case Gumbel1Distribution:
			return tool_random_engine_run_gumbel1
				(dao, specs, &info->param.gumbel);
		case Gumbel2Distribution:
			return tool_random_engine_run_gumbel2
				(dao, specs, &info->param.gumbel);
		case BinomialDistribution:
			return tool_random_engine_run_binomial
			        (dao, specs, &info->param.binomial);
		case NegativeBinomialDistribution:	
			return tool_random_engine_run_negbinom
			        (dao, specs, &info->param.negbinom);
		}
	}
	return TRUE;  /* We shouldn't get here */
}

