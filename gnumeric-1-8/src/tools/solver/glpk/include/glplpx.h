/* glplpx.h (LP/MIP problem object) */

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

#ifndef _GLPLPX_H
#define _GLPLPX_H

#include "gnumeric-config.h"
#include "gnumeric.h"
#include "numbers.h"

#include "glpavl.h"
#include "glpinv.h"
#include "glpstr.h"

#define lpx_create_prob       glp_lpx_create_prob
#define lpx_set_prob_name     glp_lpx_set_prob_name
#define lpx_set_class         glp_lpx_set_class
#define lpx_set_obj_name      glp_lpx_set_obj_name
#define lpx_set_obj_dir       glp_lpx_set_obj_dir
#define lpx_add_rows          glp_lpx_add_rows
#define lpx_add_cols          glp_lpx_add_cols
#define lpx_set_row_name      glp_lpx_set_row_name
#define lpx_set_col_name      glp_lpx_set_col_name
#define lpx_set_col_kind      glp_lpx_set_col_kind
#define lpx_set_row_bnds      glp_lpx_set_row_bnds
#define lpx_set_col_bnds      glp_lpx_set_col_bnds
#define lpx_set_obj_coef      glp_lpx_set_obj_coef
#define lpx_set_mat_row       glp_lpx_set_mat_row
#define lpx_set_mat_col       glp_lpx_set_mat_col
#define lpx_load_matrix       glp_lpx_load_matrix
#define lpx_order_matrix      glp_lpx_order_matrix
#define lpx_set_rii           glp_lpx_set_rii
#define lpx_set_sjj           glp_lpx_set_sjj
#define lpx_set_row_stat      glp_lpx_set_row_stat
#define lpx_set_col_stat      glp_lpx_set_col_stat
#define lpx_del_rows          glp_lpx_del_rows
#define lpx_del_cols          glp_lpx_del_cols
#define lpx_delete_prob       glp_lpx_delete_prob
#if 1 /* 15/VIII-2004 */
#define lpx_create_index      glp_lpx_create_index
#define lpx_find_row          glp_lpx_find_row
#define lpx_find_col          glp_lpx_find_col
#define lpx_delete_index      glp_lpx_delete_index
#endif
#define lpx_put_lp_basis      glp_lpx_put_lp_basis
#define lpx_put_solution      glp_lpx_put_solution
#define lpx_put_ray_info      glp_lpx_put_ray_info
#define lpx_put_ipt_soln      glp_lpx_put_ipt_soln
#define lpx_put_mip_soln      glp_lpx_put_mip_soln

#define lpx_get_prob_name     glp_lpx_get_prob_name
#define lpx_get_class         glp_lpx_get_class
#define lpx_get_obj_name      glp_lpx_get_obj_name
#define lpx_get_obj_dir       glp_lpx_get_obj_dir
#define lpx_get_num_rows      glp_lpx_get_num_rows
#define lpx_get_num_cols      glp_lpx_get_num_cols
#define lpx_get_num_int       glp_lpx_get_num_int
#define lpx_get_num_bin       glp_lpx_get_num_bin
#define lpx_get_row_name      glp_lpx_get_row_name
#define lpx_get_col_name      glp_lpx_get_col_name
#define lpx_get_col_kind      glp_lpx_get_col_kind
#define lpx_get_row_type      glp_lpx_get_row_type
#define lpx_get_row_lb        glp_lpx_get_row_lb
#define lpx_get_row_ub        glp_lpx_get_row_ub
#define lpx_get_col_type      glp_lpx_get_col_type
#define lpx_get_col_lb        glp_lpx_get_col_lb
#define lpx_get_col_ub        glp_lpx_get_col_ub
#define lpx_get_obj_coef      glp_lpx_get_obj_coef
#define lpx_get_num_nz        glp_lpx_get_num_nz
#define lpx_get_mat_row       glp_lpx_get_mat_row
#define lpx_get_mat_col       glp_lpx_get_mat_col
#define lpx_get_rii           glp_lpx_get_rii
#define lpx_get_sjj           glp_lpx_get_sjj
#define lpx_is_b_avail        glp_lpx_is_b_avail
#define lpx_get_b_info        glp_lpx_get_b_info
#define lpx_get_row_b_ind     glp_lpx_get_row_b_ind
#define lpx_get_col_b_ind     glp_lpx_get_col_b_ind
#define lpx_access_inv        glp_lpx_access_inv
#define lpx_get_status        glp_lpx_get_status
#define lpx_get_prim_stat     glp_lpx_get_prim_stat
#define lpx_get_dual_stat     glp_lpx_get_dual_stat
#define lpx_get_obj_val       glp_lpx_get_obj_val
#define lpx_get_row_stat      glp_lpx_get_row_stat
#define lpx_get_row_prim      glp_lpx_get_row_prim
#define lpx_get_row_dual      glp_lpx_get_row_dual
#define lpx_get_col_stat      glp_lpx_get_col_stat
#define lpx_get_col_prim      glp_lpx_get_col_prim
#define lpx_get_col_dual      glp_lpx_get_col_dual
#define lpx_get_ray_info      glp_lpx_get_ray_info
#define lpx_ipt_status        glp_lpx_ipt_status
#define lpx_ipt_obj_val       glp_lpx_ipt_obj_val
#define lpx_ipt_row_prim      glp_lpx_ipt_row_prim
#define lpx_ipt_row_dual      glp_lpx_ipt_row_dual
#define lpx_ipt_col_prim      glp_lpx_ipt_col_prim
#define lpx_ipt_col_dual      glp_lpx_ipt_col_dual
#define lpx_mip_status        glp_lpx_mip_status
#define lpx_mip_obj_val       glp_lpx_mip_obj_val
#define lpx_mip_row_val       glp_lpx_mip_row_val
#define lpx_mip_col_val       glp_lpx_mip_col_val
#define lpx_get_row_bnds      glp_lpx_get_row_bnds       /* obsolete */
#define lpx_get_col_bnds      glp_lpx_get_col_bnds       /* obsolete */
#define lpx_get_row_info      glp_lpx_get_row_info       /* obsolete */
#define lpx_get_col_info      glp_lpx_get_col_info       /* obsolete */

