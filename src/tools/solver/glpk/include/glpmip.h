/* glpmip.h (branch-and-bound method) */

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

#ifndef _GLPMIP_H
#define _GLPMIP_H

#include "gnumeric-config.h"
#include "gnumeric.h"
#include "numbers.h"

#include "glplpx.h"

#define mip_create_tree       glp_mip_create_tree
#define mip_revive_node       glp_mip_revive_node
#define mip_freeze_node       glp_mip_freeze_node
#define mip_clone_node        glp_mip_clone_node
#define mip_delete_node       glp_mip_delete_node
#define mip_delete_tree       glp_mip_delete_tree
#define mip_pseudo_root       glp_mip_pseudo_root
#define mip_best_node         glp_mip_best_node
#define mip_relative_gap      glp_mip_relative_gap
#define mip_solve_node        glp_mip_solve_node
#define mip_driver            glp_mip_driver

typedef struct MIPTREE MIPTREE; /* branch-and-bound tree */
typedef struct MIPSLOT MIPSLOT; /* node subproblem slot */
typedef struct MIPNODE MIPNODE; /* node subproblem descriptor */
typedef struct MIPBNDS MIPBNDS; /* bounds change entry */
typedef struct MIPSTAT MIPSTAT; /* status change entry */

struct MIPTREE
{     /* branch-and-bound tree */
      /*--------------------------------------------------------------*/
      /* global information (valid for all subproblems) */
      int m;
      /* number of rows */
      int n;
      /* number of columns */
      int dir;
      /* optimization direction:
         LPX_MIN - minimization
         LPX_MAX - maximization */
      int int_obj;
      /* if this flag is set, the objective function is integral */
      int *int_col; /* int int_col[1+n]; */
      /* integer column flags;
         int_col[0] is not used;
         int_col[j], 1 <= j <= n, is the flag of j-th column; if this
         flag is set, corresponding structural variable is required to
         be integer */
      /*--------------------------------------------------------------*/
      /* memory management */
      DMP *node_pool;
      /* memory pool for MIPNODE objects */
      DMP *bnds_pool;
      /* memory pool for MIPBNDS objects */
      DMP *stat_pool;
      /* memory pool for MIPSTAT objects */
      /*--------------------------------------------------------------*/
      /* branch-and-bound tree */
      int nslots;
      /* length of the array of slots (increased automatically) */
      int avail;
      /* index of first free slot; 0 means all slots are in use */
      MIPSLOT *slot; /* MIPSLOT slot[1+nslots]; */
      /* array of slots:
         slot[0] is never used;
         slot[p], 1 <= p <= nslots, either contains a pointer to some
         node of the branch-and-bound tree, in which case p is used on
         api level as the reference number of corresponding subproblem,
         or is free; all free slots are linked into single linked list;
         slot[1] always contains a pointer to the root node (it is free
         only if the tree is empty) */
      MIPNODE *head;
      /* pointer to the head of the active list */
      MIPNODE *tail;
      /* pointer to the tail of the active list */
      /* the active list is a doubly linked list of active subproblems
         which correspond to leaves of the tree; all subproblems in the
         active list are ordered chronologically (each a new subproblem
         is always added to the tail of the list) */
      int a_cnt;
      /* current number of active nodes (including the current one) */
      int n_cnt;
      /* current number of all (active and inactive) nodes */
      int t_cnt;
      /* total number of nodes including those which have been already
         removed from the tree; this count is increased whenever a new
         node is created and never decreased */
      /*--------------------------------------------------------------*/
      /* best known integer feasible solution */
      int found;
      /* if this flag is set, at least one integer feasible solution has
         been found */
      gnm_float best;
      /* incumbent objective value, that is the objective value which
         corresponds to the best known integer feasible solution (it is
         undefined if the flag found is not set); this value is a global
         upper (minimization) or lower (maximization) bound for integer
         optimal solution of the original problem being solved */
      gnm_float *mipx; /* gnm_float mipx[1+m+n]; */
      /* values of auxiliary and structural variables which correspond
         to the best known integer feasible solution (these are values
         are undefined if the flag found is not set):
         mipx[0] is not used;
         mipx[i], 1 <= i <= m, is a value of i-th auxiliary variable;
         mipx[m+j], 1 <= j <= n, is a value of j-th structural variable
         note: if j-th structural variable is required to be integer,
         its value mipx[m+j] is provided to be integral */
      /*--------------------------------------------------------------*/
      /* current subproblem and its LP relaxation */
      MIPNODE *curr;
      /* pointer to the current subproblem (which can be only active);
         NULL means the current subproblem does not exist */
      LPX *lp;
      /* LP relaxation of the current subproblem (this problem object
         contains global data valid for all subproblems, namely, the
         sets of rows and columns, objective coefficients, as well as
         the constraint matrix; only bounds and statuses of some rows
         and/or columns may be changed for a particular subproblem) */
      int *old_type;  /* int old_type[1+m+n]; */
      gnm_float *old_lb; /* gnm_float old_lb[1+m+n]; */
      gnm_float *old_ub; /* gnm_float old_ub[1+m+n]; */
      int *old_stat;  /* int old_stat[1+m+n]; */
      /* these four arrays contain attributes of rows and columns which
         they have in the parent subproblem (types, lower bounds, upper
         bounds, and statuses); this information is used to build change
         lists on freezing the current subproblem; note that if the root
         subproblem is current, standard attributes of rows and columns
         are used (all rows are free and basic, all columns are fixed at
         zero and non-basic) */
      int *non_int; /* int non_int[1+n]; */
      /* these column flags are set once LP relaxation of the current
         subproblem has been solved;
         non_int[0] is not used;
         non_int[j], 1 <= j <= n, is the flag of j-th column; if this
         flag is set, corresponding structural variable is required to
         be integer, but its value in basic solution is fractional */
      /*--------------------------------------------------------------*/
      /* control parameters and statistics */
      int msg_lev;
      /* level of messages issued by the solver:
         0 - no output
         1 - error messages only
         2 - normal output
         3 - detailed step-by-step output */
      int branch; /* MIP */
      /* branching heuristic:
         0 - branch on first variable
         1 - branch on last variable
         2 - branch using heuristic by Driebeck and Tomlin
         3 - branch on most fractional variable */
      int btrack; /* MIP */
      /* backtracking heuristic:
         0 - select most recent node (depth first search)
         1 - select earliest node (breadth first search)
         2 - select node using the best projection heuristic
         3 - select node with best local bound */
      gnm_float tol_int;
      /* absolute tolerance used to check if the current basic solution
         is integer feasible */
      gnm_float tol_obj;
      /* relative tolerance used to check if the value of the objective
         function is better than the incumbent objective value */
      gnm_float tm_lim;
      /* searching time limit, in seconds; if this value is positive,
         it is decreased whenever one complete round of the search is
         performed by the amount of time spent for the round, and
         reaching zero value signals the solver to stop the search;
         negative value means no time limit */
      gnm_float out_frq;
      /* output frequency, in seconds; this parameter specifies how
         frequently the solver sends information about the progress of
         the search to the standard output */
      gnm_float out_dly;
      /* output delay, in seconds; this parameter specifies how long
         output from the LP solver is delayed on solving LP relaxation
         of the current subproblem; zero value means no delay */
      gnm_float tm_beg;
      /* starting time of the search, in seconds; the total time of the
         search is the difference between utime() and tm_beg */
      gnm_float tm_lag;
      /* the most recent time, in seconds, at which the progress of the
         the search was displayed */
};

