/* glplpx6a.c (simplex-based solver routines) */

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

#include <math.h>
#include <string.h>
#include "glplib.h"
#include "glplpp.h"
#include "glpspx.h"

/*----------------------------------------------------------------------
-- lpx_simplex - easy-to-use driver to the simplex method.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_simplex(LPX *lp);
--
-- *Description*
--
-- The routine lpx_simplex is intended to find optimal solution of an
-- LP problem, which is specified by the parameter lp.
--
-- Currently this routine implements an easy variant of the two-phase
-- primal simplex method, where on the phase I the routine searches for
-- a primal feasible solution, and on the phase II for an optimal one.
-- (However, if the initial basic solution is primal infeasible, but
-- dual feasible, the dual simplex method may be used; see the control
-- parameter LPX_K_DUAL.)
--
-- *Returns*
--
-- If the LP presolver is not used, the routine lpx_simplex returns one
-- of the following exit codes:
--
-- LPX_E_OK       the LP problem has been successfully solved.
--
-- LPX_E_FAULT    either the LP problem has no rows and/or columns, or
--                the initial basis is invalid, or the basis matrix is
--                singular or ill-conditioned.
--
-- LPX_E_OBJLL    the objective function has reached its lower limit
--                and continues decreasing.
--
-- LPX_E_OBJUL    the objective function has reached its upper limit
--                and continues increasing.
--
-- LPX_E_ITLIM    iterations limit exceeded.
--
-- LPX_E_TMLIM    time limit exceeded.
--
-- LPX_E_SING     the basis matrix becomes singular or ill-conditioned
--                due to improper simplex iteration.
--
-- If the LP presolver is used, the routine lpx_simplex returns one of
-- the following exit codes:
--
-- LPX_E_OK       optimal solution of the LP problem has been found.
--
-- LPX_E_FAULT    the LP problem has no rows and/or columns.
--
-- LPX_E_NOPFS    the LP problem has no primal feasible solution.
--
-- LPX_E_NODFS    the LP problem has no dual feasible solution.
--
-- LPX_E_NOSOL    the presolver cannot recover undefined or non-optimal
--                solution.
--
-- LPX_E_ITLIM    same as above.
--
-- LPX_E_TMLIM    same as above.
--
-- LPX_E_SING     same as above.
--
-- Note that additional exit codes may appear in the future versions of
-- this routine. */

