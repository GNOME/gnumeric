/* glplpx4.c (problem scaling routines) */

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

#include <float.h>
#include <math.h>
#include <string.h>
#include "glplib.h"
#include "glplpx.h"

/*----------------------------------------------------------------------
-- eq_scal - implicit equilibration scaling.
--
-- *Synopsis*
--
-- static void eq_scal(int m, int n, void *info,
--    int (*mat)(void *info, int k, int ndx[], gnm_float val[]),
--    gnm_float R[], gnm_float S[], int ord);
--
-- *Description*
--
-- The routine eq_scal performs the implicit equilibration scaling of
-- the matrix R*A*S, where A is a given rectangular matrix, R and S are
-- given diagonal scaling matrices. The result of the scaling is the
-- matrix R'*A*S', where R' and S' are new scaling matrices computed by
-- the routine and stored on exit in the same arrays as the matrices R
-- and S.
--
-- Diagonal elements of the matrices R and S are stored in locations
-- R[1], ..., R[m] and S[1], ..., S[n] respectively, where m and n are
-- number of rows and number of columns of the matrix A. The locations
-- R[0] and S[0] are not used.
--
-- The parameter info is a transit pointer passed to the formal routine
-- mat (see below).
--
-- The formal routine mat specifies the given matrix A in both row- and
-- column-wise formats. In order to obtain an i-th row of the matrix A
-- the routine eq_scal calls the routine mat with the parameter k = +i,
-- 1 <= i <= m. In response the routine mat should store column indices
-- and values of (non-zero) elements of the i-th row to the locations
-- ndx[1], ..., ndx[len] and val[1], ..., val[len] respectively, where
-- len is number of non-zeros in the i-th row returned by the routine
-- mat on exit. Similarly, in order to obtain a j-th column the routine
-- mat is called with the parameter k = -j, 1 <= j <= n, and should
-- return the j-th column of the matrix A in the same way as explained
-- above for rows. Note that the routine mat is called more than once
-- for the same row and column numbers.
--
-- To perform equilibration scaling the routine eq_scal just divides
-- all elements of each row (column) by the largest absolute value of
-- elements in this row (column).
--
-- On entry the matrices R and S should be defined (if the matrix A is
-- unscaled, R and S should be untity matrices). On exit the routine
-- computes new matrices R' and S' that define the scaled matrix R'*A*S'
-- (thus scaling is implicit, since the matrix A is not changed).
--
-- The parameter ord defines the order of scaling:
--
-- if ord = 0, at first rows, then columns;
-- if ord = 1, at first columns, then rows. */

static void eq_scal(int m, int n, void *info,
      int (*mat)(void *info, int k, int ndx[], gnm_float val[]),
      gnm_float R[], gnm_float S[], int ord)
{     int i, j, len, t, pass, *ndx;
      gnm_float big, temp, *val;
      if (!(m > 0 && n > 0))
         fault("eq_scal: m = %d; n = %d; invalid parameters", m, n);
      ndx = ucalloc(1 + (m >= n ? m : n), sizeof(int));
      val = ucalloc(1 + (m >= n ? m : n), sizeof(gnm_float));
      for (pass = 0; pass <= 1; pass++)
      {  if (ord == pass)
         {  /* scale rows of the matrix R*A*S */
            for (i = 1; i <= m; i++)
            {  big = 0.0;
               /* obtain the i-th row of the matrix A */
               len = mat(info, +i, ndx, val);
               if (!(0 <= len && len <= n))
                  fault("eq_scal: i = %d; len = %d; invalid row length",
                     i, len);
               /* compute big = max(a[i,j]) for the i-th row */
               for (t = 1; t <= len; t++)
               {  j = ndx[t];
                  if (!(1 <= j && j <= n))
                     fault("eq_scal: i = %d; j = %d; invalid column ind"
                        "ex", i, j);
                  temp = R[i] * gnm_abs(val[t]) * S[j];
                  if (big < temp) big = temp;
               }
               /* scale the i-th row */
               if (big != 0.0) R[i] /= big;
            }
         }
         else
         {  /* scale columns of the matrix R*A*S */
            for (j = 1; j <= n; j++)
            {  big = 0.0;
               /* obtain the j-th column of the matrix A */
               len = mat(info, -j, ndx, val);
               if (!(0 <= len && len <= m))
                  fault("eq_scal: j = %d; len = %d; invalid column leng"
                     "th", j, len);
               /* compute big = max(a[i,j]) for the j-th column */
               for (t = 1; t <= len; t++)
               {  i = ndx[t];
                  if (!(1 <= i && i <= m))
                     fault("eq_scal: i = %d; j = %d; invalid row index",
                        i, j);
                  temp = R[i] * gnm_abs(val[t]) * S[j];
                  if (big < temp) big = temp;
               }
               /* scale the j-th column */
               if (big != 0.0) S[j] /= big;
            }
         }
      }
      ufree(ndx);
      ufree(val);
      return;
}

