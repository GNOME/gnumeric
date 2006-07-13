/* glpios.h (integer optimization suite) */

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

#ifndef _GLPIOS_H
#define _GLPIOS_H

#include "gnumeric-config.h"
#include "gnumeric.h"
#include "numbers.h"

#include "glpiet.h"

#define ios_attach_npd        glp_ios_attach_npd
#define ios_attach_rgd        glp_ios_attach_rgd
#define ios_attach_cgd        glp_ios_attach_cgd
#define ios_attach_row        glp_ios_attach_row
#define ios_attach_col        glp_ios_attach_col
#define ios_detach_row        glp_ios_detach_row
#define ios_detach_col        glp_ios_detach_col
#define ios_hook_routine      glp_ios_hook_routine

#define ios_create_tree       glp_ios_create_tree
#define ios_revive_node       glp_ios_revive_node
#define ios_freeze_node       glp_ios_freeze_node
#define ios_clone_node        glp_ios_clone_node
#define ios_delete_node       glp_ios_delete_node
#define ios_delete_tree       glp_ios_delete_tree

#define ios_get_curr_node     glp_ios_get_curr_node
#define ios_get_next_node     glp_ios_get_next_node
#define ios_get_prev_node     glp_ios_get_prev_node
#define ios_get_up_node       glp_ios_get_up_node
#define ios_get_node_lev      glp_ios_get_node_lev
#define ios_get_node_cnt      glp_ios_get_node_cnt
#define ios_pseudo_root       glp_ios_pseudo_root

#define ios_set_obj_dir       glp_ios_set_obj_dir
#define ios_add_rows          glp_ios_add_rows
#define ios_add_cols          glp_ios_add_cols
#define ios_check_name        glp_ios_check_name
#define ios_set_row_name      glp_ios_set_row_name
#define ios_set_col_name      glp_ios_set_col_name
#define ios_set_row_attr      glp_ios_set_row_attr
#define ios_set_col_attr      glp_ios_set_col_attr
#define ios_set_col_kind      glp_ios_set_col_kind
#define ios_set_row_bnds      glp_ios_set_row_bnds
#define ios_set_col_bnds      glp_ios_set_col_bnds
#define ios_set_obj_coef      glp_ios_set_obj_coef
#define ios_set_mat_row       glp_ios_set_mat_row
#define ios_set_mat_col       glp_ios_set_mat_col
#define ios_set_row_stat      glp_ios_set_row_stat
#define ios_set_col_stat      glp_ios_set_col_stat
#define ios_del_rows          glp_ios_del_rows
#define ios_del_cols          glp_ios_del_cols

#define ios_get_obj_dir       glp_ios_get_obj_dir
#define ios_get_num_rows      glp_ios_get_num_rows
#define ios_get_num_cols      glp_ios_get_num_cols
#define ios_get_num_nz        glp_ios_get_num_nz
#define ios_get_row_name      glp_ios_get_row_name
#define ios_get_col_name      glp_ios_get_col_name
#define ios_get_row_mark      glp_ios_get_row_mark
#define ios_get_row_link      glp_ios_get_row_link
#define ios_get_col_mark      glp_ios_get_col_mark
#define ios_get_col_link      glp_ios_get_col_link
#define ios_get_col_kind      glp_ios_get_col_kind
#define ios_get_row_bnds      glp_ios_get_row_bnds
#define ios_get_col_bnds      glp_ios_get_col_bnds
#define ios_get_obj_coef      glp_ios_get_obj_coef
#define ios_get_mat_row       glp_ios_get_mat_row
#define ios_get_mat_col       glp_ios_get_mat_col

#define ios_p_status          glp_ios_p_status
#define ios_d_status          glp_ios_d_status
#define ios_get_row_soln      glp_ios_get_row_soln
#define ios_get_col_soln      glp_ios_get_col_soln
#define ios_get_row_pi        glp_ios_get_row_pi
#define ios_is_col_frac       glp_ios_is_col_frac

