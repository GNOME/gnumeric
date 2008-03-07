/* glplpx2.c (problem retrieving routines) */

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

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "glplib.h"
#define _GLPLPX_UNLOCK
#include "glplpx.h"

/*----------------------------------------------------------------------
-- lpx_get_prob_name - retrieve problem name.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- char *lpx_get_prob_name(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_get_prob_name returns a pointer to a static buffer,
-- which contains symbolic name of the problem. However, if the problem
-- has no assigned name, the routine returns NULL. */

char *lpx_get_prob_name(LPX *lp)
{     char *name;
      if (lp->name == NULL)
         name = NULL;
      else
         name = get_str(lp->str_buf, lp->name);
      return name;
}

/*----------------------------------------------------------------------
-- lpx_get_class - retrieve problem class.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_class(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_get_class returns a problem class for the specified
-- problem object:
--
-- LPX_LP  - pure linear programming (LP) problem;
-- LPX_MIP - mixed integer programming (MIP) problem. */

int lpx_get_class(LPX *lp)
{     int klass;
      klass = lp->klass;
      return klass;
}

/*----------------------------------------------------------------------
-- lpx_get_obj_name - retrieve objective function name.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- char *lpx_get_obj_name(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_get_obj_name returns a pointer to a static buffer,
-- which contains a symbolic name of the objective function. However,
-- if the objective function has no assigned name, the routine returns
-- NULL. */

char *lpx_get_obj_name(LPX *lp)
{     char *name;
      if (lp->obj == NULL)
         name = NULL;
      else
         name = get_str(lp->str_buf, lp->obj);
      return name;
}

/*----------------------------------------------------------------------
-- lpx_get_obj_dir - retrieve optimization direction flag.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_obj_dir(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_get_obj_dir returns the optimization direction flag
-- (i.e. "sense" of the objective function):
--
-- LPX_MIN - minimization;
-- LPX_MAX - maximization. */

int lpx_get_obj_dir(LPX *lp)
{     int dir;
      dir = lp->dir;
      return dir;
}

/*----------------------------------------------------------------------
-- lpx_get_num_rows - retrieve number of rows.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_num_rows(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_get_num_rows returns the current number of rows in
-- a problem object, which the parameter lp points to. */

int lpx_get_num_rows(LPX *lp)
{     int m;
      m = lp->m;
      return m;
}

/*----------------------------------------------------------------------
-- lpx_get_num_cols - retrieve number of columns.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_num_cols(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_get_num_cols returns the current number of columns
-- in a problem object, which the parameter lp points to. */

int lpx_get_num_cols(LPX *lp)
{     int n;
      n = lp->n;
      return n;
}

/*----------------------------------------------------------------------
-- lpx_get_num_int - retrieve number of integer columns.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_num_int(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_get_num_int returns the current number of columns,
-- which are marked as integer. */

int lpx_get_num_int(LPX *lp)
{     int j, count;
      if (lp->klass != LPX_MIP)
         fault("lpx_get_num_int: not a MIP problem");
      count = 0;
      for (j = 1; j <= lp->n; j++)
         if (lp->col[j]->kind == LPX_IV) count++;
      return count;
}

/*----------------------------------------------------------------------
-- lpx_get_num_bin - retrieve number of binary columns.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_num_bin(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_get_num_bin returns the current number of columns,
-- which are marked as integer and whose lower bound is zero and upper
-- bound is one. */

int lpx_get_num_bin(LPX *lp)
{     LPXCOL *col;
      int j, count;
      if (lp->klass != LPX_MIP)
         fault("lpx_get_num_bin: not a MIP problem");
      count = 0;
      for (j = 1; j <= lp->n; j++)
      {  col = lp->col[j];
         if (col->kind == LPX_IV && col->type == LPX_DB &&
             col->lb == 0.0 && col->ub == 1.0) count++;
      }
      return count;
}

/*----------------------------------------------------------------------
-- lpx_get_row_name - retrieve row name.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- char *lpx_get_row_name(LPX *lp, int i);
--
-- *Returns*
--
-- The routine lpx_get_row_name returns a pointer to a static buffer,
-- which contains symbolic name of i-th row. However, if i-th row has
-- no assigned name, the routine returns NULL. */

char *lpx_get_row_name(LPX *lp, int i)
{     char *name;
      if (!(1 <= i && i <= lp->m))
         fault("lpx_get_row_name: i = %d; row number out of range", i);
      if (lp->row[i]->name == NULL)
         name = NULL;
      else
         name = get_str(lp->str_buf, lp->row[i]->name);
      return name;
}

