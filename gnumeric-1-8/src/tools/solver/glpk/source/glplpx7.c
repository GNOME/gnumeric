/* glplpx7.c (LP basis and simplex table routines) */

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
#include <stddef.h>
#include "glplib.h"
#include "glplpx.h"

/*----------------------------------------------------------------------
-- lpx_invert - compute factorization of basis matrix.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_invert(LPX *lp);
--
-- *Description*
--
-- The routine lpx_invert computes factorization of the current basis
-- matrix for the specified problem object.
--
-- *Returns*
--
-- The routine lpx_invert returns one of the following error codes:
--
-- 0 - factorization has been successfully computed;
-- 1 - the basis matrix is singular;
-- 2 - the basis matrix is ill-conditioned;
-- 3 - either the problem object has no rows or the current basis is
--     invalid (the number of basic variables is not the same as the
--     number of rows). */

struct invert_info
{     /* transitional info passed to the routine basic_column */
      LPX *lp;
      /* pointer to the problem object */
      int *basis; /* int basis[1+m]; */
      /* list of basic variables (basis header) */
};

static int basic_column(void *_info, int j, int rn[], gnm_float bj[])
{     /* this routine returns row indices and numeric values of
         non-zero elements in j-th column of the basis matrix to be
         factorized */
      struct invert_info *info = _info;
      int k, m, t, len;
      gnm_float rii, sjj;
      m = lpx_get_num_rows(info->lp);
      /* determine the ordinal number of basic auxiliary or structural
         variable, which corresponds to j-th basic variable xB[j] */
      insist(1 <= j && j <= m);
      k = info->basis[j];
      /* construct j-th column of the basis matrix */
      if (k <= m)
      {  /* x[k] is auxiliary variable */
         len = 1;
         rn[1] = k;
         bj[1] = 1.0;
      }
      else
      {  /* x[k] is structural variable */
         len = lpx_get_mat_col(info->lp, k-m, rn, bj);
         /* scale the column and change its sign */
         sjj = lpx_get_sjj(info->lp, k-m);
         for (t = 1; t <= len; t++)
         {  rii = lpx_get_rii(info->lp, rn[t]);
            bj[t] *= - rii * sjj;
         }
      }
      return len;
}

int lpx_invert(LPX *lp)
{     /* compute factorization of basis matrix */
      struct invert_info info;
      INV *b_inv;
      int i, k, m, n, ret, stat, *basis;
      m = lpx_get_num_rows(lp);
      n = lpx_get_num_cols(lp);
      /* build the list of basic variables (basis header) */
      basis = ucalloc(1+m, sizeof(int));
      i = 0;
      for (k = 1; k <= m+n; k++)
      {  if (k <= m)
            stat = lpx_get_row_stat(lp, k);
         else
            stat = lpx_get_col_stat(lp, k-m);
         if (stat == LPX_BS)
         {  i++;
            if (i > m)
            {  /* too many basic variables */
               ret = 3;
               goto fini;
            }
            basis[i] = k;
         }
      }
      if (i < m)
      {  /* too few basic variables */
         ret = 3;
         goto fini;
      }
      /* access factorization data structure */
      b_inv = lpx_access_inv(lp);
      /* if the factorization has wrong size, delete it */
      if (b_inv != NULL && b_inv->m != m)
         inv_delete(b_inv), b_inv = NULL;
      /* if the factorization does not exist, create it */
      if (m == 0)
      {  /* the problem object has no rows */
         ret = 3;
         goto fini;
      }
      if (b_inv == NULL) b_inv = inv_create(m, 50);
      /* try to factorize the basis matrix */
      info.lp = lp;
      info.basis = basis;
      ret = inv_decomp(b_inv, &info, basic_column);
      insist(ret == 0 || ret == 1 || ret == 2);
fini: /* put the basis back to the problem object */
      lpx_put_lp_basis(lp, (ret == 0 ? LPX_B_VALID : LPX_B_UNDEF),
         basis, b_inv);
      ufree(basis);
      return ret;
}

/*----------------------------------------------------------------------
-- lpx_ftran - forward transformation (solve system B*x = b).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_ftran(LPX *lp, gnm_float x[]);
--
-- *Description*
--
-- The routine lpx_ftran performs forward transformation, i.e. solves
-- the system B*x = b, where B is the basis matrix corresponding to the
-- current basis for the specified problem object, x is the vector of
-- unknowns to be computed, b is the vector of right-hand sides.
--
-- On entry elements of the vector b should be stored in dense format
-- in locations x[1], ..., x[m], where m is the number of rows. On exit
-- the routine stores elements of the vector x in the same locations.
--
-- *Scaling/Unscaling*
--
-- Let A~ = (I | -A) is the augmented constraint matrix of the original
-- (unscaled) problem. In the scaled LP problem instead the matrix A the
-- scaled matrix A" = R*A*S is actually used, so
--
--    A~" = (I | A") = (I | R*A*S) = (R*I*inv(R) | R*A*S) =
--                                                                   (1)
--        = R*(I | A)*S~ = R*A~*S~,
--
-- is the scaled augmented constraint matrix, where R and S are diagonal
-- scaling matrices used to scale rows and columns of the matrix A, and
--
--    S~ = diag(inv(R) | S)                                          (2)
--
-- is an augmented diagonal scaling matrix.
--
-- By definition:
--
--    A~ = (B | N),                                                  (3)
--
-- where B is the basic matrix, which consists of basic columns of the
-- augmented constraint matrix A~, and N is a matrix, which consists of
-- non-basic columns of A~. From (1) it follows that:
--
--    A~" = (B" | N") = (R*B*SB | R*N*SN),                           (4)
--
-- where SB and SN are parts of the augmented scaling matrix S~, which
-- correspond to basic and non-basic variables, respectively. Therefore
--
--    B" = R*B*SB,                                                   (5)
--
-- which is the scaled basis matrix. */

void lpx_ftran(LPX *lp, gnm_float x[])
{     /* B*x = b ===> (R*B*SB)*(inv(SB)*x) = R*b ===> B"*x" = b",
         where b" = R*b, x = SB*x" */
      INV *b_inv;
      int i, k, m;
      if (!lpx_is_b_avail(lp))
         fault("lpx_ftran: LP basis is not available");
      m = lpx_get_num_rows(lp);
      /* compute b" = R*b */
      for (i = 1; i <= m; i++)
      {  if (x[i] == 0.0) continue;
         x[i] *= lpx_get_rii(lp, i);
      }
      /* solve the system B"*x" = b" */
      b_inv = lpx_access_inv(lp);
      insist(b_inv != NULL);
      insist(b_inv->m == m);
      insist(b_inv->valid);
      inv_ftran(b_inv, x, 0);
      /* compute x = SB*x" */
      for (i = 1; i <= m; i++)
      {  if (x[i] == 0.0) continue;
         k = lpx_get_b_info(lp, i); /* x[k] = xB[i] */
         if (k <= m)
            x[i] /= lpx_get_rii(lp, k);
         else
            x[i] *= lpx_get_sjj(lp, k-m);
      }
      return;
}