struct MIPSLOT
{     /* node subproblem slot */
      MIPNODE *node;
      /* pointer to subproblem descriptor; NULL means free slot */
      int next;
      /* index of another free slot (only if this slot is free) */
};

struct MIPNODE
{     /* node subproblem descriptor */
      int p;
      /* subproblem reference number (it is the index to corresponding
         slot, i.e. slot[p] points to this descriptor) */
      MIPNODE *up;
      /* pointer to parent subproblem; NULL means this node is the root
         of the tree, in which case p = 1 */
      int level;
      /* node level (the root node has level 0) */
      int count;
      /* if count = 0, this subproblem is active; if count > 0, this
         subproblem is inactive, in which case count is the number of
         its child subproblems */
      MIPBNDS *bnds;
      /* linked list of rows and columns whose types and bounds were
         changed; this list is destroyed on reviving and built anew on
         freezing the subproblem */
      MIPSTAT *stat;
      /* linked list of rows and columns whose statuses were changed;
         this list is destroyed on reviving and built anew on freezing
         the subproblem */
      gnm_float bound;
      /* local lower (minimization) or upper (maximization) bound of
         integer optimal solution of *this* subproblem; this bound is
         local in the sense that only subproblems in the subtree rooted
         at this node cannot have better integer feasible solutions;
         on creating a subproblem its local bound is inherited from its
         parent and then can be made stronger (never weaker); for the
         root subproblem its local bound is initially set to -DBL_MAX
         (minimization) or +DBL_MAX (maximization) and then improved as
         the root LP relaxation has been solved */
      /* if this subproblem is inactive, the following two quantities
         correspond to final optimal solution of its LP relaxation; for
         active subproblems these quantities are undefined */
      int ii_cnt;
      /* number of columns (structural variables) of integer kind whose
         primal values are fractional */
      gnm_float ii_sum;
      /* the sum of integer infeasibilities */
      MIPNODE *temp;
      /* auxiliary pointer used by some routines */
      MIPNODE *prev;
      /* pointer to previous subproblem in the active list */
      MIPNODE *next;
      /* pointer to next subproblem in the active list */
};

