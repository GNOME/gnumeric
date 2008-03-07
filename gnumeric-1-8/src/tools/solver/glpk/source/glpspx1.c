/* glpspx1.c (simplex method generic routines) */

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
#include "glpspx.h"

/*----------------------------------------------------------------------
-- spx_invert - reinvert the basis matrix.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- int spx_invert(SPX *spx);
--
-- *Description*
--
-- The routine spx_invert computes a factorization of the current basis
-- matrix B specified by the arrays tagx, posx, and indx.
--
-- *Returns*
--
-- The routine returns one of the following codes:
--
-- 0 - the basis matrix has been successfully factorized;
-- 1 - the basis matrix is singular;
-- 2 - the basis matrix is ill-conditioned.
--
-- In the case of non-zero return code the calling program can "repair"
-- the basis matrix replacing linearly dependent columns by appropriate
-- unity columns of auxiliary variable using information stored by the
-- factorizing routine in the object spx->inv (for details see comments
-- in the module GLPINV). */

static int inv_col(void *info, int j, int rn[], gnm_float bj[])
{     /* this auxiliary routine returns row indices and numerical
         values of non-zero elements of the j-th column of the current
         basis matrix B, which has to be reinverted */
      SPX *spx = info;
      int m = spx->m;
      int n = spx->n;
      int *AT_ptr = spx->AT_ptr;
      int *AT_ind = spx->AT_ind;
      gnm_float *AT_val = spx->AT_val;
      int *indx = spx->indx;
      int k, ptr, len, t;
      insist(1 <= j && j <= m);
      k = indx[j]; /* x[k] = xB[j] */
      insist(1 <= k && k <= m+n);
      if (k <= m)
      {  /* x[k] is auxiliary variable */
         len = 1;
         rn[1] = k;
         bj[1] = +1.0;
      }
      else
      {  /* x[k] is structural variable */
         ptr = AT_ptr[k-m];
         len = AT_ptr[k-m+1] - ptr;
         memcpy(&rn[1], &AT_ind[ptr], len * sizeof(int));
         memcpy(&bj[1], &AT_val[ptr], len * sizeof(gnm_float));
         for (t = len; t >= 1; t--) bj[t] = - bj[t];
      }
      return len;
}

int spx_invert(SPX *spx)
{     static gnm_float piv_tol[1+3] = { 0.00, 0.10, 0.30, 0.70 };
      int try, ret;
      /* if the invertable form has a wrong size, delete it */
      if (spx->inv != NULL && spx->inv->m != spx->m)
         inv_delete(spx->inv), spx->inv = NULL;
      /* if the invertable form does not exist, create it */
#if 0
      if (spx->inv == NULL) spx->inv = inv_create(spx->m, 100);
#else
      if (spx->inv == NULL) spx->inv = inv_create(spx->m, 50);
#endif
      /* try to factorize the basis matrix */
      for (try = 1; try <= 3; try++)
      {  if (try > 1 && spx->msg_lev >= 3)
            print("spx_invert: trying to factorize the basis using thre"
               "shold tolerance %g", piv_tol[try]);
         spx->inv->luf->piv_tol = piv_tol[try];
         ret = inv_decomp(spx->inv, spx, inv_col);
         if (ret == 0) break;
      }
      /* analyze the return code */
      switch (ret)
      {  case 0:
            /* success */
            spx->b_stat = LPX_B_VALID;
            break;
         case 1:
            if (spx->msg_lev >= 1)
               print("spx_invert: the basis matrix is singular");
            spx->b_stat = LPX_B_UNDEF;
            break;
         case 2:
            if (spx->msg_lev >= 1)
               print("spx_invert: the basis matrix is ill-conditioned");
            spx->b_stat = LPX_B_UNDEF;
            break;
         default:
            insist(ret != ret);
      }
      /* return to the calling program */
      return ret;
}

/*----------------------------------------------------------------------
-- spx_ftran - perform forward transformation (FTRAN).
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- void spx_ftran(SPX *spx, gnm_float x[], int save);
--
-- *Description*
--
-- The routine spx_ftran performs forward transformation (FTRAN) of the
-- given vector using the factorization of the basis matrix.
--
-- In order to perform FTRAN the routine solves the system B*x' = x,
-- where B is the basis matrix, x' is vector of unknowns (transformed
-- vector that should be computed), x is vector of right-hand sides
-- (input vector that should be transformed).
--
-- On entry the array x should contain components of the vector x in
-- locations x[1], x[2], ..., x[m], where m is the order of the basis
-- matrix. On exit this array will contain components of the vector x'
-- in the same locations.
--
-- The parameter save is a flag. If this flag is set, it means that the
-- input vector x is a column of the non-basic variable, which has been
-- chosen to enter the basis. In this case the routine spx_ftran saves
-- this column (after partial transformation) in order that the routine
-- spx_update could update (recompute) the factorization for an adjacent
-- basis using this partially transformed column. The simplex method
-- routine should call the routine spx_ftran with the save flag set at
-- least once before a subsequent call to the routine spx_update. */

void spx_ftran(SPX *spx, gnm_float x[], int save)
{     insist(spx->b_stat == LPX_B_VALID);
      inv_ftran(spx->inv, x, save);
      return;
}

/*----------------------------------------------------------------------
-- spx_btran - perform backward transformation (BTRAN).
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- void spx_btran(SPX *spx, gnm_float x[]);
--
-- *Description*
--
-- The routine spx_btran performs backward transformation (BTRAN) of the
-- given vector using the factorization of the basis matrix.
--
-- In order to perform BTRAN the routine solves the system B'*x' = x,
-- where B' is a matrix transposed to the basis matrix B, x' is vector
-- of unknowns (transformed vector that should be computed), x is vector
-- of right-hand sides (input vector that should be transformed).
--
-- On entry the array x should contain components of the vector x in
-- locations x[1], x[2], ..., x[m], where m is the order of the basis
-- matrix. On exit this array will contain components of the vector x'
-- in the same locations. */

void spx_btran(SPX *spx, gnm_float x[])
{     insist(spx->b_stat == LPX_B_VALID);
      inv_btran(spx->inv, x);
      return;
}

/*----------------------------------------------------------------------
-- spx_update - update factorization for adjacent basis matrix.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- int spx_update(SPX *spx, int j);
--
-- *Description*
--
-- The routine spx_update recomputes the factorization, which on entry
-- corresponds to the current basis matrix B, in order that the updated
-- factorization would correspond to the adjacent basis matrix B' that
-- differs from B in the j-th column.
--
-- The new j-th column of the basis matrix is passed implicitly to the
-- routine spx_update. It is assumed that this column was saved before
-- by the routine spx_ftran (see above).
--
-- *Returns*
--
-- Zero return code means that the factorization has been successfully
-- updated. Non-zero return code means that the factorization should be
-- reinverted by the routine spx_invert. */

int spx_update(SPX *spx, int j)
{     int ret;
      insist(1 <= j && j <= spx->m);
      ret = inv_update(spx->inv, j);
      return ret;
}

/*----------------------------------------------------------------------
-- spx_eval_xn_j - determine value of non-basic variable.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- gnm_float spx_eval_xn_j(SPX *spx, int j);
--
-- *Returns*
--
-- The routine spx_eval_xn_j returns a value of the non-basic variable
-- xN[j], 1 <= j <= n, for the current basic solution. */

gnm_float spx_eval_xn_j(SPX *spx, int j)
{     int m = spx->m;
      int n = spx->n;
      gnm_float *lb = spx->lb;
      gnm_float *ub = spx->ub;
      int *tagx = spx->tagx;
      int *indx = spx->indx;
      int k;
      gnm_float xn_j;
      insist(1 <= j && j <= n);
      k = indx[m+j]; /* x[k] = xN[j] */
      switch (tagx[k])
      {  case LPX_NL:
            /* xN[j] is on its lower bound */
            xn_j = lb[k]; break;
         case LPX_NU:
            /* xN[j] is on its upper bound */
            xn_j = ub[k]; break;
         case LPX_NF:
            /* xN[j] is free variable */
            xn_j = 0.0; break;
         case LPX_NS:
            /* xN[j] is fixed variable */
            xn_j = lb[k]; break;
         default:
            insist(tagx != tagx);
      }
      return xn_j;
}

/*----------------------------------------------------------------------
-- spx_eval_bbar - compute values of basic variables.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- void spx_eval_bbar(SPX *spx);
--
-- *Description*
--
-- The routine spx_eval_bbar computes values of basic variables
--
--    xB = beta = (beta_1, ..., beta_m)
--
-- for the current basis and stores elements of the vector beta to the
-- locations bbar[1], ..., bbar[m].
--
-- The vector beta is computed using the following formula:
--
--    beta = - inv(B) * (N * xN) =
--
--         = inv(B) * (- N[1]*xN[1] - ... - N[n]*xN[n]),
--
-- where N[j] is the j-th column of the augmented constraint matrix A~,
-- which corresponds to the non-basic variable xN[j]. */

