/* glprsm2.c */

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
#include <stddef.h>
#include "glprsm.h"

/*----------------------------------------------------------------------
-- rsm_feas - find feasible solution using primal simplex method.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- int rsm_feas(RSM *rsm, int (*monit)(void),
--    gnum_float tol_bnd, gnum_float tol_dj, gnum_float tol_piv, gnum_float gvec[],
--    int relax);
--
-- *Description*
--
-- The rsm_feas routine searches for primal feasible solution of LP
-- problem using the method of implicit artificial variables based on
-- the primal simplex method.
--
-- The parameter rsm points to the common block. On entry this block
-- specifies some initial basis solution which is assumed to be primal
-- infeasible. On exit this block specifies the last reached basis
-- solution.
--
-- The parameter monit specifies the user-supplied routine used for
-- monitoring purposes. It is called by the rsm_feas routine each time
-- before the next iteration. If the monit routine returns non-zero, the
-- search is terminated. The parameter monit can be set to NULL.
--
-- The parameter tol_bnd is a relative tolerance which is used by the
-- routine in order to see if the current solution is primal feasible.
--
-- The parameter tol_dj is a relative tolerance which is used by the
-- routine in order to see if there is no primal feasible solution
-- (i.e. if the current solution is optimal for some auxiliary objective
-- function which is the sum of residuals).
--
-- The parameter tol_piv is a relative tolerance which is used by the
-- routine in order to choose the pivot element of the simplex table.
--
-- If the parameter gvec is NULL, the routine chooses non-basic variable
-- using the standard technique. Otherwise the routine uses the steepest
-- edge technique. In the latter case the array gvec should specifiy
-- elements of the vector gamma in locations gvec[1], ..., gvec[n]. On
-- entry the vector gamma should correspond to the initial basis passed
-- to the routine. This vector is updated on every iteration, therefore
-- on exit it corresponds to the final basis reached by the routine.
--
-- The parameter relax is a flag. If it is zero, the routine chooses
-- basic variable using the standard ratio test. Otherwise the routine
-- uses two-pass ratio test.
--
-- *Returns*
--
-- The rsm_feas routine returns one of the following codes:
--
-- 0 - primal feasible solution found;
-- 1 - problem has no (primal) feasible solution;
-- 2 - numerical stability lost (the current basis solution of some
--     auxiliary LP problem became infeasible due to round-off errors);
-- 3 - numerical problems with basis matrix (the current basis matrix
--     became singular or ill-conditioned due to unsuccessful choice of
--     the pivot element on the last simplex iteration);
-- 4 - user-supplied routine returned non-zero code.
--
-- *Method*
--
-- The rsm_feas routine searches for primal feasible solution of LP
-- problem using the method of implicit artificial variables based on
-- the primal simplex method.
--
-- At first the routine computes current values of the basic variables
-- and replaces basic variables which violate their bounds, by implicit
-- artificial variables in order to construct feasible basis solution:
--
-- if bbar[i] < lB[i] - eps, i.e. if the basic variable xB[i] violates
-- its lower bound, the routine replaces the constraint xB[i] >= lB[i]
-- by constraint xB[i] <= lB[i] introducing implicit artificial variable
-- which satisfies its upper (!) bound;
--
-- if bbar[i] > uB[i] + eps, i.e. if the basic variable xB[i] violates
-- its upper bound, the routine replaces the constraint xB[i] <= uB[i]
-- by constraint xB[i] >= uB[i] introducing implicit artificial variable
-- which satisfies its lower (!) bound.
--
-- In both cases eps = tol*max(|lb[i]|,1) or eps = tol*max(|ub[i]|,1)
-- (depending on what bound is checked), where tol = 0.30*tol_bnd.
--
-- Should note that actually no new variables are introduced to the
-- problem. The routine just replaces types and of bounds of some basic
-- variables by new ones.
--
-- Implicit artificial variables correspond to resduals. Therefore the
-- goal is to turn out all implicit variables from the basis that allows
-- to eliminate corresponding residuals. To do this the routine uses the
-- special objective function. If there is the implicit artificial
-- variable xB[i] >= lB[i], the routine sets its coefficient to +1 in
-- order to minimize the corresponding residual from the point of view
-- of the original problem. Analogously, if there is the implicit
-- artificial variable xB[i] <= uB[i], the routine sets its coefficient
-- to -1 again in order to minimize the corresponding residual. Other
-- coefficient are set to zero.
--
-- Should note that when some artificial variable becomes non-basic,
-- the corresponding residual vanishes. Hence the artificial objective
-- function is changed on each iteration of the simplex method.
--
-- Let the basis variable xB[p] leaves the basis. If it is the implicit
-- artificial variable (that recognized by changed type and bounds of
-- this variable), the routine restores its type and bounds to original
-- ones, because in the adjacent basis the corresponding residual will
-- be zero.
--
-- In the case of degeneracy all implicit artificial variables may
-- become close to zero being basic variables. In this case the routine
-- doesn't wait these variables to leave the basis and terminates the
-- search, since such basis solution may be considered as feasible.
--
-- In any case (whether feasible solution was found or not) the routine
-- restores original types and bounds of all variables after the search
-- was finished. */

