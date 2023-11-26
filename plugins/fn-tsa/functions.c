/*
 * fn-tsa plugin
 * functions.c
 *
 * Copyright (C) 2006 Laurency Franck
 * Copyright (C) 2007 Jean Bréfort <jean.brefort@normalesup.org>
 * Copyright (C) 2009 Andreas Guelzow <aguelzow@pyrshep.ca>
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>
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
#include <func-builtin.h>
#include <gnm-i18n.h>
#include <goffice/goffice.h>
#include <gnm-plugin.h>
#include <tools/analysis-tools.h>
#include <complex.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

GNM_PLUGIN_MODULE_HEADER;

enum {
	FILTER_NONE,
	FILTER_BARTLETT,
	FILTER_HANN,
	FILTER_WELCH,
	FILTER_GAUSSIAN,
};

enum {
	INTERPOLATION_LINEAR = 0,
	INTERPOLATION_LINEAR_AVG,
	INTERPOLATION_STAIRCASE,
	INTERPOLATION_STAIRCASE_AVG,
	INTERPOLATION_SPLINE,
	INTERPOLATION_SPLINE_AVG,
};

#define GnmCSpline GNM_SUFFIX(GOCSpline)
#define gnm_cspline_init GNM_SUFFIX(go_cspline_init)
#define gnm_cspline_destroy GNM_SUFFIX(go_cspline_destroy)
#define gnm_cspline_get_value GNM_SUFFIX(go_cspline_get_value)
#define gnm_cspline_get_values GNM_SUFFIX(go_cspline_get_values)
#define gnm_cspline_get_integrals GNM_SUFFIX(go_cspline_get_integrals)

#define INTERPOLATIONMETHODS { GNM_FUNC_HELP_DESCRIPTION, F_("Possible interpolation methods are:\n" \
							     "0: linear;\n" \
							     "1: linear with averaging;\n" \
							     "2: staircase;\n" \
							     "3: staircase with averaging;\n" \
							     "4: natural cubic spline;\n" \
							     "5: natural cubic spline with averaging.") }

/**************************************************************************/
/**************************************************************************/
/*           CALCULATION FUNCTIONS FOR TOOLS                                */
/**************************************************************************/
/**************************************************************************/

static void
gnm_fourier_fft (gnm_complex const *in, int n, int skip, gnm_complex **fourier, gboolean inverse)
{
	gnm_complex  *fourier_1, *fourier_2;
	int        i;
	int        nhalf = n / 2;
	gnm_float argstep;

	*fourier = g_new (gnm_complex, n);

	if (n == 1) {
		(*fourier)[0] = in[0];
		return;
	}

	gnm_fourier_fft (in, nhalf, skip * 2, &fourier_1, inverse);
	gnm_fourier_fft (in + skip, nhalf, skip * 2, &fourier_2, inverse);

	argstep = (inverse ? M_PIgnum : -M_PIgnum) / nhalf;
	for (i = 0; i < nhalf; i++) {
		gnm_complex dir, tmp;

		dir = GNM_CPOLAR (1, argstep * i);
		tmp = GNM_CMUL (fourier_2[i], dir);

		(*fourier)[i] = GNM_CSCALE (GNM_CADD (fourier_1[i], tmp), 0.5);

		(*fourier)[i + nhalf] = GNM_CSCALE (GNM_CSUB (fourier_1[i], tmp), 0.5);
	}

	g_free (fourier_1);
	g_free (fourier_2);
}


/*******LINEAR INTERPOLATION*******/

