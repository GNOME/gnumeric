/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* gsl-pdf.c: Probability density functions from the GNU Scientific library
 *            version 1.1.1.
 * 
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <math.h>
#include <value.h>


gnum_float
random_binomial_pdf (int k, gnum_float p, int n)
{
        if (k > n)
	        return 0;
	else {
	        gnum_float a   = k;
		gnum_float b   = n - k;
		gnum_float Cnk = combin (n, k) ;

		return Cnk * powgnum (p, a) * powgnum (1 - p, b);
      	}
}

gnum_float
random_chisq_pdf (gnum_float x, gnum_float nu)
{
        if (x <= 0)
	        return 0;
	else {
	        gnum_float p;
		gnum_float lngamma = lgamma (nu / 2);
      
		return expgnum ((nu / 2 - 1) * loggnum (x / 2)
				- x / 2 - lngamma) / 2;
	}
}

gnum_float
random_fdist_pdf (gnum_float x, gnum_float nu1, gnum_float nu2)
{
        if (x < 0)
	        return 0;
	else {
	        gnum_float p;
		gnum_float lglg = (nu1 / 2) * loggnum (nu1)
		        + (nu2 / 2) * loggnum (nu2) ;
		gnum_float lg12 = lgamma ((nu1 + nu2) / 2);
		gnum_float lg1  = lgamma (nu1 / 2);
		gnum_float lg2  = lgamma (nu2 / 2);
      
		return expgnum (lglg + lg12 - lg1 - lg2)
		        * powgnum (x, nu1 / 2 - 1)
		        * powgnum (nu2 + nu1 * x, -nu1 / 2 - nu2 / 2);
	}
}

gnum_float
random_hypergeometric_pdf (int k, int n1, int n2, int t)
{
        if (t > n1 + n2)
	        t = n1 + n2;

	if (k > n1 || k > t)
	        return 0;
	else if (t > n2 && k + n2 < t )
	        return 0 ;
	else {
	        gnum_float c1 = combin (n1, k);
		gnum_float c2 = combin (n2, t - k);
		gnum_float c3 = combin (n1 + n2, t);

		return expgnum (c1 + c2 - c3);
	}
}


gnum_float
random_logarithmic_pdf (int k, gnum_float p)
{
        if (k == 0)
	        return 0;
	else 
	        return powgnum (p, k) / (gnum_float) k / loggnum (1 / (1 - p));
}

gnum_float
random_lognormal_pdf (gnum_float x, gnum_float zeta, gnum_float sigma)
{
        if (x <= 0)
	        return 0;
	else {
	        gnum_float u = (loggnum (x) - zeta) / sigma;

		return 1 / (x * gnumabs (sigma) * sqrtgnum (2 * M_PI))
		        * expgnum (-(u * u) / 2);
	}
}

gnum_float
random_negative_binomial_pdf (int k, gnum_float p, gnum_float n)
{
        gnum_float f = lgamma (k + n);
	gnum_float a = lgamma (n);
	gnum_float b = lgamma (k + 1.0);

	return expgnum (f - a - b) * powgnum (p, n)
	        * powgnum (1 - p, (gnum_float) k);
}

gnum_float
random_poisson_pdf (int k, gnum_float mu)
{
        gnum_float lf = fact (k); 

	return expgnum (loggnum (mu) * k - lf - mu);
}

gnum_float
random_rayleigh_pdf (gnum_float x, gnum_float sigma)
{
        if (x < 0)
	        return 0;
	else {
	        gnum_float u = x / sigma;

		return (u / sigma) * expgnum (-u * u / 2.0);
	}
}

gnum_float
random_rayleigh_tail_pdf (gnum_float x, gnum_float a, gnum_float sigma)
{
        if (x < a)
	        return 0;
	else {
	        gnum_float u = x / sigma ;
		gnum_float v = a / sigma ;

		return (u / sigma) * expgnum ((v + u) * (v - u) / 2.0) ;
	}
}

gnum_float
random_tdist_pdf (gnum_float x, gnum_float nu)
{
        gnum_float lg1 = lgamma (nu / 2);
	gnum_float lg2 = lgamma ((nu + 1) / 2);

	return ((expgnum (lg2 - lg1) / sqrtgnum (M_PI * nu)) 
		* powgnum ((1 + x * x / nu), -(nu + 1) / 2));
}

gnum_float
random_weibull_pdf (gnum_float x, gnum_float a, gnum_float b)
{
        if (x < 0)
	        return 0;
	else if (x == 0)
	        if (b == 1)
		        return 1 / a;
		else
		        return 0;
	else if (b == 1)
	        return expgnum (-x / a) / a ;
	else
	        return (b / a) * expgnum (-powgnum (x / a, b)
					  + (b - 1) * loggnum (x / a));
}