#define lpx_reset_parms       glp_lpx_reset_parms
#define lpx_set_int_parm      glp_lpx_set_int_parm
#define lpx_get_int_parm      glp_lpx_get_int_parm
#define lpx_set_real_parm     glp_lpx_set_real_parm
#define lpx_get_real_parm     glp_lpx_get_real_parm

#define lpx_scale_prob        glp_lpx_scale_prob
#define lpx_unscale_prob      glp_lpx_unscale_prob

#define lpx_std_basis         glp_lpx_std_basis
#define lpx_adv_basis         glp_lpx_adv_basis

#define lpx_simplex           glp_lpx_simplex
#define lpx_check_kkt         glp_lpx_check_kkt
#define lpx_interior          glp_lpx_interior
#define lpx_integer           glp_lpx_integer
#define lpx_check_int         glp_lpx_check_int
#define lpx_intopt            glp_lpx_intopt

#define lpx_invert            glp_lpx_invert
#define lpx_ftran             glp_lpx_ftran
#define lpx_btran             glp_lpx_btran
#define lpx_eval_b_prim       glp_lpx_eval_b_prim
#define lpx_eval_b_dual       glp_lpx_eval_b_dual
#define lpx_warm_up           glp_lpx_warm_up
#define lpx_eval_tab_row      glp_lpx_eval_tab_row
#define lpx_eval_tab_col      glp_lpx_eval_tab_col
#define lpx_transform_row     glp_lpx_transform_row
#define lpx_transform_col     glp_lpx_transform_col
#define lpx_prim_ratio_test   glp_lpx_prim_ratio_test
#define lpx_dual_ratio_test   glp_lpx_dual_ratio_test

#define lpx_remove_tiny       glp_lpx_remove_tiny
#define lpx_reduce_form       glp_lpx_reduce_form
#define lpx_eval_row          glp_lpx_eval_row
#define lpx_eval_degrad       glp_lpx_eval_degrad
#define lpx_gomory_cut        glp_lpx_gomory_cut

#define lpx_read_mps          glp_lpx_read_mps
#define lpx_write_mps         glp_lpx_write_mps
#define lpx_read_bas          glp_lpx_read_bas
#define lpx_write_bas         glp_lpx_write_bas
#define lpx_read_freemps      glp_lpx_read_freemps
#define lpx_write_freemps     glp_lpx_write_freemps
#define lpx_print_prob        glp_lpx_print_prob
#define lpx_print_sol         glp_lpx_print_sol
#define lpx_print_ips         glp_lpx_print_ips
#define lpx_print_mip         glp_lpx_print_mip
#define lpx_print_sens_bnds   glp_lpx_print_sens_bnds
#define lpx_read_cpxlp        glp_lpx_read_cpxlp
#define lpx_write_cpxlp       glp_lpx_write_cpxlp
#define lpx_extract_prob      glp_lpx_extract_prob
#define lpx_read_model        glp_lpx_read_model
#define lpx_read_prob         glp_lpx_read_prob
#define lpx_write_prob        glp_lpx_write_prob

/*----------------------------------------------------------------------
-- The structure LPX is an LP/MIP problem object, which corresponds to
-- the following problem statement:
--
--    minimize (or maximize)
--
--       Z = c[1]*x[m+1] + c[2]*x[m+2] + ... + c[n]*x[m+n] + c[0]    (1)
--
--    subject to linear constraints
--
--       x[1] = a[1,1]*x[m+1] + a[1,2]*x[m+1] + ... + a[1,n]*x[m+n]
--       x[2] = a[2,1]*x[m+1] + a[2,2]*x[m+1] + ... + a[2,n]*x[m+n]  (2)
--                . . . . . .
--       x[m] = a[m,1]*x[m+1] + a[m,2]*x[m+1] + ... + a[m,n]*x[m+n]
--
--    and bounds of variables
--
--         l[1] <= x[1]   <= u[1]
--         l[2] <= x[2]   <= u[2]                                    (3)
--             . . . . . .
--       l[m+n] <= x[m+n] <= u[m+n]
--
-- where:
-- x[1], ..., x[m]      - rows (auxiliary variables);
-- x[m+1], ..., x[m+n]  - columns (structural variables);
-- Z                    - objective function;
-- c[1], ..., c[n]      - coefficients of the objective function;
-- c[0]                 - constant term of the objective function;
-- a[1,1], ..., a[m,n]  - constraint coefficients;
-- l[1], ..., l[m+n]    - lower bounds of variables;
-- u[1], ..., u[m+n]    - upper bounds of variables.
--
-- Using vector-matrix notations the LP problem (1)-(3) can be written
-- as follows:
--
--    minimize (or maximize)
--
--       Z = c * x + c[0]                                            (4)
--
--    subject to linear constraints
--
--       xR = A * xS                                                 (5)
--
--    and bounds of variables
--
--       l <= x <= u                                                 (6)
--
-- where:
-- xR                   - vector of auxiliary variables;
-- xS                   - vector of structural variables;
-- x = (xR, xS)         - vector of all variables;
-- c                    - vector of objective coefficients;
-- A                    - constraint matrix (has m rows and n columns);
-- l                    - vector of lower bounds of variables;
-- u                    - vector of upper bounds of variables.
--
-- The system of constraints (5) can be written in homogeneous form as
-- follows:
--
--    A~ * x = 0,                                                    (7)
--
-- where
--
--    A~ = (I | -A)                                                  (8)
--
-- is an augmented constraint matrix (has m rows and m+n columns), I is
-- the unity matrix of the order m. Note that in the structure LPX only
-- the original constraint matrix A is explicitly stored.
--
-- The current basis is defined by partitioning columns of the matrix
-- A~ into basic and non-basic ones, in which case the system (7) can
-- be written as
--
--    B * xB + N * xN = 0,                                           (9)
--
-- where B is a square non-sigular mxm matrix built of basic columns
-- and called the basis matrix, N is a mxn matrix built of non-basic
-- columns, xB is vector of basic variables, xN is vector of non-basic
-- variables.
--
-- Using the partitioning (9) the LP problem (4)-(6) can be written in
-- a form, which defines components of the corresponding basic solution
-- and is called the simplex table:
--
--    Z = d * xN + c[0]                                             (10)
--
--    xB = A^ * xN                                                  (11)
--
--    lB <= xB <= uB                                                (12)
--
--    lN <= xN <= uN                                                (13)
--
-- where:
--
--    A^ = (alfa[i,j]) = - inv(B) * N                               (14)
--
-- is the mxn matrix of influence coefficients;
--
--    d = (d[j]) = cN - N' * pi                                     (15)
--
-- is the vector of reduced costs of non-basic variables; and
--
--    pi = (pi[i]) = inv(B') * cB                                   (16)
--
-- is the vector of simplex (Lagrange) multipliers, which correspond to
-- the equiality constraints (5).
--
-- Note that signs of the reduced costs d are determined by the formula
-- (15) in both cases of minimization and maximization.
--
-- The structure LPX allows scaling the problem. In the scaled problem
-- the constraint matrix is scaled and has the form:
--
--    A" = R * A * S,                                               (17)
--
-- where A is the constraint matrix of the original (unscaled) problem,
-- R and S are, respectively, diagonal scaling mxm and nxn matrices with
-- positive diagonal elements used to scale rows and columns of A.
--
-- The connection between the original and scaled components is defined
-- by (17) and expressed with the following formulae:
--
--    c"  = S * c          (objective coefficients)
--
--    xR" = R * xR         (values of auxiliary variables)
--    lR" = R * lR         (lower bounds of auxiliary variables)
--    uR" = R * uR         (upper bounds of auxiliary variables)
--
--    xS" = inv(S) * xS    (values of structural variables)
--    lS" = inv(S) * lS    (lower bounds of structural variables)
--    uS" = inv(S) * uS    (upper bounds of structural variables)
--
--    A"  = R * A * S      (constraint matrix)
--
-- Note that substitution scaled components into (4)-(6) gives the same
-- LP problem. */