static int simplex1(LPX *lpx)
{     /* base driver which does not use LP presolver */
      SPX _spx, *spx = &_spx;
      int i, j, k, m, n, t, nnz, typx, tagx, pos, ret;
      gnm_float rii, sjj, lb, ub, *prim, *dual;
      spx->m = m = lpx_get_num_rows(lpx);
      spx->n = n = lpx_get_num_cols(lpx);
      spx->typx = ucalloc(1+m+n, sizeof(int));
      spx->lb = ucalloc(1+m+n, sizeof(gnm_float));
      spx->ub = ucalloc(1+m+n, sizeof(gnm_float));
      for (i = 1; i <= m; i++)
      {  rii = lpx_get_rii(lpx, i);
         lpx_get_row_bnds(lpx, i, &typx, &lb, &ub);
         spx->typx[i] = typx;
         spx->lb[i] = lb * rii;
         spx->ub[i] = ub * rii;
      }
      for (j = 1; j <= n; j++)
      {  sjj = lpx_get_sjj(lpx, j);
         lpx_get_col_bnds(lpx, j, &typx, &lb, &ub);
         spx->typx[m+j] = typx;
         spx->lb[m+j] = lb / sjj;
         spx->ub[m+j] = ub / sjj;
      }
      spx->dir = lpx_get_obj_dir(lpx);
      spx->coef = ucalloc(1+m+n, sizeof(gnm_float));
      spx->coef[0] = lpx_get_obj_coef(lpx, 0);
      for (i = 1; i <= m; i++) spx->coef[i] = 0.0;
      for (j = 1; j <= n; j++)
      {  sjj = lpx_get_sjj(lpx, j);
         spx->coef[m+j] = lpx_get_obj_coef(lpx, j) * sjj;
      }
      nnz = lpx_get_num_nz(lpx);
      spx->A_ptr = ucalloc(1+m+1, sizeof(int));
      spx->A_ind = ucalloc(1+nnz, sizeof(int));
      spx->A_val = ucalloc(1+nnz, sizeof(gnm_float));
      pos = 1;
      for (i = 1; i <= m; i++)
      {  rii = lpx_get_rii(lpx, i);
         spx->A_ptr[i] = pos;
         pos += lpx_get_mat_row(lpx, i,
            &spx->A_ind[pos-1], &spx->A_val[pos-1]);
         for (k = spx->A_ptr[i]; k < pos; k++)
            spx->A_val[k] *= (rii * lpx_get_sjj(lpx, spx->A_ind[k]));
      }
      spx->A_ptr[m+1] = pos;
      insist(pos - 1 == nnz);
      spx->AT_ptr = ucalloc(1+n+1, sizeof(int));
      spx->AT_ind = ucalloc(1+nnz, sizeof(int));
      spx->AT_val = ucalloc(1+nnz, sizeof(gnm_float));
      pos = 1;
      for (j = 1; j <= n; j++)
      {  sjj = lpx_get_sjj(lpx, j);
         spx->AT_ptr[j] = pos;
         pos += lpx_get_mat_col(lpx, j,
            &spx->AT_ind[pos-1], &spx->AT_val[pos-1]);
         for (k = spx->AT_ptr[j]; k < pos; k++)
            spx->AT_val[k] *= (lpx_get_rii(lpx, spx->AT_ind[k]) * sjj);
      }
      spx->AT_ptr[n+1] = pos;
      insist(pos - 1 == nnz);
      if (lpx_is_b_avail(lpx))
         spx->b_stat = LPX_B_VALID;
      else
         spx->b_stat = LPX_B_UNDEF;
      spx->p_stat = LPX_P_UNDEF;
      spx->d_stat = LPX_D_UNDEF;
      spx->tagx = ucalloc(1+m+n, sizeof(int));
      for (i = 1; i <= m; i++)
      {  lpx_get_row_info(lpx, i, &tagx, NULL, NULL);
         spx->tagx[i] = tagx;
      }
      for (j = 1; j <= n; j++)
      {  lpx_get_col_info(lpx, j, &tagx, NULL, NULL);
         spx->tagx[m+j] = tagx;
      }
      spx->posx = ucalloc(1+m+n, sizeof(int));
      spx->indx = ucalloc(1+m+n, sizeof(int));
      spx->inv = lpx_access_inv(lpx);
      if (spx->b_stat == LPX_B_VALID)
      {  for (k = 1; k <= m+n; k++) spx->posx[k] = spx->indx[k] = 0;
         for (i = 1; i <= m; i++)
         {  k = lpx_get_b_info(lpx, i); /* xB[i] = x[k] */
            insist(1 <= k && k <= m+n);
            insist(spx->posx[k] == 0);
            insist(spx->indx[i] == 0);
            spx->posx[k] = i, spx->indx[i] = k;
         }
         j = 0;
         for (k = 1; k <= m+n; k++)
         {  if (spx->posx[k] == 0)
            {  j++;
               spx->posx[k] = m+j, spx->indx[m+j] = k;
            }
         }
         insist(j == n);
         insist(spx->inv != NULL);
         insist(spx->inv->m == m);
         insist(spx->inv->valid);
      }
      spx->bbar = ucalloc(1+m, sizeof(gnm_float));
      spx->pi = ucalloc(1+m, sizeof(gnm_float));
      spx->cbar = ucalloc(1+n, sizeof(gnm_float));
      spx->some = 0;
      spx->msg_lev = lpx_get_int_parm(lpx, LPX_K_MSGLEV);
      spx->dual = lpx_get_int_parm(lpx, LPX_K_DUAL);
      spx->price = lpx_get_int_parm(lpx, LPX_K_PRICE);
      spx->relax = lpx_get_real_parm(lpx, LPX_K_RELAX);
      spx->tol_bnd = lpx_get_real_parm(lpx, LPX_K_TOLBND);
      spx->tol_dj = lpx_get_real_parm(lpx, LPX_K_TOLDJ);
      spx->tol_piv = lpx_get_real_parm(lpx, LPX_K_TOLPIV);
      spx->obj_ll = lpx_get_real_parm(lpx, LPX_K_OBJLL);
      spx->obj_ul = lpx_get_real_parm(lpx, LPX_K_OBJUL);
      spx->it_lim = lpx_get_int_parm(lpx, LPX_K_ITLIM);
      spx->it_cnt = lpx_get_int_parm(lpx, LPX_K_ITCNT);
      spx->tm_lim = lpx_get_real_parm(lpx, LPX_K_TMLIM);
      spx->out_frq = lpx_get_int_parm(lpx, LPX_K_OUTFRQ);
      spx->out_dly = lpx_get_real_parm(lpx, LPX_K_OUTDLY);
      spx->meth = 0;
      ret = spx_simplex(spx);
      if (ret == LPX_E_FAULT) goto skip;
      prim = ucalloc(1+m+n, sizeof(gnm_float));
      dual = ucalloc(1+m+n, sizeof(gnm_float));
      for (k = 1; k <= m+n; k++)
      {  t = spx->posx[k];
         if (t <= m)
         {  prim[k] = spx->bbar[t];
            dual[k] = 0.0;
         }
         else
         {  prim[k] = spx_eval_xn_j(spx, t-m);
            dual[k] = spx->cbar[t-m];
         }
         /* unscale */
         if (k <= m)
         {  prim[k] /= lpx_get_rii(lpx, k);
            dual[k] *= lpx_get_rii(lpx, k);
         }
         else
         {  prim[k] *= lpx_get_sjj(lpx, k-m);
            dual[k] /= lpx_get_sjj(lpx, k-m);
         }
      }
      lpx_put_solution(lpx, spx->p_stat, spx->d_stat,
         &spx->tagx[0], &prim[0], &dual[0],
         &spx->tagx[m], &prim[m], &dual[m]);
      if (spx->p_stat == LPX_P_FEAS && spx->d_stat == LPX_D_NOFEAS)
         lpx_put_ray_info(lpx, spx->some);
      else
         lpx_put_ray_info(lpx, 0);
      ufree(prim);
      ufree(dual);
      lpx_put_lp_basis(lpx, spx->b_stat, &spx->indx[0], spx->inv);
      lpx_set_int_parm(lpx, LPX_K_ITLIM, spx->it_lim);
      lpx_set_int_parm(lpx, LPX_K_ITCNT, spx->it_cnt);
      lpx_set_real_parm(lpx, LPX_K_TMLIM, spx->tm_lim);
skip: ufree(spx->typx);
      ufree(spx->lb);
      ufree(spx->ub);
      ufree(spx->coef);
      ufree(spx->A_ptr);
      ufree(spx->A_ind);
      ufree(spx->A_val);
      ufree(spx->AT_ptr);
      ufree(spx->AT_ind);
      ufree(spx->AT_val);
      ufree(spx->tagx);
      ufree(spx->posx);
      ufree(spx->indx);
      ufree(spx->bbar);
      ufree(spx->pi);
      ufree(spx->cbar);
      insist(spx->meth == 0);
      return ret;
}