void spx_eval_bbar(SPX *spx)
{     int m = spx->m;
      int n = spx->n;
      int *AT_ptr = spx->AT_ptr;
      int *AT_ind = spx->AT_ind;
      gnm_float *AT_val = spx->AT_val;
      int *indx = spx->indx;
      gnm_float *bbar = spx->bbar;
      int i, j, j_beg, j_end, j_ptr, k;
      gnm_float *rhs = bbar, xn_j;
      /* rhs := - N*xN = - N[1]*xN[1] - ... - N[n]*xN[n] */
      for (i = 1; i <= m; i++) rhs[i] = 0.0;
      for (j = 1; j <= n; j++)
      {  xn_j = spx_eval_xn_j(spx, j);
         if (xn_j == 0.0) continue;
         k = indx[m+j]; /* x[k] = xN[j] */
         if (k <= m)
         {  /* x[k] is auxiliary variable */
            rhs[k] -= xn_j;
         }
         else
         {  /* x[k] is structural variable */
            j_beg = AT_ptr[k-m], j_end = AT_ptr[k-m+1];
            for (j_ptr = j_beg; j_ptr < j_end; j_ptr++)
               rhs[AT_ind[j_ptr]] += AT_val[j_ptr] * xn_j;
         }
      }
      /* bbar := inv(B) * rhs */
      spx_ftran(spx, rhs, 0);
      return;
}

/*----------------------------------------------------------------------
-- spx_eval_pi - compute simplex multipliers.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- void spx_eval_pi(SPX *spx);
--
-- *Description*
--
-- The routine spx_eval_pi computes simplex multipliers (i.e. Lagrange
-- multipliers that correspond to the equality constraints)
--
--    pi = (pi_1, ..., pi_m)
--
-- for the current basis and stores elements of the vector pi to the
-- locations pi[1], ..., pi[m].
--
-- The vector pi is computed using the following formula:
--
--    pi = inv(B') * cB,
--
-- where B' is a matrix transposed to the current basis matrix B, cB is
-- the vector of objective coefficients at basic variables xB. */

void spx_eval_pi(SPX *spx)
{     int m = spx->m;
      gnm_float *coef = spx->coef;
      int *indx = spx->indx;
      int i;
      gnm_float *cb = spx->pi;
      /* make the vector cB */
      for (i = 1; i <= m; i++) cb[i] = coef[indx[i]];
      /* pi := inv(B') * cB */
      spx_btran(spx, cb);
      return;
}

/*----------------------------------------------------------------------
-- spx_eval_cbar - compute reduced costs of non-basic variables.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- void spx_eval_cbar(SPX *spx);
--
-- *Description*
--
-- The routine spx_eval_cbar computes reduced costs
--
--    d = (d_1, ..., d_n)
--
-- of non-basic variables for the current basis and stores elements of
-- the vector d to the locations cbar[1], ..., cbar[n].
--
-- The vector d is computed using the following formula:
--
--    d[j] = cN[j] - pi' * N[j], j = 1, ..., n,
--
-- where cN[j] is coefficient of the objective function at the variable
-- xN[j], pi is the vector of simplex multipliers (should be computed
-- before), N[j] is the j-th column of the augmented constraint matrix
-- A~, which corresponds to the non-basic variable xN[j]. */

void spx_eval_cbar(SPX *spx)
{     int m = spx->m;
      int n = spx->n;
      gnm_float *coef = spx->coef;
      int *AT_ptr = spx->AT_ptr;
      int *AT_ind = spx->AT_ind;
      gnm_float *AT_val = spx->AT_val;
      int *indx = spx->indx;
      gnm_float *pi = spx->pi;
      gnm_float *cbar = spx->cbar;
      int j, j_beg, j_end, j_ptr, k;
      gnm_float cbar_j;
      /* d[j] := cN[j] - pi' * N[j] */
      for (j = 1; j <= n; j++)
      {  k = indx[m+j]; /* x[k] = xN[j] */
         cbar_j = coef[k];
         if (k <= m)
         {  /* x[k] is auxiliary variable */
            cbar_j -= pi[k];
         }
         else
         {  /* x[k] is structural variable */
            j_beg = AT_ptr[k-m], j_end = AT_ptr[k-m+1];
            for (j_ptr = j_beg; j_ptr < j_end; j_ptr++)
               cbar_j += pi[AT_ind[j_ptr]] * AT_val[j_ptr];
         }
         cbar[j] = cbar_j;
      }
      return;
}

/*----------------------------------------------------------------------
-- spx_eval_obj - compute value of the objective function.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- gnm_float spx_eval_obj(SPX *spx);
--
-- *Returns*
--
-- The routine spx_eval_obj returns the current value of the objective
-- function (used mainly for displaying). */

gnm_float spx_eval_obj(SPX *spx)
{     int m = spx->m;
      int n = spx->n;
      int *tagx = spx->tagx;
      int *posx = spx->posx;
      gnm_float *coef = spx->coef;
      gnm_float *bbar = spx->bbar;
      int i, j, k;
      gnm_float obj = coef[0];
      for (k = 1; k <= m+n; k++)
      {  if (tagx[k] == LPX_BS)
         {  /* x[k] = xB[i] */
            i = posx[k];
            insist(1 <= i && i <= m);
            obj += coef[k] * bbar[i];
         }
         else
         {  /* x[k] = xN[j] */
            j = posx[k] - m;
            obj += coef[k] * spx_eval_xn_j(spx, j);
         }
      }
      return obj;
}

/*----------------------------------------------------------------------
-- spx_eval_col - compute column of the simplex table.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- void spx_eval_col(SPX *spx, int j, gnm_float col[], int save);
--
-- *Description*
--
-- The routine spx_eval_col computes the j-th column of the current
-- simplex table A^ = -inv(B)*N and stores elements of this column to
-- the locations col[1], ..., col[m].
--
-- The parameter save is a flag. If this flag is set, it means that the
-- computed column is the column of non-basic variable xN[q], which has
-- been chosen to enter the basis (i.e. j = q). This flag is used by the
-- routine spx_ftran in order to save and pass the corresponding column
-- to the routine spx_update.
--
-- The j-th column is computed using the following formula:
--
--    A^[j] = - inv(B) * N[j],
--
-- where N[j] is the j-th column of the augmented constraint matrix A~,
-- which corresponds to the non-basic variable xN[j]. */

void spx_eval_col(SPX *spx, int j, gnm_float col[], int save)
{     int m = spx->m;
      int n = spx->n;
      int *AT_ptr = spx->AT_ptr;
      int *AT_ind = spx->AT_ind;
      gnm_float *AT_val = spx->AT_val;
      int *indx = spx->indx;
      int i, j_beg, j_end, j_ptr, k;
      gnm_float *rhs = col;
      insist(1 <= j && j <= n);
      /* rhs := N[j] */
      for (i = 1; i <= m; i++) rhs[i] = 0.0;
      k = indx[m+j]; /* x[k] = xN[j] */
      if (k <= m)
      {  /* x[k] is auxiliary variable */
         rhs[k] = +1.0;
      }
      else
      {  /* x[k] is structural variable */
         j_beg = AT_ptr[k-m], j_end = AT_ptr[k-m+1];
         for (j_ptr = j_beg; j_ptr < j_end; j_ptr++)
            rhs[AT_ind[j_ptr]] = - AT_val[j_ptr];
      }
      /* A^[j] := - inv(B) * N[j] */
      spx_ftran(spx, rhs, save);
      for (i = 1; i <= m; i++) col[i] = - col[i];
      return;
}

/*----------------------------------------------------------------------
-- spx_eval_rho - compute row of the inverse.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- void spx_eval_rho(SPX *spx, int i, gnm_float rho[]);
--
-- *Description*
--
-- The routine spx_eval_rho computes the i-th row of the matrix inv(B),
-- where B is the current basis matrix, and stores elements of this row
-- to the locations rho[1], ..., rho[m].
--
-- The i-th row of the inverse is computed using the following formula:
--
--    zeta = inv(B') * e[i],
--
-- where B' is a matrix transposed to B, e[i] is a unity vector, which
-- contains one in the i-th position. */

void spx_eval_rho(SPX *spx, int i, gnm_float rho[])
{     int m = spx->m;
      int j;
      gnm_float *rhs = rho;
      insist(1 <= i && i <= m);
      for (j = 1; j <= m; j++) rhs[j] = 0.0;
      rhs[i] = +1.0;
      spx_btran(spx, rhs);
      return;
}

/*----------------------------------------------------------------------
-- spx_eval_row - compute row of the simplex table.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- void spx_eval_row(SPX *spx, gnm_float rho[], gnm_float row[]);
--
-- *Description*
--
-- The routine spx_eval_col computes the i-th row of the current simplex
-- table A^ = -inv(B)*N and stores elements of this row to the locations
-- row[1], ..., row[n].
--
-- The row number i is defined implicitly by the array rho, which on
-- entry should contain the i-th row of the inverse inv(B) computed by
-- means of the routine spx_eval_rho. This array is not changed on exit.
--
-- In order to exploit possible sparsity of the vector rho, the routine
-- computes the i-th row as a linear combination of rows of the matrix N
-- using the following formula:
--
--    a^[i] = - N' * rho = - N'[1]*rho[1] - ... - N'[m]*rho[m],
--
-- N' is a matrix transposed to N, N is the matrix of non-basic columns
-- of the augmented constraint matrix A~, N'[i] is the i-th row of the
-- matrix N. */

