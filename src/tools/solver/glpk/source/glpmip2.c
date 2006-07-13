/* glpmip2.c */

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
#include <stdio.h>
#include "glplib.h"
#include "glpmip.h"

/*----------------------------------------------------------------------
-- show_progress - display progress of the search.
--
-- This routine displays some information about progress of the search.
--
-- This information includes:
--
-- the current number of iterations performed by the simplex solver;
--
-- the objective value for the best known integer feasible solution,
-- which is upper (minimization) or lower (maximization) global bound
-- for optimal solution of the original mip problem;
--
-- the best local bound for active nodes, which is lower (minimization)
-- or upper (maximization) global bound for optimal solution of the
-- original mip problem;
--
-- the relative mip gap, in percents;
--
-- the number of open (active) subproblems;
--
-- the number of completely explored subproblems, i.e. whose nodes have
-- been already removed from the tree. */

static void show_progress(MIPTREE *tree)
{     int p;
      gnm_float temp;
      char best_mip[50], best_bound[50], *rho, rel_gap[50];
      /* format the best known integer feasible solution */
      if (tree->found)
         sprintf(best_mip, "%17.9e", tree->best);
      else
         sprintf(best_mip, "%17s", "not found yet");
      /* determine reference number of an active subproblem whose local
         bound is best */
      p = mip_best_node(tree);
      /* format the best bound */
      if (p == 0)
         sprintf(best_bound, "%17s", "tree is empty");
      else
      {  temp = tree->slot[p].node->bound;
         if (temp == -DBL_MAX)
            sprintf(best_bound, "%17s", "-inf");
         else if (temp == +DBL_MAX)
            sprintf(best_bound, "%17s", "+inf");
         else
            sprintf(best_bound, "%17.9e", temp);
      }
      /* choose the relation sign between global bounds */
      switch (tree->dir)
      {  case LPX_MIN: rho = ">="; break;
         case LPX_MAX: rho = "<="; break;
         default: insist(tree != tree);
      }
      /* format the relative mip gap */
      temp = mip_relative_gap(tree);
      if (temp == 0.0)
         sprintf(rel_gap, "  0.0%%");
      else if (temp < 0.001)
         sprintf(rel_gap, "< 0.1%%");
      else if (temp <= 9.999)
         sprintf(rel_gap, "%5.1f%%", 100.0 * temp);
      else
         sprintf(rel_gap, "%6s", "");
      /* display progress of the search */
      print("+%6d: mip = %s %s %s %s (%d; %d)",
         lpx_get_int_parm(tree->lp, LPX_K_ITCNT), best_mip, rho,
         best_bound, rel_gap, tree->a_cnt, tree->t_cnt - tree->n_cnt);
      tree->tm_lag = utime();
      return;
}

/*----------------------------------------------------------------------
-- set_local_bound - set local bound for current subproblem.
--
-- This routine sets the local bound for integer optimal solution of
-- the current subproblem, which is optimal solution of LP relaxation.
-- If the latter is fractional while the objective function is known to
-- be integral for any integer feasible point, the bound is strengthen
-- by rounding up (minimization) or down (maximization). */

static void set_local_bound(MIPTREE *tree)
{     gnm_float bound;
      bound = lpx_get_obj_val(tree->lp);
      if (tree->int_obj)
      {  /* the objective function is known to be integral */
         gnm_float temp;
         temp = gnm_floor(bound + 0.5);
         if (temp - 1e-5 <= bound && bound <= temp + 1e-5)
            bound = temp;
         else
         {  switch (tree->dir)
            {  case LPX_MIN:
                  bound = gnm_ceil(bound); break;
               case LPX_MAX:
                  bound = gnm_floor(bound); break;
               default:
                  insist(tree != tree);
            }
         }
      }
      insist(tree->curr != NULL);
      tree->curr->bound = bound;
      if (tree->msg_lev >= 3)
         print("Local bound is %.9e", bound);
      return;
}

/*----------------------------------------------------------------------
-- is_branch_hopeful - check if specified branch is hopeful.
--
-- This routine checks if the specified subproblem can have an integer
-- optimal solution which is better than the best known one.
--
-- The check is based on comparison of the local objective bound stored
-- in the subproblem descriptor and the incumbent objective value which
-- is the global objective bound.
--
-- If there is a chance that the specified subproblem can have a better
-- integer optimal solution, the routine returns non-zero. Otherwise, if
-- the corresponding branch can pruned, zero is returned. */

static int is_branch_hopeful(MIPTREE *tree, int p)
{     int ret = 1;
      if (tree->found)
      {  gnm_float bound, eps;
         insist(1 <= p && p <= tree->nslots);
         insist(tree->slot[p].node != NULL);
         bound = tree->slot[p].node->bound;
         eps = tree->tol_obj * (1.0 + gnm_abs(tree->best));
         switch (tree->dir)
         {  case LPX_MIN:
               if (bound >= tree->best - eps) ret = 0;
               break;
            case LPX_MAX:
               if (bound <= tree->best + eps) ret = 0;
               break;
            default:
               insist(tree != tree);
         }
      }
      return ret;
}

