/* glpbbm.c */

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
#include "glpbbm.h"

/*----------------------------------------------------------------------
-- bbm1_driver - driver for the branch-and-bound method.
--
-- *Synopsis*
--
-- #include "glpbbm.h"
-- int bbm1_driver(LP *mip, LPSOL *sol, struct bbm1_cp *cp);
--
-- *Description*
--
-- The bbm1_driver routine is a driver to the routines which implement
-- components of the branch-and-bound procedure (based on the revised
-- simplex method) for mixed integer linear programming.
--
-- The parameter mip points to the LP problem data block. This block
-- specifies the MIP problem which should be solved. It is not changed
-- on exit. Note that bounds of integer variables MUST BE integer.
--
-- The parameter sol points to the LP problem basis solution block. On
-- entry this block should contain optimal relaxed solution of the MIP
-- problem (i.e. solution where all integer variables are considered as
-- continuous). This initial relaxed solution is used as a root node of
-- branch-and-bound tree. On exit this block contains solution obtained
-- by the bbm1_driver routine.
--
-- The parameter cp points to the block of control parameters which
-- affect on the behavior of the bbm1_driver routine.
--
-- Since many MIP problems may take a long time, this driver routine
-- reports some visual information about current status of the search.
-- The information is sent to stdout approximately once per second and
-- has the following format:
--
--    +nnn: mip = xxx; lp = yyy (aaa; sss)
--
-- where nnn is total number of simplex iteration, xxx is a value of
-- the objective function that corresponds to the best MIP solution
-- (if this solution has not been found yet, xxx is the text "not found
-- yet"), yyy is a value of the objective function that corresponds to
-- the initial relaxed optimal solution (it is not changed during all
-- the optimization process), aaa is number of subroblems in the active
-- list, sss is number of subproblems which have been solved yet.
--
-- *Returns*
--
-- The bbm1_driver routine returns one of the following codes:
--
-- 0 - no errors. This case means that the solver has successfully
--     finished solving the problem;
-- 1 - iterations limit exceeded. In this case the solver reports the
--     best known integer feasible solution;
-- 2 - time limit exceeded. In this case the solver reports the best
--     known integer feasible solution;
-- 3 - numerical unstability or problems with the basis matrix. This
--     case means that the solver is not able to solve the problem. */

static struct bbm1_cp *cp;
/* control parameters block */

static int m;
/* number of rows = number of auxiliary variables */

static int n;
/* number of columns = number of structural variables */

static BBDATA *bb;
/* branch-and-bound main data block */

static gnum_float *c; /* gnum_float c[1+m+n]; */
/* expanded vector of the objective coefficients c = (0, c'), where 0
   is zero subvector for auxiliary variables, c' is the original vector
   of coefficients for structural variables (in case of maximization
   all objective coefficients have opposite signs) */

static gnum_float *dvec; /* gnum_float dvec[1+m]; */
/* the vector delta (used for the dual steepest edge technique) */

static gnum_float *gvec; /* gnum_float gvec[1+n]; */
/* the vector gamma (used for the primal steepest edge technique) */

static int *work; /* int work[1+m+n]; */
/* auxiliary working array */

static gnum_float start;
/* the search starting time */

static gnum_float t_last;
/* most recent time at which visual information was displayed */

/*----------------------------------------------------------------------
-- initialize - initialize branch-and-bound environment.
--
-- This routine creates and initializes all data structures related to
-- branch-and-bound method for mixed integer linear programming (MIP).
-- After initializing the routine creates the root problem and inserts
-- it to the active list (the root problem is considered as active and
-- therefore formally it has been not solved yet).
--
-- The parameter mip specifies LP data block for the MIP problem which
-- should be solved. This block is not changed on exit.
--
-- The parameter sol specifies LP solution block which should contain
-- an optimal basis solution found for the relaxed original MIP problem
-- (i.e. when all variables are considered as continuous). This block
-- is not changed on exit. */

static void initialize(LP *mip, LPSOL *sol)
{     BBNODE *node;
      int i, j, k;
      /* set problem dimension */
      m = mip->m;
      n = mip->n;
      /* check LP data block for correctness */
      check_lp(mip);
      if (mip->kind == NULL)
         fault("bbm1_driver: problem has no integer variables");
      for (j = 1; j <= n; j++)
      {  if (!mip->kind[j]) continue;
         k = m + j; /* x[k] = xS[j] */
         if (mip->lb[k] != floor(mip->lb[k]))
            fault("bbm1_driver: lower bound of some integer structural "
               "variable is not integer");
         if (mip->ub[k] != floor(mip->ub[k]))
            fault("bbm1_driver: upper bound of some integer structural "
               "variable is not integer");
      }
      /* check LP solution block for correctness */
      if (!(sol->m == m && sol->n == n))
         fault("bbm1_driver: solution block has inconsistent dimension")
            ;
      if (sol->status != 'O')
         fault("bbm1_driver: relaxed MIP solution has invalid status");
      /* create branch-and-bound main data block */
      bb = umalloc(sizeof(BBDATA));
      bb->m = m;
      bb->n = n;
      /* create a copy of MIP problem data block (the constraint matrix
         A is not copied to this block) */
      bb->mip = create_lp(m, n, 1);
      for (k = 1; k <= m+n; k++)
      {  bb->mip->type[k] = mip->type[k];
         bb->mip->lb[k] = mip->lb[k];
         bb->mip->ub[k] = mip->ub[k];
      }
      bb->mip->dir = mip->dir;
      for (j = 0; j <= n; j++)
      {  bb->mip->c[j] = mip->c[j];
         bb->mip->kind[j] = mip->kind[j];
      }
      /* save optimal basis solution found for the relaxed original MIP
         problem */
      bb->tagx = ucalloc(1+m+n, sizeof(int));
      for (k = 1; k <= m+n; k++) bb->tagx[k] = sol->tagx[k];
      /* create empty branch-and-bound tree */
      bb->node_pool = create_pool(sizeof(BBNODE));
      bb->tagx_pool = create_pool(sizeof(BBTAGX));
      bb->root = bb->first = bb->last = NULL;
      bb->count = bb->total = 0;
      bb->found = 0;
      bb->best = 0.0;
      /* create common block for the simplex method routines */
      bb->rsm = umalloc(sizeof(RSM));
      bb->rsm->m = m;
      bb->rsm->n = n;
      bb->rsm->type = ucalloc(1+m+n, sizeof(int));
      bb->rsm->lb = ucalloc(1+m+n, sizeof(gnum_float));
      bb->rsm->ub = ucalloc(1+m+n, sizeof(gnum_float));
      bb->rsm->A = create_mat(m, m+n);
      bb->rsm->posx = ucalloc(1+m+n, sizeof(int));
      bb->rsm->indb = ucalloc(1+m, sizeof(int));
      bb->rsm->indn = ucalloc(1+n, sizeof(int));
      bb->rsm->tagn = ucalloc(1+n, sizeof(int));
      bb->rsm->inv = inv_create(m, 100);
      bb->rsm->iter = cp->it_cnt;
      /* so far as the RSM arrays type, lb, ub, posx, indb, indn, and
         tagn will be set later for each individual subproblem, these
         arrays are not initialized here */
      /* build the expanded matrix A = (I | -A'), where I is the unity
         matrix, A' is the original matrix of constraint coefficients */
      for (i = 1; i <= m; i++)
         new_elem(bb->rsm->A, i, i, +1.0);
      for (j = 1; j <= n; j++)
      {  ELEM *e;
         for (e = mip->A->col[j]; e != NULL; e = e->col)
            new_elem(bb->rsm->A, e->i, m+j, -e->val);
      }
      /* allocate the array bbar */
      bb->bbar = ucalloc(1+m, sizeof(gnum_float));
      /* create the expanded vector of coefficients of the objective
         function c = (0, c'), where 0 is zero subvector for auxiliary
         variables, c' is the original vector of coefficients for
         structural variables (in case of maximization all coefficients
         have opposite signs) */
      c = ucalloc(1+m+n, sizeof(gnum_float));
      for (i = 1; i <= m; i++) c[i] = 0.0;
      switch (mip->dir)
      {  case '-':
            for (j = 1; j <= n; j++) c[m+j] = + mip->c[j];
            break;
         case '+':
            for (j = 1; j <= n; j++) c[m+j] = - mip->c[j];
            break;
         default:
            insist(mip->dir != mip->dir);
      }
      /* if the steepest edge technique is required, create the vectors
         delta and gamma */
      if (cp->steep)
      {  dvec = ucalloc(1+m, sizeof(gnum_float));
         for (i = 1; i <= m; i++) dvec[i] = 1.0;
         gvec = ucalloc(1+n, sizeof(gnum_float));
         for (j = 1; j <= n; j++) gvec[j] = 1.0;
      }
      else
         dvec = gvec = NULL;
      /* allocate auxiliary working array */
      work = ucalloc(1+m+n, sizeof(int));
      /* reset timers */
      start = utime();
      t_last = -1.0;
      /* create the root problem */
      node = get_atom(bb->node_pool);
      node->up = NULL;
      node->j = 0;
      node->type = '?';
      node->bound = 0.0;
      node->solved = 0;
      node->diff = NULL;
      node->objval = (mip->dir == '-' ? +sol->objval : -sol->objval);
      node->infsum = 1.0; /* will be computed later */
      node->left = node->right = NULL;
      node->temp = NULL;
      /* make the problem to be root of the branch-and-bound tree */
      bb->root = node;
      /* insert the root problem to the active list */
      bb->first = bb->last = node;
      bb->count++;
      /* return to the calling program */
      return;
}