#define ios_extract_lp        glp_ios_extract_lp
#define ios_put_lp_soln       glp_ios_put_lp_soln
#define ios_solve_root        glp_ios_solve_root
#define ios_solve_node        glp_ios_solve_node

#define ios_branch_first      glp_ios_branch_first
#define ios_branch_last       glp_ios_branch_last
#define ios_branch_drtom      glp_ios_branch_drtom
#define ios_branch_on         glp_ios_branch_on
#define ios_select_fifo       glp_ios_select_fifo
#define ios_select_lifo       glp_ios_select_lifo
#define ios_select_node       glp_ios_select_node
#define ios_driver            glp_ios_driver

typedef struct IOS IOS;       /* integer optimization suite */
typedef struct IOSNPD IOSNPD; /* node (sub)problem descriptor */
typedef struct IOSRGD IOSRGD; /* row global descriptor */
typedef struct IOSCGD IOSCGD; /* column global descriptor */
typedef struct IOSROW IOSROW; /* row local descriptor */
typedef struct IOSCOL IOSCOL; /* column local descriptor */

struct IOS
{     /* integer optimization suite */
      /*--------------------------------------------------------------*/
      /* memory management */
      DMP *npd_pool;
      /* memory pool for IOSNPD objects */
      DMP *rgd_pool;
      /* memory pool for IOSRGD objects */
      DMP *cgd_pool;
      /* memory pool for IOSCGD objects */
      DMP *row_pool;
      /* memory pool for IOSROW objects */
      DMP *col_pool;
      /* memory pool for IOSCOL objects */
      /*--------------------------------------------------------------*/
      /* enumeration tree interface */
      IET *iet;
      /* implicit enumeration tree */
      char *hook_name;
      /* pointer to a symbolic name passed to the hook routine */
      union
      {  IOSNPD *npd;
         IOSRGD *rgd;
         IOSCGD *cgd;
      } hook_link;
      /* pointer to a global extension passed to the hook routine */
      /*--------------------------------------------------------------*/
      /* main options */
      int dir;
      /* optimization direction flag (objective sense): */
#define IOS_MIN         501   /* minimization */
#define IOS_MAX         502   /* maximization */
      int int_obj;
      /* if this flag is set, the objective function is integral */
      int row_gen;
      /* if this flag is set, row generation is enabled */
      int col_gen;
      /* if this flag is set, column generation is enabled */
      int cut_gen;
      /* if this flag is set, cut generation is enabled */
      /*--------------------------------------------------------------*/
      /* incumbent objective value */
      int found;
      /* if this flag is set, at least one integer feasible solution has
         been found */
      gnm_float best;
      /* incumbent objective value, that is the objective value which
         corresponds to the best known integer feasible solution (it is
         undefined if the flag found is not set); this value is a global
         upper (minimization) or lower (maximization) bound for integer
         optimal solution of the original problem being solved */
      /*--------------------------------------------------------------*/
      /* basic solution of LP relaxation of the current subproblem */
      int p_stat;
      /* primal status: */
#define IOS_UNDEF       511   /* undefined */
#define IOS_FEAS        512   /* feasible */
#define IOS_INFEAS      513   /* infeasible (intermediate) */
#define IOS_NOFEAS      514   /* infeasible (final) */
      int d_stat;
      /* dual status: */
#define IOS_UNDEF       511   /* undefined */
#define IOS_FEAS        512   /* feasible */
#define IOS_INFEAS      513   /* infeasible (intermediate) */
#define IOS_NOFEAS      514   /* infeasible (final) */
      gnm_float lp_obj;
      /* value of the objective function */
      gnm_float lp_sum;
      /* the sum of primal infeasibilites */
      int ii_cnt;
      /* number of columns (structural variables) of integer kind whose
         primal values are fractional */
      gnm_float ii_sum;
      /* the sum of integer infeasibilities */
      /*--------------------------------------------------------------*/
      /* control parameters and statistics */
      int msg_lev;
      /* level of messages issued by the solver:
         0 - no output
         1 - error messages only
         2 - normal output
         3 - detailed step-by-step output */
      int init_lp;
      /* option to solve initial LP relaxation of the root subproblem:
         0 - solve starting from the standard basis of all slacks
         1 - solve starting from an advanced basis
         2 - solve starting from the basis provided by the application
             procedure */
      int scale;
      /* option to scale LP relaxation of the current subproblem before
         solving:
         0 - do not scale
         1 - scale using default settings */
      gnm_float tol_int;
      /* absolute tolerance used to check if the current basic solution
         is integer feasible */
      gnm_float tol_obj;
      /* relative tolerance used to check if the value of the objective
         function is better than the incumbent objective value */
      gnm_float out_frq;
      /* output frequency, in seconds; this parameter specifies how
         frequently the solver sends information about the progress of
         the search to the standard output */
      gnm_float out_dly;
      /* output delay, in seconds; this parameter specifies how long
         output from the LP solver is delayed on solving LP relaxation
         of the current subproblem; zero value means no delay */
      int it_cnt;
      /* simplex iterations count, that is the total number of simplex
         iterations performed by the LP solver */
      gnm_float tm_beg;
      /* starting time of the search, in seconds; the total time of the
         search is the difference between utime() and tm_beg */
      gnm_float tm_lag;
      /* the most recent time, in seconds, at which the progress of the
         the search was displayed */
      /*--------------------------------------------------------------*/
      /* application procedure interface */
      void (*appl)(IOS *ios, void *info);
      /* entry point to the event-driven application procedure */
      void *info;
      /* transitional pointer passed to the application procedure */
      int event;
      /* current event code: */
#define IOS_V_NONE      601   /* dummy event (never raised) */
#define IOS_V_INIT      602   /* initializing */
#define IOS_V_GENROW    603   /* row generation required */
#define IOS_V_GENCOL    604   /* column generation required */
#define IOS_V_GENCUT    605   /* cut generation required */
#define IOS_V_BINGO     606   /* better integer solution found */
#define IOS_V_BRANCH    607   /* branching required */
#define IOS_V_SELECT    608   /* subproblem selection required */
#define IOS_V_DELSUB    609   /* subproblem is being deleted */
#define IOS_V_DELROW    610   /* row is being deleted */
#define IOS_V_DELCOL    611   /* column is being deleted */
#define IOS_V_TERM      612   /* terminating */
      int r_flag;
      /* reoptimization flag; if this flag is set, LP relaxation of the
         current subproblem needs to be re-optimized */
      int b_flag;
      /* branching flag; if this flag is set, branching is done */
      int t_flag;
      /* backtracking flag; if this flag is set, some active subproblem
         has been selected */
};