#define prefix "lpx_simplex: "

static int simplex2(LPX *orig)
{     /* extended driver which uses LP presolver */
      LPP *lpp;
      LPX *prob;
      int orig_m, orig_n, orig_nnz, k, ret;
      orig_m = lpx_get_num_rows(orig);
      orig_n = lpx_get_num_cols(orig);
      orig_nnz = lpx_get_num_nz(orig);
      if (lpx_get_int_parm(orig, LPX_K_MSGLEV) >= 3)
      {  print(prefix "original LP has %d row%s, %d column%s, %d non-ze"
            "ro%s",
            orig_m, orig_m == 1 ? "" : "s",
            orig_n, orig_n == 1 ? "" : "s",
            orig_nnz, orig_nnz == 1 ? "" : "s");
      }
      /* the problem must have at least one row and one column */
      if (!(orig_m > 0 && orig_n > 0))
      {  if (lpx_get_int_parm(orig, LPX_K_MSGLEV) >= 1)
            print(prefix "problem has no rows/columns");
         return LPX_E_FAULT;
      }
      /* check that each gnm_float-bounded variable has correct lower and
         upper bounds */
      for (k = 1; k <= orig_m + orig_n; k++)
      {  int typx;
         gnm_float lb, ub;
         if (k <= orig_m)
            lpx_get_row_bnds(orig, k, &typx, &lb, &ub);
         else
            lpx_get_col_bnds(orig, k-orig_m, &typx, &lb, &ub);
         if (typx == LPX_DB && lb >= ub)
         {  if (lpx_get_int_parm(orig, LPX_K_MSGLEV) >= 1)
               print(prefix "gnm_float-bounded variable %d has invalid bou"
                  "nds", k);
            return LPX_E_FAULT;
         }
      }
      /* create LP presolver workspace */
      lpp = lpp_create_wksp();
      /* load the original problem into LP presolver workspace */
      lpp_load_orig(lpp, orig);
      /* perform LP presolve analysis */
      ret = lpp_presolve(lpp);
      switch (ret)
      {  case 0:
            /* presolving has been successfully completed */
            break;
         case 1:
            /* the original problem is primal infeasible */
            if (lpx_get_int_parm(orig, LPX_K_MSGLEV) >= 3)
               print("PROBLEM HAS NO PRIMAL FEASIBLE SOLUTION");
            lpp_delete_wksp(lpp);
            return LPX_E_NOPFS;
         case 2:
            /* the original problem is dual infeasible */
            if (lpx_get_int_parm(orig, LPX_K_MSGLEV) >= 3)
               print("PROBLEM HAS NO DUAL FEASIBLE SOLUTION");
            lpp_delete_wksp(lpp);
            return LPX_E_NODFS;
         default:
            insist(ret != ret);
      }
      /* if the resultant problem is empty, it has an empty solution,
         which is optimal */
      if (lpp->row_ptr == NULL || lpp->col_ptr == NULL)
      {  insist(lpp->row_ptr == NULL);
         insist(lpp->col_ptr == NULL);
         if (lpx_get_int_parm(orig, LPX_K_MSGLEV) >= 3)
         {  print("Objective value = %.10g",
               lpp->orig_dir == LPX_MIN ? + lpp->c0 : - lpp->c0);
            print("OPTIMAL SOLUTION FOUND BY LP PRESOLVER");
         }
         /* allocate recovered solution segment */
         lpp_alloc_sol(lpp);
         goto post;
      }
      /* build resultant LP problem object */
      prob = lpp_build_prob(lpp);
      if (lpx_get_int_parm(orig, LPX_K_MSGLEV) >= 3)
      {  int m = lpx_get_num_rows(prob);
         int n = lpx_get_num_cols(prob);
         int nnz = lpx_get_num_nz(prob);
         print(prefix "presolved LP has %d row%s, %d column%s, %d non-z"
            "ero%s", m, m == 1 ? "" : "s", n, n == 1 ? "" : "s",
            nnz, nnz == 1 ? "" : "s");
      }
      /* inherit some control parameters from the original object */
      lpx_set_int_parm(prob, LPX_K_MSGLEV, lpx_get_int_parm(orig,
         LPX_K_MSGLEV));
      lpx_set_int_parm(prob, LPX_K_SCALE, lpx_get_int_parm(orig,
         LPX_K_SCALE));
      lpx_set_int_parm(prob, LPX_K_DUAL, lpx_get_int_parm(orig,
         LPX_K_DUAL));
      lpx_set_int_parm(prob, LPX_K_PRICE, lpx_get_int_parm(orig,
         LPX_K_PRICE));
      lpx_set_real_parm(prob, LPX_K_RELAX, lpx_get_real_parm(orig,
         LPX_K_RELAX));
      lpx_set_real_parm(prob, LPX_K_TOLBND, lpx_get_real_parm(orig,
         LPX_K_TOLBND));
      lpx_set_real_parm(prob, LPX_K_TOLDJ, lpx_get_real_parm(orig,
         LPX_K_TOLDJ));
      lpx_set_real_parm(prob, LPX_K_TOLPIV, lpx_get_real_parm(orig,
         LPX_K_TOLPIV));
      lpx_set_int_parm(prob, LPX_K_ROUND, 0);
      lpx_set_int_parm(prob, LPX_K_ITLIM, lpx_get_int_parm(orig,
         LPX_K_ITLIM));
      lpx_set_int_parm(prob, LPX_K_ITCNT, lpx_get_int_parm(orig,
         LPX_K_ITCNT));
      lpx_set_real_parm(prob, LPX_K_TMLIM, lpx_get_real_parm(orig,
         LPX_K_TMLIM));
      lpx_set_int_parm(prob, LPX_K_OUTFRQ, lpx_get_int_parm(orig,
         LPX_K_OUTFRQ));
      lpx_set_real_parm(prob, LPX_K_OUTDLY, lpx_get_real_parm(orig,
         LPX_K_OUTDLY));
      /* scale the resultant problem */
      lpx_scale_prob(prob);
      /* build advanced initial basis */
      lpx_adv_basis(prob);
      /* try to solve the resultant problem */
      ret = simplex1(prob);
      /* copy back statistics about resources spent by the solver */
      lpx_set_int_parm(orig, LPX_K_ITCNT, lpx_get_int_parm(prob,
         LPX_K_ITCNT));
      lpx_set_int_parm(orig, LPX_K_ITLIM, lpx_get_int_parm(prob,
         LPX_K_ITLIM));
      lpx_set_real_parm(orig, LPX_K_TMLIM, lpx_get_real_parm(prob,
         LPX_K_TMLIM));
      /* check if an optimal solution has been found */
      if (!(ret == LPX_E_OK && lpx_get_status(prob) == LPX_OPT))
      {  if (lpx_get_int_parm(orig, LPX_K_MSGLEV) >= 3)
            print(prefix "cannot recover undefined or non-optimal solut"
               "ion");
         if (ret == LPX_E_OK)
         {  if (lpx_get_prim_stat(prob) == LPX_P_NOFEAS)
               ret = LPX_E_NOPFS;
            else if (lpx_get_dual_stat(prob) == LPX_D_NOFEAS)
               ret = LPX_E_NODFS;
         }
         lpx_delete_prob(prob);
         lpp_delete_wksp(lpp);
         return ret;
      }
      /* allocate recovered solution segment */
      lpp_alloc_sol(lpp);
      /* load basic solution of the resultant problem into LP presolver
         workspace */
      lpp_load_sol(lpp, prob);
      /* the resultant problem object is no longer needed */
      lpx_delete_prob(prob);
post: /* perform LP postsolve processing */
      lpp_postsolve(lpp);
      /* unload recovered basic solution and store it into the original
         problem object */
      lpp_unload_sol(lpp, orig);
      /* delete LP presolver workspace */
      lpp_delete_wksp(lpp);
      /* the original problem has been successfully solved */
      return LPX_E_OK;
}

