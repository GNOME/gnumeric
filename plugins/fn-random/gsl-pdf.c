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
random_gamma_pdf (gnum_float x, gnum_float a, gnum_float b)
{
        if (x < 0)
	        return 0;
	else if (x == 0) {
	        if (a == 1)
		        return 1 / b;
		else
		        return 0;
	} else if (a == 1)
	        return expgnum (-x / b) / b;
	else {
	        gnum_float p;
		gnum_float lngamma = lgamma (a);

		p = expgnum ((a - 1) * loggnum (x / b) - x / b - lngamma) / b;
		return p;
	}
}

gnum_float
random_beta_pdf (gnum_float x, gnum_float a, gnum_float b)
{
        if (x < 0 || x > 1)
	        return 0;
	else {
		gnum_float gab = lgamma (a + b);
		gnum_float ga  = lgamma (a);
		gnum_float gb  = lgamma (b);

		return expgnum (gab - ga - gb) * powgnum (x, a - 1) *
		        powgnum (1 - x, b - 1);
	}
}

gnum_float
random_exponential_pdf (gnum_float x, gnum_float mu)
{
        if (x < 0)
	        return 0;
	else
	        return expgnum (-x / mu) / mu;
}

gnum_float
random_bernoulli_pdf (int k, gnum_float p)
{
        if (k == 0)
	        return 1 - p;
	else if (k == 1)
	        return p;
	else
	        return 0;
}

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
random_cauchy_pdf (gnum_float x, gnum_float a)
{
        gnum_float u = x / a;

	return (1 / (M_PI * a)) / (1 + u * u);
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
random_landau_pdf (gnum_float x)
{
        static gnum_float P1[5] = {
	        0.4259894875E0, -0.1249762550E0, 0.3984243700E-1,
		-0.6298287635E-2, 0.1511162253E-2
	};
	static gnum_float P2[5] = {
	        0.1788541609E0, 0.1173957403E0, 0.1488850518E-1,
		-0.1394989411E-2, 0.1283617211E-3
	};
	static gnum_float P3[5] = {
	        0.1788544503E0, 0.9359161662E-1, 0.6325387654E-2,
		0.6611667319E-4, -0.2031049101E-5
	};
	static gnum_float P4[5] = {
	        0.9874054407E0, 0.1186723273E3, 0.8492794360E3,
		-0.7437792444E3, 0.4270262186E3
	};
	static gnum_float P5[5] = {
	        0.1003675074E1, 0.1675702434E3, 0.4789711289E4,
		0.2121786767E5, -0.2232494910E5
	};
	static gnum_float P6[5] = {
	        0.1000827619E1, 0.6649143136E3, 0.6297292665E5,
		0.4755546998E6, -0.5743609109E7
	};

	static gnum_float Q1[5] = {
	        1.0, -0.3388260629E0, 0.9594393323E-1,
		-0.1608042283E-1, 0.3778942063E-2
	};
	static gnum_float Q2[5] = {
	        1.0, 0.7428795082E0, 0.3153932961E0,
		0.6694219548E-1, 0.8790609714E-2
	};
	static gnum_float Q3[5] = {
	        1.0, 0.6097809921E0, 0.2560616665E0,
		0.4746722384E-1, 0.6957301675E-2
	};
	static gnum_float Q4[5] = {
	        1.0, 0.1068615961E3, 0.3376496214E3,
		0.2016712389E4, 0.1597063511E4
	};
	static gnum_float Q5[5] = {
	        1.0, 0.1569424537E3, 0.3745310488E4,
		0.9834698876E4, 0.6692428357E5
	};
	static gnum_float Q6[5] = {
	        1.0, 0.6514101098E3, 0.5697473333E5,
		0.1659174725E6, -0.2815759939E7
	};

	static gnum_float A1[3] = {
	        0.4166666667E-1, -0.1996527778E-1, 0.2709538966E-1
	};
	static gnum_float A2[2] = {
	        -0.1845568670E1, -0.4284640743E1
	};

	gnum_float U, V, DENLAN;

	V = x;
	if (V < -5.5) {
	        U      = expgnum (V + 1.0);
		DENLAN = 0.3989422803 * (expgnum ( -1 / U) / sqrtgnum (U)) *
		        (1 + (A1[0] + (A1[1] + A1[2] * U) * U) * U);
	} else if (V < -1) {
	        U = expgnum (-V - 1);
		DENLAN = expgnum ( -U) * sqrtgnum (U) *
		        (P1[0] + (P1[1] + (P1[2] + (P1[3] + P1[4] * V) * V)
				  * V) * V) /
		        (Q1[0] + (Q1[1] + (Q1[2] + (Q1[3] + Q1[4] * V) * V)
				  * V) * V);
	} else if (V < 1) {
	        DENLAN = (P2[0] + (P2[1] + (P2[2] + (P2[3] + P2[4] * V) * V)
				   * V) * V) /
		        (Q2[0] + (Q2[1] + (Q2[2] + (Q2[3] + Q2[4] * V) * V)
				  * V) * V);
	} else if (V < 5) {
	        DENLAN = (P3[0] + (P3[1] + (P3[2] + (P3[3] + P3[4] * V) * V)
				   * V) * V) /
		        (Q3[0] + (Q3[1] + (Q3[2] + (Q3[3] + Q3[4] * V) * V)
				  * V) * V);
	} else if (V < 12) {
	        U = 1 / V;
		DENLAN = U * U *
		        (P4[0] + (P4[1] + (P4[2] + (P4[3] + P4[4] * U) * U)
				  * U) * U) /
		        (Q4[0] + (Q4[1] + (Q4[2] + (Q4[3] + Q4[4] * U) * U)
				  * U) * U);
	} else if (V < 50) {
	        U = 1 / V;
		DENLAN = U * U *
		        (P5[0] + (P5[1] + (P5[2] + (P5[3] + P5[4] * U) * U)
				  * U) * U) /
		        (Q5[0] + (Q5[1] + (Q5[2] + (Q5[3] + Q5[4] * U) * U)
				  * U) * U);
	} else if (V < 300) {
	        U = 1 / V;
		DENLAN = U * U *
		        (P6[0] + (P6[1] + (P6[2] + (P6[3] + P6[4] * U) * U)
				  * U) * U) /
		        (Q6[0] + (Q6[1] + (Q6[2] + (Q6[3] + Q6[4] * U) * U)
				  * U) * U);
	} else {
	        U = 1 / (V - V * log(V) / (V + 1));
		DENLAN = U * U * (1 + (A2[0] + A2[1] * U) * U);
	}

	return DENLAN;
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
random_logistic_pdf (gnum_float x, gnum_float a)
{
        gnum_float u = expgnum (-gnumabs (x) / a);

	return u / (gnumabs (a) * (1 + u) * (1 + u));
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
random_pareto_pdf (gnum_float x, gnum_float a, gnum_float b)
{
        if (x >= b)
	        return (a / b) / powgnum (x / b, a + 1);
	else
	        return 0;
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

gnum_float
random_geometric_pdf (int k, gnum_float p)
{
        if (k == 0)
	        return 0;
	else if (k == 1)
	        return p ;
	else
	        return p * powgnum (1 - p, k - 1.0);
}