/*----------------------------------------------------------------------
-- lpx_btran - backward transformation (solve system B'*x = b).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_btran(LPX *lp, gnm_float x[]);
--
-- *Description*
--
-- The routine lpx_btran performs backward transformation, i.e. solves
-- the system B'*x = b, where B' is a matrix transposed to the basis
-- matrix corresponding to the current basis for the specified problem
-- problem object, x is the vector of unknowns to be computed, b is the
-- vector of right-hand sides.
--
-- On entry elements of the vector b should be stored in dense format
-- in locations x[1], ..., x[m], where m is the number of rows. On exit
-- the routine stores elements of the vector x in the same locations.
--
-- *Scaling/Unscaling*
--
-- See comments to the routine lpx_ftran. */

void lpx_btran(LPX *lp, gnm_float x[])
{     /* B'*x = b ===> (SB*B'*R)*(inv(R)*x) = SB*b ===> (B")'*x" = b",
         where b" = SB*b, x = R*x" */
      INV *b_inv;
      int i, k, m;
      if (!lpx_is_b_avail(lp))
         fault("lpx_btran: LP basis is not available");
      m = lpx_get_num_rows(lp);
      /* compute b" = SB*b */
      for (i = 1; i <= m; i++)
      {  if (x[i] == 0.0) continue;
         k = lpx_get_b_info(lp, i); /* x[k] = xB[i] */
         if (k <= m)
            x[i] /= lpx_get_rii(lp, k);
         else
            x[i] *= lpx_get_sjj(lp, k-m);
      }
      /* solve the system (B")'*x" = b" */
      b_inv = lpx_access_inv(lp);
      insist(b_inv != NULL);
      insist(b_inv->m == m);
      insist(b_inv->valid);
      inv_btran(b_inv, x);
      /* compute x = R*x" */
      for (i = 1; i <= m; i++)
      {  if (x[i] == 0.0) continue;
         x[i] *= lpx_get_rii(lp, i);
      }
      return;
}

/*----------------------------------------------------------------------
-- lpx_eval_b_prim - compute primal basic solution components.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_eval_b_prim(LPX *lp, gnm_float row_prim[], gnm_float col_prim[]);
--
-- *Description*
--
-- The routine lpx_eval_b_prim computes primal values of all auxiliary
-- and structural variables for the specified problem object.
--
-- NOTE: This routine is intended for internal use only.
--
-- *Background*
--
-- By definition (see glplpx.h):
--
--    B * xB + N * xN = 0,                                           (1)
--
-- where the (basis) matrix B and the matrix N are built of columns of
-- the augmented constraint matrix A~ = (I | -A).
--
-- The formula (1) allows computing primal values of basic variables xB
-- as follows:
--
--    xB = - inv(B) * N * xN,                                        (2)
--
-- where values of non-basic variables xN are defined by corresponding
-- settings for the current basis. */

void lpx_eval_b_prim(LPX *lp, gnm_float row_prim[], gnm_float col_prim[])
{     int i, j, k, m, n, stat, len, *ind;
      gnm_float xN, *NxN, *xB, *val;
      if (!lpx_is_b_avail(lp))
         fault("lpx_eval_b_prim: LP basis is not available");
      m = lpx_get_num_rows(lp);
      n = lpx_get_num_cols(lp);
      /* store values of non-basic auxiliary and structural variables
         and compute the right-hand side vector (-N*xN) */
      NxN = ucalloc(1+m, sizeof(gnm_float));
      for (i = 1; i <= m; i++) NxN[i] = 0.0;
      /* walk through auxiliary variables */
      for (i = 1; i <= m; i++)
      {  /* obtain status of i-th auxiliary variable */
         stat = lpx_get_row_stat(lp, i);
         /* if it is basic, skip it */
         if (stat == LPX_BS) continue;
         /* i-th auxiliary variable is non-basic; get its value */
         switch (stat)
         {  case LPX_NL: xN = lpx_get_row_lb(lp, i); break;
            case LPX_NU: xN = lpx_get_row_ub(lp, i); break;
            case LPX_NF: xN = 0.0; break;
            case LPX_NS: xN = lpx_get_row_lb(lp, i); break;
            default: insist(lp != lp);
         }
         /* store the value of non-basic auxiliary variable */
         row_prim[i] = xN;
         /* and add corresponding term to the right-hand side vector */
         NxN[i] -= xN;
      }
      /* walk through structural variables */
      ind = ucalloc(1+m, sizeof(int));
      val = ucalloc(1+m, sizeof(gnm_float));
      for (j = 1; j <= n; j++)
      {  /* obtain status of j-th structural variable */
         stat = lpx_get_col_stat(lp, j);
         /* if it basic, skip it */
         if (stat == LPX_BS) continue;
         /* j-th structural variable is non-basic; get its value */
         switch (stat)
         {  case LPX_NL: xN = lpx_get_col_lb(lp, j); break;
            case LPX_NU: xN = lpx_get_col_ub(lp, j); break;
            case LPX_NF: xN = 0.0; break;
            case LPX_NS: xN = lpx_get_col_lb(lp, j); break;
            default: insist(lp != lp);
         }
         /* store the value of non-basic structural variable */
         col_prim[j] = xN;
         /* and add corresponding term to the right-hand side vector */
         if (xN != 0.0)
         {  len = lpx_get_mat_col(lp, j, ind, val);
            for (k = 1; k <= len; k++) NxN[ind[k]] += val[k] * xN;
         }
      }
      ufree(ind);
      ufree(val);
      /* solve the system B*xB = (-N*xN) to compute the vector xB */
      xB = NxN, lpx_ftran(lp, xB);
      /* store values of basic auxiliary and structural variables */
      for (i = 1; i <= m; i++)
      {  k = lpx_get_b_info(lp, i);
         insist(1 <= k && k <= m+n);
         if (k <= m)
            row_prim[k] = xB[i];
         else
            col_prim[k-m] = xB[i];
      }
      ufree(NxN);
      return;
}