/*----------------------------------------------------------------------
-- gm_scal - implicit geometric mean scaling.
--
-- *Synopsis*
--
-- static void gm_scal(int m, int n, void *info,
--    int (*mat)(void *info, int k, int ndx[], gnm_float val[]),
--    gnm_float R[], gnm_float S[], int ord, int it_max, gnm_float eps);
--
-- *Description*
--
-- The routine gm_scal performs the implicit geometric mean scaling of
-- the matrix R*A*S, where A is a given rectangular matrix, R and S are
-- given diagonal scaling matrices. The result of the scaling is the
-- matrix R'*A*S', where R' and S' are new scaling matrices computed by
-- the routine and stored on exit in the same arrays as the matrices R
-- and S.
--
-- Diagonal elements of the matrices R and S are stored in locations
-- R[1], ..., R[m] and S[1], ..., S[n] respectively, where m and n are
-- number of rows and number of columns of the matrix A. The locations
-- R[0] and S[0] are not used.
--
-- The parameter info is a transit pointer passed to the formal routine
-- mat (see below).
--
-- The formal routine mat specifies the given matrix A in both row- and
-- column-wise formats. In order to obtain an i-th row of the matrix A
-- the routine gm_scal calls the routine mat with the parameter k = +i,
-- 1 <= i <= m. In response the routine mat should store column indices
-- and values of (non-zero) elements of the i-th row to the locations
-- ndx[1], ..., ndx[len] and val[1], ..., val[len] respectively, where
-- len is number of non-zeros in the i-th row returned by the routine
-- mat on exit. Similarly, in order to obtain a j-th column the routine
-- mat is called with the parameter k = -j, 1 <= j <= n, and should
-- return the j-th column of the matrix A in the same way as explained
-- above for rows. Note that the routine mat is called more than once
-- for the same row and column numbers.
--
-- To perform geometric mean scaling the routine gm_scal divides all
-- elements of each row (column) by gnm_sqrt(beta/alfa), where alfa and beta
-- are, respectively, smallest and largest absolute values of non-zero
-- elements of the corresponding row (column). In order to improve the
-- scaling quality the routine scales rows and columns several times.
--
-- On entry the matrices R and S should be defined (if the matrix A is
-- unscaled, R and S should be untity matrices). On exit the routine
-- computes new matrices R' and S' that define the scaled matrix R'*A*S'
-- (thus scaling is implicit, since the matrix A is not changed).
--
-- The parameter ord defines the order of scaling:
--
-- if ord = 0, at first rows, then columns;
-- if ord = 1, at first columns, then rows.
--
-- The parameter it_max defines maximal number of scaling iterations.
-- Recommended value it_max = 10 .. 50.
--
-- The parameter eps > 0 is a criterion used to decide when the scaling
-- process should stop. The process stops if the condition
--
--    t[k-1] - t[k] < eps * t[k-1]                                   (1)
--
-- becomes true, where
--
--    t[k] = beta[k] / alfa[k]                                       (2)
--
-- is the "quality" of scaling, alfa[k] and beta[k] are, respectively,
-- smallest and largest absolute values of (non-zero) elements of the
-- current matrix R'*A*S', k is the number of iteration. For most cases
-- eps = 0.10 .. 0.01 may be recommended.
--
-- The routine gm_scal prints the "quality" of scaling (2) on entry and
-- on exit. */

