/* glpbbm.h */

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

#ifndef _GLPBBM_H
#define _GLPBBM_H

#include "gnumeric-config.h"
#include "gnumeric.h"
#include "numbers.h"

#include "glprsm.h"

#define bbm1_driver           glp_bbm1_driver
#define branch_drtom          glp_branch_drtom
#define branch_first          glp_branch_first
#define branch_last           glp_branch_last
#define btrack_bestp          glp_btrack_bestp
#define btrack_fifo           glp_btrack_fifo
#define btrack_lifo           glp_btrack_lifo

typedef struct BBDATA BBDATA;
typedef struct BBNODE BBNODE;
typedef struct BBTAGX BBTAGX;

struct BBDATA
{     /* branch-and-bound main data block */
      int m;
      /* number of rows (auxiliary variables) */
      int n;
      /* number of columns (structural variables) */
      LP *mip;
      /* original MIP problem data block (the constraint matrix in this
         block is not used; instead that the expanded constraint matrix
         placed in RSM block below is used) */
      int *tagx;  /* int tagx[1+m+n]; */
      /* tagx[0] is not used; tagx[k] is the status of variable x[k]
         (1 <= k <= m+n) in the optimal basis solution found for the
         relaxed original MIP problem (i.e. when all integer variables
         are considered as continuous):
         'B' - basic variable
         'L' - non-basic variable on its lower bound
         'U' - non-basic variable on its upper bound
         'F' - non-basic free variable
         'S' - non-basic fixed variable */
      POOL *node_pool;
      /* memory pool for BBNODE objects */
      POOL *tagx_pool;
      /* memory pool for BBTAGX objects */
      BBNODE *root;
      /* pointer to the root node */
      BBNODE *first, *last;
      /* pointers to the first and to the last problems respectively in
         the active list (new problem nodes are always added to the end
         of this linked list) */
      int count;
      /* current number of problem nodes in the active list */
      int total;
      /* total number of problems which have been solved yet (some of
         these problems can be still in the tree in case if they have
         active descendant subproblems) */
      int found;
      /* this flag is set if the solver has found at least one integer
         feasible solution */
      gnm_float best;
      /* value of the objective function for the best integer feasible
         solution found by the solver (if the flag found is clear, this
         value is undefined; in the case of maximization this value has
         opposite sign) */
      RSM *rsm;
      /* revised simplex method common block (this block contains all
         information related to the current active problem) */
      gnm_float *bbar; /* gnm_float bbar[1+m]; */
      /* values of basic variables (for the current active problem) */
};

struct BBNODE
{     /* node of the branch-and-bound tree */
      BBNODE *up;
      /* pointer to the parent problem (or NULL, if this problem is the
         root of the tree) */
      int j;
      /* number of the structural integer variable xS[j] (1 <= j <= n)
         which has been chosen to create this problem (not used for the
         root node) */
      int type;
      /* type of new bound of the variable xS[j] (not used for the root
         node):
         'L' - xS[j] >= new lower bound
         'U' - xS[j] <= new upper bound */
      gnm_float bound;
      /* new lower (if type = 'L') or new upper (if type = 'U') bound
         for the variable xS[j] (not used for the root node) */
      int solved;
      /* this problem status:
         0 - problem has not been solved yet (active problem)
         1 - problem has been solved and divided in two subproblems */
      BBTAGX *diff;
      /* if this problem is active (solved = 0), this field is NULL; if
         this problem has been solved (solved = 1), this field points to
         the linked list which shows in what variables an optimal basis
         of this problem differs from an optimal basis of its parent (in
         case of the root problem this field is NULL, because the root
         problem has no parent) */
      gnm_float objval;
      /* value of the objective function for optimal solution (if this
         problem has not been solved yet, this value is undefined); in
         the case of maximization this value has opposite sign */
      gnm_float infsum;
      /* sum of integer infeasibilites (if this problem has not been
         solved yet, this sum is undefined) */
      BBNODE *left, *right;
      /* if this problem is active (solved = 0), left and right point
         respectively to the previous and to the next active problems;
         if this problem is solved (solved = 1), left and right point
         respectively to the first and to the second child problems in
         which this problem has been divided; in the latter case one of
         these pointers may be NULL that means that the corresponding
         child problem has been fathomed; both these pointers can't be
         NULL at the same time, because if this happens, this problem
         is also considered as fathomed and automatically removed from
         the tree by the solver */
      void *temp;
      /* temporary pointer (used for auxiliary purposes) */
};