/*----------------------------------------------------------------------
-- get_value - determine the current value of a variable.
--
-- This routine returns the value of variable x[k] (1 <= k <= m+n) that
-- corresponds to the current basis solution. */

static gnum_float get_value(int k)
{     int i, j;
      gnum_float val;
      insist(1 <= k && k <= m+n);
      if (bb->rsm->posx[k] > 0)
      {  i = +bb->rsm->posx[k]; /* x[k] = xB[i] */
         insist(1 <= i && i <= m);
         val = bb->bbar[i];
      }
      else
      {  j = -bb->rsm->posx[k]; /* x[k] = xN[j] */
         insist(1 <= j && j <= n);
         val = eval_xn(bb->rsm, j);
      }
      return val;
}

/*----------------------------------------------------------------------
-- display - display visual information.
--
-- This routine displays visual information which includes total number
-- of simplex iteration, value of the objective function for the best
-- integer feasible solution, value of the objective function for the
-- root LP problem, number of subproblems in the active list, and number
-- of subproblems which have been solved. */

static void display(void)
{     gnum_float s = (bb->mip->dir == '-' ? +1.0 : -1.0);
      if (!bb->found)
         print("+%6d: mip = %17s; lp = %17.9e (%d; %d)",
            bb->rsm->iter, "not found yet", s * bb->root->objval,
            bb->count, bb->total);
      else
         print("+%6d: mip = %17.9e; lp = %17.9e (%d; %d)",
            bb->rsm->iter, s * bb->best, s * bb->root->objval,
            bb->count, bb->total);
      t_last = utime();
      return;
}

/*----------------------------------------------------------------------
-- eval_objfun - compute current value of the objective function.
--
-- This routine computes and returns current value of the objective
-- function (which includes the constant term) using the current basis
-- solution. In case of maximization the returned value has opposite
-- sign. */

static gnum_float eval_objfun(void)
{     int j;
      gnum_float sum = bb->mip->c[0];
      for (j = 1; j <= n; j++) sum += bb->mip->c[j] * get_value(m+j);
      return bb->mip->dir == '-' ? +sum : -sum;
}

/*----------------------------------------------------------------------
-- dual_monit - dual simplex method monitoring routine.
--
-- This routine is called by the rsm_dual routine (which implements the
-- dual simplex method) each time before the next iteration.
--
-- This routine is intended for three purposes.
--
-- Firstly, it calls the display routine approximately once per second
-- in order to display visual information about optimization progress.
--
-- Secondly, it checks the current value of the objective function.
-- If this value is worse (greater) that the value for the best integer
-- feasible solution, the routine tells the dual simplex to terminate
-- the search, because further iterations will give only worser values
-- of the objective function.
--
-- Thirdly, if number of simplex iterations exceeds allowed maximum, it
-- also tells the dual simplex to terminate the search. */

static int why;
/* this flag explains why the monitoring routine returned non-zero:
   0 - further serach is useless
   1 - iterations limit exhausted
   2 - time limit exhausted */

static int dual_monit(void)
{     int ret = 0;
      gnum_float spent;
      /* determine the spent amount of time */
      spent = utime() - start;
      /* display visual information (once per three seconds) */
      if (t_last < 0.0 || utime() - t_last >= 2.9) display();
      /* if the current value of the objective function is not better
         than for the best integer feasible solution, further search is
         useless and may be terminated */
      if (bb->found)
      {  eval_bbar(bb->rsm, bb->bbar);
         if (check_rr(eval_objfun(), bb->best, cp->tol_obj) >= -1)
         {  why = 0;
            ret = 1;
         }
      }
      /* check if the iterations limit has been exhausted */
      if (cp->it_lim >= 0 && bb->rsm->iter >= cp->it_lim)
      {  why = 1;
         ret = 1;
      }
      /* check if the time limit has been exhausted */
      if (cp->tm_lim >= 0 && spent >= cp->tm_lim)
      {  why = 2;
         ret = 1;
      }
      return ret;
}

/*----------------------------------------------------------------------
-- dual_simplex - solve current problem using dual simplex method.
--
-- This routine searches for optimal solution of the current LP problem
-- using the dual simplex method. The routine assumes that on entry the
-- block bb->rsm specifies the current problem which should be solved,
-- and its initial basis solution which should be dual feasible. On exit
-- this segment specifies the final basis solution.
--
-- The routine returns one of the following codes:
--
-- 0 - optimal solution found;
-- 1 - either problem has no (primal) feasible solution or its optimal
--     solution is not better that the best integer feasible solution
--     which is currently known;
-- 2 - numerical instability (the current basis solution became dual
--     infeasible due to round-off errors);
-- 3 - numerical problems with basis matrix (the current basis matrix
--     became singular or ill-conditioned due to unsuccessful choice of
--     the pivot element on the last simplex iteration);
-- 4 - iterations limit exceeded;
-- 5 - time limit exceeded. */

static int dual_simplex(void)
{     int ret;
      /* call the dual simplex method driver routine */
      ret = rsm_dual(bb->rsm, dual_monit, c, cp->tol_bnd, cp->tol_dj,
         cp->tol_piv, dvec, cp->relax);
      if (ret == 4)
      {  switch (why)
         {  case 0: ret = 1; break;
            case 1: ret = 4; break;
            case 2: ret = 5; break;
            default: insist(why != why);
         }
      }
      /* return to the calling program */
      return ret;
}

/*----------------------------------------------------------------------
-- primal_monit - primal simplex method monitoring routine.
--
-- This routine is called by the rsm_feas routine (which searches for
-- primal feasible solution using the primal simplex method) and by the
-- rsm_primal routine (which searches for optimal solution again using
-- the primal simplex method).
--
-- Since the primal simplex method is used in the case when the dual
-- simplex method fails (because of numerical instability), information
-- displayed by this routine reflects the search of optimal solution of
-- the current LP problem (unlike the dual_monit routine).
--
-- If number of simplex iteration exceeds allowed maximum, this routine
-- tells the primal simplex to terminate the search. */

static int phase;
/* phase number:
   1 - searching for (primal) feasible solution
   2 - searching for optimal solution */

static int primal_monit(void)
{     int ret = 0;
      gnum_float spent;
      /* determine the spent amount of time */
      spent = utime() - start;
      /* display visual information (once per three seconds) */
      if (t_last < 0.0 || utime() - t_last >= 2.9)
      {  int i, j, k, defect;
         gnum_float objval, infsum;
         /* compute current values of basic variables */
         eval_bbar(bb->rsm, bb->bbar);
         /* compute current value of the objective function */
         objval = bb->mip->c[0];
         for (j = 1; j <= n; j++)
            objval += bb->mip->c[j] * get_value(m+j);
         /* compute sum of primal infeasibilities */
         infsum = 0.0;
         for (i = 1; i <= m; i++)
         {  k = bb->rsm->indb[i]; /* x[k] = xB[i] */
            if (bb->rsm->type[k] == 'L' || bb->rsm->type[k] == 'D' ||
                bb->rsm->type[k] == 'S')
            {  /* x[k] is basic variable and has lower bound */
               if (bb->bbar[i] < bb->rsm->lb[k])
                  infsum += (bb->rsm->lb[k] - bb->bbar[i]);
            }
            if (bb->rsm->type[k] == 'U' || bb->rsm->type[k] == 'D' ||
                bb->rsm->type[k] == 'S')
            {  /* x[k] is basic variable and has upper bound */
               if (bb->bbar[i] > bb->rsm->ub[k])
                  infsum += (bb->bbar[i] - bb->rsm->ub[k]);
            }
         }
         /* determine defect of the basis solution (which is a number
            of fixed basic variables) */
         defect = 0;
         for (i = 1; i <= m; i++)
         {  k = bb->rsm->indb[i]; /* x[k] = xB[i] */
            if (bb->rsm->type[k] == 'S') defect++;
         }
         /* display visual information */
         print("%c%6d:   objval = %17.9e   infsum = %17.9e (%d)",
            phase == 1 ? ' ' : '*', bb->rsm->iter, objval, infsum,
            defect);
         t_last = utime();
      }
      /* check if the iterations limit has been exhausted */
      if (cp->it_lim >= 0 && bb->rsm->iter >= cp->it_lim)
      {  why = 1;
         ret = 1;
      }
      /* check if the time limit has been exhausted */
      if (cp->tm_lim >= 0 && spent >= cp->tm_lim)
      {  why = 2;
         ret = 1;
      }
      return ret;
}

