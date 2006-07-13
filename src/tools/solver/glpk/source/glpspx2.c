/* glpspx2.c (simplex method solver routines) */

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

#include <string.h>
#include "glplib.h"
#include "glpspx.h"

/*----------------------------------------------------------------------
-- spx_warm_up - "warm up" the initial basis.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- int spx_warm_up(SPX *spx);
--
-- *Description*
--
-- The routine spx_warm_up "warms up" the initial basis specified by
-- the array tagx. "Warming up" includes (if necessary) reinverting
-- (factorizing) the initial basis matrix, computing the initial basic
-- solution components (values of basic variables, simplex multipliers,
-- reduced costs of non-basic variables), and determining primal and
-- dual statuses of the initial basic solution.
--
-- *Returns*
--
-- The routine spx_warm_up returns one of the following exit codes:
--
-- LPX_E_OK       the initial basis has been successfully "warmed up".
--
-- LPX_E_EMPTY    the problem has no rows and/or no columns.
--
-- LPX_E_BADB     the initial basis is invalid, because number of basic
--                variables and number of rows are different.
--
-- LPX_E_SING     the initial basis matrix is numerically singular or
--                ill-conditioned.
--
-- Note that additional exit codes may appear in the future versions of
-- this routine. */

int spx_warm_up(SPX *spx)
{     int m = spx->m;
      int n = spx->n;
      int ret;
      /* check if the problem is empty */
      if (!(m > 0 && n > 0))
      {  ret = LPX_E_EMPTY;
         goto done;
      }
      /* reinvert the initial basis matrix (if necessary) */
      if (spx->b_stat != LPX_B_VALID)
      {  int i, j, k;
         /* invalidate the basic solution */
         spx->p_stat = LPX_P_UNDEF;
         spx->d_stat = LPX_D_UNDEF;
         /* build the arrays posx and indx using the array tagx */
         i = j = 0;
         for (k = 1; k <= m+n; k++)
         {  if (spx->tagx[k] == LPX_BS)
            {  /* x[k] = xB[i] */
               i++;
               if (i > m)
               {  /* too many basic variables */
                  ret = LPX_E_BADB;
                  goto done;
               }
               spx->posx[k] = i, spx->indx[i] = k;
            }
            else
            {  /* x[k] = xN[j] */
               j++;
               if (j > n)
               {  /* too many non-basic variables */
                  ret = LPX_E_BADB;
                  goto done;
               }
               spx->posx[k] = m+j, spx->indx[m+j] = k;
            }
         }
         insist(i == m && j == n);
         /* reinvert the initial basis matrix */
         if (spx_invert(spx) != 0)
         {  /* the basis matrix is singular or ill-conditioned */
            ret = LPX_E_SING;
            goto done;
         }
      }
      /* now the basis is valid */
      insist(spx->b_stat == LPX_B_VALID);
      /* compute the initial primal solution */
      spx_eval_bbar(spx);
      if (spx_check_bbar(spx, spx->tol_bnd) == 0.0)
         spx->p_stat = LPX_P_FEAS;
      else
         spx->p_stat = LPX_P_INFEAS;
      /* compute the initial dual solution */
      spx_eval_pi(spx);
      spx_eval_cbar(spx);
      if (spx_check_cbar(spx, spx->tol_dj) == 0.0)
         spx->d_stat = LPX_D_FEAS;
      else
         spx->d_stat = LPX_D_INFEAS;
      /* the basis has been successfully "warmed up" */
      ret = LPX_E_OK;
      /* return to the calling program */
done: return ret;
}

/*----------------------------------------------------------------------
-- spx_prim_opt - find optimal solution (primal simplex).
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- int spx_prim_opt(SPX *spx);
--
-- *Description*
--
-- The routine spx_prim_opt is intended to find optimal solution of an
-- LP problem using the primal simplex method.
--
-- On entry to the routine the initial basis should be "warmed up" and,
-- moreover, the initial basic solution should be primal feasible.
--
-- Structure of this routine can be an example for other variants based
-- on the primal simplex method.
--
-- *Returns*
--
-- The routine spx_prim_opt returns one of the folloiwng exit codes:
--
-- LPX_E_OK       optimal solution found.
--
-- LPX_E_NOFEAS   the problem has no dual feasible solution, therefore
--                its primal solution is unbounded.
--
-- LPX_E_ITLIM    iterations limit exceeded.
--
-- LPX_E_TMLIM    time limit exceeded.
--
-- LPX_E_BADB     the initial basis is not "warmed up".
--
-- LPX_E_INFEAS   the initial basic solution is primal infeasible.
--
-- LPX_E_INSTAB   numerical instability; the current basic solution got
--                primal infeasible due to excessive round-off errors.
--
-- LPX_E_SING     singular basis; the current basis matrix got singular
--                or ill-conditioned due to improper simplex iteration.
--
-- Note that additional exit codes may appear in the future versions of
-- this routine. */

static void prim_opt_dpy(SPX *spx)
{     /* this auxiliary routine displays information about the current
         basic solution */
      int i, def = 0;
      for (i = 1; i <= spx->m; i++)
         if (spx->typx[spx->indx[i]] == LPX_FX) def++;
      print("*%6d:   objval = %17.9e   infeas = %17.9e (%d)",
         spx->it_cnt, spx_eval_obj(spx), spx_check_bbar(spx, 0.0), def);
      return;
}