/*----------------------------------------------------------------------
-- lpx_get_col_name - retrieve column name.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- char *lpx_get_col_name(LPX *lp, int j);
--
-- *Returns*
--
-- The routine lpx_get_col_name returns a pointer to a static buffer,
-- which contains symbolic name of j-th column. However, if j-th column
-- has no assigned name, the routine returns NULL. */

char *lpx_get_col_name(LPX *lp, int j)
{     char *name;
      if (!(1 <= j && j <= lp->n))
         fault("lpx_get_col_name: j = %d; column number out of range",
            j);
      if (lp->col[j]->name == NULL)
         name = NULL;
      else
         name = get_str(lp->str_buf, lp->col[j]->name);
      return name;
}

/*----------------------------------------------------------------------
-- lpx_get_col_kind - retrieve column kind.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_col_kind(LPX *lp, int j);
--
-- *Returns*
--
-- The routine lpx_get_col_kind returns the kind of j-th column, i.e.
-- the kind of corresponding structural variable, as follows:
--
-- LPX_CV - continuous variable;
-- LPX_IV - integer variable. */

int lpx_get_col_kind(LPX *lp, int j)
{     if (lp->klass != LPX_MIP)
         fault("lpx_get_col_kind: not a MIP problem");
      if (!(1 <= j && j <= lp->n))
         fault("lpx_get_col_kind: j = %d; column number out of range",
            j);
      return lp->col[j]->kind;
}

/*----------------------------------------------------------------------
-- lpx_get_row_type - retrieve row type.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_row_type(LPX *lp, int i);
--
-- *Returns*
--
-- The routine lpx_get_row_type returns the type of i-th row, i.e. the
-- type of corresponding auxiliary variable, as follows:
--
-- LPX_FR -g_free (unbounded) variable;
-- LPX_LO - variable with lower bound;
-- LPX_UP - variable with upper bound;
-- LPX_DB - gnm_float-bounded variable;
-- LPX_FX - fixed variable. */

int lpx_get_row_type(LPX *lp, int i)
{     if (!(1 <= i && i <= lp->m))
         fault("lpx_get_row_type: i = %d; row number out of range", i);
      return lp->row[i]->type;
}

/*----------------------------------------------------------------------
-- lpx_get_row_lb - retrieve row lower bound.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_get_row_lb(LPX *lp, int i);
--
-- *Returns*
--
-- The routine lpx_get_row_lb returns the lower bound of i-th row, i.e.
-- the lower bound of corresponding auxiliary variable. However, if the
-- row has no lower bound, the routine returns zero. */

gnm_float lpx_get_row_lb(LPX *lp, int i)
{     if (!(1 <= i && i <= lp->m))
         fault("lpx_get_row_lb: i = %d; row number out of range", i);
      return lp->row[i]->lb;
}

/*----------------------------------------------------------------------
-- lpx_get_row_ub - retrieve row upper bound.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_get_row_ub(LPX *lp, int i);
--
-- *Returns*
--
-- The routine lpx_get_row_ub returns the upper bound of i-th row, i.e.
-- the upper bound of corresponding auxiliary variable. However, if the
-- row has no upper bound, the routine returns zero. */

gnm_float lpx_get_row_ub(LPX *lp, int i)
{     if (!(1 <= i && i <= lp->m))
         fault("lpx_get_row_ub: i = %d; row number out of range", i);
      return lp->row[i]->ub;
}

/*----------------------------------------------------------------------
-- lpx_get_col_type - retrieve column type.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_col_type(LPX *lp, int j);
--
-- *Returns*
--
-- The routine lpx_get_col_type returns the type of j-th column, i.e.
-- the type of corresponding structural variable, as follows:
--
-- LPX_FR -g_free (unbounded) variable;
-- LPX_LO - variable with lower bound;
-- LPX_UP - variable with upper bound;
-- LPX_DB - gnm_float-bounded variable;
-- LPX_FX - fixed variable. */

int lpx_get_col_type(LPX *lp, int j)
{     if (!(1 <= j && j <= lp->n))
         fault("lpx_get_col_type: j = %d; column number out of range",
            j);
      return lp->col[j]->type;
}