/*----------------------------------------------------------------------
-- lpx_eval_b_dual - compute dual basic solution components.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_eval_b_dual(LPX *lp, gnm_float row_dual[], gnm_float col_dual[]);
--
-- *Description*
--
-- The routine lpx_eval_b_dual computes dual values (that is reduced
-- costs) of all auxiliary and structural variables for the specified
-- problem object.
--
-- NOTE: This routine is intended for internal use only.
--
-- *Background*
--
-- By definition (see glplpx.h):
--
--    B * xB + N * xN = 0,                                           (1)
--
-- where the (basis) matrix B and the matrix N are built of columns of
-- the augmented constraint matrix A~ = (I | -A).
--
-- The objective function can be written as:
--
--    Z = cB' * xB + cN' * xN + c0,                                  (2)
--
-- where cB and cN are objective coefficients at, respectively, basic
-- and non-basic variables.
--
-- From (1) it follows that:
--
--    xB = - inv(B) * N * xN,                                        (3)
--
-- so substituting xB from (3) to (2) we have:
--
--    Z = - cB' * inv(B) * N * xN + cN' * xN + c0 =
--
--      = (cN' - cB' * inv(B) * N) * xN + c0 =
--                                                                   (4)
--      = (cN - N' * inv(B') * cB)' * xN + c0 =
--
--      = d' * xN + c0,
--
-- where
--
--    d = cN - N' * inv(B') * cB                                     (5)
--
-- is the vector of dual values (reduced costs) of non-basic variables.
--
-- The routine first computes the vector pi:
--
--    pi = inv(B') * cB,                                             (6)
--
-- and then computes the vector d as follows:
--
--    d = cN - N' * pi.                                              (7)
--
-- Note that dual values of basic variables are zero by definition. */

void lpx_eval_b_dual(LPX *lp, gnm_float row_dual[], gnm_float col_dual[])
{     int i, j, k, m, n, len, *ind;
      gnm_float dj, *cB, *pi, *val;
      if (!lpx_is_b_avail(lp))
         fault("lpx_eval_b_dual: LP basis is not available");
      m = lpx_get_num_rows(lp);
      n = lpx_get_num_cols(lp);
      /* store zero reduced costs of basic auxiliary and structural
         variables and build the vector cB of objective coefficients at
         basic variables */
      cB = ucalloc(1+m, sizeof(gnm_float));
      for (i = 1; i <= m; i++)
      {  k = lpx_get_b_info(lp, i);
         /* xB[i] is k-th original variable */
         insist(1 <= k && k <= m+n);
         if (k <= m)
         {  row_dual[k] = 0.0;
            cB[i] = 0.0;
         }
         else
         {  col_dual[k-m] = 0.0;
            cB[i] = lpx_get_obj_coef(lp, k-m);
         }
      }
      /* solve the system B'*pi = cB to compute the vector pi */
      pi = cB, lpx_btran(lp, pi);
      /* compute reduced costs of non-basic auxiliary variables */
      for (i = 1; i <= m; i++)
      {  if (lpx_get_row_stat(lp, i) != LPX_BS)
            row_dual[i] = - pi[i];
      }
      /* compute reduced costs of non-basic structural variables */
      ind = ucalloc(1+m, sizeof(int));
      val = ucalloc(1+m, sizeof(gnm_float));
      for (j = 1; j <= n; j++)
      {  if (lpx_get_col_stat(lp, j) != LPX_BS)
         {  dj = lpx_get_obj_coef(lp, j);
            len = lpx_get_mat_col(lp, j, ind, val);
            for (k = 1; k <= len; k++) dj += val[k] * pi[ind[k]];
            col_dual[j] = dj;
         }
      }
      ufree(ind);
      ufree(val);
      ufree(cB);
      return;
}

/*----------------------------------------------------------------------
-- lpx_warm_up - "warm up" LP basis.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_warm_up(LPX *lp);
--
-- *Description*
--
-- The routine lpx_warm_up "warms up" the LP basis for the specified
-- problem object using current statuses assigned to rows and columns
-- (i.e. to auxiliary and structural variables).
--
-- "Warming up" includes reinverting (factorizing) the basis matrix (if
-- neccesary), computing primal and dual components of basic solution,
-- and determining primal and dual statuses of the basic solution.
--
-- *Returns*
--
-- The routine lpx_warm_up returns one of the following exit codes:
--
-- LPX_E_OK       the LP basis has been successfully "warmed up".
--
-- LPX_E_EMPTY    the problem has no rows and/or columns.
--
-- LPX_E_BADB     the LP basis is invalid, because the number of basic
--                variables is not the same as the number of rows.
--
-- LPX_E_SING     the basis matrix is singular or ill-conditioned. */

int lpx_warm_up(LPX *lp)
{     int m, n, k, ret, type, stat, p_stat, d_stat;
      gnm_float lb, ub, prim, dual, tol_bnd, tol_dj, dir;
      gnm_float *row_prim, *row_dual, *col_prim, *col_dual;
      m = lpx_get_num_rows(lp);
      n = lpx_get_num_cols(lp);
      /* reinvert the basis matrix, if necessary */
      if (lpx_is_b_avail(lp))
         ret = LPX_E_OK;
      else
      {  if (m == 0 || n == 0)
         {  ret = LPX_E_EMPTY;
            goto done;
         }
         ret = lpx_invert(lp);
         switch (ret)
         {  case 0:
               ret = LPX_E_OK;
               break;
            case 1:
            case 2:
               ret = LPX_E_SING;
               goto done;
            default:
               insist(ret != ret);
         }
      }
      /* allocate working arrays */
      row_prim = ucalloc(1+m, sizeof(gnm_float));
      row_dual = ucalloc(1+m, sizeof(gnm_float));
      col_prim = ucalloc(1+n, sizeof(gnm_float));
      col_dual = ucalloc(1+n, sizeof(gnm_float));
      /* compute primal basic solution components */
      lpx_eval_b_prim(lp, row_prim, col_prim);
      /* determine primal status of basic solution */
      tol_bnd = 3.0 * lpx_get_real_parm(lp, LPX_K_TOLBND);
      p_stat = LPX_P_FEAS;
      for (k = 1; k <= m+n; k++)
      {  if (k <= m)
         {  type = lpx_get_row_type(lp, k);
            lb = lpx_get_row_lb(lp, k);
            ub = lpx_get_row_ub(lp, k);
            prim = row_prim[k];
         }
         else
         {  type = lpx_get_col_type(lp, k-m);
            lb = lpx_get_col_lb(lp, k-m);
            ub = lpx_get_col_ub(lp, k-m);
            prim = col_prim[k-m];
         }
         if (type == LPX_LO || type == LPX_DB || type == LPX_FX)
         {  /* variable x[k] has lower bound */
            if (prim < lb - tol_bnd * (1.0 + gnm_abs(lb)))
            {  p_stat = LPX_P_INFEAS;
               break;
            }
         }
         if (type == LPX_UP || type == LPX_DB || type == LPX_FX)
         {  /* variable x[k] has upper bound */
            if (prim > ub + tol_bnd * (1.0 + gnm_abs(ub)))
            {  p_stat = LPX_P_INFEAS;
               break;
            }
         }
      }
      /* compute dual basic solution components */
      lpx_eval_b_dual(lp, row_dual, col_dual);
      /* determine dual status of basic solution */
      tol_dj = 3.0 * lpx_get_real_parm(lp, LPX_K_TOLDJ);
      dir = (lpx_get_obj_dir(lp) == LPX_MIN ? +1.0 : -1.0);
      d_stat = LPX_D_FEAS;
      for (k = 1; k <= m+n; k++)
      {  if (k <= m)
         {  stat = lpx_get_row_stat(lp, k);
            dual = row_dual[k];
         }
         else
         {  stat = lpx_get_col_stat(lp, k-m);
            dual = col_dual[k-m];
         }
         if (stat == LPX_BS || stat == LPX_NL || stat == LPX_NF)
         {  /* reduced cost of x[k] must be non-negative (minimization)
               or non-positive (maximization) */
            if (dir * dual < - tol_dj)
            {  d_stat = LPX_D_INFEAS;
               break;
            }
         }
         if (stat == LPX_BS || stat == LPX_NU || stat == LPX_NF)
         {  /* reduced cost of x[k] must be non-positive (minimization)
               or non-negative (maximization) */
            if (dir * dual > + tol_dj)
            {  d_stat = LPX_D_INFEAS;
               break;
            }
         }
      }
      /* store basic solution components */
      lpx_put_solution(lp, p_stat, d_stat, NULL, row_prim, row_dual,
         NULL, col_prim, col_dual);
      /* free working arrays */
      ufree(row_prim);
      ufree(row_dual);
      ufree(col_prim);
      ufree(col_dual);
done: /* return to the calling program */
      return ret;
}

