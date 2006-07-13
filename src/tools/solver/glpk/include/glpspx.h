/* glpspx.h (simplex method) */

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

#ifndef _GLPSPX_H
#define _GLPSPX_H

#include "gnumeric-config.h"
#include "gnumeric.h"
#include "numbers.h"

#include "glplpx.h"

#define spx_invert            glp_spx_invert
#define spx_ftran             glp_spx_ftran
#define spx_btran             glp_spx_btran
#define spx_update            glp_spx_update
#define spx_eval_xn_j         glp_spx_eval_xn_j
#define spx_eval_bbar         glp_spx_eval_bbar
#define spx_eval_pi           glp_spx_eval_pi
#define spx_eval_cbar         glp_spx_eval_cbar
#define spx_eval_obj          glp_spx_eval_obj
#define spx_eval_col          glp_spx_eval_col
#define spx_eval_rho          glp_spx_eval_rho
#define spx_eval_row          glp_spx_eval_row
#define spx_check_bbar        glp_spx_check_bbar
#define spx_check_cbar        glp_spx_check_cbar
#define spx_prim_chuzc        glp_spx_prim_chuzc
#define spx_prim_chuzr        glp_spx_prim_chuzr
#define spx_dual_chuzr        glp_spx_dual_chuzr
#define spx_dual_chuzc        glp_spx_dual_chuzc
#define spx_update_bbar       glp_spx_update_bbar
#define spx_update_pi         glp_spx_update_pi
#define spx_update_cbar       glp_spx_update_cbar
#define spx_change_basis      glp_spx_change_basis
#define spx_err_in_bbar       glp_spx_err_in_bbar
#define spx_err_in_pi         glp_spx_err_in_pi
#define spx_err_in_cbar       glp_spx_err_in_cbar
#define spx_reset_refsp       glp_spx_reset_refsp
#define spx_update_gvec       glp_spx_update_gvec
#define spx_err_in_gvec       glp_spx_err_in_gvec
#define spx_update_dvec       glp_spx_update_dvec
#define spx_err_in_dvec       glp_spx_err_in_dvec

#define spx_warm_up           glp_spx_warm_up
#define spx_prim_opt          glp_spx_prim_opt
#define spx_prim_feas         glp_spx_prim_feas
#define spx_dual_opt          glp_spx_dual_opt
#define spx_simplex           glp_spx_simplex

typedef struct SPX SPX;