/*----------------------------------------------------------------------
-- lpx_get_col_lb - retrieve column lower bound.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_get_col_lb(LPX *lp, int j);
--
-- *Returns*
--
-- The routine lpx_get_col_lb returns the lower bound of j-th column,
-- i.e. the lower bound of corresponding structural variable. However,
-- if the column has no lower bound, the routine returns zero. */

gnm_float lpx_get_col_lb(LPX *lp, int j)
{     if (!(1 <= j && j <= lp->n))
         fault("lpx_get_col_lb: j = %d; column number out of range", j);
      return lp->col[j]->lb;
}

/*----------------------------------------------------------------------
-- lpx_get_col_ub - retrieve column upper bound.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_get_col_ub(LPX *lp, int j);
--
-- *Returns*
--
-- The routine lpx_get_col_ub returns the upper bound of j-th column,
-- i.e. the upper bound of corresponding structural variable. However,
-- if the column has no upper bound, the routine returns zero. */

gnm_float lpx_get_col_ub(LPX *lp, int j)
{     if (!(1 <= j && j <= lp->n))
         fault("lpx_get_col_ub: j = %d; column number out of range", j);
      return lp->col[j]->ub;
}

/*----------------------------------------------------------------------
-- lpx_get_obj_coef - retrieve obj. coefficient or constant term.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_get_obj_coef(LPX *lp, int j);
--
-- *Returns*
--
-- The routine lpx_get_obj_coef returns the objective coefficient at
-- j-th structural variable (column) of the specified problem object.
-- However, if the parameter j is 0, the routine returns the constant
-- term (i.e. "shift") of the objective function. */

gnm_float lpx_get_obj_coef(LPX *lp, int j)
{     if (!(0 <= j && j <= lp->n))
         fault("lpx_get_obj_coef: j = %d; column number out of range",
            j);
      return j == 0 ? lp->c0 : lp->col[j]->coef;
}

/*----------------------------------------------------------------------
-- lpx_get_num_nz - retrieve number of constraint coefficients.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_num_nz(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_get_num_nz returns the number of (non-zero) elements
-- in the constraint matrix of the specified problem object. */

int lpx_get_num_nz(LPX *lp)
{     int count;
      count = lp->aij_pool->count;
      return count;
}

/*----------------------------------------------------------------------
-- lpx_get_mat_row - retrieve row of the constraint matrix.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_mat_row(LPX *lp, int i, int ind[], gnm_float val[]);
--
-- *Description*
--
-- The routine lpx_get_mat_row scans (non-zero) elements of i-th row
-- of the constraint matrix of the specified problem object and stores
-- their column indices and numeric values to locations ind[1], ...,
-- ind[len] and val[1], ..., val[len], respectively, where 0 <= len <= n
-- is the number of elements in i-th row, n is the number of columns.
--
-- The parameter ind or/and val can be specified as NULL, in which case
-- corresponding information is not stored.
--
-- *Returns*
--
-- The routine lpx_get_mat_row returns the length len, i.e. the number
-- of (non-zero) elements in i-th row. */

int lpx_get_mat_row(LPX *lp, int i, int ind[], gnm_float val[])
{     LPXAIJ *aij;
      int len;
      if (!(1 <= i && i <= lp->m))
         fault("lpx_get_mat_row: i = %d; row number out of range", i);
      len = 0;
      for (aij = lp->row[i]->ptr; aij != NULL; aij = aij->r_next)
      {  len++;
         if (ind != NULL) ind[len] = aij->col->j;
         if (val != NULL) val[len] = aij->val;
      }
      insist(len <= lp->n);
      return len;
}

/*----------------------------------------------------------------------
-- lpx_get_mat_col - retrieve column of the constraint matrix.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_mat_col(LPX *lp, int j, int ind[], gnm_float val[]);
--
-- *Description*
--
-- The routine lpx_get_mat_col scans (non-zero) elements of j-th column
-- of the constraint matrix of the specified problem object and stores
-- their row indices and numeric values to locations ind[1], ...,
-- ind[len] and val[1], ..., val[len], respectively, where 0 <= len <= m
-- is the number of elements in j-th column, m is the number of rows.
--
-- The parameter ind or/and val can be specified as NULL, in which case
-- corresponding information is not stored.
--
-- *Returns*
--
-- The routine lpx_get_mat_col returns the length len, i.e. the number
-- of (non-zero) elements in j-th column. */