struct IOSNPD
{     /* extension of node (sub)problem descriptor */
      gnm_float bound;
      /* local lower (minimization) or upper (maximization) bound of
         integer optimal solution of *this* subproblem; this bound is
         local in the sense that only subproblems in the subtree rooted
         at this node cannot have better integer feasible solutions;
         on creating a subproblem its local bound is inherited from its
         parent and then can be made stronger (never weaker); note that
         for the root subproblem until its complete LP relaxation has
         been solved, the local bound is set to -DBL_MAX (minimization)
         or +DBL_MAX (maximization) */
      /* if this subproblem is inactive, the following two quantities
         correspond to final optimal solution of its LP relaxation; for
         active subproblems these quantities are undefined */
      int ii_cnt;
      /* number of columns (structural variables) of integer kind whose
         primal values are fractional */
      gnm_float ii_sum;
      /* the sum of integer infeasibilities */
};

struct IOSRGD
{     /* extension of row global descriptor */
      int mark;
      /* row mark (reserved for application) */
      void *link;
      /* row link (reserved for application) */
};

struct IOSCGD
{     /* extension of column global descriptor */
      int kind;
      /* column kind: */
#define IOS_NUM         521   /* continuous column */
#define IOS_INT         522   /* integer column */
      int mark;
      /* column mark (reserved for application) */
      void *link;
      /* column link (reserved for application) */
};