struct SPX
{     /* data block used by simplex method routines */
      /*--------------------------------------------------------------*/
      /* LP problem data */
      int m;
      /* number of rows (auxiliary variables) */
      int n;
      /* number of columns (structural variables) */
      int *typx; /* int typx[1+m+n]; */
      /* typx[0] is not used;
         typx[k], 1 <= k <= m+n, is the type of the variable x[k]: */
#define LPX_FR          110   /* free variable:  -inf <  x[k] < +inf  */
#define LPX_LO          111   /* lower bound:    l[k] <= x[k] < +inf  */
#define LPX_UP          112   /* upper bound:    -inf <  x[k] <= u[k] */
#define LPX_DB          113   /* gnm_float bound:   l[k] <= x[k] <= u[k] */
#define LPX_FX          114   /* fixed variable: l[k]  = x[k]  = u[k] */
      gnm_float *lb; /* gnm_float lb[1+m+n]; */
      /* lb[0] is not used;
         lb[k], 1 <= k <= m+n, is an lower bound of the variable x[k];
         if x[k] has no lower bound, lb[k] is zero */
      gnm_float *ub; /* gnm_float ub[1+m+n]; */
      /* ub[0] is not used;
         ub[k], 1 <= k <= m+n, is an upper bound of the variable x[k];
         if x[k] has no upper bound, ub[k] is zero; if x[k] is of fixed
         type, ub[k] is equal to lb[k] */
      int dir;
      /* optimization direction (sense of the objective function): */
#define LPX_MIN         120   /* minimization */
#define LPX_MAX         121   /* maximization */
      gnm_float *coef; /* gnm_float coef[1+m+n]; */
      /* coef[0] is a constant term of the objective function;
         coef[k], 1 <= k <= m+n, is a coefficient of the objective
         function at the variable x[k] (note that auxiliary variables
         also may have non-zero objective coefficients) */
      /*--------------------------------------------------------------*/
      /* constraint matrix (has m rows and n columns) */
      int *A_ptr; /* int A_ptr[1+m+1]; */
      int *A_ind; /* int A_ind[A_ptr[m+1]]; */
      gnm_float *A_val; /* gnm_float A_val[A_ptr[m+1]]; */
      /* constraint matrix in storage-by-rows format */
      int *AT_ptr; /* int AT_ptr[1+n+1]; */
      int *AT_ind; /* int AT_ind[AT_ptr[n+1]]; */
      gnm_float *AT_val; /* gnm_float AT_val[AT_ptr[n+1]]; */
      /* constraint matrix in storage-by-columns format */
      /*--------------------------------------------------------------*/
      /* basic solution */
      int b_stat;
      /* status of the current basis: */
#define LPX_B_UNDEF     130   /* current basis is undefined */
#define LPX_B_VALID     131   /* current basis is valid */
      int p_stat;
      /* status of the primal solution: */
#define LPX_P_UNDEF     132   /* primal status is undefined */
#define LPX_P_FEAS      133   /* solution is primal feasible */
#define LPX_P_INFEAS    134   /* solution is primal infeasible */
#define LPX_P_NOFEAS    135   /* no primal feasible solution exists */
      int d_stat;
      /* status of the dual solution: */
#define LPX_D_UNDEF     136   /* dual status is undefined */
#define LPX_D_FEAS      137   /* solution is dual feasible */
#define LPX_D_INFEAS    138   /* solution is dual infeasible */
#define LPX_D_NOFEAS    139   /* no dual feasible solution exists */
      int *tagx; /* int tagx[1+m+n]; */
      /* tagx[0] is not used;
         tagx[k], 1 <= k <= m+n, is the status of the variable x[k]
         (contents of this array is always defined independently on the
         status of the current basis): */
#define LPX_BS          140   /* basic variable */
#define LPX_NL          141   /* non-basic variable on lower bound */
#define LPX_NU          142   /* non-basic variable on upper bound */
#define LPX_NF          143   /* non-basic free variable */
#define LPX_NS          144   /* non-basic fixed variable */
      int *posx; /* int posx[1+m+n]; */
      /* posx[0] is not used;
         posx[k], 1 <= k <= m+n, is the position of the variable x[k]
         in the vector of basic variables xB or non-basic variables xN:
         posx[k] = i   means that x[k] = xB[i], 1 <= i <= m
         posx[k] = m+j means that x[k] = xN[j], 1 <= j <= n
         (if the current basis is undefined, contents of this array is
         undefined) */
      int *indx; /* int indx[1+m+n]; */
      /* indx[0] is not used;
         indx[i], 1 <= i <= m, is the original number of the basic
         variable xB[i], i.e. indx[i] = k means that posx[k] = i
         indx[m+j], 1 <= j <= n, is the original number of the non-basic
         variable xN[j], i.e. indx[m+j] = k means that posx[k] = m+j
         (if the current basis is undefined, contents of this array is
         undefined) */
      INV *inv; /* INV inv[1:m,1:m]; */
      /* an invertable (factorized) form of the current basis matrix */
      gnm_float *bbar; /* gnm_float bbar[1+m]; */
      /* bbar[0] is not used;
         bbar[i], 1 <= i <= m, is a value of basic variable xB[i] */
      gnm_float *pi; /* gnm_float pi[1+m]; */
      /* pi[0] is not used;
         pi[i], 1 <= i <= m, is a simplex (Lagrange) multiplier, which
         corresponds to the i-th row (equality constraint) */
      gnm_float *cbar; /* gnm_float cbar[1+n]; */
      /* cbar[0] is not used;
         cbar[j], 1 <= j <= n, is a reduced cost of non-basic variable
         xN[j] */
      int some;
      /* ordinal number of some auxiliary or structural variable which
         has certain property, 1 <= some <= m+n */
      /*--------------------------------------------------------------*/
      /* control parameters and statistics */
      int msg_lev;
      /* level of messages output by the solver:
         0 - no output
         1 - error messages only
         2 - normal output
         3 - full output (includes informational messages) */
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
      /*--------------------------------------------------------------*/
      /* working segment */
      int meth;
      /* which method is used:
         'P' - primal simplex
         'D' - dual simplex */
      int p;
      /* the number of basic variable xB[p], 1 <= p <= m, chosen to
         leave the basis; the special case p < 0 means that non-basic
         gnm_float-bounded variable xN[q] just goes to its opposite bound,
         and the basis remains unchanged; p = 0 means that no choice
         can be made (in the case of primal simplex non-basic variable
         xN[q] can infinitely change, in the case of dual simplex the
         current basis is primal feasible) */
      int p_tag;
      /* if 1 <= p <= m, p_tag is a non-basic tag, which should be set
         for the variable xB[p] after it has left the basis */
      int q;
      /* the number of non-basic variable xN[q], 1 <= q <= n, chosen to
         enter the basis; q = 0 means that no choice can be made (in
         the case of primal simplex the current basis is dual feasible,
         in the case of dual simplex the dual variable that corresponds
         to xB[p] can infinitely change) */
      gnm_float *zeta; /* gnm_float zeta[1+m]; */
      /* the p-th row of the inverse inv(B) */
      gnm_float *ap; /* gnm_float ap[1+n]; */
      /* the p-th row of the current simplex table:
         ap[0] is not used;
         ap[j], 1 <= j <= n, is an influence coefficient, which defines
         how the non-basic variable xN[j] affects on the basic variable
         xB[p] = ... + ap[j] * xN[j] + ... */
      gnm_float *aq; /* gnm_float aq[1+m]; */
      /* the q-th column of the current simplex table;
         aq[0] is not used;
         aq[i], 1 <= i <= m, is an influence coefficient, which defines
         how the non-basic variable xN[q] affects on the basic variable
         xB[i] = ... + aq[i] * xN[q] + ... */
      gnm_float *gvec; /* gnm_float gvec[1+n]; */
      /* gvec[0] is not used;
         gvec[j], 1 <= j <= n, is a weight of non-basic variable xN[j];
         this vector is used to price non-basic variables in the primal
         simplex (for example, using the steepest edge technique) */
      gnm_float *dvec; /* gnm_float dvec[1+m]; */
      /* dvec[0] is not used;
         dvec[i], 1 <= i <= m, is a weight of basic variable xB[i]; it
         is used to price basic variables in the dual simplex */
      int *refsp; /* int refsp[1+m+n]; */
      /* the current reference space (used in the projected steepest
         edge technique); the flag refsp[k], 1 <= k <= m+n, is set if
         the variable x[k] belongs to the current reference space */
      int count;
      /* if this count (used in the projected steepest edge technique)
         gets zero, the reference space is automatically redefined */
      gnm_float *work; /* gnm_float work[1+m+n]; */
      /* working array (used for various purposes) */
      int *orig_typx; /* orig_typx[1+m+n]; */
      /* is used to save the original types of variables */
      gnm_float *orig_lb; /* orig_lb[1+m+n]; */
      /* is used to save the original lower bounds of variables */
      gnm_float *orig_ub; /* orig_ub[1+m+n]; */
      /* is used to save the original upper bounds of variables */
      int orig_dir;
      /* is used to save the original optimization direction */
      gnm_float *orig_coef; /* orig_coef[1+m+n]; */
      /* is used to save the original objective coefficients */
};