typedef struct LPX LPX;
typedef struct LPXROW LPXROW;
typedef struct LPXCOL LPXCOL;
typedef struct LPXAIJ LPXAIJ;

#ifndef _GLPLPX_UNLOCK
struct LPX { int none_; };
struct LPX_LOCKED
#else
struct LPX
#endif
{     /* LP/MIP problem object */
      /*--------------------------------------------------------------*/
      /* memory management */
      DMP *row_pool;
      /* memory pool for LPXROW objects */
      DMP *col_pool;
      /* memory pool for LPXCOL objects */
      DMP *aij_pool;
      /* memory pool for LPXAIJ objects */
      DMP *str_pool;
      /* memory pool for segmented character strings */
      char *str_buf; /* char str_buf[255+1]; */
      /* working buffer to store character strings */
      /*--------------------------------------------------------------*/
      /* LP/MIP data */
      STR *name;
      /* problem name (1 to 255 chars); NULL means no name is assigned
         to the problem */
      int klass;
      /* problem class: */
#define LPX_LP          100   /* linear programming (LP) */
#define LPX_MIP         101   /* mixed integer programming (MIP) */
      STR *obj;
      /* objective function name (1 to 255 chars); NULL means no name
         is assigned to the objective function */
      int dir;
      /* optimization direction flag (objective "sense"): */
#define LPX_MIN         120   /* minimization */
#define LPX_MAX         121   /* maximization */
      gnm_float c0;
      /* constant term of the objective function ("shift") */
      int m_max;
      /* length of the array of rows (enlarged automatically) */
      int n_max;
      /* length of the array of columns (enlarged automatically) */
      int m;
      /* number of rows, 0 <= m <= m_max */
      int n;
      /* number of columns, 0 <= n <= n_max */
      LPXROW **row; /* LPXROW *row[1+m_max]; */
      /* row[0] is not used;
         row[i], 1 <= i <= m, is a pointer to i-th row */
      LPXCOL **col; /* LPXCOL *col[1+n_max]; */
      /* col[0] is not used;
         col[j], 1 <= j <= n, is a pointer to j-th column */
#if 1 /* 15/VIII-2004 */
      AVLTREE *r_tree;
      /* row index to find rows by their names; NULL means this index
         does not exist */
      AVLTREE *c_tree;
      /* column index to find columns by their names; NULL means this
         index does not exist */
#endif
      /*--------------------------------------------------------------*/
      /* LP basis */
      int b_stat;
      /* basis status: */
#define LPX_B_UNDEF     130   /* current basis is undefined */
#define LPX_B_VALID     131   /* current basis is valid */
      int *basis; /* int basis[1+m_max]; */
      /* basis header (valid only if the basis status is LPX_B_VALID):
         basis[0] is not used;
         basis[i] = k is the ordinal number of auxiliary (1 <= k <= m)
         or structural (m+1 <= k <= m+n) variable which corresponds to
         i-th basic variable xB[i], 1 <= i <= m */
      INV *b_inv; /* INV b_inv[1:m,1:m]; */
      /* factorization (invertable form) of the current basis matrix;
         NULL means the factorization does not exist; it is valid only
         if the basis status is LPX_B_VALID */
      /*--------------------------------------------------------------*/
      /* LP/MIP solution */
      int p_stat;
      /* status of primal basic solution: */
#define LPX_P_UNDEF     132   /* primal solution is undefined */
#define LPX_P_FEAS      133   /* solution is primal feasible */
#define LPX_P_INFEAS    134   /* solution is primal infeasible */
#define LPX_P_NOFEAS    135   /* no primal feasible solution exists */
      int d_stat;
      /* status of dual basic solution: */
#define LPX_D_UNDEF     136   /* dual solution is undefined */
#define LPX_D_FEAS      137   /* solution is dual feasible */
#define LPX_D_INFEAS    138   /* solution is dual infeasible */
#define LPX_D_NOFEAS    139   /* no dual feasible solution exists */
      int some;
      /* ordinal number of some auxiliary or structural variable which
         has certain property, 0 <= some <= m+n */
      int t_stat;
      /* status of interior-point solution: */
#define LPX_T_UNDEF     150   /* interior solution is undefined */
#define LPX_T_OPT       151   /* interior solution is optimal */
      int i_stat;
      /* status of integer solution: */
#define LPX_I_UNDEF     170   /* integer solution is undefined */
#define LPX_I_OPT       171   /* integer solution is optimal */
#define LPX_I_FEAS      172   /* integer solution is feasible */
#define LPX_I_NOFEAS    173   /* no integer solution exists */
      /*--------------------------------------------------------------*/
      /* control parameters and statistics */
      int msg_lev;
      /* level of messages output by the solver:
         0 - no output
         1 - error messages only
         2 - normal output
         3 - full output (includes informational messages) */
      int scale;
      /* scaling option:
         0 - no scaling
         1 - equilibration scaling
         2 - geometric mean scaling
         3 - geometric mean scaling, then equilibration scaling */
      int dual;
      /* dual simplex option:
         0 - do not use the dual simplex
         1 - if the initial basic solution being primal infeasible is
             dual feasible, use the dual simplex */
      int price;
      /* pricing option (for both primal and dual simplex):
         0 - textbook pricing
         1 - steepest edge pricing */
      gnm_float relax;
      /* relaxation parameter used in the ratio test; if it is zero,
         the textbook ratio test is used; if it is non-zero (should be
         positive), Harris' two-pass ratio test is used; in the latter
         case on the first pass basic variables (in the case of primal
         simplex) or reduced costs of non-basic variables (in the case
         of dual simplex) are allowed to slightly violate their bounds,
         but not more than (relax * tol_bnd) or (relax * tol_dj) (thus,
         relax is a percentage of tol_bnd or tol_dj) */
      gnm_float tol_bnd;
      /* relative tolerance used to check if the current basic solution
         is primal feasible */
      gnm_float tol_dj;
      /* absolute tolerance used to check if the current basic solution
         is dual feasible */
      gnm_float tol_piv;
      /* relative tolerance used to choose eligible pivotal elements of
         the simplex table in the ratio test */
      int round;
      /* solution rounding option:
         0 - report all computed values and reduced costs "as is"
         1 - if possible (allowed by the tolerances), replace computed
             values and reduced costs which are close to zero by exact
             zeros */
      gnm_float obj_ll;
      /* lower limit of the objective function; if on the phase II the
         objective function reaches this limit and continues decreasing,
         the solver stops the search */
      gnm_float obj_ul;
      /* upper limit of the objective function; if on the phase II the
         objective function reaches this limit and continues increasing,
         the solver stops the search */
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
      int out_frq;
      /* output frequency, in iterations; this parameter specifies how
         frequently the solver sends information about the solution to
         the standard output */
      gnm_float out_dly;
      /* output delay, in seconds; this parameter specifies how long
         the solver should delay sending information about the solution
         to the standard output; zero value means no delay */
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
      gnm_float tol_int; /* MIP */
      /* absolute tolerance used to check if the current basic solution
         is integer feasible */
      gnm_float tol_obj; /* MIP */
      /* relative tolerance used to check if the value of the objective
         function is not better than in the best known integer feasible
         solution */
      int mps_info; /* lpx_write_mps */
      /* if this flag is set, the routine lpx_write_mps outputs several
         comment cards that contains some information about the problem;
         otherwise the routine outputs no comment cards */
      int mps_obj; /* lpx_write_mps */
      /* this parameter tells the routine lpx_write_mps how to output
         the objective function row:
         0 - never output objective function row
         1 - always output objective function row
         2 - output objective function row if and only if the problem
             has no free rows */
      int mps_orig; /* lpx_write_mps */
      /* if this flag is set, the routine lpx_write_mps uses original
         row and column symbolic names; otherwise the routine generates
         plain names using ordinal numbers of rows and columns */
      int mps_wide; /* lpx_write_mps */
      /* if this flag is set, the routine lpx_write_mps uses all data
         fields; otherwise the routine keeps fields 5 and 6 empty */
      int mps_free; /* lpx_write_mps */
      /* if this flag is set, the routine lpx_write_mps omits column
         and vector names everytime if possible (free style); otherwise
         the routine never omits these names (pedantic style) */
      int mps_skip; /* lpx_write_mps */
      /* if this flag is set, the routine lpx_write_mps skips empty
         columns (i.e. which has no constraint coefficients); otherwise
         the routine outputs all columns */
      int lpt_orig; /* lpx_write_lpt */
      /* if this flag is set, the routine lpx_write_lpt uses original
         row and column symbolic names; otherwise the routine generates
         plain names using ordinal numbers of rows and columns */
      int presol; /* lpx_simplex */
      /* LP presolver option:
         0 - do not use LP presolver
         1 - use LP presolver */
#if 1 /* 07/I-2006 */
      int binarize; /* lpx_intopt */
      /* if this flag is set, the routine lpx_intopt replaces integer
         columns by binary ones */
      int use_cuts; /* lpx_intopt */
      /* if this flag is set, the routine lpx_intopt tries generating
         cutting planes */
#endif
};