/*----------------------------------------------------------------------
-- primal_simplex - solve current problem using primal simplex method.
--
-- This routine seraches for optimal solution of the current LP problem
-- using the two-phase primal simplex method. The routine is able to
-- start from any basis, therefore it can be used in those cases when
-- the dual simplex method routine fails due to numerical instability.
-- The routine assumes that on entry the block bb->rsm specifies the
-- problem which should be solved, and its initial basis solution which
-- may be primal or/and dual infeasible. On exit this segment specifies
-- the final basis solution.
--
-- The routine returns one of the following codes:
--
-- 0 - optimal solution found;
-- 1 - problem has no (primal) feasible solution;
-- 2 - numerical instability (the current basis solution became primal
--     infeasible due to round-off errors);
-- 3 - numerical problems with basis matrix (the current basis matrix
--     became singular or ill-conditioned due to unsuccessful choice of
--     the pivot element on the last simplex iteration);
-- 4 - iterations limit exceeded;
-- 5 - time limit exceeded. */

static int primal_simplex(void)
{     int ret;
      /* search for feasible solution using the primal simplex method
         and the implicit artificial variables technique */
      phase = 1;
      ret = rsm_feas(bb->rsm, primal_monit, cp->tol_bnd, cp->tol_dj,
         cp->tol_piv, gvec, cp->relax);
      if (ret != 0) goto done;
      /* search for optimal solution using the primal simplex method */
      phase = 2;
      ret = rsm_primal(bb->rsm, primal_monit, c, cp->tol_bnd,
         0.70 * cp->tol_dj, cp->tol_piv, gvec, cp->relax);
      insist(ret != 1);
done: /* analyze return code */
      if (ret == 4)
      {  switch (why)
         {  case 1: ret = 4; break;
            case 2: ret = 5; break;
            default: insist(why != why);
         }
      }
      /* return to the calling program */
      return ret;
}

/*----------------------------------------------------------------------
-- new_bound - set new lower/upper bound of branching variable.
--
-- This routine sets new lower (if type = 'L') or upper (if type = 'U')
-- bound of the integer structural variable xS[j] and corrects the type
-- of xS[j] (if necessary). The routine assumes that new bound of xS[j]
-- is *tighter* than its current bound. */

static void new_bound(int j, int type, gnum_float bound)
{     int k;
      RSM *rsm = bb->rsm;
      k = m + j;
      /* x[k] = xS[j] should be integer structural variable */
      insist(1 <= j && j <= n && bb->mip->kind[j]);
      /* new bound should be integer */
      insist(bound == floor(bound));
      /* set new lower or upper bound of the variable x[k] = xS[j] */
      switch (type)
      {  case 'L':
            /* xS[j] >= new lower bound */
            switch (rsm->type[k])
            {  case 'F':
                  rsm->type[k] = 'L';
                  rsm->lb[k] = bound;
                  break;
               case 'L':
                  rsm->type[k] = 'L';
                  insist(rsm->lb[k] < bound);
                  rsm->lb[k] = bound;
                  break;
               case 'U':
                  rsm->type[k] = 'D';
                  rsm->lb[k] = bound;
                  break;
               case 'D':
                  rsm->type[k] = 'D';
                  insist(rsm->lb[k] < bound);
                  rsm->lb[k] = bound;
                  insist(rsm->lb[k] <= rsm->ub[k]);
                  if (rsm->lb[k] == rsm->ub[k]) rsm->type[k] = 'S';
                  break;
               case 'S':
                  /* branching variable can't be of fixed type */
               default:
                  insist(rsm->type[k] != rsm->type[k]);
            }
            break;
         case 'U':
            /* xS[j] <= new upper bound */
            switch (rsm->type[k])
            {  case 'F':
                  rsm->type[k] = 'U';
                  rsm->ub[k] = bound;
                  break;
               case 'L':
                  rsm->type[k] = 'D';
                  rsm->ub[k] = bound;
                  break;
               case 'U':
                  rsm->type[k] = 'U';
                  insist(rsm->ub[k] > bound);
                  rsm->ub[k] = bound;
                  break;
               case 'D':
                  rsm->type[k] = 'D';
                  insist(rsm->ub[k] > bound);
                  rsm->ub[k] = bound;
                  insist(rsm->lb[k] <= rsm->ub[k]);
                  if (rsm->lb[k] == rsm->ub[k]) rsm->type[k] = 'S';
                  break;
               case 'S':
                  /* branching variable can't be of fixed type */
               default:
                  insist(rsm->type[k] != rsm->type[k]);
            }
            break;
         default:
            insist(type != type);
      }
      return;
}

/*----------------------------------------------------------------------
-- revive_node - restore specified problem and its optimal basis.
--
-- This routine restores bounds of variables and optimal basis for the
-- problem specified by the parameter node.
--
-- It is assumed that the problem node belongs to the branch-and-bound
-- tree and this problem has been solved. The special case node = NULL
-- is allowed and means a dummy problem which is a parent for the root
-- problem.
--
-- The routine stores all information related to the specified problem
-- to the RSM block. However it doesn't rebuild a representation of the
-- corresponding basis matrix -- it is assumed that this operation will
-- be performed by other routine which needs the representation. */

static void revive_node(BBNODE *node)
{     int *tagx = bb->rsm->posx, i, j, k;
      RSM *rsm = bb->rsm;
      /* restore the root problem and its optimal basis */
      for (k = 1; k <= m+n; k++)
      {  rsm->type[k] = bb->mip->type[k];
         rsm->lb[k] = bb->mip->lb[k];
         rsm->ub[k] = bb->mip->ub[k];
         tagx[k] = bb->tagx[k];
      }
      /* the special case node = NULL means a dummy problem which is a
         parent for the root problem */
      if (node == NULL) goto skip;
      /* the specified problem should be solved (because for an active
         problem optimal basis is not known yet) */
      insist(node->solved);
      /* go upstairs from the specified node to the root in order to
         build a path from the root to the specified node */
      node->temp = NULL;
      for (node = node; node != NULL; node = node->up)
      {  if (node->up == NULL)
            insist(node == bb->root);
         else
            node->up->temp = node;
      }
      /* go downstairs from the root to the specified node in order to
         restore bounds of variable and optimal basis for the specified
         problem using information containing in each visited node (the
         root node is ignored) */
      for (node = bb->root->temp; node != NULL; node = node->temp)
      {  BBTAGX *t;
         /* set new lower/upper bound of a branching variable for the
            problem that corresponds to the currently visited node */
         new_bound(node->j, node->type, node->bound);
         /* remember optimal basis for the problem that corresponds to
            the currently visited node */
         for (t = node->diff; t != NULL; t = t->next)
         {  insist(1 <= t->k && t->k <= m+n);
            tagx[t->k] = t->tagx;
         }
      }
skip: /* restore optimal basis using information accumulated in the
         array tagx */
      i = j = 0;
      for (k = 1; k <= m+n; k++)
      {  if (tagx[k] == 'B')
         {  /* x[k] is basic variable */
            i++;
            rsm->indb[i] = k;
         }
         else
         {  /* x[k] is non-basic variable */
            j++;
            rsm->indn[j] = k;
            rsm->tagn[j] = tagx[k];
         }
      }
      insist(i == m && j == n);
      for (i = 1; i <= m; i++) rsm->posx[rsm->indb[i]] = +i;
      for (j = 1; j <= n; j++) rsm->posx[rsm->indn[j]] = -j;
      /* check the RSM block for correctness */
      check_rsm(rsm);
      /* note that now a representation of the current basis matrix is
         not valid yet */
      return;
}

/*----------------------------------------------------------------------
-- round_off - round off values of integer structural variables.
--
-- This routine rounds off current values of basic integer structural
-- variables which are close to their (integer) bounds or to the nearest
-- integers. It is assumed that the current basis is primal feasible.
--
-- This operation is extremely important for correct branching! */