void spx_eval_row(SPX *spx, gnm_float rho[], gnm_float row[])
{     int m = spx->m;
      int n = spx->n;
      int *A_ptr = spx->A_ptr;
      int *A_ind = spx->A_ind;
      gnm_float *A_val = spx->A_val;
      int *posx = spx->posx;
      int i, i_beg, i_end, i_ptr, j;
      gnm_float rho_i;
      for (j = 1; j <= n; j++) row[j] = 0.0;
      for (i = 1; i <= m; i++)
      {  rho_i = rho[i];
         if (rho_i == 0.0) continue;
         /* i-th row of the augmented constraint matrix A~ has the form
            (0 ... 0 +1 0 ... 0 | -a[i,1] ... -a[i,m]), where the unity
            subvector is not stored explicitly; we need to consider only
            elements, which are placed in non-basic columns */
         /* look through the unity subvector of the i-th row of A~ */
         j = posx[i] - m;
         if (j > 0)
         {  /* xN[j] is the auxiliary variable x[i] */
            row[j] -= rho_i;
         }
         /* look through other non-zero elements in the i-th row of A~
            that correspond to structural variables */
         i_beg = A_ptr[i], i_end = A_ptr[i+1];
         for (i_ptr = i_beg; i_ptr < i_end; i_ptr++)
         {  j = posx[A_ind[i_ptr] + m] - m;
            if (j > 0)
            {  /* xN[j] is some structural variable */
               row[j] += rho_i * A_val[i_ptr];
            }
         }
      }
      return;
}

/*----------------------------------------------------------------------
-- spx_check_bbar - check primal feasibility.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- gnm_float spx_check_bbar(SPX *spx, gnm_float tol);
--
-- *Description*
--
-- The routine spx_check_bbar checks if the current basic solution is
-- primal feasible, i.e. whether the current values of basic variables
-- are within their bounds, using a relative tolerance tol.
--
-- *Returns*
--
-- The routine returns the non-positive sum of primal infeasibilities.
-- If the basic solution is primal feasible, this sum is exact zero. */

gnm_float spx_check_bbar(SPX *spx, gnm_float tol)
{     int m = spx->m;
      int *typx = spx->typx;
      gnm_float *lb = spx->lb;
      gnm_float *ub = spx->ub;
      int *indx = spx->indx;
      gnm_float *bbar = spx->bbar;
      int i, k, typx_k;
      gnm_float lb_k, ub_k, bbar_i;
      gnm_float sum = 0.0;
      for (i = 1; i <= m; i++)
      {  k = indx[i]; /* x[k] = xB[i] */
         typx_k = typx[k];
         bbar_i = bbar[i];
         if (typx_k == LPX_LO || typx_k == LPX_DB || typx_k == LPX_FX)
         {  /* xB[i] has lower bound */
            lb_k = lb[k];
            if ((lb_k - bbar_i) / (1.0 + gnm_abs(lb_k)) > tol)
               sum += (lb_k - bbar_i);
         }
         if (typx_k == LPX_UP || typx_k == LPX_DB || typx_k == LPX_FX)
         {  /* xB[i] has upper bound */
            ub_k = ub[k];
            if ((bbar_i - ub_k) / (1.0 + gnm_abs(ub_k)) > tol)
               sum += (bbar_i - ub_k);
         }
      }
      return sum;
}

/*----------------------------------------------------------------------
-- spx_check_cbar - check dual feasibility.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- gnm_float spx_check_cbar(SPX *spx, gnm_float tol);
--
-- *Description*
--
-- The routine spx_check_cbar checks if the current basic solution is
-- dual feasible, i.e. whether the current reduced costs of non-basic
-- variables have correct signs, using an absolute tolerance tol.
--
-- *Returns*
--
-- The routine returns the non-positive sum of dual infeasibilities. If
-- the basic solution is dual feasible, this sum is exact zero. */

gnm_float spx_check_cbar(SPX *spx, gnm_float tol)
{     int m = spx->m;
      int n = spx->n;
      gnm_float dir = (spx->dir == LPX_MIN ? +1.0 : -1.0);
      int *tagx = spx->tagx;
      int *indx = spx->indx;
      gnm_float *cbar = spx->cbar;
      int j, k, tagx_k;
      gnm_float cbar_j;
      gnm_float sum = 0.0;
      for (j = 1; j <= n; j++)
      {  k = indx[m+j]; /* x[k] = xN[j] */
         tagx_k = tagx[k];
         cbar_j = dir * cbar[j];
         if (tagx_k == LPX_NF || tagx_k == LPX_NL)
         {  /* xN[j] can increase */
            if (cbar_j < - tol) sum -= cbar_j;
         }
         if (tagx_k == LPX_NF || tagx_k == LPX_NU)
         {  /* xN[j] can decrease */
            if (cbar_j > + tol) sum += cbar_j;
         }
      }
      return sum;
}

/*----------------------------------------------------------------------
-- spx_prim_chuzc - choose non-basic variable (primal simplex).
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- void spx_prim_chuzc(SPX *spx, gnm_float tol);
--
-- *Description*
--
-- The routine spx_prim_chuzc chooses a non-basic variable xN[q], which
-- should enter the basis.
--
-- The parameter tol is an absolute tolerance, which is used to check
-- if some non-basic variable violates the dual feasibility condition
-- and therefore can be considered as a possible candidate.
--
-- The routine chooses a non-basic variable xN[q], which can change in
-- a feasible direction improving the objective fucntion and which has
-- the largest weighted reduced cost:
--
--    |d[q]| / gnm_sqrt(gamma[q]) = max(|d[j]| / gnm_sqrt(gamma[j])),
--
-- where d[j] is a reduced cost of the non-basic variable xN[j], and
-- gamma[j] is a "weight" of this variable.
--
-- If the non-basic variable xN[q] has been chosen, the routine sets q
-- to the number of this variable, 1 <= q <= n. However, if the current
-- basis is dual feasible and therefore no choice has been made, the
-- routine sets q to 0.
--
-- *Returns*
--
-- Non-zero return code means that the routine recomputed all the basic
-- solution components in order to improve their numeric accuracy. */

int spx_prim_chuzc(SPX *spx, gnm_float tol)
{     int m = spx->m;
      int n = spx->n;
      gnm_float dir = (spx->dir == LPX_MIN ? +1.0 : -1.0);
      gnm_float *coef = spx->coef;
      int *AT_ptr = spx->AT_ptr;
      int *AT_ind = spx->AT_ind;
      gnm_float *AT_val = spx->AT_val;
      int *tagx = spx->tagx;
      int *indx = spx->indx;
      gnm_float *pi = spx->pi;
      gnm_float *cbar = spx->cbar;
      gnm_float *gvec = spx->gvec;
      int j, j_beg, j_end, j_ptr, k, q, retry = 0;
      gnm_float best, temp, cbar_j, cbar_q;
loop: /* recompute basic solution components (if required) */
      if (retry)
      {  spx_eval_bbar(spx);
         spx_eval_pi(spx);
         spx_eval_cbar(spx);
      }
      /* nothing chosen so far */
      q = 0, best = 0.0;
      /* look through the list of non-basic variables */
      for (j = 1; j <= n; j++)
      {  cbar_j = dir * cbar[j];
         /* if xN[j] doesn't affect on the obj. function, skip it */
         if (cbar_j == 0.0) continue;
         /* analyze main cases */
         k = indx[m+j]; /* x[k] = xN[j] */
         switch (tagx[k])
         {  case LPX_NL:
               /* xN[j] can increase */
               if (cbar_j > -tol) continue;
               break;
            case LPX_NU:
               /* xN[j] can decrease */
               if (cbar_j < +tol) continue;
               break;
            case LPX_NF:
               /* xN[j] can change in any direction */
               if (-tol < cbar_j && cbar_j < +tol) continue;
               break;
            case LPX_NS:
               /* xN[j] can't change */
               continue;
            default:
               insist(tagx != tagx);
         }
         /* xN[j] can improve the objective function */
         temp = (cbar_j * cbar_j) / gvec[j];
         if (best < temp) q = j, best = temp;
      }
      /* since reduced costs are not computed directly every time, but
         recomputed recursively, the choice sometimes may be unreliable
         due to excessive round-off errors in the reduced costs; so, if
         q = 0 or if some test shows that the current value of cbar[q]
         is inaccurate, it is reasonable to recompute components of the
         current basic solution and then repeat the choice */
      if (!retry)
      {  if (q == 0)
         {  /* the current basic solution seems to be dual feasible,
               but we need to be sure that this is really so */
            retry = 1;
         }
         else
         {  /* xN[q] has been chosen; recompute its reduced cost more
               accurately using the formula d[q] = cN[q] - pi' * N[q] */
            k = indx[m+q]; /* x[k] = xN[q] */
            cbar_q = coef[k];
            if (k <= m)
            {  /* x[k] is auxiliary variable */
               cbar_q -= pi[k];
            }
            else
            {  /* x[k] is structural variable */
               j_beg = AT_ptr[k-m], j_end = AT_ptr[k-m+1];
               for (j_ptr = j_beg; j_ptr < j_end; j_ptr++)
                  cbar_q += pi[AT_ind[j_ptr]] * AT_val[j_ptr];
            }
            /* estimate an error in cbar[q] */
            temp = gnm_abs(cbar[q] - cbar_q) / (1.0 + gnm_abs(cbar_q));
            if (temp <= 0.10 * tol)
            {  /* the error is not so big; replace cbar[q] by the new,
                  more accurate value */
               cbar[q] = cbar_q;
            }
            else
            {  /* the error is too big; in this case cbar[q] may have
                  wrong sign (if it is close to zero) and involve wrong
                  choice a basic variable xB[p] in the future causing
                  numeric instability; therefore the vector of reduced
                  costs should be recomputed more accurately */
               if (spx->msg_lev >= 3)
                  print("spx_prim_chuzc: recomputing basic solution com"
                     "ponents");
               retry = 1;
            }
         }
         if (retry) goto loop;
      }
      /* store the number q and return */
      spx->q = q;
      return retry;
}

