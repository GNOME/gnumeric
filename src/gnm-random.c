#include <gnumeric-config.h>
#include <gnumeric.h>
#include <gnm-random.h>
#include <mathfunc.h>
#include <sf-dpq.h>
#include <sf-gamma.h>
#include <glib/gstdio.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif

#include <unistd.h>
#include <string.h>

/*
 * gnm-random.c:  Mathematical functions.
 *
 * Authors:
 *   Morten Welinder <terra@gnome.org>
 *   Makoto Matsumoto and Takuji Nishimura (Mersenne Twister, see note in code)
 *   James Theiler.  (See note 1.)
 *   Brian Gough.  (See note 1.)
 *
 *
 * NOTE 1: most of the random distribution code comes from the GNU Scientific
 * Library (GSL), notably version 1.1.1.  GSL is distributed under GPL licence,
 * see COPYING. The relevant parts are copyright (C) 1996, 1997, 1998, 1999,
 * 2000 James Theiler and Brian Gough.
 *
 * Thank you!
 */

/* ------------------------------------------------------------------------- */
/* http://www.math.keio.ac.jp/matumoto/CODES/MT2002/mt19937ar.c  */
/* Imported by hand -- MW.  */

/*
   A C-program for MT19937, with initialization improved 2002/1/26.
   Coded by Takuji Nishimura and Makoto Matsumoto.

   Before using, initialize the state by using init_genrand(seed)
   or init_by_array(init_key, key_length).

   Copyright (C) 1997 - 2002, Makoto Matsumoto and Takuji Nishimura,
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

     1. Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.

     2. Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.

     3. The names of its contributors may not be used to endorse or promote
        products derived from this software without specific prior written
        permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


   Any feedback is very welcome.
   http://www.math.keio.ac.jp/matumoto/emt.html
   email: matumoto@math.keio.ac.jp
*/

#if 0
#include <stdio.h>
#endif

/* Period parameters */
#define N 624
#define M 397
#define MATRIX_A 0x9908b0dfUL   /* constant vector a */
#define UPPER_MASK 0x80000000UL /* most significant w-r bits */
#define LOWER_MASK 0x7fffffffUL /* least significant r bits */

static unsigned long mt[N]; /* the array for the state vector  */
static int mti=N+1; /* mti==N+1 means mt[N] is not initialized */

/* initializes mt[N] with a seed */
static void init_genrand(unsigned long s)
{
    mt[0]= s & 0xffffffffUL;
    for (mti=1; mti<N; mti++) {
        mt[mti] =
	    (1812433253UL * (mt[mti-1] ^ (mt[mti-1] >> 30)) + mti);
        /* See Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier. */
        /* In the previous versions, MSBs of the seed affect   */
        /* only MSBs of the array mt[].                        */
        /* 2002/01/09 modified by Makoto Matsumoto             */
        mt[mti] &= 0xffffffffUL;
        /* for >32 bit machines */
    }
}

/* initialize by an array with array-length */
/* init_key is the array for initializing keys */
/* key_length is its length */
static void mt_init_by_array(unsigned long init_key[], int key_length)
{
    int i, j, k;
    init_genrand(19650218UL);
    i=1; j=0;
    k = (N>key_length ? N : key_length);
    for (; k; k--) {
        mt[i] = (mt[i] ^ ((mt[i-1] ^ (mt[i-1] >> 30)) * 1664525UL))
          + init_key[j] + j; /* non linear */
        mt[i] &= 0xffffffffUL; /* for WORDSIZE > 32 machines */
        i++; j++;
        if (i>=N) { mt[0] = mt[N-1]; i=1; }
        if (j>=key_length) j=0;
    }
    for (k=N-1; k; k--) {
        mt[i] = (mt[i] ^ ((mt[i-1] ^ (mt[i-1] >> 30)) * 1566083941UL))
          - i; /* non linear */
        mt[i] &= 0xffffffffUL; /* for WORDSIZE > 32 machines */
        i++;
        if (i>=N) { mt[0] = mt[N-1]; i=1; }
    }

    mt[0] = 0x80000000UL; /* MSB is 1; assuring non-zero initial array */
}

/* generates a random number on [0,0xffffffff]-interval */
static unsigned long genrand_int32(void)
{
    unsigned long y;
    static unsigned long mag01[2]={0x0UL, MATRIX_A};
    /* mag01[x] = x * MATRIX_A  for x=0,1 */

    if (mti >= N) { /* generate N words at one time */
        int kk;

        if (mti == N+1)   /* if init_genrand() has not been called, */
            init_genrand(5489UL); /* a default initial seed is used */

        for (kk=0;kk<N-M;kk++) {
            y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
            mt[kk] = mt[kk+M] ^ (y >> 1) ^ mag01[y & 0x1UL];
        }
        for (;kk<N-1;kk++) {
            y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
            mt[kk] = mt[kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1UL];
        }
        y = (mt[N-1]&UPPER_MASK)|(mt[0]&LOWER_MASK);
        mt[N-1] = mt[M-1] ^ (y >> 1) ^ mag01[y & 0x1UL];

        mti = 0;
    }

    y = mt[mti++];

    /* Tempering */
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680UL;
    y ^= (y << 15) & 0xefc60000UL;
    y ^= (y >> 18);

    return y;
}

#if 0
/* generates a random number on [0,0x7fffffff]-interval */
long genrand_int31(void)
{
    return (long)(genrand_int32()>>1);
}

/* generates a random number on [0,1]-real-interval */
double genrand_real1(void)
{
    return genrand_int32()*(1.0/4294967295.0);
    /* divided by 2^32-1 */
}

/* generates a random number on [0,1)-real-interval */
double genrand_real2(void)
{
    return genrand_int32()*(1.0/4294967296.0);
    /* divided by 2^32 */
}

/* generates a random number on (0,1)-real-interval */
double genrand_real3(void)
{
    return (((double)genrand_int32()) + 0.5)*(1.0/4294967296.0);
    /* divided by 2^32 */
}

/* generates a random number on [0,1) with 53-bit resolution*/
static double genrand_res53(void)
{
    unsigned long a=genrand_int32()>>5, b=genrand_int32()>>6;
    return(a*67108864.0+b)*(1.0/9007199254740992.0);
}
/* These real versions are due to Isaku Wada, 2002/01/09 added */
#endif