int lpx_get_mat_col(LPX *lp, int j, int ind[], gnm_float val[])
{     LPXAIJ *aij;
      int len;
      if (!(1 <= j && j <= lp->n))
         fault("lpx_get_mat_col: j = %d; column number out of range",
            j);
      len = 0;
      for (aij = lp->col[j]->ptr; aij != NULL; aij = aij->c_next)
      {  len++;
         if (ind != NULL) ind[len] = aij->row->i;
         if (val != NULL) val[len] = aij->val;
      }
      insist(len <= lp->m);
      return len;
}

/*----------------------------------------------------------------------
-- lpx_get_rii - retrieve row scale factor.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_get_rii(LPX *lp, int i);
--
-- *Returns*
--
-- The routine lpx_get_rii returns current scale factor r[i,i] for i-th
-- row of the specified problem object. */

gnm_float lpx_get_rii(LPX *lp, int i)
{     if (!(1 <= i && i <= lp->m))
         fault("lpx_get_rii: i = %d; row number out of range", i);
      return lp->row[i]->rii;
}

/*----------------------------------------------------------------------
-- lpx_get_sjj - retrieve column scale factor.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_get_sjj(LPX *lp, int j);
--
-- *Returns*
--
-- The routine lpx_get_sjj returns current scale factor s[j,j] for j-th
-- column of the specified problem object. */

gnm_float lpx_get_sjj(LPX *lp, int j)
{     if (!(1 <= j && j <= lp->n))
         fault("lpx_get_sjj: j = %d; column number out of range", j);
      return lp->col[j]->sjj;
}

/*----------------------------------------------------------------------
-- lpx_is_b_avail - check if LP basis is available.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_is_b_avail(LPX *lp);
--
-- *Returns*
--
-- If the LP basis associated with the specified problem object exists
-- and therefore available for computations, the routine lpx_is_b_avail
-- returns non-zero. Otherwise, if the LP basis is not available, the
-- routine returns zero. */

int lpx_is_b_avail(LPX *lp)
{     int avail;
      switch (lp->b_stat)
      {  case LPX_B_UNDEF:
            avail = 0;
            break;
         case LPX_B_VALID:
            insist(lp->b_inv != NULL);
            insist(lp->b_inv->m == lp->m);
            insist(lp->b_inv->valid);
            avail = 1;
            break;
         default:
            insist(lp != lp);
      }
      return avail;
}

/*----------------------------------------------------------------------
-- lpx_get_b_info - retrieve LP basis information.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_b_info(LPX *lp, int i);
--
-- *Returns*
--
-- The routine lpx_get_b_info returns the ordinal number k of auxiliary
-- (1 <= k <= m) or structural (m+1 <= k <= m+n) variable, which is
-- basic variable xB[i], 1 <= i <= m, in the current basis associated
-- with the specified problem object, where m is the number of rows and
-- n is the number of columns. */

int lpx_get_b_info(LPX *lp, int i)
{     if (!lpx_is_b_avail(lp))
         fault("lpx_get_b_info: LP basis is not available");
      if (!(1 <= i && i <= lp->m))
         fault("lpx_get_b_info: i = %d; index out of range", i);
      return lp->basis[i];
}

/*----------------------------------------------------------------------
-- lpx_get_row_b_ind - retrieve row index in LP basis.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_row_b_ind(LPX *lp, int i);
--
-- *Returns*
--
-- The routine lpx_get_row_b_ind returns the index k of basic variable
-- xB[k], 1 <= k <= m, which is the auxiliary variable associated with
-- i-th row in the current basis for the specified problem object.
-- However, if the auxiliary variable is non-basic, the routine returns
-- zero. */

int lpx_get_row_b_ind(LPX *lp, int i)
{     if (!lpx_is_b_avail(lp))
         fault("lpx_get_row_b_ind: LP basis is not available");
      if (!(1 <= i && i <= lp->m))
         fault("lpx_get_row_b_ind: i = %d; row number out of range", i);
      return lp->row[i]->b_ind;
}

/*----------------------------------------------------------------------
-- lpx_get_col_b_ind - retrieve column index in LP basis.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_col_b_ind(LPX *lp, int j);
--
-- *Returns*
--
-- The routine lpx_get_col_b_ind returns the index k of basic variable
-- xB[k], 1 <= k <= m, which is the structural variable associated with
-- j-th column in the current basis for the specified problem object.
-- However, if the structural variable is non-basic, the routine returns
-- zero. */