/*----------------------------------------------------------------------
-- spx_prim_chuzr - choose basic variable (primal simplex).
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- int spx_prim_chuzr(SPX *spx, gnm_float relax);
--
-- *Description*
--
-- The routine spx_prim_chuzr chooses a basic variable xB[p], which
-- should leave the basis.
--
-- The parameter relax is used to relax bounds of basic variables. If
-- relax = 0, the routine implements standard ("textbook") ratio test.
-- Otherwise, if relax > 0, the routine implements two-pass ratio test
-- proposed by P.Harris.
--
-- The routine chooses a basic variable xB[p], which reaches its bound
-- before any other basic variables when the chosen non-basic variable
-- xN[q] is changing in a valid direction.
--
-- If the basic variable xB[p] has been chosen, the routine sets p to
-- the number of this variable, 1 <= p <= m, and also provide the tag
-- p_tag, which should be set for xB[p] after it has left the basis.
-- The special case p < 0 means that the non-basic variable xN[q] being
-- gnm_float-bounded just goes to its opposite bound, so the current basis
-- remains unchanged. If xN[q] can infinitely change and therefore the
-- choice cannot be made, the routine sets p to 0.
--
-- *Returns*
--
-- Non-zero return code means that all elements of the q-th column of
-- the simplex table are too small (in absolute value); in this case
-- it is desirable to reinvert the current basis matrix and repeat the
-- choice. */

int spx_prim_chuzr(SPX *spx, gnm_float relax)
{     int m = spx->m;
      int n = spx->n;
      int *typx = spx->typx;
      gnm_float *lb = spx->lb;
      gnm_float *ub = spx->ub;
      gnm_float dir = (spx->dir == LPX_MIN ? +1.0 : -1.0);
      gnm_float *bbar = spx->bbar;
      gnm_float *cbar = spx->cbar;
      int *indx = spx->indx;
      int q = spx->q;
      gnm_float *aq = spx->aq;
      int i, i_tag, k, p, p_tag, ret = 0;
      gnm_float aq_i, abs_aq_i, big, eps, temp, teta;
      insist(1 <= q && q <= n);
      /* turn to the case of increasing xN[q] in order to simplify the
         program logic */
      if (dir * cbar[q] > 0.0)
         for (i = 1; i <= m; i++) aq[i] = - aq[i];
      /* compute the largest absolute value of elements of the q-th
         column of the simplex table */
      big = 0.0;
      for (i = 1; i <= m; i++)
      {  temp = aq[i];
         if (temp < 0.0) temp = - temp;
         if (big < temp) big = temp;
      }
#if 0
      /* if all elements of the q-th column of the simplex table are
         too small, tell the calling program to reinvert the basis and
         repeat the choice */
      if (big < spx->tol_piv) ret = 1;
#endif
      /* compute the absolute tolerance eps used to check elements of
         the q-th column */
      eps = spx->tol_piv * (1.0 + big);
      /* initial settings for the first pass */
      k = indx[m+q]; /* x[k] = xN[q] */
      if (typx[k] == LPX_DB)
      {  /* xN[q] has both lower and upper bounds */
         p = -1, p_tag = 0, teta = (ub[k] - lb[k]) + relax, big = 1.0;
      }
      else
      {  /* xN[q] has no opposite bound */
         p = 0, p_tag = 0, teta = DBL_MAX, big = 0.0;
      }
      /* look through the list of basic variables */
      for (i = 1; i <= m; i++)
      {  aq_i = aq[i];
         /* if xN[q] doesn't affect on xB[i], skip the latter */
         if (aq_i == 0.0) continue;
         abs_aq_i = (aq_i > 0.0 ? +aq_i : -aq_i);
         /* analyze main cases */
         k = indx[i]; /* x[k] = xB[i] */
         switch (typx[k])
         {  case LPX_FR:
               /* xB[i] is free variable */
               continue;
            case LPX_LO:
lo_1:          /* xB[i] has lower bound */
               if (aq_i > -eps) continue;
               i_tag = LPX_NL;
               temp = ((lb[k] - relax) - bbar[i]) / aq_i;
               break;
            case LPX_UP:
               /* xB[i] has upper bound */
up_1:          if (aq_i < +eps) continue;
               i_tag = LPX_NU;
               temp = ((ub[k] + relax) - bbar[i]) / aq_i;
               break;
            case LPX_DB:
               /* xB[i] has both lower and upper bounds */
               if (aq_i < 0.0) goto lo_1; else goto up_1;
            case LPX_FX:
               /* xB[i] is fixed variable */
               if (abs_aq_i < eps) continue;
               i_tag = LPX_NS;
               temp = relax / abs_aq_i;
               break;
            default:
               insist(typx != typx);
         }
         /* if xB[i] violates its bound (slightly, because the current
            basis is assumed to be primal feasible), temp is negative;
            we can think that this happens due to round-off errors and
            xB[i] is exactly on its bound; this allows to replace temp
            by zero */
         if (temp < 0.0) temp = 0.0;
         /* apply minimal ratio test */
         if (teta > temp || teta == temp && big < abs_aq_i)
            p = i, p_tag = i_tag, teta = temp, big = abs_aq_i;
      }
      /* the standard ratio test has been completed */
      if (relax == 0.0) goto done;
      /* if no basic variable has been chosen on the first pass, the
         second pass should be skipped */
      if (p == 0) goto done;
      /* teta is a maximal change (in absolute value) of the variable
         xN[q] in a valid direction, on which all the basic variables
         still remain within their (relaxed) bounds; on the second pass
         we try to choose a basic variable xB[p], for which the change
         of xN[q] is not greater than teta and which has the greatest
         (in absolute value) influence coefficient (element of the q-th
         column of the simplex table) */
      teta *= (1.0 + 3.0 * DBL_EPSILON);
      /* if xN[q] is gnm_float-bounded variable and can go to the opposite
         bound, it is reasonable to choose it */
      k = indx[m+q]; /* x[k] = xN[q] */
      if (typx[k] == LPX_DB && ub[k] - lb[k] <= teta)
      {  p = -1, p_tag = 0;
         goto done;
      }
      /* initial settings for the second pass */
      p = 0, p_tag = 0, big = 0.0;
      /* look through the list of basic variables */
      for (i = 1; i <= m; i++)
      {  aq_i = aq[i];
         /* if xN[q] doesn't affect on xB[i], skip the latter */
         if (aq_i == 0.0) continue;
         abs_aq_i = (aq_i > 0.0 ? +aq_i : -aq_i);
         /* analyze main cases */
         k = indx[i]; /* x[k] = xB[i] */
         switch (typx[k])
         {  case LPX_FR:
               /* xB[i] is free variable */
               continue;
            case LPX_LO:
lo_2:          /* xB[i] has lower bound */
               if (aq_i > -eps) continue;
               i_tag = LPX_NL;
               temp = (lb[k] - bbar[i]) / aq_i;
               break;
            case LPX_UP:
up_2:          /* xB[i] has upper bound */
               if (aq_i < +eps) continue;
               i_tag = LPX_NU;
               temp = (ub[k] - bbar[i]) / aq_i;
               break;
            case LPX_DB:
               /* xB[i] has both lower and upper bounds */
               if (aq_i < 0.0) goto lo_2; else goto up_2;
            case LPX_FX:
               /* xB[i] is fixed variable */
               if (abs_aq_i < eps) continue;
               i_tag = LPX_NS;
               temp = 0.0;
               break;
            default:
               insist(typx != typx);
         }
         /* see comments about this trick in the first pass loop */
         if (temp < 0.0) temp = 0.0;
         /* apply Harris' rule */
         if (temp <= teta && big < abs_aq_i)
            p = i, p_tag = i_tag, big = abs_aq_i;
      }
      /* the second pass should always choose some xB[p] */
      insist(1 <= p && p <= m);
done: /* restore original signs of the coefficients aq[i] */
      if (dir * cbar[q] > 0.0)
         for (i = 1; i <= m; i++) aq[i] = - aq[i];
      /* store the number p and the tag p_tag */
      spx->p = p, spx->p_tag = p_tag;
      /* return to the simplex method program */
      return ret;
}