#if 0
int main(void)
{
    int i;
    unsigned long init[4]={0x123, 0x234, 0x345, 0x456}, length=4;
    init_by_array(init, length);
    g_printerr("1000 outputs of genrand_int32()\n");
    for (i=0; i<1000; i++) {
      g_printerr("%10lu ", genrand_int32());
      if (i%5==4) g_printerr("\n");
    }
    g_printerr("\n1000 outputs of genrand_real2()\n");
    for (i=0; i<1000; i++) {
      g_printerr("%10.8f ", genrand_real2());
      if (i%5==4) g_printerr("\n");
    }
    return 0;
}
#endif


#undef N
#undef M
#undef MATRIX_A
#undef UPPER_MASK
#undef LOWER_MASK

/* ------------------------------------------------------------------------ */

static void
mt_setup_seed (const char *seed)
{
	int len = strlen (seed);
	int i;
	unsigned long *longs = g_new (unsigned long, len + 1);

	/* We drop only one character into each long.  */
	for (i = 0; i < len; i++)
		longs[i] = (unsigned char)seed[i];
	mt_init_by_array (longs, len);
	g_free (longs);
}

#ifdef G_OS_WIN32

static gboolean
mt_setup_win32 (void)
{
	/* See http://msdn.microsoft.com/en-us/library/Aa387694 */
	typedef BOOLEAN (CALLBACK* LPFNRTLGENRANDOM) (void*,ULONG);
	LPFNRTLGENRANDOM MyRtlGenRandom;
	unsigned long buffer[256];
	HMODULE hmod;
	gboolean res = FALSE;

	hmod = GetModuleHandle ("ADVAPI32.DLL");
	if (!hmod)
		return FALSE;

	MyRtlGenRandom = (LPFNRTLGENRANDOM)
		GetProcAddress(hmod, "SystemFunction036");
	if (MyRtlGenRandom &&
	    MyRtlGenRandom (buffer, sizeof (buffer))) {
		mt_init_by_array (buffer, G_N_ELEMENTS (buffer));
		res = TRUE;
	}

	FreeLibrary (hmod);

	return res;
}

#endif

/* ------------------------------------------------------------------------ */

typedef enum {
	RS_UNDETERMINED,
	RS_MERSENNE,
	RS_DEVICE
} RandomSource;

static RandomSource random_src = RS_UNDETERMINED;

static FILE *random_device_file = NULL;
#define RANDOM_DEVICE "/dev/urandom"

static void
random_source_determine (void)
{
	char const *seed = g_getenv ("GNUMERIC_PRNG_SEED");
	if (seed) {
		mt_setup_seed (seed);
		g_warning ("Using pseudo-random numbers.");
		random_src = RS_MERSENNE;
		return;
	}

#ifdef G_OS_WIN32
	if (mt_setup_win32 ()) {
		random_src = RS_MERSENNE;
		return;
	}
#endif

	random_device_file = g_fopen (RANDOM_DEVICE, "rb");
	if (random_device_file) {
		random_src = RS_DEVICE;
		return;
	}

	/* Fallback.  */
	g_warning ("Using pseudo-random numbers.");
	random_src = RS_MERSENNE;
	return;
}

static guint32
gnm_random_32_device (void)
{
	guint32 res;

	if (fread (&res, sizeof (res), 1, random_device_file) != 1) {
		g_warning ("Reading from %s failed; reverting to pseudo-random.",
			   RANDOM_DEVICE);
		res = genrand_int32 ();
	}

	return res;
}

static guint32
random_32 (void)
{
	if (random_src == RS_UNDETERMINED)
		random_source_determine ();

	switch (random_src) {
	case RS_UNDETERMINED:
	default:
		g_assert_not_reached ();
	case RS_MERSENNE:
		return genrand_int32 ();
	case RS_DEVICE:
		return gnm_random_32_device ();
	}
}

/* ------------------------------------------------------------------------ */

gnm_float
random_01 (void)
{
	gnm_float res = 0;

#if GNM_RADIX == 2
	for (int d = GNM_MANT_DIG; d > 0; d -= 32) {
		uint32_t bits = random_32 ();
		if (d >= 32) {
			res = (res + bits) / 4294967296ull;
		} else {
			bits &= ((1 << d) - 1);
			res = (res + bits) / (1 << d);
			break;
		}
	}

	return res;
#elif GNM_RADIX == 10
	static const uint32_t p10[10] = {
		1, 10, 100, 1000, 10000,
		100000, 1000000, 10000000, 100000000, 1000000000
	};

	for (int d = GNM_MANT_DIG; d > 0; d -= 9) {
		uint32_t bits;
		do {
			bits = random_32 () / 4;
			// Rejecting roughly 7% of cases
		} while (bits >= 1000000000);
		if (d >= 9) {
			res = (res + bits) / GNM_const(1e9);
		} else {
			bits %= p10[d];
			res = (res + bits) / p10[d];
			break;
		}
	}

	return res;
#else
#error "Code needs fixing here"
#endif
}

/* ------------------------------------------------------------------------ */

/**
 * random_normal:
 *
 * Returns: a N(0,1) distributed random number.
 */
gnm_float
random_normal (void)
{
	static gboolean  has_saved = FALSE;
	static gnm_float saved;

	if (has_saved) {
		has_saved = FALSE;
		return saved;
	} else {
		gnm_float u, v, r2, rsq;
		do {
			u = 2 * random_01 () - 1;
			v = 2 * random_01 () - 1;
			r2 = u * u + v * v;
		} while (r2 > 1 || r2 == 0);

		rsq = gnm_sqrt (-2 * gnm_log (r2) / r2);

		has_saved = TRUE;
		saved = v * rsq;

		return u * rsq;
	}
}

gnm_float
random_lognormal (gnm_float zeta, gnm_float sigma)
{
	return gnm_exp (sigma * random_normal () + zeta);
}

static gnm_float
random_gaussian (gnm_float sigma)
{
	return sigma * random_normal ();
}

/*
 * Generate a poisson distributed number.
 */
gnm_float
random_poisson (gnm_float lambda)
{
	/*
	 * This may not be optimal code, but it sure is easy to
	 * understand compared to R's code.
	 */
	return qpois (random_01 (), lambda, TRUE, FALSE);
}

/*
 * Generate a binomial distributed number.
 */
gnm_float
random_binomial (gnm_float p, gnm_float trials)
{
	return qbinom (random_01 (), trials, p, TRUE, FALSE);
}

/*
 * Generate a negative binomial distributed number.
 */
gnm_float
random_negbinom (gnm_float p, gnm_float f)
{
	return qnbinom (random_01 (), f, p, TRUE, FALSE);
}