static void round_off(void)
{     int i, j, k;
      gnum_float tol = cp->tol_int, ival;
      /* look up the list of basic variables (since only basic integer
         structural variables may be integer infeasible) */
      for (i = 1; i <= m; i++)
      {  k = bb->rsm->indb[i]; /* x[k] = xB[i] */
         if (k <= m) continue; /* skip auxiliary variable */
         j = k - m; /* x[k] = xS[j] */
         insist(1 <= j && j <= n);
         if (!bb->mip->kind[j]) continue; /* skip continuous one */
         /* if xS[j] violates its bound (slightly, because the basis is
            assumed to be primal feasible), it should be set exactly on
            its bound; this is extremely important! */
         if (bb->rsm->type[k] == 'L' || bb->rsm->type[k] == 'D' ||
             bb->rsm->type[k] == 'S')
         {  /* xS[j] has lower bound */
            if (bb->bbar[i] < bb->rsm->lb[k])
               bb->bbar[i] = bb->rsm->lb[k];
         }
         if (bb->rsm->type[k] == 'U' || bb->rsm->type[k] == 'D' ||
             bb->rsm->type[k] == 'S')
         {  /* xS[j] has upper bound */
            if (bb->bbar[i] > bb->rsm->ub[k])
               bb->bbar[i] = bb->rsm->ub[k];
         }
         /* if xS[j] is close to the nearest integer, it may be assumed
            that actually it is exact integer, where a small difference
            appeared due to round-off errors */
         ival = floor(bb->bbar[i] + 0.5); /* the nearest integer */
         if (ival - tol <= bb->bbar[i] && bb->bbar[i] <= ival + tol)
            bb->bbar[i] = ival;
      }
      return;
}

/*----------------------------------------------------------------------
-- eval_infsum - compute current sum of integer infeasibilities.
--
-- This routine computes and returns sum of integer infeasibilities for
-- the current basis solution. (Zero means that the solution is integer
-- feasible.) */

static gnum_float eval_infsum(void)
{     int j;
      gnum_float sum = 0.0;
      for (j = 1; j <= n; j++)
      {  if (bb->mip->kind[j])
         {  gnum_float val, t1, t2;
            val = get_value(m+j);
            t1 = val - floor(val);
            t2 = ceil(val) - val;
            insist(t1 >= 0.0 && t2 >= 0.0);
            sum += (t1 < t2 ? t1 : t2);
         }
      }
      return sum;
}

/*----------------------------------------------------------------------
-- exempt_node - remove problem node from the active list.
--
-- This routine removes the problem node from the active list. Further
-- this problem may be either divided in two childs or removed from the
-- branch-and-bound tree. */

static void exempt_node(BBNODE *node)
{     insist(!node->solved);
      node->solved = 1;
      if (node->left == NULL)
         bb->first = node->right;
      else
         node->left->right = node->right;
      if (node->right == NULL)
         bb->last = node->left;
      else
         node->right->left = node->left;
      node->left = node->right = NULL;
      bb->count--, bb->total++;
      return;
}

/*----------------------------------------------------------------------
-- solve_problem - solve the given active problem.
--
-- This routine solves an LP problem which is specified by the active
-- node of the branch-and-bound tree.
--
-- At first the routine restores the parent problem and its (optimal)
-- basis solution. Since a problem differs from its parent problem only
-- in a tighter bound of one variable, the optimal basis of the parent
-- problem is dual feasible for its child, therefore the dual simplex
-- method may be used. However, if the flag warm is set, it means that
-- the parent problem has been just now solved, therefore no restoring
-- is needed.
--
-- Then the routine sets new bound of a bracnhing variable chosen for
-- the specified problem and invokes the dual simplex method. If due to
-- round-off errors the basis solution becomes dual infeasible and the
-- dual simplex method fails, the routine invokes the two-phase primal
-- simplex method which is able to start from any valid basis.
--
-- If the final basis solution is optimal, the routine computes values
-- of basic variables, round off them (that is extremely important for
-- correct branching), and stores value of the objective function and
-- sum of integer infeasibilities to the problem node.
--
-- Finally the routine marks the problem as solved and removes it from
-- the active list.
--
-- The routine returns one of the following codes:
--
-- 0 - optimal solution found;
-- 1 - either problem has no (primal) feasible solution or its optimal
--     solution is not better than the best integer feasible solution
--     which is currently known;
-- 2 - (never returned);
-- 3 - numerical problems with basis matrix (the current basis matrix
--     became singular or ill-conditioned due to unsuccessful choice of
--     the pivot element on the last simplex iteration);
-- 4 - iterations limit exceeded;
-- 5 - time limit exceeded. */

static int solve_problem(BBNODE *node, int warm)
{     int ret, i, j;
      /* node points to the problem which should be solved */
      insist(node != NULL && !node->solved);
      /* restore bounds and (optimal) basis of the parent problem */
      if (!warm)
      {  revive_node(node->up);
         /* reset the vector delta */
         if (dvec != NULL) for (i = 1; i <= m; i++) dvec[i] = 1.0;
         /* so far as the parent problem was already solved, its basis
            can't be singular or ill-conditioned */
         insist(invert_b(bb->rsm) == 0);
      }
      /* now the block bb->rsm corresponds to the parent problem and
         its optimal solution; set new (tighter) bound of a branching
         variable chosen for the given problem (if the given problem is
         the root problem, this step should be skipped) */
      if (node->up != NULL)
      {  /* the branching variable should be only basic for the parent
            problem (otherwise it couldn't be integer infeasible) */
         insist(bb->rsm->posx[m+node->j] > 0);
         /* set new lower or upper bound for the branching variable;
            this makes the current basis primal infeasible */
         new_bound(node->j, node->type, node->bound);
      }
      /* now the block bb->rsm corresponds to the given problem and the
         current basis is dual feasible; invoke the dual simplex method
         to find a primal feasible (i.e. optimal) basis */
      ret = dual_simplex();
      /* it may happen that due to excessive round-off error the basis
         becomes dual infeasible and the dual simplex method fails; in
         this case the two-phase primal simplex method which is able to
         start from any valid basis can be used in order to solve the
         problem */
      if (ret == 2)
      {  print("Numerical instability; recovering...");
         t_last = utime();
         /* although the current (dual infeasible) basis may be used as
            initial basis, the practice shows that it's better to start
            again from the optimal basis of the parent problem */
         revive_node(node->up);
         /* reset the vector gamma */
         if (gvec != NULL) for (j = 1; j <= n; j++) gvec[j] = 1.0;
         /* so far as the parent problem was already solved, its basis
            can't be singular or ill-conditioned */
         insist(invert_b(bb->rsm) == 0);
         /* set new (tighter) bound of a branching variable chosen for
            the given problem (if the given problem is the root problem,
            this step should be skipped) */
         if (node->up != NULL)
         {  insist(bb->rsm->posx[m+node->j] > 0);
            new_bound(node->j, node->type, node->bound);
         }
         /* try the two-phase primal simplex method */
         ret = primal_simplex();
      }
      /* it also may happen that due to excessive round-off errors the
         basis becomes primal infeasible and the primal simplex method
         fails; however in this case the primal simplex can be invoked
         once again */
      while (ret == 2)
      {  print("Numerical instability (again); recovering...");
         t_last = utime();
         /* reinvert the current basis matrix (this usually helps to
            improve accuracy of the representation) */
         if (invert_b(bb->rsm))
amba:    {  print("Numerical problems with basis matrix");
            print("Sorry, basis recovery procedure not implemented");
            ret = 3;
            goto done;
         }
         /* try the two-phase primal simplex method once again */
         ret = primal_simplex();
      }
      /* analyze return code */
      switch (ret)
      {  case 0:
            /* optimal solution found */
            /* compute current values of basic variables */
            eval_bbar(bb->rsm, bb->bbar);
            /* round off values of those (basic) integer structural
               variables which are close to the corresponding integer
               values or to their (integer) bounds; this operation is
               extremely important for correct branching */
            round_off();
            /* store value of the objective function and sum of integer
               infeasibilities to the problem node */
            node->objval = eval_objfun();
            node->infsum = eval_infsum();
            break;
         case 1:
            /* problem has no (primal) feasible solution */
            break;
         case 2:
            /* (it should never be) */
            insist(ret != ret);
         case 3:
            /* the basis matrix became singular or ill-conditioned due
               to unsuccessful choice of the pivot element on the last
               dual simplex iteration */
            goto amba;
         case 4:
            /* iterations limit exceeded; search terminated */
            display();
            print("ITERATIONS LIMIT EXCEEDED; SEARCH TERMINATED");
            break;
         case 5:
            /* time limit exceeded; search terminated */
            display();
            print("TIME LIMIT EXCEEDED; SEARCH TERMINATED");
            break;
         default:
            insist(ret != ret);
      }
done: /* remove the current subproblem from the active list, because it
         has been solved (or looks so) */
      exempt_node(node);
      /* return to the calling program */
      return ret;
}

/*----------------------------------------------------------------------
-- store_sol - store current basis solution.
--
-- This routine stores current basis solution to the solution block (it
-- is assumed that values of basic variables have been computed). */