struct LPXROW
{     /* LP row (auxiliary variable) */
      int i;
      /* ordinal number (1 to m) assigned to this row */
      STR *name;
      /* row name (1 to 255 chars); NULL means no name is assigned to
         this row */
#if 1 /* 15/VIII-2004 */
      AVLNODE *node;
      /* pointer to corresponding node in the row index; NULL means
         that either the row index does not exist or this row has no
         name assigned */
#endif
      int type;
      /* type of the auxiliary variable: */
#define LPX_FR          110   /* free variable */
#define LPX_LO          111   /* variable with lower bound */
#define LPX_UP          112   /* variable with upper bound */
#define LPX_DB          113   /* gnm_float-bounded variable */
#define LPX_FX          114   /* fixed variable */
      gnm_float lb; /* non-scaled */
      /* lower bound; if the row has no lower bound, lb is zero */
      gnm_float ub; /* non-scaled */
      /* upper bound; if the row has no upper bound, ub is zero */
      /* if the row type is LPX_FX, ub is equal to lb */
      LPXAIJ *ptr; /* non-scaled */
      /* pointer to doubly linked list of constraint coefficients which
         are placed in this row */
      gnm_float rii;
      /* diagonal element r[i,i] of the scaling matrix R (see (17)) for
         this row; if the scaling is not used, r[i,i] is 1 */
      int stat;
      /* status of the auxiliary variable: */
#define LPX_BS          140   /* basic variable */
#define LPX_NL          141   /* non-basic variable on lower bound */
#define LPX_NU          142   /* non-basic variable on upper bound */
#define LPX_NF          143   /* non-basic free variable */
#define LPX_NS          144   /* non-basic fixed variable */
      int b_ind;
      /* if the auxiliary variable is basic (LPX_BS), lpx.basis[b_ind]
         refers to this row; if the auxiliary variable is non-basic,
         b_ind is 0; this attribute is valid only if the basis status
         is LPX_B_VALID */
      gnm_float prim; /* non-scaled */
      /* primal value of the auxiliary variable in basic solution */
      gnm_float dual; /* non-scaled */
      /* dual value of the auxiliary variable in basic solution */
      gnm_float pval; /* non-scaled */
      /* primal value of the auxiliary variable in interior solution */
      gnm_float dval; /* non-scaled */
      /* dual value of the auxiliary variable in interior solution */
      gnm_float mipx; /* non-scaled */
      /* primal value of the auxiliary variable in integer solution */
};

