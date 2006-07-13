/* glplpx6d.c (advanced branch-and-bound solver) */

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
#include <stdio.h>
#include <string.h>
#include "glpipp.h"
#include "glplib.h"

/*----------------------------------------------------------------------
-- show_status - display current status of the problem.
--
-- This routine displays some information about current status of the
-- problem.
--
-- This information includes:
--
-- the number of iterations performed by the simplex solver;
--
-- the current objective value;
--
-- the number of integral columns whose current values are fractional;
--
-- the number of cutting planes generated;
--
-- the number of non-zero coefficients in all cut inequalities. */

static void show_status(LPX *prob, int prob_m, int prob_nz)
{     int n, j, count;
      gnm_float x, tol_int;
      /* determine the number of structural variables of integer kind
         whose current values are still fractional */
      n = lpx_get_num_cols(prob);
      tol_int = lpx_get_real_parm(prob, LPX_K_TOLINT);
      count = 0;
      for (j = 1; j <= n; j++)
      {  if (lpx_get_col_kind(prob, j) != LPX_IV) continue;
         x = lpx_get_col_prim(prob, j);
         if (gnm_abs(x - gnm_floor(x + 0.5)) <= tol_int) continue;
         count++;
      }
      print("&%6d: obj = %17.9e   frac = %5d   cuts = %5d (%d)",
         lpx_get_int_parm(prob, LPX_K_ITCNT),
         lpx_get_obj_val(prob), count,
         lpx_get_num_rows(prob) - prob_m,
         lpx_get_num_nz(prob) - prob_nz);
      return;
}

/*----------------------------------------------------------------------
-- gen_gomory_cut - try to generate Gomory's mixed integer cut.
--
-- This routine generates Gomory's mixed integer cuts by one for each
-- integer variable having fractional value in the current solution and
-- chooses the cut providing either maximal degradation of the objective
-- or maximal scaled residual. Once the cut has been chosen, the routine
-- adds it to the problem. */

static void gen_gomory_cut(LPX *prob, int maxlen)
{     int m = lpx_get_num_rows(prob);
      int n = lpx_get_num_cols(prob);
      int i, j, k, len, cut_j, *ind;
      gnm_float x, d, r, temp, cut_d, cut_r, *val, *work;
      insist(lpx_get_status(prob) == LPX_OPT);
      /* allocate working arrays */
      ind = ucalloc(1+n, sizeof(int));
      val = ucalloc(1+n, sizeof(gnm_float));
      work = ucalloc(1+m+n, sizeof(gnm_float));
      /* nothing is chosen so far */
      cut_j = 0; cut_d = 0.0; cut_r = 0.0;
      /* look through all structural variables */
      for (j = 1; j <= n; j++)
      {  /* if the variable is continuous, skip it */
         if (lpx_get_col_kind(prob, j) != LPX_IV) continue;
         /* if the variable is non-basic, skip it */
         if (lpx_get_col_stat(prob, j) != LPX_BS) continue;
         /* if the variable is fixed, skip it */
         if (lpx_get_col_type(prob, j) == LPX_FX) continue;
         /* obtain current primal value of the variable */
         x = lpx_get_col_prim(prob, j);
         /* if the value is close enough to nearest integer, skip the
            variable */
         if (gnm_abs(x - gnm_floor(x + 0.5)) < 1e-4) continue;
         /* compute the row of the simplex table corresponding to the
            variable */
         len = lpx_eval_tab_row(prob, m+j, ind, val);
         len = lpx_remove_tiny(len, ind, NULL, val, 1e-10);
         /* generate Gomory's mixed integer cut:
            a[1]*x[1] + ... + a[n]*x[n] >= b */
         len = lpx_gomory_cut(prob, len, ind, val, work);
         if (len < 0) continue;
         insist(0 <= len && len <= n);
         len = lpx_remove_tiny(len, ind, NULL, val, 1e-10);
         if (gnm_abs(val[0]) < 1e-10) val[0] = 0.0;
         /* if the cut is too long, skip it */
         if (len > maxlen) continue;
         /* if the cut contains coefficients with too large magnitude,
            do not use it to prevent numeric instability */
         for (k = 0; k <= len; k++) /* including rhs */
            if (gnm_abs(val[k]) > 1e+6) break;
         if (k <= len) continue;
         /* at the current point the cut inequality is violated, i.e.
            the residual b - (a[1]*x[1] + ... + a[n]*x[n]) > 0; note
            that for Gomory's cut the residual is less than 1.0 */
         /* in order not to depend on the magnitude of coefficients we
            use scaled residual:
            r = [b - (a[1]*x[1] + ... + a[n]*x[n])] / max(1, |a[j]|) */
         temp = 1.0;
         for (k = 1; k <= len; k++)
            if (temp < gnm_abs(val[k])) temp = gnm_abs(val[k]);
         r = (val[0] - lpx_eval_row(prob, len, ind, val)) / temp;
         if (r < 1e-5) continue;
         /* estimate degradation (worsening) of the objective function
            by one dual simplex step if the cut row would be introduced
            in the problem */
         d = lpx_eval_degrad(prob, len, ind, val, LPX_LO, val[0]);
         /* ignore the sign of degradation */
         d = gnm_abs(d);
         /* which cut should be used? there are two basic cases:
            1) if the degradation is non-zero, we are interested in a
               cut providing maximal degradation;
            2) if the degradation is zero (i.e. a non-basic variable
               which would enter the basis in the adjacent vertex has
               zero reduced cost), we are interested in a cut providing
               maximal scaled residual;
            in both cases it is desired that the cut length (the number
            of inequality coefficients) is possibly short */
         /* if both degradation and scaled residual are small, skip the
            cut */
         if (d < 0.001 && r < 0.001)
            continue;
         /* if there is no cut chosen, choose this cut */
         else if (cut_j == 0)
            ;
         /* if this cut provides stronger degradation and has shorter
            length, choose it */
         else if (cut_d != 0.0 && cut_d < d)
            ;
         /* if this cut provides larger scaled residual and has shorter
            length, choose it */
         else if (cut_d == 0.0 && cut_r < r)
            ;
         /* otherwise skip the cut */
         else
            continue;
         /* save attributes of the cut choosen */
         cut_j = j, cut_r = r, cut_d = d;
      }
      /* if a cut has been chosen, include it to the problem */
      if (cut_j != 0)
      {  j = cut_j;
         /* compute the row of the simplex table */
         len = lpx_eval_tab_row(prob, m+j, ind, val);
         len = lpx_remove_tiny(len, ind, NULL, val, 1e-10);
         /* generate the cut */
         len = lpx_gomory_cut(prob, len, ind, val, work);
         insist(0 <= len && len <= n);
         len = lpx_remove_tiny(len, ind, NULL, val, 1e-10);
         if (gnm_abs(val[0]) < 1e-10) val[0] = 0.0;
         /* include the corresponding row in the problem */
         i = lpx_add_rows(prob, 1);
         lpx_set_row_bnds(prob, i, LPX_LO, val[0], 0.0);
         lpx_set_mat_row(prob, i, len, ind, val);
      }
      /* free working arrays */
      ufree(ind);
      ufree(val);
      ufree(work);
      return;
}