/*----------------------------------------------------------------------
-- check_integrality - check integrality of basic solution.
--
-- This routine checks if the basic solution of LP relaxation of the
-- current subproblem satisfies to integrality conditions, i.e. that all
-- variables of integer kind have integral primal values. (The solution
-- is assumed to be optimal.)
--
-- For each variable of integer kind the routine computes the following
-- quantity:
--
--    ii(x[j]) = min(x[j] - gnm_floor(x[j]), gnm_ceil(x[j]) - x[j]),         (1)
--
-- which is a measure of the integer infeasibility (non-integrality) of
-- x[j] (for example, ii(2.1) = 0.1, ii(3.7) = 0.3, ii(5.0) = 0). It is
-- understood that 0 <= ii(x[j]) <= 0.5, and variable x[j] is integer
-- feasible if ii(x[j]) = 0. However, due to floating-point arithmetic
-- the routine checks less restrictive condition:
--
--    ii(x[j]) <= tol_int,                                           (2)
--
-- where tol_int is a given tolerance (small positive number) and marks
-- each variable which does not satisfy to (2) as integer infeasible by
-- setting its fractionality flag.
--
-- In order to characterize integer infeasibility of the basic solution
-- in the whole the routine computes two parameters: ii_cnt, which is
-- the number of variables with the fractionality flag set, and ii_sum,
-- which is the sum of integer infeasibilities (1). */

static void check_integrality(MIPTREE *tree)
{     LPX *lp = tree->lp;
      int j, type, ii_cnt = 0;
      gnm_float lb, ub, x, temp1, temp2, ii_sum = 0.0;
      /* walk through the set of columns (structural variables) */
      for (j = 1; j <= tree->n; j++)
      {  tree->non_int[j] = 0;
         /* if the column is continuous, skip it */
         if (!tree->int_col[j]) continue;
         /* if the column is non-basic, it is integer feasible */
         if (lpx_get_col_stat(lp, j) != LPX_BS) continue;
         /* obtain the type and bounds of the column */
         type = lpx_get_col_type(lp, j);
         lb = lpx_get_col_lb(lp, j);
         ub = lpx_get_col_ub(lp, j);
         /* obtain value of the column in optimal basic solution */
         x = lpx_get_col_prim(lp, j);
         /* if the column's primal value is close to the lower bound,
            the column is integer feasible within given tolerance */
         if (type == LPX_LO || type == LPX_DB || type == LPX_FX)
         {  temp1 = lb - tree->tol_int;
            temp2 = lb + tree->tol_int;
            if (temp1 <= x && x <= temp2) continue;
            /* the lower bound must not be violated */
            insist(x >= lb);
         }
         /* if the column's primal value is close to the upper bound,
            the column is integer feasible within given tolerance */
         if (type == LPX_UP || type == LPX_DB || type == LPX_FX)
         {  temp1 = ub - tree->tol_int;
            temp2 = ub + tree->tol_int;
            if (temp1 <= x && x <= temp2) continue;
            /* the upper bound must not be violated */
            insist(x <= ub);
         }
         /* if the column's primal value is close to nearest integer,
            the column is integer feasible within given tolerance */
         temp1 = gnm_floor(x + 0.5) - tree->tol_int;
         temp2 = gnm_floor(x + 0.5) + tree->tol_int;
         if (temp1 <= x && x <= temp2) continue;
         /* otherwise the column is integer infeasible */
         tree->non_int[j] = 1;
         /* increase the number of fractional-valued columns */
         ii_cnt++;
         /* compute the sum of integer infeasibilities */
         temp1 = x - gnm_floor(x);
         temp2 = gnm_ceil(x) - x;
         insist(temp1 > 0.0 && temp2 > 0.0);
         ii_sum += (temp1 <= temp2 ? temp1 : temp2);
      }
      /* store ii_cnt and ii_sum in the current problem descriptor */
      insist(tree->curr != NULL);
      tree->curr->ii_cnt = ii_cnt;
      tree->curr->ii_sum = ii_sum;
      /* and also display these parameters */
      if (tree->msg_lev >= 3)
      {  if (ii_cnt == 0)
            print("There are no fractional columns");
         else if (ii_cnt == 1)
            print("There is one fractional column, integer infeasibilit"
               "y is %.3e", ii_sum);
         else
            print("There are %d fractional columns, integer infeasibili"
               "ty is %.3e", ii_cnt, ii_sum);
      }
      return;
}

/*----------------------------------------------------------------------
-- record_solution - record better integer feasible solution.
--
-- This routine records optimal basic solution of LP relaxation of the
-- current subproblem, which being integer feasible is better than the
-- best known integer feasible solution. */

static void record_solution(MIPTREE *tree)
{     int m = tree->m;
      int n = tree->n;
      LPX *lp = tree->lp;
      int i, j;
      gnm_float temp;
      tree->found = 1;
      tree->best = lpx_get_obj_val(lp);
      for (i = 1; i <= m; i++)
      {  temp = lpx_get_row_prim(lp, i);
         tree->mipx[i] = temp;
      }
      for (j = 1; j <= n; j++)
      {  temp = lpx_get_col_prim(lp, j);
         /* value of the integer column must be integral */
         if (tree->int_col[j]) temp = gnm_floor(temp + 0.5);
         tree->mipx[m+j] = temp;
      }
      return;
}

