/*
 * random-generator.c:
 *
 * Authors:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *   Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
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

int
random_tool (WorkbookControl *wbc, Sheet *sheet, int vars, int count,
	     random_distribution_t distribution,
	     random_tool_t *param, data_analysis_output_t *dao)
{
	if (distribution != DiscreteDistribution)
		dao_prepare_output (wbc, dao, _("Random"));

	switch (distribution) {
	case DiscreteDistribution: {
		Value *range = param->discrete.range;
	        int n = range->v_range.cell.b.row - range->v_range.cell.a.row + 1;
	        gnum_float *prob = g_new (gnum_float, n);
		gnum_float *cumul_p = g_new (gnum_float, n);
		Value **values = g_new0 (Value *, n);
                gnum_float cumprob = 0;
		int j = 0;
		int i;
		int err = 0;

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
				err = 1;
				goto random_tool_discrete_out;
			}
			if ((thisprob = value_get_as_float (v)) < 0) {
				err = 3;
				goto random_tool_discrete_out;
			}

			prob[j] = thisprob;
			cumprob += thisprob;
			cumul_p[j] = cumprob;

		        cell = sheet_cell_get (range->v_range.cell.a.sheet,
					       range->v_range.cell.a.col, i);

			if (cell == NULL || cell->value == NULL) {
				err = 4;
				goto random_tool_discrete_out;
			}

			values[j] = value_duplicate (cell->value);
		}

		if (cumprob == 0) {
			err = 2;
			goto random_tool_discrete_out;
		}
		/* Rescale... */
		for (i = 0; i < n; i++) {
			prob[i] /= cumprob;
			cumul_p[i] /= cumprob;
		}

		dao_prepare_output (wbc, dao, _("Random"));
	        for (i = 0; i < vars; i++) {
			int k;
		        for (k = 0; k < count; k++) {
				int j;
			        gnum_float x = random_01 ();

				for (j = 0; cumul_p[j] < x; j++)
				        ;

				dao_set_cell_value (dao, i, k, value_duplicate (values[j]));
			}
		}

	random_tool_discrete_out:
		for (i = 0; i < n; i++)
			if (values[i])
				value_release (values[i]);
		g_free (prob);
		g_free (cumul_p);
		g_free (values);
		value_release (range);

		if (err)
			return err;

	        break;
	}

	case NormalDistribution: {
		int i, n;
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
			        v = param->normal.stdev * random_normal () + param->normal.mean;
				dao_set_cell_float (dao, i, n, v);
			}
		}
	        break;
	}

	case BernoulliDistribution: {
		int i, n;
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
			        gnum_float tmp = random_bernoulli (param->bernoulli.p);
				dao_set_cell_int (dao, i, n, (int)tmp);
			}
		}
	        break;
	}

	case UniformDistribution: {
		int i, n;
		gnum_float range = param->uniform.upper_limit - param->uniform.lower_limit;
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
				v = range * random_01 () + param->uniform.lower_limit;
				dao_set_cell_float (dao, i, n, v);
			}
		}
		break;
	}

	case PoissonDistribution: {
		int i, n;
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
			        v = random_poisson (param->poisson.lambda);
				dao_set_cell_float (dao, i, n, v);
			}
		}
	        break;
	}

	case ExponentialDistribution: {
		int i, n;
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
			        v = random_exponential (param->exponential.b);
				dao_set_cell_float (dao, i, n, v);
			}
		}
	        break;
	}

	case BinomialDistribution: {
		int i, n;
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
			        v = random_binomial (param->binomial.p,
						     param->binomial.trials);
				dao_set_cell_float (dao, i, n, v);
			}
		}
	        break;
	}

	case NegativeBinomialDistribution: {
		int i, n;
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
			        v = random_negbinom (param->negbinom.p,
						     param->negbinom.f);
				dao_set_cell_float (dao, i, n, v);
			}
		}
	        break;
	}

	default:
	        printf (_("Not implemented yet.\n"));
		break;
	}

	dao_autofit_columns (dao);
	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (dao->sheet);

	return 0;
}

