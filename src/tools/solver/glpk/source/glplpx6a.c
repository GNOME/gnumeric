/* glplpx6a.c (simplex-based solver routines) */

/*----------------------------------------------------------------------
-- Copyright (C) 2000, 2001, 2002 Andrew Makhorin <mao@mai2.rcnet.ru>,
--               Department for Applied Informatics, Moscow Aviation
--               Institute, Moscow, Russia. All rights reserved.
--
-- This file is a part of GLPK (GNU Linear Programming Kit).
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
-- Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
-- 02111-1307, USA.
----------------------------------------------------------------------*/

#include <math.h>
#include <string.h>
#include "glpspx.h"

/*----------------------------------------------------------------------
-- lpx_warm_up - "warm up" the initial basis.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_warm_up(LPX *lp);
--
-- *Description*
--
-- The routine lpx_warm_up "warms up" the initial basis specified by
-- the array lp->tagx. "Warming up" includes (if necessary) reinverting
-- (factorizing) the initial basis matrix, computing the initial basic
-- solution components (values of basic variables, simplex multipliers,
-- reduced costs of non-basic variables), and determining primal and
-- dual statuses of the initial basic solution.
--
-- *Returns*
--
-- The routine lpx_warm_up returns one of the following exit codes:
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

int lpx_warm_up(LPX *lp)
{     int m = lp->m;
      int n = lp->n;
      int ret;
      /* check if the problem is empty */
      if (!(m > 0 && n > 0))
      {  ret = LPX_E_EMPTY;
         goto done;
      }
      /* reinvert the initial basis matrix (if necessary) */
      if (lp->b_stat != LPX_B_VALID)
      {  int i, j, k;
         /* invalidate the basic solution */
         lp->p_stat = LPX_P_UNDEF;
         lp->d_stat = LPX_D_UNDEF;
         /* build the arrays posx and tagx using the array tagx */
         i = j = 0;
         for (k = 1; k <= m+n; k++)
         {  if (lp->tagx[k] == LPX_BS)
            {  /* x[k] = xB[i] */
               i++;
               if (i > m)
               {  /* too many basic variables */
                  ret = LPX_E_BADB;
                  goto done;
               }
               lp->posx[k] = i, lp->indx[i] = k;
            }
            else
            {  /* x[k] = xN[j] */
               j++;
               if (j > n)
               {  /* too many non-basic variables */
                  ret = LPX_E_BADB;
                  goto done;
               }
               lp->posx[k] = m+j, lp->indx[m+j] = k;
            }
         }
         insist(i == m && j == n);
         /* reinvert the initial basis matrix */
         if (spx_invert(lp) != 0)
         {  /* the basis matrix is singular or ill-conditioned */
            ret = LPX_E_SING;
            goto done;
         }
      }
      /* now the basis is valid */
      insist(lp->b_stat == LPX_B_VALID);
      /* compute the initial primal solution */
      if (lp->p_stat == LPX_P_UNDEF)
      {  spx_eval_bbar(lp);
         if (spx_check_bbar(lp, lp->tol_bnd) == 0.0)
            lp->p_stat = LPX_P_FEAS;
         else
            lp->p_stat = LPX_P_INFEAS;
      }
      /* compute the initial dual solution */
      if (lp->d_stat == LPX_D_UNDEF)
      {  spx_eval_pi(lp);
         spx_eval_cbar(lp);
         if (spx_check_cbar(lp, lp->tol_dj) == 0.0)
            lp->d_stat = LPX_D_FEAS;
         else
            lp->d_stat = LPX_D_INFEAS;
      }
      /* the basis has been successfully "warmed up" */
      ret = LPX_E_OK;
      /* return to the calling program */
done: return ret;
}

/*----------------------------------------------------------------------
-- lpx_prim_opt - find optimal solution (primal simplex).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_prim_opt(LPX *lp);
--
-- *Description*
--
-- The routine lpx_prim_opt is intended to find optimal solution of an
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
-- The routine lpx_prim_opt returns one of the folloiwng exit codes:
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
      LPX *lp = spx->lp;
      int i, def = 0;
      for (i = 1; i <= lp->m; i++)
         if (lp->typx[lp->indx[i]] == LPX_FX) def++;
      print("*%6d:   objval = %17.9e   infeas = %17.9e (%d)",
         lp->it_cnt, spx_eval_obj(lp), spx_check_bbar(lp, 0.0), def);
      return;
}

int lpx_prim_opt(LPX *lp)
{     /* find optimal solution (primal simplex) */
      SPX *spx = NULL;
      int m = lp->m;
      int n = lp->n;
      int ret;
      gnm_float start = utime(), spent = 0.0;
      /* the initial basis should be "warmed up" */
      if (lp->b_stat != LPX_B_VALID ||
          lp->p_stat == LPX_P_UNDEF || lp->d_stat == LPX_D_UNDEF)
      {  ret = LPX_E_BADB;
         goto done;
      }
      /* the initial basic solution should be primal feasible */
      if (lp->p_stat != LPX_P_FEAS)
      {  ret = LPX_E_INFEAS;
         goto done;
      }
      /* if the initial basic solution is dual feasible, nothing to
         search for */
      if (lp->d_stat == LPX_D_FEAS)
      {  ret = LPX_E_OK;
         goto done;
      }
      /* allocate the common block */
      spx = umalloc(sizeof(SPX));
      spx->lp = lp;
      spx->meth = 'P';
      spx->p = 0;
      spx->p_tag = 0;
      spx->q = 0;
      spx->zeta = ucalloc(1+m, sizeof(gnm_float));
      spx->ap = ucalloc(1+n, sizeof(gnm_float));
      spx->aq = ucalloc(1+m, sizeof(gnm_float));
      spx->gvec = ucalloc(1+n, sizeof(gnm_float));
      spx->dvec = NULL;
      spx->refsp = (lp->price ? ucalloc(1+m+n, sizeof(int)) : NULL);
      spx->count = 0;
      spx->work = ucalloc(1+m+n, sizeof(gnm_float));
      spx->orig_typx = NULL;
      spx->orig_lb = spx->orig_ub = NULL;
      spx->orig_dir = 0;
      spx->orig_coef = NULL;