/*----------------------------------------------------------------------
-- lpx_eval_tab_row - compute row of the simplex table.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_eval_tab_row(LPX *lp, int k, int ind[], gnm_float val[]);
--
-- *Description*
--
-- The routine lpx_eval_tab_row computes a row of the current simplex
-- table for the basic variable, which is specified by the number k:
-- if 1 <= k <= m, x[k] is k-th auxiliary variable; if m+1 <= k <= m+n,
-- x[k] is (k-m)-th structural variable, where m is number of rows, and
-- n is number of columns. The current basis must be available.
--
-- The routine stores column indices and numerical values of non-zero
-- elements of the computed row using sparse format to the locations
-- ind[1], ..., ind[len] and val[1], ..., val[len], respectively, where
-- 0 <= len <= n is number of non-zeros returned on exit.
--
-- Element indices stored in the array ind have the same sense as the
-- index k, i.e. indices 1 to m denote auxiliary variables and indices
-- m+1 to m+n denote structural ones (all these variables are obviously
-- non-basic by the definition).
--
-- The computed row shows how the specified basic variable x[k] = xB[i]
-- depends on non-basic variables:
--
--    xB[i] = alfa[i,1]*xN[1] + alfa[i,2]*xN[2] + ... + alfa[i,n]*xN[n],
--
-- where alfa[i,j] are elements of the simplex table row, xN[j] are
-- non-basic (auxiliary and structural) variables.
--
-- *Returns*
--
-- The routine returns number of non-zero elements in the simplex table
-- row stored in the arrays ind and val.
--
-- *Background*
--
-- The system of equality constraints of the LP problem is:
--
--    xR = A * xS,                                                   (1)
--
-- where xR is the vector of auxliary variables, xS is the vector of
-- structural variables, A is the matrix of constraint coefficients.
--
-- The system (1) can be written in homogenous form as follows:
--
--    A~ * x = 0,                                                    (2)
--
-- where A~ = (I | -A) is the augmented constraint matrix (has m rows
-- and m+n columns), x = (xR | xS) is the vector of all (auxiliary and
-- structural) variables.
--
-- By definition for a given basis we have:
--
--    A~ = (B | N),                                                  (3)
--
-- where B is the basis matrix. Thus, the system (2) can be written as:
--
--    B * xB + N * xN = 0.                                           (4)
--
-- From (4) it follows that:
--
--    xB = A^ * xN,                                                  (5)
--
-- where the matrix
--
--    A^ = - inv(B) * N                                              (6)
--
-- is called the simplex table.
--
-- It is understood that i-th row of the simplex table is:
--
--    e * A^ = - e * inv(B) * N,                                     (7)
--
-- where e is a unity vector with e[i] = 1.
--
-- To compute i-th row of the simplex table the routine first computes
-- i-th row of the inverse:
--
--    rho = inv(B') * e,                                             (8)
--
-- where B' is a matrix transposed to B, and then computes elements of
-- i-th row of the simplex table as scalar products:
--
--    alfa[i,j] = - rho * N[j]   for all j,                          (9)
--
-- where N[j] is a column of the augmented constraint matrix A~, which
-- corresponds to some non-basic auxiliary or structural variable. */

int lpx_eval_tab_row(LPX *lp, int k, int ind[], gnm_float val[])
{     int m, n, i, t, len, lll, *iii;
      gnm_float alfa, *rho, *vvv;
      if (!lpx_is_b_avail(lp))
         fault("lpx_eval_tab_row: LP basis is not available");
      m = lpx_get_num_rows(lp);
      n = lpx_get_num_cols(lp);
      if (!(1 <= k && k <= m+n))
         fault("lpx_eval_tab_row: k = %d; variable number out of range",
            k);
      /* determine xB[i] which corresponds to x[k] */
      if (k <= m)
         i = lpx_get_row_b_ind(lp, k);
      else
         i = lpx_get_col_b_ind(lp, k-m);
      if (i == 0)
         fault("lpx_eval_tab_row: k = %d; variable must be basic", k);
      insist(1 <= i && i <= m);
      /* allocate working arrays */
      rho = ucalloc(1+m, sizeof(gnm_float));
      iii = ucalloc(1+m, sizeof(int));
      vvv = ucalloc(1+m, sizeof(gnm_float));
      /* compute i-th row of the inverse; see (8) */
      for (t = 1; t <= m; t++) rho[t] = 0.0;
      rho[i] = 1.0;
      lpx_btran(lp, rho);
      /* compute i-th row of the simplex table */
      len = 0;
      for (k = 1; k <= m+n; k++)
      {  if (k <= m)
         {  /* x[k] is auxiliary variable, so N[k] is a unity column */
            if (lpx_get_row_stat(lp, k) == LPX_BS) continue;
            /* compute alfa[i,j]; see (9) */
            alfa = - rho[k];
         }
         else
         {  /* x[k] is structural variable, so N[k] is a column of the
               original constraint matrix A with negative sign */
            if (lpx_get_col_stat(lp, k-m) == LPX_BS) continue;
            /* compute alfa[i,j]; see (9) */
            lll = lpx_get_mat_col(lp, k-m, iii, vvv);
            alfa = 0.0;
            for (t = 1; t <= lll; t++) alfa += rho[iii[t]] * vvv[t];
         }
         /* store alfa[i,j] */
         if (alfa != 0.0) len++, ind[len] = k, val[len] = alfa;
      }
      insist(len <= n);
      /* free working arrays */
      ufree(rho);
      ufree(iii);
      ufree(vvv);
      /* return to the calling program */
      return len;
}