struct LPXCOL
{     /* LP column (structural variable) */
      int j;
      /* ordinal number (1 to n) assigned to this column */
      STR *name;
      /* column name (1 to 255 chars); NULL means no name is assigned
         to this column */
#if 1 /* 15/VIII-2004 */
      AVLNODE *node;
      /* pointer to corresponding node in the column index; NULL means
         that either the column index does not exist or the column has
         no name assigned */
#endif
      int kind;
      /* kind of the structural variable: */
#define LPX_CV          160   /* continuous variable */
#define LPX_IV          161   /* integer variable */
      int type;
      /* type of the structural variable: */
#define LPX_FR          110   /* free variable */
#define LPX_LO          111   /* variable with lower bound */
#define LPX_UP          112   /* variable with upper bound */
#define LPX_DB          113   /* gnm_float-bounded variable */
#define LPX_FX          114   /* fixed variable */
      gnm_float lb; /* non-scaled */
      /* lower bound; if the column has no lower bound, lb is zero */
      gnm_float ub; /* non-scaled */
      /* upper bound; if the column has no upper bound, ub is zero */
      /* if the column type is LPX_FX, ub is equal to lb */
      gnm_float coef; /* non-scaled */
      /* objective coefficient at the structural variable */
      LPXAIJ *ptr; /* non-scaled */
      /* pointer to doubly linked list of constraint coefficients which
         are placed in this column */
      gnm_float sjj;
      /* diagonal element s[j,j] of the scaling matrix S (see (17)) for
         this column; if the scaling is not used, s[j,j] is 1 */
      int stat;
      /* status of the structural variable: */
#define LPX_BS          140   /* basic variable */
#define LPX_NL          141   /* non-basic variable on lower bound */
#define LPX_NU          142   /* non-basic variable on upper bound */
#define LPX_NF          143   /* non-basic free variable */
#define LPX_NS          144   /* non-basic fixed variable */
      int b_ind;
      /* if the structural variable is basic (LPX_BS), lpx.basis[b_ind]
         refers to this column; if the structural variable is non-basic,
         b_ind is 0; this attribute is valid only if the basis status
         is LPX_B_VALID */
      gnm_float prim; /* non-scaled */
      /* primal value of the structural variable in basic solution */
      gnm_float dual; /* non-scaled */
      /* dual value of the structural variable in basic solution */
      gnm_float pval; /* non-scaled */
      /* primal value of the structural variable in interior solution */
      gnm_float dval; /* non-scaled */
      /* dual value of the structural variable in interior solution */
      gnm_float mipx;
      /* primal value of the structural variable in integer solution */
};

struct LPXAIJ
{     /* constraint coefficient a[i,j]; see (2) and (5) */
      LPXROW *row;
      /* pointer to row, where this coefficient is placed */
      LPXCOL *col;
      /* pointer to column, where this coefficient is placed */
      gnm_float val;
      /* numeric (non-zero) value of this coefficient */
      LPXAIJ *r_prev;
      /* pointer to previous coefficient in the same row */
      LPXAIJ *r_next;
      /* pointer to next coefficient in the same row */
      LPXAIJ *c_prev;
      /* pointer to previous coefficient in the same column */
      LPXAIJ *c_next;
      /* pointer to next coefficient in the same column */
};

/* status codes reported by the routine lpx_get_status: */
#define LPX_OPT         180   /* optimal */
#define LPX_FEAS        181   /* feasible */
#define LPX_INFEAS      182   /* infeasible */
#define LPX_NOFEAS      183   /* no feasible */
#define LPX_UNBND       184   /* unbounded */
#define LPX_UNDEF       185   /* undefined */

/* exit codes returned by solver routines: */
#define LPX_E_OK        200   /* success */
#define LPX_E_EMPTY     201   /* empty problem */
#define LPX_E_BADB      202   /* invalid initial basis */
#define LPX_E_INFEAS    203   /* infeasible initial solution */
#define LPX_E_FAULT     204   /* unable to start the search */
#define LPX_E_OBJLL     205   /* objective lower limit reached */
#define LPX_E_OBJUL     206   /* objective upper limit reached */
#define LPX_E_ITLIM     207   /* iterations limit exhausted */
#define LPX_E_TMLIM     208   /* time limit exhausted */
#define LPX_E_NOFEAS    209   /* no feasible solution */
#define LPX_E_INSTAB    210   /* numerical instability */
#define LPX_E_SING      211   /* problems with basis matrix */
#define LPX_E_NOCONV    212   /* no convergence (interior) */
#define LPX_E_NOPFS     213   /* no primal feas. sol. (LP presolver) */
#define LPX_E_NODFS     214   /* no dual feas. sol. (LP presolver) */