/*----------------------------------------------------------------------
-- fix_by_red_cost - fix non-basic integer columns by reduced costs.
--
-- This routine fixes some non-basic integer columns if their reduced
-- costs indicate that increasing (decreasing) the column at least by
-- one involves the objective value becoming worse than the incumbent
-- objective value (i.e. the global bound). */

static void fix_by_red_cost(MIPTREE *tree)
{     int n = tree->n;
      LPX *lp = tree->lp;
      int j, stat, fixed = 0;
      gnm_float obj, lb, ub, dj;
      /* the global bound must exist */
      insist(tree->found);
      /* basic solution of LP relaxation must be optimal */
      insist(lpx_get_status(lp) == LPX_OPT);
      /* determine the objective function value */
      obj = lpx_get_obj_val(lp);
      /* walk through the column list */
      for (j = 1; j <= n; j++)
      {  /* if j-th column is not of integer kind, skip it */
         if (!tree->int_col[j]) continue;
         /* obtain bounds of j-th column */
         lb = lpx_get_col_lb(lp, j);
         ub = lpx_get_col_ub(lp, j);
         /* and determine its status and reduced cost */
         stat = lpx_get_col_stat(lp, j);
         dj = lpx_get_col_dual(lp, j);
         /* analyze the reduced cost */
         switch (tree->dir)
         {  case LPX_MIN:
               /* minimization */
               if (stat == LPX_NL)
               {  /* j-th column is non-basic on its lower bound */
                  if (dj < 0.0) dj = 0.0;
                  if (obj + dj >= tree->best)
                     lpx_set_col_bnds(lp, j, LPX_FX, lb, lb), fixed++;
               }
               else if (stat == LPX_NU)
               {  /* j-th column is non-basic on its upper bound */
                  if (dj > 0.0) dj = 0.0;
                  if (obj - dj >= tree->best)
                     lpx_set_col_bnds(lp, j, LPX_FX, ub, ub), fixed++;
               }
               break;
            case LPX_MAX:
               /* maximization */
               if (stat == LPX_NL)
               {  /* j-th column is non-basic on its lower bound */
                  if (dj > 0.0) dj = 0.0;
                  if (obj + dj <= tree->best)
                     lpx_set_col_bnds(lp, j, LPX_FX, lb, lb), fixed++;
               }
               else if (stat == LPX_NU)
               {  /* j-th column is non-basic on its upper bound */
                  if (dj < 0.0) dj = 0.0;
                  if (obj - dj <= tree->best)
                     lpx_set_col_bnds(lp, j, LPX_FX, ub, ub), fixed++;
               }
               break;
            default:
               insist(tree != tree);
         }
      }
      if (tree->msg_lev >= 3)
      {  if (fixed == 0)
            /* nothing to say */;
         else if (fixed == 1)
            print("One column has been fixed by reduced cost");
         else
            print("%d columns have been fixed by reduced costs", fixed);
      }
      /* fixing non-basic columns on their current bounds does not
         change the basic solution itself, however, lpx_set_col_bnds
         resets the primal and dual statuses to undefined state, and
         therefore we need to restore them */
      lpx_put_solution(lp, LPX_P_FEAS, LPX_D_FEAS, NULL, NULL, NULL,
         NULL, NULL, NULL);
      return;
}

/*----------------------------------------------------------------------
-- branch_on - perform branching on specified column.
--
-- This routine performs branching on j-th column (structural variable)
-- of the current subproblem. The specified column must be of integer
-- kind and must have a fractional value in optimal basic solution of
-- LP relaxation of the current subproblem (i.e. only columns for which
-- the flag non_int[j] is set are valid candidates to branch on).
--
-- Let x be j-th structural variable, and beta be its primal fractional
-- value in the current basic solution. Branching on j-th variable is
-- dividing the current subproblem into two new subproblems, which are
-- identical to the current subproblem with the following exception: in
-- the first subproblem that begins the down-branch x has a new upper
-- bound x <= gnm_floor(beta), and in the second subproblem that begins the
-- up-branch x has a new lower bound x >= gnm_ceil(beta).
--
-- The parameter next specifies which subproblem should be solved next
-- to continue the search:
--
-- -1 means that one which begins the down-branch;
-- +1 means that one which begins the up-branch. */