static void store_sol(LPSOL *sol, int status)
{     LP *mip = bb->mip;
      RSM *rsm = bb->rsm;
      int i, j, k;
      gnum_float *bbar = bb->bbar, *pi, *cbar;
      pi = ucalloc(1+m, sizeof(gnum_float));
      cbar = ucalloc(1+n, sizeof(gnum_float));
      /* compute simplex multipliers */
      eval_pi(rsm, c, pi);
      /* compute reduced costs of non-basic variables */
      eval_cbar(rsm, c, pi, cbar);
      /* correct signs of reduced costs in case of maximization */
      if (mip->dir == '+')
         for (j = 1; j <= n; j++) cbar[j] = - cbar[j];
      /* store values and reduced costs of variables */
      for (k = 1; k <= m+n; k++)
      {  if (rsm->posx[k] > 0)
         {  i = +rsm->posx[k]; /* x[k] = xB[i] */
            sol->tagx[k] = 'B';
            if (cp->round && fabs(bbar[i]) < cp->tol_bnd) bbar[i] = 0.0;
            sol->valx[k] = bbar[i];
            sol->dx[k] = 0.0;
         }
         else
         {  j = -rsm->posx[k]; /* x[k] = xN[j] */
            sol->tagx[k] = rsm->tagn[j];
            sol->valx[k] = eval_xn(rsm, j);
            if (cp->round && fabs(cbar[j]) < cp->tol_dj) cbar[j] = 0.0;
            sol->dx[k] = cbar[j];
         }
      }
      /* set MIP solution flag */
      sol->mipsol = 1;
      /* set solution status */
      sol->status = status;
      /* compute value of the objective function */
      sol->objval = mip->c[0];
      for (j = 1; j <= n; j++)
         sol->objval += mip->c[j] * sol->valx[m+j];
      ufree(pi);
      ufree(cbar);
      return;
}

/*----------------------------------------------------------------------
-- build_tagx - remember optimal basis of the given problem.
--
-- This routine determines the optimal basis of the given problem and
-- stores it to the array tagx. It is assumed that the problem has been
-- solved and its node is still in the branch-and-bound tree. */

static void build_tagx(BBNODE *node, int tagx[])
{     int k;
      /* the specified problem should be solved (because for an active
         problem optimal basis is not known yet) */
      insist(node != NULL && node->solved);
      /* get the optimal basis of the root problem */
      for (k = 1; k <= m+n; k++) tagx[k] = bb->tagx[k];
      /* go upstairs from the specified node to the root in order to
         build a path from the root to the specified node */
      node->temp = NULL;
      for (node = node; node != NULL; node = node->up)
      {  if (node->up == NULL)
            insist(node == bb->root);
         else
            node->up->temp = node;
      }
      /* go downstairs from the root to the specified node in order to
         restore bounds of variable and optimal basis for the specified
         problem using information containing in each visited node (the
         root node is ignored) */
      for (node = bb->root->temp; node != NULL; node = node->temp)
      {  /* remember optimal basis for the problem that corresponds to
            the currently visited node */
         BBTAGX *t;
         for (t = node->diff; t != NULL; t = t->next)
         {  insist(1 <= t->k && t->k <= m+n);
            tagx[t->k] = t->tagx;
         }
      }
      return;
}

/*----------------------------------------------------------------------
-- save_diff - save differences in basis for the current problem.
--
-- This routine compares the optimal basis of the current problem with
-- the optimal basis of its parent and stores all differences to the
-- problem node. It is assumed that the current problem has been solved
-- just now, and therefore its optimal basis is still in the RSM block.
--
-- This feature is optional and allows to solve a problem starting from
-- optimal basis of its parent (which is more appropriate than optimal
-- basis of the root node). */

static void save_diff(BBNODE *node)
{     int *tagx = work, i, j, k, temp;
      insist(node->diff == NULL);
      /* get the optimal basis of the parent problem */
      build_tagx(node->up, tagx);
      /* compare it with the optimal basis of the current problem and
         store all differences to the problem node */
      for (k = 1; k <= m+n; k++)
      {  if (bb->rsm->posx[k] > 0)
         {  i = +bb->rsm->posx[k]; /* x[k] = xB[i] */
            insist(1 <= i && i <= m);
            temp = 'B';
         }
         else
         {  j = -bb->rsm->posx[k]; /* x[k] = xN[j] */
            insist(1 <= j && j <= n);
            temp = bb->rsm->tagn[j];
         }
         if (temp != tagx[k])
         {  /* the variable x[k] has different status; add a new entry
               to the list of differences */
            BBTAGX *t;
            t = get_atom(bb->tagx_pool);
            t->k = k;
            t->tagx = temp;
            t->next = node->diff;
            node->diff = t;
         }
      }
      return;
}

/*----------------------------------------------------------------------
-- choose_branch - choose a branching variable.
--
-- On entry to this routine the RSM block corresponds to the problem
-- which has been solved just now. An optimal solution of this problem
-- is better than the best known integer feasible solution, however it
-- is integer infeasible.
--
-- This routine chooses a branching variable xS[j] (1 <= j <= n) and
-- sets the parameter what which tells to the solver what of two child
-- subproblems derived from the current problem should be solved first:
--
-- 'L' - that one where xS[j] >= new lower bound = ceil(beta)  > beta
-- 'U' - that one where xS[j] <= new upper bound = floor(beta) < beta
--
-- where beta is the (fractional) value of the variable xS[j] in basis
-- solution of the current problem.
--
-- Structure of the branch-and-bound tree completely depends on the
-- choice of a branching variable, therefore this operation is crucial
-- for the efficiency of the branch-and-bound method. */

static int choose_branch(int *what)
{     int i, j, k;
      *what = '?';
      /* choose a branching variable using required heuristic */
      switch (cp->branch)
      {  case BB_FIRST:
            /* branch on the first appropriate variable */
            j = branch_first(bb, what);
            break;
         case BB_LAST:
            /* branch on the last appropriate variable */
            j = branch_last(bb, what);
            break;
         case BB_DRTOM:
            /* branch using heuristic by Driebeck and Tomlin */
            j = branch_drtom(bb, what);
            break;
         default:
            insist(cp->branch != cp->branch);
      }
      /* xS[j] should have correct number */
      insist(1 <= j && j <= n);
      /* xS[j] should be of integer type */
      insist(bb->mip->kind[j]);
      /* xS[j] should be basic variable */
      k = m + j; /* x[k] = xS[j] */
      i = bb->rsm->posx[k]; /* xB[i] = xS[j] */
      insist(1 <= i && i <= m);
      /* xS[j] should have fractional (integer infeasible) value in the
         optimal basis solution of the current problem */
      insist(bb->bbar[i] != floor(bb->bbar[i]));
      /* check subproblem flag */
      insist(*what == 'L' || *what == 'U');
      /* return to the calling routine */
      return j;
}

/*----------------------------------------------------------------------
-- split_node - divide problem in two child subproblems.
--
-- This routine divides the current problem in two child subproblems
-- using the chosen branching variable xS[j], 1 <= j <= n. In the first
-- child subproblem (which is fixed to the left of the current problem
-- node) the variable xS[j] has new lower bound ceil(beta), where beta
-- is the value of xS[j] in the optimal basis solution of its parent,
-- and in the second child subproblem (which is fixed to the right of
-- of the current problem node) the variable xS[j] has new upper bound
-- floor(beta). After creating subproblems the routine adds them to the
-- end of the active list.
--
-- It is assumed that the current problem (specified by the parameter
-- node) has been solved just now and therefore the RSM block contains
-- its optimal basis solution. */

static void split_node(BBNODE *node, int j)
{     BBNODE *child;
      gnum_float beta;
      /* the current problem should be solved */
      insist(node->solved);
      /* determine value of the branching variable xS[j] in the optimal
         basis solution of the current problem */
      beta = get_value(m+j);
      /* create the first child subproblem */
      child = node->left = get_atom(bb->node_pool);
      child->up = node;
      child->j = j;
      child->type = 'L';
      child->bound = ceil(beta);
      child->solved = 0;
      child->diff = NULL;
      child->objval = child->infsum = 0.0;
      child->left = bb->last;
      child->right = NULL;
      child->temp = NULL;
      /* add the first subproblem to the active list */
      if (bb->first == NULL)
         bb->first = child;
      else
         bb->last->right = child;
      bb->last = child;
      bb->count++;
      /* create the second child subproblem */
      child = node->right = get_atom(bb->node_pool);
      child->up = node;
      child->j = j;
      child->type = 'U';
      child->bound = floor(beta);
      child->solved = 0;
      child->diff = NULL;
      child->objval = child->infsum = 0.0;
      child->left = bb->last;
      child->right = NULL;
      child->temp = NULL;
      /* add the second subproblem to the active list */
      if (bb->first == NULL)
         bb->first = child;
      else
         bb->last->right = child;
      bb->last = child;
      bb->count++;
      return;
}