int spx_prim_opt(SPX *spx)
{     /* find optimal solution (primal simplex) */
      int m = spx->m;
      int n = spx->n;
      int ret;
      gnm_float start = utime(), spent = 0.0;
      /* the initial basis should be "warmed up" */
      if (spx->b_stat != LPX_B_VALID ||
          spx->p_stat == LPX_P_UNDEF || spx->d_stat == LPX_D_UNDEF)
      {  ret = LPX_E_BADB;
         goto done;
      }
      /* the initial basic solution should be primal feasible */
      if (spx->p_stat != LPX_P_FEAS)
      {  ret = LPX_E_INFEAS;
         goto done;
      }
      /* if the initial basic solution is dual feasible, nothing to
         search for */
      if (spx->d_stat == LPX_D_FEAS)
      {  ret = LPX_E_OK;
         goto done;
      }
      /* allocate the working segment */
      insist(spx->meth == 0);
      spx->meth = 'P';
      spx->p = 0;
      spx->p_tag = 0;
      spx->q = 0;
      spx->zeta = ucalloc(1+m, sizeof(gnm_float));
      spx->ap = ucalloc(1+n, sizeof(gnm_float));
      spx->aq = ucalloc(1+m, sizeof(gnm_float));
      spx->gvec = ucalloc(1+n, sizeof(gnm_float));
      spx->dvec = NULL;
      spx->refsp = (spx->price ? ucalloc(1+m+n, sizeof(int)) : NULL);
      spx->count = 0;
      spx->work = ucalloc(1+m+n, sizeof(gnm_float));
      spx->orig_typx = NULL;
      spx->orig_lb = spx->orig_ub = NULL;
      spx->orig_dir = 0;
      spx->orig_coef = NULL;
beg:  /* initialize weights of non-basic variables */
      if (!spx->price)
      {  /* textbook pricing will be used */
         int j;
         for (j = 1; j <= n; j++) spx->gvec[j] = 1.0;
      }
      else
      {  /* steepest edge pricing will be used */
         spx_reset_refsp(spx);
      }
      /* display information about the initial basic solution */
      if (spx->msg_lev >= 2 && spx->it_cnt % spx->out_frq != 0 &&
          spx->out_dly <= spent) prim_opt_dpy(spx);
      /* main loop starts here */
      for (;;)
      {  /* determine the spent amount of time */
         spent = utime() - start;
         /* display information about the current basic solution */
         if (spx->msg_lev >= 2 && spx->it_cnt % spx->out_frq == 0 &&
             spx->out_dly <= spent) prim_opt_dpy(spx);
         /* check if the iterations limit has been exhausted */
         if (spx->it_lim == 0)
         {  ret = LPX_E_ITLIM;
            break;
         }
         /* check if the time limit has been exhausted */
         if (spx->tm_lim >= 0.0 && spx->tm_lim <= spent)
         {  ret = LPX_E_TMLIM;
            break;
         }
         /* choose non-basic variable xN[q] */
         if (spx_prim_chuzc(spx, spx->tol_dj))
         {  /* basic solution components were recomputed; check primal
               feasibility */
            if (spx_check_bbar(spx, spx->tol_bnd) != 0.0)
            {  /* the current solution became primal infeasible due to
                  round-off errors */
               ret = LPX_E_INSTAB;
               break;
            }
         }
         /* if no xN[q] has been chosen, the current basic solution is
            dual feasible and therefore optimal */
         if (spx->q == 0)
         {  ret = LPX_E_OK;
            break;
         }
         /* compute the q-th column of the current simplex table (later
            this column will enter the basis) */
         spx_eval_col(spx, spx->q, spx->aq, 1);
         /* choose basic variable xB[p] */
         if (spx_prim_chuzr(spx, spx->relax * spx->tol_bnd))
         {  /* the basis matrix should be reinverted, because the q-th
               column of the simplex table is unreliable */
            ret = LPX_E_INSTAB;
            break;
         }
         /* if no xB[p] has been chosen, the problem is unbounded (has
            no dual feasible solution) */
         if (spx->p == 0)
         {  spx->some = spx->indx[m + spx->q];
            ret = LPX_E_NOFEAS;
            break;
         }
         /* update values of basic variables */
         spx_update_bbar(spx, NULL);
         if (spx->p > 0)
         {  /* compute the p-th row of the inverse inv(B) */
            spx_eval_rho(spx, spx->p, spx->zeta);
            /* compute the p-th row of the current simplex table */
            spx_eval_row(spx, spx->zeta, spx->ap);
            /* update simplex multipliers */
            spx_update_pi(spx);
            /* update reduced costs of non-basic variables */
            spx_update_cbar(spx, 0);
            /* update weights of non-basic variables */
            if (spx->price) spx_update_gvec(spx);
         }
         /* jump to the adjacent vertex of the LP polyhedron */
         if (spx_change_basis(spx))
         {  /* the basis matrix should be reinverted */
            if (spx_invert(spx) != 0)
            {  /* numerical problems with the basis matrix */
               spx->p_stat = LPX_P_UNDEF;
               spx->d_stat = LPX_D_UNDEF;
               ret = LPX_E_SING;
               goto done;
            }
            /* compute the current basic solution components */
            spx_eval_bbar(spx);
            spx_eval_pi(spx);
            spx_eval_cbar(spx);
            /* check primal feasibility */
            if (spx_check_bbar(spx, spx->tol_bnd) != 0.0)
            {  /* the current solution became primal infeasible due to
                  round-off errors */
               ret = LPX_E_INSTAB;
               break;
            }
         }
#if 0
         /* check accuracy of main solution components after updating
            (for debugging purposes only) */
         {  gnm_float ae_bbar = spx_err_in_bbar(spx);
            gnm_float ae_pi   = spx_err_in_pi(spx);
            gnm_float ae_cbar = spx_err_in_cbar(spx, 0);
            gnm_float ae_gvec = spx->price ? spx_err_in_gvec(spx) : 0.0;
            print("bbar: %g; pi: %g; cbar: %g; gvec: %g",
               ae_bbar, ae_pi, ae_cbar, ae_gvec);
            if (ae_bbar > 1e-7 || ae_pi > 1e-7 || ae_cbar > 1e-7 ||
                ae_gvec > 1e-3) fault("solution accuracy too low");
         }
#endif
      }
      /* compute the final basic solution components */
      spx_eval_bbar(spx);
      spx_eval_pi(spx);
      spx_eval_cbar(spx);
      if (spx_check_bbar(spx, spx->tol_bnd) == 0.0)
         spx->p_stat = LPX_P_FEAS;
      else
         spx->p_stat = LPX_P_INFEAS;
      if (spx_check_cbar(spx, spx->tol_dj) == 0.0)
         spx->d_stat = LPX_D_FEAS;
      else
         spx->d_stat = LPX_D_INFEAS;
      /* display information about the final basic solution */
      if (spx->msg_lev >= 2 && spx->it_cnt % spx->out_frq != 0 &&
          spx->out_dly <= spent) prim_opt_dpy(spx);
      /* correct the preliminary diagnosis */
      switch (ret)
      {  case LPX_E_OK:
            /* assumed LPX_P_FEAS and LPX_D_FEAS */
            if (spx->p_stat != LPX_P_FEAS)
               ret = LPX_E_INSTAB;
            else if (spx->d_stat != LPX_D_FEAS)
            {  /* it seems we need to continue the search */
               goto beg;
            }
            break;
         case LPX_E_ITLIM:
         case LPX_E_TMLIM:
            /* assumed LPX_P_FEAS and LPX_D_INFEAS */
            if (spx->p_stat != LPX_P_FEAS)
               ret = LPX_E_INSTAB;
            else if (spx->d_stat == LPX_D_FEAS)
               ret = LPX_E_OK;
            break;
         case LPX_E_NOFEAS:
            /* assumed LPX_P_FEAS and LPX_D_INFEAS */
            if (spx->p_stat != LPX_P_FEAS)
               ret = LPX_E_INSTAB;
            else if (spx->d_stat == LPX_D_FEAS)
               ret = LPX_E_OK;
            else
               spx->d_stat = LPX_D_NOFEAS;
            break;
         case LPX_E_INSTAB:
            /* assumed LPX_P_INFEAS */
            if (spx->p_stat == LPX_P_FEAS)
            {  if (spx->d_stat == LPX_D_FEAS)
                  ret = LPX_E_OK;
               else
               {  /* it seems we need to continue the search */
                  goto beg;
               }
            }
            break;
         default:
            insist(ret != ret);
      }
done: /* deallocate the working segment */
      if (spx->meth != 0)
      {  spx->meth = 0;
         ufree(spx->zeta);
         ufree(spx->ap);
         ufree(spx->aq);
         ufree(spx->gvec);
         if (spx->price) ufree(spx->refsp);
         ufree(spx->work);
      }
      /* determine the spent amount of time */
      spent = utime() - start;
      /* decrease the time limit by the spent amount */
      if (spx->tm_lim >= 0.0)
      {  spx->tm_lim -= spent;
         if (spx->tm_lim < 0.0) spx->tm_lim = 0.0;
      }
      /* return to the calling program */
      return ret;
}