/*
 * Generate an exponential distributed number.
 */
gnm_float
random_exponential (gnm_float b)
{
	return -b * gnm_log (random_01 ());
}

/*
 * Generate a bernoulli distributed number.
 */
gnm_float
random_bernoulli (gnm_float p)
{
	gnm_float r = random_01 ();

	return (r <= p) ? 1.0 : 0.0;
}

/*
 * Generate a cauchy distributed number. From the GNU Scientific library 1.1.1.
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough.
 */
gnm_float
random_cauchy (gnm_float a)
{
	gnm_float u;

	do {
		u = random_01 ();
	} while (u == GNM_const(0.5) || u == 0);

	return a * gnm_tanpi (u);
}

/*
 * Generate a Weibull distributed number. From the GNU Scientific
 * library 1.1.1.
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough.
 */
gnm_float
random_weibull (gnm_float a, gnm_float b)
{
	gnm_float x, z;

	do {
		x = random_01 ();
	} while (x == 0);

	z = gnm_pow (-gnm_log (x), 1 / b);

	return a * z;
}

/*
 * Generate a Laplace (two-sided exponential probability) distributed number.
 * From the GNU Scientific library 1.1.1.
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough.
 */
gnm_float
random_laplace (gnm_float a)
{
	gnm_float u;

	do {
		u = 2 * random_01 () - 1;
	} while (u == 0);

	if (u < 0)
		return a * gnm_log (-u);
	else
		return -a * gnm_log (u);
}

gnm_float
random_laplace_pdf (gnm_float x, gnm_float a)
{
	return (1 / (2 * a)) * gnm_exp (-gnm_abs (x) / a);
}

/*
 * Generate a Rayleigh distributed number.  From the GNU Scientific library
 * 1.1.1.
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough.
 */
gnm_float
random_rayleigh (gnm_float sigma)
{
	gnm_float u;

	do {
		u = random_01 ();
	} while (u == 0);

	return sigma * gnm_sqrt (-2 * gnm_log (u));
}

/*
 * Generate a Rayleigh tail distributed number.	 From the GNU Scientific library
 * 1.1.1.  The Rayleigh tail distribution has the form
 *   p(x) dx = (x / sigma^2) exp((a^2 - x^2)/(2 sigma^2)) dx
 *
 *   for x = a ... +infty
 *
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough.
 */
gnm_float
random_rayleigh_tail (gnm_float a, gnm_float sigma)
{
	gnm_float u;

	do {
		u = random_01 ();
	} while (u == 0);

	return gnm_sqrt (a * a - 2 * sigma * sigma * gnm_log (u));
}

/* The Gamma distribution of order a>0 is defined by:
 *
 *  p(x) dx = {1 / \Gamma(a) b^a } x^{a-1} e^{-x/b} dx
 *
 *  for x>0.  If X and Y are independent gamma-distributed random
 *   variables of order a1 and a2 with the same scale parameter b, then
 *   X+Y has gamma distribution of order a1+a2.
 *
 *  The algorithms below are from Knuth, vol 2, 2nd ed, p. 129.
 */

static gnm_float
gamma_frac (gnm_float a)
{
	/* This is exercise 16 from Knuth; see page 135, and the solution is
	 * on page 551.	 */

	gnm_float x, q;
	gnm_float p = M_Egnum / (a + M_Egnum);
	do {
		gnm_float v;
		gnm_float u = random_01 ();
		do {
			v = random_01 ();
		} while (v == 0);

		if (u < p) {
			x = gnm_pow (v, 1 / a);
			q = gnm_exp (-x);
		} else {
			x = 1 - gnm_log (v);
			q = gnm_pow (x, a - 1);
		}
	} while (random_01 () >= q);

	return x;
}

static gnm_float
gamma_large (gnm_float a)
{
	/*
	 * Works only if a > 1, and is most efficient if a is large
	 *
	 * This algorithm, reported in Knuth, is attributed to Ahrens.	A
	 * faster one, we are told, can be found in: J. H. Ahrens and
	 * U. Dieter, Computing 12 (1974) 223-246.
	 */

	gnm_float sqa, x, y, v;
	sqa = gnm_sqrt (2 * a - 1);
	do {
		do {
			y = gnm_tan (M_PIgnum * random_01 ());
			x = sqa * y + a - 1;
		} while (x <= 0);
		v = random_01 ();
	} while (v > (1 + y * y) * gnm_exp ((a - 1) * gnm_log (x / (a - 1)) -
					    sqa * y));

	return x;
}

static gnm_float
ran_gamma_int (gnm_float a)
{
	if (a < 12) {
		gnm_float prod;

		do {
			unsigned int i, ua;
			prod = 1;
			ua = (unsigned int)a;

			for (i = 0; i < ua; i++)
				prod *= random_01 ();

			/*
			 * This handles the 0-probability event of getting
			 * an actual zero as well as the possibility of
			 * underflow.
			 */
		} while (prod == 0);

		return -gnm_log (prod);
	} else
		return gamma_large (a);
}

/*
 * Generate a Gamma distributed number.	 From the GNU Scientific library 1.1.1.
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough.
 */
gnm_float
random_gamma (gnm_float a, gnm_float b)
{
	gnm_float na;

	if (gnm_isnan (a) || gnm_isnan (b) || a <= 0 || b <= 0)
		return gnm_nan;

	na = gnm_floor (a);

	if (a == na)
		return b * ran_gamma_int (na);
	else if (na == 0)
		return b * gamma_frac (a);
	else
		return b * (ran_gamma_int (na) + gamma_frac (a - na));
}

/*
 * Generate a Pareto distributed number. From the GNU Scientific library 1.1.1.
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough.
 */
gnm_float
random_pareto (gnm_float a, gnm_float b)
{
	gnm_float x;

	do {
		x = random_01 ();
	} while (x == 0);

	return b * gnm_pow (x, -1 / a);
}

/*
 * Generate a F-distributed number. From the GNU Scientific library 1.1.1.
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough.
 */
gnm_float
random_fdist (gnm_float nu1, gnm_float nu2)
{
	gnm_float Y1 = random_gamma (nu1 / 2, 2);
	gnm_float Y2 = random_gamma (nu2 / 2, 2);

	return (Y1 * nu2) / (Y2 * nu1);
}

/*
 * Generate a Beta-distributed number. From the GNU Scientific library 1.1.1.
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough.
 */