/*----------------------------------------------------------------------
-- generate_cuts - generate cutting planes.
--
-- This routine generates cutting planes and add them to the specified
-- problem object to improve LP relaxation. */

static int generate_cuts(LPX *prob)
{     int prob_m, prob_n, prob_nz, msg_lev, dual, nrows, it_cnt, ret;
      gnm_float out_dly, tm_lim, tm_lag = 0.0, tm_beg = utime();
      print("Generating cutting planes...");
      /* determine the number of rows, columns, and non-zeros on entry
         to the routine */
      prob_m = lpx_get_num_rows(prob);
      prob_n = lpx_get_num_cols(prob);
      prob_nz = lpx_get_num_nz(prob);
      /* save some control parameters */
      msg_lev = lpx_get_int_parm(prob, LPX_K_MSGLEV);
      dual = lpx_get_int_parm(prob, LPX_K_DUAL);
      out_dly = lpx_get_real_parm(prob, LPX_K_OUTDLY);
      tm_lim = lpx_get_real_parm(prob, LPX_K_TMLIM);
      /* and set their new values needed for re-optimization */
      lpx_set_int_parm(prob, LPX_K_MSGLEV, 1);
      lpx_set_int_parm(prob, LPX_K_DUAL, 1);
      lpx_set_real_parm(prob, LPX_K_OUTDLY, 10.0);
      lpx_set_real_parm(prob, LPX_K_TMLIM, -1.0);
loop: /* main loop starts here */
      /* display current status of the problem */
      if (utime() - tm_lag >= 5.0 - 0.001)
         show_status(prob, prob_m, prob_nz), tm_lag = utime();
      /* check if the patience has been exhausted */
      if (tm_lim >= 0.0 && tm_lim <= utime() - tm_beg)
      {  ret = LPX_E_TMLIM;
         goto done;
      }
      /* not more than 500 cut inequalities are allowed */
      if (lpx_get_num_rows(prob) - prob_m >= 500)
      {  ret = LPX_E_OK;
         goto done;
      }
      /* not more than 50,000 cut coefficients are allowed */
      if (lpx_get_num_nz(prob) - prob_nz >= 50000)
      {  ret = LPX_E_OK;
         goto done;
      }
      /* try to generate Gomory's mixed integer cut */
      nrows = lpx_get_num_rows(prob);
      gen_gomory_cut(prob, prob_n);
      if (nrows == lpx_get_num_rows(prob))
      {  /* nothing has been generated */
         ret = LPX_E_OK;
         goto done;
      }
      /* re-optimize current LP relaxation using dual simplex */
      it_cnt = lpx_get_int_parm(prob, LPX_K_ITCNT);
      switch (lpx_simplex(prob))
      {  case LPX_E_OK:
            break;
         case LPX_E_ITLIM:
            ret = LPX_E_ITLIM;
            goto done;
         default:
            ret = LPX_E_SING;
            goto done;
      }
      if (it_cnt == lpx_get_int_parm(prob, LPX_K_ITCNT))
      {  ret = LPX_E_OK;
         goto done;
      }
      /* analyze status of the basic solution */
      switch (lpx_get_status(prob))
      {  case LPX_OPT:
            break;
         case LPX_NOFEAS:
            ret = LPX_E_NOPFS;
            goto done;
         default:
            insist(prob != prob);
      }
      /* continue generating cutting planes */
      goto loop;
done: /* display final status of the problem */
      show_status(prob, prob_m, prob_nz);
      switch (ret)
      {  case LPX_E_OK:
            break;
         case LPX_E_NOPFS:
            print("PROBLEM HAS NO INTEGER FEASIBLE SOLUTION");
            break;
         case LPX_E_ITLIM:
            print("ITERATIONS LIMIT EXCEEDED; SEARCH TERMINATED");
            break;
         case LPX_E_TMLIM:
            print("TIME LIMIT EXCEEDED; SEARCH TERMINATED");
            break;
         case LPX_E_SING:
            print("lpx_intopt: cannot re-optimize LP relaxation");
            break;
         default:
            insist(ret != ret);
      }
      /* decrease the time limit by spent amount of the time */
      if (tm_lim >= 0.0)
      {  tm_lim -= (utime() - tm_beg);
         if (tm_lim < 0.0) tm_lim = 0.0;
      }
      /* restore some control parameters and update statistics */
      lpx_set_int_parm(prob, LPX_K_MSGLEV, msg_lev);
      lpx_set_int_parm(prob, LPX_K_DUAL, dual);
      lpx_set_real_parm(prob, LPX_K_OUTDLY, out_dly);
      lpx_set_real_parm(prob, LPX_K_TMLIM, tm_lim);
      return ret;
}