struct IOSROW
{     /* extension of row local descriptor */
      /* type of the auxiliary variable: */
#define IOS_FR       IET_FR   /* free variable */
#define IOS_LO       IET_LO   /* variable with lower bound */
#define IOS_UP       IET_UP   /* variable with upper bound */
#define IOS_DB       IET_DB   /* gnm_float-bounded variable */
#define IOS_FX       IET_FX   /* fixed variable */
      /* status of the auxiliary variable: */
#define IOS_BS       IET_BS   /* basic variable */
#define IOS_NL       IET_NL   /* non-basic variable on lower bound */
#define IOS_NU       IET_NU   /* non-basic variable on upper bound */
#define IOS_NF       IET_NF   /* non-basic free variable */
#define IOS_NS       IET_NS   /* non-basic fixed variable */
      gnm_float prim;
      /* primal value of the auxiliary variable */
      gnm_float dual;
      /* dual value (reduced cost) of the auxiliary variable */
      gnm_float pi;
      /* Lagrange multiplier for this row which is intended for using
         in the application procedure to generate columns; if the basic
         solution of LP relaxation of the current subproblem is optimal,
         this multiplier corresponds to the original objective function;
         if the LP relaxation has no (primal) feasible solutions, this
         multiplier corresponds to the sum of primal infeasibilities;
         in the latter case, if the original objective function has to
         be maximized, the sum of primal infeasibilities is taken with
         the minus sign in order to keep the original objective sense */
};

struct IOSCOL
{     /* extension of column local descriptor */
      /* type of the structural variable: */
#define IOS_FR       IET_FR   /* free variable */
#define IOS_LO       IET_LO   /* variable with lower bound */
#define IOS_UP       IET_UP   /* variable with upper bound */
#define IOS_DB       IET_DB   /* gnm_float-bounded variable */
#define IOS_FX       IET_FX   /* fixed variable */
      /* status of the structural variable: */
#define IOS_BS       IET_BS   /* basic variable */
#define IOS_NL       IET_NL   /* non-basic variable on lower bound */
#define IOS_NU       IET_NU   /* non-basic variable on upper bound */
#define IOS_NF       IET_NF   /* non-basic free variable */
#define IOS_NS       IET_NS   /* non-basic fixed variable */
      gnm_float prim;
      /* primal value of the structural variable */
      gnm_float dual;
      /* dual value (reduced cost) of the structural variable */
      int frac;
      /* if this flag is set, the column is fractional-valued, i.e. it
         is of integer kind, but its primal value is integer infeasible
         within given tolerance */
};

/**********************************************************************/
/* * *               LOW-LEVEL MAINTENANCE ROUTINES               * * */
/**********************************************************************/

#define ios_get_npd_ptr(ios, p) \
      ((IOSNPD *)iet_get_node_link(ios->iet, p))
/* obtain pointer to extension of subproblem descriptor */

#define ios_get_rgd_ptr(ios, i) \
      ((IOSRGD *)iet_get_row_link(ios->iet, i))
/* obtain pointer to extension of row global descriptor */

#define ios_get_cgd_ptr(ios, j) \
      ((IOSCGD *)iet_get_col_link(ios->iet, j))
/* obtain pointer to extension of column global descriptor */

#define ios_get_row_ptr(ios, i) \
      ((IOSROW *)iet_get_row_locl(ios->iet, i))
/* obtain pointer to extension of row local descriptor */

#define ios_get_col_ptr(ios, j) \
      ((IOSCOL *)iet_get_col_locl(ios->iet, j))
/* obtain pointer to extension of column local descriptor */

#define ios_set_npd_ptr(ios, p, node) \
      iet_set_node_link(ios->iet, p, node)
/* store pointer to extension of subproblem descriptor */

#define ios_set_rgd_ptr(ios, i, rgd) \
      iet_set_row_link(ios->iet, i, rgd)
/* store pointer to extension of row global descriptor */

#define ios_set_cgd_ptr(ios, j, cgd) \
      iet_set_col_link(ios->iet, j, cgd)
/* store pointer to extension of column global descriptor */

#define ios_set_row_ptr(ios, i, row) \
      iet_set_row_locl(ios->iet, i, row)