int lpx_get_col_b_ind(LPX *lp, int j)
{     if (!lpx_is_b_avail(lp))
         fault("lpx_get_col_b_ind: LP basis is not available");
      if (!(1 <= j && j <= lp->n))
         fault("lpx_get_col_b_ind: j = %d; column number out of range",
            j);
      return lp->col[j]->b_ind;
}

/*----------------------------------------------------------------------
-- lpx_access_inv - access factorization of basis matrix.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- INV *lpx_access_inv(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_access_inv returns a pointer to a factorization of
-- the basis matrix for the specified problem object. The factorization
-- may not exist, in which case the routine returns NULL.
--
-- NOTE: This routine is intended for internal use only. */

INV *lpx_access_inv(LPX *lp)
{     INV *b_inv;
      b_inv = lp->b_inv;
      return b_inv;
}

/*----------------------------------------------------------------------
-- lpx_get_status - retrieve generic status of basic solution.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_status(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_get_status reports the generic status of the basic
-- solution for the specified problem object as follows:
--
-- LPX_OPT    - solution is optimal;
-- LPX_FEAS   - solution is feasible;
-- LPX_INFEAS - solution is infeasible;
-- LPX_NOFEAS - problem has no feasible solution;
-- LPX_UNBND  - problem has unbounded solution;
-- LPX_UNDEF  - solution is undefined. */

int lpx_get_status(LPX *lp)
{     int status;
      switch (lp->p_stat)
      {  case LPX_P_UNDEF:
            status = LPX_UNDEF;
            break;
         case LPX_P_FEAS:
            switch (lp->d_stat)
            {  case LPX_D_UNDEF:
                  status = LPX_FEAS;
                  break;
               case LPX_D_FEAS:
                  status = LPX_OPT;
                  break;
               case LPX_D_INFEAS:
                  status = LPX_FEAS;
                  break;
               case LPX_D_NOFEAS:
                  status = LPX_UNBND;
                  break;
               default:
                  insist(lp != lp);
            }
            break;
         case LPX_P_INFEAS:
            status = LPX_INFEAS;
            break;
         case LPX_P_NOFEAS:
            status = LPX_NOFEAS;
            break;
         default:
            insist(lp != lp);
      }
      return status;
}

/*----------------------------------------------------------------------
-- lpx_get_prim_stat - retrieve primal status of basic solution.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_prim_stat(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_get_prim_stat reports the primal status of the basic
-- solution for the specified problem object as follows:
--
-- LPX_P_UNDEF  - primal solution is undefined;
-- LPX_P_FEAS   - solution is primal feasible;
-- LPX_P_INFEAS - solution is primal infeasible;
-- LPX_P_NOFEAS - no primal feasible solution exists. */

int lpx_get_prim_stat(LPX *lp)
{     int p_stat;
      p_stat = lp->p_stat;
      return p_stat;
}

/*----------------------------------------------------------------------
-- lpx_get_dual_stat - retrieve dual status of basic solution.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_dual_stat(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_get_dual_stat reports the dual status of the basic
-- solution for the specified problem object as follows:
--
-- LPX_D_UNDEF  - dual solution is undefined;
-- LPX_D_FEAS   - solution is dual feasible;
-- LPX_D_INFEAS - solution is dual infeasible;
-- LPX_D_NOFEAS - no dual feasible solution exists. */

int lpx_get_dual_stat(LPX *lp)
{     int d_stat;
      d_stat = lp->d_stat;
      return d_stat;
}

/*----------------------------------------------------------------------
-- lpx_get_obj_val - retrieve objective value (basic solution).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_get_obj_val(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_get_obj_val returns value of the objective function
-- for basic solution. */

gnm_float lpx_get_obj_val(LPX *lp)
{     LPXCOL *col;
      int j;
      gnm_float sum;
      sum = lp->c0;
      for (j = 1; j <= lp->n; j++)
      {  col = lp->col[j];
         sum += col->coef * col->prim;
      }
      if (lp->round && gnm_abs(sum) < 1e-9) sum = 0.0;
      return sum;
}

/*----------------------------------------------------------------------
-- lpx_get_row_stat - retrieve row status (basic solution).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_row_stat(LPX *lp, int i);
--
-- *Returns*
--
-- The routine lpx_get_row_stat returns current status assigned to the
-- auxiliary variable associated with i-th row as follows:
--
-- LPX_BS - basic variable;
-- LPX_NL - non-basic variable on its lower bound;
-- LPX_NU - non-basic variable on its upper bound;
-- LPX_NF - non-basicg_free (unbounded) variable;
-- LPX_NS - non-basic fixed variable. */