struct BBTAGX
{     /* status of a variable */
      int k;
      /* number of some variable x[k] (1 <= k <= m+n) */
      int tagx;
      /* status of the variable x[k] (has the same meaning as elements
         of the array tagx in BBDATA; see above) */
      BBTAGX *next;
      /* pointer to the next entry */
};

extern int branch_drtom(BBDATA *bb, int *what);
/* choose a branch using Driebeek and Tomlin heuristic */

extern int branch_first(BBDATA *bb, int *what);
/* choose the first appropriate branching variable */

extern int branch_last(BBDATA *bb, int *what);
/* choose the last appropriate branching variable */

extern BBNODE *btrack_bestp(BBDATA *bb);
/* select problem using the best projection heuristic */

extern BBNODE *btrack_fifo(BBDATA *bb);
/* select active problem using FIFO heuristic */

extern BBNODE *btrack_lifo(BBDATA *bb);
/* select active problem using LIFO heuristic */

struct bbm1_cp
{     /* control parameters passed to the bbm1_driver routine */
      int what;
      /* this parameter specifies what basis solution should be found
         by the solver:
         0 - initial relaxed solution
         1 - feasible integer solution
         2 - optimal integer solution */
      int branch;
      /* this parameter specifies what branching heuristic should be
         used by the solver: */
#define BB_FIRST  0  /* branch on the first variable */
#define BB_LAST   1  /* branch on the last variable */
#define BB_DRTOM  2  /* branch using heuristic by Driebeck and Tomlin */
      int btrack;
      /* this parameter specifies what backtracking heuristic should be
         used by the solver: */
#define BB_FIFO   0  /* backtrack using FIFO heuristic */
#define BB_LIFO   1  /* backtrack using LIFO heuristic */
#define BB_BESTP  2  /* backtrack using the best projection heuristic */
      gnm_float tol_int;
      /* absolute tolerance which is used to see if the solution is
         integer feasible */
      gnm_float tol_obj;
      /* relative tolerance which is used to check if current value of
         the objective function is not better than for the best integer
         feasible solution found */
      int steep;
      /* if this flag is set, the solver uses the steepest edge pricing
         proposed by Goldfarb & Reid; otherwise the standard "textbook"
         pricing is used */
      int relax;
      /* if this flag is set, the solver uses two-pass ratio test
         proposed by P.Harris; otherwise the standard "textbook" ratio
         test is used */
      gnm_float tol_bnd;
      /* relative tolerance which is used to see if the solution is
         primal feasible */
      gnm_float tol_dj;
      /* relative tolerance which is used to see if the solution is
         dual feasible */
      gnm_float tol_piv;
      /* relative tolerance which is used to choose the pivot element
         of the simplex table */
      int it_lim;
      /* simplex iterations limit; if this value is positive, it is
         decreased by one each time when one simplex iteration has been
         performed, and reaching zero value signals the solver to stop
         the search; negative value means no iterations limit */
      int it_cnt;
      /* simplex iterations count; this count is increased by one each
         time when one simplex iteration has been performed */
      gnm_float tm_lim;
      /* searching time limit, in seconds; if this value is positive,
         it is decreased each time when one simplex iteration has been
         performed by the amount of time spent for the iteration, and
         reaching zero value signals the solver to stop the search;
         negative value means no time limit */
      int round;
      /* if this flag is set, the solver replaces computed values of
         basic continuous variables which are close to zero by exact
         zeros; otherwise all computed values are remained "as is" */
};

extern int bbm1_driver(LP *lp, LPSOL *sol, struct bbm1_cp *cp);
/* driver for the branch-and-bound method */

#endif

/* eof */
