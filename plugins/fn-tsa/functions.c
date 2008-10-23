/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-tsa plugin
 * functions.c
 *
 * Copyright (C) 2006 Laurency Franck
 * Copyright (C) 2007 Jean Bréfort <jean.brefort@normalesup.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */


/***************************************************************
 * This file contains all declarations of time series analysis
 * functions and their help functions
****************************************************************/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <func.h>
#include <parse-util.h>
#include <mathfunc.h>
#include <rangefunc.h>
#include <regression.h>
#include <workbook.h>
#include <sheet.h>
#include <cell.h>
#include <collect.h>
#include <number-match.h>
#include <value.h>
#include <expr.h>
#include <expr-impl.h>
#include <func-builtin.h>
#include <gnm-i18n.h>
#include <goffice/math/go-cspline.h>
#include <gnm-plugin.h>
#include <tools/analysis-tools.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

GNM_PLUGIN_MODULE_HEADER;

enum {
	FILTER_NONE,
	FILTER_BARTLETT,
	FILTER_HANN,
	FILTER_WELCH,
	FILTER_GAUSSIAN,
};

enum {
	INTERPOLATION_LINEAR,
	INTERPOLATION_LINEAR_AVG,
	INTERPOLATION_STAIRCASE,
	INTERPOLATION_STAIRCASE_AVG,
	INTERPOLATION_SPLINE,
	INTERPOLATION_SPLINE_AVG,
};

#ifdef WITH_LONG_DOUBLE
#	define GnmCSpline GOCSplinel
#	define gnm_cspline_init go_cspline_initl
#	define gnm_cspline_destroy go_cspline_destroyl
#	define gnm_cspline_get_value go_cspline_get_valuel
#	define gnm_cspline_get_values go_cspline_get_valuesl
#	define gnm_cspline_get_integrals go_cspline_get_integralsl
#else
#	define GnmCSpline GOCSpline
#	define gnm_cspline_init go_cspline_init
#	define gnm_cspline_destroy go_cspline_destroy
#	define gnm_cspline_get_value go_cspline_get_value
#	define gnm_cspline_get_values go_cspline_get_values
#	define gnm_cspline_get_integrals go_cspline_get_integrals
#endif

/**************************************************************************/
/**************************************************************************/
/*           CALCULATION FUNCTIONS FOR TOOLS                                */
/**************************************************************************/
/**************************************************************************/

/*******LINEAR INTERPOLATION*******/

static gnm_float*
linear_interpolation (const gnm_float *absc, const gnm_float *ord, int nb_knots,
		      const gnm_float *targets, int nb_targets)
{
	int i, j, k, jmax = nb_knots - 1;
	gnm_float slope, *res;
	if (nb_knots < 2)
		return NULL;
	res = g_new (double, nb_targets);
	if (go_range_increasing (targets, nb_targets)) {
		j = 1;
		k = 0;
		slope = (ord[1] - ord[0]) / (absc[1] - absc[0]);
		for (i = 0; i < nb_targets; i++) {
			while (j < jmax && targets[i] > absc[j])
				j++;
			if (k < j - 1) {
				k = j - 1;
				slope = (ord[j] - ord[k]) / (absc[j] - absc[k]);
			}
			res[i] = (targets[i] - absc[k]) * slope + ord[k];
		}
	} else {
		int l;
		for (i = 0; i < nb_targets; i++) {
			k = jmax - 1;
			if (targets[i] >= absc[k])
				res[i] = (targets[i] - absc[k]) *
					(ord[jmax] - ord[k]) / (absc[jmax] - absc[k])
					+ ord[k];
			else if (targets[i] <= absc[1])
				res[i] = (targets[i] - absc[0]) *
					(ord[1] - ord[0]) / (absc[1] - absc[0])
					+ ord[0];
			else {
				j = 1;
				while (k > j + 1) {
					l = (k + j) / 2;
					if (targets[i] > absc[l])
						j = l;
					else
						k = l;
				}
				res[i] = (targets[i] - absc[j]) *
					(ord[k] - ord[j]) / (absc[k] - absc[j])
					+ ord[j];
			}
		}
	}
	return res;
}

