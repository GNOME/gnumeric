/* glplp.c */

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

#include <float.h>
#include <math.h>
#include <stddef.h>
#include "glpk.h"
#include "glplp.h"
#include "glprsm.h"

/*----------------------------------------------------------------------
-- create_lp - create linear programming problem data block.
--
-- *Synopsis*
--
-- #include "glplp.h"
-- LP *create_lp(int m, int n, int mip);
--
-- *Description*
--
-- The create_lp routine creates a linear programming problem (LP) data
-- block, which describes the problem with m rows (auxiliary variables)
-- and n columns (structural variables).
--
-- The parameter mip is a flag. If it is zero, the routine creates data
-- block for pure LP problem. Otherwise, if this flag is non-zero, the
-- routine creates data block for MIP problem.
--
-- Initially all auxiliary variables are equal to zero, all structural
-- variables are non-negative, constraint matrix has no elements (i.e.
-- it is zero matrix), optimization direction is minimization, and all
-- coefficients of the objective function (including the constant term)
-- are equal to zero.
--
-- *Returns*
--
-- The create_lp routine returns a pointer to the created block. */

LP *create_lp(int m, int n, int mip)
{     LP *lp;
      int j, k;
      if (!(m > 0 && n > 0))
         fault("create_lp: invalid dimension");
      lp = umalloc(sizeof(LP));
      lp->m = m;
      lp->n = n;
      if (!mip)
         lp->kind = NULL;
      else
      {  lp->kind = ucalloc(1+n, sizeof(int));
         /* initially all structural variables are continuous */
         for (j = 1; j <= n; j++) lp->kind[j] = 0;
      }
      lp->type = ucalloc(1+m+n, sizeof(int));
      lp->lb = ucalloc(1+m+n, sizeof(gnum_float));
      lp->ub = ucalloc(1+m+n, sizeof(gnum_float));
      /* initially all auxiliary variables are equal to zero and all
         structural variables are non-negative */
      for (k = 1; k <= m+n; k++)
      {  lp->type[k] = (k <= m ? 'S' : 'L');
         lp->lb[k] = lp->ub[k] = 0.0;
      }
      lp->A = create_mat(m, n);
      lp->dir = '-';
      lp->c = ucalloc(1+n, sizeof(gnum_float));
      for (j = 0; j <= n; j++) lp->c[j] = 0.0;
      return lp;
}

/*----------------------------------------------------------------------
-- check_lp - check LP data block for correctness.
--
-- *Synopsis*
--
-- #include "glplp.h"
-- void check_lp(LP *lp);
--
-- *Description*
--
-- The check_lp routine checks the linear programming data block, which
-- lp points to, for correctness. In case of error the routine displays
-- an error message and terminates the program.
--
-- Note that the check_lp routine doesn't check the constraint matrix,
-- because the corresponding operation is extermely inefficient. It may
-- be checked additionally by the check_mat routine. */

void check_lp(LP *lp)
{     int k;
      if (lp->m < 1)
         fault("check_lp: invalid number of rows");
      if (lp->n < 1)
         fault("check_lp: invalid number of columns");
      for (k = 1; k <= lp->m+lp->n; k++)
      {  switch (lp->type[k])
         {  case 'F':
               if (!(lp->lb[k] == 0.0 && lp->ub[k] == 0.0))
err:              fault("check_lp: invalid bounds of row/column");
               break;
            case 'L':
               if (lp->ub[k] != 0.0) goto err;
               break;
            case 'U':
               if (lp->lb[k] != 0.0) goto err;
               break;
            case 'D':
               break;
            case 'S':
               if (lp->lb[k] != lp->ub[k]) goto err;
               break;
            default:
               fault("check_lp: invalid type of row/column");
         }
      }
      if (!(lp->A->m == lp->m && lp->A->n == lp->n))
         fault("check_lp: invalid dimension of constraint matrix");
      if (!(lp->dir == '-' || lp->dir == '+'))
         fault("check_lp: invalid optimization direction flag");
      return;
}

/*----------------------------------------------------------------------
-- delete_lp - delete linear programming problem data block.
--
-- *Synopsis*
--
-- #include "glplp.h"
-- void delete_lp(LP *lp);
--
-- *Description*
--
-- The delete_lp routine deletes the linear programming (LP) data block
-- which lp points to, freeing all memory allocated to this object. */

void delete_lp(LP *lp)
{     if (lp->kind != NULL) ufree(lp->kind);
      ufree(lp->type);
      ufree(lp->lb);
      ufree(lp->ub);
      delete_mat(lp->A);
      ufree(lp->c);
      ufree(lp);
      return;
}

/*----------------------------------------------------------------------
-- create_lpsol - create linear programming problem solution block.
--
-- *Synopsis*
--
-- #include "glplp.h"
-- LPSOL *create_lpsol(int m, int n);
--
-- *Description*
--
-- The create_lpsol routine creates a linear programming (LP) basis
-- solution block, which correcponds to the problem with m rows and n
-- columns.
--
-- *Returns*
--
-- The create_lpsol routine returns a pointer to the created block. */