/*----------------------------------------------------------------------
-- lpx_eval_tab_col - compute column of the simplex table.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_eval_tab_col(LPX *lp, int k, int ind[], gnm_float val[]);
--
-- *Description*
--
-- The routine lpx_eval_tab_col computes a column of the current simplex
-- table for the non-basic variable, which is specified by the number k:
-- if 1 <= k <= m, x[k] is k-th auxiliary variable; if m+1 <= k <= m+n,
-- x[k] is (k-m)-th structural variable, where m is number of rows, and
-- n is number of columns. The current basis must be valid.
--
-- The routine stores row indices and numerical values of non-zero
-- elements of the computed column using sparse format to the locations
-- ind[1], ..., ind[len] and val[1], ..., val[len] respectively, where
-- 0 <= len <= m is number of non-zeros returned on exit.
--
-- Element indices stored in the array ind have the same sense as the
-- index k, i.e. indices 1 to m denote auxiliary variables and indices
-- m+1 to m+n denote structural ones (all these variables are obviously
-- basic by the definition).
--
-- The computed column shows how basic variables depend on the specified
-- non-basic variable x[k] = xN[j]:
--
--    xB[1] = ... + alfa[1,j]*xN[j] + ...
--    xB[2] = ... + alfa[2,j]*xN[j] + ...
--             . . . . . .
--    xB[m] = ... + alfa[m,j]*xN[j] + ...
--
-- where alfa[i,j] are elements of the simplex table column, xB[i] are
-- basic (auxiliary and structural) variables.
--
-- *Returns*
--
-- The routine returns number of non-zero elements in the simplex table
-- column stored in the arrays ind and val.
--
-- *Background*
--
-- As it was explained in comments to the routine lpx_eval_tab_row (see
-- above) the simplex table is the following matrix:
--
--    A^ = - inv(B) * N.                                             (1)
--
-- Therefore j-th column of the simplex table is:
--
--    A^ * e = - inv(B) * N * e = - inv(B) * N[j],                   (2)
--
-- where e is a unity vector with e[j] = 1, B is the basis matrix, N[j]
-- is a column of the augmented constraint matrix A~, which corresponds
-- to the given non-basic auxiliary or structural variable. */

int lpx_eval_tab_col(LPX *lp, int k, int ind[], gnm_float val[])
{     int m, n, t, len, stat;
      gnm_float *col;
      if (!lpx_is_b_avail(lp))
         fault("lpx_eval_tab_col: LP basis is not available");
      m = lpx_get_num_rows(lp);
      n = lpx_get_num_cols(lp);
      if (!(1 <= k && k <= m+n))
         fault("lpx_eval_tab_col: k = %d; variable number out of range",
            k);
      if (k <= m)
         stat = lpx_get_row_stat(lp, k);
      else
         stat = lpx_get_col_stat(lp, k-m);
      if (stat == LPX_BS)
         fault("lpx_eval_tab_col: k = %d; variable must be non-basic",
            k);
      /* obtain column N[k] with negative sign */
      col = ucalloc(1+m, sizeof(gnm_float));
      for (t = 1; t <= m; t++) col[t] = 0.0;
      if (k <= m)
      {  /* x[k] is auxiliary variable, so N[k] is a unity column */
         col[k] = -1.0;
      }
      else
      {  /* x[k] is structural variable, so N[k] is a column of the
            original constraint matrix A with negative sign */
         len = lpx_get_mat_col(lp, k-m, ind, val);
         for (t = 1; t <= len; t++) col[ind[t]] = val[t];
      }
      /* compute column of the simplex table, which corresponds to the
         specified non-basic variable x[k] */
      lpx_ftran(lp, col);
      len = 0;
      for (t = 1; t <= m; t++)
      {  if (col[t] != 0.0)
         {  len++;
            ind[len] = lpx_get_b_info(lp, t);
            val[len] = col[t];
         }
      }
      ufree(col);
      /* return to the calling program */
      return len;
}

/*----------------------------------------------------------------------
-- lpx_transform_row - transform explicitly specified row.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_transform_row(LPX *lp, int len, int ind[], gnm_float val[]);
--
-- *Description*
--
-- The routine lpx_transform_row performs the same operation as the
-- routine lpx_eval_tab_row with exception that the transformed row is
-- specified explicitly as a sparse vector.
--
-- The explicitly specified row may be thought as a linear form:
--
--    x = a[1]*x[m+1] + a[2]*x[m+2] + ... + a[n]*x[m+n],             (1)
--
-- where x is an auxiliary variable for this row, a[j] are coefficients
-- of the linear form, x[m+j] are structural variables.
--
-- On entry column indices and numerical values of non-zero elements of
-- the row should be stored in locations ind[1], ..., ind[len] and
-- val[1], ..., val[len], where len is the number of non-zero elements.
--
-- This routine uses the system of equality constraints and the current
-- basis in order to express the auxiliary variable x in (1) through the
-- current non-basic variables (as if the transformed row were added to
-- the problem object and its auxiliary variable were basic), i.e. the
-- resultant row has the form:
--
--    x = alfa[1]*xN[1] + alfa[2]*xN[2] + ... + alfa[n]*xN[n],       (2)
--
-- where xN[j] are non-basic (auxiliary or structural) variables, n is
-- the number of columns in the LP problem object.
--
-- On exit the routine stores indices and numerical values of non-zero
-- elements of the resultant row (2) in locations ind[1], ..., ind[len']
-- and val[1], ..., val[len'], where 0 <= len' <= n is the number of
-- non-zero elements in the resultant row returned by the routine. Note
-- that indices (numbers) of non-basic variables stored in the array ind
-- correspond to original ordinal numbers of variables: indices 1 to m
-- mean auxiliary variables and indices m+1 to m+n mean structural ones.
--
-- *Returns*
--
-- The routine returns len', which is the number of non-zero elements in
-- the resultant row stored in the arrays ind and val.
--
-- *Background*
--
-- The explicitly specified row (1) is transformed in the same way as
-- it were the objective function row.
--
-- From (1) it follows that:
--
--    x = aB * xB + aN * xN,                                         (3)
--
-- where xB is the vector of basic variables, xN is the vector of
-- non-basic variables.
--
-- The simplex table, which corresponds to the current basis, is:
--
--    xB = [-inv(B) * N] * xN.                                       (4)
--
-- Therefore substituting xB from (4) to (3) we have:
--
--    x = aB * [-inv(B) * N] * xN + aN * xN =
--                                                                   (5)
--      = rho * (-N) * xN + aN * xN = alfa * xN,
--
-- where:
--
--    rho = inv(B') * aB,                                            (6)
--
-- and
--
--    alfa = aN + rho * (-N)                                         (7)
--
-- is the resultant row computed by the routine. */

