/* glplp.h */

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

#ifndef _GLPLP_H
#define _GLPLP_H

#include "gnumeric-config.h"
#include "gnumeric.h"
#include "numbers.h"

#include "glpmat.h"

#define check_lp              glp_check_lp
#define create_lp             glp_create_lp
#define create_lpsol          glp_create_lpsol
#define delete_lp             glp_delete_lp
#define delete_lpsol          glp_delete_lpsol
#define extract_prob          glp_extract_prob
#define prepro_lp             glp_prepro_lp

/* the structure LP is a linear programming problem data block that
-- corresponds to the following problem formulation:
--
-- minimize or maximize
--
--    Z = c[1]*x[m+1] + c[2]*x[m+2] + ... + c[n]*x[m+n] + c[0]
--
-- subject to linear constraints
--
--    x[1] = a[1,1]*x[m+1] + a[1,2]*x[m+2] + ... + a[1,n]*x[m+n]
--    x[2] = a[2,1]*x[m+1] + a[2,2]*x[m+2] + ... + a[2,n]*x[m+n]
--             . . . . . .
--    x[m] = a[m,1]*x[m+1] + a[m,2]*x[m+2] + ... + a[m,n]*x[m+n]
--
-- and bounds of variables
--
--      l[1] <= x[1]   <= u[1]
--      l[2] <= x[2]   <= u[2]
--          . . . . . .
--    l[m+n] <= x[m+n] <= u[m+n]
--
-- where:
-- x[1], ..., x[m]      - rows (auxiliary variables);
-- x[m+1], ..., x[m+n]  - columns (structural variables);
-- Z                    - objective function;
-- c[1], ..., c[n]      - coefficients of the objective function (at
--                        structural variables);
-- c[0]                 - constant term of the objective function;
-- a[1,1], ..., a[m,n]  - constraint coefficients;
-- l[1], ..., l[m+n]    - lower bounds of variables;
-- u[1], ..., u[m+n]    - upper bounds of variables. */

typedef struct LP LP;

struct LP
{     /* linear programming (LP) problem data block */
      int m;
      /* number of rows (auxiliary variables) */
      int n;
      /* number of columns (structural variables) */
      int *kind; /* int kind[1+n]; */
      /* if this field is NULL, the problem is pure LP; otherwise the
         problem is mixed integer linear programming problem (MIP); in
         the latter case kind[0] is not used, and kind[j] is a kind of
         the structural variable x[m+j] (1 <= j <= n): zero means the
         continuous variable, non-zero means the integer variable */
      int *type; /* int type[1+m+n]; */
      /* type[0] is not used; type[k] specifies the type of variable
         x[k] (1 <= k <= m+n):
         'F' - free variable:    -inf <  x[k] < +inf
         'L' - lower bound:      l[k] <= x[k] < +inf
         'U' - upper bound:      -inf <  x[k] <= u[k]
         'D' - gnum_float bound:     l[k] <= x[k] <= u[k]
         'S' - fixed variable:   l[k]  = x[k]  = u[k] */
      gnum_float *lb; /* gnum_float lb[1+m+n]; */
      /* lb[0] is not used; lb[k] is the lower bound of variable x[k]
         (1 <= k <= m+n); if x[k] has no lower bound, lb[k] is zero */
      gnum_float *ub; /* gnum_float ub[1+m+n]; */
      /* ub[0] is not used; ub[k] is the upper bound of variable x[k]
         (1 <= k <= m+n); if x[k] has no upper bound, ub[k] is zero;
         if x[k] is fixed variable, lb[k] is equal to ub[k] */
      MAT *A; /* MAT A[1:m,1:n]; */
      /* matrix of constraint coefficients (m rows and n columns) */
      int dir;
      /* optimization direction flag:
         '-' - objective function should be minimized
         '+' - objective function should be maximized */
      gnum_float *c; /* gnum_float c[1+n]; */
      /* c[0] is the constant term of the objective function; c[j] is
         the coefficient of the objective function at the (structural)
         variable x[m+j] (1 <= j <= n) */
};

/* the structure LPSOL is a linear programming problem basis solution
-- block that corresponds to the following simplex table:
--
--        Z =      d[1]*xN[1] +      d[2]*xN[2] + ... +      d[n]*xN[n]
--    xB[1] = alfa[1,1]*xN[1] + alfa[1,2]*xN[2] + ... + alfa[1,n]*xN[n]
--    xB[2] = alfa[2,1]*xN[1] + alfa[2,2]*xN[2] + ... + alfa[2,n]*xN[n]
--             . . . . . .
--    xB[m] = alfa[m,1]*xN[1] + alfa[m,2]*xN[2] + ... + alfa[m,n]*xN[n]
--
-- where:
-- Z                         - the objective function;
-- xB[1], ..., xB[m]         - basic variables;
-- xN[1], ..., xN[n]         - non-basic variables;
-- d[1], ..., d[n]           - reduced costs (marginal values);
-- alfa[1,1], ..., alfa[m,n] - elements of the matrix (-inv(B)*N).
--
-- Note that signs of reduced costs are determined by the given simplex
-- table for both minimization and maximization. */

typedef struct LPSOL LPSOL;

struct LPSOL
{     /* linear programming (LP) problem basis solution block */
      int m;
      /* number of rows (auxiliary variables) */
      int n;
      /* number of columns (structural variables) */
      int mipsol;
      /* if this flag is zero, the block contains solution of pure LP
         problem; otherwise it contains solution of MIP problem */
      int status;
      /* solution status:
         '?' - solution is undefined
         'O' - solution is optimal (integer optimal)
         'F' - solution is feasible (integer feasible)
         'I' - solution is infeasible (integer infeasible)
         'N' - problem has no feasible (integer feasible) solution
         'U' - problem has unbounded solution */
      gnum_float objval;
      /* value of the objective function */
      int *tagx; /* int tagx[1+m+n]; */
      /* tagx[0] is not used; tagx[k] is the status of variable x[k]
         (1 <= k <= m+n):
         '?' - status is undefined
         'B' - basic variable
         'L' - non-basic variable on its lower bound
         'U' - non-basic variable on its upper bound
         'F' - non-basic free variable
         'S' - non-basic fixed variable */
      gnum_float *valx; /* gnum_float valx[1+m+n]; */
      /* valx[0] is not used; valx[k] is the value of the variable x[k]
         (1 <= k <= m+n) */
      gnum_float *dx; /* gnum_float dx[1+m+n]; */
      /* dx[0] is not used; dx[k] is the reduced cost (marginal value)
         of the variable x[k] (1 <= k <= m+n) */
};

extern void check_lp(LP *lp);
/* check LP data block for correctness */

extern LP *create_lp(int m, int n, int mip);
/* create linear programming problem data block */

extern LPSOL *create_lpsol(int m, int n);
/* create linear programming problem solution block */

extern void delete_lp(LP *lp);
/* delete linear programming problem data block */

extern void delete_lpsol(LPSOL *sol);
/* delete linear programming problem solution block */

extern LP *extract_prob(void *lpi);
/* extract LP problem data from LPI object */

extern int prepro_lp(LP *lp);
/* perform preprocessing LP/MIP problem */

#endif

/* eof */
