/* glplpx6c.c (branch-and-bound solver routine) */

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
#include "glpbbm.h"
#include "glplp.h"
#include "glplpx.h"

/*----------------------------------------------------------------------
-- lpx_integer - easy-to-use driver to the branch-and-bound method.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_integer(LPX *lp);
--
-- *Description*
--
-- The routine lpx_integer is intended to solve a MIP problem, which is
-- specified by the parameter lp.
--
-- On entry the problem object should contain an optimal solution of LP
-- relaxation, which can be obtained by the simplex method.
--
-- *Returns*
--
-- The routine lpx_integer returns one of the following exit codes:
--
-- LPX_E_OK       the MIP problem has been successfully solved.
--
-- LPX_E_FAULT    the solver can't start the search because either
--                the problem is not of MIP class, or
--                the problem object doesn't contain optimal solution
--                for LP relaxation, or
--                some integer variable has non-integer lower or upper
--                bound, or
--                some row has non-zero objective coefficient.
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

int lpx_integer(LPX *lp)
{     int m = lpx_get_num_rows(lp);
      int n = lpx_get_num_cols(lp);
      LP *mip = NULL;
      LPSOL *sol = NULL;
      struct bbm1_cp parm;
      int j, k, ret, *ndx;
      gnm_float trick = 1e-12, *val;
#     define prefix "lpx_integer: "
      /* the problem should be of MIP class */
      if (lpx_get_class(lp) != LPX_MIP)
      {  print(prefix "problem is not of MIP class");
         ret = LPX_E_FAULT;
         goto done;
      }
      /* an optimal solution of LP relaxation should be known */
      if (lpx_get_status(lp) != LPX_OPT)
      {  print(prefix "optimal solution of LP relaxation required");
         ret = LPX_E_FAULT;
         goto done;
      }
      /* create LP block used by the branch-and-bound routine */
      mip = create_lp(m, n, 1);
      /* copy column kinds */
      for (j = 1; j <= n; j++)
         mip->kind[j] = (lpx_get_col_kind(lp, j) == LPX_IV);
      /* copy bounds of rows and columns */
      for (k = 1; k <= m+n; k++)
      {  int typx;
         gnm_float lb, ub, temp;
         if (k <= m)
            lpx_get_row_bnds(lp, k, &typx, &lb, &ub);
         else
            lpx_get_col_bnds(lp, k-m, &typx, &lb, &ub);
         temp = floorgnum(lb + 0.5);
         if (gnumabs(lb - temp) / (1.0 + gnumabs(lb)) <= trick) lb = temp;
         temp = floorgnum(ub + 0.5);
         if (gnumabs(ub - temp) / (1.0 + gnumabs(ub)) <= trick) ub = temp;
         if (k > m && mip->kind[k-m])
         {  if (!(lb == floorgnum(lb) && ub == floorgnum(ub)))
            {  print(prefix "integer column %d has non-integer lower/up"
                  "per bound", k-m);
               ret = LPX_E_FAULT;
               goto done;
            }
         }
         switch (typx)
         {  case LPX_FR: typx = 'F'; break;
            case LPX_LO: typx = 'L'; break;
            case LPX_UP: typx = 'U'; break;
            case LPX_DB: typx = 'D'; break;
            case LPX_FX: typx = 'S'; break;
            default: insist(typx != typx);
         }
         mip->type[k] = typx;
         mip->lb[k] = lb;
         mip->ub[k] = ub;
      }
      /* copy the constraint matrix */
      ndx = ucalloc(1+m, sizeof(int));
      val = ucalloc(1+m, sizeof(gnm_float));
      for (j = 1; j <= n; j++)
      {  int len, t;
         gnm_float temp;
         len = lpx_get_mat_col(lp, j, ndx, val);
         for (t = 1; t <= len; t++)
         {  temp = floorgnum(val[t] + 0.5);
            if (gnumabs(val[t] - temp) / (1.0 + gnumabs(val[t])) <= trick)
               val[t] = temp;
            new_elem(mip->A, ndx[t], j, val[t]);
         }
      }
      ufree(ndx);
      ufree(val);
      /* copy the objective function */
      mip->dir = (lpx_get_obj_dir(lp) == LPX_MIN ? '-' : '+');
      for (k = 0; k <= m+n; k++)
      {  gnm_float c, temp;
         if (k == 0)
            c = lpx_get_obj_c0(lp);
         else if (k <= m)
            c = lpx_get_row_coef(lp, k);
         else
            c = lpx_get_col_coef(lp, k-m);
         temp = floorgnum(c + 0.5);
         if (gnumabs(c - temp) / (1.0 + gnumabs(c)) <= trick) c = temp;
         if (k == 0)
            mip->c[0] = c;
         else if (k <= m)
         {  if (c != 0.0)
            {  print(prefix "row %d has non-zero obj. coefficient", k);
               ret = LPX_E_FAULT;
               goto done;
            }
         }
         else
            mip->c[k-m] = c;
      }
      /* create solution block used by the branch-and-bound routine and
         initialize it with an optimal solution of LP relaxation */
      sol = create_lpsol(m, n);
      sol->mipsol = 0;
      sol->status = 'O';
      sol->objval = lpx_get_obj_val(lp);
      for (k = 1; k <= m+n; k++)
      {  int tagx;
         gnm_float vx, dx;
         if (k <= m)
            lpx_get_row_info(lp, k, &tagx, &vx, &dx);
         else
            lpx_get_col_info(lp, k-m, &tagx, &vx, &dx);
         switch (tagx)
         {  case LPX_BS: tagx = 'B'; vx = vx;         break;
            case LPX_NL: tagx = 'L'; vx = mip->lb[k]; break;
            case LPX_NU: tagx = 'U'; vx = mip->ub[k]; break;
            case LPX_NF: tagx = 'F'; vx = 0.0;        break;
            case LPX_NS: tagx = 'S'; vx = mip->lb[k]; break;
            default: insist(tagx != tagx);
         }
         sol->tagx[k] = tagx;
         sol->valx[k] = vx;
         sol->dx[k] = dx;
      }
      /* set control parameters */
      parm.what = 2;
      switch (lpx_get_int_parm(lp, LPX_K_BRANCH))
      {  case 0: parm.branch = BB_FIRST; break;
         case 1: parm.branch = BB_LAST;  break;
         case 2: parm.branch = BB_DRTOM; break;
         default: insist(lp != lp);
      }
      switch (lpx_get_int_parm(lp, LPX_K_BTRACK))
      {  case 0: parm.btrack = BB_LIFO;  break;
         case 1: parm.btrack = BB_FIFO;  break;
         case 2: parm.btrack = BB_BESTP; break;
         default: insist(lp != lp);
      }
      parm.tol_int = lpx_get_real_parm(lp, LPX_K_TOLINT);
      parm.tol_obj = lpx_get_real_parm(lp, LPX_K_TOLOBJ);
      parm.steep = (lpx_get_int_parm(lp, LPX_K_PRICE) == 0 ? 0 : 1);
      parm.relax = (lpx_get_real_parm(lp, LPX_K_RELAX) == 0.0 ? 0 : 1);
      parm.tol_bnd = lpx_get_real_parm(lp, LPX_K_TOLBND);
      parm.tol_dj = lpx_get_real_parm(lp, LPX_K_TOLDJ);
      parm.tol_piv = lpx_get_real_parm(lp, LPX_K_TOLPIV);
      parm.it_lim = lpx_get_int_parm(lp, LPX_K_ITLIM);
      parm.it_cnt = lpx_get_int_parm(lp, LPX_K_ITCNT);
      parm.tm_lim = lpx_get_real_parm(lp, LPX_K_TMLIM);
      parm.round = 0;
      /* solve MIP problem */
      ret = bbm1_driver(mip, sol, &parm);
      /* analyze return code */
      switch (ret)
      {  case 0: ret = LPX_E_OK;    break;
         case 1: ret = LPX_E_ITLIM; break;
         case 2: ret = LPX_E_TMLIM; break;
         case 3: ret = LPX_E_SING;  break;
         default: insist(ret != ret);
      }
      /* reflect statistics about the spent resources */
      lpx_set_int_parm(lp, LPX_K_ITLIM, parm.it_lim);
      lpx_set_int_parm(lp, LPX_K_ITCNT, parm.it_cnt);
      lpx_set_real_parm(lp, LPX_K_TMLIM, parm.tm_lim);
      /* store solution found by the solver */
      if (!sol->mipsol)
         lp->i_stat = LPX_I_UNDEF;
      else
      {  switch (sol->status)
         {  case '?': lp->i_stat = LPX_I_UNDEF;  break;
            case 'O': lp->i_stat = LPX_I_OPT;    break;
            case 'F': lp->i_stat = LPX_I_FEAS;   break;
            case 'I': lp->i_stat = LPX_I_UNDEF;  break;
            case 'N': lp->i_stat = LPX_I_NOFEAS; break;
            default: insist(sol != sol);
         }
      }
      if (lp->i_stat == LPX_I_OPT || lp->i_stat == LPX_I_FEAS)
      {  for (k = 1; k <= m+n; k++)
         {  gnm_float temp;
            temp = sol->valx[k];
            if (k > m && mip->kind[k-m]) insist(temp == floorgnum(temp));
            lp->mipx[k] = temp;
         }
      }
done: /* free working data structures */
      if (mip != NULL) delete_lp(mip);
      if (sol != NULL) delete_lpsol(sol);
      /* return to the calling program */
      return ret;
#     undef prefix
}

/* eof */