/*----------------------------------------------------------------------
-- spx_prim_feas - find primal feasible solution (primal simplex).
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- int spx_prim_feas(SPX *spx);
--
-- *Description*
--
-- The routine spx_prim_feas tries to find primal feasible solution of
-- an LP problem using the method of implicit artificial variables that
-- is based on the primal simplex method (see the comments below).
--
-- On entry to the routine the initial basis should be "warmed up".
--
-- *Returns*
--
-- The routine spx_prim_feas returns one of the following exit codes:
--
-- LPX_E_OK       primal feasible solution found.
--
-- LPX_E_NOFEAS   the problem has no primal feasible solution.
--
-- LPX_E_ITLIM    iterations limit exceeded.
--
-- LPX_E_TMLIM    time limit exceeded.
--
-- LPX_E_BADB     the initial basis is not "warmed up".
--
-- LPX_E_INSTAB   numerical instability; the current artificial basic
--                solution (internally constructed by the routine) got
--                primal infeasible due to excessive round-off errors.
--
-- LPX_E_SING     singular basis; the current basis matrix got singular
--                or ill-conditioned due to improper simplex iteration.
--
-- Note that additional exit codes may appear in the future versions of
-- this routine. */

static gnm_float orig_objval(SPX *spx)
{     /* this auxliary routine computes value of the objective function
         for the original LP problem */
      gnm_float objval;
      void *t;
      t = spx->typx, spx->typx = spx->orig_typx, spx->orig_typx = t;
      t = spx->lb, spx->lb = spx->orig_lb, spx->orig_lb = t;
      t = spx->ub, spx->ub = spx->orig_ub, spx->orig_ub = t;
      t = spx->coef, spx->coef = spx->orig_coef, spx->orig_coef = t;
      objval = spx_eval_obj(spx);
      t = spx->typx, spx->typx = spx->orig_typx, spx->orig_typx = t;
      t = spx->lb, spx->lb = spx->orig_lb, spx->orig_lb = t;
      t = spx->ub, spx->ub = spx->orig_ub, spx->orig_ub = t;
      t = spx->coef, spx->coef = spx->orig_coef, spx->orig_coef = t;
      return objval;
}

static gnm_float orig_infsum(SPX *spx, gnm_float tol)
{     /* this auxiliary routine computes the sum of infeasibilities for
         the original LP problem */
      gnm_float infsum;
      void *t;
      t = spx->typx, spx->typx = spx->orig_typx, spx->orig_typx = t;
      t = spx->lb, spx->lb = spx->orig_lb, spx->orig_lb = t;
      t = spx->ub, spx->ub = spx->orig_ub, spx->orig_ub = t;
      t = spx->coef, spx->coef = spx->orig_coef, spx->orig_coef = t;
      infsum = spx_check_bbar(spx, tol);
      t = spx->typx, spx->typx = spx->orig_typx, spx->orig_typx = t;
      t = spx->lb, spx->lb = spx->orig_lb, spx->orig_lb = t;
      t = spx->ub, spx->ub = spx->orig_ub, spx->orig_ub = t;
      t = spx->coef, spx->coef = spx->orig_coef, spx->orig_coef = t;
      return infsum;
}

static void prim_feas_dpy(SPX *spx, gnm_float sum_0)
{     /* this auxiliary routine displays information about the current
         basic solution */
      int i, def = 0;
      for (i = 1; i <= spx->m; i++)
         if (spx->typx[spx->indx[i]] == LPX_FX) def++;
      print(" %6d:   objval = %17.9e   infeas = %17.9e (%d)",
         spx->it_cnt, orig_objval(spx), orig_infsum(spx, 0.0) / sum_0,
         def);
      return;
}