static gnm_float*
linear_interpolation (const gnm_float *absc, const gnm_float *ord, int nb_knots,
		      const gnm_float *targets, int nb_targets)
{
	int i, j, k, jmax = nb_knots - 1;
	gnm_float slope, *res;
	if (nb_knots < 2)
		return NULL;
	res = g_new (gnm_float, nb_targets);
	if (gnm_range_increasing (targets, nb_targets)) {
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
	if (nb_knots < 2 || !gnm_range_increasing (targets, nb_targets + 1))
		return NULL;
	res = g_new (gnm_float, nb_targets);
	j = 1;
	while (j < jmax && targets[0] > absc[j])
		j++;
	k = j - 1;
	slope = (ord[j] - ord[k]) / (absc[j] - absc[k]) / 2;
	for (i = 1; i <= nb_targets; i++) {
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
			slope = (ord[j] - ord[k]) / x0 / 2;
			res[i - 1] += x0 * (slope * x0 + ord[k]);
		}
		if (j > k + 1) {
			k = j - 1;
			slope = (ord[j] - ord[k]) / (absc[j] - absc[k]) / 2;
		} else
			k = j;
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

	if (nb_knots <= 0)
		return NULL;

	res = g_new (gnm_float, nb_targets);
	if (gnm_range_increasing (targets, nb_targets)) {
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


	if (nb_knots <= 0)
		return NULL;

	if (!gnm_range_increasing (targets, nb_targets + 1))
		return NULL;
	res = g_new (gnm_float, nb_targets);
	j = 1;
	while (j <= jmax && targets[0] >= absc[j])
		j++;
	for (i = 1; i <= nb_targets; i++) {
		if (j > jmax || targets[i] < absc[j]) {
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
	GnmCSpline *sp = gnm_cspline_init (absc, ord, nb_knots,
					   GO_CSPLINE_NATURAL, 0., 0.);
	if (!sp)
		return NULL;
	if (gnm_range_increasing (targets, nb_targets))
		res = gnm_cspline_get_values (sp, targets, nb_targets);
	else {
		res = g_new (gnm_float, nb_targets);
		for (i = 0; i < nb_targets; i++)
			res[i] = gnm_cspline_get_value (sp, targets[i]);
	}
	gnm_cspline_destroy (sp);
	return res;
}

/*******SPLINE AVERAGING*******/

static gnm_float *
spline_averaging (const gnm_float *absc, const gnm_float *ord, int nb_knots,
		  const gnm_float *targets, int nb_targets)
{
	gnm_float *res;
	int i, imax;
	GnmCSpline *sp;
	if (!gnm_range_increasing (targets, nb_targets + 1))
		return NULL;
	sp = gnm_cspline_init (absc, ord, nb_knots,
			       GO_CSPLINE_NATURAL, 0., 0.);
	if (!sp)
		return NULL;
	res = gnm_cspline_get_integrals (sp, targets, nb_targets + 1);
	imax = nb_targets;
	for (i = 0; i < imax; i++)
		res[i] /= targets[i + 1] - targets[i];
	gnm_cspline_destroy (sp);
	return res;
}

/*******Interpolation procedure********/

typedef  gnm_float* (*INTERPPROC) (const gnm_float*, const gnm_float*,
				       int, const gnm_float*, int);

/******************************************************************************/
/*                    INTERPOLATION FUNCTION                               */
/******************************************************************************/

static GnmFuncHelp const help_interpolation[] = {
	{ GNM_FUNC_HELP_NAME, F_("INTERPOLATION:interpolated values corresponding to the given abscissa targets") },
	{ GNM_FUNC_HELP_ARG, F_("abscissae:abscissae of the given data points") },
	{ GNM_FUNC_HELP_ARG, F_("ordinates:ordinates of the given data points") },
	{ GNM_FUNC_HELP_ARG, F_("targets:abscissae of the interpolated data") },
	{ GNM_FUNC_HELP_ARG, F_("interpolation:method of interpolation, defaults to 0 (\'linear\')") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The output consists always of one column of numbers.") },
	INTERPOLATIONMETHODS,
	{ GNM_FUNC_HELP_NOTE, F_("The @{abscissae} should be given in increasing order. If the @{abscissae} is not in "
				 "increasing order the INTERPOLATION function is significantly slower.") },
	{ GNM_FUNC_HELP_NOTE, F_("If any two @{abscissae} values are equal an error is returned.") },
	{ GNM_FUNC_HELP_NOTE, F_("If any of interpolation methods 1 ('linear with averaging'), 3 "
				 "('staircase with averaging'), and 5 ('natural cubic spline with "
				 "averaging') is used, the number "
				 "of returned values is one less than the number of targets and the target "
				 "values must be given in increasing order. The values returned "
				 "are the average heights of the interpolation function on the intervals "
				 "determined by consecutive target values.") },
	{ GNM_FUNC_HELP_NOTE, F_("Strings and empty cells in @{abscissae} and @{ordinates} are ignored.") },
	{ GNM_FUNC_HELP_NOTE, F_("If several target data are provided they must be in the same column in consecutive cells.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=interpolation(array(1,2,3),array(10,20,20),1.5,0)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=interpolation(array(1,2,3),array(10,20,20),array(1.5,4),1)" },
	{ GNM_FUNC_HELP_SEEALSO, "PERIODOGRAM" },
	{ GNM_FUNC_HELP_END, NULL }
};

static GnmValue *
gnumeric_interpolation (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *vals0, *vals1, *vals2, *fres;
	int n0, n2;
	int interp;
	GnmValue *error = NULL;
	GnmValue *res;
	CollectFlags flags;
	GnmEvalPos const * const ep = ei->pos;
	GnmValue const * const PtInterpolation = argv[2];
	int r, i;
	GSList *missing2 = NULL, *missing;
	INTERPPROC interpproc = NULL;
	gboolean constp = FALSE;

	/* argv[2] */

	int const cols = value_area_get_width (PtInterpolation, ep);
	int const rows = value_area_get_height (PtInterpolation, ep);

	if (rows == 0 || cols != 1) {
		res = value_new_error_std (ei->pos, GNM_ERROR_VALUE);
		return res;
	}

	flags = COLLECT_IGNORE_BLANKS | COLLECT_IGNORE_STRINGS | COLLECT_IGNORE_BOOLS | COLLECT_IGNORE_ERRORS;
	vals2 = collect_floats_value_with_info (PtInterpolation, ei->pos, flags,
						&n2, &missing2, &error);
	if (error) {
		g_slist_free (missing2);
		return error;
	}

	/* argv[3] */

	if (argv[3]) {
		interp = (int) gnm_floor (value_get_as_float (argv[3]));
		if (interp < 0 || interp > INTERPOLATION_SPLINE_AVG) {
			g_slist_free (missing2);
			g_free (vals2);
			return value_new_error_VALUE (ei->pos);
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

	if (n2 <= 0) {
		g_slist_free (missing2);
		g_free (vals2);
		return value_new_error_std (ei->pos, GNM_ERROR_VALUE);
	}

	/* argv[0] & argv[1] */

	flags = COLLECT_IGNORE_BLANKS | COLLECT_IGNORE_STRINGS | COLLECT_IGNORE_BOOLS;
	error = collect_float_pairs (argv[0], argv[1], ei->pos, flags, &vals0, &vals1,
				     &n0, &constp);

	if (error) {
		g_slist_free (missing2);
		g_free (vals2);
		return error;
	}

	/* Check whether the abscissae are increasing, if not order them */
	if (!gnm_range_increasing (vals0, n0)) {
		gboolean switched = TRUE;
		if (constp) {
			vals0 = go_memdup_n (vals0, n0, sizeof(gnm_float));
			vals1 = go_memdup_n (vals1, n0, sizeof(gnm_float));
			constp = FALSE;
		}
		while (switched) {
			gnm_float *val;
			switched = FALSE;
			for (i = 1, val = vals0; i < n0; i++, val++) {
				if (*val == *(val + 1)) {
					res = value_new_error_std (ei->pos, GNM_ERROR_VALUE) ;
					goto done;
				}
				if (*val > *(val + 1)) {
					gnm_float v = *val;
					*val = *(val + 1);
					*(val + 1) = v;
					v = *(vals1 + i);
					*(vals1 + i) = *(vals1 + i - 1);
					*(vals1 + i - 1) = v;
					switched = TRUE;
				}
			}
		}
	}

	{
		int n = n2;

		if (missing2)
			gnm_strip_missing (vals2, &n, missing2);
		res = value_new_array_non_init (1 , n2);
		i = 0;

		res->v_array.vals[0] = g_new (GnmValue *, n2);

		fres = interpproc (vals0, vals1, n0, vals2, n);
		missing = missing2;
		if (fres) {
			i = 0;
			for (r = 0 ; r < n2; ++r)
				if (missing && r == GPOINTER_TO_INT (missing->data)) {
					missing = missing->next;
					res->v_array.vals[0][r] = value_new_error_std (ei->pos, GNM_ERROR_VALUE);
				} else
					res->v_array.vals[0][r] = value_new_float (fres[i++]);
			g_free (fres);
		} else {
			for( r = 0 ; r < n2; ++r)
				res->v_array.vals[0][r] = value_new_error_std (ei->pos, GNM_ERROR_VALUE);
		}

	}

 done:
	g_slist_free (missing2);
	if (!constp) {
		g_free (vals0);
		g_free (vals1);
	}
	g_free (vals2);
	return res;
}

/*********************************************************************************************************/

/******************************************************************************/
/*                    PERIODOGRAM WITH INTERPOLATION FUNCTION                 */
/******************************************************************************/

static GnmFuncHelp const help_periodogram[] = {
	{ GNM_FUNC_HELP_NAME, F_("PERIODOGRAM:periodogram of the given data") },
	{ GNM_FUNC_HELP_ARG, F_("ordinates:ordinates of the given data") },
	{ GNM_FUNC_HELP_ARG, F_("filter:windowing function to be used, defaults to no filter") },
	{ GNM_FUNC_HELP_ARG, F_("abscissae:abscissae of the given data, defaults to regularly spaced abscissae") },
	{ GNM_FUNC_HELP_ARG, F_("interpolation:method of interpolation, defaults to none") },
	{ GNM_FUNC_HELP_ARG, F_("number:number of interpolated data points") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("If an interpolation method is used, the number of returned values is one less than the number of targets and the targets values must be given in increasing order.") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The output consists always of one column of numbers.") },
	INTERPOLATIONMETHODS,
	{ GNM_FUNC_HELP_DESCRIPTION, F_("Possible window functions are:\n"
					"0: no filter (rectangular window)\n"
					"1: Bartlett (triangular window)\n"
					"2: Hahn (cosine window)\n"
					"3: Welch (parabolic window)") },
	{ GNM_FUNC_HELP_NOTE, F_("Strings and empty cells in @{abscissae} and @{ordinates} are ignored.") },
	{ GNM_FUNC_HELP_NOTE, F_("If several target data are provided they must be in the same column in consecutive cells.") },
	{ GNM_FUNC_HELP_SEEALSO, "INTERPOLATION" },
	{ GNM_FUNC_HELP_END, NULL }
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
	gnm_complex *in, *out = NULL;

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
		gnm_float *interpolated, *new_ord, start, incr;
		int n2;
		INTERPPROC(interpproc) = NULL;
		absc = collect_floats_value_with_info (argv[2], ei->pos, flags,
						       &n1, &missing1, &error);
		if (n1 == 1) {
			g_slist_free (missing1);
			g_free (absc);
			goto no_absc;
		}
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
			GSList *missing = gnm_slist_sort_merge (missing0, missing1);
			gnm_strip_missing (ord, &n0, missing);
			gnm_strip_missing (absc, &n1, missing);
			g_slist_free (missing);

			if (n0 != n1)
				g_warning ("This should not happen. n0=%d n1=%d\n",
					   n0, n1);
		}
		n0 = n1 = MIN (n0, n1);
		/* here we test if there is abscissas are always increasing, if not,
		   an error is returned */
		if (n0 < 2 || !gnm_range_increasing (absc, n0) || n0 == 0) {
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
			n2 = n1;
			break;
		case INTERPOLATION_LINEAR_AVG:
			interpproc = linear_averaging;
			start = absc[0] - incr / 2;
			n2 = n1 + 1;
			break;
		case INTERPOLATION_STAIRCASE:
			interpproc = staircase_interpolation;
			start = absc[0];
			n2 = n1;
			break;
		case INTERPOLATION_STAIRCASE_AVG:
			interpproc = staircase_averaging;
			start = absc[0] - incr / 2;
			n2 = n1 + 1;
			break;
		case INTERPOLATION_SPLINE:
			interpproc = spline_interpolation;
			start = absc[0];
			n2 = n1;
			break;
		case INTERPOLATION_SPLINE_AVG:
			interpproc = spline_averaging;
			start = absc[0] - incr / 2;
			n2 = n1 + 1;
			break;
		default:
			g_free (absc);
			g_free (ord);
			return value_new_error_std (ei->pos, GNM_ERROR_NA);
		}
		interpolated = g_new (gnm_float, n1);
		for (i = 0; i < n2; i++)
			interpolated[i] = start + i * incr;
		new_ord = interpproc (absc, ord, n0, interpolated, n1);
		g_free (ord);
		ord = new_ord;
		if (ord == NULL) {
			g_free (absc);
			g_free (interpolated);
			return value_new_error_std (ei->pos, GNM_ERROR_NA);
		}
		n0 = n1;
	} else {
no_absc:
		/* we have no interpolation to apply, so just take the values */
		if (missing0) {
			gnm_strip_missing (ord, &n0, missing0);
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
			factor = n0 / 2;
			for (i = 0; i < n0; i++)
				ord[i] *= 1 - gnm_abs ((i / factor - 1));
			break;
		case FILTER_HANN:
			factor = 2 * M_PIgnum / n0;
			for (i = 0; i < n0; i++)
				ord[i] *= GNM_const(0.5) * (1 - gnm_cos (factor * i));
			break;
		case FILTER_WELCH:
			factor = n0 / 2;
			for (i = 0; i < n0; i++)
				ord[i] *= 1 - (i / factor - 1) * (i / factor - 1);
			break;
		}
	}

	/* Transform and return the result */
	in = g_new0 (gnm_complex, nb);
	for (i = 0; i < n0; i++){
		in[i].re = ord[i];
	}
	g_free (ord);
	gnm_fourier_fft (in, nb, 1, &out, FALSE);
	g_free (in);
	nb /= 2;
	if (out && nb > 0) {
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

/******************************************************************************/
/*                    Fourier Transform                                       */
/******************************************************************************/

static GnmFuncHelp const help_fourier[] = {
	{ GNM_FUNC_HELP_NAME, F_("FOURIER:Fourier or inverse Fourier transform") },
	{ GNM_FUNC_HELP_ARG, F_("Sequence:the data sequence to be transformed") },
	{ GNM_FUNC_HELP_ARG, F_("Inverse:if true, the inverse Fourier transform is calculated, defaults to false") },
	{ GNM_FUNC_HELP_ARG, F_("Separate:if true, the real and imaginary parts are given separately, defaults to false") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This array function returns the Fourier or inverse Fourier transform of the given data sequence.") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The output consists of one column of complex numbers if @{Separate} is false and of two columns of real numbers if @{Separate} is true.") },
{ GNM_FUNC_HELP_DESCRIPTION, F_("If @{Separate} is true the first output column contains the real parts and the second column the imaginary parts.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{Sequence} is neither an n by 1 nor 1 by n array, this function returns #VALUE!") },
	{ GNM_FUNC_HELP_END, NULL }
};

static GnmValue *
gnumeric_fourier (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *ord;
	gboolean inverse = FALSE;
	gboolean sep_columns = FALSE;
	int n0, nb;
	GnmValue *error = NULL;
	GnmValue *res;
	CollectFlags flags;
	GnmEvalPos const * const ep = ei->pos;
	GnmValue const * const Pt = argv[0];
	int i;
	GSList *missing0 = NULL;
	gnm_complex *in, *out = NULL;

	int const cols = value_area_get_width (Pt, ep);
	int const rows = value_area_get_height (Pt, ep);

	if (cols != 1 && rows != 1) {
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
		inverse = 0 != (int) gnm_floor (value_get_as_float (argv[1]));
		if (argv[2]) {
			sep_columns = (0 != (int)
				       gnm_floor (value_get_as_float (argv[2])));
		}
	}

	if (missing0) {
		gnm_strip_missing (ord, &n0, missing0);
		g_slist_free (missing0);
	}

	nb = 1;
	while (nb < n0)
		nb *= 2;

	/* Transform and return the result */
	in = g_new0 (gnm_complex, nb);
	for (i = 0; i < n0; i++)
		in[i].re = ord[i];
	g_free (ord);
	gnm_fourier_fft (in, nb, 1, &out, inverse);
	g_free (in);

	if (out && !sep_columns) {
		res = value_new_array_empty (1 , nb);
		for (i = 0; i < nb; i++)
			res->v_array.vals[0][i] = value_new_string_nocopy
				(gnm_complex_to_string (&(out[i]), 'i'));
		g_free (out);
	} else if (out && sep_columns) {
		res = value_new_array_empty (2 , nb);
		for (i = 0; i < nb; i++) {
			res->v_array.vals[0][i] = value_new_float (out[i].re);
			res->v_array.vals[1][i] = value_new_float (out[i].im);
		}
		g_free (out);
	} else
		res = value_new_error_std (ei->pos, GNM_ERROR_VALUE);

	return res;
}

/******************************************************************************/

static GnmFuncHelp const help_hpfilter[] = {
	{ GNM_FUNC_HELP_NAME, F_("HPFILTER:Hodrick Prescott Filter") },
	{ GNM_FUNC_HELP_ARG, F_("Sequence:the data sequence to be transformed") },
	{ GNM_FUNC_HELP_ARG, F_("\316\273:filter parameter \316\273, defaults to 1600") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This array function returns the trend and cyclical components obtained by applying the Hodrick Prescott Filter with parameter @{\316\273} to the given data sequence.") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The output consists of two columns of numbers, the first containing the trend component, the second the cyclical component.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{Sequence} is neither an n by 1 nor 1 by n array, this function returns #VALUE!") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{Sequence} contains less than 6 numerical values, this function returns #VALUE!") },
	{ GNM_FUNC_HELP_END, NULL }
};


static void
gnm_hpfilter (gnm_float *data, int n, gnm_float lambda, int *err)
{
 	gnm_float *a, *b, *c;
 	int i;
 	gnm_float lambda6 = 6 * lambda + 1;
 	gnm_float lambda4 = -4 * lambda;
 	gnm_float h[5] = {0,0,0,0,0};
 	gnm_float g[5] = {0,0,0,0,0};
 	gnm_float j[2] = {0,0};
 	gnm_float h_b, h_c, denom;

 	g_return_if_fail (n > 5);
 	g_return_if_fail (data != NULL);
 	g_return_if_fail (err != NULL);

 	/* Initializing arrays a, b, and c */

 	a = g_new (gnm_float, n);
 	b = g_new (gnm_float, n);
 	c = g_new (gnm_float, n);

 	a[0] = lambda + 1;
 	b[0] = -2 * lambda;
 	c[0] = lambda;

 	for (i = 1; i < n - 2; i++) {
 		a[i] = lambda6;
 		b[i] = lambda4;
 		c[i] = lambda;
 	}

 	a[n - 2] = a[1] = lambda6 - lambda;
 	a[n - 1] = a[0];
 	b[n - 2] = b[0];
 	b[n - 1] = 0;
 	c[n - 2] = 0;
 	c[n - 1] = 0;

 	/* Forward */
 	for (i = 0; i < n; i++) {
 		denom = a[i]- h[3]*h[0] - g[4]*g[1];
 		if (denom == 0) {
 			*err = GNM_ERROR_DIV0;
 			goto done;
 		}

 		h_b = b[i];
 		g[0] = h[0];
 		b[i] = h[0] = (h_b - h[3] * h[1])/denom;

 		h_c = c[i];
 		g[1] = h[1];
 		c[i] = h[1] = h_c/denom;

 		a[i] = (data[i] - g[2]*g[4] - h[2]*h[3])/denom;

 		g[2] = h[2];
 		h[2] = a[i];
 		h[3] = h_b - h[4] * g[0];
 		g[4] = h[4];
 		h[4] = h_c;
 	}

 	data[n - 1] = j[0] = a[n - 1];

 	/* Backwards */
 	for (i = n - 1; i > 0; i--) {
 		data[i - 1] = a[i - 1] - b[i - 1] * j[0] - c[i - 1] * j[1];
 		j[1] = j[0];
 		j[0] = data[i - 1];
 	}

 done:
 	g_free (a);
 	g_free (b);
 	g_free (c);
 	return;
}

static GnmValue *
gnumeric_hpfilter (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *raw, *filtered;
	gnm_float lambda;
	int n = 0;
	GnmValue *error = NULL;
	GnmValue *res;
	CollectFlags flags;
	GnmEvalPos const * const ep = ei->pos;
	GnmValue const * const Pt = argv[0];
	int i, err = -1;

	int const cols = value_area_get_width (Pt, ep);
	int const rows = value_area_get_height (Pt, ep);

	if (cols != 1 && rows != 1) {
		res = value_new_error_std (ei->pos, GNM_ERROR_VALUE);
		return res;
	}

	flags=COLLECT_IGNORE_BLANKS | COLLECT_IGNORE_STRINGS | COLLECT_IGNORE_BOOLS;

	raw = collect_floats_value (argv[0], ei->pos, flags,
					      &n, &error);
	if (error)
		return error;

	if (n < 6) {
		g_free (raw);
		res = value_new_error_std (ei->pos, GNM_ERROR_VALUE);
		return res;
	}

	if (argv[1])
		lambda = value_get_as_float (argv[1]);
	else
		lambda = 1600.;


	/* Filter and return the result */
	filtered = g_new0 (gnm_float, n);
	for (i = 0; i < n; i++)
		filtered[i] = raw[i];
	gnm_hpfilter (filtered, n, lambda, &err);
	if (err > -1) {
		g_free (raw);
		g_free (filtered);
		res = value_new_error_std (ei->pos, err);
		return res;
	}

	res = value_new_array_empty (2 , n);
	for (i = 0; i < n; i++) {
		res->v_array.vals[0][i] = value_new_float (filtered[i]);
		res->v_array.vals[1][i] = value_new_float (raw[i] - filtered[i]);
	}
	g_free (raw);
	g_free (filtered);

	return res;
}

const GnmFuncDescriptor TimeSeriesAnalysis_functions[] = {

        { "interpolation",       "AAA|f",
	  help_interpolation, gnumeric_interpolation, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "periodogram",       "A|fAff",
	  help_periodogram, gnumeric_periodogram, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "fourier",       "A|bb",
	  help_fourier, gnumeric_fourier, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "hpfilter",       "A|f",
	  help_hpfilter, gnumeric_hpfilter, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{NULL}
};