/*----------------------------------------------------------------------
-- remove_node - remove problem node from the branch-and-bound tree.
--
-- This routine removes the specified problem from the branch-and-bound
-- tree. If after removing the parent problem has no child subproblems,
-- the routine also deletes it recursively from the tree. */

static void remove_node(BBNODE *node)
{     BBNODE *parent;
      insist(node != NULL);
loop: /* determine the parent */
      parent = node->up;
      /* detach the current node from its parent */
      if (parent != NULL)
      {  if (parent->left == node)
            parent->left = NULL;
         else if (parent->right == node)
            parent->right = NULL;
         else
            insist(parent != parent);
      }
      /* free the list of differences */
      while (node->diff != NULL)
      {  BBTAGX *t = node->diff->next;
         free_atom(bb->tagx_pool, node->diff);
         node->diff = t;
      }
      /* delete the current node */
      free_atom(bb->node_pool, node);
      /* if the parent exists and now has no childs, it also should be
         removed from the tree */
      if (parent != NULL)
      {  if (parent->left == NULL && parent->right == NULL)
         {  node = parent;
            goto loop;
         }
      }
      return;
}

/*----------------------------------------------------------------------
-- clean_tree - clean the branch-and-bound tree.
--
-- This routine looks up the active list and removes active problems
-- whose optimal solution can't be better than the best known integer
-- feasible solution.
--
-- It has sense to call this routine every time when the solver finds
-- a new best integer feasible solution. */

static void clean_tree(void)
{     BBNODE *node, *next;
      /* there should be a best known integer feasible solution */
      insist(bb->found);
      /* look up the active list */
      node = bb->first;
      while (node != NULL)
      {  /* save a pointer to the next active node (although deleting
            active node may involve deleting other nodes, this pointer
            remains correct, because other nodes are parents and don't
            belong to the active list) */
         next = node->right;
         /* if an optimal solution of the parent problem is not better
            than the best known integer feasible solution, its child
            problem also can't have better solution and therefore may
            be removed from the active list and from the tree */
         if (node->up != NULL &&
             check_rr(node->up->objval, bb->best, cp->tol_obj) >= -1)
         {  /* remove the problem from the active list */
            exempt_node(node);
            /* and remove it from the branch-and-bound tree */
            remove_node(node);
         }
         /* consider the next active problem */
         node = next;
      }
      return;
}

/*----------------------------------------------------------------------
-- select_node - select a problem from the active list.
--
-- This routine selects an appropriate active problem which should be
-- solved next and returns a pointer to the problem node */

static BBNODE *select_node(void)
{     BBNODE *node;
      switch (cp->btrack)
      {  case BB_FIFO:
            /* select a problem using FIFO heuristic */
            node = btrack_fifo(bb);
            break;
         case BB_LIFO:
            /* select a problem using LIFO heuristic */
            node = btrack_lifo(bb);
            break;
         case BB_BESTP:
            /* select a problem using the best projection heuristic */
            node = btrack_bestp(bb);
            break;
         default:
            insist(cp->btrack != cp->btrack);
      }
      insist(node != NULL);
      return node;
}

/*----------------------------------------------------------------------
-- terminate - terminate branch-and-bound environment.
--
-- This routine frees all memory allocated to the branch-and-bound data
-- structures. */

static void terminate(void)
{     delete_lp(bb->mip);
      ufree(bb->tagx);
      delete_pool(bb->node_pool);
      delete_pool(bb->tagx_pool);
      ufree(bb->rsm->type);
      ufree(bb->rsm->lb);
      ufree(bb->rsm->ub);
      delete_mat(bb->rsm->A);
      ufree(bb->rsm->posx);
      ufree(bb->rsm->indb);
      ufree(bb->rsm->indn);
      ufree(bb->rsm->tagn);
      inv_delete(bb->rsm->inv);
      ufree(bb->rsm);
      ufree(bb->bbar);
      ufree(bb);
      ufree(c);
      if (dvec != NULL) ufree(dvec);
      if (gvec != NULL) ufree(gvec);
      ufree(work);
      return;
}

/*----------------------------------------------------------------------
-- bbm1_driver - driver routine.
--
-- This routine is a driver for the branch-and-bound procedure which is
-- intended for solving mixed integer linear programming problems. */

int bbm1_driver(LP *mip, LPSOL *sol, struct bbm1_cp *_cp)
{     BBNODE *node;
      int warm = 0, j, what, ret;
      /* externalize control parameters block */
      cp = _cp;
      /* initialize branch and bound environment */
      initialize(mip, sol);
      /* choose the root problem (now it is the only active problem) */
      node = bb->root;
      insist(bb->first == node && bb->last == node);
loop: /* main loop starts here */
      /* solve the current problem and remove it from the active list */
      ret = solve_problem(node, warm);
      /* analyze return code reported by the simplex method routine */
      switch (ret)
      {  case 0:
            /* optimal solution found */
            break;
         case 1:
            /* either problem has no (primal) feasible solution or it
               has an optimal solution which is not better than the best
               known integer feasible solution; therefore this problem
               has been fathomed */
            goto cut;
         case 2:
            /* (this should never be) */
            insist(ret != ret);
         case 3:
            /* the basis matrix became singular or ill-conditioned due
               to unsuccessful choice of the pivot element on the last
               dual simplex iteration */
            ret = 3;
            goto done;
         case 4:
            /* iterations limit exceeded; search terminated */
            ret = 1;
            goto done;
         case 5:
            /* time limit exceeded; search terminated */
            ret = 2;
            goto done;
         default:
            insist(ret != ret);
      }
      /* store an initial solution which is the optimal solution of the
         root problem */
      if (node->up == NULL)
      {  insist(bb->root == node);
         store_sol(sol, node->infsum == 0.0 ? 'O' : 'I');
      }
      /* if only the initial solution is required, print an appropriate
         message and return */
      if (node->up == NULL && cp->what == 0)
      {  switch (sol->status)
         {  case 'O':
               bb->found = 1;
               bb->best = node->objval;
               display();
               print("Initial solution is INTEGER OPTIMAL");
               break;
            case 'I':
               display();
               print("Initial solution is INTEGER INFEASIBLE");
               break;
            default:
               insist(sol->status != sol->status);
         }
         ret = 0;
         goto done;
      }
      /* if value of the objective function in optimal solution of the
         current problem is not better than for the best known integer
         feasible solution, this problem may be considered as fathomed
         (although the dual simplex performs the same check on every
         iteration, an optimal solution can be found also by the primal
         simplex which performs no check) */
      if (bb->found)
      {  if (check_rr(node->objval, bb->best, cp->tol_obj) >= -1)
            goto cut;
      }
      /* if optimal solution of the current problem being better than
         the best known integer feasible solution is integer feasible,
         we have found a new best integer feasible solution */
      if (node->infsum == 0.0)
      {  /* new best integer feasible solution has been found */
         bb->found = 1;
         bb->best = node->objval;
         display();
         /* store this new best solution */
         store_sol(sol, 'F');
         /* if only some integer feasible solution is required, print
            an appropriate message and return */
         if (cp->what == 1)
         {  print("INTEGER FEASIBLE SOLUTION FOUND");
            ret = 0;
            goto done;
         }
         /* the current problem has been fathomed */
         goto cut;
      }
      /* thus, optimal solution of the current problem is better than
         the best known integer feasible solution, however it is integer
         infeasible */
      /* save differences between optimal bases of the current problem
         and its parent (this is not needed for the root problem) */
      if (node->up != NULL) save_diff(node);
      /* choose a branching variable xS[j] (1 <= j <= n) among basic
         integer structural variables which have fractional values */
      j = choose_branch(&what);
      /* divide the current problem into two child subproblems using
         the branching variable xS[j] and add them to the end of the
         active list (the current problem is still kept in the tree) */
      split_node(node, j);
      /* choose one of two child subproblems of the current problem and
         make it current active problem */
      switch (what)
      {  case 'L':
            /* that one where xS[j] >= new lower bound */
            node = node->left;
            insist(node->type == 'L');
            break;
         case 'U':
            /* that one where xS[j] <= new upper bound */
            node = node->right;
            insist(node->type == 'U');
            break;
         default: insist(what != what);
      }
      /* the new current problem is a child of the old one, therefore
         the solver may not restore the parent problem and its basis */
      warm = 1;
      /* continue the search */
      goto loop;
cut:  /* the current problem has been fathomed and should be removed
         from the branch-and-bound tree */
      remove_node(node);
      /* in the active list there may be other problems whose optimal
         solutions are wittingly not better than the best known integer
         feasible solution, therefore such problems also may be removed
         from the active list and from the tree */
      if (bb->found) clean_tree();
      /* if the active list is empty, the best known integer feasible
         solution is optimal solution of the MIP problem */
      if (bb->first == NULL)
      {  insist(bb->last == NULL);
         display();
         if (!bb->found)
         {  print("PROBLEM HAS NO INTEGER FEASIBLE SOLUTION");
            sol->status = 'N';
         }
         else
         {  print("INTEGER OPTIMAL SOLUTION FOUND");
            sol->status = 'O';
         }
         ret = 0;
         goto done;
      }
      /* select an appropriate problem which should be solved next from
         the active list (so called "backtracking") */
      node = select_node();
      /* new current problem is *not* a child of the old one, therefore
         the solver should restore the parent problem and its basis */
      warm = 0;
      /* continue the search */
      goto loop;
done: /* reflect the spent resources in the parameter block */
      if (cp->it_lim >= 0)
      {  cp->it_lim -= (bb->rsm->iter - cp->it_cnt);
         if (cp->it_lim < 0) cp->it_lim = 0;
      }
      cp->it_cnt = bb->rsm->iter;
      if (cp->tm_lim >= 0.0)
      {  cp->tm_lim -= (utime() - start);
         if (cp->tm_lim < 0.0) cp->tm_lim = 0.0;
      }
      /* terminate branch-and-bound environment */
      terminate();
      /* return to the calling program */
      return ret;
}