int lpx_transform_row(LPX *lp, int len, int ind[], gnm_float val[])
{     int i, j, k, m, n, t, lll, *iii;
      gnm_float alfa, *a, *aB, *rho, *vvv;
      if (!lpx_is_b_avail(lp))
         fault("lpx_transform_row: LP basis is not available");
      m = lpx_get_num_rows(lp);
      n = lpx_get_num_cols(lp);
      /* unpack the row to be transformed to the array a */
      a = ucalloc(1+n, sizeof(gnm_float));
      for (j = 1; j <= n; j++) a[j] = 0.0;
      if (!(0 <= len && len <= n))
         fault("lpx_transform_row: len = %d; invalid row length", len);
      for (t = 1; t <= len; t++)
      {  j = ind[t];
         if (!(1 <= j && j <= n))
            fault("lpx_transform_row: ind[%d] = %d; column index out of"
               " range", t, j);
         if (val[t] == 0.0)
            fault("lpx_transform_row: val[%d] = 0; zero coefficient not"
               " allowed", t);
         if (a[j] != 0.0)
            fault("lpx_transform_row: ind[%d] = %d; duplicate column in"
               "dices not allowed", t, j);
         a[j] = val[t];
      }
      /* construct the vector aB */
      aB = ucalloc(1+m, sizeof(gnm_float));
      for (i = 1; i <= m; i++)
      {  k = lpx_get_b_info(lp, i);
         /* xB[i] is k-th original variable */
         insist(1 <= k && k <= m+n);
         aB[i] = (k <= m ? 0.0 : a[k-m]);
      }
      /* solve the system B'*rho = aB to compute the vector rho */
      rho = aB, lpx_btran(lp, rho);
      /* compute coefficients at non-basic auxiliary variables */
      len = 0;
      for (i = 1; i <= m; i++)
      {  if (lpx_get_row_stat(lp, i) != LPX_BS)
         {  alfa = - rho[i];
            if (alfa != 0.0)
            {  len++;
               ind[len] = i;
               val[len] = alfa;
            }
         }
      }
      /* compute coefficients at non-basic structural variables */
      iii = ucalloc(1+m, sizeof(int));
      vvv = ucalloc(1+m, sizeof(gnm_float));
      for (j = 1; j <= n; j++)
      {  if (lpx_get_col_stat(lp, j) != LPX_BS)
         {  alfa = a[j];
            lll = lpx_get_mat_col(lp, j, iii, vvv);
            for (t = 1; t <= lll; t++) alfa += vvv[t] * rho[iii[t]];
            if (alfa != 0.0)
            {  len++;
               ind[len] = m+j;
               val[len] = alfa;
            }
         }
      }
      insist(len <= n);
      ufree(iii);
      ufree(vvv);
      ufree(aB);
      ufree(a);
      return len;
}

/*----------------------------------------------------------------------
-- lpx_transform_col - transform explicitly specified column.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_transform_col(LPX *lp, int len, int ind[], gnm_float val[]);
--
-- *Description*
--
-- The routine lpx_transform_col performs the same operation as the
-- routine lpx_eval_tab_col with exception that the transformed column
-- is specified explicitly as a sparse vector.
--
-- The explicitly specified column may be thought as if it were added
-- to the original system of equality constraints:
--
--    x[1] = a[1,1]*x[m+1] + ... + a[1,n]*x[m+n] + a[1]*x
--    x[2] = a[2,1]*x[m+1] + ... + a[2,n]*x[m+n] + a[2]*x            (1)
--       .  .  .  .  .  .  .  .  .  .  .  .  .  .  .
--    x[m] = a[m,1]*x[m+1] + ... + a[m,n]*x[m+n] + a[m]*x
--
-- where x[i] are auxiliary variables, x[m+j] are structural variables,
-- x is a structural variable for the explicitly specified column, a[i]
-- are constraint coefficients for x.
--
-- On entry row indices and numerical values of non-zero elements of
-- the column should be stored in locations ind[1], ..., ind[len] and
-- val[1], ..., val[len], where len is the number of non-zero elements.
--
-- This routine uses the system of equality constraints and the current
-- basis in order to express the current basic variables through the
-- structural variable x in (1) (as if the transformed column were added
-- to the problem object and the variable x were non-basic), i.e. the
-- resultant column has the form:
--
--    xB[1] = ... + alfa[1]*x
--    xB[2] = ... + alfa[2]*x                                        (2)
--       .  .  .  .  .  .
--    xB[m] = ... + alfa[m]*x
--
-- where xB are basic (auxiliary and structural) variables, m is the
-- number of rows in the problem object.
--
-- On exit the routine stores indices and numerical values of non-zero
-- elements of the resultant column (2) in locations ind[1], ...,
-- ind[len'] and val[1], ..., val[len'], where 0 <= len' <= m is the
-- number of non-zero element in the resultant column returned by the
-- routine. Note that indices (numbers) of basic variables stored in
-- the array ind correspond to original ordinal numbers of variables:
-- indices 1 to m mean auxiliary variables and indices m+1 to m+n mean
-- structural ones.
--
-- *Returns*
--
-- The routine returns len', which is the number of non-zero elements
-- in the resultant column stored in the arrays ind and val.
--
-- *Background*
--
-- The explicitly specified column (1) is transformed in the same way
-- as any other column of the constraint matrix using the formula:
--
--    alfa = inv(B) * a,                                             (3)
--
-- where alfa is the resultant column computed by the routine. */

int lpx_transform_col(LPX *lp, int len, int ind[], gnm_float val[])
{     int i, m, t;
      gnm_float *a, *alfa;
      if (!lpx_is_b_avail(lp))
         fault("lpx_transform_col: LP basis is not available");
      m = lpx_get_num_rows(lp);
      /* unpack the column to be transformed to the array a */
      a = ucalloc(1+m, sizeof(gnm_float));
      for (i = 1; i <= m; i++) a[i] = 0.0;
      if (!(0 <= len && len <= m))
         fault("lpx_transform_col: len = %d; invalid column length",
            len);
      for (t = 1; t <= len; t++)
      {  i = ind[t];
         if (!(1 <= i && i <= m))
            fault("lpx_transform_col: ind[%d] = %d; row index out of ra"
               "nge", t, i);
         if (val[t] == 0.0)
            fault("lpx_transform_col: val[%d] = 0; zero coefficient not"
               " allowed", t);
         if (a[i] != 0.0)
            fault("lpx_transform_col: ind[%d] = %d; duplicate row indic"
               "es not allowed", t, i);
         a[i] = val[t];
      }
      /* solve the system B*a = alfa to compute the vector alfa */
      alfa = a, lpx_ftran(lp, alfa);
      /* store resultant coefficients */
      len = 0;
      for (i = 1; i <= m; i++)
      {  if (alfa[i] != 0.0)
         {  len++;
            ind[len] = lpx_get_b_info(lp, i);
            val[len] = alfa[i];
         }
      }
      ufree(a);
      return len;
}