/*----------------------------------------------------------------------
-- spx_dual_chuzr - choose basic variable (dual simplex).
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- void spx_dual_chuzr(SPX *spx, gnm_float tol);
--
-- *Description*
--
-- The routine spx_dual_chuzr chooses a basic variable xB[p], which
-- should leave the basis.
--
-- The parameter tol is a relative tolerance, which is used to check
-- if some basic variable violates the primal feasibility condition and
-- therefore can be considered as a possible candidate.
--
-- The routine chooses a basic variable xB[p], which has the largest
-- weighted residual:
--
--    |r[p]| / gnm_sqrt(delta[p]) = max(|r[i]| / gnm_sqrt(delta[i])),
--
-- where r[i] is a current residual for variable xB[i]:
--
--    r[i] = max(0, lB[i] - bbar[i]), if xB[i] has an lower bound,
--
--    r[i] = max(0, bbar[i] - uB[i]), if xB[i] has an upper bound,
--
-- and delta[i] is a "weight" of the variable xB[i].
--
-- If the basic variable xB[p] has been chosen, the routine sets p to
-- the number of this variable, 1 <= p <= m, and also provide the tag
-- p_tag, which should be set for xB[p] after it has left the basis.
-- Note that if xB[p] is fixed variable, the routine sets the tag as if
-- xB[p] were gnm_float-bounded variable; this is used in order to know in
-- what direction this variable will change leaving the basis. If the
-- current basis is primal feasible and therefore no choice has been
-- made, the routine sets p to 0. */

void spx_dual_chuzr(SPX *spx, gnm_float tol)
{     int m = spx->m;
      int *typx = spx->typx;
      gnm_float *lb = spx->lb;
      gnm_float *ub = spx->ub;
      int *indx = spx->indx;
      gnm_float *bbar = spx->bbar;
      gnm_float *dvec = spx->dvec;
      int i, k, p, p_tag, typx_k;
      gnm_float best, lb_k, ub_k, bbar_i, temp;
      /* nothing chosen so far */
      p = 0, p_tag = 0, best = 0.0;
      /* look through the list of basic variables */
      for (i = 1; i <= m; i++)
      {  k = indx[i]; /* x[k] = xB[i] */
         typx_k = typx[k];
         bbar_i = bbar[i];
         if (typx_k == LPX_LO || typx_k == LPX_DB || typx_k == LPX_FX)
         {  /* xB[i] has lower bound */
            lb_k = lb[k];
            temp = bbar_i - lb_k;
            lb_k = (lb_k < 0.0 ? -lb_k : +lb_k);
            if (temp / (1.0 + lb_k) < - tol)
            {  /* xB[i] violates its lower bound */
               temp = (temp * temp) / dvec[i];
               if (best < temp) p = i, p_tag = LPX_NL, best = temp;
            }
         }
         if (typx_k == LPX_UP || typx_k == LPX_DB || typx_k == LPX_FX)
         {  /* xB[i] has upper bound */
            ub_k = ub[k];
            temp = bbar_i - ub_k;
            ub_k = (ub_k < 0.0 ? -ub_k : +ub_k);
            if (temp / (1.0 + ub_k) > + tol)
            {  /* xB[i] violates its upper bound */
               temp = (temp * temp) / dvec[i];
               if (best < temp) p = i, p_tag = LPX_NU, best = temp;
            }
         }
      }
      spx->p = p, spx->p_tag = p_tag;
      return;
}

/*----------------------------------------------------------------------
-- spx_dual_chuzc - choose non-basic variable (dual simplex).
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- int spx_dual_chuzc(SPX *spx, gnm_float relax);
--
-- *Description*
--
-- The routine spx_dual_chuzc chooses a non-basic variable xN[q], which
-- should enter the basis.
--
-- The parameter relax is used to relax (zero) bounds of reduced costs
-- of non-basic variables (i.e. bounds of dual variables). If relax = 0,
-- the routine implements standard ("textbook") ratio test. Otherwise,
-- if relax > 0, the routine implements two-pass ratio test proposed by
-- P.Harris.
--
-- The routine chooses a non-basic variables xN[q], whose reduced cost
-- reaches zero before reduced costs of any other non-basic variables
-- when reduced cost of the chosen basic variable xB[p] is changing in
-- in a valid direction starting from zero.
--
-- If the non-basic variable xN[q] has been chosen, the routine sets q
-- to the number of this variable, 1 <= q <= n. If reduced cost of xB[p]
-- can infinitely change and therefore the choice cannot be made, the
-- routine sets q to 0. */

int spx_dual_chuzc(SPX *spx, gnm_float relax)
{     int m = spx->m;
      int n = spx->n;
      gnm_float dir = (spx->dir == LPX_MIN ? +1.0 : -1.0);
      int *indx = spx->indx;
      int *tagx = spx->tagx;
      gnm_float *cbar = spx->cbar;
      int p = spx->p;
      int p_tag = spx->p_tag;
      gnm_float *ap = spx->ap;
      int j, k, q, ret = 0;
      gnm_float big, eps, teta, ap_j, abs_ap_j, temp;
      insist(1 <= p && p <= m);
      insist(p_tag == LPX_NL || p_tag == LPX_NU);
      /* turn to the case of increasing xB[p] in order to simplify the
         program logic */
      if (p_tag == LPX_NU)
         for (j = 1; j <= n; j++) ap[j] = - ap[j];
      /* compute the largest absolute value of elements of the p-th row
         of the simplex table */
      big = 0.0;
      for (j = 1; j <= n; j++)
      {  temp = ap[j];
         if (temp < 0.0) temp = - temp;
         if (big < temp) big = temp;
      }
#if 0
      /* if all elements in the p-th row of the simplex table are too
         small, tell the calling program to reinvert the basis and then
         repeat the choice */
      if (big < spx->tol_piv) ret = 1;
#endif
      /* compute the absolute tolerance eps used to check elements of
         the p-th row */
      eps = spx->tol_piv * (1.0 + big);
      /* initial settings for the first pass */
      q = 0, teta = DBL_MAX, big = 0.0;
      /* look through the list of non-basic variables */
      for (j = 1; j <= n; j++)
      {  ap_j = ap[j];
         /* if xN[j] doesn't affect ob xB[p], skip the former */
         if (ap_j == 0.0) continue;
         abs_ap_j = (ap_j > 0.0 ? +ap_j : -ap_j);
         /* analyze main cases */
         k = indx[m+j]; /* x[k] = xN[j] */
         switch (tagx[k])
         {  case LPX_NF:
               /* xN[j] is free variable */
               if (abs_ap_j < eps) continue;
               temp = relax / abs_ap_j;
               break;
            case LPX_NL:
               /* xN[j] is on its lower bound */
               if (ap_j < +eps) continue;
               temp = (dir * cbar[j] + relax) / ap_j;
               break;
            case LPX_NU:
               /* xN[j] is on its upper bound */
               if (ap_j > -eps) continue;
               temp = (dir * cbar[j] - relax) / ap_j;
               break;
            case LPX_NS:
               /* xN[j] is fixed variable */
               continue;
            default:
               insist(tagx != tagx);
         }
         /* if reduced cost of xN[j] violates its zero bound (slightly,
            because the current basis is assumed to be dual feasible),
            temp is negative; we can think that this happens due to
            round-off errors and reduced cost of xN[j] is exact zero in
            this case; this allows to replace temp by zero */
         if (temp < 0.0) temp = 0.0;
         /* apply minimal ratio test */
         if (teta > temp || teta == temp && big < abs_ap_j)
            q = j, teta = temp, big = abs_ap_j;
      }
      /* the standard ratio test has been completed */
      if (relax == 0.0) goto done;
      /* if no variable has been chosen on the first pass, the second
         pass should be skipped */
      if (q == 0) goto done;
      /* teta is a maximal change (in absolute value) of reduced cost
         of the variable xB[p] in a valid direction, on which reduced
         costs of all the non-basic variables still remain within their
         relaxed bounds; on the second pass we try to choose a non-basic
         variable xN[q], for which the change of reduced cost of xB[p]
         if not greater than teta and which has the largest (in absolute
         value) influence coefficient (i.e. element of the p-th row of
         the simplex table) */
      teta *= (1.0 + 3.0 * DBL_EPSILON);
      /* initial settings for the second pass */
      q = 0, big = 0.0;
      /* look through the list of non-basic variables */
      for (j = 1; j <= n; j++)
      {  ap_j = ap[j];
         /* if xN[j] doesn't affect ob xB[p], skip the former */
         if (ap_j == 0.0) continue;
         abs_ap_j = (ap_j > 0.0 ? +ap_j : -ap_j);
         /* analyze main cases */
         k = indx[m+j]; /* x[k] = xN[j] */
         switch (tagx[k])
         {  case LPX_NF:
               /* xN[j] is free variable */
               if (abs_ap_j < eps) continue;
               temp = 0.0;
               break;
            case LPX_NL:
               /* xN[j] is on its lower bound */
               if (ap_j < +eps) continue;
               temp = (dir * cbar[j]) / ap_j;
               break;
            case LPX_NU:
               /* xN[j] is on its upper bound */
               if (ap_j > -eps) continue;
               temp = (dir * cbar[j]) / ap_j;
               break;
            case LPX_NS:
               /* xN[j] is fixed variable */
               continue;
            default:
               insist(tagx != tagx);
         }
         /* see comments about this trick in the first pass loop */
         if (temp < 0.0) temp = 0.0;
         /* apply Harris' rule */
         if (temp <= teta && big < abs_ap_j)
            q = j, big = abs_ap_j;
      }
      /* the second pass should always choose some xN[q] */
      insist(1 <= q && q <= n);
done: /* restore original signs of the coefficients ap[j] */
      if (p_tag == LPX_NU)
         for (j = 1; j <= n; j++) ap[j] = - ap[j];
      /* store the number q */
      spx->q = q;
      /* return to the simplex method program */
      return ret;
}