struct MIPBNDS
{     /* bounds change entry */
      int k;
      /* ordinal number of corresponding row (1 <= k <= m) or column
         (m+1 <= k <= m+n) */
      int type;
      /* new type */
      gnm_float lb;
      /* new lower bound */
      gnm_float ub;
      /* new upper bound */
      MIPBNDS *next;
      /* pointer to next entry for the same subproblem */
};

struct MIPSTAT
{     /* status change entry */
      int k;
      /* ordinal number of corresponding row (1 <= k <= m) or column
         (m+1 <= k <= m+n) */
      int stat;
      /* new status */
      MIPSTAT *next;
      /* pointer to next entry for the same subproblem */
};

/* exit codes returned by the routine mip_driver: */
#define MIP_E_OK        1200  /* the search is completed */
#define MIP_E_ITLIM     1201  /* iterations limit exhausted */
#define MIP_E_TMLIM     1202  /* time limit exhausted */
#define MIP_E_ERROR     1203  /* error on solving LP relaxation */

MIPTREE *mip_create_tree(int m, int n, int dir);
/* create branch-and-bound tree */

void mip_revive_node(MIPTREE *tree, int p);
/* revive specified subproblem */

void mip_freeze_node(MIPTREE *tree);
/* freeze current subproblem */

void mip_clone_node(MIPTREE *tree, int p, int nnn, int ref[]);
/* clone specified subproblem */

void mip_delete_node(MIPTREE *tree, int p);
/* delete specified subproblem */

void mip_delete_tree(MIPTREE *tree);
/* delete branch-and-bound tree */

int mip_pseudo_root(MIPTREE *tree);
/* find pseudo-root of the branch-and-bound tree */

int mip_best_node(MIPTREE *tree);
/* find active node with best local bound */

gnm_float mip_relative_gap(MIPTREE *tree);
/* compute relative mip gap */

int mip_solve_node(MIPTREE *tree);
/* solve LP relaxation of current subproblem */

int mip_driver(MIPTREE *tree);
/* branch-and-bound driver */

#endif

/* eof */