gnm_float
random_beta (gnm_float a, gnm_float b)
{
	gnm_float x1 = random_gamma (a, 1.0);
	gnm_float x2 = random_gamma (b, 1.0);

	return x1 / (x1 + x2);
}

/*
 * Generate a Chi-Square-distributed number. From the GNU Scientific library
 * 1.1.1.
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough.
 */
gnm_float
random_chisq (gnm_float nu)
{
	return 2 * random_gamma (nu / 2, 1.0);
}

/*
 * Generate a logistic-distributed number. From the GNU Scientific library
 * 1.1.1.
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough.
 */
gnm_float
random_logistic (gnm_float a)
{
	gnm_float x;

	do {
		x = random_01 ();
	} while (x == 0 || x == 1);

	return a * gnm_log (x / (1 - x));
}

/*
 * Generate a geometric-distributed number. From the GNU Scientific library
 * 1.1.1.
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough.
 */
gnm_float
random_geometric (gnm_float p)
{
	gnm_float u;

	if (p == 1)
		return 1;
	do {
		u = random_01 ();
	} while (u == 0);

	/*
	 * Change from gsl version: we have support {0,1,2,...}
	 */
	return gnm_floor (gnm_log (u) / gnm_log1p (-p));
}

gnm_float
random_hypergeometric (gnm_float n1, gnm_float n2, gnm_float t)
{
	return qhyper (random_01 (), n1, n2, t, TRUE, FALSE);
}


/*
 * Generate a logarithmic-distributed number. From the GNU Scientific library
 * 1.1.1.
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough.
 */
gnm_float
random_logarithmic (gnm_float p)
{
	gnm_float c, v;

	c = gnm_log1p (-p);
	do {
		v = random_01 ();
	} while (v == 0);

	if (v >= p)
		return 1;
	else {
		gnm_float u, q;

		do {
			u = random_01 ();
		} while (u == 0);
		q = -gnm_expm1 (c * u);

		if (v <= q * q)
			return gnm_floor (1 + gnm_log (v) / gnm_log (q));
		else if (v <= q)
			return 2;
		else
			return 1;
	}
}

/*
 * Generate a T-distributed number. From the GNU Scientific library 1.1.1.
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough.
 */
gnm_float
random_tdist (gnm_float nu)
{
	if (nu <= 2) {
		gnm_float Y1 = random_normal ();
		gnm_float Y2 = random_chisq (nu);

		gnm_float t = Y1 / gnm_sqrt (Y2 / nu);

		return t;
	} else {
		gnm_float Y1, Y2, Z, t;
		do {
			Y1 = random_normal ();
			Y2 = random_exponential (1 / (nu / 2 - 1));

			Z = Y1 * Y1 / (nu - 2);
		} while (1 - Z < 0 || gnm_exp (-Y2 - Z) > (1 - Z));

		/* Note that there is a typo in Knuth's formula, the line below
		 * is taken from the original paper of Marsaglia, Mathematics
		 * of Computation, 34 (1980), p 234-256. */

		t = Y1 / gnm_sqrt ((1 - 2 / nu) * (1 - Z));
		return t;
	}
}

/*
 * Generate a Type I Gumbel-distributed random number. From the GNU
 * Scientific library 1.1.1.
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough.
 */
gnm_float
random_gumbel1 (gnm_float a, gnm_float b)
{
	gnm_float x;

	do {
		x = random_01 ();
	} while (x == 0);

	return (gnm_log (b) - gnm_log (-gnm_log (x))) / a;
}

/*
 * Generate a Type II Gumbel-distributed random number. From the GNU
 * Scientific library 1.1.1.
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough.
 */
gnm_float
random_gumbel2 (gnm_float a, gnm_float b)
{
	gnm_float x;

	do {
		x = random_01 ();
	} while (x == 0);

	return gnm_pow (-b / gnm_log (x), 1 / a);
}

/*
 * Generate a stable Levy-distributed random number. From the GNU
 * Scientific library 1.1.1.
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough.
 *
 * The stable Levy probability distributions have the form
 *
 * p(x) dx = (1/(2 pi)) \int dt exp(- it x - |c t|^alpha)
 *
 * with 0 < alpha <= 2.
 *
 * For alpha = 1, we get the Cauchy distribution
 * For alpha = 2, we get the Gaussian distribution with sigma = sqrt(2) c.
 *
 * Fromn Chapter 5 of Bratley, Fox and Schrage "A Guide to
 * Simulation". The original reference given there is,
 *
 * J.M. Chambers, C.L. Mallows and B. W. Stuck. "A method for
 * simulating stable random variates". Journal of the American
 * Statistical Association, JASA 71 340-344 (1976).
 */
gnm_float
random_levy (gnm_float c, gnm_float alpha)
{
	gnm_float u, v, t, s;

	do {
		u = random_01 ();
	} while (u == 0);

	u = M_PIgnum * (u - GNM_const(0.5));

	if (alpha == 1) {	      /* cauchy case */
		t = gnm_tan (u);
		return c * t;
	}

	do {
		v = random_exponential (1.0);
	} while (v == 0);

	if (alpha == 2) {	     /* gaussian case */
		t = 2 * gnm_sin (u) * gnm_sqrt (v);
		return c * t;
	}

	/* general case */

	t = gnm_sin (alpha * u) / gnm_pow (gnm_cos (u), 1 / alpha);
	s = gnm_pow (gnm_cos ((1 - alpha) * u) / v, (1 - alpha) / alpha);

	return c * t * s;
}

/*
 * The following routine for the skew-symmetric case was provided by
 * Keith Briggs.
 *
 * The stable Levy probability distributions have the form
 *
 * 2*pi* p(x) dx
 *
 *  = int dt exp(mu*i*t-|sigma*t|^alpha*(1-i*beta*sign(t)*tan(pi*alpha/2))) for
 *    alpha != 1
 *  = int dt exp(mu*i*t-|sigma*t|^alpha*(1+i*beta*sign(t)*2/pi*log(|t|)))   for
      alpha == 1
 *
 *  with 0<alpha<=2, -1<=beta<=1, sigma>0.
 *
 *  For beta=0, sigma=c, mu=0, we get gsl_ran_levy above.
 *
 *  For alpha = 1, beta=0, we get the Lorentz distribution
 *  For alpha = 2, beta=0, we get the Gaussian distribution
 *
 *  See A. Weron and R. Weron: Computer simulation of Lévy alpha-stable
 *  variables and processes, preprint Technical University of Wroclaw.
 *  http://www.im.pwr.wroc.pl/~hugo/Publications.html
 */