static void branch_on(MIPTREE *tree, int j, int next)
{     LPX *lp = tree->lp;
      int p, type, clone[1+2];
      gnm_float beta, lb, ub, new_lb, new_ub;
      /* check input parameters for correctness */
      insist(1 <= j && j <= tree->n);
      insist(tree->non_int[j]);
      insist(next == -1 || next == +1);
      /* obtain primal value of j-th column in basic solution */
      beta = lpx_get_col_prim(lp, j);
      if (tree->msg_lev >= 3)
         print("Branching on column %d, primal value is %.9e", j, beta);
      /* determine the reference number of the current subproblem */
      insist(tree->curr != NULL);
      p = tree->curr->p;
      /* freeze the current subproblem */
      mip_freeze_node(tree);
      /* create two clones of the current subproblem; the first clone
         begins the down-branch, the second one begins the up-branch */
      mip_clone_node(tree, p, 2, clone);
      if (tree->msg_lev >= 3)
         print("Node %d begins down branch, node %d begins up branch",
            clone[1], clone[2]);
      /* set new upper bound of j-th column in the first subproblem */
      mip_revive_node(tree, clone[1]);
      type = lpx_get_col_type(lp, j);
      lb = lpx_get_col_lb(lp, j);
      ub = lpx_get_col_ub(lp, j);
      new_ub = gnm_floor(beta);
      switch (type)
      {  case LPX_FR:
            type = LPX_UP;
            break;
         case LPX_LO:
            insist(lb <= new_ub);
            type = (lb < new_ub ? LPX_DB : LPX_FX);
            break;
         case LPX_UP:
            insist(new_ub <= ub - 1.0);
            break;
         case LPX_DB:
            insist(lb <= new_ub && new_ub <= ub - 1.0);
            type = (lb < new_ub ? LPX_DB : LPX_FX);
            break;
         default:
            insist(type != type);
      }
      lpx_set_col_bnds(lp, j, type, lb, new_ub);
      mip_freeze_node(tree);
      /* set new lower bound of j-th column in the second subproblem */
      mip_revive_node(tree, clone[2]);
      type = lpx_get_col_type(lp, j);
      lb = lpx_get_col_lb(lp, j);
      ub = lpx_get_col_ub(lp, j);
      new_lb = gnm_ceil(beta);
      switch (type)
      {  case LPX_FR:
            type = LPX_LO;
            break;
         case LPX_LO:
            insist(lb + 1.0 <= new_lb);
            break;
         case LPX_UP:
            insist(new_lb <= ub);
            type = (new_lb < ub ? LPX_DB : LPX_FX);
            break;
         case LPX_DB:
            insist(lb + 1.0 <= new_lb && new_lb <= ub);
            type = (new_lb < ub ? LPX_DB : LPX_FX);
            break;
         default:
            insist(type != type);
      }
      lpx_set_col_bnds(lp, j, type, new_lb, ub);
      mip_freeze_node(tree);
      /* revive the subproblem to be solved next */
      mip_revive_node(tree, clone[next < 0 ? 1 : 2]);
      return;
}

/*----------------------------------------------------------------------
-- branch_first - choose first branching variable.
--
-- This routine looks up the list of structural variables and chooses
-- the first one, which is of integer kind and has fractional value in
-- optimal solution of the current LP relaxation.
--
-- This routine also selects the branch to be solved next where integer
-- infeasibility of the chosen variable is less than in other one. */

static void branch_first(MIPTREE *tree)
{     int n = tree->n;
      LPX *lp = tree->lp;
      int j, next;
      gnm_float beta;
      /* choose the column to branch on */
      for (j = 1; j <= n; j++)
         if (tree->non_int[j]) break;
      insist(1 <= j && j <= n);
      /* select the branch to be solved next */
      beta = lpx_get_col_prim(lp, j);
      if (beta - gnm_floor(beta) < gnm_ceil(beta) - beta)
         next = -1; /* down branch */
      else
         next = +1; /* up branch */
      /* perform branching */
      branch_on(tree, j, next);
      return;
}

/*----------------------------------------------------------------------
-- branch_last - choose last branching variable.
--
-- This routine looks up the list of structural variables and chooses
-- the last one, which is of integer kind and has fractional value in
-- optimal solution of the current LP relaxation.
--
-- This routine also selects the branch to be solved next where integer
-- infeasibility of the chosen variable is less than in other one. */

static void branch_last(MIPTREE *tree)
{     int n = tree->n;
      LPX *lp = tree->lp;
      int j, next;
      gnm_float beta;
      /* choose the column to branch on */
      for (j = n; j >= 1; j--)
         if (tree->non_int[j]) break;
      insist(1 <= j && j <= n);
      /* select the branch to be solved next */
      beta = lpx_get_col_prim(lp, j);
      if (beta - gnm_floor(beta) < gnm_ceil(beta) - beta)
         next = -1; /* down branch */
      else
         next = +1; /* up branch */
      /* perform branching */
      branch_on(tree, j, next);
      return;
}

/*----------------------------------------------------------------------
-- branch_drtom - choose branching variable with Driebeck-Tomlin heur.
--
-- This routine chooses a structural variable, which is required to be
-- integral and has fractional value in optimal solution of the current
-- LP relaxation, using a heuristic proposed by Driebeck and Tomlin.
--
-- The routine also selects the branch to be solved next, again due to
-- Driebeck and Tomlin.
--
-- This routine is based on the heuristic proposed in:
--
-- Driebeck N.J. An algorithm for the solution of mixed-integer
-- programming problems, Management Science, 12: 576-87 (1966)
--
-- and improved in:
--
-- Tomlin J.A. Branch and bound methods for integer and non-convex
-- programming, in J.Abadie (ed.), Integer and Nonlinear Programming,
-- North-Holland, Amsterdam, pp. 437-50 (1970).
--
-- Must note that this heuristic is time-expensive, because computing
-- one-step degradation (see the routine below) requires one BTRAN for
-- every fractional-valued structural variable. */