/*----------------------------------------------------------------------
-- spx_update_bbar - update values of basic variables.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- void spx_update_bbar(SPX *spx, gnm_float *obj);
--
-- *Description*
--
-- The routine spx_update_bbar recomputes values of basic variables for
-- an adjacent basis.
--
-- If the parameter obj is not NULL, it is assumed that *obj is the
-- current value of the objective function, in which case the routine
-- recomputes it for the adjacent basis.
--
-- This routine assumes that the basic variable xB[p] and the non-basic
-- variable xN[q] (that define the adjacent basis) have been chosen but
-- the current basis has not been changed yet. */

void spx_update_bbar(SPX *spx, gnm_float *obj)
{     int m = spx->m;
      int n = spx->n;
      int *typx = spx->typx;
      gnm_float *lb = spx->lb;
      gnm_float *ub = spx->ub;
      int *tagx = spx->tagx;
      int *indx = spx->indx;
      gnm_float *bbar = spx->bbar;
      int p = spx->p;
      int p_tag = spx->p_tag;
      int q = spx->q;
      gnm_float *aq = spx->aq;
      int i, k;
      gnm_float aq_i, dxn_q, new_xb_p;
      if (p < 0)
      {  /* the special case: xN[q] goes to its opposite bound */
         insist(1 <= q && q <= n);
         k = indx[m+q]; /* x[k] = xN[q] */
         insist(typx[k] == LPX_DB);
         switch (tagx[k])
         {  case LPX_NL:
               /* xN[q] goes from its lower bound to its upper one */
               dxn_q = ub[k] - lb[k];
               break;
            case LPX_NU:
               /* xN[q] goes from its upper bound to its lower one */
               dxn_q = lb[k] - ub[k];
               break;
            default:
               insist(tagx != tagx);
         }
         /* recompute values of the basic variables */
         for (i = 1; i <= m; i++)
         {  aq_i = aq[i];
            if (aq_i != 0.0) bbar[i] += aq_i * dxn_q;
         }
      }
      else
      {  /* xB[p] will leave and xN[q] will enter the basis */
         insist(1 <= p && p <= m);
         insist(1 <= q && q <= n);
         /* determine value of xB[p] in the adjacent basis */
         k = indx[p];
         switch (p_tag)
         {  case LPX_NL:
               new_xb_p = lb[k]; break;
            case LPX_NU:
               new_xb_p = ub[k]; break;
            case LPX_NF:
               new_xb_p = 0.0; break;
            case LPX_NS:
               new_xb_p = lb[k]; break;
            default:
               insist(p_tag != p_tag);
         }
         /* determine increment of xN[q] in the adjacent basis */
         insist(aq[p] != 0.0);
         dxn_q = (new_xb_p - bbar[p]) / aq[p];
         /* compute a new value of xN[q] when it has entered the basis
            and taken the place of xB[p] */
         bbar[p] = spx_eval_xn_j(spx, q) + dxn_q;
         /* recompute values of other basic variables */
         for (i = 1; i <= m; i++)
         {  if (i == p) continue;
            aq_i = aq[i];
            if (aq_i != 0.0) bbar[i] += aq_i * dxn_q;
         }
      }
      /* recompute value of the objective function (if req'd) */
      if (obj != NULL) *obj += spx->cbar[q] * dxn_q;
      return;
}

/*----------------------------------------------------------------------
-- spx_update_pi - update simplex multipliers.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- void spx_update_pi(SPX *spx);
--
-- *Description*
--
-- The routine spx_update_pi recomputes simplex multipliers (Lagrange
-- multipliers for the equality constraints) for an adjacent basis.
--
-- This routine assumes that the basic variable xB[p] and the non-basic
-- variable xN[q] (that define the adjacent basis) have been chosen but
-- the current basis has not been changed yet. */

void spx_update_pi(SPX *spx)
{     int m = spx->m;
      int n = spx->n;
      gnm_float *pi = spx->pi;
      gnm_float *cbar = spx->cbar;
      int p = spx->p;
      int q = spx->q;
      gnm_float *zeta = spx->zeta;
      gnm_float *ap = spx->ap;
      int i;
      gnm_float new_cbar_q, zeta_i;
      /* xB[p] will leave and xN[q] will enter the basis */
      insist(1 <= p && p <= m);
      insist(1 <= q && q <= n);
      insist(ap[q] != 0.0);
      new_cbar_q = cbar[q] / ap[q];
      for (i = 1; i <= m; i++)
      {  zeta_i = zeta[i];
         if (zeta_i != 0.0) pi[i] -= zeta_i * new_cbar_q;
      }
      return;
}

/*----------------------------------------------------------------------
-- spx_update_cbar - update reduced costs of non-basic variables.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- void spx_update_cbar(SPX *spx, int all);
--
-- *Description*
--
-- The routine spx_update_cbar recomputes reduced costs of non-basic
-- variables for an adjacent basis.
--
-- If the parameter all is set, the routine recomputes reduced costs of
-- all non-basic variables. Otherwise reduced costs for fixed non-basic
-- variables are not recomputed.
--
-- This routine assumes that the basic variable xB[p] and the non-basic
-- variable xN[q] (that define the adjacent basis) have been chosen but
-- the current basis has not been changed yet. */

void spx_update_cbar(SPX *spx, int all)
{     int m = spx->m;
      int n = spx->n;
      int *tagx = spx->tagx;
      int *indx = spx->indx;
      gnm_float *cbar = spx->cbar;
      int p = spx->p;
      int q = spx->q;
      gnm_float *ap = spx->ap;
      int j, k;
      gnm_float new_cbar_q, ap_j;
      /* xB[p] will leave and xN[q] will enter the basis */
      insist(1 <= p && p <= m);
      insist(1 <= q && q <= n);
      /* compute a new reduced cost of xB[p] when it has left the basis
         and taken the place of xN[q] */
      new_cbar_q = (cbar[q] /= ap[q]);
      /* recompute reduced costs of other non-basic variables */
      for (j = 1; j <= n; j++)
      {  if (j == q) continue;
         if (!all)
         {  k = indx[m+j]; /* x[k] = xN[j] */
            if (tagx[k] == LPX_NS)
            {  cbar[j] = 0.0;
               continue;
            }
         }
         ap_j = ap[j];
         if (ap_j != 0.0) cbar[j] -= ap_j * new_cbar_q;
      }
      return;
}

/*----------------------------------------------------------------------
-- change_basis - change basis and update the factorization.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- int spx_change_basis(SPX *spx);
--
-- *Description*
--
-- The routine spx_change_basis changes the current basis assuming that
-- the basic variable xB[p] leaves the basis and the non-basic variable
-- xN[q] enters it. Then the routine updates the factorization of the
-- basis matrix for the adjacent basis.
--
-- In the specilal case, which is indicated by p < 0, the current basis
-- is not changed, but the non-basic gnm_float-bounded variable xN[q] goes
-- from its current bound to the opposite one.
--
-- *Returns*
--
-- Zero return code means that the factorization has been successfully
-- updated. Non-zero return code means that the factorization should be
-- reinverted by the routine spx_invert. */