/*----------------------------------------------------------------------
-- branch_first - choose the first appropriate branching variable.
--
-- *Synopsis*
--
-- #include "glpbbm.h"
-- int branch_first(BBDATA *bb, int *what);
--
-- *Description*
--
-- The branch_first routine chooses a branching variable which is an
-- integer structural variable xS[j] (1 <= j <= n).
--
-- The routine also sets the parameter what which tells the solver what
-- of two subproblems should be solved after the current problem:
--
-- 'L' - that one where xS[j] >= new lower bound = ceil(beta)  > beta
-- 'U' - that one where xS[j] <= new upper bound = floor(beta) < beta
--
-- where beta is the (fractional) value of the variable xS[j] in basis
-- solution of the current problem.
--
-- *Returns*
--
-- The branch_first routine returns a number of the variable xS[j].
--
-- *Heuristic*
--
-- The branch_first routine looks up the list of structural variables
-- and chooses the first one which is of integer kind and (being basic)
-- has fractional (non-integer) value in the current basis solution. */

int branch_first(BBDATA *bb, int *what)
{     int m = bb->m, n = bb->n, i, j, k, this = 0;
      gnum_float beta;
      for (j = 1; j <= n; j++)
      {  /* skip continuous variable */
         if (!bb->mip->kind[j]) continue;
         /* x[k] = xS[j] */
         k = m + j;
         /* skip non-basic variable */
         if (bb->rsm->posx[k] < 0) continue;
         /* xB[i] = x[k] */
         i = +bb->rsm->posx[k];
         insist(1 <= i && i <= m);
         /* beta = value of xS[j] in the current basis solution */
         beta = bb->bbar[i];
         /* skip basic variable which has an integer value */
         if (beta == floor(beta)) continue;
         /* choose xS[j] */
         this = j;
         break;
      }
      insist(1 <= this && this <= n);
      /* tell the solver to get next that problem where xS[j] violates
         its new bound less than in the other */
      if (ceil(beta) - beta < beta - floor(beta))
         *what = 'L';
      else
         *what = 'U';
      return this;
}

/*----------------------------------------------------------------------
-- branch_last - choose the last appropriate branching variable.
--
-- *Synopsis*
--
-- #include "glpbbm.h"
-- int branch_last(BBDATA *bb, int *what);
--
-- *Description*
--
-- The branch_last routine chooses a branching variable which is an
-- integer structural variable xS[j] (1 <= j <= n).
--
-- The routine also sets the parameter what which tells the solver what
-- of two subproblems should be solved after the current problem:
--
-- 'L' - that one where xS[j] >= new lower bound = ceil(beta)  > beta
-- 'U' - that one where xS[j] <= new upper bound = floor(beta) < beta
--
-- where beta is the (fractional) value of the variable xS[j] in basis
-- solution of the current problem.
--
-- *Returns*
--
-- The branch_last routine returns a number of the variable xS[j].
--
-- *Heuristic*
--
-- The branch_last routine looks up the list of structural variables in
-- reverse order and the chooses the last variable (if to see from the
-- beginning of the list) which is of integer kind and (being basic) has
-- fractional (non-integer) value in the current basis solution. */

int branch_last(BBDATA *bb, int *what)
{     int m = bb->m, n = bb->n, i, j, k, this = 0;
      gnum_float beta;
      for (j = n; j >= 1; j--)
      {  /* skip continuous variable */
         if (!bb->mip->kind[j]) continue;
         /* x[k] = xS[j] */
         k = m + j;
         /* skip non-basic variable */
         if (bb->rsm->posx[k] < 0) continue;
         /* xB[i] = x[k] */
         i = +bb->rsm->posx[k];
         insist(1 <= i && i <= m);
         /* beta = value of xS[j] in the current basis solution */
         beta = bb->bbar[i];
         /* skip basic variable which has an integer value */
         if (beta == floor(beta)) continue;
         /* choose xS[j] */
         this = j;
         break;
      }
      insist(1 <= this && this <= n);
      /* tell the solver to get next that problem where xS[j] violates
         its new bound less than in the other */
      if (ceil(beta) - beta < beta - floor(beta))
         *what = 'L';
      else
         *what = 'U';
      return this;
}

/*----------------------------------------------------------------------
-- branch_drtom - choose a branch using Driebeek and Tomlin heuristic.
--
-- *Synopsis*
--
-- #include "glpbbm.h"
-- int branch_drtom(BBDATA *bb, int *what);
--
-- *Description*
--
-- The branch_drtom routine chooses a branching variable which is an
-- integer structural variable xS[j] (1 <= j <= n).
--
-- The routine also sets the parameter what which tells the solver what
-- of two subproblems should be solved after the current problem:
--
-- 'L' - that one where xS[j] >= new lower bound = ceil(beta)  > beta
-- 'U' - that one where xS[j] <= new upper bound = floor(beta) < beta
--
-- where beta is the (fractional) value of the variable xS[j] in basis
-- solution of the current problem.
--
-- *Returns*
--
-- The branch_drtom routine returns a number of the variable xS[j].
--
-- *Heuristic*
--
-- The branch_drtom routine is based on easy heuristic proposed in:
--
-- Driebeek N.J. An algorithm for the solution of mixed-integer
-- programming problems, Management Science, 12: 576-87 (1966)
--
-- and improved in:
--
-- Tomlin J.A. Branch and bound methods for integer and non-convex
-- programming, in J.Abadie (ed.), Integer and Nonlinear Programming,
-- North-Holland, Amsterdam, pp. 437-50 (1970).
--
-- Note that the implementation of this heuristic is time-expensive,
-- because computing one-step degradation (see the routines below) for
-- each basic integer structural variable requires one BTRAN. */

/*----------------------------------------------------------------------
-- degrad - compute degradation of the objective function.
--
-- If tagp = 'L', this routine computes degradation (worsening) of the
-- objective function when the basic variable xB[p] increasing goes to
-- its new lower bound ceil(beta), where beta is value of this variable
-- in the current basis solution.
--
-- If tagp = 'U', this routine computes degradation (worsening) of the
-- objective function when the basic variable xB[p] decreasing goes to
-- its new upper bound floor(beta).
--
-- The array ap is the p-th row of the current simplex table. The array
-- cbar is the vector of reduced costs of non-basic variables.
--
-- In order to compute degradation the routine implicitly performs one
-- iteration of the dual simplex method (note that the current basis is
-- noy changed).
--
-- Note that actual degradation of the objective function (which would
-- happen if the corresponding problem with the new bound of xB[p] were
-- solved completely) is not less than the one-step degradation computed
-- by this routine. In other word this routine computes an estimation
-- (lower bound) of the true degradation. */