static void branch_drtom(MIPTREE *tree)
{     int m = tree->m;
      int n = tree->n;
      LPX *lp = tree->lp;
      int j, jj, k, t, next, kase, len, stat, *ind;
      gnm_float x, dk, alfa, delta_j, delta_k, delta_z, dz_dn, dz_up,
         dd_dn, dd_up, degrad, *val;
      /* basic solution of LP relaxation must be optimal */
      insist(lpx_get_status(lp) == LPX_OPT);
      /* allocate working arrays */
      ind = ucalloc(1+n, sizeof(int));
      val = ucalloc(1+n, sizeof(gnm_float));
      /* nothing has been chosen so far */
      jj = 0, degrad = -1.0;
      /* walk through the list of columns (structural variables) */
      for (j = 1; j <= n; j++)
      {  /* if j-th column is not marked as fractional, skip it */
         if (!tree->non_int[j]) continue;
         /* obtain (fractional) value of j-th column in basic solution
            of LP relaxation */
         x = lpx_get_col_prim(lp, j);
         /* since the value of j-th column is fractional, the column is
            basic; compute corresponding row of the simplex table */
         len = lpx_eval_tab_row(lp, m+j, ind, val);
         /* the following fragment computes a change in the objective
            function: delta Z = new Z - old Z, where old Z is the
            objective value in the current optimal basis, and new Z is
            the objective value in the adjacent basis, for two cases:
            1) if new upper bound ub' = gnm_floor(x[j]) is introduced for
               j-th column (down branch);
            2) if new lower bound lb' = gnm_ceil(x[j]) is introduced for
               j-th column (up branch);
            since in both cases the solution remaining dual feasible
            becomes primal infeasible, one implicit simplex iteration
            is performed to determine the change delta Z;
            it is obvious that new Z, which is never better than old Z,
            is a lower (minimization) or upper (maximization) bound of
            the objective function for down- and up-branches. */
         for (kase = -1; kase <= +1; kase += 2)
         {  /* if kase < 0, the new upper bound of x[j] is introduced;
               in this case x[j] should decrease in order to leave the
               basis and go to its new upper bound */
            /* if kase > 0, the new lower bound of x[j] is introduced;
               in this case x[j] should increase in order to leave the
               basis and go to its new lower bound */
            /* apply the dual ratio test in order to determine which
               auxiliary or structural variable should enter the basis
               to keep dual feasibility */
            k = lpx_dual_ratio_test(lp, len, ind, val, kase, 1e-8);
            /* if no non-basic variable has been chosen, LP relaxation
               of corresponding branch being primal infeasible and dual
               unbounded has no primal feasible solution; in this case
               the change delta Z is formally set to infinity */
            if (k == 0)
            {  delta_z = (tree->dir == LPX_MIN ? +DBL_MAX : -DBL_MAX);
               goto skip;
            }
            /* row of the simplex table that corresponds to non-basic
               variable x[k] choosen by the dual ratio test is:
                  x[j] = ... + alfa * x[k] + ...
               where alfa is the influence coefficient (an element of
               the simplex table row) */
            /* determine the coefficient alfa */
            for (t = 1; t <= len; t++) if (ind[t] == k) break;
            insist(1 <= t && t <= len);
            alfa = val[t];
            /* since in the adjacent basis the variable x[j] becomes
               non-basic, knowing its value in the current basis we can
               determine its change delta x[j] = new x[j] - old x[j] */
            delta_j = (kase < 0 ? gnm_floor(x) : gnm_ceil(x)) - x;
            /* and knowing the coefficient alfa we can determine the
               corresponding change delta x[k] = new x[k] - old x[k],
               where old x[k] is a value of x[k] in the current basis,
               and new x[k] is a value of x[k] in the adjacent basis */
            delta_k = delta_j / alfa;
            /* Tomlin noticed that if the variable x[k] is of integer
               kind, its change cannot be less (eventually) than one in
               the magnitude */
            if (k > m && tree->int_col[k-m])
            {  /* x[k] is structural integer variable */
               if (gnm_abs(delta_k - gnm_floor(delta_k + 0.5)) > 1e-3)
               {  if (delta_k > 0.0)
                     delta_k = gnm_ceil(delta_k);  /* +3.14 -> +4 */
                  else
                     delta_k = gnm_floor(delta_k); /* -3.14 -> -4 */
               }
            }
            /* now determine the status and reduced cost of x[k] in the
               current basis */
            if (k <= m)
            {  stat = lpx_get_row_stat(lp, k);
               dk = lpx_get_row_dual(lp, k);
            }
            else
            {  stat = lpx_get_col_stat(lp, k-m);
               dk = lpx_get_col_dual(lp, k-m);
            }
            /* if the current basis is dual degenerative, some reduced
               costs which are close to zero may have wrong sign due to
               round-off errors, so correct the sign of d[k] */
            switch (tree->dir)
            {  case LPX_MIN:
                  if (stat == LPX_NL && dk < 0.0 ||
                      stat == LPX_NU && dk > 0.0 ||
                      stat == LPX_NF) dk = 0.0;
                  break;
               case LPX_MAX:
                  if (stat == LPX_NL && dk > 0.0 ||
                      stat == LPX_NU && dk < 0.0 ||
                      stat == LPX_NF) dk = 0.0;
                  break;
               default:
                  insist(tree != tree);
            }
            /* now knowing the change of x[k] and its reduced cost d[k]
               we can compute the corresponding change in the objective
               function delta Z = new Z - old Z = d[k] * delta x[k];
               note that due to Tomlin's modification new Z can be even
               worse than in the adjacent basis */
            delta_z = dk * delta_k;
skip:       /* new Z is never better than old Z, therefore the change
               delta Z is always non-negative (in case of minimization)
               or non-positive (in case of maximization) */
            switch (tree->dir)
            {  case LPX_MIN: insist(delta_z >= 0.0); break;
               case LPX_MAX: insist(delta_z <= 0.0); break;
               default: insist(tree != tree);
            }
            /* save the change in the objective fnction for down- and
               up-branches, respectively */
            if (kase < 0) dz_dn = delta_z; else dz_up = delta_z;
         }
         /* thus, in down-branch no integer feasible solution can be
            better than Z + dz_dn, and in up-branch no integer feasible
            solution can be better than Z + dz_up, where Z is value of
            the objective function in the current basis */
         /* following the heuristic by Driebeck and Tomlin we choose a
            column (i.e. structural variable) which provides largest
            degradation of the objective function in some of branches;
            besides, we select the branch with smaller degradation to
            be solved next and keep other branch with larger degradation
            in the active list hoping to minimize the number of further
            backtrackings */
         if (degrad < gnm_abs(dz_dn) || degrad < gnm_abs(dz_up))
         {  jj = j;
            if (gnm_abs(dz_dn) < gnm_abs(dz_up))
            {  /* select down branch to be solved next */
               next = -1;
               degrad = gnm_abs(dz_up);
            }
            else
            {  /* select up branch to be solved next */
               next = +1;
               degrad = gnm_abs(dz_dn);
            }
            /* save the objective changes for printing */
            dd_dn = dz_dn, dd_up = dz_up;
            /* if down- or up-branch has no feasible solution, we does
               not need to consider other candidates (in principle, the
               corresponding branch could be pruned right now) */
            if (degrad == DBL_MAX) break;
         }
      }
      /* free working arrays */
      ufree(ind);
      ufree(val);
      /* something must be chosen */
      insist(1 <= jj && jj <= n);
      if (tree->msg_lev >= 3)
      {  print("branch_drtom: column %d chosen to branch on", jj);
         if (gnm_abs(dd_dn) == DBL_MAX)
            print("branch_drtom: down-branch is infeasible");
         else
            print("branch_drtom: down-branch bound is %.9e",
               lpx_get_obj_val(lp) + dd_dn);
         if (gnm_abs(dd_up) == DBL_MAX)
            print("branch_drtom: up-branch   is infeasible");
         else
            print("branch_drtom: up-branch   bound is %.9e",
               lpx_get_obj_val(lp) + dd_up);
      }
      /* perform branching */
      branch_on(tree, jj, next);
      return;
}