static int debug = 0; /* debug mode flag (to check the vector gamma) */

int rsm_feas(RSM *rsm, int (*monit)(void),
      gnum_float tol_bnd, gnum_float tol_dj, gnum_float tol_piv, gnum_float gvec[],
      int relax)
{     int m = rsm->m, n = rsm->n, p, tagp, q, i, k, ret;
      gnum_float *bbar, *c, *pi, *cbar, *ap, *aq, *zeta;
      int *orig_type;
      gnum_float *orig_lb, *orig_ub;
      /* check common block for correctness */
      check_rsm(rsm);
      /* allocate working arrays */
      bbar = ucalloc(1+m, sizeof(gnum_float));
      c = ucalloc(1+m+n, sizeof(gnum_float));
      pi = ucalloc(1+m, sizeof(gnum_float));
      cbar = ucalloc(1+n, sizeof(gnum_float));
      aq = ucalloc(1+m, sizeof(gnum_float));
      if (gvec != NULL)
      {  ap = ucalloc(1+n, sizeof(gnum_float));
         zeta = ucalloc(1+m, sizeof(gnum_float));
      }
      /* save original types and bounds of variables and allocate new
         copy of types and bounds for artificial basis solution */
      orig_type = rsm->type, orig_lb = rsm->lb, orig_ub = rsm->ub;
      rsm->type = ucalloc(1+m+n, sizeof(int));
      rsm->lb = ucalloc(1+m+n, sizeof(gnum_float));
      rsm->ub = ucalloc(1+m+n, sizeof(gnum_float));
      for (k = 1; k <= m+n; k++)
      {  rsm->type[k] = orig_type[k];
         rsm->lb[k] = orig_lb[k];
         rsm->ub[k] = orig_ub[k];
      }
      /* compute current values of basic variables */
      eval_bbar(rsm, bbar);
      /* construct artificial feasible basis solution */
      for (i = 1; i <= m; i++)
      {  k = rsm->indb[i]; /* x[k] = xB[i] */
         if (rsm->type[k] == 'L' || rsm->type[k] == 'D' ||
             rsm->type[k] == 'S')
         {  if (check_rr(bbar[i], rsm->lb[k], 0.30 * tol_bnd) < -1)
            {  /* xB[i] violates its lower bound */
               rsm->type[k] = 'U';
               rsm->lb[k] = 0.0;
               rsm->ub[k] = orig_lb[k];
               continue;
            }
         }
         if (rsm->type[k] == 'U' || rsm->type[k] == 'D' ||
             rsm->type[k] == 'S')
         {  if (check_rr(bbar[i], rsm->ub[k], 0.30 * tol_bnd) > +1)
            {  /* xB[i] violates its upper bound */
               rsm->type[k] = 'L';
               rsm->lb[k] = orig_ub[k];
               rsm->ub[k] = 0.0;
               continue;
            }
         }
      }
      /* main loop starts here */
      for (;;)
      {  /* call user-supplied routine */
         if (monit != NULL)
         {  int *type; gnum_float *lb, *ub;
            type = rsm->type, lb = rsm->lb, ub = rsm->ub;
            rsm->type = orig_type, rsm->lb = orig_lb, rsm->ub = orig_ub;
            ret = monit();
            rsm->type = type, rsm->lb = lb, rsm->ub = ub;
            if (ret)
            {  ret = 4;
               break;
            }
         }
         /* compute current values of basic variables */
         eval_bbar(rsm, bbar);
         /* artificial basis solution should be primal feasible */
         if (check_bbar(rsm, bbar, tol_bnd))
         {  ret = 2;
            break;
         }
         /* check original basis solution for primal feasibility */
         {  int *type; gnum_float *lb, *ub;
            type = rsm->type, lb = rsm->lb, ub = rsm->ub;
            rsm->type = orig_type, rsm->lb = orig_lb, rsm->ub = orig_ub;
            ret = check_bbar(rsm, bbar, 0.30 * tol_bnd);
            rsm->type = type, rsm->lb = lb, rsm->ub = ub;
            if (!ret)
            {  ret = 0;
               break;
            }
         }
         /* construct auxiliary objective function */
         for (k = 1; k <= m+n; k++)
         {  c[k] = 0.0;
            if (rsm->type[k] == orig_type[k]) continue;
            /* if type[k] differs from orig_type[k], x[k] is implicit
               artificial variable (which should be basic variable!) */
            insist(rsm->posx[k] > 0);
            if (rsm->type[k] == 'L')
            {  /* x[k] should decrease since in the original solution
                  x[k] violates (is greater than) its upper bound */
               c[k] = +1.0;
            }
            else if (rsm->type[k] == 'U')
            {  /* x[k] should increase since in the original solution
                  x[k] violates (is less than) its lower bound */
               c[k] = -1.0;
            }
            else
               insist(rsm->type[k] != rsm->type[k]);
         }
         /* compute simplex multipliers */
         eval_pi(rsm, c, pi);
         /* compute reduced costs of non-basic variables */
         eval_cbar(rsm, c, pi, cbar);
         /* choose non-basic variable xN[q] */
         q = pivot_col(rsm, c, cbar, gvec, tol_dj);
         if (q == 0)
         {  /* problem has no feasible solution */
            ret = 1;
            break;
         }
         /* compute q-th (pivot) column of the simplex table */
         eval_col(rsm, q, aq, 1);
         /* choose basis variable xB[p] */
         if (!relax)
         {  /* use standard ratio test */
            p = pivot_row(rsm, q, cbar[q] > 0.0, aq, bbar, &tagp,
               tol_piv);
         }
         else
         {  /* use two-pass ratio test */
            p = harris_row(rsm, q, cbar[q] > 0.0, aq, bbar, &tagp,
#if 0
               tol_piv, 0.15 * tol_bnd);
#else /* 3.0.3 */
               tol_piv, 0.003 * tol_bnd);
#endif
         }
         if (p == 0)
         {  /* it should never be */
            fault("rsm_feas: internal logic error");
         }
         /* xB[p] will leave the basis on the next iteration; if it is
            implicit artificial variable, the corresponding residual
            vanishes, therefore type and bounds of this variable should
            be restored to the original ones */
         if (p > 0)
         {  k = rsm->indb[p]; /* x[k] = xB[p] */
            if (rsm->type[k] != orig_type[k])
            {  /* x[k] is implicit artificial variable */
               insist(tagp == 'L' || tagp == 'U');
               tagp = (tagp == 'L' ? 'U' : 'L');
               rsm->type[k] = orig_type[k];
               rsm->lb[k] = orig_lb[k];
               rsm->ub[k] = orig_ub[k];
               if (rsm->type[k] == 'S') tagp = 'S';
            }
         }
         /* update the vector gamma */
         if (gvec != NULL && p > 0)
         {  /* compute p-th row of inv(B) */
            eval_zeta(rsm, p, zeta);
            /* compute p-th (pivot) row of the simplex table */
            eval_row(rsm, zeta, ap);
            /* update the vector gamma */
            update_gvec(rsm, gvec, p, q, ap, aq, zeta);
         }
         /* jump to the adjacent basis */
         if (change_b(rsm, p, tagp, q))
         {  /* numerical problems with basis matrix */
            ret = 3;
            break;
         }
         /* check accuracy of the vector gamma */
         if (debug && gvec != NULL)
            print("check_gvec: %g", check_gvec(rsm, gvec));
         /* end of main loop */
      }
      /* restore original types and bounds of variables */
      ufree(rsm->type), ufree(rsm->lb), ufree(rsm->ub);
      rsm->type = orig_type, rsm->lb = orig_lb, rsm->ub = orig_ub;
      /* free working arrays */
      ufree(bbar);
      ufree(c);
      ufree(pi);
      ufree(cbar);
      ufree(aq);
      if (gvec != NULL)
      {  ufree(ap);
         ufree(zeta);
      }
      /* return to the calling program */
      return ret;
}