LPSOL *create_lpsol(int m, int n)
{     LPSOL *sol;
      int k;
      if (!(m > 0 && n > 0))
         fault("create_lpsol: invalid dimension");
      sol = umalloc(sizeof(LPSOL));
      sol->m = m;
      sol->n = n;
      sol->mipsol = 0;
      sol->status = '?';
      sol->objval = 0.0;
      sol->tagx = ucalloc(1+m+n, sizeof(int));
      sol->valx = ucalloc(1+m+n, sizeof(gnum_float));
      sol->dx = ucalloc(1+m+n, sizeof(gnum_float));
      for (k = 1; k <= m+n; k++)
      {  sol->tagx[k] = '?';
         sol->valx[k] = sol->dx[k] = 0.0;
      }
      return sol;
}

/*----------------------------------------------------------------------
-- delete_lpsol - delete linear programming problem solution block.
--
-- *Synopsis*
--
-- #include "glplp.h"
-- void delete_lpsol(LPSOL *sol);
--
-- *Description*
--
-- The delete_lpsol routine deletes the linear programming (LP) problem
-- solution block, which sol points to, freeing all memory allocated to
-- this object. */

void delete_lpsol(LPSOL *sol)
{     ufree(sol->tagx);
      ufree(sol->valx);
      ufree(sol->dx);
      ufree(sol);
      return;
}

#if 0
/*----------------------------------------------------------------------
-- extract_prob - extract LP problem data from LPI object.
--
-- *Synopsis*
--
-- #include "glplp.h"
-- LP *extract_prob(void *lpi);
--
-- *Description*
--
-- The routine extract_prob extracts linear programming problem data
-- from the LPI object (for details see GLPK API).
--
-- *Returns*
--
-- The routine returns a pointer to the LP data block. */

LP *extract_prob(void *lpi)
{     LP *lp;
      int m = glp_get_num_rows(lpi), n = glp_get_num_cols(lpi), i, j;
      int *cn = ucalloc(1+n, sizeof(int));
      gnum_float *ai = ucalloc(1+n, sizeof(gnum_float));
      if (m == 0)
         fault("extract_prob: problem has no rows");
      if (n == 0)
         fault("extract_prob: problem has no columns");
      lp = create_lp(m, n, glp_get_num_int(lpi) == 0 ? 0 : 1);
      for (i = 1; i <= m; i++)
      {  int nz, t;
         glp_get_row_bnds(lpi, i, &lp->type[i], &lp->lb[i],
            &lp->ub[i]);
         nz = glp_get_row_coef(lpi, i, cn, ai);
         for (t = 1; t <= nz; t++)
            if (ai[t] != 0.0) new_elem(lp->A, i, cn[t], ai[t]);
      }
      for (j = 1; j <= n; j++)
      {  if (glp_get_col_kind(lpi, j) == 'I') lp->kind[j] = 1;
         glp_get_col_bnds(lpi, j, &lp->type[m+j], &lp->lb[m+j],
            &lp->ub[m+j]);
      }
      lp->dir = glp_get_obj_sense(lpi);
      for (j = 0; j <= n; j++) lp->c[j] = glp_get_obj_coef(lpi, j);
      ufree(cn);
      ufree(ai);
      check_lp(lp);
      if (check_mplets(lp->A) != NULL)
         fault("extract_prob: constraint matrix has multiplets");
      return lp;
}
#endif

/*----------------------------------------------------------------------
-- prepro_lp - perform preprocessing LP/MIP problem.
--
-- *Synopsis*
--
-- #include "glplp.h"
-- int prepro_lp(LP *lp);
--
-- *Description*
--
-- The prepro_lp routine performs preprocessing LP/MIP problem specified
-- by the data block, which the parameter lp points to.
--
-- The result of preprocessing is a problem, which has the same feasible
-- region as the original problem.
--
-- In the resultant problem lower and/or upper bounds of some auxiliary
-- variables may be removed, and bounds of some structural variables may
-- be tightened (in particular, additional fixed variables may appear).
-- The constraint matrix is not changed.
--
-- *Returns*
--
-- The prepro_lp routine returns one of the following codes:
--
-- 0 - ok (no infeasibility detected);
-- i - i-th row is inconsistent. */

static LP *lp;
/* LP/MIP problem data block */

static int m;
/* number of rows = number of auxiliary variables */

static int n;
/* number of columns = number of structural variables */

struct limit { gnum_float val; int cnt; };
/* this structure represents infimum/supremum of a row; val is a finite
   part, and cnt is number of infinite terms */

static struct limit *f_min; /* struct limit f_min[1+m]; */
/* f_min[i] is an infimum of the i-th row (1 <= i <= m) computed using
   the current bounds of structural variables */

static struct limit *f_max; /* struct limit f_max[1+m]; */
/* f_max[i] is a supremum of the i-th row (1 <= i <= m) computed using
   the current bounds of structural variables */

static int nr; /* 0 <= nr <= m */
/* current number of rows in the active row list */

static int *rlist; /* int rlist[1+m]; */
/* row list: the elements rlist[1], ..., rlist[nr] are numbers of rows,
   which should be processed on the next pass */

static char *rflag; /* char rflag[1+m]; */
/* rflag[i] != 0 means that the i-th row is in the row list */

static int nc; /* 0 <= nc <= n */
/* current number of columns in the active column list */

static int *clist; /* int clist[1+n]; */
/* column list: the elements clist[1], ..., clist[nc] are numbers of
   columns, which should be processed on the next pass */

static char *cflag; /* char cflag[1+n]; */
/* cflag[j] != 0 means that the j-th column is in the column list */

/*----------------------------------------------------------------------
 */
