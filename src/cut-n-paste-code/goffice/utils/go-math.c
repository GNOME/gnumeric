/*
 * go-math.c:  Mathematical functions.
 *
 * Authors:
 *   Ross Ihaka.  (See note 1.)
 *   The R Development Core Team.  (See note 1.)
 *   Morten Welinder <terra@gnome.org>
 *   Miguel de Icaza (miguel@gnu.org)
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 *   James Theiler.  (See note 2.)
 *   Brian Gough.  (See note 2.)
 *   Makoto Matsumoto and Takuji Nishimura (Mersenne Twister, see note in code)
 *   Ian Smith (iandjmsmith@aol.com).  (See note 3.)
 */

/*
 * NOTE 1: most of this file comes from the "R" package, notably version 1.8.1
 * or newer (we re-sync from time to time).
 * "R" is distributed under GPL licence, see file COPYING.
 * The relevant parts are copyright (C) 1998 Ross Ihaka and
 * 2000-2002 The R Development Core Team.
 *
 * Thank you!
 */

/*
 * NOTE 2: most of the random distribution code comes from the GNU Scientific
 * Library (GSL), notably version 1.1.1.  GSL is distributed under GPL licence,
 * see COPYING. The relevant parts are copyright (C) 1996, 1997, 1998, 1999,
 * 2000 James Theiler and Brian Gough.
 *
 * Thank you!
 */

/*
 * NOTE 3: the pbeta (and support) code comes from Ian Smith.  (Translated
 * into C, adapted to Gnumeric naming convensions, and R's API conventions
 * by Morten Welinder.  Blame me for problems.)
 *
 * Copyright © Ian Smith 2002-2003
 * Version 1.0.24
 * Thanks to Jerry W. Lewis for help with testing of and improvements to the code. 
 *
 * Thank you!
 */

#include <goffice/goffice-config.h>
#include "go-math.h"
#include <glib/gmessages.h>
#include <stdlib.h>
#include <stdio.h>
#include <locale.h>
#include <signal.h>
#include <errno.h>

#if defined (HAVE_IEEEFP_H) || defined (HAVE_IEEE754_H)
/* Make sure we have this symbol defined, since the existance of either
   header file implies it.  */
#ifndef IEEE_754
#define IEEE_754
#endif
#endif

#define ML_UNDERFLOW (GO_EPSILON * GO_EPSILON)

double go_nan;
double go_pinf;
double go_ninf;

void
go_math_init (void)
{
	const char *bug_url = "http://bugzilla.gnome.org/enter_bug.cgi?product=gnumeric";
	char *old_locale;
	double d;
#ifdef SIGFPE
	void (*signal_handler)(int) = (void (*)(int))signal (SIGFPE, SIG_IGN);
#endif

	go_pinf = HUGE_VAL;
	if (go_pinf > 0 && !go_finite (go_pinf))
		goto have_pinf;

#if defined(INFINITY) && defined(__STDC_IEC_559__)
	go_pinf = INFINITY;
	if (go_pinf > 0 && !go_finite (go_pinf))
		goto have_pinf;
#endif

	/* Try sscanf with fixed strings.  */
	old_locale = setlocale (LC_ALL, "C");
	if (sscanf ("Inf", "%lf", &d) != 1 &&
	    sscanf ("+Inf", "%lf", &d) != 1)
		d = 0;
	setlocale (LC_ALL, old_locale);
	go_pinf = d;
	if (go_pinf > 0 && !go_finite (go_pinf))
		goto have_pinf;

	/* Try overflow.  */
	go_pinf = (HUGE_VAL * HUGE_VAL);
	if (go_pinf > 0 && !go_finite (go_pinf))
		goto have_pinf;

	g_error ("Failed to generate +Inf.  Please report at %s",
		 bug_url);
	abort ();

 have_pinf:
	/* ---------------------------------------- */

	go_ninf = -go_pinf;
	if (go_ninf < 0 && !go_finite (go_ninf))
		goto have_ninf;

	g_error ("Failed to generate -Inf.  Please report at %s",
		 bug_url);
	abort ();

 have_ninf:
	/* ---------------------------------------- */

	go_nan = go_pinf * 0.0;
	if (isnan (go_nan))
		goto have_nan;

	/* Try sscanf with fixed strings.  */
	old_locale = setlocale (LC_ALL, "C");
	if (sscanf ("NaN", "%lf", &d) != 1 &&
	    sscanf ("NAN", "%lf", &d) != 1 &&
	    sscanf ("+NaN", "%lf", &d) != 1 &&
	    sscanf ("+NAN", "%lf", &d) != 1)
		d = 0;
	setlocale (LC_ALL, old_locale);
	go_nan = d;
	if (isnan (go_nan))
		goto have_nan;

	go_nan = go_pinf / go_pinf;
	if (isnan (go_nan))
		goto have_nan;

	g_error ("Failed to generate NaN.  Please report at %s",
		 bug_url);
	abort ();

 have_nan:
#ifdef SIGFPE
	signal (SIGFPE, signal_handler);
#endif
	return;
}

/*
 * In preparation for truncation, make the value a tiny bit larger (seen
 * absolutely).  This makes ROUND (etc.) behave a little closer to what
 * people want, even if it is a bit bogus.
 */
double
go_add_epsilon (double x)
{
	if (!go_finite (x) || x == 0)
		return x;
	else {
		int exp;
		double mant = frexp (fabs (x), &exp);
		double absres = ldexp (mant + DBL_EPSILON, exp);
		return (x < 0) ? -absres : absres;
	}
}

double
go_sub_epsilon (double x)
{
	if (!go_finite (x) || x == 0)
		return x;
	else {
		int exp;
		double mant = frexp (fabs (x), &exp);
		double absres = ldexp (mant - DBL_EPSILON, exp);
		return (x < 0) ? -absres : absres;
	}
}

double
go_fake_floor (double x)
{
	return floor (go_add_epsilon (x));
}

double
go_fake_trunc (double x)
{
	return (x >= 0)
		? go_fake_floor (x)
		: -go_fake_floor (-x);
}
