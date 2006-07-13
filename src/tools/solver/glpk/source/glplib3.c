/* glplib3.c */

/*----------------------------------------------------------------------
-- This code is part of GNU Linear Programming Kit (GLPK).
--
-- Copyright (C) 2000, 01, 02, 03, 04, 05, 06 Andrew Makhorin,
-- Department for Applied Informatics, Moscow Aviation Institute,
-- Moscow, Russia. All rights reserved. E-mail: <mao@mai2.rcnet.ru>.
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
-- 02110-1301, USA.
----------------------------------------------------------------------*/

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "glplib.h"

/*----------------------------------------------------------------------
-- str2int - convert character string to value of integer type.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- int str2int(char *str, int *val);
--
-- *Description*
--
-- The routine str2int converts the character string str to a value of
-- integer type and stores the value into location, which the parameter
-- val points to (in the case of error content of this location is not
-- changed).
--
-- *Returns*
--
-- The routine returns one of the following error codes:
--
-- 0 - no error;
-- 1 - value out of range;
-- 2 - character string is syntactically incorrect. */

int str2int(char *str, int *_val)
{     int d, k, s, val = 0;
      /* scan optional sign */
      if (str[0] == '+')
         s = +1, k = 1;
      else if (str[0] == '-')
         s = -1, k = 1;
      else
         s = +1, k = 0;
      /* check for the first digit */
      if (!isdigit((unsigned char)str[k])) return 2;
      /* scan digits */
      while (isdigit((unsigned char)str[k]))
      {  d = str[k++] - '0';
         if (s > 0)
         {  if (val > INT_MAX / 10) return 1;
            val *= 10;
            if (val > INT_MAX - d) return 1;
            val += d;
         }
         else
         {  if (val < INT_MIN / 10) return 1;
            val *= 10;
            if (val < INT_MIN + d) return 1;
            val -= d;
         }
      }
      /* check for terminator */
      if (str[k] != '\0') return 2;
      /* conversion is completed */
      *_val = val;
      return 0;
}

/*----------------------------------------------------------------------
-- str2dbl - convert character string to value of gnm_float type.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- int str2dbl(char *str, gnm_float *val);
--
-- *Description*
--
-- The routine str2dbl converts the character string str to a value of
-- gnm_float type and stores the value into location, which the parameter
-- val points to (in the case of error content of this location is not
-- changed).
--
-- *Returns*
--
-- The routine returns one of the following error codes:
--
-- 0 - no error;
-- 1 - value out of range;
-- 2 - character string is syntactically incorrect. */

int str2dbl(char *str, gnm_float *_val)
{     int k;
      gnm_float val;
      /* scan optional sign */
      k = (str[0] == '+' || str[0] == '-' ? 1 : 0);
      /* check for decimal point */
      if (str[k] == '.')
      {  k++;
         /* a digit should follow it */
         if (!isdigit((unsigned char)str[k])) return 2;
         k++;
         goto frac;
      }
      /* integer part should start with a digit */
      if (!isdigit((unsigned char)str[k])) return 2;
      /* scan integer part */
      while (isdigit((unsigned char)str[k])) k++;
      /* check for decimal point */
      if (str[k] == '.') k++;
frac: /* scan optional fraction part */
      while (isdigit((unsigned char)str[k])) k++;
      /* check for decimal exponent */
      if (str[k] == 'E' || str[k] == 'e')
      {  k++;
         /* scan optional sign */
         if (str[k] == '+' || str[k] == '-') k++;
         /* a digit should follow E, E+ or E- */
         if (!isdigit((unsigned char)str[k])) return 2;
      }
      /* scan optional exponent part */
      while (isdigit((unsigned char)str[k])) k++;
      /* check for terminator */
      if (str[k] != '\0') return 2;
      /* perform conversion */
      {  char *endptr;
         val = strtod(str, &endptr);
         if (*endptr != '\0') return 2;
      }
      /* check for overflow */
      if (!(-DBL_MAX <= val && val <= +DBL_MAX)) return 1;
      /* check for underflow */
      if (-DBL_MIN < val && val < +DBL_MIN) val = 0.0;
      /* conversion is completed */
      *_val = val;
      return 0;
}

/*----------------------------------------------------------------------
-- strspx - remove all spaces from character string.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- char *strspx(char *str);
--
-- *Description*
--
-- The routine strspx removes all spaces from the character string str.
--
-- *Examples*
--
-- strspx("   Errare   humanum   est   ") => "Errarehumanumest"
--
-- strspx("      ")                       => ""
--
-- *Returns*
--
-- The routine returns a pointer to the character string. */

char *strspx(char *str)
{     char *s, *t;
      for (s = t = str; *s; s++) if (*s != ' ') *t++ = *s;
      *t = '\0';
      return str;
}

/*----------------------------------------------------------------------
-- strtrim - remove trailing spaces from character string.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- char *strtrim(char *str);
--
-- *Description*
--
-- The routine strtrim removes trailing spaces from the character
-- string str.
--
-- *Examples*
--
-- strtrim("Errare humanum est   ") => "Errare humanum est"
--
-- strtrim("      ")                => ""
--
-- *Returns*
--
-- The routine returns a pointer to the character string. */