/* control parameter identifiers: */
#define LPX_K_MSGLEV    300   /* lp->msg_lev */
#define LPX_K_SCALE     301   /* lp->scale */
#define LPX_K_DUAL      302   /* lp->dual */
#define LPX_K_PRICE     303   /* lp->price */
#define LPX_K_RELAX     304   /* lp->relax */
#define LPX_K_TOLBND    305   /* lp->tol_bnd */
#define LPX_K_TOLDJ     306   /* lp->tol_dj */
#define LPX_K_TOLPIV    307   /* lp->tol_piv */
#define LPX_K_ROUND     308   /* lp->round */
#define LPX_K_OBJLL     309   /* lp->obj_ll */
#define LPX_K_OBJUL     310   /* lp->obj_ul */
#define LPX_K_ITLIM     311   /* lp->it_lim */
#define LPX_K_ITCNT     312   /* lp->it_cnt */
#define LPX_K_TMLIM     313   /* lp->tm_lim */
#define LPX_K_OUTFRQ    314   /* lp->out_frq */
#define LPX_K_OUTDLY    315   /* lp->out_dly */
#define LPX_K_BRANCH    316   /* lp->branch */
#define LPX_K_BTRACK    317   /* lp->btrack */
#define LPX_K_TOLINT    318   /* lp->tol_int */
#define LPX_K_TOLOBJ    319   /* lp->tol_obj */
#define LPX_K_MPSINFO   320   /* lp->mps_info */
#define LPX_K_MPSOBJ    321   /* lp->mps_obj */
#define LPX_K_MPSORIG   322   /* lp->mps_orig */
#define LPX_K_MPSWIDE   323   /* lp->mps_wide */
#define LPX_K_MPSFREE   324   /* lp->mps_free */
#define LPX_K_MPSSKIP   325   /* lp->mps_skip */
#define LPX_K_LPTORIG   326   /* lp->lpt_orig */
#define LPX_K_PRESOL    327   /* lp->presol */
#define LPX_K_BINARIZE  328   /* lp->binarize */
#define LPX_K_USECUTS   329   /* lp->use_cuts */

typedef struct LPXKKT LPXKKT;

struct LPXKKT
{     /* this structure contains results reported by the routines which
         checks Karush-Kuhn-Tucker conditions (for details see comments
         to those routines) */
      /*--------------------------------------------------------------*/
      /* xR - A * xS = 0 (KKT.PE) */
      gnm_float pe_ae_max;
      /* largest absolute error */
      int    pe_ae_row;
      /* number of row with largest absolute error */
      gnm_float pe_re_max;
      /* largest relative error */
      int    pe_re_row;
      /* number of row with largest relative error */
      int    pe_quality;
      /* quality of primal solution:
         'H' - high
         'M' - medium
         'L' - low
         '?' - primal solution is wrong */
      /*--------------------------------------------------------------*/
      /* l[k] <= x[k] <= u[k] (KKT.PB) */
      gnm_float pb_ae_max;
      /* largest absolute error */
      int    pb_ae_ind;
      /* number of variable with largest absolute error */
      gnm_float pb_re_max;
      /* largest relative error */
      int    pb_re_ind;
      /* number of variable with largest relative error */
      int    pb_quality;
      /* quality of primal feasibility:
         'H' - high
         'M' - medium
         'L' - low
         '?' - primal solution is infeasible */
      /*--------------------------------------------------------------*/
      /* A' * (dR - cR) + (dS - cS) = 0 (KKT.DE) */
      gnm_float de_ae_max;
      /* largest absolute error */
      int    de_ae_col;
      /* number of column with largest absolute error */
      gnm_float de_re_max;
      /* largest relative error */
      int    de_re_col;
      /* number of column with largest relative error */
      int    de_quality;
      /* quality of dual solution:
         'H' - high
         'M' - medium
         'L' - low
         '?' - dual solution is wrong */
      /*--------------------------------------------------------------*/
      /* d[k] >= 0 or d[k] <= 0 (KKT.DB) */
      gnm_float db_ae_max;
      /* largest absolute error */
      int    db_ae_ind;
      /* number of variable with largest absolute error */
      gnm_float db_re_max;
      /* largest relative error */
      int    db_re_ind;
      /* number of variable with largest relative error */
      int    db_quality;
      /* quality of dual feasibility:
         'H' - high
         'M' - medium
         'L' - low
         '?' - dual solution is infeasible */
      /*--------------------------------------------------------------*/
      /* (x[k] - bound of x[k]) * d[k] = 0 (KKT.CS) */
      gnm_float cs_ae_max;
      /* largest absolute error */
      int    cs_ae_ind;
      /* number of variable with largest absolute error */
      gnm_float cs_re_max;
      /* largest relative error */
      int    cs_re_ind;
      /* number of variable with largest relative error */
      int    cs_quality;
      /* quality of complementary slackness:
         'H' - high
         'M' - medium
         'L' - low
         '?' - primal and dual solutions are not complementary */
};

/* problem creating and modifying routines ---------------------------*/

LPX *lpx_create_prob(void);
/* create problem object */

void lpx_set_prob_name(LPX *lp, char *name);
/* assign (change) problem name */

void lpx_set_class(LPX *lp, int klass);
/* set (change) problem class */

void lpx_set_obj_name(LPX *lp, char *name);
/* assign (change) objective function name */

void lpx_set_obj_dir(LPX *lp, int dir);
/* set (change) optimization direction flag */

int lpx_add_rows(LPX *lp, int nrs);
/* add new rows to problem object */

int lpx_add_cols(LPX *lp, int ncs);
/* add new columns to problem object */

void lpx_set_row_name(LPX *lp, int i, char *name);
/* assign (change) row name */

void lpx_set_col_name(LPX *lp, int j, char *name);
/* assign (change) column name */

void lpx_set_col_kind(LPX *lp, int j, int kind);
/* set (change) column kind */

void lpx_set_row_bnds(LPX *lp, int i, int type, gnm_float lb, gnm_float ub);
/* set (change) row bounds */

void lpx_set_col_bnds(LPX *lp, int j, int type, gnm_float lb, gnm_float ub);
/* set (change) column bounds */

void lpx_set_obj_coef(LPX *lp, int j, gnm_float coef);
/* set (change) obj. coefficient or constant term */

void lpx_set_mat_row(LPX *lp, int i, int len, int ind[], gnm_float val[]);
/* set (replace) row of the constraint matrix */

void lpx_set_mat_col(LPX *lp, int j, int len, int ind[], gnm_float val[]);
/* set (replace) column of the constraint matrix */

void lpx_load_matrix(LPX *lp, int ne, int ia[], int ja[], gnm_float ar[]);
/* load (replace) the whole constraint matrix */

void lpx_order_matrix(LPX *lp);
/* order rows and columns of the constraint matrix */

void lpx_set_rii(LPX *lp, int i, gnm_float rii);
/* set (change) row scale factor */

void lpx_set_sjj(LPX *lp, int j, gnm_float sjj);
/* set (change) column scale factor */

void lpx_set_row_stat(LPX *lp, int i, int stat);
/* set (change) row status */