beg:  /* initialize weights of non-basic variables */
      if (!lp->price)
      {  /* textbook pricing will be used */
         int j;
         for (j = 1; j <= n; j++) spx->gvec[j] = 1.0;
      }
      else
      {  /* steepest edge pricing will be used */
         spx_reset_refsp(spx);
      }
      /* display information about the initial basic solution */
      if (lp->msg_lev >= 2 && lp->it_cnt % lp->out_frq != 0 &&
          lp->out_dly <= spent) prim_opt_dpy(spx);
      /* main loop starts here */
      for (;;)
      {  /* determine the spent amount of time */
         spent = utime() - start;
         /* display information about the current basic solution */
         if (lp->msg_lev >= 2 && lp->it_cnt % lp->out_frq == 0 &&
             lp->out_dly <= spent) prim_opt_dpy(spx);
         /* check if the iterations limit has been exhausted */
         if (lp->it_lim == 0)
         {  ret = LPX_E_ITLIM;
            break;
         }
         /* check if the time limit has been exhausted */
         if (lp->tm_lim >= 0.0 && lp->tm_lim <= spent)
         {  ret = LPX_E_TMLIM;
            break;
         }
         /* choose non-basic variable xN[q] */
         if (spx_prim_chuzc(spx, lp->tol_dj))
         {  /* basic solution components were recomputed; check primal
               feasibility */
            if (spx_check_bbar(lp, lp->tol_bnd) != 0.0)
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
         spx_eval_col(lp, spx->q, spx->aq, 1);
         /* choose basic variable xB[p] */
         if (spx_prim_chuzr(spx, lp->relax * lp->tol_bnd))
         {  /* the basis matrix should be reinverted, because the q-th
               column of the simplex table is unreliable */
            insist("not implemented yet" == NULL);
         }
         /* if no xB[p] has been chosen, the problem is unbounded (has
            no dual feasible solution) */
         if (spx->p == 0)
         {  ret = LPX_E_NOFEAS;
            break;
         }
         /* update values of basic variables */
         spx_update_bbar(spx, NULL);
         if (spx->p > 0)
         {  /* compute the p-th row of the inverse inv(B) */
            spx_eval_rho(lp, spx->p, spx->zeta);
            /* compute the p-th row of the current simplex table */
            spx_eval_row(lp, spx->zeta, spx->ap);
            /* update simplex multipliers */
            spx_update_pi(spx);
            /* update reduced costs of non-basic variables */
            spx_update_cbar(spx, 0);
            /* update weights of non-basic variables */
            if (lp->price) spx_update_gvec(spx);
         }
         /* jump to the adjacent vertex of the LP polyhedron */
         if (spx_change_basis(spx))
         {  /* the basis matrix should be reinverted */
            if (spx_invert(lp) != 0)
            {  /* numerical problems with the basis matrix */
               lp->p_stat = LPX_P_UNDEF;
               lp->d_stat = LPX_D_UNDEF;
               ret = LPX_E_SING;
               goto done;
            }
            /* compute the current basic solution components */
            spx_eval_bbar(lp);
            spx_eval_pi(lp);
            spx_eval_cbar(lp);
            /* check primal feasibility */
            if (spx_check_bbar(lp, lp->tol_bnd) != 0.0)
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
            gnm_float ae_gvec = lp->price ? spx_err_in_gvec(spx) : 0.0;
            print("bbar: %g; pi: %g; cbar: %g; gvec: %g",
               ae_bbar, ae_pi, ae_cbar, ae_gvec);
            if (ae_bbar > 1e-7 || ae_pi > 1e-7 || ae_cbar > 1e-7 ||
                ae_gvec > 1e-3) fault("solution accuracy too low");
         }
#endif
      }
      /* compute the final basic solution components */
      spx_eval_bbar(lp);
      spx_eval_pi(lp);
      spx_eval_cbar(lp);
      if (spx_check_bbar(lp, lp->tol_bnd) == 0.0)
         lp->p_stat = LPX_P_FEAS;
      else
         lp->p_stat = LPX_P_INFEAS;
      if (spx_check_cbar(lp, lp->tol_dj) == 0.0)
         lp->d_stat = LPX_D_FEAS;
      else
         lp->d_stat = LPX_D_INFEAS;
      /* display information about the final basic solution */
      if (lp->msg_lev >= 2 && lp->it_cnt % lp->out_frq != 0 &&
          lp->out_dly <= spent) prim_opt_dpy(spx);
      /* correct the preliminary diagnosis */
      switch (ret)
      {  case LPX_E_OK:
            /* assumed LPX_P_FEAS and LPX_D_FEAS */
            if (lp->p_stat != LPX_P_FEAS)
               ret = LPX_E_INSTAB;
            else if (lp->d_stat != LPX_D_FEAS)
            {  /* it seems we need to continue the search */
               goto beg;
            }
            break;
         case LPX_E_ITLIM:
         case LPX_E_TMLIM:
            /* assumed LPX_P_FEAS and LPX_D_INFEAS */
            if (lp->p_stat != LPX_P_FEAS)
               ret = LPX_E_INSTAB;
            else if (lp->d_stat == LPX_D_FEAS)
               ret = LPX_E_OK;
            break;
         case LPX_E_NOFEAS:
            /* assumed LPX_P_FEAS and LPX_D_INFEAS */
            if (lp->p_stat != LPX_P_FEAS)
               ret = LPX_E_INSTAB;
            else if (lp->d_stat == LPX_D_FEAS)
               ret = LPX_E_OK;
            else
               lp->d_stat = LPX_D_NOFEAS;
            break;
         case LPX_E_INSTAB:
            /* assumed LPX_P_INFEAS */
            if (lp->p_stat == LPX_P_FEAS)
            {  if (lp->d_stat == LPX_D_FEAS)
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
done: /* deallocate the common block */
      if (spx != NULL)
      {  ufree(spx->zeta);
         ufree(spx->ap);
         ufree(spx->aq);
         ufree(spx->gvec);
         if (lp->price) ufree(spx->refsp);
         ufree(spx->work);
         ufree(spx);
      }
      /* determine the spent amount of time */
      spent = utime() - start;
      /* decrease the time limit by the spent amount */
      if (lp->tm_lim >= 0.0)
      {  lp->tm_lim -= spent;
         if (lp->tm_lim < 0.0) lp->tm_lim = 0.0;
      }
      /* return to the calling program */
      return ret;
}

/*----------------------------------------------------------------------
-- lpx_prim_art - find primal feasible solution (primal simplex).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_prim_art(LPX *lp);
--
-- *Description*
--
-- The routine lpx_prim_art tries to find primal feasible solution of
-- an LP problem using the method of single artificial variable, which
-- is based on the primal simplex method (see the comments below).
--
-- On entry to the routine the initial basis should be "warmed up".
--
-- *Returns*
--
-- The routine lpx_prim_art returns one of the following exit codes:
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
-- this routine.
--
-- *Algorithm*
--
-- Let the current simplex table be
--
--    xB = A^ * xN,                                                  (1)
--
-- where
--
--    A^ = - inv(B) * N,                                             (2)
--
-- and some basic variables xB violate their (lower or upper) bounds.
-- We can make the current basic solution to be primal feasible if we
-- add some appropriate quantities to each right part of the simplex
-- table:
--
--    xB = A^ * xN + av,                                             (3)
--
-- where
--
--    av[i] = (lB)i - bbar[i] + delta[i], if bbar[i] < (lB)i,        (4)
--
--    av[i] = (uB)i - bbar[i] - delta[i], if bbar[i] > (uB)i,        (5)
--
-- and delta[i] > 0 is a non-negative offset intended to avoid primal
-- degeneracy, because after introducing the vector av basic variable
-- xB[i] is equal to (lB)i + delta[i] or (uB)i - delta[i].
--
-- Formally (3) is equivalent to introducing an artificial variable xv,
-- which is non-basic with the initial value 1 and has the column av as
-- its coefficients in the simplex table:
--
--    xB = A^ * xN + av * xv.                                        (6)
--
-- Multiplying both parts of (6) on B and accounting (2) we have:
--
--    B * xB + N * xN - B * av * xv = 0.                             (7)
--
-- We can consider the column (-B * av) as an additional column of the
-- augmented constraint matrix A~ = (I | -A), or, that is the same, the
-- column (B * av) as an additional column of the original constraint
-- matrix A, which corresponds to the artificial variable xv.
--
-- If the variable xv is non-basic and equal to 1, the artificial basic
-- solution is primal feasible and therefore can be used as an initial
-- solution on the phase I. Thus, in order to find a primal feasible
-- basic solution of the original LP problem, which has no artificial
-- variables, xv should be minimized to zero (if it is impossible, the
-- original problem has no feasible solution). Note also that the value
-- of xv, which is in the range [0,1], can be considered as a measure
-- of primal infeasibility. */

static gnm_float orig_objfun(SPX *spx)
{     /* this auxiliary routine computes the objective function value
         for the original LP problem */
      LPX *lp = spx->lp;
      gnm_float objval;
      void *t;
      t = lp->coef, lp->coef = spx->orig_coef, spx->orig_coef = t;
      objval = spx_eval_obj(lp);
      t = lp->coef, lp->coef = spx->orig_coef, spx->orig_coef = t;
      return objval;
}

static gnm_float orig_infeas(SPX *spx)
{     /* this auxiliary routine computes the infeasibilitiy measure for
         the original LP problem */
      LPX *lp = spx->lp;
      gnm_float infeas;
      /* the infeasibility measure is a current value of the artificial
         variable */
      if (lp->tagx[lp->m+lp->n] == LPX_BS)
         infeas = lp->bbar[lp->posx[lp->m+lp->n]];
      else
         infeas = spx_eval_xn_j(lp, lp->posx[lp->m+lp->n] - lp->m);
      return infeas;
}

static void prim_art_dpy(SPX *spx)
{     /* this auxiliary routine displays information about the current
         basic solution */
      LPX *lp = spx->lp;
      int i, def = 0;
      for (i = 1; i <= lp->m; i++)
         if (lp->typx[lp->indx[i]] == LPX_FX) def++;
      print(" %6d:   objval = %17.9e   infeas = %17.9e (%d)",
         lp->it_cnt, orig_objfun(spx), orig_infeas(spx), def);
      return;
}

int lpx_prim_art(LPX *lp)
{     /* find primal feasible solution (primal simplex) */
      SPX *spx = NULL;
      int m = lp->m;
      int n = lp->n;
      int i, j, k, ret, *ndx, final = 0;
      gnm_float *av, *col;
      gnm_float start = utime(), spent = 0.0;
      /* the initial basis should be "warmed up" */
      if (lp->b_stat != LPX_B_VALID ||
          lp->p_stat == LPX_P_UNDEF || lp->d_stat == LPX_D_UNDEF)
      {  ret = LPX_E_BADB;
         goto done;
      }
      /* if the initial basic solution is primal feasible, nothing to
         search for */
      if (lp->p_stat == LPX_P_FEAS)
      {  ret = LPX_E_OK;
         goto done;
      }
      /* allocate the common block (one extra location in the arrays
         ap, gvec, refsp, work, and coef is reserved for the artificial
         column, which will be introduced to the problem) */
      spx = umalloc(sizeof(SPX));
      spx->lp = lp;
      spx->meth = 'P';
      spx->p = 0;
      spx->p_tag = 0;
      spx->q = 0;
      spx->zeta = ucalloc(1+m, sizeof(gnm_float));
      spx->ap = ucalloc(1+n+1, sizeof(gnm_float));
      spx->aq = ucalloc(1+m, sizeof(gnm_float));
      spx->gvec = ucalloc(1+n+1, sizeof(gnm_float));
      spx->dvec = NULL;
      spx->refsp = (lp->price ? ucalloc(1+m+n+1, sizeof(int)) : NULL);
      spx->count = 0;
      spx->work = ucalloc(1+m+n+1, sizeof(gnm_float));
      spx->orig_typx = NULL;
      spx->orig_lb = spx->orig_ub = NULL;
      spx->orig_dir = 0;
      spx->orig_coef = ucalloc(1+m+n+1, sizeof(gnm_float));
beg:  /* save the original objective function, because it is changed by
         the routine */
      spx->orig_dir = lp->dir;
      memcpy(spx->orig_coef, lp->coef, (1+m+n) * sizeof(gnm_float));
      spx->orig_coef[m+n+1] = 0.0;
      /* compute the vector av */
      av = ucalloc(1+m, sizeof(gnm_float));
      for (i = 1; i <= m; i++)
      {  gnm_float eps = 0.10 * lp->tol_bnd, delta = 100.0, av_i, temp;
         k = lp->indx[i]; /* x[k] = xB[i] */
         av_i = 0.0;
         switch (lp->typx[k])
         {  case LPX_FR:
               /* xB[i] is free variable */
               break;
            case LPX_LO:
               /* xB[i] has lower bound */
               if (lp->bbar[i] < lp->lb[k] - eps)
                  av_i = (lp->lb[k] - lp->bbar[i]) + delta;
               break;
            case LPX_UP:
               /* xB[i] has upper bound */
               if (lp->bbar[i] > lp->ub[k] + eps)
                  av_i = (lp->ub[k] - lp->bbar[i]) - delta;
               break;
            case LPX_DB:
            case LPX_FX:
               /* xB[i] is gnm_float-bounded or fixed variable */
               if (lp->bbar[i] < lp->lb[k] - eps)
               {  temp = 0.5 * gnumabs(lp->lb[k] - lp->ub[k]);
                  if (temp > delta) temp = delta;
                  av_i = (lp->lb[k] - lp->bbar[i]) + temp;
               }
               if (lp->bbar[i] > lp->ub[k] + eps)
               {  temp = 0.5 * gnumabs(lp->lb[k] - lp->ub[k]);
                  if (temp > delta) temp = delta;
                  av_i = (lp->ub[k] - lp->bbar[i]) - temp;
               }
               break;
            default:
               insist(lp->typx != lp->typx);
         }
         av[i] = av_i;
      }
      /* compute the column B*av */
      ndx = ucalloc(1+m, sizeof(int));
      col = ucalloc(1+m, sizeof(gnm_float));
      for (i = 1; i <= m; i++) col[i] = 0.0;
      for (j = 1; j <= m; j++)
      {  int k = lp->indx[j]; /* x[k] = xB[j]; */
         if (k <= m)
         {  /* x[k] is auxiliary variable */
            col[k] += av[j];
         }
         else
         {  /* x[k] is structural variable */
            int ptr = lp->A->ptr[k];
            int end = ptr + lp->A->len[k] - 1;
            for (ptr = ptr; ptr <= end; ptr++)
               col[lp->A->ndx[ptr]] -= lp->A->val[ptr] * av[j];
         }
      }
      /* convert the column B*av to sparse format and "anti-scale" it,
         in order to avoid scaling performed by lpx_set_mat_col */
      k = 0;
      for (i = 1; i <= m; i++)
      {  if (col[i] != 0.0)
         {  k++;
            ndx[k] = i;
            col[k] = col[i] / lp->rs[i];
         }
      }
      /* add the artificial variable and its column to the problem */
      lpx_add_cols(lp, 1), n++;
      lpx_set_col_bnds(lp, n, LPX_DB, 0.0, 1.0);
      lpx_set_mat_col(lp, n, k, ndx, col);
      ufree(av);
      ufree(ndx);
      ufree(col);
      /* set the artificial variable to its upper unity bound in order
         to make the current basic solution primal feasible */
      lp->tagx[m+n] = LPX_NU;
      /* the artificial variable should be minimized to zero */
      lp->dir = LPX_MIN;
      for (k = 0; k <= m+n; k++) lp->coef[k] = 0.0;
      lp->coef[m+n] = 1.0;
      /* since the problem size has been changed, the factorization of
         the basis matrix doesn't exist; reinvert the basis matrix */
      {  int i, j, k;
         i = j = 0;
         for (k = 1; k <= m+n; k++)
         {  if (lp->tagx[k] == LPX_BS)
               i++, lp->posx[k] = i, lp->indx[i] = k;
            else
               j++, lp->posx[k] = m+j, lp->indx[m+j] = k;
         }
         insist(i == m && j == n);
      }
      if (spx_invert(lp) != 0) goto sing;
      /* compute the artificial basic solution components */
      spx_eval_bbar(lp);
      spx_eval_pi(lp);
      spx_eval_cbar(lp);
      /* now the basic solution should be primal feasible */
      insist(spx_check_bbar(lp, lp->tol_bnd) == 0.0);
      /* initialize weights of non-basic variables */
      if (!lp->price)
      {  /* textbook pricing will be used */
         int j;
         for (j = 1; j <= n; j++) spx->gvec[j] = 1.0;
      }
      else
      {  /* steepest edge pricing will be used */
         spx_reset_refsp(spx);
      }
      /* display information about the initial basic solution */
      if (lp->msg_lev >= 2 && lp->it_cnt % lp->out_frq != 0 &&
          lp->out_dly <= spent) prim_art_dpy(spx);
      /* main loop starts here */
      for (;;)
      {  /* determine the spent amount of time */
         spent = utime() - start;
         /* display information about the current basic solution */
         if (lp->msg_lev >= 2 && lp->it_cnt % lp->out_frq == 0 &&
             lp->out_dly <= spent) prim_art_dpy(spx);
         /* we needn't to wait until the artificial variable has left
            the basis */
         if (orig_infeas(spx) < 1e-10)
         {  /* the infeasibility is near to zero, therefore the current
               solution is primal feasible for the original problem */
            ret = LPX_E_OK;
            break;
         }
         /* check if the iterations limit has been exhausted */
         if (lp->it_lim == 0)
         {  ret = LPX_E_ITLIM;
            break;
         }
         /* check if the time limit has been exhausted */
         if (lp->tm_lim >= 0.0 && lp->tm_lim <= spent)
         {  ret = LPX_E_TMLIM;
            break;
         }
         /* choose non-basic variable xN[q] */
         if (spx_prim_chuzc(spx, lp->tol_dj))
         {  /* basic solution components were recomputed; check primal
               feasibility */
            if (spx_check_bbar(lp, lp->tol_bnd) != 0.0)
            {  /* the current solution became primal infeasible due to
                  round-off errors */
               ret = LPX_E_INSTAB;
               break;
            }
         }
         /* if no xN[q] has been chosen, the infeasibility is minimal
            but non-zero; therefore the original problem has no primal
            feasible solution */
         if (spx->q == 0)
         {  ret = LPX_E_NOFEAS;
            break;
         }
         /* compute the q-th column of the current simplex table (later
            this column will enter the basis) */
         spx_eval_col(lp, spx->q, spx->aq, 1);
         /* choose basic variable xB[p] */
         if (spx_prim_chuzr(spx, lp->relax * lp->tol_bnd))
         {  /* the basis matrix should be reinverted, because the q-th
               column of the simplex table is unreliable */
            insist("not implemented yet" == NULL);
         }
         /* the infeasibility can't be negative, therefore the modified
            problem can't have unbounded solution */
         insist(spx->p != 0);
         /* update values of basic variables */
         spx_update_bbar(spx, NULL);
         if (spx->p > 0)
         {  /* compute the p-th row of the inverse inv(B) */
            spx_eval_rho(lp, spx->p, spx->zeta);
            /* compute the p-th row of the current simplex table */
            spx_eval_row(lp, spx->zeta, spx->ap);
            /* update simplex multipliers */
            spx_update_pi(spx);
            /* update reduced costs of non-basic variables */
            spx_update_cbar(spx, 0);
            /* update weights of non-basic variables */
            if (lp->price) spx_update_gvec(spx);
         }
         /* jump to the adjacent vertex of the LP polyhedron */
         if (spx_change_basis(spx))
         {  /* the basis matrix should be reinverted */
            if (spx_invert(lp) != 0)
sing:       {  /* remove the artificial variable from the problem */
               lpx_unmark_all(lp);
               lpx_mark_col(lp, n, 1);
               lpx_del_items(lp);
               /* numerical problems with the basis matrix */
               lp->p_stat = LPX_P_UNDEF;
               lp->d_stat = LPX_D_UNDEF;
               ret = LPX_E_SING;
               goto done;
            }
            /* compute the current basic solution components */
            spx_eval_bbar(lp);
            spx_eval_pi(lp);
            spx_eval_cbar(lp);
            /* check primal feasibility */
            if (spx_check_bbar(lp, lp->tol_bnd) != 0.0)
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
            gnm_float ae_gvec = lp->price ? spx_err_in_gvec(spx) : 0.0;
            print("bbar: %g; pi: %g; cbar: %g; gvec: %g",
               ae_bbar, ae_pi, ae_cbar, ae_gvec);
            if (ae_bbar > 1e-7 || ae_pi > 1e-7 || ae_cbar > 1e-7 ||
                ae_gvec > 1e-3) fault("solution accuracy too low");
         }
#endif
      }
      /* display information about the final basic solution */
      if (lp->msg_lev >= 2 && lp->it_cnt % lp->out_frq != 0 &&
          lp->out_dly <= spent) prim_art_dpy(spx);
      /* if the artificial variable is still basic, we have to pull it
         from the basis, because the original problem has no artificial
         variables */
      if (lp->tagx[m+n] == LPX_BS)
      {  /* replace the artificial variable by a non-basic variable,
            which has greatest influence coefficient (for the sake of
            numerical stability); it is understood that this operation
            is a dual simplex iteration */
         int j;
         gnm_float big;
         spx->p = lp->posx[m+n]; /* x[m+n] = xB[p] */
         insist(1 <= spx->p && spx->p <= m);
         /* the artificial variable will be set on its lower bound */
         spx->p_tag = LPX_NL;
         /* compute the p-th row of the inverse inv(B) */
         spx_eval_rho(lp, spx->p, spx->zeta);
         /* compute the p-th row of the current simplex table */
         spx_eval_row(lp, spx->zeta, spx->ap);
         /* choose non-basic variable xN[q] with greatest (in absolute
            value) influence coefficient */
         spx->q = 0, big = 0.0;
         for (j = 1; j <= n; j++)
         {  if (big < gnumabs(spx->ap[j]))
               spx->q = j, big = gnumabs(spx->ap[j]);
         }
         insist(spx->q != 0);
         /* perform forward transformation of the column of xN[q] (to
            prepare it for replacing the artificial column in the basis
            matrix) */
         spx_eval_col(lp, spx->q, spx->aq, 1);
         /* jump to the adjacent basis, where the artificial variable
            is non-basic; note that if the artificial variable being
            basic is close to zero (and therefore the artificial basic
            solution is primal feasible), it is not changed becoming
            non-basic, in which case basic variables are also remain
            unchanged, so the adjacent basis remains primal feasible */
         spx_change_basis(spx);
         insist(lp->tagx[m+n] == LPX_NL);
      }
      /* remove the artificial variable from the problem */
      lpx_unmark_all(lp);
      lpx_mark_col(lp, n, 1);
      lpx_del_items(lp), n--;
      /* restore the original objective function */
      lp->dir = spx->orig_dir;
      memcpy(lp->coef, spx->orig_coef, (1+m+n) * sizeof(gnm_float));
      /* since the problem size has been changed, the factorization of
         the basis matrix doesn't exist; reinvert the basis matrix */
      {  int i, j, k;
         i = j = 0;
         for (k = 1; k <= m+n; k++)
         {  if (lp->tagx[k] == LPX_BS)
               i++, lp->posx[k] = i, lp->indx[i] = k;
            else
               j++, lp->posx[k] = m+j, lp->indx[m+j] = k;
         }
         insist(i == m && j == n);
      }
      insist(spx_invert(lp) == 0);
      /* compute the final basic solution components */
      spx_eval_bbar(lp);
      spx_eval_pi(lp);
      spx_eval_cbar(lp);
      if (spx_check_bbar(lp, lp->tol_bnd) == 0.0)
         lp->p_stat = LPX_P_FEAS;
      else
         lp->p_stat = LPX_P_INFEAS;
      if (spx_check_cbar(lp, lp->tol_dj) == 0.0)
         lp->d_stat = LPX_D_FEAS;
      else
         lp->d_stat = LPX_D_INFEAS;
      /* correct the preliminary diagnosis */
      switch (ret)
      {  case LPX_E_OK:
            /* assumed LPX_P_FEAS */
            if (lp->p_stat != LPX_P_FEAS)
               ret = LPX_E_INSTAB;
            break;
         case LPX_E_ITLIM:
         case LPX_E_TMLIM:
            /* assumed LPX_P_INFEAS */
            if (lp->p_stat == LPX_P_FEAS)
               ret = LPX_E_OK;
            break;
         case LPX_E_NOFEAS:
            /* assumed LPX_P_INFEAS */
            if (lp->p_stat == LPX_P_FEAS)
               ret = LPX_E_OK;
            else
            {  /* the problem can have tiny feasible region, which is
                  not always reliably detected by this method; because
                  of that we need to repeat the search before making a
                  final conclusion */
               if (!final)
               {  final = 1;
                  goto beg;
               }
               lp->p_stat = LPX_P_NOFEAS;
            }
            break;
         case LPX_E_INSTAB:
            /* assumed LPX_P_INFEAS */
            if (lp->p_stat == LPX_P_FEAS)
               ret = LPX_E_OK;
            break;
         default:
            insist(ret != ret);
      }
done: /* deallocate the common block */
      if (spx != NULL)
      {  ufree(spx->zeta);
         ufree(spx->ap);
         ufree(spx->aq);
         ufree(spx->gvec);
         if (lp->price) ufree(spx->refsp);
         ufree(spx->work);
         ufree(spx->orig_coef);
         ufree(spx);
      }
      /* determine the spent amount of time */
      spent = utime() - start;
      /* decrease the time limit by the spent amount */
      if (lp->tm_lim >= 0.0)
      {  lp->tm_lim -= spent;
         if (lp->tm_lim < 0.0) lp->tm_lim = 0.0;
      }
      /* return to the calling program */
      return ret;
}

/*----------------------------------------------------------------------
-- lpx_dual_opt - find optimal solution (dual simplex).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_dual_opt(LPX *lp);
--
-- *Description*
--
-- The routine lpx_dual_opt is intended to find optimal solution of an
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
-- The routine lpx_dual_opt returns one of the following exit codes:
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
      LPX *lp = spx->lp;
      int i, def = 0;
      for (i = 1; i <= lp->m; i++)
         if (lp->typx[lp->indx[i]] == LPX_FX) def++;
      print("|%6d:   objval = %17.9e   infeas = %17.9e (%d)",
         lp->it_cnt, spx_eval_obj(lp), spx_check_bbar(lp, 0.0), def);
      return;
}

int lpx_dual_opt(LPX *lp)
{     /* find optimal solution (dual simplex) */
      SPX *spx = NULL;
      int m = lp->m;
      int n = lp->n;
      int ret;
      gnm_float start = utime(), spent = 0.0, obj;
      /* the initial basis should be "warmed up" */
      if (lp->b_stat != LPX_B_VALID ||
          lp->p_stat == LPX_P_UNDEF || lp->d_stat == LPX_D_UNDEF)
      {  ret = LPX_E_BADB;
         goto done;
      }
      /* the initial basic solution should be dual feasible */
      if (lp->d_stat != LPX_D_FEAS)
      {  ret = LPX_E_INFEAS;
         goto done;
      }
      /* if the initial basic solution is primal feasible, nothing to
         search for */
      if (lp->p_stat == LPX_P_FEAS)
      {  ret = LPX_E_OK;
         goto done;
      }
      /* allocate common block */
      spx = umalloc(sizeof(SPX));
      spx->lp = lp;
      spx->meth = 'D';
      spx->p = 0;
      spx->p_tag = 0;
      spx->q = 0;
      spx->zeta = ucalloc(1+m, sizeof(gnm_float));
      spx->ap = ucalloc(1+n, sizeof(gnm_float));
      spx->aq = ucalloc(1+m, sizeof(gnm_float));
      spx->gvec = NULL;
      spx->dvec = ucalloc(1+m, sizeof(gnm_float));
      spx->refsp = (lp->price ? ucalloc(1+m+n, sizeof(gnm_float)) : NULL);
      spx->count = 0;
      spx->work = ucalloc(1+m+n, sizeof(gnm_float));
      spx->orig_typx = NULL;
      spx->orig_lb = spx->orig_ub = NULL;
      spx->orig_dir = 0;
      spx->orig_coef = NULL;
beg:  /* compute initial value of the objective function */
      obj = spx_eval_obj(lp);
      /* initialize weights of basic variables */
      if (!lp->price)
      {  /* textbook pricing will be used */
         int i;
         for (i = 1; i <= m; i++) spx->dvec[i] = 1.0;
      }
      else
      {  /* steepest edge pricing will be used */
         spx_reset_refsp(spx);
      }
      /* display information about the initial basic solution */
      if (lp->msg_lev >= 2 && lp->it_cnt % lp->out_frq != 0 &&
          lp->out_dly <= spent) dual_opt_dpy(spx);
      /* main loop starts here */
      for (;;)
      {  /* determine the spent amount of time */
         spent = utime() - start;
         /* display information about the current basic solution */
         if (lp->msg_lev >= 2 && lp->it_cnt % lp->out_frq == 0 &&
             lp->out_dly <= spent) dual_opt_dpy(spx);
         /* if the objective function should be minimized, check if it
            has reached its upper bound */
         if (lp->dir == LPX_MIN && obj >= lp->obj_ul)
         {  ret = LPX_E_OBJUL;
            break;
         }
         /* if the objective function should be maximized, check if it
            has reached its lower bound */
         if (lp->dir == LPX_MAX && obj <= lp->obj_ll)
         {  ret = LPX_E_OBJLL;
            break;
         }
         /* check if the iterations limit has been exhausted */
         if (lp->it_lim == 0)
         {  ret = LPX_E_ITLIM;
            break;
         }
         /* check if the time limit has been exhausted */
         if (lp->tm_lim >= 0.0 && lp->tm_lim <= spent)
         {  ret = LPX_E_TMLIM;
            break;
         }
         /* choose basic variable */
         spx_dual_chuzr(spx, lp->tol_bnd);
         /* if no xB[p] has been chosen, the current basic solution is
            primal feasible and therefore optimal */
         if (spx->p == 0)
         {  ret = LPX_E_OK;
            break;
         }
         /* compute the p-th row of the inverse inv(B) */
         spx_eval_rho(lp, spx->p, spx->zeta);
         /* compute the p-th row of the current simplex table */
         spx_eval_row(lp, spx->zeta, spx->ap);
         /* choose non-basic variable xN[q] */
         if (spx_dual_chuzc(spx, lp->relax * lp->tol_dj))
         {  /* the basis matrix should be reinverted, because the p-th
               row of the simplex table is unreliable */
            insist("not implemented yet" == NULL);
         }
         /* if no xN[q] has been chosen, there is no primal feasible
            solution (the dual problem has unbounded solution) */
         if (spx->q == 0)
         {  ret = LPX_E_NOFEAS;
            break;
         }
         /* compute the q-th column of the current simplex table (later
            this column will enter the basis) */
         spx_eval_col(lp, spx->q, spx->aq, 1);
         /* update values of basic variables and value of the objective
            function */
         spx_update_bbar(spx, &obj);
         /* update simplex multipliers */
         spx_update_pi(spx);
         /* update reduced costs of non-basic variables */
         spx_update_cbar(spx, 0);
         /* update weights of basic variables */
         if (lp->price) spx_update_dvec(spx);
         /* if xB[p] is fixed variable, adjust its non-basic tag */
         if (lp->typx[lp->indx[spx->p]] == LPX_FX) spx->p_tag = LPX_NS;
         /* jump to the adjacent vertex of the LP polyhedron */
         if (spx_change_basis(spx))
         {  /* the basis matrix should be reinverted */
            if (spx_invert(lp) != 0)
            {  /* numerical problems with the basis matrix */
               lp->p_stat = LPX_P_UNDEF;
               lp->d_stat = LPX_D_UNDEF;
               ret = LPX_E_SING;
               goto done;
            }
            /* compute the current basic solution components */
            spx_eval_bbar(lp);
            obj = spx_eval_obj(lp);
            spx_eval_pi(lp);
            spx_eval_cbar(lp);
            /* check dual feasibility */
            if (spx_check_cbar(lp, lp->tol_dj) != 0.0)
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
            gnm_float ae_dvec = lp->price ? spx_err_in_dvec(spx) : 0.0;
            print("bbar: %g; pi: %g; cbar: %g; dvec: %g",
               ae_bbar, ae_pi, ae_cbar, ae_dvec);
            if (ae_bbar > 1e-9 || ae_pi > 1e-9 || ae_cbar > 1e-9 ||
                ae_dvec > 1e-3)
               insist("solution accuracy too low" == NULL);
         }
#endif
      }
      /* compute the final basic solution components */
      spx_eval_bbar(lp);
      obj = spx_eval_obj(lp);
      spx_eval_pi(lp);
      spx_eval_cbar(lp);
      if (spx_check_bbar(lp, lp->tol_bnd) == 0.0)
         lp->p_stat = LPX_P_FEAS;
      else
         lp->p_stat = LPX_P_INFEAS;
      if (spx_check_cbar(lp, lp->tol_dj) == 0.0)
         lp->d_stat = LPX_D_FEAS;
      else
         lp->d_stat = LPX_D_INFEAS;
      /* display information about the final basic solution */
      if (lp->msg_lev >= 2 && lp->it_cnt % lp->out_frq != 0 &&
          lp->out_dly <= spent) dual_opt_dpy(spx);
      /* correct the preliminary diagnosis */
      switch (ret)
      {  case LPX_E_OK:
            /* assumed LPX_P_FEAS and LPX_D_FEAS */
            if (lp->d_stat != LPX_D_FEAS)
               ret = LPX_E_INSTAB;
            else if (lp->p_stat != LPX_P_FEAS)
            {  /* it seems we need to continue the search */
               goto beg;
            }
            break;
         case LPX_E_OBJLL:
         case LPX_E_OBJUL:
            /* assumed LPX_P_INFEAS and LPX_D_FEAS */
            if (lp->d_stat != LPX_D_FEAS)
               ret = LPX_E_INSTAB;
            else if (lp->p_stat == LPX_P_FEAS)
               ret = LPX_E_OK;
            else if (lp->dir == LPX_MIN && obj < lp->obj_ul ||
                     lp->dir == LPX_MAX && obj > lp->obj_ll)
            {  /* it seems we need to continue the search */
               goto beg;
            }
            break;
         case LPX_E_ITLIM:
         case LPX_E_TMLIM:
            /* assumed LPX_P_INFEAS and LPX_D_FEAS */
            if (lp->d_stat != LPX_D_FEAS)
               ret = LPX_E_INSTAB;
            else if (lp->p_stat == LPX_P_FEAS)
               ret = LPX_E_OK;
            break;
         case LPX_E_NOFEAS:
            /* assumed LPX_P_INFEAS and LPX_D_FEAS */
            if (lp->d_stat != LPX_D_FEAS)
               ret = LPX_E_INSTAB;
            else if (lp->p_stat == LPX_P_FEAS)
               ret = LPX_E_OK;
            else
               lp->p_stat = LPX_P_NOFEAS;
            break;
         case LPX_E_INSTAB:
            /* assumed LPX_D_INFEAS */
            if (lp->d_stat == LPX_D_FEAS)
            {  if (lp->p_stat == LPX_P_FEAS)
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
done: /* deallocate the common block */
      if (spx != NULL)
      {  ufree(spx->zeta);
         ufree(spx->ap);
         ufree(spx->aq);
         ufree(spx->dvec);
         if (lp->price) ufree(spx->refsp);
         ufree(spx->work);
         ufree(spx);
      }
      /* determine the spent amount of time */
      spent = utime() - start;
      /* decrease the time limit by the spent amount */
      if (lp->tm_lim >= 0.0)
      {  lp->tm_lim -= spent;
         if (lp->tm_lim < 0.0) lp->tm_lim = 0.0;
      }
      /* return to the calling program */
      return ret;
}

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
-- The routine lpx_simplex returns one of the following exit codes:
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
-- LPX_E_SING     the basis matrix got singular or ill-conditioned due
--                to improper simplex iteration.
--
-- Note that additional exit codes may appear in the future versions of
-- this routine. */

int lpx_simplex(LPX *lp)
{     int ret;
#     define prefix "lpx_simplex: "
      /* "warm up" the initial basis */
      ret = lpx_warm_up(lp);
      switch (ret)
      {  case LPX_E_OK:
            break;
         case LPX_E_EMPTY:
            if (lp->msg_lev >= 1)
               print(prefix "problem has no rows/columns");
            ret = LPX_E_FAULT;
            goto done;
         case LPX_E_BADB:
            if (lp->msg_lev >= 1)
               print(prefix "initial basis is invalid");
            ret = LPX_E_FAULT;
            goto done;
         case LPX_E_SING:
            if (lp->msg_lev >= 1)
               print(prefix "initial basis is singular");
            ret = LPX_E_FAULT;
            goto done;
         default:
            insist(ret != ret);
      }
      /* if the initial basic solution is optimal (i.e. primal and dual
         feasible), nothing to search for */
      if (lp->p_stat == LPX_P_FEAS && lp->d_stat == LPX_D_FEAS)
      {  if (lp->msg_lev >= 2 && lp->out_dly == 0.0)
            print("!%6d:   objval = %17.9e   infeas = %17.9e",
               lp->it_cnt, lpx_get_obj_val(lp), 0.0);
         if (lp->msg_lev >= 3)
            print("OPTIMAL SOLUTION FOUND");
         ret = LPX_E_OK;
         goto done;
      }
      /* if the initial basic solution is primal infeasible, but dual
         feasible, the dual simplex method may be used */
      if (lp->d_stat == LPX_D_FEAS && lp->dual) goto dual;
feas: /* phase I: find a primal feasible basic solution */
      ret = lpx_prim_art(lp);
      switch (ret)
      {  case LPX_E_OK:
            goto opt;
         case LPX_E_NOFEAS:
            if (lp->msg_lev >= 3)
               print("PROBLEM HAS NO FEASIBLE SOLUTION");
            ret = LPX_E_OK;
            goto done;
         case LPX_E_ITLIM:
            if (lp->msg_lev >= 3)
               print("ITERATION LIMIT EXCEEDED; SEARCH TERMINATED");
            goto done;
         case LPX_E_TMLIM:
            if (lp->msg_lev >= 3)
               print("TIME LIMIT EXCEEDED; SEARCH TERMINATED");
            goto done;
         case LPX_E_INSTAB:
            if (lp->msg_lev >= 2)
               print(prefix "numerical instability");
            goto feas;
         case LPX_E_SING:
            if (lp->msg_lev >= 1)
            {  print(prefix "numerical problems with basis matrix");
               print(prefix "sorry, basis recovery procedure not implem"
                  "ented yet");
            }
            goto done;
         default:
            insist(ret != ret);
      }
opt:  /* phase II: find an optimal basic solution (primal simplex) */
      ret = lpx_prim_opt(lp);
      switch (ret)
      {  case LPX_E_OK:
            if (lp->msg_lev >= 3)
               print("OPTIMAL SOLUTION FOUND");
            goto done;
         case LPX_E_NOFEAS:
            if (lp->msg_lev >= 3)
               print("PROBLEM HAS UNBOUNDED SOLUTION");
            ret = LPX_E_OK;
            goto done;
         case LPX_E_ITLIM:
            if (lp->msg_lev >= 3)
               print("ITERATIONS LIMIT EXCEEDED; SEARCH TERMINATED");
            goto done;
         case LPX_E_TMLIM:
            if (lp->msg_lev >= 3)
               print("TIME LIMIT EXCEEDED; SEARCH TERMINATED");
            goto done;
         case LPX_E_INSTAB:
            if (lp->msg_lev >= 2)
               print(prefix "numerical instability");
            goto feas;
         case LPX_E_SING:
            if (lp->msg_lev >= 1)
            {  print(prefix "numerical problems with basis matrix");
               print(prefix "sorry, basis recovery procedure not implem"
                  "ented yet");
            }
            goto done;
         default:
            insist(ret != ret);
      }
dual: /* phase II: find an optimal basic solution (dual simplex) */
      ret = lpx_dual_opt(lp);
      switch (ret)
      {  case LPX_E_OK:
            if (lp->msg_lev >= 3)
               print("OPTIMAL SOLUTION FOUND");
            goto done;
         case LPX_E_NOFEAS:
            if (lp->msg_lev >= 3)
               print("PROBLEM HAS NO FEASIBLE SOLUTION");
            ret = LPX_E_OK;
            goto done;
         case LPX_E_OBJLL:
            if (lp->msg_lev >= 3)
               print("OBJECTIVE LOWER LIMIT REACHED; SEARCH TERMINATED")
                  ;
            goto done;
         case LPX_E_OBJUL:
            if (lp->msg_lev >= 3)
               print("OBJECTIVE UPPER LIMIT REACHED; SEARCH TERMINATED")
                  ;
            goto done;
         case LPX_E_ITLIM:
            if (lp->msg_lev >= 3)
               print("ITERATIONS LIMIT EXCEEDED; SEARCH TERMINATED");
            goto done;
         case LPX_E_TMLIM:
            if (lp->msg_lev >= 3)
               print("TIME LIMIT EXCEEDED; SEARCH TERMINATED");
            goto done;
         case LPX_E_INSTAB:
            if (lp->msg_lev >= 2)
               print(prefix "numerical instability");
            goto feas;
         case LPX_E_SING:
            if (lp->msg_lev >= 1)
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
#     undef prefix
}

/* eof */