/*----------------------------------------------------------------------
-- branch_mostf - choose most fractional branching variable.
--
-- This routine looks up the list of structural variables and chooses
-- that one, which is of integer kind and has most fractional value in
-- optimal solution of the current LP relaxation.
--
-- This routine also selects the branch to be solved next where integer
-- infeasibility of the chosen variable is less than in other one. */

static void branch_mostf(MIPTREE *tree)
{     int n = tree->n;
      LPX *lp = tree->lp;
      int j, jj, next;
      gnm_float beta, most, temp;
      /* choose the column to branch on */
      jj = 0, most = DBL_MAX;
      for (j = 1; j <= n; j++)
      {  if (tree->non_int[j])
         {  beta = lpx_get_col_prim(lp, j);
            temp = gnm_floor(beta) + 0.5;
            if (most > gnm_abs(beta - temp))
            {  jj = j, most = gnm_abs(beta - temp);
               if (beta < temp)
                  next = -1; /* down branch */
               else
                  next = +1; /* up branch */
            }
         }
      }
      /* perform branching */
      branch_on(tree, jj, next);
      return;
}

/*----------------------------------------------------------------------
-- cleanup_the_tree - prune hopeless branches from the tree.
--
-- This routine walks through the active list and checks the local
-- bound for every active subproblem. If the local bound indicates that
-- the subproblem cannot have integer optimal solution better than the
-- incumbent objective value, the routine deletes such subproblem that,
-- in turn, involves pruning the corresponding branch of the tree. */

static void cleanup_the_tree(MIPTREE *tree)
{     MIPNODE *node, *next_node;
      int count = 0;
      /* the global bound must exist */
      insist(tree->found);
      /* walk through the list of active subproblems */
      for (node = tree->head; node != NULL; node = next_node)
      {  /* deleting some active problem node may involve deleting its
            parents recursively; however, all its parents being created
            *before* it are always *precede* it in the node list, so
            the next problem node is never affected by such deletion */
         next_node = node->next;
         /* if the branch is hopeless, prune it */
         if (!is_branch_hopeful(tree, node->p))
            mip_delete_node(tree, node->p), count++;
      }
      if (tree->msg_lev >= 3)
      {  if (count == 1)
            print("One hopeless branch has been pruned");
         else if (count > 1)
            print("%d hopeless branches have been pruned", count);
      }
      return;
}