gnm_float
random_levy_skew (gnm_float c, gnm_float alpha, gnm_float beta)
{
	gnm_float V, W, X;

	if (beta == 0) /* symmetric case */
		return random_levy (c, alpha);

	do {
		V = random_01 ();
	} while (V == 0);

	V = M_PIgnum * (V - GNM_const(0.5));

	do {
		W = random_exponential (1.0);
	} while (W == 0);

	if (alpha == 1) {
		X = ((M_PI_2gnum + beta * V) * gnm_tan (V) -
		     beta * gnm_log (M_PI_2gnum * W * gnm_cos (V) /
				     (M_PI_2gnum + beta * V))) / M_PI_2gnum;
		return c * (X + beta * gnm_log (c) / M_PI_2gnum);
	} else {
		gnm_float t = beta * gnm_tan (M_PI_2gnum * alpha);
		gnm_float B = gnm_atan (t) / alpha;
		gnm_float S = pow1p (t * t, 1 / (2 * alpha));

		X = S * gnm_sin (alpha * (V + B)) / gnm_pow (gnm_cos (V),
							     1 / alpha)
			* gnm_pow (gnm_cos (V - alpha * (V + B)) / W,
				   (1 - alpha) / alpha);
		return c * X;
	}
}

gnm_float
random_exppow_pdf (gnm_float x, gnm_float a, gnm_float b)
{
	gnm_float lngamma = lgamma1p (1 / b);

	return (1 / (2 * a)) * gnm_exp (-gnm_pow (gnm_abs (x / a), b) - lngamma);
}

/*
 * The exponential power probability distribution is
 *
 *  p(x) dx = (1/(2 a Gamma(1+1/b))) * exp(-|x/a|^b) dx
 *
 * for -infty < x < infty. For b = 1 it reduces to the Laplace
 * distribution.
 *
 * The exponential power distribution is related to the gamma
 * distribution by E = a * pow(G(1/b),1/b), where E is an exponential
 * power variate and G is a gamma variate.
 *
 * We use this relation for b < 1. For b >=1 we use rejection methods
 * based on the laplace and gaussian distributions which should be
 *  faster.
 *
 * See P. R. Tadikamalla, "Random Sampling from the Exponential Power
 * Distribution", Journal of the American Statistical Association,
 * September 1980, Volume 75, Number 371, pages 683-686.
 *
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough
 */

gnm_float
random_exppow (gnm_float a, gnm_float b)
{
	/* See http://www.mcgill.ca/files/economics/propertiesandestimation.pdf */
	if (!(a > 0) || gnm_isnan (b))
		return gnm_nan;

	if (b < 1) {
		gnm_float u = random_01 ();
		gnm_float v = random_gamma (1 / b, 1.0);
		gnm_float z = a * gnm_pow (v, 1 / b) ;

		if (u > GNM_const(0.5))
			return z;
		else
			return -z;
	} else if (b == 1)
		return random_laplace (a);   /* Laplace distribution */
	else if (b < 2) {
		/* Use laplace distribution for rejection method */
		gnm_float x, y, h, ratio, u;

		/* Scale factor chosen by upper bound on ratio at b = 2 */
		gnm_float s = 1.4489;
		do {
			x     = random_laplace (a);
			y     = random_laplace_pdf (x, a);
			h     = random_exppow_pdf (x, a, b);
			ratio = h / (s * y);
			u     = random_01 ();
		} while (u > ratio);

		return x ;
	} else if (b == 2)   /* Gaussian distribution */
		return random_gaussian (a / gnm_sqrt (2.0));
	else {
		/* Use gaussian for rejection method */
		gnm_float x, y, h, ratio, u;
		const gnm_float sigma = a / gnm_sqrt (2.0);

		/* Scale factor chosen by upper bound on ratio at b = infinity.
		 * This could be improved by using a rational function
		 * approximation to the bounding curve. */

		gnm_float s = 2.4091 ;	 /* this is sqrt(pi) e / 2 */

		do {
			x     = random_gaussian (sigma) ;
			y     = dnorm (x, 0.0, gnm_abs (sigma), FALSE) ;
			h     = random_exppow_pdf (x, a, b) ;
			ratio = h / (s * y) ;
			u     = random_01 ();
		} while (u > ratio);

		return x;
	}
}

/*
 * Generate a Gaussian tail-distributed random number. From the GNU
 * Scientific library 1.1.1.
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 James Theiler, Brian Gough
 */
gnm_float
random_gaussian_tail (gnm_float a, gnm_float sigma)
{
	/*
	 * Returns a gaussian random variable larger than a
	 * This implementation does one-sided upper-tailed deviates.
	 */

	gnm_float s = a / sigma;

	if (s < 1) {
		/* For small s, use a direct rejection method. The limit s < 1
		 * can be adjusted to optimise the overall efficiency */

		gnm_float x;

		do {
			x = random_gaussian (1.0);
		} while (x < s);
		return x * sigma;
	} else {
		/* Use the "supertail" deviates from the last two steps
		 * of Marsaglia's rectangle-wedge-tail method, as described
		 * in Knuth, v2, 3rd ed, pp 123-128.  (See also exercise 11,
		 * p139, and the solution, p586.)
		 */

		gnm_float u, v, x;

		do {
			u = random_01 ();
			do {
				v = random_01 ();
			} while (v == 0);
			x = gnm_sqrt (s * s - 2 * gnm_log (v));
		} while (x * u > s);
		return x * sigma;
	}
}

/*
 * Generate a Landau-distributed random number. From the GNU Scientific
 * library 1.1.1.
 *
 * Copyright (C) 2001 David Morrison
 *
 * Adapted from the CERN library routines DENLAN, RANLAN, and DISLAN
 * as described in http://consult.cern.ch/shortwrups/g110/top.html.
 * Original author: K.S. K\"olbig.
 *
 * The distribution is given by the complex path integral,
 *
 *  p(x) = (1/(2 pi i)) \int_{c-i\inf}^{c+i\inf} ds exp(s log(s) + x s)
 *
 * which can be converted into a real integral over [0,+\inf]
 *
 *  p(x) = (1/pi) \int_0^\inf dt \exp(-t log(t) - x t) sin(pi t)
 */

