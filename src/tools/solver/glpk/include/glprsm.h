/* glprsm.h */

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

#ifndef _GLPRSM_H
#define _GLPRSM_H

#include "gnumeric.h"
#include "numbers.h"

#include "glpinv.h"
#include "glpk.h"
#include "glplp.h"

#define btran                 glp_btran
#define build_basis           glp_build_basis
#define change_b              glp_change_b
#define check_bbar            glp_check_bbar
#define check_cbar            glp_check_cbar
#define check_dvec            glp_check_dvec
#define check_gvec            glp_check_gvec
#define check_rr              glp_check_rr
#define check_rsm             glp_check_rsm
#define create_rsm            glp_create_rsm
#define delete_rsm            glp_delete_rsm
#define dual_col              glp_dual_col
#define dual_row              glp_dual_row
#define eval_bbar             glp_eval_bbar
#define eval_cbar             glp_eval_cbar
#define eval_col              glp_eval_col
#define eval_pi               glp_eval_pi
#define eval_row              glp_eval_row
#define eval_xn               glp_eval_xn
#define eval_zeta             glp_eval_zeta
#define exact_dvec            glp_exact_dvec
#define exact_gvec            glp_exact_gvec
#define ftran                 glp_ftran
#define harris_col            glp_harris_col
#define harris_row            glp_harris_row
#define init_dvec             glp_init_dvec
#define init_gvec             glp_init_gvec
#define invert_b              glp_invert_b
#define pivot_col             glp_pivot_col
#define pivot_row             glp_pivot_row
#define rsm_dual              glp_rsm_dual
#define rsm_feas              glp_rsm_feas
#define rsm_primal            glp_rsm_primal
#define scale_rsm             glp_scale_rsm
#define update_b              glp_update_b
#define update_dvec           glp_update_dvec
#define update_gvec           glp_update_gvec

typedef struct RSM RSM;

struct RSM
{     /* revised simplex method common block */
      int m;
      /* number of rows (auxiliary variables) */
      int n;
      /* number of columns (structural variables) */
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
      MAT *A; /* MAT A[1:m,1:m+n]; */
      /* expanded matrix of constraint coefficients A~ = (I | -A),
         where I is the unity matrix, A is the original matrix of the LP
         problem; using the matrix A~ the system of equality constraints
         may be written in homogeneous form A~*x = 0, where x = (xR, xS)
         is the united vector of all variables, xR = (x[1],...,x[m]) is
         the subvector of auxiliary variables, xS = (x[m+1],...,x[m+n])
         is the subvector of structural variables */
      int *posx; /* int posx[1+m+n]; */
      /* posx[0] is not used; posx[k] is the position of the variable
         x[k] (1 <= k <= m+n) in the vector xB of basis variables or in
         the vector xN of non-basis variables:
         posx[k] = +i means that x[k] = xB[i] (1 <= i <= m)
         posx[k] = -j means that x[k] = xN[j] (1 <= j <= n) */
      int *indb; /* int indb[1+m]; */
      /* indb[0] is not used; indb[i] = k means that xB[i] = x[k] */
      int *indn; /* int indn[1+n]; */
      /* indn[0] is not used; indn[j] = k means that xN[j] = x[k] */
      int *tagn; /* int tagn[1+n]; */
      /* tagn[0] is not used; tagn[j] is the status of the non-basis
         variable xN[j] (1 <= j <= n):
         'L' - non-basis variable on its lower bound
         'U' - non-basis variable on its upper bound
         'F' - non-basis free variable
         'S' - non-basis fixed variable */
      INV *inv;
      /* an invertable (factorized) form of the basis matrix */
      int iter;
      /* iteration count (increased each time when the current basis is
         replaced by the adjacent one) */
};

/*** operations on basis matrix ***/

extern int invert_b(RSM *rsm);
/* rebuild representation of the basis matrix */

extern void ftran(RSM *rsm, gnum_float u[], int save);
/* perform forward transformation */

extern void btran(RSM *rsm, gnum_float u[]);
/* perform backward transformation */

extern int update_b(RSM *rsm, int p);
/* update representation of the basis matrix */

/*** operations on simplex table ***/

extern void check_rsm(RSM *rsm);
/* check common block for correctness */

extern gnum_float eval_xn(RSM *rsm, int j);
/* determine value of non-basic variable */

extern void eval_bbar(RSM *rsm, gnum_float bbar[]);
/* compute values of basic variables */

extern void eval_pi(RSM *rsm, gnum_float c[], gnum_float pi[]);
/* compute simplex multipliers */