static void gm_scal(int m, int n, void *info,
      int (*mat)(void *info, int k, int ndx[], gnm_float val[]),
      gnm_float R[], gnm_float S[], int ord, int it_max, gnm_float eps)
{     int iter, i, j, len, t, pass, *ndx;
      gnm_float alfa, beta, told, tnew, temp, *val;
      if (!(m > 0 && n > 0))
         fault("gm_scal: m = %d; n = %d; invalid parameters", m, n);
      ndx = ucalloc(1 + (m >= n ? m : n), sizeof(int));
      val = ucalloc(1 + (m >= n ? m : n), sizeof(gnm_float));
      told = DBL_MAX;
      for (iter = 1; ; iter++)
      {  /* compute the scaling "quality" */
         alfa = DBL_MAX, beta = 0.0;
         for (i = 1; i <= m; i++)
         {  /* obtain the i-th row of the matrix A */
            len = mat(info, +i, ndx, val);
            if (!(0 <= len && len <= n)) goto err1;
            /* compute alfa = min(a[i,j]) and beta = max(a[i,j]) */
            for (t = 1; t <= len; t++)
            {  j = ndx[t];
               if (!(1 <= j && j <= n)) goto err2;
               temp = R[i] * gnm_abs(val[t]) * S[j];
               if (temp == 0.0) continue;
               if (alfa > temp) alfa = temp;
               if (beta < temp) beta = temp;
            }
         }
         tnew = (beta == 0.0 ? 1.0 : beta / alfa);
         /* print the initial scaling "quality" */
         if (iter == 1)
            print("gm_scal: max / min = %9.3e", tnew);
         /* check if the scaling process should stop */
         if (iter > it_max || told - tnew < eps * told)
         {  /* print the final scaling "quality" and leave the loop */
            print("gm_scal: max / min = %9.3e", tnew);
            break;
         }
         told = tnew;
         /* perform the next scaling iteration */
         for (pass = 0; pass <= 1; pass++)
         {  if (ord == pass)
            {  /* scale rows of the matrix R*A*S */
               for (i = 1; i <= m; i++)
               {  alfa = DBL_MAX, beta = 0.0;
                  /* obtain the i-th row of the matrix A */
                  len = mat(info, +i, ndx, val);
                  if (!(0 <= len && len <= n))
err1:                fault("gm_scal: i = %d; len = %d; invalid row leng"
                        "th", i, len);
                  /* compute alfa = min(a[i,j]) and beta = max(a[i,j])
                     for non-zero elements in the i-th row */
                  for (t = 1; t <= len; t++)
                  {  j = ndx[t];
                     if (!(1 <= j && j <= n))
err2:                   fault("gm_scal: i = %d; j = %d; invalid column "
                           "index", i, j);
                     temp = R[i] * gnm_abs(val[t]) * S[j];
                     if (temp == 0.0) continue;
                     if (alfa > temp) alfa = temp;
                     if (beta < temp) beta = temp;
                  }
                  /* scale the i-th row */
                  if (beta != 0.0) R[i] /= gnm_sqrt(alfa * beta);
               }
            }
            else
            {  /* scale columns of the matrix R*A*S */
               for (j = 1; j <= n; j++)
               {  alfa = DBL_MAX, beta = 0.0;
                  /* obtain the j-th column of the matrix A */
                  len = mat(info, -j, ndx, val);
                  if (!(0 <= len && len <= m))
                     fault("gm_scal: j = %d; len = %d; invalid column l"
                        "ength", j, len);
                  /* compute alfa = min(a[i,j]) and beta = max(a[i,j])
                     for non-zero elements in the j-th column */
                  for (t = 1; t <= len; t++)
                  {  i = ndx[t];
                     if (!(1 <= i && i <= m))
                        fault("gm_scal: i = %d; j = %d; invalid row ind"
                           "ex", i, j);
                     temp = R[i] * gnm_abs(val[t]) * S[j];
                     if (temp == 0.0) continue;
                     if (alfa > temp) alfa = temp;
                     if (beta < temp) beta = temp;
                  }
                  /* scale the j-th column */
                  if (beta != 0.0) S[j] /= gnm_sqrt(alfa * beta);
               }
            }
         }
      }
      ufree(ndx);
      ufree(val);
      return;
}