/*----------------------------------------------------------------------
-- lpx_prim_ratio_test - perform primal ratio test.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_prim_ratio_test(LPX *lp, int len, int ind[], gnm_float val[],
--    int how, gnm_float tol);
--
-- *Description*
--
-- The routine lpx_prim_ratio_test performs the primal ratio test for
-- an explicitly specified column of the simplex table.
--
-- The primal basic solution associated with an LP problem object,
-- which the parameter lp points to, should be feasible. No components
-- of the LP problem object are changed by the routine.
--
-- The explicitly specified column of the simplex table shows how the
-- basic variables xB depend on some non-basic variable y (which is not
-- necessarily presented in the problem object):
--
--    xB[1] = ... + alfa[1]*y + ...
--    xB[2] = ... + alfa[2]*y + ...                                  (*)
--       .  .  .  .  .  .  .  .
--    xB[m] = ... + alfa[m]*y + ...
--
-- The column (*) is specifed on entry to the routine using the sparse
-- format. Ordinal numbers of basic variables xB[i] should be placed in
-- locations ind[1], ..., ind[len], where ordinal number 1 to m denote
-- auxiliary variables, and ordinal numbers m+1 to m+n denote structural
-- variables. The corresponding non-zero coefficients alfa[i] should be
-- placed in locations val[1], ..., val[len]. The arrays ind and val are
-- not changed on exit.
--
-- The parameter how specifies in which direction the variable y changes
-- on entering the basis: +1 means increasing, -1 means decreasing.
--
-- The parameter tol is a relative tolerance (small positive number)
-- used by the routine to skip small alfa[i] of the column (*).
--
-- The routine determines the ordinal number of some basic variable
-- (specified in ind[1], ..., ind[len]), which should leave the basis
-- instead the variable y in order to keep primal feasibility, and
-- returns it on exit. If the choice cannot be made (i.e. if the
-- adjacent basic solution is primal unbounded), the routine returns
-- zero.
--
-- *Note*
--
-- If the non-basic variable y is presented in the LP problem object,
-- the column (*) can be computed using the routine lpx_eval_tab_col.
-- Otherwise it can be computed using the routine lpx_transform_col.
--
-- *Returns*
--
-- The routine lpx_prim_ratio_test returns the ordinal number of some
-- basic variable xB[i], which should leave the basis instead the
-- variable y in order to keep primal feasibility. If the adjacent basic
-- solution is primal unbounded and therefore the choice cannot be made,
-- the routine returns zero. */

int lpx_prim_ratio_test(LPX *lp, int len, int ind[], gnm_float val[],
      int how, gnm_float tol)
{     int i, k, m, n, p, t, typx, tagx;
      gnm_float alfa_i, abs_alfa_i, big, eps, bbar_i, lb_i, ub_i, temp,
         teta;
      if (!lpx_is_b_avail(lp))
         fault("lpx_prim_ratio_test: LP basis is not available");
      if (lpx_get_prim_stat(lp) != LPX_P_FEAS)
         fault("lpx_prim_ratio_test: current basic solution is not prim"
            "al feasible");
      if (!(how == +1 || how == -1))
         fault("lpx_prim_ratio_test: how = %d; invalid parameter", how);
      m = lpx_get_num_rows(lp);
      n = lpx_get_num_cols(lp);
      /* compute the largest absolute value of the specified influence
         coefficients */
      big = 0.0;
      for (t = 1; t <= len; t++)
      {  temp = val[t];
         if (temp < 0.0) temp = - temp;
         if (big < temp) big = temp;
      }
      /* compute the absolute tolerance eps used to skip small entries
         of the column */
      if (!(0.0 < tol && tol < 1.0))
         fault("lpx_prim_ratio_test: tol = %g; invalid tolerance", tol);
      eps = tol * (1.0 + big);
      /* initial settings */
      p = 0, teta = DBL_MAX, big = 0.0;
      /* walk through the entries of the specified column */
      for (t = 1; t <= len; t++)
      {  /* get the ordinal number of basic variable */
         k = ind[t];
         if (!(1 <= k && k <= m+n))
            fault("lpx_prim_ratio_test: ind[%d] = %d; variable number o"
               "ut of range", t, k);
         if (k <= m)
            tagx = lpx_get_row_stat(lp, k);
         else
            tagx = lpx_get_col_stat(lp, k-m);
         if (tagx != LPX_BS)
            fault("lpx_prim_ratio_test: ind[%d] = %d; non-basic variabl"
               "e not allowed", t, k);
         /* determine index of the variable x[k] in the vector xB */
         if (k <= m)
            i = lpx_get_row_b_ind(lp, k);
         else
            i = lpx_get_col_b_ind(lp, k-m);
         insist(1 <= i && i <= m);
         /* determine unscaled bounds and value of the basic variable
            xB[i] in the current basic solution */
         if (k <= m)
         {  typx = lpx_get_row_type(lp, k);
            lb_i = lpx_get_row_lb(lp, k);
            ub_i = lpx_get_row_ub(lp, k);
            bbar_i = lpx_get_row_prim(lp, k);
         }
         else
         {  typx = lpx_get_col_type(lp, k-m);
            lb_i = lpx_get_col_lb(lp, k-m);
            ub_i = lpx_get_col_ub(lp, k-m);
            bbar_i = lpx_get_col_prim(lp, k-m);
         }
         /* determine influence coefficient for the basic variable
            x[k] = xB[i] in the explicitly specified column and turn to
            the case of increasing the variable y in order to simplify
            the program logic */
         alfa_i = (how > 0 ? +val[t] : -val[t]);
         abs_alfa_i = (alfa_i > 0.0 ? +alfa_i : -alfa_i);
         /* analyze main cases */
         switch (typx)
         {  case LPX_FR:
               /* xB[i] is free variable */
               continue;
            case LPX_LO:
lo:            /* xB[i] has an lower bound */
               if (alfa_i > - eps) continue;
               temp = (lb_i - bbar_i) / alfa_i;
               break;
            case LPX_UP:
up:            /* xB[i] has an upper bound */
               if (alfa_i < + eps) continue;
               temp = (ub_i - bbar_i) / alfa_i;
               break;
            case LPX_DB:
               /* xB[i] has both lower and upper bounds */
               if (alfa_i < 0.0) goto lo; else goto up;
            case LPX_FX:
               /* xB[i] is fixed variable */
               if (abs_alfa_i < eps) continue;
               temp = 0.0;
               break;
            default:
               insist(typx != typx);
         }
         /* if the value of the variable xB[i] violates its lower or
            upper bound (slightly, because the current basis is assumed
            to be primal feasible), temp is negative; we can think this
            happens due to round-off errors and the value is exactly on
            the bound; this allows replacing temp by zero */
         if (temp < 0.0) temp = 0.0;
         /* apply the minimal ratio test */
         if (teta > temp || teta == temp && big < abs_alfa_i)
            p = k, teta = temp, big = abs_alfa_i;
      }
      /* return the ordinal number of the chosen basic variable */
      return p;
}