extern void eval_cbar(RSM *rsm, gnum_float c[], gnum_float pi[], gnum_float cbar[]);
/* compute reduced costs of non-basic variables */

extern int check_rr(gnum_float x, gnum_float x0, gnum_float tol);
/* check relative residual */

extern int check_bbar(RSM *rsm, gnum_float bbar[], gnum_float tol);
/* check basis solution for primal feasibility */

extern int check_cbar(RSM *rsm, gnum_float c[], gnum_float cbar[], gnum_float tol);
/* check basis solution for dual feasibility */

extern void eval_col(RSM *rsm, int j, gnum_float aj[], int save);
/* compute column of the simplex table */

extern void eval_zeta(RSM *rsm, int i, gnum_float zeta[]);
/* compute row of the inverse */

extern void eval_row(RSM *rsm, gnum_float zeta[], gnum_float ai[]);
/* compute row of the simplex table */

extern int change_b(RSM *rsm, int p, int tagp, int q);
/* change basis */

/*** primal steepest edge routines ***/

extern void init_gvec(RSM *rsm, gnum_float gvec[]);
/* initialize the vector gamma */

extern void update_gvec(RSM *rsm, gnum_float gvec[], int p, int q,
      gnum_float ap[], gnum_float aq[], gnum_float w[]);
/* update the vector gamma */

extern gnum_float exact_gvec(RSM *rsm, int j);
/* compute exact value of gamma[j] */

extern gnum_float check_gvec(RSM *rsm, gnum_float gvec[]);
/* check accuracy of the vector gamma */

/*** dual steepest edge routines ***/

extern void init_dvec(RSM *rsm, gnum_float dvec[]);
/* initialize the vector delta */

extern void update_dvec(RSM *rsm, gnum_float dvec[], int p, int q,
      gnum_float ap[], gnum_float aq[], gnum_float w[]);
/* update the vector delta */

extern gnum_float exact_dvec(RSM *rsm, int i);
/* compute exact value of delta[i] */

extern gnum_float check_dvec(RSM *rsm, gnum_float dvec[]);
/* check accuracy of the vector delta */

/*** primal simplex method routines ***/

extern int pivot_col(RSM *rsm, gnum_float c[], gnum_float cbar[], gnum_float gvec[],
      gnum_float tol);
/* choose non-basic variable (primal) */

extern int pivot_row(RSM *rsm, int q, int dir, gnum_float aq[],
      gnum_float bbar[], int *tagp, gnum_float tol);
/* choose basic variable (primal, standard technique) */

extern int harris_row(RSM *rsm, int q, int dir, gnum_float aq[],
      gnum_float bbar[], int *tagp, gnum_float tol, gnum_float tol1);
/* choose basic variable (primal, Harris technique) */

/*** dual simplex method routines ***/

extern int dual_row(RSM *rsm, gnum_float bbar[], gnum_float dvec[], int *tagp,
      gnum_float tol);
/* choose basic variable (dual) */

extern int dual_col(RSM *rsm, int tagp, gnum_float ap[], gnum_float cbar[],
      gnum_float tol);
/* choose non-basic variable (dual, standard technique) */

extern int harris_col(RSM *rsm, int tagp, gnum_float ap[], gnum_float c[],
      gnum_float cbar[], gnum_float tol, gnum_float tol1);
/* choose non-basic variable (dual, Harris technique) */

/*** driver simplex method routines ***/

extern int rsm_primal(RSM *rsm, int (*monit)(void), gnum_float c[],
      gnum_float tol_bnd, gnum_float tol_dj, gnum_float tol_piv, gnum_float gvec[],
      int relax);
/* find optimal solution using primal simplex method */

extern int rsm_dual(RSM *rsm, int (*monit)(void), gnum_float c[],
      gnum_float tol_bnd, gnum_float tol_dj, gnum_float tol_piv, gnum_float dvec[],
      int relax);
/* find optimal solution using dual simplex method */

extern int rsm_feas(RSM *rsm, int (*monit)(void),
      gnum_float tol_bnd, gnum_float tol_dj, gnum_float tol_piv, gnum_float gvec[],
      int relax);
/* find feasible solution using primal simplex method */

/*** auxiliary routines ***/

#if 0
extern RSM *create_rsm(LPI *lp);
/* create revised simplex method common block */
#endif

extern void delete_rsm(RSM *rsm);
/* delete revised simplex method common block */

extern void scale_rsm(RSM *rsm, gnum_float R[], gnum_float S[]);
/* scale problem components in RSM block */

#if 0
extern int build_basis(RSM *rsm, LPI *lp);
/* build advanced basis */
#endif

#endif

/* eof */