char *strtrim(char *str)
{     char *t;
      for (t = strrchr(str, '\0') - 1; t >= str; t--)
      {  if (*t != ' ') break;
         *t = '\0';
      }
      return str;
}

/*----------------------------------------------------------------------
-- fp2rat - convert floating-point number to rational number.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- int fp2rat(gnm_float x, gnm_float eps, gnm_float *p, gnm_float *q);
--
-- *Description*
--
-- Given a floating-point number 0 <= x < 1 the routine fp2rat finds
-- its "best" rational approximation p / q, where p >= 0 and q > 0 are
-- integer numbers, such that |x - p / q| <= eps.
--
-- *Example*
--
-- For x = gnm_sqrt(2) - 1 = 0.414213562373095 and eps = 1e-6 the routine
-- gives p = 408 and q = 985, where 408 / 985 = 0.414213197969543.
--
-- *Returns*
--
-- The routine fp2rat returns the number of iterations used to achieve
-- the specified precision eps.
--
-- *Background*
--
-- It is well known that every positive real number x can be expressed
-- as the following continued fraction:
--
--    x = b[0] + a[1]
--               ------------------------
--               b[1] + a[2]
--                      -----------------
--                      b[2] + a[3]
--                             ----------
--                             b[3] + ...
--
-- where:
--
--    a[k] = 1,                  k = 0, 1, 2, ...
--
--    b[k] = gnm_floor(x[k]),        k = 0, 1, 2, ...
--
--    x[0] = x,
--
--    x[k] = 1 / frac(x[k-1]),   k = 1, 2, 3, ...
--
-- To find the "best" rational approximation of x the routine computes
-- partial fractions f[k] by dropping after k terms as follows:
--
--    f[k] = A[k] / B[k],
--
-- where:
--
--    A[-1] = 1,   A[0] = b[0],   B[-1] = 0,   B[0] = 1,
--
--    A[k] = b[k] * A[k-1] + a[k] * A[k-2],
--
--    B[k] = b[k] * B[k-1] + a[k] * B[k-2].
--
-- Once the condition
--
--    |x - f[k]| <= eps
--
-- has been satisfied, the routine reports p = A[k] and q = B[k] as the
-- final answer.
--
-- In the table below here is some statistics obtained for one million
-- random numbers uniformly distributed in the range [0, 1).
--
--     eps      max p   mean p      max q    mean q  max k   mean k
--    -------------------------------------------------------------
--    1e-1          8      1.6          9       3.2    3      1.4
--    1e-2         98      6.2         99      12.4    5      2.4
--    1e-3        997     20.7        998      41.5    8      3.4
--    1e-4       9959     66.6       9960     133.5   10      4.4
--    1e-5      97403    211.7      97404     424.2   13      5.3
--    1e-6     479669    669.9     479670    1342.9   15      6.3
--    1e-7    1579030   2127.3    3962146    4257.8   16      7.3
--    1e-8   26188823   6749.4   26188824   13503.4   19      8.2
--
-- *References*
--
-- W. B. Jones and W. J. Thron, "Continued Fractions: Analytic Theory
-- and Applications," Encyclopedia on Mathematics and Its Applications,
-- Addison-Wesley, 1980. */

int fp2rat(gnm_float x, gnm_float eps, gnm_float *p, gnm_float *q)
{     int k;
      gnm_float xk, Akm1, Ak, Bkm1, Bk, ak, bk, fk, temp;
      if (!(0.0 <= x && x < 1.0))
         fault("fp2rat: x = %g; number out of range", x);
      for (k = 0; ; k++)
      {  insist(k <= 100);
         if (k == 0)
         {  /* x[0] = x */
            xk = x;
            /* A[-1] = 1 */
            Akm1 = 1.0;
            /* A[0] = b[0] = gnm_floor(x[0]) = 0 */
            Ak = 0.0;
            /* B[-1] = 0 */
            Bkm1 = 0.0;
            /* B[0] = 1 */
            Bk = 1.0;
         }
         else
         {  /* x[k] = 1 / frac(x[k-1]) */
            temp = xk - gnm_floor(xk);
            insist(temp != 0.0);
            xk = 1.0 / temp;
            /* a[k] = 1 */
            ak = 1.0;
            /* b[k] = gnm_floor(x[k]) */
            bk = gnm_floor(xk);
            /* A[k] = b[k] * A[k-1] + a[k] * A[k-2] */
            temp = bk * Ak + ak * Akm1;
            Akm1 = Ak, Ak = temp;
            /* B[k] = b[k] * B[k-1] + a[k] * B[k-2] */
            temp = bk * Bk + ak * Bkm1;
            Bkm1 = Bk, Bk = temp;
         }
         /* f[k] = A[k] / B[k] */
         fk = Ak / Bk;
#if 0
         print("%.*g / %.*g = %.*g", DBL_DIG, Ak, DBL_DIG, Bk, DBL_DIG,
            fk);
#endif
         if (gnm_abs(x - fk) <= eps) break;
      }
      *p = Ak;
      *q = Bk;
      return k;
}

/* eof */