/*----------------------------------------------------------------------
-- lpx_dual_ratio_test - perform dual ratio test.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_dual_ratio_test(LPX *lp, int len, int ind[], gnm_float val[],
--    int how, gnm_float tol);
--
-- *Description*
--
-- The routine lpx_dual_ratio_test performs the dual ratio test for an
-- explicitly specified row of the simplex table.
--
-- The dual basic solution associated with an LP problem object, which
-- the parameter lp points to, should be feasible. No components of the
-- LP problem object are changed by the routine.
--
-- The explicitly specified row of the simplex table is a linear form,
-- which shows how some basic variable y (not necessarily presented in
-- the problem object) depends on non-basic variables xN:
--
--    y = alfa[1]*xN[1] + alfa[2]*xN[2] + ... + alfa[n]*xN[n].       (*)
--
-- The linear form (*) is specified on entry to the routine using the
-- sparse format. Ordinal numbers of non-basic variables xN[j] should be
-- placed in locations ind[1], ..., ind[len], where ordinal numbers 1 to
-- m denote auxiliary variables, and ordinal numbers m+1 to m+n denote
-- structural variables. The corresponding non-zero coefficients alfa[j]
-- should be placed in locations val[1], ..., val[len]. The arrays ind
-- and val are not changed on exit.
--
-- The parameter how specifies in which direction the variable y changes
-- on leaving the basis: +1 means increasing, -1 means decreasing.
--
-- The parameter tol is a relative tolerance (small positive number)
-- used by the routine to skip small alfa[j] of the form (*).
--
-- The routine determines the ordinal number of some non-basic variable
-- (specified in ind[1], ..., ind[len]), which should enter the basis
-- instead the variable y in order to keep dual feasibility, and returns
-- it on exit. If the choice cannot be made (i.e. if the adjacent basic
-- solution is dual unbounded), the routine returns zero.
--
-- *Note*
--
-- If the basic variable y is presented in the LP problem object, the
-- row (*) can be computed using the routine lpx_eval_tab_row. Otherwise
-- it can be computed using the routine lpx_transform_row.
--
-- *Returns*
--
-- The routine lpx_dual_ratio_test returns the ordinal number of some
-- non-basic variable xN[j], which should enter the basis instead the
-- variable y in order to keep dual feasibility. If the adjacent basic
-- solution is dual unbounded and therefore the choice cannot be made,
-- the routine returns zero. */

int lpx_dual_ratio_test(LPX *lp, int len, int ind[], gnm_float val[],
      int how, gnm_float tol)
{     int k, m, n, t, q, tagx;
      gnm_float dir, alfa_j, abs_alfa_j, big, eps, cbar_j, temp, teta;
      if (!lpx_is_b_avail(lp))
         fault("lpx_dual_ratio_test: LP basis is not available");
      if (lpx_get_dual_stat(lp) != LPX_D_FEAS)
         fault("lpx_dual_ratio_test: current basic solution is not dual"
            " feasible");
      if (!(how == +1 || how == -1))
         fault("lpx_dual_ratio_test: how = %d; invalid parameter", how);
      m = lpx_get_num_rows(lp);
      n = lpx_get_num_cols(lp);
      dir = (lpx_get_obj_dir(lp) == LPX_MIN ? +1.0 : -1.0);
      /* compute the largest absolute value of the specified influence
         coefficients */
      big = 0.0;
      for (t = 1; t <= len; t++)
      {  temp = val[t];
         if (temp < 0.0) temp = - temp;
         if (big < temp) big = temp;
      }
      /* compute the absolute tolerance eps used to skip small entries
         of the row */
      if (!(0.0 < tol && tol < 1.0))
         fault("lpx_dual_ratio_test: tol = %g; invalid tolerance", tol);
      eps = tol * (1.0 + big);
      /* initial settings */
      q = 0, teta = DBL_MAX, big = 0.0;
      /* walk through the entries of the specified row */
      for (t = 1; t <= len; t++)
      {  /* get ordinal number of non-basic variable */
         k = ind[t];
         if (!(1 <= k && k <= m+n))
            fault("lpx_dual_ratio_test: ind[%d] = %d; variable number o"
               "ut of range", t, k);
         if (k <= m)
            tagx = lpx_get_row_stat(lp, k);
         else
            tagx = lpx_get_col_stat(lp, k-m);
         if (tagx == LPX_BS)
            fault("lpx_dual_ratio_test: ind[%d] = %d; basic variable no"
               "t allowed", t, k);
         /* determine unscaled reduced cost of the non-basic variable
            x[k] = xN[j] in the current basic solution */
         if (k <= m)
            cbar_j = lpx_get_row_dual(lp, k);
         else
            cbar_j = lpx_get_col_dual(lp, k-m);
         /* determine influence coefficient at the non-basic variable
            x[k] = xN[j] in the explicitly specified row and turn to
            the case of increasing the variable y in order to simplify
            program logic */
         alfa_j = (how > 0 ? +val[t] : -val[t]);
         abs_alfa_j = (alfa_j > 0.0 ? +alfa_j : -alfa_j);
         /* analyze main cases */
         switch (tagx)
         {  case LPX_NL:
               /* xN[j] is on its lower bound */
               if (alfa_j < +eps) continue;
               temp = (dir * cbar_j) / alfa_j;
               break;
            case LPX_NU:
               /* xN[j] is on its upper bound */
               if (alfa_j > -eps) continue;
               temp = (dir * cbar_j) / alfa_j;
               break;
            case LPX_NF:
               /* xN[j] is non-basic free variable */
               if (abs_alfa_j < eps) continue;
               temp = 0.0;
               break;
            case LPX_NS:
               /* xN[j] is non-basic fixed variable */
               continue;
            default:
               insist(tagx != tagx);
         }
         /* if the reduced cost of the variable xN[j] violates its zero
            bound (slightly, because the current basis is assumed to be
            dual feasible), temp is negative; we can think this happens
            due to round-off errors and the reduced cost is exact zero;
            this allows replacing temp by zero */
         if (temp < 0.0) temp = 0.0;
         /* apply the minimal ratio test */
         if (teta > temp || teta == temp && big < abs_alfa_j)
            q = k, teta = temp, big = abs_alfa_j;
      }
      /* return the ordinal number of the chosen non-basic variable */
      return q;
}

/* eof */