/*******LINEAR AVERAGING*******/

static gnm_float*
linear_averaging (const gnm_float *absc, const gnm_float *ord, int nb_knots,
		      const gnm_float *targets, int nb_targets)
{
	int i, j, k, jmax = nb_knots - 1;
	gnm_float slope, *res, x0, x1;
	if (nb_knots < 2 || !go_range_increasing (targets, nb_targets))
		return NULL;
	res = g_new (double, nb_targets - 1);
	j = 1;
	while (j < jmax && targets[0] > absc[j])
		j++;
	k = j - 1;
	slope = (ord[j] - ord[k]) / (absc[j] - absc[k]) / 2.;
	for (i = 1; i < nb_targets; i++) {
		if (targets[i] < absc[j] || j == jmax) {
			x0 = targets[i - 1] - absc[k];
			x1 = targets[i] - absc[k];
			res[i - 1] = (x1 * (slope * x1 + ord[k])
				- x0 * (slope * x0 + ord[k]))
				/ (x1 - x0);
			continue;
		}
		x0 = targets[i - 1] - absc[k];
		x1 = absc[j] - absc[k];
		res[i - 1] = (x1 * (slope * x1 + ord[k])
			- x0 * (slope * x0 + ord[k]));
		while (j < jmax && targets[i] > absc[++j]) {
			k++;
			x0 = absc[j] - absc[k];
			slope = (ord[j] - ord[k]) / x0 / 2.;
			res[i - 1] += x0 * (slope * x0 + ord[k]);
		}
		if (j > k - 1) {
		    k = j - 1;
		    slope = (ord[j] - ord[k]) / (absc[j] - absc[k]) / 2.;
		}
		x0 = targets[i] - absc[k];
		res[i - 1] += x0 * (slope * x0 + ord[k]);
		res[i - 1] /= (targets[i] - targets[i - 1]);
	}
	return res;
}

/*******STAIRCASE INTERPOLATION*******/

static gnm_float*
staircase_interpolation (const gnm_float *absc, const gnm_float *ord, int nb_knots,
		      const gnm_float *targets, int nb_targets)
{
	int i, j, jmax = nb_knots - 1;
	gnm_float *res;
	res = g_new (double, nb_targets);
	if (go_range_increasing (targets, nb_targets)) {
		j = 1;
		for (i = 0; i < nb_targets; i++) {
			while (j <= jmax && targets[i] >= absc[j])
				j++;
			res[i] = ord[j - 1];
		}
	} else {
		int k, l;
		for (i = 0; i < nb_targets; i++) {
			k = jmax;
			if (targets[i] >= absc[jmax])
				res[i] = ord[jmax];
			else {
				j = 0;
				while (k > j + 1) {
					l = (k + j) / 2;
					if (targets[i] >= absc[l])
						j = l;
					else
						k = l;
				}
				if (k != j && targets[i] >= absc[k])
					j = k;
				res[i] = ord[j];
			}
		}
	}
	return res;
}

/*******STAIRCASE AVERAGING*******/

static gnm_float*
staircase_averaging (const gnm_float *absc, const gnm_float *ord, int nb_knots,
		      const gnm_float *targets, int nb_targets)
{
	int i, j, jmax = nb_knots - 1;
	gnm_float *res;
	if (!go_range_increasing (targets, nb_targets))
		return NULL;
	res = g_new (double, nb_targets - 1);
	j = 1;
	while (j <= jmax && targets[0] >= absc[j])
		j++;
	for (i = 1; i < nb_targets; i++) {
		if (targets[i] < absc[j] || j > jmax) {
			res[i - 1] = ord[j - 1];
			continue;
		}
		res[i - 1] = (absc[j] - targets[i - 1]) * ord[j - 1];
		while (j < jmax && targets[i] >= absc[++j])
			res[i - 1] += (absc[j] - absc[j - 1]) * ord[j - 1];
		if (targets[i] >= absc[j])
			j++;
		res[i - 1] += (targets[i] - absc[j - 1]) * ord[j - 1];
		res[i - 1] /= (targets[i] - targets[i - 1]);
	}
	return res;
}

/*******SPLINE INTERPOLATION*******/