/*----------------------------------------------------------------------
-- rsm_primal - find optimal solution using primal simplex method.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- int rsm_primal(RSM *rsm, int (*monit)(void), gnum_float c[],
--    gnum_float tol_bnd, gnum_float tol_dj, gnum_float tol_piv, gnum_float gvec[],
--    int relax);
--
-- *Description*
--
-- The rsm_primal routine searches for optimal solution of LP problem
-- using the primal simplex method.
--
-- The parameter rsm points to the common block. On entry this block
-- specifies an initial basis solution which should be primal feasible.
-- On exit this block specifies the last reached basis solution.
--
-- The parameter monit specifies the user-supplied routine used for
-- monitoring purposes. It is called by the rsm_primal routine each time
-- before the next iteration. If the monit routine returns non-zero, the
-- search is terminated. The parameter monit can be set to NULL.
--
-- The array c specifies elements of the expanded vector of coefficients
-- of the objective function in locations c[1], ..., c[m+n]. This array
-- is not changed on exit. Should note that the routine *minimizes* the
-- objective function, therefore in the case of maximization the vector
-- c should have the opposite sign.
--
-- The parameter tol_bnd is a relative tolerance which is used by the
-- routine in order to see if the current solution is primal feasible.
--
-- The parameter tol_dj is a relative tolerance which is used by the
-- routine in order to see if the current solution is dual feasible.
--
-- The parameter tol_piv is a relative tolerance which is used by the
-- routine in order to choose the pivot element of the simplex table.
--
-- If the parameter gvec is NULL, the routine chooses non-basic variable
-- using the standard technique. Otherwise the routine uses the steepest
-- edge technique. In the latter case the array gvec should specifiy
-- elements of the vector gamma in locations gvec[1], ..., gvec[n]. On
-- entry the vector gamma should correspond to the initial basis passed
-- to the routine. This vector is updated on every iteration, therefore
-- on exit it corresponds to the final basis reached by the routine.
--
-- The parameter relax is a flag. If it is zero, the routine chooses
-- basic variable using the standard ratio test. Otherwise the routine
-- uses two-pass ratio test.
--
-- *Returns*
--
-- The rsm_primal routine returns one of the following codes:
--
-- 0 - optimal solution found;
-- 1 - problem has unbounded solution;
-- 2 - numerical stability lost (the current basis solution became
--     primal infeasible due to round-off errors);
-- 3 - numerical problems with basis matrix (the current basis matrix
--     became singular or ill-conditioned due to unsuccessful choice of
--     the pivot element on the last simplex iteration);
-- 4 - user-supplied routine returned non-zero code. */