int spx_prim_feas(SPX *spx)
{     /* find primal feasible solution (primal simplex) */
      int m = spx->m;
      int n = spx->n;
      int i, k, ret;
      gnm_float sum_0;
      gnm_float start = utime(), spent = 0.0;
      /* the initial basis should be "warmed up" */
      if (spx->b_stat != LPX_B_VALID ||
          spx->p_stat == LPX_P_UNDEF || spx->d_stat == LPX_D_UNDEF)
      {  ret = LPX_E_BADB;
         goto done;
      }
      /* if the initial basic solution is primal feasible, nothing to
         search for */
      if (spx->p_stat == LPX_P_FEAS)
      {  ret = LPX_E_OK;
         goto done;
      }
      /* allocate the working segment */
      insist(spx->meth == 0);
      spx->meth = 'P';
      spx->p = 0;
      spx->p_tag = 0;
      spx->q = 0;
      spx->zeta = ucalloc(1+m, sizeof(gnm_float));
      spx->ap = ucalloc(1+n, sizeof(gnm_float));
      spx->aq = ucalloc(1+m, sizeof(gnm_float));
      spx->gvec = ucalloc(1+n, sizeof(gnm_float));
      spx->dvec = NULL;
      spx->refsp = (spx->price ? ucalloc(1+m+n, sizeof(int)) : NULL);
      spx->count = 0;
      spx->work = ucalloc(1+m+n, sizeof(gnm_float));
      spx->orig_typx = ucalloc(1+m+n, sizeof(int));
      spx->orig_lb = ucalloc(1+m+n, sizeof(gnm_float));
      spx->orig_ub = ucalloc(1+m+n, sizeof(gnm_float));
      spx->orig_dir = 0;
      spx->orig_coef = ucalloc(1+m+n, sizeof(gnm_float));
      /* save components of the original LP problem, which are changed
         by the routine */
      memcpy(spx->orig_typx, spx->typx, (1+m+n) * sizeof(int));
      memcpy(spx->orig_lb, spx->lb, (1+m+n) * sizeof(gnm_float));
      memcpy(spx->orig_ub, spx->ub, (1+m+n) * sizeof(gnm_float));
      spx->orig_dir = spx->dir;
      memcpy(spx->orig_coef, spx->coef, (1+m+n) * sizeof(gnm_float));
      /* build an artificial basic solution, which is primal feasible,
         and also build an auxiliary objective function to minimize the
         sum of infeasibilities (residuals) for the original problem */
      spx->dir = LPX_MIN;
      for (k = 0; k <= m+n; k++) spx->coef[k] = 0.0;
      for (i = 1; i <= m; i++)
      {  int typx_k;
         gnm_float lb_k, ub_k, bbar_i;
         gnm_float eps = 0.10 * spx->tol_bnd;
         k = spx->indx[i]; /* x[k] = xB[i] */
         typx_k = spx->orig_typx[k];
         lb_k = spx->orig_lb[k];
         ub_k = spx->orig_ub[k];
         bbar_i = spx->bbar[i];
         if (typx_k == LPX_LO || typx_k == LPX_DB || typx_k == LPX_FX)
         {  /* in the original problem x[k] has an lower bound */
            if (bbar_i < lb_k - eps)
            {  /* and violates it */
               spx->typx[k] = LPX_UP;
               spx->lb[k] = 0.0;
               spx->ub[k] = lb_k;
               spx->coef[k] = -1.0; /* x[k] should be increased */
            }
         }
         if (typx_k == LPX_UP || typx_k == LPX_DB || typx_k == LPX_FX)
         {  /* in the original problem x[k] has an upper bound */
            if (bbar_i > ub_k + eps)
            {  /* and violates it */
               spx->typx[k] = LPX_LO;
               spx->lb[k] = ub_k;
               spx->ub[k] = 0.0;
               spx->coef[k] = +1.0; /* x[k] should be decreased */
            }
         }
      }
      /* now the initial basic solution should be primal feasible due
         to changes of bounds of some basic variables, which turned to
         implicit artifical variables */
      insist(spx_check_bbar(spx, spx->tol_bnd) == 0.0);
      /* compute the initial sum of infeasibilities for the original
         problem */
      sum_0 = orig_infsum(spx, 0.0);
      /* it can't be zero, because the initial basic solution is primal
         infeasible */
      insist(sum_0 != 0.0);
      /* compute simplex multipliers and reduced costs of non-basic
         variables once again (because the objective function has been
         changed) */
      spx_eval_pi(spx);
      spx_eval_cbar(spx);
      /* initialize weights of non-basic variables */
      if (!spx->price)
      {  /* textbook pricing will be used */
         int j;
         for (j = 1; j <= n; j++) spx->gvec[j] = 1.0;
      }
      else
      {  /* steepest edge pricing will be used */
         spx_reset_refsp(spx);
      }
      /* display information about the initial basic solution */
      if (spx->msg_lev >= 2 && spx->it_cnt % spx->out_frq != 0 &&
          spx->out_dly <= spent) prim_feas_dpy(spx, sum_0);
      /* main loop starts here */
      for (;;)
      {  /* determine the spent amount of time */
         spent = utime() - start;
         /* display information about the current basic solution */
         if (spx->msg_lev >= 2 && spx->it_cnt % spx->out_frq == 0 &&
             spx->out_dly <= spent) prim_feas_dpy(spx, sum_0);
         /* we needn't to wait until all artificial variables leave the
            basis */
         if (orig_infsum(spx, spx->tol_bnd) == 0.0)
         {  /* the sum of infeasibilities is zero, therefore the current
               solution is primal feasible for the original problem */
            ret = LPX_E_OK;
            break;
         }
         /* check if the iterations limit has been exhausted */
         if (spx->it_lim == 0)
         {  ret = LPX_E_ITLIM;
            break;
         }
         /* check if the time limit has been exhausted */
         if (spx->tm_lim >= 0.0 && spx->tm_lim <= spent)
         {  ret = LPX_E_TMLIM;
            break;
         }
         /* choose non-basic variable xN[q] */
         if (spx_prim_chuzc(spx, spx->tol_dj))
         {  /* basic solution components were recomputed; check primal
               feasibility (of the artificial solution) */
            if (spx_check_bbar(spx, spx->tol_bnd) != 0.0)
            {  /* the current solution became primal infeasible due to
                  round-off errors */
               ret = LPX_E_INSTAB;
               break;
            }
         }
         /* if no xN[q] has been chosen, the sum of infeasibilities is
            minimal but non-zero; therefore the original problem has no
            primal feasible solution */
         if (spx->q == 0)
         {  ret = LPX_E_NOFEAS;
            break;
         }
         /* compute the q-th column of the current simplex table (later
            this column will enter the basis) */
         spx_eval_col(spx, spx->q, spx->aq, 1);
         /* choose basic variable xB[p] */
         if (spx_prim_chuzr(spx, spx->relax * spx->tol_bnd))
         {  /* the basis matrix should be reinverted, because the q-th
               column of the simplex table is unreliable */
            ret = LPX_E_INSTAB;
            break;
         }
         /* the sum of infeasibilities can't be negative, therefore the
            modified problem can't have unbounded solution */
         insist(spx->p != 0);
         /* update values of basic variables */
         spx_update_bbar(spx, NULL);
         if (spx->p > 0)
         {  /* compute the p-th row of the inverse inv(B) */
            spx_eval_rho(spx, spx->p, spx->zeta);
            /* compute the p-th row of the current simplex table */
            spx_eval_row(spx, spx->zeta, spx->ap);
            /* update simplex multipliers */
            spx_update_pi(spx);
            /* update reduced costs of non-basic variables */
            spx_update_cbar(spx, 0);
            /* update weights of non-basic variables */
            if (spx->price) spx_update_gvec(spx);
         }
         /* xB[p] is leaving the basis; if it is implicit artificial
            variable, the corresponding residual vanishes; therefore
            bounds of this variable should be restored to the original
            ones */
         if (spx->p > 0)
         {  k = spx->indx[spx->p]; /* x[k] = xB[p] */
            if (spx->typx[k] != spx->orig_typx[k])
            {  /* x[k] is implicit artificial variable */
               spx->typx[k] = spx->orig_typx[k];
               spx->lb[k] = spx->orig_lb[k];
               spx->ub[k] = spx->orig_ub[k];
               insist(spx->p_tag == LPX_NL || spx->p_tag == LPX_NU);
               spx->p_tag = (spx->p_tag == LPX_NL ? LPX_NU : LPX_NL);
               if (spx->typx[k] == LPX_FX) spx->p_tag = LPX_NS;
               /* nullify the objective coefficient at x[k] */
               spx->coef[k] = 0.0;
               /* since coef[k] has been changed, we need to compute
                  new reduced cost of x[k], which it will have in the
                  adjacent basis */
               /* the formula d[j] = cN[j] - pi' * N[j] is used (note
                  that the vector pi is not changed, because it depends
                  on objective coefficients at basic variables, but in
                  the adjacent basis, for which the vector pi has been
                  just recomputed, x[k] is non-basic) */
               if (k <= m)
               {  /* x[k] is auxiliary variable */
                  spx->cbar[spx->q] = - spx->pi[k];
               }
               else
               {  /* x[k] is structural variable */
                  int ptr = spx->AT_ptr[k-m];
                  int end = spx->AT_ptr[k-m+1];
                  gnm_float d = 0.0;
                  for (ptr = ptr; ptr < end; ptr++)
                     d += spx->pi[spx->AT_ind[ptr]] * spx->AT_val[ptr];
                  spx->cbar[spx->q] = d;
               }
            }
         }
         /* jump to the adjacent vertex of the LP polyhedron */
         if (spx_change_basis(spx))
         {  /* the basis matrix should be reinverted */
            if (spx_invert(spx))
            {  /* numerical problems with the basis matrix */
               ret = LPX_E_SING;
               break;
            }
            /* compute the current basic solution components */
            spx_eval_bbar(spx);
            spx_eval_pi(spx);
            spx_eval_cbar(spx);
            /* check primal feasibility */
            if (spx_check_bbar(spx, spx->tol_bnd) != 0.0)
            {  /* the current solution became primal infeasible due to
                  excessive round-off errors */
               ret = LPX_E_INSTAB;
               break;
            }
         }
#if 0
         /* check accuracy of main solution components after updating
            (for debugging purposes only) */
         {  gnm_float ae_bbar = spx_err_in_bbar(spx);
            gnm_float ae_pi   = spx_err_in_pi(spx);
            gnm_float ae_cbar = spx_err_in_cbar(spx, 0);
            gnm_float ae_gvec = spx->price ? spx_err_in_gvec(spx) : 0.0;
            print("bbar: %g; pi: %g; cbar: %g; gvec: %g",
               ae_bbar, ae_pi, ae_cbar, ae_gvec);
            if (ae_bbar > 1e-7 || ae_pi > 1e-7 || ae_cbar > 1e-7 ||
                ae_gvec > 1e-3) fault("solution accuracy too low");
         }
#endif
      }
      /* restore components of the original problem, which were changed
         by the routine */
      memcpy(spx->typx, spx->orig_typx, (1+m+n) * sizeof(int));
      memcpy(spx->lb, spx->orig_lb, (1+m+n) * sizeof(gnm_float));
      memcpy(spx->ub, spx->orig_ub, (1+m+n) * sizeof(gnm_float));
      spx->dir = spx->orig_dir;
      memcpy(spx->coef, spx->orig_coef, (1+m+n) * sizeof(gnm_float));
      /* if there are numerical problems with the basis matrix, the
         latter must be repaired; mark the basic solution as undefined
         and exit immediately */
      if (ret == LPX_E_SING)
      {  spx->p_stat = LPX_P_UNDEF;
         spx->d_stat = LPX_D_UNDEF;
         goto done;
      }
      /* compute the final basic solution components */
      spx_eval_bbar(spx);
      spx_eval_pi(spx);
      spx_eval_cbar(spx);
      if (spx_check_bbar(spx, spx->tol_bnd) == 0.0)
         spx->p_stat = LPX_P_FEAS;
      else
         spx->p_stat = LPX_P_INFEAS;
      if (spx_check_cbar(spx, spx->tol_dj) == 0.0)
         spx->d_stat = LPX_D_FEAS;
      else
         spx->d_stat = LPX_D_INFEAS;
      /* display information about the final basic solution */
      if (spx->msg_lev >= 2 && spx->it_cnt % spx->out_frq != 0 &&
          spx->out_dly <= spent) prim_feas_dpy(spx, sum_0);
      /* correct the preliminary diagnosis */
      switch (ret)
      {  case LPX_E_OK:
            /* assumed LPX_P_FEAS */
            if (spx->p_stat != LPX_P_FEAS)
               ret = LPX_E_INSTAB;
            break;
         case LPX_E_ITLIM:
         case LPX_E_TMLIM:
            /* assumed LPX_P_INFEAS */
            if (spx->p_stat == LPX_P_FEAS)
               ret = LPX_E_OK;
            break;
         case LPX_E_NOFEAS:
            /* assumed LPX_P_INFEAS */
            if (spx->p_stat == LPX_P_FEAS)
               ret = LPX_E_OK;
            else
               spx->p_stat = LPX_P_NOFEAS;
            break;
         case LPX_E_INSTAB:
            /* assumed LPX_P_INFEAS */
            if (spx->p_stat == LPX_P_FEAS)
               ret = LPX_E_OK;
            break;
         default:
            insist(ret != ret);
      }
done: /* deallocate the working segment */
      if (spx->meth != 0)
      {  spx->meth = 0;
         ufree(spx->zeta);
         ufree(spx->ap);
         ufree(spx->aq);
         ufree(spx->gvec);
         if (spx->price) ufree(spx->refsp);
         ufree(spx->work);
         ufree(spx->orig_typx);
         ufree(spx->orig_lb);
         ufree(spx->orig_ub);
         ufree(spx->orig_coef);
      }
      /* determine the spent amount of time */
      spent = utime() - start;
      /* decrease the time limit by the spent amount */
      if (spx->tm_lim >= 0.0)
      {  spx->tm_lim -= spent;
         if (spx->tm_lim < 0.0) spx->tm_lim = 0.0;
      }
      /* return to the calling program */
      return ret;
}