int lpx_get_row_stat(LPX *lp, int i)
{     if (!(1 <= i && i <= lp->m))
         fault("lpx_get_row_stat: i = %d; row number out of range", i);
      return lp->row[i]->stat;
}

/*----------------------------------------------------------------------
-- lpx_get_row_prim - retrieve row primal value (basic solution).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_get_row_prim(LPX *lp, int i);
--
-- *Returns*
--
-- The routine lpx_get_row_prim returns primal value of the auxiliary
-- variable associated with i-th row. */

gnm_float lpx_get_row_prim(LPX *lp, int i)
{     gnm_float prim;
      if (!(1 <= i && i <= lp->m))
         fault("lpx_get_row_prim: i = %d; row number out of range", i);
      prim = lp->row[i]->prim;
      if (lp->round && gnm_abs(prim) < 1e-9) prim = 0.0;
      return prim;
}

/*----------------------------------------------------------------------
-- lpx_get_row_dual - retrieve row dual value (basic solution).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_get_row_dual(LPX *lp, int i);
--
-- *Returns*
--
-- The routine lpx_get_row_dual returns dual value (i.e. reduced cost)
-- of the auxiliary variable associated with i-th row. */

gnm_float lpx_get_row_dual(LPX *lp, int i)
{     gnm_float dual;
      if (!(1 <= i && i <= lp->m))
         fault("lpx_get_row_dual: i = %d; row number out of range", i);
      dual = lp->row[i]->dual;
      if (lp->round && gnm_abs(dual) < 1e-9) dual = 0.0;
      return dual;
}

/*----------------------------------------------------------------------
-- lpx_get_col_stat - retrieve column status (basic solution).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_col_stat(LPX *lp, int j);
--
-- *Returns*
--
-- The routine lpx_get_col_stat returns current status assigned to the
-- structural variable associated with j-th column as follows:
--
-- LPX_BS - basic variable;
-- LPX_NL - non-basic variable on its lower bound;
-- LPX_NU - non-basic variable on its upper bound;
-- LPX_NF - non-basicg_free (unbounded) variable;
-- LPX_NS - non-basic fixed variable. */

int lpx_get_col_stat(LPX *lp, int j)
{     if (!(1 <= j && j <= lp->n))
         fault("lpx_get_col_stat: j = %d; column number out of range",
            j);
      return lp->col[j]->stat;
}

/*----------------------------------------------------------------------
-- lpx_get_col_prim - retrieve column primal value (basic solution).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_get_col_prim(LPX *lp, int j);
--
-- *Returns*
--
-- The routine lpx_get_col_prim returns primal value of the structural
-- variable associated with j-th column. */

gnm_float lpx_get_col_prim(LPX *lp, int j)
{     gnm_float prim;
      if (!(1 <= j && j <= lp->n))
         fault("lpx_get_col_prim: j = %d; column number out of range",
            j);
      prim = lp->col[j]->prim;
      if (lp->round && gnm_abs(prim) < 1e-9) prim = 0.0;
      return prim;
}

/*----------------------------------------------------------------------
-- lpx_get_col_dual - retrieve column dual value (basic solution).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_get_col_dual(LPX *lp, int j);
--
-- *Returns*
--
-- The routine lpx_get_col_dual returns dual value (i.e. reduced cost)
-- of the structural variable associated with j-th column. */

gnm_float lpx_get_col_dual(LPX *lp, int j)
{     gnm_float dual;
      if (!(1 <= j && j <= lp->n))
         fault("lpx_get_col_dual: j = %d; column number out of range",
            j);
      dual = lp->col[j]->dual;
      if (lp->round && gnm_abs(dual) < 1e-9) dual = 0.0;
      return dual;
}

/*----------------------------------------------------------------------
-- lpx_get_ray_info - retrieve row/column which causes unboundness.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_get_ray_info(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_get_ray_info returns the number k of some non-basic
-- variable x[k], which causes primal unboundness. If such a variable
-- cannot be identified, the routine returns zero.
--
-- If 1 <= k <= m, x[k] is the auxiliary variable associated with k-th
-- row, if m+1 <= k <= m+n, x[k] is the structural variable associated
-- with (k-m)-th column, where m is the number of rows, n is the number
-- of columns in the LP problem object. */