int spx_change_basis(SPX *spx)
{     int m = spx->m;
      int n = spx->n;
      int *typx = spx->typx;
      int *tagx = spx->tagx;
      int *posx = spx->posx;
      int *indx = spx->indx;
      int p = spx->p;
      int p_tag = spx->p_tag;
      int q = spx->q;
      int k, kp, kq, ret;
      if (p < 0)
      {  /* the special case: xN[q] goes to its opposite bound */
         insist(1 <= q && q <= n);
         k = indx[m+q]; /* x[k] = xN[q] */
         insist(typx[k] == LPX_DB);
         insist(tagx[k] == LPX_NL || tagx[k] == LPX_NU);
         tagx[k] = (tagx[k] == LPX_NL ? LPX_NU : LPX_NL);
         ret = 0;
      }
      else
      {  /* xB[p] leaves the basis, xN[q] enters the basis */
         insist(1 <= p && p <= m);
         insist(1 <= q && q <= n);
         /* xN[q] takes the place of xB[p] and vice versa */
         kp = indx[p];   /* x[kp] = xB[p] */
         kq = indx[m+q]; /* x[kq] = xN[q] */
         tagx[kp] = p_tag,  posx[kp] = m+q, indx[m+q] = kp;
         tagx[kq] = LPX_BS, posx[kq] = p,   indx[p]   = kq;
         /* check the tag of xN[q] (former xB[p]) */
         switch (typx[kp])
         {  case LPX_FR:
               insist(p_tag == LPX_NF); break;
            case LPX_LO:
               insist(p_tag == LPX_NL); break;
            case LPX_UP:
               insist(p_tag == LPX_NU); break;
            case LPX_DB:
               insist(p_tag == LPX_NL || p_tag == LPX_NU); break;
            case LPX_FX:
               insist(p_tag == LPX_NS); break;
            default:
               insist(typx != typx);
         }
         /* update the factorization of the basis matrix */
         ret = spx_update(spx, p);
      }
      /* one simplex iteration has been performed */
      if (spx->it_lim > 0) spx->it_lim--;
      spx->it_cnt++;
      /* return to the simplex method program */
      return ret;
}

/*----------------------------------------------------------------------
-- spx_err_in_bbar - compute maximal absolute error in bbar.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- gnm_float spx_err_in_bbar(SPX *spx);
--
-- *Returns*
--
-- The routine spx_err_in_bbar returns the maximal absolute error
--
--    max|bbar[i] - bbar'[i]|,
--
-- where bbar and bbar' are, respectively, directly computed and the
-- current (updated) values of basic variables.
--
-- This routine is intended for debugging purposes only. */

gnm_float spx_err_in_bbar(SPX *spx)
{     int m = spx->m;
      gnm_float *bbar = spx->bbar;
      int i;
      gnm_float d, dmax;
      spx->bbar = ucalloc(1+m, sizeof(gnm_float));
      spx_eval_bbar(spx);
      dmax = 0.0;
      for (i = 1; i <= m; i++)
      {  d = gnm_abs(spx->bbar[i] - bbar[i]);
         if (dmax < d) dmax = d;
      }
      ufree(spx->bbar);
      spx->bbar = bbar;
      return dmax;
}

/*----------------------------------------------------------------------
-- spx_err_in_pi - compute maximal absolute error in pi.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- gnm_float spx_err_in_pi(SPX *spx);
--
-- *Returns*
--
-- The routine spx_err_in_pi returns the maximal absolute error
--
--    max|pi[i] - pi'[i]|,
--
-- where pi and pi' are, respectively, directly computed and the current
-- (updated) values of simplex multipliers.
--
-- This routine is intended for debugging purposes only. */

gnm_float spx_err_in_pi(SPX *spx)
{     int m = spx->m;
      gnm_float *pi = spx->pi;
      int i;
      gnm_float d, dmax;
      spx->pi = ucalloc(1+m, sizeof(gnm_float));
      spx_eval_pi(spx);
      dmax = 0.0;
      for (i = 1; i <= m; i++)
      {  d = gnm_abs(spx->pi[i] - pi[i]);
         if (dmax < d) dmax = d;
      }
      ufree(spx->pi);
      spx->pi = pi;
      return dmax;
}

/*----------------------------------------------------------------------
-- spx_err_in_cbar - compute maximal absolute error in cbar.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- gnm_float spx_err_in_cbar(SPX *spx, int all);
--
-- *Returns*
--
-- The routine spx_err_in_cbar returns the maximal absolute error
--
--    max|cbar[j] - cbar'[j]|,
--
-- where cbar and cbar' are, respectively, directly computed and the
-- current (updated) reduced costs of non-basic variables (if the flag
-- all is not set, fixed variables are not considered).
--
-- This routine is intended for debugging purposes only. */

gnm_float spx_err_in_cbar(SPX *spx, int all)
{     int m = spx->m;
      int n = spx->n;
      int *tagx = spx->tagx;
      int *indx = spx->indx;
      gnm_float *cbar = spx->cbar;
      int j, k;
      gnm_float d, dmax;
      spx->cbar = ucalloc(1+n, sizeof(gnm_float));
      spx_eval_cbar(spx);
      dmax = 0.0;
      for (j = 1; j <= n; j++)
      {  if (!all)
         {  k = indx[m+j]; /* x[k] = xN[j] */
            if (tagx[k] == LPX_NS) continue;
         }
         d = gnm_abs(spx->cbar[j] - cbar[j]);
         if (dmax < d) dmax = d;
      }
      ufree(spx->cbar);
      spx->cbar = cbar;
      return dmax;
}

/*----------------------------------------------------------------------
-- spx_reset_refsp - reset the reference space.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- void spx_reset_refsp(SPX *spx);
--
-- *Description*
--
-- The routine spx_reset_refsp resets (redefines) the reference space
-- and the vector gamma (in the case of primal simplex) or the vector
-- delta (in the case of dual simplex) used in the projected steepest
-- edge technique. After reset the reference space corresponds to the
-- current set of non-basic (primal simplex) or basic (dual simplex)
-- variables, and therefore all elements of the vector gamma or delta
-- are equal to one (by the definition). */

void spx_reset_refsp(SPX *spx)
{     int m = spx->m;
      int n = spx->n;
      int *tagx = spx->tagx;
      gnm_float *gvec = spx->gvec;
      gnm_float *dvec = spx->dvec;
      int *refsp = spx->refsp;
      int i, j, k;
      switch (spx->meth)
      {  case 'P':
            /* primal simplex is used */
            for (k = 1; k <= m+n; k++)
               refsp[k] = (tagx[k] == LPX_BS ? 0 : 1);
            for (j = 1; j <= n; j++) gvec[j] = 1.0;
            break;
         case 'D':
            /* dual simplex is used */
            for (k = 1; k <= m+n; k++)
               refsp[k] = (tagx[k] == LPX_BS ? 1 : 0);
            for (i = 1; i <= m; i++) dvec[i] = 1.0;
            break;
         default:
            insist(spx->meth != spx->meth);
      }
      /* after this number of simplex iterations the reference space
         should be again reset */
      spx->count = 1000;
      return;
}

/*----------------------------------------------------------------------
-- spx_update_gvec - update the vector gamma for adjacent basis.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- void spx_update_gvec(SPX *spx);
--
-- *Description*
--
-- The routine spx_update_gvec recomputes the vector gamma (used in the
-- primal projected steepest edge technique) for an adjacent basis.
--
-- This routine assumes that the basic variable xB[p] and the non-basic
-- variable xN[q] (that define the adjacent basis) have been chosen but
-- the current basis has not been changed yet. */

void spx_update_gvec(SPX *spx)
{     int m = spx->m;
      int n = spx->n;
      int *AT_ptr = spx->AT_ptr;
      int *AT_ind = spx->AT_ind;
      gnm_float *AT_val = spx->AT_val;
      int *tagx = spx->tagx;
      int *indx = spx->indx;
      int p = spx->p;
      int q = spx->q;
      gnm_float *ap = spx->ap;
      gnm_float *aq = spx->aq;
      gnm_float *gvec = spx->gvec;
      int *refsp = spx->refsp;
      int i, j, j_beg, j_end, j_ptr, k, ref_k, ref_p, ref_q;
      gnm_float ap_j, ap_q, aq_i, s_j, t1, t2, t3, sum, temp;
      gnm_float *w = spx->work;
      insist(1 <= p && p <= m);
      insist(1 <= q && q <= n);
      /* check if it's time to reset the reference space */
      if (spx->count <= 0)
      {  /* yes, do that */
         spx_reset_refsp(spx);
         goto done;
      }
      else
      {  /* otherwise decrease the count */
         spx->count--;
      }
      /* compute t1 and w */
      t1 = 0.0;
      for (i = 1; i <= m; i++)
      {  if (i != p && refsp[indx[i]])
         {  w[i] = aq[i];
            t1 += w[i] * w[i];
         }
         else
            w[i] = 0.0;
      }
      spx_btran(spx, w);
      /* update the vector gamma */
      ref_p = refsp[indx[p]];    /* if xB[p] belongs to the space */
      ref_q = refsp[indx[m+q]];  /* if xN[q] belongs to the space */
      ap_q = ap[q];
      insist(ap_q != 0.0);
      for (j = 1; j <= n; j++)
      {  /* gvec[q] will be computed later */
         if (j == q) continue;
         /* if xN[j] is fixed variable, its weight is not used, because
            fixed variables never enter the basis */
         k = indx[m+j]; /* x[k] = xN[j] */
         if (tagx[k] == LPX_NS)
         {  gvec[j] = 1.0;
            continue;
         }
         ref_k = refsp[k]; /* if xN[j] belongs to the space */
         /* compute s[j] */
         ap_j = ap[j];
         s_j = gvec[j];
         if (ref_p) s_j -= ap_j * ap_j;
         if (ref_k) s_j -= 1.0;
         if (ap_j == 0.0)
            t3 = 0.0;
         else
         {  /* t2 := N[j] * w */
            if (k <= m)
            {  /* x[k] is auxiliary variable */
               t2 = + w[k];
            }
            else
            {  /* x[k] is structural variable */
               t2 = 0.0;
               j_beg = AT_ptr[k-m], j_end = AT_ptr[k-m+1];
               for (j_ptr = j_beg; j_ptr < j_end; j_ptr++)
                  t2 -= AT_val[j_ptr] * w[AT_ind[j_ptr]];
            }
            t3 = ap_j / ap_q;
            s_j += (2.0 * t2 + t1 * t3) * t3;
         }
         /* update gvec[j] */
         if (ref_k) s_j += 1.0;
         if (ref_q) s_j += t3 * t3;
         if (s_j < DBL_EPSILON) s_j = 1.0;
         gvec[j] = s_j;
      }
      /* compute exact value of gvec[q] */
      sum = (ref_p ? 1.0 : 0.0);
      temp = ap_q * ap_q;
      for (i = 1; i <= m; i++)
      {  if (i == p)
         {  if (ref_q)
               sum += 1.0 / temp;
         }
         else
         {  if (refsp[indx[i]])
            {  aq_i = aq[i];
               sum += (aq_i * aq_i) / temp;
            }
         }
      }
      gvec[q] = sum;
done: return;
}