/*----------------------------------------------------------------------
-- spx_dual_opt - find optimal solution (dual simplex).
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- int spx_dual_opt(SPX *spx);
--
-- *Description*
--
-- The routine spx_dual_opt is intended to find optimal solution of an
-- LP problem using the dual simplex method.
--
-- On entry to the routine the initial basis should be "warmed up" and,
-- moreover, the initial basic solution should be dual feasible.
--
-- Structure of this routine can be an example for other variants based
-- on the dual simplex method.
--
-- *Returns*
--
-- The routine spx_dual_opt returns one of the following exit codes:
--
-- LPX_E_OK       optimal solution found.
--
-- LPX_E_NOFEAS   the problem has no primal feasible solution.
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
-- LPX_E_BADB     the initial basis is not "warmed up".
--
-- LPX_E_INFEAS   the initial basic solution is dual infeasible.
--
-- LPX_E_INSTAB   numerical instability; the current basic solution got
--                primal infeasible due to excessive round-off errors.
--
-- LPX_E_SING     singular basis; the current basis matrix got singular
--                or ill-conditioned due to improper simplex iteration.
--
-- Note that additional exit codes may appear in the future versions of
-- this routine. */

static void dual_opt_dpy(SPX *spx)
{     /* this auxiliary routine displays information about the current
         basic solution */
      int i, def = 0;
      for (i = 1; i <= spx->m; i++)
         if (spx->typx[spx->indx[i]] == LPX_FX) def++;
      print("|%6d:   objval = %17.9e   infeas = %17.9e (%d)",
         spx->it_cnt, spx_eval_obj(spx), spx_check_bbar(spx, 0.0), def);
      return;
}