int lpx_get_ray_info(LPX *lp)
{     int k;
      k = lp->some;
      return k;
}

/*----------------------------------------------------------------------
-- lpx_ipt_status - retrieve status of interior-point solution.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_ipt_status(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_ipt_status reports the status of solution found by
-- interior-point solver as follows:
--
-- LPX_T_UNDEF - interior-point solution is undefined;
-- LPX_T_OPT   - interior-point solution is optimal. */

int lpx_ipt_status(LPX *lp)
{     int t_stat;
      t_stat = lp->t_stat;
      return t_stat;
}

/*----------------------------------------------------------------------
-- lpx_ipt_obj_val - retrieve objective value (interior point).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_ipt_obj_val(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_ipt_obj_val returns value of the objective function
-- for interior-point solution. */

gnm_float lpx_ipt_obj_val(LPX *lp)
{     LPXCOL *col;
      int j;
      gnm_float sum;
      sum = lp->c0;
      for (j = 1; j <= lp->n; j++)
      {  col = lp->col[j];
         sum += col->coef * col->pval;
      }
      if (lp->round && gnm_abs(sum) < 1e-9) sum = 0.0;
      return sum;
}

/*----------------------------------------------------------------------
-- lpx_ipt_row_prim - retrieve row primal value (interior point).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_ipt_row_prim(LPX *lp, int i);
--
-- *Returns*
--
-- The routine lpx_ipt_row_prim returns primal value of the auxiliary
-- variable associated with i-th row. */

gnm_float lpx_ipt_row_prim(LPX *lp, int i)
{     gnm_float pval;
      if (!(1 <= i && i <= lp->m))
         fault("lpx_ipt_row_prim: i = %d; row number out of range", i);
      pval = lp->row[i]->pval;
      if (lp->round && gnm_abs(pval) < 1e-9) pval = 0.0;
      return pval;
}

/*----------------------------------------------------------------------
-- lpx_ipt_row_dual - retrieve row dual value (interior point).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_ipt_row_dual(LPX *lp, int i);
--
-- *Returns*
--
-- The routine lpx_ipt_row_dual returns dual value (i.e. reduced cost)
-- of the auxiliary variable associated with i-th row. */

gnm_float lpx_ipt_row_dual(LPX *lp, int i)
{     gnm_float dval;
      if (!(1 <= i && i <= lp->m))
         fault("lpx_ipt_row_dual: i = %d; row number out of range", i);
      dval = lp->row[i]->dval;
      if (lp->round && gnm_abs(dval) < 1e-9) dval = 0.0;
      return dval;
}

/*----------------------------------------------------------------------
-- lpx_ipt_col_prim - retrieve column primal value (interior point).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_ipt_col_prim(LPX *lp, int j);
--
-- *Returns*
--
-- The routine lpx_ipt_col_prim returns primal value of the structural
-- variable associated with j-th column. */

gnm_float lpx_ipt_col_prim(LPX *lp, int j)
{     gnm_float pval;
      if (!(1 <= j && j <= lp->n))
         fault("lpx_ipt_col_prim: j = %d; column number out of range",
            j);
      pval = lp->col[j]->pval;
      if (lp->round && gnm_abs(pval) < 1e-9) pval = 0.0;
      return pval;
}

/*----------------------------------------------------------------------
-- lpx_ipt_col_dual - retrieve column dual value (interior point).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_ipt_col_dual(LPX *lp, int j);
--
-- *Returns*
--
-- The routine lpx_ipt_col_dual returns dual value (i.e. reduced cost)
-- of the structural variable associated with j-th column. */

gnm_float lpx_ipt_col_dual(LPX *lp, int j)
{     gnm_float dval;
      if (!(1 <= j && j <= lp->n))
         fault("lpx_ipt_col_dual: j = %d; column number out of range",
            j);
      dval = lp->col[j]->dval;
      if (lp->round && gnm_abs(dval) < 1e-9) dval = 0.0;
      return dval;
}

/*----------------------------------------------------------------------
-- lpx_mip_status - retrieve status of MIP solution.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_mip_status(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_mip_status reports the status of MIP solution found
-- by branch-and-bound solver as follows:
--
-- LPX_I_UNDEF  - MIP solution is undefined;
-- LPX_I_OPT    - MIP solution is integer optimal;
-- LPX_I_FEAS   - MIP solution is integer feasible but its optimality
--                (or non-optimality) has not been proven, perhaps due
--                to premature termination of the search;
-- LPX_I_NOFEAS - problem has no integer feasible solution (proven by
--                the solver). */