gnm_float
random_landau (void)
{
	static gnm_float F[983] = {
		0.0000000, /*
			    * Add empty element [0] to account for difference
			    * between C and Fortran convention for lower bound.
			    */
		00.000000, 00.000000, 00.000000, 00.000000, 00.000000,
		-2.244733, -2.204365, -2.168163, -2.135219, -2.104898,
		-2.076740, -2.050397, -2.025605, -2.002150, -1.979866,
		-1.958612, -1.938275, -1.918760, -1.899984, -1.881879,
		-1.864385, -1.847451, -1.831030, -1.815083, -1.799574,
		-1.784473, -1.769751, -1.755383, -1.741346, -1.727620,
		-1.714187, -1.701029, -1.688130, -1.675477, -1.663057,
		-1.650858, -1.638868, -1.627078, -1.615477, -1.604058,
		-1.592811, -1.581729, -1.570806, -1.560034, -1.549407,
		-1.538919, -1.528565, -1.518339, -1.508237, -1.498254,
		-1.488386, -1.478628, -1.468976, -1.459428, -1.449979,
		-1.440626, -1.431365, -1.422195, -1.413111, -1.404112,
		-1.395194, -1.386356, -1.377594, -1.368906, -1.360291,
		-1.351746, -1.343269, -1.334859, -1.326512, -1.318229,
		-1.310006, -1.301843, -1.293737, -1.285688, -1.277693,
		-1.269752, -1.261863, -1.254024, -1.246235, -1.238494,
		-1.230800, -1.223153, -1.215550, -1.207990, -1.200474,
		-1.192999, -1.185566, -1.178172, -1.170817, -1.163500,
		-1.156220, -1.148977, -1.141770, -1.134598, -1.127459,
		-1.120354, -1.113282, -1.106242, -1.099233, -1.092255,
		-1.085306, -1.078388, -1.071498, -1.064636, -1.057802,
		-1.050996, -1.044215, -1.037461, -1.030733, -1.024029,
		-1.017350, -1.010695, -1.004064, -0.997456, -0.990871,
		-0.984308, -0.977767, -0.971247, -0.964749, -0.958271,
		-0.951813, -0.945375, -0.938957, -0.932558, -0.926178,
		-0.919816, -0.913472, -0.907146, -0.900838, -0.894547,
		-0.888272, -0.882014, -0.875773, -0.869547, -0.863337,
		-0.857142, -0.850963, -0.844798, -0.838648, -0.832512,
		-0.826390, -0.820282, -0.814187, -0.808106, -0.802038,
		-0.795982, -0.789940, -0.783909, -0.777891, -0.771884,
		-0.765889, -0.759906, -0.753934, -0.747973, -0.742023,
		-0.736084, -0.730155, -0.724237, -0.718328, -0.712429,
		-0.706541, -0.700661, -0.694791, -0.688931, -0.683079,
		-0.677236, -0.671402, -0.665576, -0.659759, -0.653950,
		-0.648149, -0.642356, -0.636570, -0.630793, -0.625022,
		-0.619259, -0.613503, -0.607754, -0.602012, -0.596276,
		-0.590548, -0.584825, -0.579109, -0.573399, -0.567695,
		-0.561997, -0.556305, -0.550618, -0.544937, -0.539262,
		-0.533592, -0.527926, -0.522266, -0.516611, -0.510961,
		-0.505315, -0.499674, -0.494037, -0.488405, -0.482777,
		-0.477153, -0.471533, -0.465917, -0.460305, -0.454697,
		-0.449092, -0.443491, -0.437893, -0.432299, -0.426707,
		-0.421119, -0.415534, -0.409951, -0.404372, -0.398795,
		-0.393221, -0.387649, -0.382080, -0.376513, -0.370949,
		-0.365387, -0.359826, -0.354268, -0.348712, -0.343157,
		-0.337604, -0.332053, -0.326503, -0.320955, -0.315408,
		-0.309863, -0.304318, -0.298775, -0.293233, -0.287692,
		-0.282152, -0.276613, -0.271074, -0.265536, -0.259999,
		-0.254462, -0.248926, -0.243389, -0.237854, -0.232318,
		-0.226783, -0.221247, -0.215712, -0.210176, -0.204641,
		-0.199105, -0.193568, -0.188032, -0.182495, -0.176957,
		-0.171419, -0.165880, -0.160341, -0.154800, -0.149259,
		-0.143717, -0.138173, -0.132629, -0.127083, -0.121537,
		-0.115989, -0.110439, -0.104889, -0.099336, -0.093782,
		-0.088227, -0.082670, -0.077111, -0.071550, -0.065987,
		-0.060423, -0.054856, -0.049288, -0.043717, -0.038144,
		-0.032569, -0.026991, -0.021411, -0.015828, -0.010243,
		-0.004656, 00.000934, 00.006527, 00.012123, 00.017722,
		00.023323, 00.028928, 00.034535, 00.040146, 00.045759,
		00.051376, 00.056997, 00.062620, 00.068247, 00.073877,
		00.079511, 00.085149, 00.090790, 00.096435, 00.102083,
		00.107736, 00.113392, 00.119052, 00.124716, 00.130385,
		00.136057, 00.141734, 00.147414, 00.153100, 00.158789,
		00.164483, 00.170181, 00.175884, 00.181592, 00.187304,
		00.193021, 00.198743, 00.204469, 00.210201, 00.215937,
		00.221678, 00.227425, 00.233177, 00.238933, 00.244696,
		00.250463, 00.256236, 00.262014, 00.267798, 00.273587,
		00.279382, 00.285183, 00.290989, 00.296801, 00.302619,
		00.308443, 00.314273, 00.320109, 00.325951, 00.331799,
		00.337654, 00.343515, 00.349382, 00.355255, 00.361135,
		00.367022, 00.372915, 00.378815, 00.384721, 00.390634,
		00.396554, 00.402481, 00.408415, 00.414356, 00.420304,
		00.426260, 00.432222, 00.438192, 00.444169, 00.450153,
		00.456145, 00.462144, 00.468151, 00.474166, 00.480188,
		00.486218, 00.492256, 00.498302, 00.504356, 00.510418,
		00.516488, 00.522566, 00.528653, 00.534747, 00.540850,
		00.546962, 00.553082, 00.559210, 00.565347, 00.571493,
		00.577648, 00.583811, 00.589983, 00.596164, 00.602355,
		00.608554, 00.614762, 00.620980, 00.627207, 00.633444,
		00.639689, 00.645945, 00.652210, 00.658484, 00.664768,
		00.671062, 00.677366, 00.683680, 00.690004, 00.696338,
		00.702682, 00.709036, 00.715400, 00.721775, 00.728160,
		00.734556, 00.740963, 00.747379, 00.753807, 00.760246,
		00.766695, 00.773155, 00.779627, 00.786109, 00.792603,
		00.799107, 00.805624, 00.812151, 00.818690, 00.825241,
		00.831803, 00.838377, 00.844962, 00.851560, 00.858170,
		00.864791, 00.871425, 00.878071, 00.884729, 00.891399,
		00.898082, 00.904778, 00.911486, 00.918206, 00.924940,
		00.931686, 00.938446, 00.945218, 00.952003, 00.958802,
		00.965614, 00.972439, 00.979278, 00.986130, 00.992996,
		00.999875, 01.006769, 01.013676, 01.020597, 01.027533,
		01.034482, 01.041446, 01.048424, 01.055417, 01.062424,
		01.069446, 01.076482, 01.083534, 01.090600, 01.097681,
		01.104778, 01.111889, 01.119016, 01.126159, 01.133316,
		01.140490, 01.147679, 01.154884, 01.162105, 01.169342,
		01.176595, 01.183864, 01.191149, 01.198451, 01.205770,
		01.213105, 01.220457, 01.227826, 01.235211, 01.242614,
		01.250034, 01.257471, 01.264926, 01.272398, 01.279888,
		01.287395, 01.294921, 01.302464, 01.310026, 01.317605,
		01.325203, 01.332819, 01.340454, 01.348108, 01.355780,
		01.363472, 01.371182, 01.378912, 01.386660, 01.394429,
		01.402216, 01.410024, 01.417851, 01.425698, 01.433565,
		01.441453, 01.449360, 01.457288, 01.465237, 01.473206,
		01.481196, 01.489208, 01.497240, 01.505293, 01.513368,
		01.521465, 01.529583, 01.537723, 01.545885, 01.554068,
		01.562275, 01.570503, 01.578754, 01.587028, 01.595325,
		01.603644, 01.611987, 01.620353, 01.628743, 01.637156,
		01.645593, 01.654053, 01.662538, 01.671047, 01.679581,
		01.688139, 01.696721, 01.705329, 01.713961, 01.722619,
		01.731303, 01.740011, 01.748746, 01.757506, 01.766293,
		01.775106, 01.783945, 01.792810, 01.801703, 01.810623,
		01.819569, 01.828543, 01.837545, 01.846574, 01.855631,
		01.864717, 01.873830, 01.882972, 01.892143, 01.901343,
		01.910572, 01.919830, 01.929117, 01.938434, 01.947781,
		01.957158, 01.966566, 01.976004, 01.985473, 01.994972,
		02.004503, 02.014065, 02.023659, 02.033285, 02.042943,
		02.052633, 02.062355, 02.072110, 02.081899, 02.091720,
		02.101575, 02.111464, 02.121386, 02.131343, 02.141334,
		02.151360, 02.161421, 02.171517, 02.181648, 02.191815,
		02.202018, 02.212257, 02.222533, 02.232845, 02.243195,
		02.253582, 02.264006, 02.274468, 02.284968, 02.295507,
		02.306084, 02.316701, 02.327356, 02.338051, 02.348786,
		02.359562, 02.370377, 02.381234, 02.392131, 02.403070,
		02.414051, 02.425073, 02.436138, 02.447246, 02.458397,
		02.469591, 02.480828, 02.492110, 02.503436, 02.514807,
		02.526222, 02.537684, 02.549190, 02.560743, 02.572343,
		02.583989, 02.595682, 02.607423, 02.619212, 02.631050,
		02.642936, 02.654871, 02.666855, 02.678890, 02.690975,
		02.703110, 02.715297, 02.727535, 02.739825, 02.752168,
		02.764563, 02.777012, 02.789514, 02.802070, 02.814681,
		02.827347, 02.840069, 02.852846, 02.865680, 02.878570,
		02.891518, 02.904524, 02.917588, 02.930712, 02.943894,
		02.957136, 02.970439, 02.983802, 02.997227, 03.010714,
		03.024263, 03.037875, 03.051551, 03.065290, 03.079095,
		03.092965, 03.106900, 03.120902, 03.134971, 03.149107,
		03.163312, 03.177585, 03.191928, 03.206340, 03.220824,
		03.235378, 03.250005, 03.264704, 03.279477, 03.294323,
		03.309244, 03.324240, 03.339312, 03.354461, 03.369687,
		03.384992, 03.400375, 03.415838, 03.431381, 03.447005,
		03.462711, 03.478500, 03.494372, 03.510328, 03.526370,
		03.542497, 03.558711, 03.575012, 03.591402, 03.607881,
		03.624450, 03.641111, 03.657863, 03.674708, 03.691646,
		03.708680, 03.725809, 03.743034, 03.760357, 03.777779,
		03.795300, 03.812921, 03.830645, 03.848470, 03.866400,
		03.884434, 03.902574, 03.920821, 03.939176, 03.957640,
		03.976215, 03.994901, 04.013699, 04.032612, 04.051639,
		04.070783, 04.090045, 04.109425, 04.128925, 04.148547,
		04.168292, 04.188160, 04.208154, 04.228275, 04.248524,
		04.268903, 04.289413, 04.310056, 04.330832, 04.351745,
		04.372794, 04.393982, 04.415310, 04.436781, 04.458395,
		04.480154, 04.502060, 04.524114, 04.546319, 04.568676,
		04.591187, 04.613854, 04.636678, 04.659662, 04.682807,
		04.706116, 04.729590, 04.753231, 04.777041, 04.801024,
		04.825179, 04.849511, 04.874020, 04.898710, 04.923582,
		04.948639, 04.973883, 04.999316, 05.024942, 05.050761,
		05.076778, 05.102993, 05.129411, 05.156034, 05.182864,
		05.209903, 05.237156, 05.264625, 05.292312, 05.320220,
		05.348354, 05.376714, 05.405306, 05.434131, 05.463193,
		05.492496, 05.522042, 05.551836, 05.581880, 05.612178,
		05.642734, 05.673552, 05.704634, 05.735986, 05.767610,
		05.799512, 05.831694, 05.864161, 05.896918, 05.929968,
		05.963316, 05.996967, 06.030925, 06.065194, 06.099780,
		06.134687, 06.169921, 06.205486, 06.241387, 06.277630,
		06.314220, 06.351163, 06.388465, 06.426130, 06.464166,
		06.502578, 06.541371, 06.580553, 06.620130, 06.660109,
		06.700495, 06.741297, 06.782520, 06.824173, 06.866262,
		06.908795, 06.951780, 06.995225, 07.039137, 07.083525,
		07.128398, 07.173764, 07.219632, 07.266011, 07.312910,
		07.360339, 07.408308, 07.456827, 07.505905, 07.555554,
		07.605785, 07.656608, 07.708035, 07.760077, 07.812747,
		07.866057, 07.920019, 07.974647, 08.029953, 08.085952,
		08.142657, 08.200083, 08.258245, 08.317158, 08.376837,
		08.437300, 08.498562, 08.560641, 08.623554, 08.687319,
		08.751955, 08.817481, 08.883916, 08.951282, 09.019600,
		09.088889, 09.159174, 09.230477, 09.302822, 09.376233,
		09.450735, 09.526355, 09.603118, 09.681054, 09.760191,
		09.840558, 09.922186, 10.005107, 10.089353, 10.174959,
		10.261958, 10.350389, 10.440287, 10.531693, 10.624646,
		10.719188, 10.815362, 10.913214, 11.012789, 11.114137,
		11.217307, 11.322352, 11.429325, 11.538283, 11.649285,
		11.762390, 11.877664, 11.995170, 12.114979, 12.237161,
		12.361791, 12.488946, 12.618708, 12.751161, 12.886394,
		13.024498, 13.165570, 13.309711, 13.457026, 13.607625,
		13.761625, 13.919145, 14.080314, 14.245263, 14.414134,
		14.587072, 14.764233, 14.945778, 15.131877, 15.322712,
		15.518470, 15.719353, 15.925570, 16.137345, 16.354912,
		16.578520, 16.808433, 17.044929, 17.288305, 17.538873,
		17.796967, 18.062943, 18.337176, 18.620068, 18.912049,
		19.213574, 19.525133, 19.847249, 20.180480, 20.525429,
		20.882738, 21.253102, 21.637266, 22.036036, 22.450278,
		22.880933, 23.329017, 23.795634, 24.281981, 24.789364,
		25.319207, 25.873062, 26.452634, 27.059789, 27.696581,
		28.365274, 29.068370, 29.808638, 30.589157, 31.413354,
		32.285060, 33.208568, 34.188705, 35.230920, 36.341388,
		37.527131, 38.796172, 40.157721, 41.622399, 43.202525,
		44.912465, 46.769077, 48.792279, 51.005773, 53.437996,
		56.123356, 59.103894
	};
	gnm_float X, U, V, RANLAN;
	int I;

	do {
		X = random_01 ();
	} while (X == 0);
	U = 1000 * X;
	I = U;
	U = U - I;

	if (I >= 70 && I <= 800)
		RANLAN = F[I] + U * (F[I + 1] - F[I]);
	else if (I >= 7 && I <= 980)
		RANLAN = F[I] + U * (F[I + 1] - F[I] -
				     GNM_const(0.25) * (1 - U) * (F[I + 2] - F[I + 1] -
								  F[I] + F[I - 1]));
	else if (I < 7) {
		V = gnm_log (X);
		U = 1 / V;
		RANLAN = ((GNM_const(0.99858950) + (GNM_const(3.45213058E1) + GNM_const(1.70854528E1) * U) * U) /
			  (1 + (GNM_const(3.41760202E1) + GNM_const(4.01244582) * U) * U)) *
			( -gnm_log ( GNM_const(-0.91893853) - V) - 1);
	} else {
		U = 1 - X;
		V = U * U;
		if (X <= GNM_const(0.999))
			RANLAN = (GNM_const(1.00060006) + GNM_const(2.63991156E2) *
				  U + GNM_const(4.37320068E3) * V) /
				((1 + GNM_const(2.57368075E2) * U + GNM_const(3.41448018E3) * V) * U);
		else
			RANLAN = (GNM_const(1.00001538) + GNM_const(6.07514119E3) * U +
				  GNM_const(7.34266409E5) * V) /
				((1 + GNM_const(6.06511919E3) * U + GNM_const(6.94021044E5) * V) * U);
	}

	return RANLAN;
}


