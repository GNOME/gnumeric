/* glplib4.c */

/*----------------------------------------------------------------------
-- This file is part of GLPK (GNU Linear Programming Kit).
--
-- This module is a modified version of the module GB_FLIP, a portable
-- pseudo-random number generator. The original version of GB_FLIP is a
-- part of The Stanford GraphBase developed by Donald E. Knuth (see
-- http://www-cs-staff.stanford.edu/~knuth/sgb.html).
--
-- Note that all changes concern only external names, so this modified
-- version provides exactly the same results as the original version.
--
-- Changes were made by Andrew Makhorin <mao@mai2.rcnet.ru>.
--
-- GLPK is free software; you can redistribute it and/or modify it
-- under the terms of the GNU General Public License as published by
-- the Free Software Foundation; either version 2, or (at your option)
-- any later version.
--
-- GLPK is distributed in the hope that it will be useful, but WITHOUT
-- ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
-- or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
-- License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with GLPK; see the file COPYING. If not, write to the Free
-- Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
-- 02110-1301  USA.
----------------------------------------------------------------------*/

#include "glplib.h"

#if 0
int A[56] = { -1 };
#else
#define A (env->rand_val)
#endif
/* pseudo-random values */

#if 0
int *fptr = A;
#else
#define fptr (env->next_val)
#endif
/* the next A value to be exported */

#define mod_diff(x, y) (((x) - (y)) & 0x7FFFFFFF)
/* difference modulo 2^31 */

static int flip_cycle(LIBENV *env)
{     /* this is an auxiliary routine to do 55 more steps of the basic
         recurrence, at high speed, and to reset fptr */
      int *ii, *jj;
      for (ii = &A[1], jj = &A[32]; jj <= &A[55]; ii++, jj++)
         *ii = mod_diff(*ii, *jj);
      for (jj = &A[1]; ii <= &A[55]; ii++, jj++)
         *ii = mod_diff(*ii, *jj);
      fptr = &A[54];
      return A[55];
}

/*----------------------------------------------------------------------
-- lib_init_rand - initialize pseudo-random number generator.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- void lib_init_rand(int seed);
--
-- *Description*
--
-- The routine lib_init_rand initializes the pseudo-random number
-- generator. The parameter seed may be any integer number. Note that
-- on initializing the library environment this routine is called with
-- the parameter seed equal to zero. */

void lib_init_rand(int seed)
{     LIBENV *env = lib_env_ptr();
      int i;
      int prev = seed, next = 1;
      seed = prev = mod_diff(prev, 0);
      A[55] = prev;
      for (i = 21; i; i = (i + 21) % 55)
      {  A[i] = next;
         next = mod_diff(prev, next);
         if (seed & 1)
            seed = 0x40000000 + (seed >> 1);
         else
            seed >>= 1;
         next = mod_diff(next, seed);
         prev = A[i];
      }
      flip_cycle(env);
      flip_cycle(env);
      flip_cycle(env);
      flip_cycle(env);
      flip_cycle(env);
      return;
}

/*----------------------------------------------------------------------
-- lib_next_rand - obtain pseudo-random integer on [0, 2^31-1].
--
-- *Synopsis*
--
-- #include "glplib.h"
-- int lib_next_rand(void);
--
-- *Returns*
--
-- The routine lib_next_rand returns a next pseudo-random integer which
-- is uniformly distributed between 0 and 2^31-1, inclusive. The period
-- length of the generated numbers is 2^85 - 2^30. The low order bits of
-- the generated numbers are just as random as the high-order bits. */

int lib_next_rand(void)
{     LIBENV *env = lib_env_ptr();
      return *fptr >= 0 ? *fptr-- : flip_cycle(env);
}

/*----------------------------------------------------------------------
-- lib_unif_rand - obtain pseudo-random integer on [0, m-1].
--
-- *Synopsis*
--
-- #include "glplib.h"
-- int lib_unif_rand(int m);
--
-- *Returns*
--
-- The routine lib_unif_rand returns a next pseudo-random integer which
-- is uniformly distributed between 0 and m-1, inclusive, where m is any
-- positive integer less than 2^31. */

#define two_to_the_31 ((unsigned int)0x80000000)

int lib_unif_rand(int m)
{     unsigned int t = two_to_the_31 - (two_to_the_31 % m);
      int r;
      do { r = lib_next_rand(); } while (t <= (unsigned int)r);
      return r % m;
}

/**********************************************************************/

#if 0
int main(void)
{     /* to be sure that this version provides the same results as the
         original version, run this validation program */
      int j;
      lib_init_rand(-314159);
      if (lib_next_rand() != 119318998)
      {  print("Failure on the first try!");
         return -1;
      }
      for (j = 1; j <= 133; j++) lib_next_rand();
      if (lib_unif_rand(0x55555555) != 748103812)
      {  print("Failure on the second try!");
         return -2;
      }
      print("OK, the random-number generator routines seem to work!");
      return 0;
}
#endif

/* eof */