/*----------------------------------------------------------------------
-- btrack_most_feas - select "most integer feasible" subproblem.
--
-- This routine selects from the active list a subproblem to be solved
-- next, whose parent has minimal sum of integer infeasibilities. */

static void btrack_most_feas(MIPTREE *tree)
{     MIPNODE *node;
      int p;
      gnm_float best;
      p = 0, best = DBL_MAX;
      for (node = tree->head; node != NULL; node = node->next)
      {  insist(node->up != NULL);
         if (best > node->up->ii_sum)
            p = node->p, best = node->up->ii_sum;
      }
      mip_revive_node(tree, p);
      return;
}

/*----------------------------------------------------------------------
-- btrack_best_proj - select subproblem with best projection heur.
--
-- This routine selects from the active list a subproblem to be solved
-- next using the best projection heuristic. */

static void btrack_best_proj(MIPTREE *tree)
{     MIPNODE *root, *node;
      int p;
      gnm_float best, deg, obj;
      /* the global bound must exist */
      insist(tree->found);
      /* obtain pointer to the root node, which must exist */
      root = tree->slot[1].node;
      insist(root != NULL);
      /* deg estimates degradation of the objective function per unit
         of the sum of integer infeasibilities */
      insist(root->ii_sum > 0.0);
      deg = (tree->best - root->bound) / root->ii_sum;
      /* nothing has been selected so far */
      p = 0, best = DBL_MAX;
      /* walk through the list of active subproblems */
      for (node = tree->head; node != NULL; node = node->next)
      {  insist(node->up != NULL);
         /* obj estimates optimal objective value if the sum of integer
            infeasibilities were zero */
         obj = node->up->bound + deg * node->up->ii_sum;
         if (tree->dir == LPX_MAX) obj = - obj;
         /* select the subproblem which has the best estimated optimal
            objective value */
         if (best > obj) p = node->p, best = obj;
      }
      mip_revive_node(tree, p);
      return;
}

/*----------------------------------------------------------------------
-- mip_driver - branch-and-bound driver.
--
-- *Synopsis*
--
-- #include "glpmip.h"
-- int mip_driver(MIPTREE *tree);
--
-- *Description*
--
-- The routine mip_driver is a branch-and-bound driver that manages the
-- process of solving MIP problem instance.
--
-- *Returns*
--
-- The routine mip_driver returns one of the following exit codes:
--
-- MIP_E_OK       the search is finished;
--
-- MIP_E_ITLIM    iterations limit exceeded;
--
-- MIP_E_TMLIM    time limit exceeded;
--
-- MIP_E_ERROR    an error occurred on solving the LP relaxation of the
--                current subproblem.
--
-- Should note that additional exit codes may appear in future versions
-- of this routine. */