/* simplex method generic routines -----------------------------------*/

int spx_invert(SPX *spx);
/* reinvert the basis matrix */

void spx_ftran(SPX *spx, gnm_float x[], int save);
/* perform forward transformation (FTRAN) */

void spx_btran(SPX *spx, gnm_float x[]);
/* perform backward transformation (BTRAN) */

int spx_update(SPX *spx, int j);
/* update factorization for adjacent basis matrix */

gnm_float spx_eval_xn_j(SPX *spx, int j);
/* determine value of non-basic variable */

void spx_eval_bbar(SPX *spx);
/* compute values of basic variables */

void spx_eval_pi(SPX *spx);
/* compute simplex multipliers */

void spx_eval_cbar(SPX *spx);
/* compute reduced costs of non-basic variables */

gnm_float spx_eval_obj(SPX *spx);
/* compute value of the objective function */

void spx_eval_col(SPX *spx, int j, gnm_float col[], int save);
/* compute column of the simplex table */

void spx_eval_rho(SPX *spx, int i, gnm_float rho[]);
/* compute row of the inverse */

void spx_eval_row(SPX *spx, gnm_float rho[], gnm_float row[]);
/* compute row of the simplex table */

gnm_float spx_check_bbar(SPX *spx, gnm_float tol);
/* check primal feasibility */

gnm_float spx_check_cbar(SPX *spx, gnm_float tol);
/* check dual feasibility */

int spx_prim_chuzc(SPX *spx, gnm_float tol);
/* choose non-basic variable (primal simplex) */

int spx_prim_chuzr(SPX *spx, gnm_float relax);
/* choose basic variable (primal simplex) */

void spx_dual_chuzr(SPX *spx, gnm_float tol);
/* choose basic variable (dual simplex) */

int spx_dual_chuzc(SPX *spx, gnm_float relax);
/* choose non-basic variable (dual simplex) */

void spx_update_bbar(SPX *spx, gnm_float *obj);
/* update values of basic variables */

void spx_update_pi(SPX *spx);
/* update simplex multipliers */

void spx_update_cbar(SPX *spx, int all);
/* update reduced costs of non-basic variables */

int spx_change_basis(SPX *spx);
/* change basis and update the factorization */

gnm_float spx_err_in_bbar(SPX *spx);
/* compute maximal absolute error in bbar */

gnm_float spx_err_in_pi(SPX *spx);
/* compute maximal absolute error in pi */

gnm_float spx_err_in_cbar(SPX *spx, int all);
/* compute maximal absolute error in cbar */

void spx_reset_refsp(SPX *spx);
/* reset the reference space */

void spx_update_gvec(SPX *spx);
/* update the vector gamma for adjacent basis */

gnm_float spx_err_in_gvec(SPX *spx);
/* compute maximal absolute error in gvec */

void spx_update_dvec(SPX *spx);
/* update the vector delta for adjacent basis */

gnm_float spx_err_in_dvec(SPX *spx);
/* compute maximal absolute error in dvec */

/* simplex method solver routines ------------------------------------*/

int spx_warm_up(SPX *spx);
/* "warm up" the initial basis */

int spx_prim_opt(SPX *spx);
/* find optimal solution (primal simplex) */

int spx_prim_feas(SPX *spx);
/* find primal feasible solution (primal simplex) */

int spx_dual_opt(SPX *spx);
/* find optimal solution (dual simplex) */

int spx_simplex(SPX *spx);
/* base driver to the simplex method */

#endif

/* eof */