static gnm_float*
spline_interpolation (const gnm_float *absc, const gnm_float *ord, int nb_knots,
		      const gnm_float *targets, int nb_targets)
{
	gnm_float *res;
	int i;
	struct GnmCSpline *sp = gnm_cspline_init (absc, ord, nb_knots,
				   GO_CSPLINE_NATURAL, 0., 0.);
	if (!sp)
		return NULL;
	if (go_range_increasing (targets, nb_targets))
		res = gnm_cspline_get_values (sp, targets, nb_targets);
	else {
		res = g_new (double, nb_targets);
		for (i = 0; i < nb_targets; i++)
			res[i] = gnm_cspline_get_value (sp, targets[i]);
	}
	gnm_cspline_destroy (sp);
	return res;
}

/*******SPLINE AVERAGING*******/

static gnm_float*
spline_averaging (const gnm_float *absc, const gnm_float *ord, int nb_knots,
		      const gnm_float *targets, int nb_targets)
{
	gnm_float *res;
	int i, imax;
	struct GnmCSpline *sp;
	if (!go_range_increasing (targets, nb_targets))
		return NULL;
	sp = gnm_cspline_init (absc, ord, nb_knots,
				   GO_CSPLINE_NATURAL, 0., 0.);
	if (!sp)
		return NULL;
	res = gnm_cspline_get_integrals (sp, targets, nb_targets);
	imax = nb_targets - 1;
	for (i = 0; i < imax; i++)
		res[i] /= targets[i + 1] - targets[i];
	gnm_cspline_destroy (sp);
	return res;
}

/*******Interpolation procedure********/

#define INTERPPROC(x) gnm_float* (*x) (const gnm_float*, const gnm_float*, \
					int, const gnm_float*, int)

/******************************************************************************/
/*                    INTERPOLATION FUNCTION                               */
/******************************************************************************/

static GnmFuncHelp const help_interpolation[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=interpolation\n"
	   "@SYNTAX=interpolation(abscissas,ordinates,targets[,interpolation])\n"

	   "@DESCRIPTION= \n"
	   "interpolation returns interpolated values corresponding\n"
	   "to the given abscissa targets as a one column matrix.\n"
	   "\n"
	   "@abscissas are the absicssas of the data to interpolate.\n"
	   "@ordinates are the ordinates of the data to interpolate.\n"
	   "* Strings and empty cells in @abscissas and @ordinates are simply ignored.\n"
	   "@targets are the abscissas of the interpolated data. If several data\n"
	   "are provided, they must be in the same column, in consecutive cells\n"
	   "@interpolation is the method to be used for the interpolation;\n"
	   "possible values are:\n"
	   "- 0: linear;\n"
	   "- 1: linear with averaging;\n"
	   "- 2: staircase;\n"
	   "- 3: staircase with averaging;\n"
	   "- 4: natural cubic spline;\n"
	   "- 5: natural cubic spline with averaging.\n"
	   "\n"
	   "If an averaging method is used, the number of returned values\n"
	   "is one less than the number of targets since the evaluation is made by\n"
	   "averaging the interpolation over the interval between two consecutive data;\n"
	   "in that case, the targets values must be given in increasing order.")
	},
	{ GNM_FUNC_HELP_END }
};

typedef struct {
	guint		alloc_count;
	guint		count;
	guint		data_count;
	gnm_float	*data;
	guint		values_allocated;
	guint		values_count;
	GnmValue	**values;
} collect_floats_t;

static GnmValue *
callback_function_collect (GnmEvalPos const *ep, GnmValue const *value,
			   void *closure)
{
	gnm_float x;
	GnmValue *val = NULL;
	collect_floats_t *cl = (collect_floats_t *) closure;

	if (value == NULL) {
		cl->count++;
		return NULL;
	} else switch (value->type) {
	case VALUE_EMPTY:
		cl->count++;
		return NULL;

	case VALUE_ERROR:
		val = value_dup (value);
		break;

	case VALUE_FLOAT:
		x = value_get_as_float (value);
		if (cl->data_count == cl->alloc_count) {
			cl->alloc_count *= 2;
			cl->data = g_realloc (cl->data, cl->alloc_count * sizeof (gnm_float));
		}

		cl->data[cl->data_count++] = x;
		break;

	default:
		val = value_new_error_VALUE (ep);
	}

	while (cl->count >= cl->values_allocated) {
		cl->values_allocated *= 2;
		cl->values = g_realloc (cl->values, cl->values_allocated * sizeof (GnmValue*));
	}
	while (cl->count > cl->values_count)
		cl->values[cl->values_count++] = value_new_error_NA (ep);
	cl->values[cl->values_count++] = val;
	cl->count++;

	return NULL;
}