/*----------------------------------------------------------------------
-- spx_err_in_gvec - compute maximal absolute error in gvec.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- gnm_float spx_err_in_gvec(SPX *spx);
--
-- *Returns*
--
-- The routine spx_err_in_gvec returns the maximal absolute error
--
--    max|gvec[j] - gvec'[j]|,
--
-- where gvec and gvec' are, respectively, directly computed and the
-- current (updated) elements of the vector gamma.
--
-- This routine is intended for debugging purposes only. */

gnm_float spx_err_in_gvec(SPX *spx)
{     int m = spx->m;
      int n = spx->n;
      int *indx = spx->indx;
      gnm_float *gvec = spx->gvec;
      int *refsp = spx->refsp;
      gnm_float *aj = spx->work;
      int i, j, k;
      gnm_float dmax, d, gvec_j;
      dmax = 0.0;
      for (j = 1; j <= n; j++)
      {  /* if xN[j] is fixed variable, skip it, because its weight is
            not updated */
         k = indx[m+j];
         if (spx->typx[k] == LPX_FX)
         {  insist(spx->tagx[k] == LPX_NS);
            continue;
         }
         /* compute exact value of gvec[j] */
         spx_eval_col(spx, j, aj, 0);
         gvec_j = (refsp[indx[m+j]] ? 1.0 : 0.0);
         for (i = 1; i <= m; i++)
            if (refsp[indx[i]]) gvec_j += aj[i] * aj[i];
         /* compute absolute error in gvec[j] */
         d = gnm_abs(gvec_j - gvec[j]);
         if (dmax < d) dmax = d;
      }
      return dmax;
}

/*----------------------------------------------------------------------
-- spx_update_dvec - update the vector delta for adjacent basis.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- void spx_update_dvec(SPX *spx);
--
-- *Description*
--
-- The routine spx_update_dvec recomputes the vector delta (used in the
-- dual projected steepest edge technique) for an adjacent basis.
--
-- This routine assumes that the basic variable xB[p] and the non-basic
-- variable xN[q] (that define the adjacent basis) have been chosen but
-- the current basis has not been changed yet. */

void spx_update_dvec(SPX *spx)
{     int m = spx->m;
      int n = spx->n;
      int *typx = spx->typx;
      int *AT_ptr = spx->AT_ptr;
      int *AT_ind = spx->AT_ind;
      gnm_float *AT_val = spx->AT_val;
      int *indx = spx->indx;
      int p = spx->p;
      int q = spx->q;
      gnm_float *ap = spx->ap;
      gnm_float *aq = spx->aq;
      gnm_float *dvec = spx->dvec;
      int *refsp = spx->refsp;
      gnm_float *w = spx->work;
      int i, j, j_beg, j_end, j_ptr, k, ref_k, ref_p, ref_q;
      gnm_float ap_j, aq_p, aq_i, s_i, t1, sum, temp;
      insist(1 <= p && p <= m);
      insist(1 <= q && q <= n);
      /* check if it's time to reset the reference space */
      if (spx->count <= 0)
      {  /* yes, do that */
         spx_reset_refsp(spx);
         goto done;
      }
      else
      {  /* otherwise decrease the count */
         spx->count--;
      }
      /* compute t1 */
      t1 = 0.0;
      for (j = 1; j <= n; j++)
      {  if (j != q && refsp[indx[m+j]])
         {  ap_j = ap[j];
            t1 += ap_j * ap_j;
         }
      }
      /* compute w */
      for (i = 1; i <= m; i++) w[i] = 0.0;
      for (j = 1; j <= n; j++)
      {  if (j != q && refsp[indx[m+j]])
         {  /* w += ap[j] * N[j] */
            ap_j = ap[j];
            if (ap_j == 0.0) continue;
            k = indx[m+j]; /* x[k] = xN[j] */
            if (k <= m)
               /* xN[j] is auxiliary variable */
               w[k] += ap_j;
            else
            {  /* xN[j] is structural variable */
               j_beg = AT_ptr[k-m], j_end = AT_ptr[k-m+1];
               for (j_ptr = j_beg; j_ptr < j_end; j_ptr++)
                  w[AT_ind[j_ptr]] -= ap_j * AT_val[j_ptr];
            }
         }
      }
      spx_ftran(spx, w, 0);
      /* update the vector delta */
      ref_p = refsp[indx[p]];    /* if xB[p] belongs to the space */
      ref_q = refsp[indx[m+q]];  /* if xN[q] belongs to the space */
      aq_p = aq[p];
      insist(aq_p != 0.0);
      for (i = 1; i <= m; i++)
      {  /* dvec[p] will be computed later */
         if (i == p) continue;
         /* if xB[i] is free variable, its weight is not used, because
            free variables never leave the basis */
         k = indx[i];
         if (typx[k] == LPX_FR)
         {  dvec[i] = 1.0;
            continue;
         }
         ref_k = refsp[k]; /* if xB[i] belongs to the space */
         /* compute s_i */
         aq_i = aq[i];
         s_i = dvec[i];
         if (ref_k) s_i -= 1.0;
         if (ref_q) s_i -= aq[i] * aq[i];
         if (aq_i == 0.0)
            temp = 0.0;
         else
         {  temp = aq_i / aq_p;
            s_i += (2.0 * w[i] + t1 * temp) * temp;
         }
         /* update dvec[i] */
         if (ref_k) s_i += 1.0;
         if (ref_p) s_i += temp * temp;
         if (s_i < DBL_EPSILON) s_i = 1.0;
         dvec[i] = s_i;
      }
      /* compute exact value of dvec[p] */
      sum = (ref_q ? 1.0 : 0.0);
      temp = aq_p * aq_p;
      for (j = 1; j <= n; j++)
      {  if (j == q)
         {  if (ref_p)
               sum += 1.0 / temp;
         }
         else
         {  if (refsp[indx[m+j]])
            {  ap_j = ap[j];
               sum += (ap_j * ap_j) / temp;
            }
         }
      }
      dvec[p] = sum;
done: return;
}

/*----------------------------------------------------------------------
-- spx_err_in_dvec - compute maximal absolute error in dvec.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- gnm_float spx_err_in_dvec(SPX *spx);
--
-- *Returns*
--
-- The routine spx_err_in_dvec returns the maximal absolute error
--
--    max|dvec[j] - dvec'[j]|,
--
-- where dvec and dvec' are, respectively, directly computed and the
-- current (updated) elements of the vector delta.
--
-- This routine is intended for debugging purposes only. */

gnm_float spx_err_in_dvec(SPX *spx)
{     int m = spx->m;
      int n = spx->n;
      int *typx = spx->typx;
      int *indx = spx->indx;
      gnm_float *dvec = spx->dvec;
      int *refsp = spx->refsp;
      gnm_float *rho = spx->work;
      gnm_float *ai = &spx->work[m];
      int i, j, k;
      gnm_float dmax, d, dvec_i;
      dmax = 0.0;
      for (i = 1; i <= m; i++)
      {  /* if xB[i] is free variable, skip it, because its weight is
            not updated */
         k = indx[i];
         if (typx[k] == LPX_FR) continue;
         /* compute exact value of dvec[i] */
         spx_eval_rho(spx, i, rho);
         spx_eval_row(spx, rho, ai);
         dvec_i = (refsp[indx[i]] ? 1.0 : 0.0);
         for (j = 1; j <= n; j++)
            if (refsp[indx[m+j]]) dvec_i += ai[j] * ai[j];
         /* compute absolute error in dvec[i] */
         d = gnm_abs(dvec_i - dvec[i]);
         if (dmax < d) dmax = d;
      }
      return dmax;
}

/* eof */