void lpx_set_col_stat(LPX *lp, int j, int stat);
/* set (change) column status */

void lpx_del_rows(LPX *lp, int nrs, int num[]);
/* delete specified rows from problem object */

void lpx_del_cols(LPX *lp, int ncs, int num[]);
/* delete specified columns from problem object */

void lpx_delete_prob(LPX *lp);
/* delete problem object */

#if 1 /* 15/VIII-2004 */
void lpx_create_index(LPX *lp);
int lpx_find_row(LPX *lp, char *name);
int lpx_find_col(LPX *lp, char *name);
void lpx_delete_index(LPX *lp);
#endif

void lpx_put_lp_basis(LPX *lp, int b_stat, int basis[], INV *b_inv);
/* store LP basis information */

void lpx_put_solution(LPX *lp, int p_stat, int d_stat,
      int row_stat[], gnm_float row_prim[], gnm_float row_dual[],
      int col_stat[], gnm_float col_prim[], gnm_float col_dual[]);
/* store basic solution components */

void lpx_put_ray_info(LPX *lp, int k);
/* store row/column which causes unboundness */

void lpx_put_ipt_soln(LPX *lp, int t_stat, gnm_float row_pval[],
      gnm_float row_dval[], gnm_float col_pval[], gnm_float col_dval[]);
/* store interior-point solution components */

void lpx_put_mip_soln(LPX *lp, int i_stat, gnm_float row_mipx[],
      gnm_float col_mipx[]);
/* store mixed integer solution components */

/* problem retrieving routines ---------------------------------------*/

char *lpx_get_prob_name(LPX *lp);
/* retrieve problem name */

int lpx_get_class(LPX *lp);
/* retrieve problem class */

char *lpx_get_obj_name(LPX *lp);
/* retrieve objective function name */

int lpx_get_obj_dir(LPX *lp);
/* retrieve optimization direction flag */

int lpx_get_num_rows(LPX *lp);
/* retrieve number of rows */

int lpx_get_num_cols(LPX *lp);
/* retrieve number of columns */

int lpx_get_num_int(LPX *lp);
/* retrieve number of integer columns */

int lpx_get_num_bin(LPX *lp);
/* retrieve number of binary columns */

char *lpx_get_row_name(LPX *lp, int i);
/* retrieve row name */

char *lpx_get_col_name(LPX *lp, int j);
/* retrieve column name */

int lpx_get_col_kind(LPX *lp, int j);
/* retrieve column kind */

int lpx_get_row_type(LPX *lp, int i);
/* retrieve row type */

gnm_float lpx_get_row_lb(LPX *lp, int i);
/* retrieve row lower bound */

gnm_float lpx_get_row_ub(LPX *lp, int i);
/* retrieve row upper bound */

int lpx_get_col_type(LPX *lp, int j);
/* retrieve column type */

gnm_float lpx_get_col_lb(LPX *lp, int j);
/* retrieve column lower bound */

gnm_float lpx_get_col_ub(LPX *lp, int j);
/* retrieve column upper bound */

gnm_float lpx_get_obj_coef(LPX *lp, int j);
/* retrieve obj. coefficient or constant term */

int lpx_get_num_nz(LPX *lp);
/* retrieve number of constraint coefficients */

int lpx_get_mat_row(LPX *lp, int i, int ind[], gnm_float val[]);
/* retrieve row of the constraint matrix */

int lpx_get_mat_col(LPX *lp, int j, int ind[], gnm_float val[]);
/* retrieve column of the constraint matrix */

gnm_float lpx_get_rii(LPX *lp, int i);
/* retrieve row scale factor */

gnm_float lpx_get_sjj(LPX *lp, int j);
/* retrieve column scale factor */

int lpx_is_b_avail(LPX *lp);
/* check if LP basis is available */

int lpx_get_b_info(LPX *lp, int i);
/* retrieve LP basis information */

int lpx_get_row_b_ind(LPX *lp, int i);
/* retrieve row index in LP basis */

int lpx_get_col_b_ind(LPX *lp, int j);
/* retrieve column index in LP basis */

INV *lpx_access_inv(LPX *lp);
/* access factorization of basis matrix */

int lpx_get_status(LPX *lp);
/* retrieve generic status of basic solution */

int lpx_get_prim_stat(LPX *lp);
/* retrieve primal status of basic solution */

int lpx_get_dual_stat(LPX *lp);
/* retrieve dual status of basic solution */

gnm_float lpx_get_obj_val(LPX *lp);
/* retrieve objective value (basic solution) */

int lpx_get_row_stat(LPX *lp, int i);
/* retrieve row status (basic solution) */

gnm_float lpx_get_row_prim(LPX *lp, int i);
/* retrieve row primal value (basic solution) */

gnm_float lpx_get_row_dual(LPX *lp, int i);
/* retrieve row dual value (basic solution) */

int lpx_get_col_stat(LPX *lp, int j);
/* retrieve column status (basic solution) */

gnm_float lpx_get_col_prim(LPX *lp, int j);
/* retrieve column primal value (basic solution) */

gnm_float lpx_get_col_dual(LPX *lp, int j);
/* retrieve column dual value (basic solution) */

int lpx_get_ray_info(LPX *lp);
/* determine what causes primal unboundness */

int lpx_ipt_status(LPX *lp);
/* retrieve status of interior-point solution */

gnm_float lpx_ipt_obj_val(LPX *lp);
/* retrieve objective value (interior point) */

gnm_float lpx_ipt_row_prim(LPX *lp, int i);
/* retrieve row primal value (interior point) */

gnm_float lpx_ipt_row_dual(LPX *lp, int i);
/* retrieve row dual value (interior point) */

gnm_float lpx_ipt_col_prim(LPX *lp, int j);
/* retrieve column primal value (interior point) */

gnm_float lpx_ipt_col_dual(LPX *lp, int j);
/* retrieve column dual value (interior point) */

int lpx_mip_status(LPX *lp);
/* retrieve status of MIP solution */

gnm_float lpx_mip_obj_val(LPX *lp);
/* retrieve objective value (MIP solution) */

gnm_float lpx_mip_row_val(LPX *lp, int i);
/* retrieve row value (MIP solution) */

gnm_float lpx_mip_col_val(LPX *lp, int j);
/* retrieve column value (MIP solution) */