int spx_dual_opt(SPX *spx)
{     /* find optimal solution (dual simplex) */
      int m = spx->m;
      int n = spx->n;
      int ret;
      gnm_float start = utime(), spent = 0.0, obj;
      /* the initial basis should be "warmed up" */
      if (spx->b_stat != LPX_B_VALID ||
          spx->p_stat == LPX_P_UNDEF || spx->d_stat == LPX_D_UNDEF)
      {  ret = LPX_E_BADB;
         goto done;
      }
      /* the initial basic solution should be dual feasible */
      if (spx->d_stat != LPX_D_FEAS)
      {  ret = LPX_E_INFEAS;
         goto done;
      }
      /* if the initial basic solution is primal feasible, nothing to
         search for */
      if (spx->p_stat == LPX_P_FEAS)
      {  ret = LPX_E_OK;
         goto done;
      }
      /* allocate the working segment */
      insist(spx->meth == 0);
      spx->meth = 'D';
      spx->p = 0;
      spx->p_tag = 0;
      spx->q = 0;
      spx->zeta = ucalloc(1+m, sizeof(gnm_float));
      spx->ap = ucalloc(1+n, sizeof(gnm_float));
      spx->aq = ucalloc(1+m, sizeof(gnm_float));
      spx->gvec = NULL;
      spx->dvec = ucalloc(1+m, sizeof(gnm_float));
      spx->refsp = (spx->price ? ucalloc(1+m+n, sizeof(int)) : NULL);
      spx->count = 0;
      spx->work = ucalloc(1+m+n, sizeof(gnm_float));
      spx->orig_typx = NULL;
      spx->orig_lb = spx->orig_ub = NULL;
      spx->orig_dir = 0;
      spx->orig_coef = NULL;
beg:  /* compute initial value of the objective function */
      obj = spx_eval_obj(spx);
      /* initialize weights of basic variables */
      if (!spx->price)
      {  /* textbook pricing will be used */
         int i;
         for (i = 1; i <= m; i++) spx->dvec[i] = 1.0;
      }
      else
      {  /* steepest edge pricing will be used */
         spx_reset_refsp(spx);
      }
      /* display information about the initial basic solution */
      if (spx->msg_lev >= 2 && spx->it_cnt % spx->out_frq != 0 &&
          spx->out_dly <= spent) dual_opt_dpy(spx);
      /* main loop starts here */
      for (;;)
      {  /* determine the spent amount of time */
         spent = utime() - start;
         /* display information about the current basic solution */
         if (spx->msg_lev >= 2 && spx->it_cnt % spx->out_frq == 0 &&
             spx->out_dly <= spent) dual_opt_dpy(spx);
         /* if the objective function should be minimized, check if it
            has reached its upper bound */
         if (spx->dir == LPX_MIN && obj >= spx->obj_ul)
         {  ret = LPX_E_OBJUL;
            break;
         }
         /* if the objective function should be maximized, check if it
            has reached its lower bound */
         if (spx->dir == LPX_MAX && obj <= spx->obj_ll)
         {  ret = LPX_E_OBJLL;
            break;
         }
         /* check if the iterations limit has been exhausted */
         if (spx->it_lim == 0)
         {  ret = LPX_E_ITLIM;
            break;
         }
         /* check if the time limit has been exhausted */
         if (spx->tm_lim >= 0.0 && spx->tm_lim <= spent)
         {  ret = LPX_E_TMLIM;
            break;
         }
         /* choose basic variable */
         spx_dual_chuzr(spx, spx->tol_bnd);
         /* if no xB[p] has been chosen, the current basic solution is
            primal feasible and therefore optimal */
         if (spx->p == 0)
         {  ret = LPX_E_OK;
            break;
         }
         /* compute the p-th row of the inverse inv(B) */
         spx_eval_rho(spx, spx->p, spx->zeta);
         /* compute the p-th row of the current simplex table */
         spx_eval_row(spx, spx->zeta, spx->ap);
         /* choose non-basic variable xN[q] */
         if (spx_dual_chuzc(spx, spx->relax * spx->tol_dj))
         {  /* the basis matrix should be reinverted, because the p-th
               row of the simplex table is unreliable */
            ret = LPX_E_INSTAB;
            break;
         }
         /* if no xN[q] has been chosen, there is no primal feasible
            solution (the dual problem has unbounded solution) */
         if (spx->q == 0)
         {  ret = LPX_E_NOFEAS;
            break;
         }
         /* compute the q-th column of the current simplex table (later
            this column will enter the basis) */
         spx_eval_col(spx, spx->q, spx->aq, 1);
         /* update values of basic variables and value of the objective
            function */
         spx_update_bbar(spx, &obj);
         /* update simplex multipliers */
         spx_update_pi(spx);
         /* update reduced costs of non-basic variables */
         spx_update_cbar(spx, 0);
         /* update weights of basic variables */
         if (spx->price) spx_update_dvec(spx);
         /* if xB[p] is fixed variable, adjust its non-basic tag */
         if (spx->typx[spx->indx[spx->p]] == LPX_FX)
            spx->p_tag = LPX_NS;
         /* jump to the adjacent vertex of the LP polyhedron */
         if (spx_change_basis(spx))
         {  /* the basis matrix should be reinverted */
            if (spx_invert(spx) != 0)
            {  /* numerical problems with the basis matrix */
               spx->p_stat = LPX_P_UNDEF;
               spx->d_stat = LPX_D_UNDEF;
               ret = LPX_E_SING;
               goto done;
            }
            /* compute the current basic solution components */
            spx_eval_bbar(spx);
            obj = spx_eval_obj(spx);
            spx_eval_pi(spx);
            spx_eval_cbar(spx);
            /* check dual feasibility */
            if (spx_check_cbar(spx, spx->tol_dj) != 0.0)
            {  /* the current solution became dual infeasible due to
                  round-off errors */
               ret = LPX_E_INSTAB;
               break;
            }
         }
#if 0
         /* check accuracy of main solution components after updating
            (for debugging purposes only) */
         {  gnm_float ae_bbar = spx_err_in_bbar(spx);
            gnm_float ae_pi   = spx_err_in_pi(spx);
            gnm_float ae_cbar = spx_err_in_cbar(spx, 0);
            gnm_float ae_dvec = spx->price ? spx_err_in_dvec(spx) : 0.0;
            print("bbar: %g; pi: %g; cbar: %g; dvec: %g",
               ae_bbar, ae_pi, ae_cbar, ae_dvec);
            if (ae_bbar > 1e-9 || ae_pi > 1e-9 || ae_cbar > 1e-9 ||
                ae_dvec > 1e-3)
               insist("solution accuracy too low" == NULL);
         }
#endif
      }
      /* compute the final basic solution components */
      spx_eval_bbar(spx);
      obj = spx_eval_obj(spx);
      spx_eval_pi(spx);
      spx_eval_cbar(spx);
      if (spx_check_bbar(spx, spx->tol_bnd) == 0.0)
         spx->p_stat = LPX_P_FEAS;
      else
         spx->p_stat = LPX_P_INFEAS;
      if (spx_check_cbar(spx, spx->tol_dj) == 0.0)
         spx->d_stat = LPX_D_FEAS;
      else
         spx->d_stat = LPX_D_INFEAS;
      /* display information about the final basic solution */
      if (spx->msg_lev >= 2 && spx->it_cnt % spx->out_frq != 0 &&
          spx->out_dly <= spent) dual_opt_dpy(spx);
      /* correct the preliminary diagnosis */
      switch (ret)
      {  case LPX_E_OK:
            /* assumed LPX_P_FEAS and LPX_D_FEAS */
            if (spx->d_stat != LPX_D_FEAS)
               ret = LPX_E_INSTAB;
            else if (spx->p_stat != LPX_P_FEAS)
            {  /* it seems we need to continue the search */
               goto beg;
            }
            break;
         case LPX_E_OBJLL:
         case LPX_E_OBJUL:
            /* assumed LPX_P_INFEAS and LPX_D_FEAS */
            if (spx->d_stat != LPX_D_FEAS)
               ret = LPX_E_INSTAB;
            else if (spx->p_stat == LPX_P_FEAS)
               ret = LPX_E_OK;
            else if (spx->dir == LPX_MIN && obj < spx->obj_ul ||
                     spx->dir == LPX_MAX && obj > spx->obj_ll)
            {  /* it seems we need to continue the search */
               goto beg;
            }
            break;
         case LPX_E_ITLIM:
         case LPX_E_TMLIM:
            /* assumed LPX_P_INFEAS and LPX_D_FEAS */
            if (spx->d_stat != LPX_D_FEAS)
               ret = LPX_E_INSTAB;
            else if (spx->p_stat == LPX_P_FEAS)
               ret = LPX_E_OK;
            break;
         case LPX_E_NOFEAS:
            /* assumed LPX_P_INFEAS and LPX_D_FEAS */
            if (spx->d_stat != LPX_D_FEAS)
               ret = LPX_E_INSTAB;
            else if (spx->p_stat == LPX_P_FEAS)
               ret = LPX_E_OK;
            else
               spx->p_stat = LPX_P_NOFEAS;
            break;
         case LPX_E_INSTAB:
            /* assumed LPX_D_INFEAS */
            if (spx->d_stat == LPX_D_FEAS)
            {  if (spx->p_stat == LPX_P_FEAS)
                  ret = LPX_E_OK;
               else
               {  /* it seems we need to continue the search */
                  goto beg;
               }
            }
            break;
         default:
            insist(ret != ret);
      }
done: /* deallocate the working segment */
      if (spx->meth != 0)
      {  spx->meth = 0;
         ufree(spx->zeta);
         ufree(spx->ap);
         ufree(spx->aq);
         ufree(spx->dvec);
         if (spx->price) ufree(spx->refsp);
         ufree(spx->work);
      }
      /* determine the spent amount of time */
      spent = utime() - start;
      /* decrease the time limit by the spent amount */
      if (spx->tm_lim >= 0.0)
      {  spx->tm_lim -= spent;
         if (spx->tm_lim < 0.0) spx->tm_lim = 0.0;
      }
      /* return to the calling program */
      return ret;
}