#if 0
static int debug = 0; /* debug mode flag (to check the vector gamma) */
#endif

int rsm_primal(RSM *rsm, int (*monit)(void), gnum_float c[],
      gnum_float tol_bnd, gnum_float tol_dj, gnum_float tol_piv, gnum_float gvec[],
      int relax)
{     int m = rsm->m, n = rsm->n, p, tagp, q, ret;
      gnum_float *bbar, *pi, *cbar, *ap, *aq, *zeta;
      /* check common block for correctness */
      check_rsm(rsm);
      /* allocate working arrays */
      bbar = ucalloc(1+m, sizeof(gnum_float));
      pi = ucalloc(1+m, sizeof(gnum_float));
      cbar = ucalloc(1+n, sizeof(gnum_float));
      aq = ucalloc(1+m, sizeof(gnum_float));
      if (gvec != NULL)
      {  ap = ucalloc(1+n, sizeof(gnum_float));
         zeta = ucalloc(1+m, sizeof(gnum_float));
      }
      /* main loop starts here */
      for (;;)
      {  /* call user-supplied routine */
         if (monit != NULL)
         {  if (monit())
            {  ret = 4;
               break;
            }
         }
         /* compute current values of basic variables */
         eval_bbar(rsm, bbar);
         /* current basis solution should be primal feasible */
         if (check_bbar(rsm, bbar, tol_bnd))
         {  ret = 2;
            break;
         }
         /* compute simplex multipliers */
         eval_pi(rsm, c, pi);
         /* compute reduced costs of non-basic variables */
         eval_cbar(rsm, c, pi, cbar);
         /* choose non-basic variable xN[q] */
         q = pivot_col(rsm, c, cbar, gvec, tol_dj);
         if (q == 0)
         {  /* optimal solution found */
            ret = 0;
            break;
         }
         /* compute q-th (pivot) column of the simplex table */
         eval_col(rsm, q, aq, 1);
         /* choose basic variable xB[p] */
         if (!relax)
         {  /* use standard ratio test */
            p = pivot_row(rsm, q, cbar[q] > 0.0, aq, bbar, &tagp,
               tol_piv);
         }
         else
         {  /* use two-pass ratio test */
            p = harris_row(rsm, q, cbar[q] > 0.0, aq, bbar, &tagp,
#if 0
               tol_piv, 0.15 * tol_bnd);
#else /* 3.0.3 */
               tol_piv, 0.003 * tol_bnd);
#endif
         }
         if (p == 0)
         {  /* problem has unbounded solution */
            ret = 1;
            break;
         }
         /* update the vector gamma */
         if (gvec != NULL && p > 0)
         {  /* compute p-th row of inv(B) */
            eval_zeta(rsm, p, zeta);
            /* compute p-th (pivot) row of the simplex table */
            eval_row(rsm, zeta, ap);
            /* update the vector gamma */
            update_gvec(rsm, gvec, p, q, ap, aq, zeta);
         }
         /* jump to the adjacent basis */
         if (change_b(rsm, p, tagp, q))
         {  /* numerical problems with basis matrix */
            ret = 3;
            break;
         }
         /* check accuracy of the vector gamma */
         if (debug && gvec != NULL)
            print("check_gvec: %g", check_gvec(rsm, gvec));
         /* end of main loop */
      }
      /* free working arrays */
      ufree(bbar);
      ufree(pi);
      ufree(cbar);
      ufree(aq);
      if (gvec != NULL)
      {  ufree(ap);
         ufree(zeta);
      }
      /* return to the calling program */
      return ret;
}