/*----------------------------------------------------------------------
-- lpx_scale_prob - scale problem data.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_scale_prob(LPX *lp);
--
-- *Description*
--
-- The routine lpx_scale_prob performs scaling of problem data for the
-- specified problem object.
--
-- The purpose of scaling is to replace the original constraint matrix
-- A by the scaled matrix A' = R*A*S, where R and S are diagonal scaling
-- matrices, in the hope that A' has better numerical properties than A.
--
-- May note that the scaling is implicit in the sense that the original
-- constraint matrix A is not changed. */

static int mat(void *info, int k, int ndx[], gnm_float val[])
{     /* this auxiliary routine obtains a required row or column of the
         original constraint matrix */
      LPX *lp = info;
      int m = lpx_get_num_rows(lp);
      int n = lpx_get_num_cols(lp);
      int i, j, len;
      if (k > 0)
      {  /* i-th row required */
         i = +k;
         insist(1 <= i && i <= m);
         len = lpx_get_mat_row(lp, i, ndx, val);
      }
      else
      {  /* j-th column required */
         j = -k;
         insist(1 <= j && j <= n);
         len = lpx_get_mat_col(lp, j, ndx, val);
      }
      return len;
}

void lpx_scale_prob(LPX *lp)
{     /* scale LP/MIP problem data */
      int m = lpx_get_num_rows(lp);
      int n = lpx_get_num_cols(lp);
      int sc_ord = 0, sc_max = 20;
      gnm_float sc_eps = 0.01; 
      int i, j;
      gnm_float *R, *S;
      /* initialize R := I and S := I */
      R = ucalloc(1+m, sizeof(gnm_float));
      S = ucalloc(1+n, sizeof(gnm_float));
      for (i = 1; i <= m; i++) R[i] = 1.0;
      for (j = 1; j <= n; j++) S[j] = 1.0;
      /* if the problem has no rows/columns, skip computations */
      if (m == 0 || n == 0) goto skip;
      /* compute the scaling matrices R and S */
      switch (lpx_get_int_parm(lp, LPX_K_SCALE))
      {  case 0:
            /* no scaling */
            break;
         case 1:
            /* equilibration scaling */
            eq_scal(m, n, lp, mat, R, S, sc_ord);
            break;
         case 2:
            /* geometric mean scaling */
            gm_scal(m, n, lp, mat, R, S, sc_ord, sc_max, sc_eps);
            break;
         case 3:
            /* geometric mean scaling, then equilibration scaling */
            gm_scal(m, n, lp, mat, R, S, sc_ord, sc_max, sc_eps);
            eq_scal(m, n, lp, mat, R, S, sc_ord);
            break;
         default:
            insist(lp != lp);
      }
skip: /* enter the scaling matrices R and S into the problem object and
         thereby perform implicit scaling */
      for (i = 1; i <= m; i++) lpx_set_rii(lp, i, R[i]);
      for (j = 1; j <= n; j++) lpx_set_sjj(lp, j, S[j]);
      ufree(R);
      ufree(S);
      return;
}

/*----------------------------------------------------------------------
-- lpx_unscale_prob - unscale problem data.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_unscale_prob(LPX *lp);
--
-- *Description*
--
-- The routine lpx_unscale_prob performs unscaling of problem data for
-- the specified problem object.
--
-- "Unscaling" means replacing the current scaling matrices R and S by
-- unity matrices that cancels the scaling effect. */

void lpx_unscale_prob(LPX *lp)
{     int m = lpx_get_num_rows(lp);
      int n = lpx_get_num_cols(lp);
      int i, j;
      for (i = 1; i <= m; i++) lpx_set_rii(lp, i, 1.0);
      for (j = 1; j <= n; j++) lpx_set_sjj(lp, j, 1.0);
      return;
}

/* eof */