int lpx_mip_status(LPX *lp)
{     int i_stat;
      if (lp->klass != LPX_MIP)
         fault("lpx_mip_status: not a MIP problem");
      i_stat = lp->i_stat;
      return i_stat;
}

/*----------------------------------------------------------------------
-- lpx_mip_obj_val - retrieve objective value (MIP solution).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_mip_obj_val(LPX *lp);
--
-- *Returns*
--
-- The routine lpx_mip_obj_val returns value of the objective function
-- for MIP solution. */

gnm_float lpx_mip_obj_val(LPX *lp)
{     LPXCOL *col;
      int j;
      gnm_float sum;
      if (lp->klass != LPX_MIP)
         fault("lpx_mip_obj_val: not a MIP problem");
      sum = lp->c0;
      for (j = 1; j <= lp->n; j++)
      {  col = lp->col[j];
         sum += col->coef * col->mipx;
      }
      if (lp->round && gnm_abs(sum) < 1e-9) sum = 0.0;
      return sum;
}

/*----------------------------------------------------------------------
-- lpx_mip_row_val - retrieve row value (MIP solution).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_mip_row_val(LPX *lp, int i);
--
-- *Returns*
--
-- The routine lpx_mip_row_val returns value of the auxiliary variable
-- associated with i-th row. */

gnm_float lpx_mip_row_val(LPX *lp, int i)
{     gnm_float mipx;
      if (lp->klass != LPX_MIP)
         fault("lpx_mip_row_val: not a MIP problem");
      if (!(1 <= i && i <= lp->m))
         fault("lpx_mip_row_val: i = %d; row number out of range", i);
      mipx = lp->row[i]->mipx;
      if (lp->round && gnm_abs(mipx) < 1e-9) mipx = 0.0;
      return mipx;
}

/*----------------------------------------------------------------------
-- lpx_mip_col_val - retrieve column value (MIP solution).
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- gnm_float lpx_mip_col_val(LPX *lp, int j);
--
-- *Returns*
--
-- The routine lpx_mip_col_val returns value of the structural variable
-- associated with j-th column. */

gnm_float lpx_mip_col_val(LPX *lp, int j)
{     gnm_float mipx;
      if (lp->klass != LPX_MIP)
         fault("lpx_mip_col_val: not a MIP problem");
      if (!(1 <= j && j <= lp->n))
         fault("lpx_mip_col_val: j = %d; column number out of range",
            j);
      mipx = lp->col[j]->mipx;
      if (lp->round && gnm_abs(mipx) < 1e-9) mipx = 0.0;
      return mipx;
}

/*----------------------------------------------------------------------
-- Obsolete API routines are kept for backward compatibility and will
-- be removed in the future. */

void lpx_get_row_bnds(LPX *lp, int i, int *typx, gnm_float *lb, gnm_float *ub)
{     /* obtain row bounds */
      if (typx != NULL) *typx = lpx_get_row_type(lp, i);
      if (lb != NULL) *lb = lpx_get_row_lb(lp, i);
      if (ub != NULL) *ub = lpx_get_row_ub(lp, i);
      return;
}

void lpx_get_col_bnds(LPX *lp, int j, int *typx, gnm_float *lb, gnm_float *ub)
{     /* obtain column bounds */
      if (typx != NULL) *typx = lpx_get_col_type(lp, j);
      if (lb != NULL) *lb = lpx_get_col_lb(lp, j);
      if (ub != NULL) *ub = lpx_get_col_ub(lp, j);
      return;
}

void lpx_get_row_info(LPX *lp, int i, int *tagx, gnm_float *vx, gnm_float *dx)
{     /* obtain row solution information */
      if (tagx != NULL) *tagx = lpx_get_row_stat(lp, i);
      if (vx != NULL) *vx = lpx_get_row_prim(lp, i);
      if (dx != NULL) *dx = lpx_get_row_dual(lp, i);
      return;
}

void lpx_get_col_info(LPX *lp, int j, int *tagx, gnm_float *vx, gnm_float *dx)
{     /* obtain column solution information */
      if (tagx != NULL) *tagx = lpx_get_col_stat(lp, j);
      if (vx != NULL) *vx = lpx_get_col_prim(lp, j);
      if (dx != NULL) *dx = lpx_get_col_dual(lp, j);
      return;
}

/* eof */