/* store pointer to extension of row local descriptor */

#define ios_set_col_ptr(ios, j, col) \
      iet_set_col_locl(ios->iet, j, col)
/* store pointer to extension of column local descriptor */

void ios_attach_npd(IOS *ios, int p);
/* attach extension to subproblem descriptor */

void ios_attach_rgd(IOS *ios, int i);
/* attach extension to row global descriptor */

void ios_attach_cgd(IOS *ios, int j);
/* attach extension to column global descriptor */

void ios_attach_row(IOS *ios, int i);
/* attach extension to row local descriptor */

void ios_attach_col(IOS *ios, int j);
/* attach extension to column local descriptor */

void ios_detach_row(IOS *ios, int i);
/* detach extension from row local descriptor */

void ios_detach_col(IOS *ios, int j);
/* detach extension from column local descriptor */

void ios_hook_routine(void *info, int what, char *name, void *link);
/* callback interface to enumeration tree */

/**********************************************************************/
/* * *                  TREE MANAGEMENT ROUTINES                  * * */
/**********************************************************************/

IOS *ios_create_tree(void (*appl)(IOS *ios, void *info), void *info);
/* create integer optimization suite */

void ios_revive_node(IOS *ios, int p);
/* revive specified subproblem */

void ios_freeze_node(IOS *ios);
/* freeze current subproblem */

void ios_clone_node(IOS *ios, int p, int nnn, int ref[]);
/* clone specified subproblem */

void ios_delete_node(IOS *ios, int p);
/* delete specified subproblem */

void ios_delete_tree(IOS *ios);
/* delete integer optimization suite */

/**********************************************************************/
/* * *                  TREE EXPLORING ROUTINES                   * * */
/**********************************************************************/

int ios_get_curr_node(IOS *ios);
/* determine current active subproblem */

int ios_get_next_node(IOS *ios, int p);
/* determine next active subproblem */

int ios_get_prev_node(IOS *ios, int p);
/* determine previous active subproblem */

int ios_get_up_node(IOS *ios, int p);
/* determine parent subproblem */

int ios_get_node_lev(IOS *ios, int p);
/* determine subproblem level */

int ios_get_node_cnt(IOS *ios, int p);
/* determine number of child subproblems */

int ios_pseudo_root(IOS *ios);
/* find pseudo-root of the tree */

/**********************************************************************/
/* * *               SUBPROBLEM MODIFYING ROUTINES                * * */
/**********************************************************************/

void ios_set_obj_dir(IOS *ios, int dir);
/* set optimization direction flag */

void ios_add_rows(IOS *ios, int nrs);
/* add new rows to current subproblem */

void ios_add_cols(IOS *ios, int ncs);
/* add new columns to current subproblem */

int ios_check_name(IOS *ios, char *name);
/* check correctness of symbolic name */

void ios_set_row_name(IOS *ios, int i, char *name);
/* assign symbolic name to row */

void ios_set_col_name(IOS *ios, int j, char *name);
/* assign symbolic name to column */

void ios_set_row_attr(IOS *ios, int i, int mark, void *link);
/* assign attributes to row */

void ios_set_col_attr(IOS *ios, int j, int mark, void *link);
/* assign attributes to column */

void ios_set_col_kind(IOS *ios, int j, int kind);
/* set column kind */

void ios_set_row_bnds(IOS *ios, int i, int type, gnm_float lb, gnm_float ub);
/* set row type and bounds */

void ios_set_col_bnds(IOS *ios, int j, int type, gnm_float lb, gnm_float ub);
/* set column type and bounds */

void ios_set_obj_coef(IOS *ios, int j, gnm_float coef);
/* set objective coefficient or constant term */

void ios_set_mat_row(IOS *ios, int i, int len, int ind[], gnm_float val[]);
/* replace row of constraint matrix */

void ios_set_mat_col(IOS *ios, int j, int len, int ind[], gnm_float val[]);
/* replace column of constraint matrix */

void ios_set_row_stat(IOS *ios, int i, int stat);
/* set row status */