/*----------------------------------------------------------------------
-- rsm_dual - find optimal solution using dual simplex method.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- int rsm_dual(RSM *rsm, int (*monit)(void), gnum_float c[],
--    gnum_float tol_bnd, gnum_float tol_dj, gnum_float tol_piv, gnum_float dvec[],
--    int relax);
--
-- *Description*
--
-- The rsm_dual routine searches for optimal solution of LP problem
-- using the dual simplex method.
--
-- The parameter rsm points to the common block. On entry this block
-- specifies an initial basis solution which should be dual feasible.
-- On exit this block specifies the last reached basis solution.
--
-- The parameter monit specifies the user-supplied routine used for
-- monitoring purposes. It is called by the rsm_dual routine each time
-- before the next iteration. If the monit routine returns non-zero,
-- the search is terminated. The parameter monit can be set to NULL.
--
-- The array c specifies elements of the expanded vector of coefficients
-- of the objective function in locations c[1], ..., c[m+n]. This array
-- is not changed on exit. Should note that the routine *minimizes* the
-- objective function, therefore in the case of maximization the vector
-- c should have the opposite sign.
--
-- The parameter tol_bnd is a relative tolerance which is used by the
-- routine in order to see if the current solution is primal feasible.
--
-- The parameter tol_dj is a relative tolerance which is used by the
-- routine in order to see if the current solution is dual feasible.
--
-- The parameter tol_piv is a relative tolerance which is used by the
-- routine in order to choose the pivot element of the simplex table.
--
-- If the parameter dvec is NULL, the routine chooses basic variable
-- using the standard technique. Otherwise the routine uses the steepest
-- edge technique. In the latter case the array dvec should specifiy
-- elements of the vector delta in locations dvec[1], ..., dvec[m]. On
-- entry the vector delta should correspond to the initial basis passed
-- to the routine. This vector is updated on every iteration, therefore
-- on exit it corresponds to the final basis reached by the routine.
--
-- The parameter relax is a flag. If this flag is zero, the routine
-- chooses non-basic variable using the standard ratio test. Otherwise
-- the routine uses two-pass ratio test.
--
-- *Returns*
--
-- The rsm_dual routine returns one of the following codes:
--
-- 0 - optimal solution found;
-- 1 - problem has no (primal) feasible solution;
-- 2 - numerical stability lost (the current basis solution became dual
--     infeasible due to round-off errors);
-- 3 - numerical problems with basis matrix (the current basis matrix
--     became singular or ill-conditioned due to unsuccessful choice of
--     the pivot element on the last simplex iteration);
-- 4 - user-supplied routine returned non-zero code. */