/*----------------------------------------------------------------------
-- spx_simplex - base driver to the simplex method.
--
-- *Synopsis*
--
-- #include "glpspx.h"
-- int spx_simplex(SPX *spx);
--
-- *Description*
--
-- The routine spx_simplex is a base driver to the simplex method.
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
-- The routine spx_simplex returns one of the following exit codes:
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
-- Note that additional exit codes may appear in the future versions of
-- this routine. */

#define prefix "spx_simplex: "

int spx_simplex(SPX *spx)
{     int ret;
      /* check that each gnm_float-bounded variable has correct lower and
         upper bounds */
      {  int k;
         for (k = 1; k <= spx->m + spx->n; k++)
         {  if (spx->typx[k] == LPX_DB && spx->lb[k] >= spx->ub[k])
            {  if (spx->msg_lev >= 1)
                  print(prefix "gnm_float-bounded variable %d has invalid "
                     "bounds", k);
               ret = LPX_E_FAULT;
               goto done;
            }
         }
      }
      /* "warm up" the initial basis */
      ret = spx_warm_up(spx);
      switch (ret)
      {  case LPX_E_OK:
            break;
         case LPX_E_EMPTY:
            if (spx->msg_lev >= 1)
               print(prefix "problem has no rows/columns");
            ret = LPX_E_FAULT;
            goto done;
         case LPX_E_BADB:
            if (spx->msg_lev >= 1)
               print(prefix "initial basis is invalid");
            ret = LPX_E_FAULT;
            goto done;
         case LPX_E_SING:
            if (spx->msg_lev >= 1)
               print(prefix "initial basis is singular");
            ret = LPX_E_FAULT;
            goto done;
         default:
            insist(ret != ret);
      }
      /* if the initial basic solution is optimal (i.e. primal and dual
         feasible), nothing to search for */
      if (spx->p_stat == LPX_P_FEAS && spx->d_stat == LPX_D_FEAS)
      {  if (spx->msg_lev >= 2 && spx->out_dly == 0.0)
            print("!%6d:   objval = %17.9e   infeas = %17.9e",
               spx->it_cnt, spx_eval_obj(spx), 0.0);
         if (spx->msg_lev >= 3)
            print("OPTIMAL SOLUTION FOUND");
         ret = LPX_E_OK;
         goto done;
      }
      /* if the initial basic solution is primal infeasible, but dual
         feasible, the dual simplex method may be used */
      if (spx->d_stat == LPX_D_FEAS && spx->dual) goto dual;
feas: /* phase I: find a primal feasible basic solution */
      ret = spx_prim_feas(spx);
      switch (ret)
      {  case LPX_E_OK:
            goto opt;
         case LPX_E_NOFEAS:
            if (spx->msg_lev >= 3)
               print("PROBLEM HAS NO FEASIBLE SOLUTION");
            ret = LPX_E_OK;
            goto done;
         case LPX_E_ITLIM:
            if (spx->msg_lev >= 3)
               print("ITERATION LIMIT EXCEEDED; SEARCH TERMINATED");
            goto done;
         case LPX_E_TMLIM:
            if (spx->msg_lev >= 3)
               print("TIME LIMIT EXCEEDED; SEARCH TERMINATED");
            goto done;
         case LPX_E_INSTAB:
            if (spx->msg_lev >= 2)
               print(prefix "numerical instability (primal simplex, pha"
                  "se I)");
            goto feas;
         case LPX_E_SING:
            if (spx->msg_lev >= 1)
            {  print(prefix "numerical problems with basis matrix");
               print(prefix "sorry, basis recovery procedure not implem"
                  "ented yet");
            }
            goto done;
         default:
            insist(ret != ret);
      }
opt:  /* phase II: find an optimal basic solution (primal simplex) */
      ret = spx_prim_opt(spx);
      switch (ret)
      {  case LPX_E_OK:
            if (spx->msg_lev >= 3)
               print("OPTIMAL SOLUTION FOUND");
            goto done;
         case LPX_E_NOFEAS:
            if (spx->msg_lev >= 3)
               print("PROBLEM HAS UNBOUNDED SOLUTION");
            ret = LPX_E_OK;
            goto done;
         case LPX_E_ITLIM:
            if (spx->msg_lev >= 3)
               print("ITERATIONS LIMIT EXCEEDED; SEARCH TERMINATED");
            goto done;
         case LPX_E_TMLIM:
            if (spx->msg_lev >= 3)
               print("TIME LIMIT EXCEEDED; SEARCH TERMINATED");
            goto done;
         case LPX_E_INSTAB:
            if (spx->msg_lev >= 2)
               print(prefix "numerical instability (primal simplex, pha"
                  "se II)");
            goto feas;
         case LPX_E_SING:
            if (spx->msg_lev >= 1)
            {  print(prefix "numerical problems with basis matrix");
               print(prefix "sorry, basis recovery procedure not implem"
                  "ented yet");
            }
            goto done;
         default:
            insist(ret != ret);
      }
dual: /* phase II: find an optimal basic solution (dual simplex) */
      ret = spx_dual_opt(spx);
      switch (ret)
      {  case LPX_E_OK:
            if (spx->msg_lev >= 3)
               print("OPTIMAL SOLUTION FOUND");
            goto done;
         case LPX_E_NOFEAS:
            if (spx->msg_lev >= 3)
               print("PROBLEM HAS NO FEASIBLE SOLUTION");
            ret = LPX_E_OK;
            goto done;
         case LPX_E_OBJLL:
            if (spx->msg_lev >= 3)
               print("OBJECTIVE LOWER LIMIT REACHED; SEARCH TERMINATED")
                  ;
            goto done;
         case LPX_E_OBJUL:
            if (spx->msg_lev >= 3)
               print("OBJECTIVE UPPER LIMIT REACHED; SEARCH TERMINATED")
                  ;
            goto done;
         case LPX_E_ITLIM:
            if (spx->msg_lev >= 3)
               print("ITERATIONS LIMIT EXCEEDED; SEARCH TERMINATED");
            goto done;
         case LPX_E_TMLIM:
            if (spx->msg_lev >= 3)
               print("TIME LIMIT EXCEEDED; SEARCH TERMINATED");
            goto done;
         case LPX_E_INSTAB:
            if (spx->msg_lev >= 2)
               print(prefix "numerical instability (dual simplex)");
            goto feas;
         case LPX_E_SING:
            if (spx->msg_lev >= 1)
            {  print(prefix "numerical problems with basis matrix");
               print(prefix "sorry, basis recovery procedure not implem"
                  "ented yet");
            }
            goto done;
         default:
            insist(ret != ret);
      }
done: /* return to the calling program */
      return ret;
}

/* eof */