static gnm_float *
_collect_floats (int argc, GnmExprConstPtr const *argv,
		GnmEvalPos const *ep, int *n, int *max, GnmValue ***values)
{
	collect_floats_t cl;
	CellIterFlags iter_flags = CELL_ITER_ALL;

	cl.alloc_count = 20;
	cl.data = g_new (gnm_float, cl.alloc_count);
	cl.count = cl.data_count = cl.values_count = 0;
	cl.values_allocated = 20;
	cl.values = g_new (GnmValue*, cl.values_allocated);

	function_iterate_argument_values
		(ep, &callback_function_collect, &cl,
		 argc, argv,
		 FALSE, iter_flags);

	*n = cl.data_count;
	*values = cl.values;
	*max = cl.values_count;
	return cl.data;
}

static GnmValue *
gnumeric_interpolation (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *vals0, *vals1, *vals2, *fres;
	int n0, n1, n2, nb;
	int interp;
	GnmValue *error = NULL;
	GnmValue *res;
	GnmValue **values;
	CollectFlags flags;
	GnmEvalPos const * const ep = ei->pos;
	GnmValue const * const PtInterpolation = argv[2];
	int	r, i;
	GSList *missing0 = NULL;
	GSList *missing1 = NULL;
	INTERPPROC(interpproc) = NULL;

	int const cols = value_area_get_width (PtInterpolation, ep);
	int const rows = value_area_get_height (PtInterpolation, ep);

	/* Collect related variables */
	GnmExpr expr_val;
	GnmExprConstPtr argv_[1] = { &expr_val };
	/* end of collect related variables */

	if (rows == 0 || cols != 1) {
		res = value_new_error_std (ei->pos, GNM_ERROR_VALUE);
		return res;
	}

	flags = COLLECT_IGNORE_BLANKS | COLLECT_IGNORE_STRINGS | COLLECT_IGNORE_BOOLS;

	vals0 = collect_floats_value_with_info (argv[0], ei->pos, flags,
						&n0, &missing0, &error);

	if (error) {
		g_slist_free (missing0);
		return error;
	}

	vals1 = collect_floats_value_with_info (argv[1], ei->pos, flags,
						&n1, &missing1, &error);
	if (error) {
		g_slist_free (missing0);
		g_slist_free (missing1);
		g_free (vals0);
		return error;
	}

	flags |= COLLECT_IGNORE_ERRORS;

	/* start collecting targets */
	gnm_expr_constant_init (&expr_val.constant, argv[2]);
	vals2 = _collect_floats (1, argv_, ei->pos, &n2, &nb, &values);

	if (argv[3]) {
		interp = (int) gnm_floor (value_get_as_float (argv[3]));
		if (interp < 0 || interp > INTERPOLATION_SPLINE_AVG) {
			g_slist_free (missing0);
			g_slist_free (missing1);
			g_free (vals0);
			g_free (vals1);
			return error;
		}
	} else
		interp = INTERPOLATION_LINEAR;

	switch (interp) {
	case INTERPOLATION_LINEAR:
		interpproc = linear_interpolation;
		break;
	case INTERPOLATION_LINEAR_AVG:
		interpproc = linear_averaging;
		n2--;
		break;
	case INTERPOLATION_STAIRCASE:
		interpproc = staircase_interpolation;
		break;
	case INTERPOLATION_STAIRCASE_AVG:
		interpproc = staircase_averaging;
		n2--;
		break;
	case INTERPOLATION_SPLINE:
		interpproc = spline_interpolation;
		break;
	case INTERPOLATION_SPLINE_AVG:
		interpproc = spline_averaging;
		n2--;
		break;
	}

	if (n0 != n1 || n0 == 0 || n2 <= 0) {
		res = value_new_error_std (ei->pos, GNM_ERROR_VALUE);
		for (i = 0; i < nb; i++)
			if (values[i])
				value_release (values[i]);
	} else {
		if (missing0 || missing1) {
			GSList *missing ;
			GArray *gval;

			missing = gnm_slist_sort_merge (missing0, missing1);
			gval = g_array_new (FALSE, FALSE, sizeof (gnm_float));
			gval = g_array_append_vals (gval, vals0, n0);
			g_free (vals0);
			gnm_strip_missing (gval, missing);
			vals0 = (gnm_float *) gval->data;
			n0 = gval->len;
			g_array_free (gval, FALSE);

			gval = g_array_new (FALSE, FALSE, sizeof (gnm_float));
			gval = g_array_append_vals (gval, vals1, n1);
			g_free (vals1);
			gnm_strip_missing (gval, missing);
			vals1 = (gnm_float *) gval->data;
			n1 = gval->len;
			g_array_free (gval, FALSE);

			g_slist_free (missing);

			if (n0 != n1) {
				g_warning ("This should not happen. n0=%d n1=%d\n",
							n0, n1);
			}
		}

		/* here we test if there is abscissas are always increasing, if not,
		an error is returned */
		if (!go_range_increasing (vals0, n0) || n2==0) {
			res = value_new_error_std (ei->pos, GNM_ERROR_VALUE);
			for (i = 0; i < nb; i++)
				if (values[i])
					value_release (values[i]);
			g_free (values);
			g_free (vals0);
			g_free (vals1);
			g_free (vals2);
			return res;
		}
		res = value_new_array_non_init (1 , nb);
		i = 0;

		res->v_array.vals[0] = g_new (GnmValue *, nb);

		fres = interpproc (vals0, vals1, n0, vals2, n2);
		if (fres) {
			i = 0;
			for( r = 0 ; r < nb; ++r)
				if (values[r])
					res->v_array.vals[0][r] = values[r];
				else {
					res->v_array.vals[0][r] = value_new_float (fres[i++]);
				}
			g_free (fres);
		} else {
			for( r = 0 ; r < nb; ++r)
				res->v_array.vals[0][r] = value_new_error_std (ei->pos, GNM_ERROR_VALUE);
			for (i = 0; i < nb; i++)
				if (values[i])
					value_release (values[i]);
		}
	}
	g_free (values);
	g_free (vals0);
	g_free (vals1);
	g_free (vals2);
	return res;
}