void ios_set_col_stat(IOS *ios, int j, int stat);
/* set column status */

void ios_del_rows(IOS *ios, int nrs, int num[]);
/* delete specified rows from current subproblem */

void ios_del_cols(IOS *ios, int ncs, int num[]);
/* delete specified columns from current subproblem */

/**********************************************************************/
/* * *                SUBPROBLEM QUERYING ROUTINES                * * */
/**********************************************************************/

int ios_get_obj_dir(IOS *ios);
/* determine optimization direction flag */

int ios_get_num_rows(IOS *ios);
/* determine number of rows */

int ios_get_num_cols(IOS *ios);
/* determine number of columns */

int ios_get_num_nz(IOS *ios);
/* determine number of constraint coefficients */

char *ios_get_row_name(IOS *ios, int i);
/* obtain row name */

char *ios_get_col_name(IOS *ios, int j);
/* obtain column name */

int ios_get_row_mark(IOS *ios, int i);
/* obtain row mark */

void *ios_get_row_link(IOS *ios, int i);
/* obtain row link */

int ios_get_col_mark(IOS *ios, int j);
/* obtain column mark */

void *ios_get_col_link(IOS *ios, int j);
/* obtain column link */

int ios_get_col_kind(IOS *ios, int j);
/* determine column kind */

int ios_get_row_bnds(IOS *ios, int i, gnm_float *lb, gnm_float *ub);
/* determine row type and bounds */

int ios_get_col_bnds(IOS *ios, int j, gnm_float *lb, gnm_float *ub);
/* determine column type and bounds */

gnm_float ios_get_obj_coef(IOS *ios, int j);
/* determine objective coefficient */

int ios_get_mat_row(IOS *ios, int i, int ind[], gnm_float val[]);
/* obtain row of constraint matrix */

int ios_get_mat_col(IOS *ios, int j, int ind[], gnm_float val[]);
/* obtain column of constraint matrix */

/**********************************************************************/
/* * *              BASIC SOLUTION QUERYING ROUTINES              * * */
/**********************************************************************/

int ios_p_status(IOS *ios);
/* determine primal status of basic solution */

int ios_d_status(IOS *ios);
/* determine dual status of basic solution */

int ios_get_row_soln(IOS *ios, int i, gnm_float *prim, gnm_float *dual);
/* obtain basic solution for given row */

int ios_get_col_soln(IOS *ios, int j, gnm_float *prim, gnm_float *dual);
/* obtain basic solution for given column */

gnm_float ios_get_row_pi(IOS *ios, int i);
/* determine Lagrange multiplier for given row */

int ios_is_col_frac(IOS *ios, int j);
/* check if specified column has fractional value */

/**********************************************************************/
/* * *                LP SOLVER INTERFACE ROUTINES                * * */
/**********************************************************************/

void *ios_extract_lp(IOS *ios);
/* extract LP relaxation of current subproblem */

void ios_put_lp_soln(IOS *ios, void *lp);
/* store basic solution of LP relaxation */

int ios_solve_root(IOS *ios);
/* solve initial LP relaxation */

int ios_solve_node(IOS *ios);
/* solve LP relaxation of current subproblem */

/**********************************************************************/
/* * *                  IOS FUNCTIONARY ROUTINES                  * * */
/**********************************************************************/

int ios_branch_first(IOS *ios, int *next);
/* choose first column to branch on */

int ios_branch_last(IOS *ios, int *next);
/* choose last column to branch on */

int ios_branch_drtom(IOS *ios, int *next);
/* choose column using Driebeck-Tomlin heuristic */

void ios_branch_on(IOS *ios, int j, int next);
/* perform branching on specified column */

int ios_select_fifo(IOS *ios);
/* select subproblem using FIFO heuristic */

int ios_select_lifo(IOS *ios);
/* select subproblem using LIFO heuristic */

void ios_select_node(IOS *ios, int p);
/* select subproblem to continue the search */

int ios_driver(void (*appl)(IOS *ios, void *info), void *info);
/* integer optimization driver routine */

#endif

/* eof */