void lpx_get_row_bnds(LPX *lp, int i, int *typx, gnm_float *lb,
      gnm_float *ub);
/* obtain row bounds */

void lpx_get_col_bnds(LPX *lp, int j, int *typx, gnm_float *lb,
      gnm_float *ub);
/* obtain column bounds */

void lpx_get_row_info(LPX *lp, int i, int *tagx, gnm_float *vx,
      gnm_float *dx);
/* obtain row solution information */

void lpx_get_col_info(LPX *lp, int j, int *tagx, gnm_float *vx,
      gnm_float *dx);
/* obtain column solution information */

/* control parameters and statistics routines ------------------------*/

void lpx_reset_parms(LPX *lp);
/* reset control parameters to default values */

void lpx_set_int_parm(LPX *lp, int parm, int val);
/* set (change) integer control parameter */

int lpx_get_int_parm(LPX *lp, int parm);
/* query integer control parameter */

void lpx_set_real_parm(LPX *lp, int parm, gnm_float val);
/* set (change) real control parameter */

gnm_float lpx_get_real_parm(LPX *lp, int parm);
/* query real control parameter */

/* problem scaling routines ------------------------------------------*/

void lpx_scale_prob(LPX *lp);
/* scale problem data */

void lpx_unscale_prob(LPX *lp);
/* unscale problem data */

/* LP basis constructing routines ------------------------------------*/

void lpx_std_basis(LPX *lp);
/* construct standard initial LP basis */

void lpx_adv_basis(LPX *lp);
/* construct advanced initial LP basis */

/* solver routines ---------------------------------------------------*/

int lpx_simplex(LPX *lp);
/* easy-to-use driver to the simplex method */

void lpx_check_kkt(LPX *lp, int scaled, LPXKKT *kkt);
/* check Karush-Kuhn-Tucker conditions */

int lpx_interior(LPX *lp);
/* easy-to-use driver to the interior point method */

int lpx_integer(LPX *lp);
/* easy-to-use driver to the branch-and-bound method */

void lpx_check_int(LPX *lp, LPXKKT *kkt);
/* check integer feasibility conditions */

int lpx_intopt(LPX *mip);
/* easy-to-use driver to the branch-and-bound method */

/* LP basis and simplex table routines -------------------------------*/

int lpx_invert(LPX *lp);
/* compute factorization of basis matrix */

void lpx_ftran(LPX *lp, gnm_float x[]);
/* forward transformation (solve system B*x = b) */

void lpx_btran(LPX *lp, gnm_float x[]);
/* backward transformation (solve system B'*x = b) */

void lpx_eval_b_prim(LPX *lp, gnm_float row_prim[], gnm_float col_prim[]);
/* compute primal basic solution components */

void lpx_eval_b_dual(LPX *lp, gnm_float row_dual[], gnm_float col_dual[]);
/* compute dual basic solution components */

int lpx_warm_up(LPX *lp);
/* "warm up" LP basis */

int lpx_eval_tab_row(LPX *lp, int k, int ind[], gnm_float val[]);
/* compute row of the simplex table */

int lpx_eval_tab_col(LPX *lp, int k, int ind[], gnm_float val[]);
/* compute column of the simplex table */

int lpx_transform_row(LPX *lp, int len, int ind[], gnm_float val[]);
/* transform explicitly specified row */

int lpx_transform_col(LPX *lp, int len, int ind[], gnm_float val[]);
/* transform explicitly specified column */

int lpx_prim_ratio_test(LPX *lp, int len, int ind[], gnm_float val[],
      int how, gnm_float tol);
/* perform primal ratio test */

int lpx_dual_ratio_test(LPX *lp, int len, int ind[], gnm_float val[],
      int how, gnm_float tol);
/* perform dual ratio test */

/*--------------------------------------------------------------------*/

int lpx_remove_tiny(int ne, int ia[], int ja[], gnm_float ar[],
      gnm_float eps);
/* remove zero and tiny elements */

int lpx_reduce_form(LPX *lp, int len, int ind[], gnm_float val[],
      gnm_float work[]);
/* reduce linear form */

gnm_float lpx_eval_row(LPX *lp, int len, int ind[], gnm_float val[]);
/* compute explicitly specified row */

gnm_float lpx_eval_degrad(LPX *lp, int len, int ind[], gnm_float val[],
      int type, gnm_float rhs);
/* compute degradation of the objective function */

int lpx_gomory_cut(LPX *lp, int len, int ind[], gnm_float val[],
      gnm_float work[]);
/* generate Gomory's mixed integer cut */

/* additional utility routines ---------------------------------------*/

LPX *lpx_read_mps(char *fname);
/* read problem data in fixed MPS format */

int lpx_write_mps(LPX *lp, char *fname);
/* write problem data in fixed MPS format */

int lpx_read_bas(LPX *lp, char *fname);
/* read LP basis in fixed MPS format */

int lpx_write_bas(LPX *lp, char *fname);
/* write LP basis in fixed MPS format */

LPX *lpx_read_freemps(char *fname);
/* read problem data in free MPS format */

int lpx_write_freemps(LPX *lp, char *fname);
/* write problem data in free MPS format */

int lpx_print_prob(LPX *lp, char *fname);
/* write problem data in plain text format */

int lpx_print_sol(LPX *lp, char *fname);
/* write LP problem solution in printable format */

int lpx_print_ips(LPX *lp, char *fname);
/* write interior point solution in printable format */

int lpx_print_mip(LPX *lp, char *fname);
/* write MIP problem solution in printable format */

int lpx_print_sens_bnds(LPX *lp, char *fname);
/* write bounds sensitivity information */

LPX *lpx_read_cpxlp(char *fname);
/* read problem data in CPLEX LP format */

int lpx_write_cpxlp(LPX *lp, char *fname);
/* write problem data in CPLEX LP format */

LPX *lpx_extract_prob(void *mpl);
/* extract problem instance from MathProg model */

LPX *lpx_read_model(char *model, char *data, char *output);
/* read LP/MIP model written in GNU MathProg language */

LPX *lpx_read_prob(char *fname);
/* read problem data in GNU LP format */

int lpx_write_prob(LPX *lp, char *fname);
/* write problem data in GNU LP format */

#endif

/* eof */