#undef prefix

int lpx_simplex(LPX *lp)
{     /* driver routine to the simplex method */
      int ret;
      lpx_put_ray_info(lp, 0);
      if (!lpx_get_int_parm(lp, LPX_K_PRESOL))
         ret = simplex1(lp);
      else
         ret = simplex2(lp);
      return ret;
}

/*----------------------------------------------------------------------
-- lpx_check_kkt - check Karush-Kuhn-Tucker conditions.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_check_kkt(LPX *lp, int scaled, LPXKKT *kkt);
--
-- *Description*
--
-- The routine lpx_check_kkt checks Karush-Kuhn-Tucker conditions for
-- the current basic solution specified by an LP problem object, which
-- the parameter lp points to. Both primal and dual components of the
-- basic solution should be defined.
--
-- If the parameter scaled is zero, the conditions are checked for the
-- original, unscaled LP problem. Otherwise, if the parameter scaled is
-- non-zero, the routine checks the conditions for an internally scaled
-- LP problem.
--
-- The parameter kkt is a pointer to the structure LPXKKT, to which the
-- routine stores the results of checking (for details see below).
--
-- The routine performs all computations using only components of the
-- given LP problem and the current basic solution.
--
-- *Background*
--
-- The first condition checked by the routine is:
--
--    xR - A * xS = 0,                                          (KKT.PE)
--
-- where xR is the subvector of auxiliary variables (rows), xS is the
-- subvector of structural variables (columns), A is the constraint
-- matrix. This condition expresses the requirement that all primal
-- variables must satisfy to the system of equality constraints of the
-- original LP problem. In case of exact arithmetic this condition is
-- satisfied for any basic solution; however, if the arithmetic is
-- inexact, it shows how accurate the primal basic solution is, that
-- depends on accuracy of a representation of the basis matrix.
--
-- The second condition checked by the routines is:
--
--    l[k] <= x[k] <= u[k]  for all k = 1, ..., m+n,            (KKT.PB)
--
-- where x[k] is auxiliary or structural variable, l[k] and u[k] are,
-- respectively, lower and upper bounds of the variable x[k] (including
-- cases of infinite bounds). This condition expresses the requirement
-- that all primal variables must satisfy to bound constraints of the
-- original LP problem. Since in case of basic solution all non-basic
-- variables are placed on their bounds, actually the condition (KKT.PB)
-- is checked for basic variables only. If the primal basic solution
-- has sufficient accuracy, this condition shows primal feasibility of
-- the solution.
--
-- The third condition checked by the routine is:
--
--    grad Z = c = (A~)' * pi + d,
--
-- where Z is the objective function, c is the vector of objective
-- coefficients, (A~)' is a matrix transposed to the expanded constraint
-- matrix A~ = (I | -A), pi is a vector of Lagrange multiplers that
-- correspond to equality constraints of the original LP problem, d is
-- a vector of Lagrange multipliers that correspond to bound constraints
-- for all (i.e. auxiliary and structural) variables of the original LP
-- problem. Geometrically the third condition expresses the requirement
-- that the gradient of the objective function must belong to the
-- orthogonal complement of a linear subspace defined by the equality
-- and active bound constraints, i.e. that the gradient must be a linear
-- combination of normals to the constraint planes, where Lagrange
-- multiplers pi and d are coefficients of that linear combination. To
-- eliminate the vector pi the third condition can be rewritten as:
--
--    (  I  )      ( dR )   ( cR )
--    (     ) pi + (    ) = (    ),
--    ( -A' )      ( dS )   ( cS )
--
-- or, equivalently:
--
--          pi + dR = cR,
--
--    -A' * pi + dS = cS.
--
-- Substituting the vector pi from the first equation into the second
-- one we have:
--
--    A' * (dR - cR) + (dS - cS) = 0,                           (KKT.DE)
--
-- where dR is the subvector of reduced costs of auxiliary variables
-- (rows), dS is the subvector of reduced costs of structural variables
-- (columns), cR and cS are, respectively, subvectors of objective
-- coefficients at auxiliary and structural variables, A' is a matrix
-- transposed to the constraint matrix of the original LP problem. In
-- case of exact arithmetic this condition is satisfied for any basic
-- solution; however, if the arithmetic is inexact, it shows how
-- accurate the dual basic solution is, that depends on accuracy of a
-- representation of the basis matrix.
--
-- The last, fourth condition checked by the routine is:
--
--           d[k] = 0,    if x[k] is basic or free non-basic
--
--      0 <= d[k] < +inf, if x[k] is non-basic on its lower
--                        (minimization) or upper (maximization)
--                        bound
--                                                              (KKT.DB)
--    -inf < d[k] <= 0,   if x[k] is non-basic on its upper
--                        (minimization) or lower (maximization)
--                        bound
--
--    -inf < d[k] < +inf, if x[k] is non-basic fixed
--
-- for all k = 1, ..., m+n, where d[k] is a reduced cost (i.e. Lagrange
-- multiplier) of auxiliary or structural variable x[k]. Geometrically
-- this condition expresses the requirement that constraints of the
-- original problem must "hold" the point preventing its movement along
-- the antigradient (in case of minimization) or the gradient (in case
-- of maximization) of the objective function. Since in case of basic
-- solution reduced costs of all basic variables are placed on their
-- (zero) bounds, actually the condition (KKT.DB) is checked for
-- non-basic variables only. If the dual basic solution has sufficient
-- accuracy, this condition shows dual feasibility of the solution.
--
-- Should note that the complete set of Karush-Kuhn-Tucker conditions
-- also includes the fifth, so called complementary slackness condition,
-- which expresses the requirement that at least either a primal
-- variable x[k] or its dual conterpart d[k] must be on its bound for
-- all k = 1, ..., m+n. However, being always satisfied for any basic
-- solution by definition that condition is not checked by the routine.
--
-- To check the first condition (KKT.PE) the routine computes a vector
-- of residuals
--
--    g = xR - A * xS,
--
-- determines components of this vector that correspond to largest
-- absolute and relative errors:
--
--    pe_ae_max = max |g[i]|,
--
--    pe_re_max = max |g[i]| / (1 + |xR[i]|),
--
-- and stores these quantities and corresponding row indices to the
-- structure LPXKKT.
--
-- To check the second condition (KKT.PB) the routine computes a vector
-- of residuals
--
--           ( 0,            if lb[k] <= x[k] <= ub[k]
--           |
--    h[k] = < x[k] - lb[k], if x[k] < lb[k]
--           |
--           ( x[k] - ub[k], if x[k] > ub[k]
--
-- for all k = 1, ..., m+n, determines components of this vector that
-- correspond to largest absolute and relative errors:
--
--    pb_ae_max = max |h[k]|,
--
--    pb_re_max = max |h[k]| / (1 + |x[k]|),
--
-- and stores these quantities and corresponding variable indices to
-- the structure LPXKKT.
--
-- To check the third condition (KKT.DE) the routine computes a vector
-- of residuals
--
--    u = A' * (dR - cR) + (dS - cS),
--
-- determines components of this vector that correspond to largest
-- absolute and relative errors:
--
--    de_ae_max = max |u[j]|,
--
--    de_re_max = max |u[j]| / (1 + |dS[j] - cS[j]|),
--
-- and stores these quantities and corresponding column indices to the
-- structure LPXKKT.
--
-- To check the fourth condition (KKT.DB) the routine computes a vector
-- of residuals
--
--           ( 0,    if d[k] has correct sign
--    v[k] = <
--           ( d[k], if d[k] has wrong sign
--
-- for all k = 1, ..., m+n, determines components of this vector that
-- correspond to largest absolute and relative errors:
--
--    db_ae_max = max |v[k]|,
--
--    db_re_max = max |v[k]| / (1 + |d[k] - c[k]|),
--
-- and stores these quantities and corresponding variable indices to
-- the structure LPXKKT. */