int mip_driver(MIPTREE *tree)
{     LPX *lp = tree->lp;
      int p, p_stat, d_stat, ret;
      /* revive the root subproblem */
      mip_revive_node(tree, 1);
loop: /* main loop starts here; at this point some subproblem has been
         just selected from the active list and made current */
      insist(tree->curr != NULL);
      /* determine the reference number of the current subproblem */
      p = tree->curr->p;
      if (tree->msg_lev >= 3)
      {  int level = tree->slot[p].node->level;
         print("-------------------------------------------------------"
            "-----------------");
         print("Processing node %d at level %d", p, level);
      }
      /* check if the time limit has been exhausted */
      if (tree->tm_lim >= 0.0 && tree->tm_lim <= utime() - tree->tm_beg)
      {  if (tree->msg_lev >= 3)
            print("Time limit exceeded; search terminated");
         ret = MIP_E_TMLIM;
         goto done;
      }
      /* display current progress of the search */
      if (tree->msg_lev >= 3 || tree->msg_lev >= 2 &&
          utime() - tree->tm_lag >= tree->out_frq - 0.001)
         show_progress(tree);
      /* solve LP relaxation of the current subproblem */
      if (tree->msg_lev >= 3)
         print("Solving LP relaxation...");
      ret = mip_solve_node(tree);
      switch (ret)
      {  case LPX_E_OK:
         case LPX_E_OBJLL:
         case LPX_E_OBJUL:
            break;
         case LPX_E_ITLIM:
            if (tree->msg_lev >= 3)
               print("Iterations limit exceeded; search terminated");
            ret = MIP_E_ITLIM;
            goto done;
         default:
            if (tree->msg_lev >= 1)
               print("mip_driver: cannot solve current LP relaxation");
            ret = MIP_E_ERROR;
            goto done;
      }
      /* analyze status of the basic solution */
      p_stat = lpx_get_prim_stat(lp);
      d_stat = lpx_get_dual_stat(lp);
      if (p_stat == LPX_P_FEAS && d_stat == LPX_D_FEAS)
      {  /* LP relaxation has optimal solution */
         if (tree->msg_lev >= 3)
            print("Found optimal solution to LP relaxation");
      }
      else if (p_stat == LPX_P_FEAS && d_stat == LPX_D_NOFEAS)
      {  /* LP relaxation has unbounded solution */
         /* since the current subproblem cannot have a larger feasible
            region than its parent, there is something wrong */
         if (tree->msg_lev >= 1)
            print("mip_driver: current LP relaxation has unbounded solu"
               "tion");
         ret = MIP_E_ERROR;
         goto done;
      }
      else if (p_stat == LPX_P_INFEAS && d_stat == LPX_D_FEAS)
      {  /* LP relaxation has no primal solution which is better than
            the incumbent objective value */
         insist(tree->found);
         if (tree->msg_lev >= 3)
            print("LP relaxation has no solution better than incumbent "
               "objective value");
         /* prune the branch */
         goto fath;
      }
      else if (p_stat == LPX_P_NOFEAS)
      {  /* LP relaxation has no primal feasible solution */
         if (tree->msg_lev >= 3)
            print("LP relaxation has no feasible solution");
         /* prune the branch */
         goto fath;
      }
      else
      {  /* other cases cannot appear */
         insist(lp != lp);
      }
      /* at this point basic solution of LP relaxation of the current
         subproblem is optimal */
      insist(p_stat == LPX_P_FEAS && d_stat == LPX_D_FEAS);
      /* thus, it defines a local bound for integer optimal solution of
         the current subproblem */
      set_local_bound(tree);
      /* if the local bound indicates that integer optimal solution of
         the current subproblem cannot be better than the global bound,
         prune the branch */
      if (!is_branch_hopeful(tree, p))
      {  if (tree->msg_lev >= 3)
            print("Current branch is hopeless and can be pruned");
         goto fath;
      }
      /* check integrality of the basic solution */
      check_integrality(tree);
      /* if the basic solution satisfies to all integrality conditions,
         it is a new, better integer feasible solution */
      if (tree->curr->ii_cnt == 0)
      {  if (tree->msg_lev >= 3)
            print("New integer feasible solution found");
         record_solution(tree);
         if (tree->msg_lev >= 2) show_progress(tree);
         /* the current subproblem is fathomed; prune its branch */
         goto fath;
      }
      /* basic solution of LP relaxation of the current subproblem is
         integer infeasible */
      /* try to fix some non-basic structural variables of integer kind
         on their current bounds using reduced costs */
      if (tree->found) fix_by_red_cost(tree);
      /* perform branching */
      switch (tree->branch)
      {  case 0:
            /* branch on first appropriate variable */
            branch_first(tree);
            break;
         case 1:
            /* branch on last appropriate variable */
            branch_last(tree);
            break;
         case 2:
            /* branch using the heuristic by Dreebeck and Tomlin */
            branch_drtom(tree);
            break;
         case 3:
            /* branch on most fractional variable */
            branch_mostf(tree);
            break;
         default:
            insist(tree != tree);
      }
      /* continue the search from the subproblem which begins down- or
         up-branch (it has been revived by branching routine) */
      goto loop;
fath: /* the current subproblem has been fathomed */
      if (tree->msg_lev >= 3)
         print("Node %d fathomed", p);
      /* freeze the current subproblem */
      mip_freeze_node(tree);
      /* and prune the corresponding branch of the tree */
      mip_delete_node(tree, p);
      /* if a new integer feasible solution has just been found, other
         branches may become hopeless and therefore should be pruned */
      if (tree->found) cleanup_the_tree(tree);
      /* if the active list is empty, the search is finished */
      if (tree->head == NULL)
      {  if (tree->msg_lev >= 3)
            print("Active list is empty!");
         insist(tree->node_pool->count == 0);
         insist(tree->bnds_pool->count == 0);
         insist(tree->stat_pool->count == 0);
         ret = MIP_E_OK;
         goto done;
      }
      /* perform backtracking */
      switch (tree->btrack)
      {  case 0:
            /* depth first search */
            insist(tree->tail != NULL);
            mip_revive_node(tree, tree->tail->p);
            break;
         case 1:
            /* breadth first search */
            insist(tree->head != NULL);
            mip_revive_node(tree, tree->head->p);
            break;
         case 2:
            if (!tree->found)
            {  /* "most integer feasible" subproblem */
               btrack_most_feas(tree);
            }
            else
            {  /* best projection heuristic */
               btrack_best_proj(tree);
            }
            break;
         case 3:
            /* select node with best local bound */
            mip_revive_node(tree, mip_best_node(tree));
            break;
         default:
            insist(tree != tree);
      }
      /* continue the search from the subproblem selected */
      goto loop;
done: /* display status of the search on exit from the solver */
      if (tree->msg_lev >= 2) show_progress(tree);
      /* decrease the time limit by spent amount of time */
      if (tree->tm_lim >= 0.0)
      {  tree->tm_lim -= (utime() - tree->tm_beg);
         if (tree->tm_lim < 0.0) tree->tm_lim = 0.0;
      }
      /* return to the calling program */
      return ret;
}

/* eof */