#define debug debug1

static int debug = 0; /* debug mode flag (to check the vector delta) */

int rsm_dual(RSM *rsm, int (*monit)(void), gnum_float c[],
      gnum_float tol_bnd, gnum_float tol_dj, gnum_float tol_piv, gnum_float dvec[],
      int relax)
{     int m = rsm->m, n = rsm->n, p, tagp, q, ret;
      gnum_float *bbar, *pi, *cbar, *ap, *aq, *zeta;
      /* check common block for correctness */
      check_rsm(rsm);
      /* allocate working arrays */
      bbar = ucalloc(1+m, sizeof(gnum_float));
      pi = ucalloc(1+m, sizeof(gnum_float));
      cbar = ucalloc(1+n, sizeof(gnum_float));
      ap = ucalloc(1+n, sizeof(gnum_float));
      aq = ucalloc(1+m, sizeof(gnum_float));
      zeta = ucalloc(1+m, sizeof(gnum_float));
      /* main loop starts here */
      for (;;)
      {  /* call user-supplied routine */
         if (monit != NULL)
         {  if (monit())
            {  ret = 4;
               break;
            }
         }
         /* compute current values of basic variables */
         eval_bbar(rsm, bbar);
         /* compute simplex multipliers */
         eval_pi(rsm, c, pi);
         /* compute reduced costs of non-basic variables */
         eval_cbar(rsm, c, pi, cbar);
         /* current basis solution should be dual feasible */
         if (check_cbar(rsm, c, cbar, tol_dj))
         {  ret = 2;
            break;
         }
         /* choose basic variable xB[p] */
         p = dual_row(rsm, bbar, dvec, &tagp, tol_bnd);
         if (p == 0)
         {  /* optimal solution found */
            ret = 0;
            break;
         }
         /* compute p-th row of inv(B) */
         eval_zeta(rsm, p, zeta);
         /* compute p-th (pivot) row of the simplex table */
         eval_row(rsm, zeta, ap);
#if 0 /* 3.0.9; hmm... either ap is computed quite accurately or the
         residual can't be detected due to zero bounds of xN */
         {  int j, k;
            gnum_float sum = 0.0, alpha;
            for (j = 1; j <= n; j++)
            {  alpha = ap[j];
               if (alpha != 0.0)
               {  k = rsm->indn[j]; /* x[k] = xN[j] */
                  switch (rsm->tagn[j])
                  {  case 'L': sum += alpha * rsm->lb[k]; break;
                     case 'U': sum += alpha * rsm->ub[k]; break;
                     case 'F': break;
                     case 'S': sum += alpha * rsm->lb[k]; break;
                  }
               }
            }
            if (fabs(bbar[p] - sum) / (1.0 + fabs(sum)) > 1e-6)
               print("bbar = %g; sum = %g", bbar[p], sum);
         }
#endif
         /* choose non-basic variable xN[q] */
         if (!relax)
         {  /* use standard ratio test */
            q = dual_col(rsm, tagp, ap, cbar, tol_piv);
         }
         else
         {  /* use two-pass ratio test */
            q = harris_col(rsm, tagp, ap, c, cbar, tol_piv,
#if 0
               0.15 * tol_dj);
#else /* 3.0.3 */
               0.003 * tol_dj);
#endif
         }
         if (q == 0)
         {  /* problem has no (primal) feasible solution */
            ret = 1;
            break;
         }
         /* compute q-th (pivot) column of the simplex table */
         eval_col(rsm, q, aq, 1);
         /* update the vector delta */
         if (dvec != NULL)
            update_dvec(rsm, dvec, p, q, ap, aq, zeta);
         /* correct tagp if xB[p] is fixed variable */
         if (rsm->type[rsm->indb[p]] == 'S') tagp = 'S';
         /* jump to the adjacent basis */
         if (change_b(rsm, p, tagp, q))
         {  /* numerical problems with basis matrix */
            ret = 3;
            break;
         }
         /* check accuracy of the vector delta */
         if (debug && dvec != NULL)
            print("check_dvec: %g", check_dvec(rsm, dvec));
         /* end of main loop */
      }
      /* free working arrays */
      ufree(bbar);
      ufree(pi);
      ufree(cbar);
      ufree(ap);
      ufree(aq);
      ufree(zeta);
      /* return to the calling program */
      return ret;
}

/* eof */