/* ------------------------------------------------------------------------ */

/*
 * Generate a skew-normal distributed random number.
 *
 * based on the information provided at
 * http://azzalini.stat.unipd.it/SN/faq-r.html
 *
 */

gnm_float
random_skew_normal (gnm_float a)
{
	gnm_float result;
	gnm_float delta = a / gnm_hypot (1, a);
	gnm_float u = random_normal ();
	gnm_float v = random_normal ();

	result = delta * u + gnm_sqrt (1 - delta * delta) * v;

	return (u < 0 ? -result : result);
}


/* ------------------------------------------------------------------------ */

/*
 * Generate a skew-t distributed random number.
 *
 * based on the information provided at
 * http://azzalini.stat.unipd.it/SN/faq-r.html
 *
 */

gnm_float
random_skew_tdist (gnm_float nu, gnm_float a)
{
	gnm_float chi = random_chisq (nu);
	gnm_float z = random_skew_normal (a);

	return (z * gnm_sqrt(nu/chi));
}

/* ------------------------------------------------------------------------ */

/**
 * gnm_random_uniform_int:
 * @n: one more than the maximum number in range.
 *
 * Returns: a uniformly distributed random non-negative integer less than @n.
 */
guint32
gnm_random_uniform_int (guint32 n)
{
	guint32 left;

	g_return_val_if_fail (n > 0, 0);

	// This is the number of value that we need to reject in order to ensure
	// uniform distribution.  In the worst case we will end up rejecting
	// about half the numbers but for sane-sized ranges it is practically
	// zero.
	left = G_MAXUINT32 % n;

	while (TRUE) {
		guint32 r = random_32 ();
		if (r > G_MAXUINT32 - left)
			continue;
		return r % n;
	}
}

/**
 * gnm_random_uniform_integer:
 * @l: integer lower bound
 * @h: integer upper bound
 *
 * Returns: a uniformly distributed random integer in the range from
 * @l to @h (inclusively).
 */
gnm_float
gnm_random_uniform_integer (gnm_float l, gnm_float h)
{
	gnm_float range, res;

	if (l > h || !gnm_finite (l) || !gnm_finite (h))
		return gnm_nan;

	range = h - l + 1;
	if (range < G_MAXUINT32) {
		do {
			res = l + gnm_random_uniform_int (range);
		} while (res > h);
	} else {
		// This could be better
		do {
			res = l + gnm_floor (range * random_01 ());
		} while (res > h);
	}

	return res;
}
