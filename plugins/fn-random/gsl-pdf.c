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