static gnum_float degrad(BBDATA *bb, int p, int tagp, gnum_float ap[],
      gnum_float cbar[])
{     int m = bb->m, n = bb->n, q, k;
      gnum_float cur_val, new_val, delta, deg;
      /* determine the current value of the variable xB[p] (note that
         the current value is assumed to be non-integer) */
      cur_val = bb->bbar[p];
      /* determine the new value which the variable xB[p] would have if
         it would leave the basis */
      switch (tagp)
      {  case 'L':
            /* xB[p] increases and goes to its new lower bound */
            new_val = ceil(cur_val);
            insist(new_val > cur_val);
            break;
         case 'U':
            /* xB[p] deacreses and goes to its new upper bound */
            new_val = floor(cur_val);
            insist(new_val < cur_val);
            break;
         default:
            insist(tagp != tagp);
      }
      /* choose the non-basic variable xN[q] which would enter the
         basis instead xB[p] in order to keep dual feasibility of the
         current basis solution (note that the current basis is assumed
         to be optimal and therefore dual feasible) */
      q = dual_col(bb->rsm, tagp, ap, cbar, 1e-10);
      /* if the choice is impossible, the problem with the new bound of
         xB[p] has no (primal) feasible solution; formally in this case
         the degradation of the objective function is infinite */
      if (q == 0) return DBL_MAX;
      /* so far as the p-th row of the current simplex table is
         xB[p] = ... + ap[q] * xN[q] + ..., where ap[q] is an influence
         coefficient, increment of xN[q] in the adjacent basis solution
         is delta(xN[q]) = delta(xB[p]) / ap[q], where delta(xB[p]) is
         increment of xB[p] which is known */
      delta = (new_val - cur_val) / ap[q];
      /* Tomlin noticed that if the variable xN[q] is of integer kind,
         its increment should be not less than one (in absolute value)
         in order that the new subproblem could have an integer feasible
         solution */
      k = bb->rsm->indn[q]; /* x[k] = xN[q] */
      if (m+1 <= k && k <= m+n && bb->mip->kind[k-m])
      {  /* x[k] = xN[q] is integer structural variable */
         if (-1.0 < delta && delta <  0.0) delta = -1.0;
         if ( 0.0 < delta && delta < +1.0) delta = +1.0;
      }
      /* so far as the objective row of the current simplex table is
         Z = ... + cbar[q] * xN[q] + ..., where cbar[q] is the reduced
         cost of xN[q], knowing increment of xN[q] we can determine the
         corresponding degradation of the objective function Z */
      deg = cbar[q] * delta;
      /* since the current basis solution is optimal and the new bound
         of xB[p] makes the basis primal infeasible, the degradation is
         always positive (in case of minimization) that corresponds to
         worsening of the objective function (however due to round-off
         errors the degradation can be slightly negative) */
      return deg;
}

/*----------------------------------------------------------------------
-- branch_drtom - choose a branching variable.
--
-- This routine looks up the list of integer structural variable and
-- chooses that one which being limited by its new lower or upper bound
-- would involve greatest degradation of the objective function. */

int branch_drtom(BBDATA *bb, int *what)
{     int m = bb->m, n = bb->n, i, j, k, p, this = 0;
      gnum_float beta, deg_L, deg_U, deg_max = -DBL_MAX;
      gnum_float *c, *pi, *cbar, *zeta, *ap;
      /* allocate working arrays */
      c = ucalloc(1+m+n, sizeof(gnum_float));
      pi = ucalloc(1+m, sizeof(gnum_float));
      cbar = ucalloc(1+n, sizeof(gnum_float));
      zeta = ucalloc(1+m, sizeof(gnum_float));
      ap = ucalloc(1+n, sizeof(gnum_float));
      /* build the expanded vector of coefficients of the objective
         function (the case of maximization is reduced to the case of
         minimization to simplify program logic) */
      for (i = 1; i <= m; i++) c[i] = 0.0;
      for (j = 1; j <= n; j++)
         c[m+j] = (bb->mip->dir == '-' ? +1.0 : -1.0) * bb->mip->c[j];
      /* compute simplex multipliers */
      eval_pi(bb->rsm, c, pi);
      /* compute reduced costs of non-basic variables */
      eval_cbar(bb->rsm, c, pi, cbar);
      /* look up the list of structural variables */
      for (j = 1; j <= n; j++)
      {  /* skip continuous variable */
         if (!bb->mip->kind[j]) continue;
         /* x[k] = xS[j] */
         k = m + j;
         /* skip non-basic variable */
         if (bb->rsm->posx[k] < 0) continue;
         /* xB[p] = x[k] */
         p = +bb->rsm->posx[k];
         insist(1 <= p && p <= m);
         /* beta = value of xS[j] in the current basis solution */
         beta = bb->bbar[p];
         /* skip basic variable which has an integer value */
         if (beta == floor(beta)) continue;
         /* compute p-th row of inv(B) */
         eval_zeta(bb->rsm, p, zeta);
         /* compute p-th row of the simplex table */
         eval_row(bb->rsm, zeta, ap);
         /* compute degradation (worsening) of the objective function
            when xS[j] = xB[p] goes to its new lower bound */
         deg_L = degrad(bb, p, 'L', ap, cbar);
         /* compute degradation (worsening) of the objective function
            when xS[j] = xB[p] goes to its new upper bound */
         deg_U = degrad(bb, p, 'U', ap, cbar);
         /* choose that variable which involves maximal degradation of
            the objective function and tell the solver to get next the
            problem where the degradation is less than for the other */
         if (deg_max < deg_L || deg_max < deg_U)
         {  this = j;
            if (deg_L > deg_U)
               deg_max = deg_L, *what = 'U';
            else
               deg_max = deg_U, *what = 'L';
         }
      }
      insist(1 <= this && this <= n);
      /* free working arrays */
      ufree(c);
      ufree(pi);
      ufree(cbar);
      ufree(zeta);
      ufree(ap);
      /* return to the calling program */
      return this;
}

/*----------------------------------------------------------------------
-- btrack_fifo - select active problem using FIFO heuristic.
--
-- *Synopsis*
--
-- #include "glpbbm.h"
-- BBNODE *btrack_fifo(BBDATA *bb);
--
-- *Description*
--
-- The btrack_fifo routine selects an appropriate problem which should
-- be considered next from the active list.
--
-- *Returns*
--
-- The btrack_fifo routine returns a pointer to the problem node.
--
-- *Heuristic*
--
-- The btrack_fifo routine implements trivial FIFO (first-in-first-out)
-- heuristic, i.e. it selects the problem which was added to the active
-- list *before* any other problems. If this heuristic is used, the tree
-- is investigated in the breadth-first-search manner. */

BBNODE *btrack_fifo(BBDATA *bb)
{     BBNODE *node;
      node = bb->first;
      insist(node != NULL);
      return node;
}

/*----------------------------------------------------------------------
-- btrack_lifo - select active problem using LIFO heuristic.
--
-- *Synopsis*
--
-- #include "glpbbm.h"
-- BBNODE *btrack_lifo(BBDATA *bb);
--
-- *Description*
--
-- The btrack_lifo routine selects an appropriate problem which should
-- be considered next from the active list.
--
-- *Returns*
--
-- The btrack_lifo routine returns a pointer to the problem node.
--
-- *Heuristic*
--
-- The btrack_lifo routine implements trivial LIFO (last-in-first-out)
-- heuristic, i.e. it selects the problem which was added to the active
-- list *after* any other problems. If this heuristic is used, the tree
-- is investigated in the depth-first-search manner. */

BBNODE *btrack_lifo(BBDATA *bb)
{     BBNODE *node;
      node = bb->last;
      insist(node != NULL);
      return node;
}

/*----------------------------------------------------------------------
-- btrack_bestp - select problem using the best projection heuristic.
--
-- *Synopsis*
--
-- #include "glpbbm.h"
-- BBNODE *btrack_bestp(BBDATA *bb);
--
-- *Description*
--
-- The btrack_bestp routine selects an appropriate problem which should
-- be considered next from the active list.
--
-- *Returns*
--
-- The btrack_bestp routine returns a pointer to the problem node.
--
-- *Heuristic*
--
-- If no integer feasible solution has been found, the btrack_bestp
-- routine selects the active problem whose parent problem has the best
-- value of the objective function, i.e. the tree is investigated in
-- the best-first-search manner.
--
-- If the best integer feasible solution is known, the routine selects
-- an active problem using the best projection heuristic. */

BBNODE *btrack_bestp(BBDATA *bb)
{     BBNODE *this = NULL, *node;
      if (!bb->found)
      {  /* no integer feasible solution has been found */
         gnum_float best = DBL_MAX;
         for (node = bb->first; node != NULL; node = node->right)
         {  if (best > node->up->objval)
               this = node, best = node->up->objval;
         }
      }
      else
      {  /* the best integer feasible solution is known */
         gnum_float best = DBL_MAX;
         for (node = bb->first; node != NULL; node = node->right)
         {  gnum_float deg, val;
            /* deg estimates degradation of the objective function per
               unit of the sum of integer infeasibilities */
            deg = (bb->best - bb->root->objval) / bb->root->infsum;
            /* val estimates optimal value of the objective function if
               the sum of integer infeasibilities would be zero */
            val = node->up->objval + deg * node->up->infsum;
            /* select the problem which have the best estimated optimal
               value of the objective function */
            if (best > val) this = node, best = val;
         }
      }
      return this;
}

/* eof */