/*----------------------------------------------------------------------
-- lpx_intopt - advanced branch-and-bound solver.
--
-- SYNOPSIS
--
-- #include "glplpx.h"
-- int lpx_intopt(LPX *mip);
--
-- DESCRIPTION
--
-- The routine lpx_intopt is intended for solving a MIP problem, which
-- is specified by the parameter mip.
--
-- Unlike the routine lpx_integer this routine needs no LP relaxation.
--
-- RETURNS
--
-- The routine lpx_intopt returns one of the following exit codes:
--
-- LPX_E_OK       the MIP problem has been successfully solved.
--
-- LPX_E_FAULT    the solver cannot start the search because either
--                the problem is not of MIP class, or
--                some integer variable has non-integer lower or upper
--                bound.
--
-- LPX_E_NOPFS    the MIP problem has no primal feasible solution.
--
-- LPX_E_NODFS    LP relaxation of the MIP problem has no dual feasible
--                solution.
--
-- LPX_E_ITLIM    iterations limit exceeded.
--
-- LPX_E_TMLIM    time limit exceeded.
--
-- LPX_E_SING     an error occurred on solving LP relaxation of some
--                subproblem during branch-and-bound.
--
-- Should note that additional exit codes may appear in future versions
-- of this routine. */

int lpx_intopt(LPX *_mip)
{     IPP *ipp = NULL;
      LPX *orig = _mip, *prob = NULL;
      int orig_m, orig_n, i, j, ret, i_stat;
      /* the problem must be of MIP class */
      if (lpx_get_class(orig) != LPX_MIP)
      {  print("lpx_intopt: problem is not of MIP class");
         ret = LPX_E_FAULT;
         goto done;
      }
      /* the problem must have at least one row and one column */
      orig_m = lpx_get_num_rows(orig);
      orig_n = lpx_get_num_cols(orig);
      if (!(orig_m > 0 && orig_n > 0))
      {  print("lpx_intopt: problem has no rows/columns");
         ret = LPX_E_FAULT;
         goto done;
      }
      /* check that each gnm_float-bounded row and column has bounds */
      for (i = 1; i <= orig_m; i++)
      {  if (lpx_get_row_type(orig, i) == LPX_DB)
         {  if (lpx_get_row_lb(orig, i) >= lpx_get_row_ub(orig, i))
            {  print("lpx_intopt: row %d has incorrect bounds", i);
               ret = LPX_E_FAULT;
               goto done;
            }
         }
      }
      for (j = 1; j <= orig_n; j++)
      {  if (lpx_get_col_type(orig, j) == LPX_DB)
         {  if (lpx_get_col_lb(orig, j) >= lpx_get_col_ub(orig, j))
            {  print("lpx_intopt: column %d has incorrect bounds", j);
               ret = LPX_E_FAULT;
               goto done;
            }
         }
      }
      /* bounds of all integer variables must be integral */
      for (j = 1; j <= orig_n; j++)
      {  int type;
         gnm_float lb, ub;
         if (lpx_get_col_kind(orig, j) != LPX_IV) continue;
         type = lpx_get_col_type(orig, j);
         if (type == LPX_LO || type == LPX_DB || type == LPX_FX)
         {  lb = lpx_get_col_lb(orig, j);
            if (lb != gnm_floor(lb))
            {  print("lpx_intopt: integer column %d has non-integer low"
                  "er bound or fixed value %g", j, lb);
               ret = LPX_E_FAULT;
               goto done;
            }
         }
         if (type == LPX_UP || type == LPX_DB)
         {  ub = lpx_get_col_ub(orig, j);
            if (ub != gnm_floor(ub))
            {  print("lpx_intopt: integer column %d has non-integer upp"
                  "er bound %g", j, ub);
               ret = LPX_E_FAULT;
               goto done;
            }
         }
      }
      /* reset the status of MIP solution */
      lpx_put_mip_soln(orig, LPX_I_UNDEF, NULL, NULL);
      /* create MIP presolver workspace */
      ipp = ipp_create_wksp();
      /* load the original problem into the presolver workspace */
      ipp_load_orig(ipp, orig);
      /* perform basic MIP presolve analysis */
      switch (ipp_basic_tech(ipp))
      {  case 0:
            /* no infeasibility is detected */
            break;
         case 1:
nopfs:      /* primal infeasibility is detected */
            print("PROBLEM HAS NO PRIMAL FEASIBLE SOLUTION");
            ret = LPX_E_NOPFS;
            goto done;
         case 2:
            /* dual infeasibility is detected */
nodfs:      print("LP RELAXATION HAS NO DUAL FEASIBLE SOLUTION");
            ret = LPX_E_NODFS;
            goto done;
         default:
            insist(ipp != ipp);
      }
      /* reduce column bounds */
      switch (ipp_reduce_bnds(ipp))
      {  case 0:  break;
         case 1:  goto nopfs;
         default: insist(ipp != ipp);
      }
      /* perform basic MIP presolve analysis */
      switch (ipp_basic_tech(ipp))
      {  case 0:  break;
         case 1:  goto nopfs;
         case 2:  goto nodfs;
         default: insist(ipp != ipp);
      }
      /* replace general integer variables by sum of binary variables,
         if required */
      if (lpx_get_int_parm(orig, LPX_K_BINARIZE))
         ipp_binarize(ipp);
      /* perform coefficient reduction */
      ipp_reduction(ipp);
      /* if the resultant problem is empty, it has an empty solution,
         which is optimal */
      if (ipp->row_ptr == NULL || ipp->col_ptr == NULL)
      {  insist(ipp->row_ptr == NULL);
         insist(ipp->col_ptr == NULL);
         print("Objective value = %.10g",
            ipp->orig_dir == LPX_MIN ? +ipp->c0 : -ipp->c0);
         print("INTEGER OPTIMAL SOLUTION FOUND BY MIP PRESOLVER");
         /* allocate recovered solution segment */
         ipp->col_stat = ucalloc(1+ipp->ncols, sizeof(int));
         ipp->col_mipx = ucalloc(1+ipp->ncols, sizeof(gnm_float));
         for (j = 1; j <= ipp->ncols; j++) ipp->col_stat[j] = 0;
         /* perform MIP postsolve processing */
         ipp_postsolve(ipp);
         /* unload recovered MIP solution and store it in the original
            problem object */
         ipp_unload_sol(ipp, orig, LPX_I_OPT);
         ret = LPX_E_OK;
         goto done;
      }
      /* build resultant MIP problem object */
      prob = ipp_build_prob(ipp);
      /* display some statistics */
      {  int m = lpx_get_num_rows(prob);
         int n = lpx_get_num_cols(prob);
         int nnz = lpx_get_num_nz(prob);
         int ni = lpx_get_num_int(prob);
         int nb = lpx_get_num_bin(prob);
         char s[50];
         print("lpx_intopt: presolved MIP has %d row%s, %d column%s, %d"
            " non-zero%s", m, m == 1 ? "" : "s", n, n == 1 ? "" : "s",
            nnz, nnz == 1 ? "" : "s");
         if (nb == 0)
            strcpy(s, "none of");
         else if (ni == 1 && nb == 1)
            strcpy(s, "");
         else if (nb == 1)
            strcpy(s, "one of");
         else if (nb == ni)
            strcpy(s, "all of");
         else
            sprintf(s, "%d of", nb);
         print("lpx_intopt: %d integer column%s, %s which %s binary",
            ni, ni == 1 ? "" : "s", s, nb == 1 ? "is" : "are");
      }
      /* inherit some control parameters and statistics */
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
      lpx_set_int_parm(prob, LPX_K_ITLIM, lpx_get_int_parm(orig,
         LPX_K_ITLIM));
      lpx_set_int_parm(prob, LPX_K_ITCNT, lpx_get_int_parm(orig,
         LPX_K_ITCNT));
      lpx_set_real_parm(prob, LPX_K_TMLIM, lpx_get_real_parm(orig,
         LPX_K_TMLIM));
      lpx_set_int_parm(prob, LPX_K_BRANCH, lpx_get_int_parm(orig,
         LPX_K_BRANCH));
      lpx_set_int_parm(prob, LPX_K_BTRACK, lpx_get_int_parm(orig,
         LPX_K_BTRACK));
      lpx_set_real_parm(prob, LPX_K_TOLINT, lpx_get_real_parm(orig,
         LPX_K_TOLINT));
      lpx_set_real_parm(prob, LPX_K_TOLOBJ, lpx_get_real_parm(orig,
         LPX_K_TOLOBJ));
      /* build an advanced initial basis */
      lpx_adv_basis(prob);
      /* solve LP relaxation */
      print("Solving LP relaxation...");
      switch (lpx_simplex(prob))
      {  case LPX_E_OK:
            break;
         case LPX_E_ITLIM:
            ret = LPX_E_ITLIM;
            goto done;
         case LPX_E_TMLIM:
            ret = LPX_E_TMLIM;
            goto done;
         default:
            print("lpx_intopt: cannot solve LP relaxation");
            ret = LPX_E_SING;
            goto done;
      }
      /* analyze status of the basic solution */
      switch (lpx_get_status(prob))
      {  case LPX_OPT:
            break;
         case LPX_NOFEAS:
            ret = LPX_E_NOPFS;
            goto done;
         case LPX_UNBND:
            ret = LPX_E_NODFS;
            goto done;
         default:
            insist(prob != prob);
      }
      /* generate cutting planes, if necessary */
      if (lpx_get_int_parm(orig, LPX_K_USECUTS))
      {  ret =  generate_cuts(prob);
         if (ret != LPX_E_OK) goto done;
      }
      /* call the branch-and-bound solver */
      ret = lpx_integer(prob);
      /* determine status of MIP solution */
      i_stat = lpx_mip_status(prob);
      if (i_stat == LPX_I_OPT || i_stat == LPX_I_FEAS)
      {  /* load MIP solution of the resultant problem into presolver
            workspace */
         ipp_load_sol(ipp, prob);
         /* perform MIP postsolve processing */
         ipp_postsolve(ipp);
         /* unload recovered MIP solution and store it in the original
            problem object */
         ipp_unload_sol(ipp, orig, i_stat);
      }
      else
      {  /* just set the status of MIP solution */
         lpx_put_mip_soln(orig, i_stat, NULL, NULL);
      }
done: /* copy back statistics about spent resources */
      if (prob != NULL)
      {  lpx_set_int_parm(orig, LPX_K_ITLIM, lpx_get_int_parm(prob,
            LPX_K_ITLIM));
         lpx_set_int_parm(orig, LPX_K_ITCNT, lpx_get_int_parm(prob,
            LPX_K_ITCNT));
         lpx_set_real_parm(orig, LPX_K_TMLIM, lpx_get_real_parm(prob,
            LPX_K_TMLIM));
      }
      /* delete the resultant problem object */
      if (prob != NULL) lpx_delete_prob(prob);
      /* delete MIP presolver workspace */
      if (ipp != NULL) ipp_delete_wksp(ipp);
      return ret;
}

/* eof */