void lpx_check_kkt(LPX *lp, int scaled, LPXKKT *kkt)
{     int m = lpx_get_num_rows(lp);
      int n = lpx_get_num_cols(lp);
#if 0 /* 21/XII-2003 */
      int *typx = lp->typx;
      gnm_float *lb = lp->lb;
      gnm_float *ub = lp->ub;
      gnm_float *rs = lp->rs;
#else
      int typx, tagx;
      gnm_float lb, ub;
#endif
      int dir = lpx_get_obj_dir(lp);
#if 0 /* 21/XII-2003 */
      gnm_float *coef = lp->coef;
#endif
#if 0 /* 22/XII-2003 */
      int *A_ptr = lp->A->ptr;
      int *A_len = lp->A->len;
      int *A_ndx = lp->A->ndx;
      gnm_float *A_val = lp->A->val;
#endif
      int *A_ndx;
      gnm_float *A_val;
#if 0 /* 21/XII-2003 */
      int *tagx = lp->tagx;
      int *posx = lp->posx;
      int *indx = lp->indx;
      gnm_float *bbar = lp->bbar;
      gnm_float *cbar = lp->cbar;
#endif
      int beg, end, i, j, k, t;
      gnm_float cR_i, cS_j, c_k, xR_i, xS_j, x_k, dR_i, dS_j, d_k;
      gnm_float g_i, h_k, u_j, v_k, temp, rii, sjj;
      if (lpx_get_prim_stat(lp) == LPX_P_UNDEF)
         fault("lpx_check_kkt: primal basic solution is undefined");
      if (lpx_get_dual_stat(lp) == LPX_D_UNDEF)
         fault("lpx_check_kkt: dual basic solution is undefined");
      /*--------------------------------------------------------------*/
      /* compute largest absolute and relative errors and corresponding
         row indices for the condition (KKT.PE) */
      kkt->pe_ae_max = 0.0, kkt->pe_ae_row = 0;
      kkt->pe_re_max = 0.0, kkt->pe_re_row = 0;
      A_ndx = ucalloc(1+n, sizeof(int));
      A_val = ucalloc(1+n, sizeof(gnm_float));
      for (i = 1; i <= m; i++)
      {  /* determine xR[i] */
#if 0 /* 21/XII-2003 */
         if (tagx[i] == LPX_BS)
            xR_i = bbar[posx[i]];
         else
            xR_i = spx_eval_xn_j(lp, posx[i] - m);
#else
         lpx_get_row_info(lp, i, NULL, &xR_i, NULL);
         xR_i *= lpx_get_rii(lp, i);
#endif
         /* g[i] := xR[i] */
         g_i = xR_i;
         /* g[i] := g[i] - (i-th row of A) * xS */
         beg = 1;
         end = lpx_get_mat_row(lp, i, A_ndx, A_val);
         for (t = beg; t <= end; t++)
         {  j = m + A_ndx[t]; /* a[i,j] != 0 */
            /* determine xS[j] */
#if 0 /* 21/XII-2003 */
            if (tagx[j] == LPX_BS)
               xS_j = bbar[posx[j]];
            else
               xS_j = spx_eval_xn_j(lp, posx[j] - m);
#else
            lpx_get_col_info(lp, j-m, NULL, &xS_j, NULL);
            xS_j /= lpx_get_sjj(lp, j-m);
#endif
            /* g[i] := g[i] - a[i,j] * xS[j] */
            rii = lpx_get_rii(lp, i);
            sjj = lpx_get_sjj(lp, j-m);
            g_i -= (rii * A_val[t] * sjj) * xS_j;
         }
         /* unscale xR[i] and g[i] (if required) */
         if (!scaled)
         {  rii = lpx_get_rii(lp, i);
            xR_i /= rii, g_i /= rii;
         }
         /* determine absolute error */
         temp = gnm_abs(g_i);
         if (kkt->pe_ae_max < temp)
            kkt->pe_ae_max = temp, kkt->pe_ae_row = i;
         /* determine relative error */
         temp /= (1.0 + gnm_abs(xR_i));
         if (kkt->pe_re_max < temp)
            kkt->pe_re_max = temp, kkt->pe_re_row = i;
      }
      ufree(A_ndx);
      ufree(A_val);
      /* estimate the solution quality */
      if (kkt->pe_re_max <= 1e-9)
         kkt->pe_quality = 'H';
      else if (kkt->pe_re_max <= 1e-6)
         kkt->pe_quality = 'M';
      else if (kkt->pe_re_max <= 1e-3)
         kkt->pe_quality = 'L';
      else
         kkt->pe_quality = '?';
      /*--------------------------------------------------------------*/
      /* compute largest absolute and relative errors and corresponding
         variable indices for the condition (KKT.PB) */
      kkt->pb_ae_max = 0.0, kkt->pb_ae_ind = 0;
      kkt->pb_re_max = 0.0, kkt->pb_re_ind = 0;
      for (k = 1; k <= m+n; k++)
      {  /* determine x[k] */
         if (k <= m)
         {  lpx_get_row_bnds(lp, k, &typx, &lb, &ub);
            rii = lpx_get_rii(lp, k);
            lb *= rii;
            ub *= rii;
            lpx_get_row_info(lp, k, &tagx, &x_k, NULL);
            x_k *= rii;
         }
         else
         {  lpx_get_col_bnds(lp, k-m, &typx, &lb, &ub);
            sjj = lpx_get_sjj(lp, k-m);
            lb /= sjj;
            ub /= sjj;
            lpx_get_col_info(lp, k-m, &tagx, &x_k, NULL);
            x_k /= sjj;
         }
         /* skip non-basic variable */
         if (tagx != LPX_BS) continue;
         /* compute h[k] */
         h_k = 0.0;
         switch (typx)
         {  case LPX_FR:
               break;
            case LPX_LO:
               if (x_k < lb) h_k = x_k - lb;
               break;
            case LPX_UP:
               if (x_k > ub) h_k = x_k - ub;
               break;
            case LPX_DB:
            case LPX_FX:
               if (x_k < lb) h_k = x_k - lb;
               if (x_k > ub) h_k = x_k - ub;
               break;
            default:
               insist(typx != typx);
         }
         /* unscale x[k] and h[k] (if required) */
         if (!scaled)
         {  if (k <= m)
            {  rii = lpx_get_rii(lp, k);
               x_k /= rii, h_k /= rii;
            }
            else
            {  sjj = lpx_get_sjj(lp, k-m);
               x_k *= sjj, h_k *= sjj;
            }
         }
         /* determine absolute error */
         temp = gnm_abs(h_k);
         if (kkt->pb_ae_max < temp)
            kkt->pb_ae_max = temp, kkt->pb_ae_ind = k;
         /* determine relative error */
         temp /= (1.0 + gnm_abs(x_k));
         if (kkt->pb_re_max < temp)
            kkt->pb_re_max = temp, kkt->pb_re_ind = k;
      }
      /* estimate the solution quality */
      if (kkt->pb_re_max <= 1e-9)
         kkt->pb_quality = 'H';
      else if (kkt->pb_re_max <= 1e-6)
         kkt->pb_quality = 'M';
      else if (kkt->pb_re_max <= 1e-3)
         kkt->pb_quality = 'L';
      else
         kkt->pb_quality = '?';
      /*--------------------------------------------------------------*/
      /* compute largest absolute and relative errors and corresponding
         column indices for the condition (KKT.DE) */
      kkt->de_ae_max = 0.0, kkt->de_ae_col = 0;
      kkt->de_re_max = 0.0, kkt->de_re_col = 0;
      A_ndx = ucalloc(1+m, sizeof(int));
      A_val = ucalloc(1+m, sizeof(gnm_float));
      for (j = m+1; j <= m+n; j++)
      {  /* determine cS[j] */
#if 0 /* 21/XII-2003 */
         cS_j = coef[j];
#else
         sjj = lpx_get_sjj(lp, j-m);
         cS_j = lpx_get_obj_coef(lp, j-m) * sjj;
#endif
         /* determine dS[j] */
#if 0 /* 21/XII-2003 */
         if (tagx[j] == LPX_BS)
            dS_j = 0.0;
         else
            dS_j = cbar[posx[j] - m];
#else
         lpx_get_col_info(lp, j-m, NULL, NULL, &dS_j);
         dS_j *= sjj;
#endif
         /* u[j] := dS[j] - cS[j] */
         u_j = dS_j - cS_j;
         /* u[j] := u[j] + (j-th column of A) * (dR - cR) */
         beg = 1;
         end = lpx_get_mat_col(lp, j-m, A_ndx, A_val);
         for (t = beg; t <= end; t++)
         {  i = A_ndx[t]; /* a[i,j] != 0 */
            /* determine cR[i] */
#if 0 /* 21/XII-2003 */
            cR_i = coef[i];
#else
            cR_i = 0.0;
#endif
            /* determine dR[i] */
#if 0 /* 21/XII-2003 */
            if (tagx[i] == LPX_BS)
               dR_i = 0.0;
            else
               dR_i = cbar[posx[i] - m];
#else
            lpx_get_row_info(lp, i, NULL, NULL, &dR_i);
            rii = lpx_get_rii(lp, i);
            dR_i /= rii;
#endif
            /* u[j] := u[j] + a[i,j] * (dR[i] - cR[i]) */
            rii = lpx_get_rii(lp, i);
            sjj = lpx_get_sjj(lp, j-m);
            u_j += (rii * A_val[t] * sjj) * (dR_i - cR_i);
         }
         /* unscale cS[j], dS[j], and u[j] (if required) */
         if (!scaled)
         {  sjj = lpx_get_sjj(lp, j-m);
            cS_j /= sjj, dS_j /= sjj, u_j /= sjj;
         }
         /* determine absolute error */
         temp = gnm_abs(u_j);
         if (kkt->de_ae_max < temp)
            kkt->de_ae_max = temp, kkt->de_ae_col = j - m;
         /* determine relative error */
         temp /= (1.0 + gnm_abs(dS_j - cS_j));
         if (kkt->de_re_max < temp)
            kkt->de_re_max = temp, kkt->de_re_col = j - m;
      }
      ufree(A_ndx);
      ufree(A_val);
      /* estimate the solution quality */
      if (kkt->de_re_max <= 1e-9)
         kkt->de_quality = 'H';
      else if (kkt->de_re_max <= 1e-6)
         kkt->de_quality = 'M';
      else if (kkt->de_re_max <= 1e-3)
         kkt->de_quality = 'L';
      else
         kkt->de_quality = '?';
      /*--------------------------------------------------------------*/
      /* compute largest absolute and relative errors and corresponding
         variable indices for the condition (KKT.DB) */
      kkt->db_ae_max = 0.0, kkt->db_ae_ind = 0;
      kkt->db_re_max = 0.0, kkt->db_re_ind = 0;
      for (k = 1; k <= m+n; k++)
      {  /* determine c[k] */
#if 0 /* 21/XII-2003 */
         c_k = coef[k];
#else
         if (k <= m)
            c_k = 0.0;
         else
         {  sjj = lpx_get_sjj(lp, k-m);
            c_k = lpx_get_obj_coef(lp, k-m) / sjj;
         }
#endif
         /* determine d[k] */
#if 0 /* 21/XII-2003 */
         d_k = cbar[j-m];
#else
         if (k <= m)
         {  lpx_get_row_info(lp, k, &tagx, NULL, &d_k);
            rii = lpx_get_rii(lp, k);
            d_k /= rii;
         }
         else
         {  lpx_get_col_info(lp, k-m, &tagx, NULL, &d_k);
            sjj = lpx_get_sjj(lp, k-m);
            d_k *= sjj;
         }
#endif
         /* skip basic variable */
         if (tagx == LPX_BS) continue;
         /* compute v[k] */
         v_k = 0.0;
         switch (tagx)
         {  case LPX_NL:
               switch (dir)
               {  case LPX_MIN:
                     if (d_k < 0.0) v_k = d_k;
                     break;
                  case LPX_MAX:
                     if (d_k > 0.0) v_k = d_k;
                     break;
                  default:
                     insist(dir != dir);
               }
               break;
            case LPX_NU:
               switch (dir)
               {  case LPX_MIN:
                     if (d_k > 0.0) v_k = d_k;
                     break;
                  case LPX_MAX:
                     if (d_k < 0.0) v_k = d_k;
                     break;
                  default:
                     insist(dir != dir);
               }
               break;
            case LPX_NF:
               v_k = d_k;
               break;
            case LPX_NS:
               break;
            default:
               insist(tagx != tagx);
         }
         /* unscale c[k], d[k], and v[k] (if required) */
         if (!scaled)
         {  if (k <= m)
            {  rii = lpx_get_rii(lp, k);
               c_k *= rii, d_k *= rii, v_k *= rii;
            }
            else
            {  sjj = lpx_get_sjj(lp, k-m);
               c_k /= sjj, d_k /= sjj, v_k /= sjj;
            }
         }
         /* determine absolute error */
         temp = gnm_abs(v_k);
         if (kkt->db_ae_max < temp)
            kkt->db_ae_max = temp, kkt->db_ae_ind = k;
         /* determine relative error */
         temp /= (1.0 + gnm_abs(d_k - c_k));
         if (kkt->db_re_max < temp)
            kkt->db_re_max = temp, kkt->db_re_ind = k;
      }
      /* estimate the solution quality */
      if (kkt->db_re_max <= 1e-9)
         kkt->db_quality = 'H';
      else if (kkt->db_re_max <= 1e-6)
         kkt->db_quality = 'M';
      else if (kkt->db_re_max <= 1e-3)
         kkt->db_quality = 'L';
      else
         kkt->db_quality = '?';
      /* complementary slackness is always satisfied by definition for
         any basic solution, so not checked */
      kkt->cs_ae_max = 0.0, kkt->cs_ae_ind = 0;
      kkt->cs_re_max = 0.0, kkt->cs_re_ind = 0;
      kkt->cs_quality = 'H';
      return;
}

/* eof */