/*********************************************************************************************************/

/******************************************************************************/
/*                    PERIODOGRAM WITH INTERPOLATION FUNCTION                 */
/******************************************************************************/

static GnmFuncHelp const help_periodogram[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=periodogram\n"
	   "@SYNTAX=periodogram(ordinates,[filter,[abscissas,[interpolation,[number]]]])\n"

	   "@DESCRIPTION= \n"
	   "periodogram returns the periodogram of the data\n"
	   "as a one column matrix.\n"
	   "\n"
	   "@ordinates are the ordinates of the data to interpolate.\n"
	   "@filter gives the window function to  be used. Possible values are:\n"
	   "- 0: no filter (rectangular window);\n"
	   "- 1: Bartlett (triangular window);\n"
	   "- 2: Hahn (cosine window);\n"
	   "- 3: Welch (parabolic window);\n"
	   "@abscissas are the absicssas of the data to interpolate. If no\n"
	   "abscissa is given, it is supposed that the data absicssas are regularly\n"
	   "spaced. Otherwise, an interpolation method will be used to evaluate\n"
	   "regularly spaced data.\n"
	   "* Strings and empty cells in @abscissas and @ordinates are simply ignored.\n"
	   "@interpolation is the method to be used for the interpolation;\n"
	   "possible values are:\n"
	   "- 0: linear;\n"
	   "- 1: linear with averaging;\n"
	   "- 2: staircase;\n"
	   "- 3: staircase with averaging;\n"
	   "- 4: natural cubic spline;\n"
	   "- 5: natural cubic spline with averaging.\n"
	   "@number is the number of interpolated data to be used. If not given,\n"
	   "a default number is automatically evaluated.\n")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_periodogram (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *ord, *absc;
	int filter, interp;
	int n0, n1, nb;
	GnmValue *error = NULL;
	GnmValue *res;
	CollectFlags flags;
	GnmEvalPos const * const ep = ei->pos;
	GnmValue const * const Pt = argv[0];
	int i;
	GSList *missing0 = NULL, *missing1 = NULL;
	complex_t *in, *out = NULL;

	int const cols = value_area_get_width (Pt, ep);
	int const rows = value_area_get_height (Pt, ep);

	if (cols == 1)
		nb=rows;
	else {
		if (rows == 1)
			nb=cols;
		else
			nb=0;
	}

	if (nb == 0) {
		res = value_new_error_std (ei->pos, GNM_ERROR_VALUE);
		return res;
	}

	flags=COLLECT_IGNORE_BLANKS | COLLECT_IGNORE_STRINGS | COLLECT_IGNORE_BOOLS;

	ord = collect_floats_value_with_info (argv[0], ei->pos, flags,
				      &n0, &missing0, &error);
	if (error) {
		g_slist_free (missing0);
		return error;
	}

	if (n0 == 0) {
		res = value_new_error_std (ei->pos, GNM_ERROR_VALUE);
		return res;
	}

	if (argv[1]) {
		filter = (int) gnm_floor (value_get_as_float (argv[1]));
		if (filter < 0 || filter > FILTER_WELCH) {
			g_slist_free (missing0);
			g_free (ord);
			res = value_new_error_std (ei->pos, GNM_ERROR_VALUE);
			return res;
		}
	} else
		filter = FILTER_NONE;

	if (argv[2]) {
		gnm_float *interpolated, start, incr;
		INTERPPROC(interpproc) = NULL;
		absc = collect_floats_value_with_info (argv[2], ei->pos, flags,
						&n1, &missing1, &error);
		if (error) {
			g_slist_free (missing0);
			g_slist_free (missing1);
			g_free (absc);
			return error;
		}
		if (n1 == 0) {
			g_slist_free (missing0);
			g_slist_free (missing1);
			g_free (absc);
			g_free (ord);
			return value_new_error_std (ei->pos, GNM_ERROR_VALUE);
		}
		if (argv[3]) {
			interp = (int) gnm_floor (value_get_as_float (argv[3]));
			if (interp < 0 || interp > INTERPOLATION_SPLINE_AVG) {
				g_slist_free (missing0);
				g_slist_free (missing1);
				g_free (absc);
				g_free (ord);
				return error;
			}
		} else
			interp = INTERPOLATION_LINEAR;

		if (missing0 || missing1) {
			GSList *missing ;
			GArray *gval;

			missing = gnm_slist_sort_merge (missing0, missing1);
			gval = g_array_new (FALSE, FALSE, sizeof (gnm_float));
			gval = g_array_append_vals (gval, ord, n0);
			g_free (ord);
			gnm_strip_missing (gval, missing);
			ord = (gnm_float *) gval->data;
			n0 = gval->len;
			g_array_free (gval, FALSE);

			gval = g_array_new (FALSE, FALSE, sizeof (gnm_float));
			gval = g_array_append_vals (gval, absc, n1);
			g_free (absc);
			gnm_strip_missing (gval, missing);
			absc = (gnm_float *) gval->data;
			n1 = gval->len;
			g_array_free (gval, FALSE);

			g_slist_free (missing);

			if (n0 != n1)
				g_warning ("This should not happen. n0=%d n1=%d\n",
							n0, n1);
		}
		/* here we test if there is abscissas are always increasing, if not,
		an error is returned */
		if (!go_range_increasing (absc, n0) || n0 == 0) {
			g_free (absc);
			g_free (ord);
			return value_new_error_std (ei->pos, GNM_ERROR_VALUE);
		}
		if (argv[4]) {
			n1 = (int) gnm_floor (value_get_as_float (argv[4]));
			if (n1 < n0) {
				g_free (absc);
				g_free (ord);
				return value_new_error_std (ei->pos, GNM_ERROR_VALUE);
			}
			nb = 1;
			while (nb < n1)
				nb *= 2;
		} else {
			n1 = 1;
			while (n1 < n0)
				n1 *= 2;
			nb = n1;
		}
		incr = (absc[n0 - 1] - absc[0]) / n1;
		switch (interp) {
		case INTERPOLATION_LINEAR:
			interpproc = linear_interpolation;
			start = absc[0];
			break;
		case INTERPOLATION_LINEAR_AVG:
			interpproc = linear_averaging;
			start = absc[0] - incr / 2.;
			n1++;
			break;
		case INTERPOLATION_STAIRCASE:
			interpproc = staircase_interpolation;
			start = absc[0];
			break;
		case INTERPOLATION_STAIRCASE_AVG:
			interpproc = linear_averaging;
			start = absc[0] - incr / 2.;
			n1++;
			break;
		case INTERPOLATION_SPLINE:
			interpproc = spline_interpolation;
			start = absc[0];
			break;
		case INTERPOLATION_SPLINE_AVG:
			interpproc = linear_averaging;
			start = absc[0] - incr / 2.;
			n1++;
			break;
		default:
			g_free (absc);
			g_free (ord);
			return value_new_error_std (ei->pos, GNM_ERROR_NA);
		}
		interpolated = g_new (gnm_float, n1);
		for (i = 0; i < n1; i++)
			interpolated[i] = start + i * incr;
		g_free (ord);
		ord = interpproc (absc, ord, n0, interpolated, n1);
		n0 = nb;
	} else {
		/* we have no interpolation to apply, so just take the values */
		if (missing0) {
			GArray *gval;

			gval = g_array_new (FALSE, FALSE, sizeof (gnm_float));
			gval = g_array_append_vals (gval, ord, n0);
			g_free (ord);
			gnm_strip_missing (gval, missing0);
			ord = (gnm_float *) gval->data;
			n0 = gval->len;
			g_array_free (gval, FALSE);

			g_slist_free (missing0);
		}
		nb = 1;
		while (nb < n0)
			nb *= 2;
	}

	/* Now apply the filter if any */
	if (filter != FILTER_NONE) {
		gnm_float factor;
		switch (filter) {
		case FILTER_BARTLETT:
			factor = n0 / 2.;
			for (i = 0; i < n0; i++)
				ord[i] *= 1. - gnm_abs ((i / factor - 1));
			break;
		case FILTER_HANN:
			factor = 2. * M_PIgnum / n0;
			for (i = 0; i < n0; i++)
				ord[i] *= 0.5 * (1 - cos (factor * i));
			break;
		case FILTER_WELCH:
			factor = n0 / 2.;
			for (i = 0; i < n0; i++)
				ord[i] *= 1. - (i / factor - 1.) * (i / factor - 1.);
			break;
		}
	}

	/* Transform and return the result */
	in = g_new0 (complex_t, nb);
	for (i = 0; i < n0; i++)
	     in[i].re = ord[i];
	g_free (ord);
	gnm_fourier_fft (in, nb, 1, &out, FALSE);
	g_free (in);
	nb /= 2;
	if (out) {
		res = value_new_array_non_init (1 , nb);
		res->v_array.vals[0] = g_new (GnmValue *, nb);
		for (i = 0; i < nb; i++)
			res->v_array.vals[0][i] =
				value_new_float (gnm_sqrt (
					out[i].re * out[i].re +
					out[i].im * out[i].im));
		g_free (out);
	} else
		res = value_new_error_std (ei->pos, GNM_ERROR_VALUE);

	return res;
}

const GnmFuncDescriptor TimeSeriesAnalysis_functions[] = {

        { "interpolation",       "AAA|f",   N_("Abscissas,Ordinates,Targets,Interpolation"),
	  help_interpolation, gnumeric_interpolation, NULL, NULL, NULL, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "periodogram",       "A|fAff",   N_("Ordinates,Filter,Abscissas,Interpolation,Number"),
	  help_periodogram, gnumeric_periodogram, NULL, NULL, NULL, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        {NULL}
};
