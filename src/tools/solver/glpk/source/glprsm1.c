/* glprsm1.c */

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
#include "glprsm.h"

/*----------------------------------------------------------------------
-- btran - perform backward transformation.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- void btran(RSM *rsm, gnum_float u[]);
--
-- *Description*
--
-- The btran routine performs backward transformation (BTRAN) of the
-- vector u using some representation of the basis matrix.
--
-- This operation means solving the system B'*u' = u, where B is a
-- matrix transposed to the current basis matrix B, u is the given
-- vector which should be transformed, u' is the resultant vector.
--
-- On entry the array u should contain elements of the vector u in
-- locations u[1], u[2], ..., u[m], where m is order of the matrix B.
-- On exit this array contains elements of the vector u' in the same
-- locations. */

void btran(RSM *rsm, gnum_float u[])
{     inv_btran(rsm->inv, u);
      return;
}

#if 0
/*----------------------------------------------------------------------
-- build_basis - build advanced basis.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- int build_basis(RSM *rsm, LPI *lp);
--
-- *Description*
--
-- The routine build_basis() obtains information about advanced basis
-- specified in a problem object lp and uses this information in order
-- to build the basis in the RSM block.
--
-- *Returns*
--
-- The routine build_basis() returns one of the following codes:
--
-- 0 - no errors;
-- 1 - type of a row/column is not compatible with its status;
-- 2 - basis has invalid structure;
-- 3 - unable to invert the basis matrix.
--
-- Should note that in case of error statuses of the variables as well
-- as representation of the basis matrix become invalid, therefore the
-- RSM block should not be used until necessary corrections have been
-- made. */

int build_basis(RSM *rsm, LPI *lp)
{     int m = rsm->m, n = rsm->n, i, j, k;
      insist(m == glp_get_num_rows(lp));
      insist(n == glp_get_num_cols(lp));
      i = 0; /* number of basic variables */
      j = 0; /* number of non-basic variables */
      for (k = 1; k <= m+n; k++)
      {  int type, tagx;
         /* obtain type and status of the k-th variable */
         if (k <= m)
         {  glp_get_row_bnds(lp, k, &type, NULL, NULL);
            glp_get_row_soln(lp, k, &tagx, NULL, NULL);
         }
         else
         {  glp_get_col_bnds(lp, k-m, &type, NULL, NULL);
            glp_get_col_soln(lp, k-m, &tagx, NULL, NULL);
         }
         /* check for compatibility between type and status */
         if (!(tagx == 'B' ||
            type == 'F' && tagx == 'F' ||
            type == 'L' && tagx == 'L' ||
            type == 'U' && tagx == 'U' ||
            type == 'D' && tagx == 'L' ||
            type == 'D' && tagx == 'U' ||
            type == 'S' && tagx == 'S')) return 1;
         /* store information about basis into RSM */
         if (tagx == 'B')
         {  /* x[k] is basic variable xB[i] */
            i++;
            if (i > m) return 2;
            rsm->posx[k] = +i;
            rsm->indb[i] =  k;
         }
         else
         {  /* x[k] is non-basic variable xN[j] */
            j++;
            if (j > n) return 2;
            rsm->posx[k] = -j;
            rsm->indn[j] =  k;
            rsm->tagn[j] = tagx;
         }
      }
      insist(i == m && j == n);
      /* try to invert the basis matrix */
      if (invert_b(rsm)) return 3;
      /* hmm... the basis seems to be ok */
      return 0;
}
#endif

/*----------------------------------------------------------------------
-- change_b - change basis.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- int change_b(RSM *rsm, int p, int tagp, int q);
--
-- *Description*
--
-- The change_b routine changes the current basis replacing it by the
-- adjacent one. The routine takes the basic variable (xB)p
-- (1 <= p <= m) out the set of basic variables xB and brings instead
-- (xB)p the non-basic variables (xN)q (1 <= q <= n) into xB.
-- Correspondingly, the basic variable (xB)p replaces the non-basic
-- variable (xN)q in the set of non-basic variables xN.
--
-- The parameter tagp specifies to what subset xF, xL, xU, or xS the
-- basic variable (xB)p should be attributed after it has left the
-- basis. The value of tagp has the same meaning as the field rsm.tagn
-- (see the structure RSM in glprsm.h) and should be compatible with
-- the type of variable (xB)p.
--
-- The special case p < 0 means that the current basis is not changed,
-- but the non-basic variable (xN)q (which should be gnum_float-bounded
-- variable) just goes from its current bound to the opposite one. The
-- parameter tagp is ignored in this special case.
--
-- The change_b routine also replaces p-th column of the basis matrix
-- B by q-th column of the matrix N and updates the representation of B
-- by means of the update_b routine. Note that new p-th column of B is
-- passed implicitly; it is assumed that this column was saved before
-- by the eval_col routine. If the representation becomes too long or
-- inaccurate, the change_b routine automatically rebuilds it by means
-- of the invert_b routine.
--
-- One call to the change_b routine is considered as one iteration of
-- the simplex method.
--
-- *Returns*
--
-- If the representation of the basis matrix was not rebuilt or it was
-- rebuilt successfully, the change_b routine returns zero. Otherwise
-- the routine returns non-zero. On the latter case the calling program
-- should not use the representation until the basis matrix will be
-- corrected and the representation will be rebuilt anew. */

int change_b(RSM *rsm, int p, int tagp, int q)
{     int m = rsm->m, n = rsm->n, ret = 0, k, kp, kq;
      if (p < 0)
      {  /* xN[q] goes from the current bound to the opposite one */
         k = rsm->indn[q]; /* x[k] = xN[q] */
         insist(rsm->type[k] == 'D');
         insist(rsm->tagn[q] == 'L' || rsm->tagn[q] == 'U');
         rsm->tagn[q] = (rsm->tagn[q] == 'L') ? 'U' : 'L';
      }
      else
      {  /* xB[p] leaves the basis, xN[q] enters the basis */
         insist(1 <= p && p <= m);
         insist(1 <= q && q <= n);
         kp = rsm->indb[p]; kq = rsm->indn[q];
         rsm->posx[kq] = +p; rsm->posx[kp] = -q;
         rsm->indb[p] = kq; rsm->indn[q] = kp;
         rsm->tagn[q] = tagp;
         /* update representation of the basis matrix */
         ret = update_b(rsm, p);
         /* if representation became inaccurate or too long, reinvert
            the basis matrix */
         if (ret != 0) ret = invert_b(rsm);
      }
      /* check common block for correctness */
      check_rsm(rsm);
      /* increase iteration count */
      rsm->iter++;
      /* returns to the calling program */
      return ret;
}

/*----------------------------------------------------------------------
-- check_bbar - check basis solution for primal feasibility.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- int check_bbar(RSM *rsm, gnum_float bbar[], gnum_float tol);
--
-- *Description*
--
-- The check_bbar routine checks the given basis solution for primal
-- feasibility.
--
-- The array bbar should contain the given values of basic variables
-- beta = (beta_1, ..., beta_m) in locations bbar[1], ..., bbar[m]
-- respectively.
--
-- The parameter tol > 0 is a relative tolerance.
--
-- In order to see if the given basis solution is primal feasible the
-- routine checks the primal feasibility conditions which using the
-- relative tolerance are the following:
--
--    (lB)i - eps <= beta_i <= (uB)i + eps, i = 1,...,m
--
-- where
--
--    eps = tol * max(1, |(lB)i|) in case of lower bound
--    eps = tol * max(1, |(uB)i|) in case of upper bound
--
-- *Returns*
--
-- If the given basis solution is primal feasible (i.e. it satisfies to
-- the conditions above) the check_bbar routine returns zero. Otherwise
-- the routine returns non-zero. */

int check_bbar(RSM *rsm, gnum_float bbar[], gnum_float tol)
{     int m = rsm->m, i, k;
      for (i = 1; i <= m; i++)
      {  k = rsm->indb[i]; /* x[k] = xB[i] */
         if (rsm->type[k] == 'L' || rsm->type[k] == 'D' ||
             rsm->type[k] == 'S')
         {  /* xB[i] has lower bound */
            if (check_rr(bbar[i], rsm->lb[k], tol) < -1) return 1;
         }
         if (rsm->type[k] == 'U' || rsm->type[k] == 'D' ||
             rsm->type[k] == 'S')
         {  /* xB[i] has upper bound */
            if (check_rr(bbar[i], rsm->ub[k], tol) > +1) return 1;
         }
      }
      return 0;
}

/*----------------------------------------------------------------------
-- check_cbar - check basis solution for dual feasibility.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- int check_cbar(RSM *rsm, gnum_float c[], gnum_float cbar[], gnum_float tol);
--
-- *Description*
--
-- The check_cbar routine checks the given basis solution for dual
-- feasibility.
--
-- The array c should contain the expanded vector c of coefficients of
-- the objective function in locations c[1], ..., c[m+n].
--
-- The array cbar should contain the given reduced costs of non-basic
-- variables d = (d_1, ..., d_n) in locations cbar[1], ..., cbar[n]
-- respectively. It is assumed that the reduced costs were computed
-- using the vector c passed to this routine.
--
-- The parameter tol > 0 is a relative tolerance.
--
-- In order to see of the given basis solution is dual feasible the
-- routine checks the dual feasibility conditions which using the
-- relative tolerance are the following:
--
--    if (xN)j in xF then -eps <= dj <= +eps
--    if (xN)j in xL then dj >= +eps
--    if (xN)j in xU then dj <= -eps
--    if (xN)j in xS then -inf < dj < +inf
--
-- where
--
--    eps = tol * max(1, |(cN)j|)
--
-- (the absolute tolerance eps reflects that the reduced cost on
-- non-basic variable (xN)j is the difference dj = (cN)j - pi'*Nj).
--
-- *Returns*
--
-- The the given basic solution is dual feasible (i.e. it satisfies to
-- the conditions above), the check_cbar routine returns zero. Otherwise
-- the routine returns non-zero. */

int check_cbar(RSM *rsm, gnum_float c[], gnum_float cbar[], gnum_float tol)
{     int n = rsm->n, j, k;
      for (j = 1; j <= n; j++)
      {  k = rsm->indn[j]; /* x[k] = xN[j] */
         if (rsm->tagn[j] == 'F' || rsm->tagn[j] == 'L')
         {  /* xN[j] can increase */
            if (check_rr(cbar[j] + c[k], c[k], tol) < -1) return 1;
         }
         if (rsm->tagn[j] == 'F' || rsm->tagn[j] == 'U')
         {  /* xN[j] can decrease */
            if (check_rr(cbar[j] + c[k], c[k], tol) > +1) return 1;
         }
      }
      return 0;
}

/*----------------------------------------------------------------------
-- check_dvec - check accuracy of the vector delta.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- gnum_float check_dvec(RSM *rsm, gnum_float dvec[]);
--
-- *Description*
--
-- The check_dvec routine is intended for checking accuracy of the
-- vector delta. It computes the absolute error
--
--    e = max |delta'[i] - delta[i]|
--
-- where delta' is exact vector computed by means of the exact_dvec
-- routine, delta is approximate vector given in the array dvec.
--
-- This operation is extremely inefficient and may be used only for
-- debugging purposes.
--
-- *Returns*
--
-- The check_dvec routine returns the absolute error e (see above). */

gnum_float check_dvec(RSM *rsm, gnum_float dvec[])
{     int m = rsm->m, i;
      gnum_float d, dmax = 0.0;
      for (i = 1; i <= m; i++)
      {  d = gnumabs(exact_dvec(rsm, i) - dvec[i]);
         if (dmax < d) dmax = d;
      }
      return dmax;
}

/*----------------------------------------------------------------------
-- check_gvec - check accuracy of the vector gamma.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- gnum_float check_gvec(RSM *rsm, gnum_float gvec[]);
--
-- *Description*
--
-- The check_gvec routine is intended for checking accuracy of the
-- vector gamma. It computes the absolute error
--
--    e = max |gamma'[j] - gamma[j]|
--
-- where gamma' is exact vector computed by means of the exact_gvec
-- routine, gamma is approximate vector given in the array gvec.
--
-- This operation is extremely inefficient and may be used only for
-- debugging purposes.
--
-- *Returns*
--
-- The check_gvec routine returns the absolute error e (see above). */

gnum_float check_gvec(RSM *rsm, gnum_float gvec[])
{     int n = rsm->n, j;
      gnum_float d, dmax = 0.0;
      for (j = 1; j <= n; j++)
      {  d = gnumabs(exact_gvec(rsm, j) - gvec[j]);
         if (dmax < d) dmax = d;
      }
      return dmax;
}

/*----------------------------------------------------------------------
-- check_rr - check relative residual.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- int check_rr(gnum_float x, gnum_float x0, gnum_float tol);
--
-- *Description*
--
-- The check_rr routine checks relative residual between the computed
-- quantity x and the given quantity x0 using the given relative
-- tolerance tol > 0.
--
-- *Returns*
--
-- The check_rr routine returns one of the following codes:
--
-- -2, if x < x0 - eps
-- -1, if x0 - eps <= x < x0
--  0, if x = x0
-- +1, if x0 < x <= x0 + eps
-- +2, if x > x0 + eps
--
-- where eps = tol * max(1, |x0|). */

int check_rr(gnum_float x, gnum_float x0, gnum_float tol)
{     int ret;
      gnum_float eps;
      eps = (x0 >= 0.0 ? +x0 : -x0);
      eps = tol * (eps > 1.0 ? eps : 1.0);
      if (x < x0)
      {  if (x < x0 - eps)
            ret = -2;
         else
            ret = -1;
      }
      else if (x > x0)
      {  if (x > x0 + eps)
            ret = +2;
         else
            ret = +1;
      }
      else
         ret = 0;
      return ret;
}

/*----------------------------------------------------------------------
-- check_rsm - check common block for correctness.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- void check_rsm(RSM *rsm);
--
-- *Description*
--
-- The check_rsm routine checks the simplex method common block, which
-- rsm points to, for correctness. In case of error the routine displays
-- an appropriate message and terminates the program.
--
-- Note that the check_rsm routine doesn't check the constraint matrix,
-- because the corresponding operation is extremely inefficient. It can
-- be checked additionally by the check_mat routine. */

void check_rsm(RSM *rsm)
{     int m = rsm->m, n = rsm->n, k;
      if (m < 1)
         fault("check_rsm: invalid number of rows");
      if (n < 1)
         fault("check_rsm: invalid number of columns");
      for (k = 1; k <= m+n; k++)
      {  switch (rsm->type[k])
         {  case 'F':
               if (!(rsm->lb[k] == 0.0 && rsm->ub[k] == 0.0))
err1:             fault("check_rsm: invalid bounds of row/column");
               break;
            case 'L':
               if (rsm->ub[k] != 0.0) goto err1;
               break;
            case 'U':
               if (rsm->lb[k] != 0.0) goto err1;
               break;
            case 'D':
               break;
            case 'S':
               if (rsm->lb[k] != rsm->ub[k]) goto err1;
               break;
            default:
               fault("check_rsm: invalid type of row/column");
         }
      }
      if (!(rsm->A->m == m && rsm->A->n == m+n))
         fault("check_rsm: invalid dimension of constraint matrix");
      for (k = 1; k <= m+n; k++)
      {  if (rsm->posx[k] > 0)
         {  int i = +rsm->posx[k]; /* xB[i] = x[k] */
            if (!(1 <= i && i <= m && rsm->indb[i] == k))
               fault("check_rsm: invalid position of basic row/column");
         }
         else
         {  int j = -rsm->posx[k]; /* xN[j] = x[k] */
            if (!(1 <= j && j <= n && rsm->indn[j] == k))
               fault("check_rsm: invalid position of non-basic row/colu"
                  "mn");
            switch (rsm->type[k])
            {  case 'F':
                  if (rsm->tagn[j] != 'F')
err2:                fault("check_rsm: invalid tag of non-basic row/col"
                        "umn");
                  break;
               case 'L':
                  if (rsm->tagn[j] != 'L') goto err2;
                  break;
               case 'U':
                  if (rsm->tagn[j] != 'U') goto err2;
                  break;
               case 'D':
                  if (!(rsm->tagn[j] == 'L' || rsm->tagn[j] == 'U'))
                     goto err2;
                  break;
               case 'S':
                  if (rsm->tagn[j] != 'S') goto err2;
                  break;
               default:
                  insist(rsm->type[k] != rsm->type[k]);
            }
         }
      }
      return;
}

#if 0 /* 3.0.7 */
/*----------------------------------------------------------------------
-- crash_aa() - crash augmented constraint matrix.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- int crash_aa(MAT *A, int tag[], PER *P, PER *Q);
--
-- *Description*
--
-- The routine crash_aa() tries to find an initial basis matrix using
-- the given augmented constraint matrix A and tags of "non-desirable"
-- columns.
--
-- On input the matrix A = (I | -A') is the augmented constraint matrix,
-- where I is the unity submatrix, columns of which correspond to the
-- auxiliary (logical) variables, and A' is the original constraint
-- matrix, columns of which correspond to the structural variables. Only
-- pattern of A is used, therefore A should contain no explicit zeros.
-- The matrix A is not changed on exit.
--
-- The array tag[] should have at least 1+n locations, where n is the
-- number of columns of the augmented matrix A (not A'). On input the
-- location tag[0] is not used, and the location tag[j], j = 1,...,n,
-- is a tag of the j-th column. If tag[j] is non-zero, this means that
-- it is *not* desirable to include the j-th column of the matrix A in
-- the initial basis matrix (such columns usually correspond to fixed
-- variables). The array tag[] is not changed on exit.
--
-- The result obtained by the routine is permutation matrices P and Q.
-- The routine finds such P and Q that the first m columns of the matrix
-- P*A*Q is a lower tringular matrix with non-zero diagonal, so this
-- submatrix may be used as an initial basis matrix. The routine tries
-- to minimize number of "non-desirable" columns in the initial basis
-- matrix. On input the matrices P and Q are ignored.
--
-- *Returns*
--
-- The routine crash_aa() returns the number of "non-desirable" columns,
-- which it couldn't get rid of and which are kept in the initial basis
-- matrix.
--
-- *Complexity*
--
-- The routine crash_aa() takes the time O(nz), where nz is number of
-- non-zeros in the matrix A.
--
-- *Algorithm*
--
-- The routine crash_aa() starts from the matrix W = P*A*Q, where P and
-- Q are the unity matrices, so initially W = A.
--
-- Before the next iteration W = (W1 | W2 | W3), where W1 is partially
-- built lower triangular submatrix, W2 is the active submatrix, and W3
-- is the submatrix that contains rejected columns. Graphically the
-- matrix W looks like the following:
--
--    1         k1         k2       n
-- 1  x . . . . . . . . . . . . . # #
--    x x . . . . . . . . . . . . # #
--    x x x . . . . . . . . . . # # #
--    x x x x . . . . . . . . . # # #
--    x x x x x . . . . . . . # # # #
-- k1 x x x x x * * * * * * * # # # #
--    x x x x x * * * * * * * # # # #
--    x x x x x * * * * * * * # # # #
--    x x x x x * * * * * * * # # # #
-- m  x x x x x * * * * * * * # # # #
--    <--W1---> <----W2-----> <--W3->
--
-- where the columns 1, ..., k1-1 form the submatrix W1, the columns
-- k1, ..., k2 form the submatrix W2, and the columns k2+1, ..., m form
-- the submatrix W3.
--
-- The matrix W has implicit representation. This means that actually
-- all transformations are performed on the permutation matrices P and
-- Q, which define the matrix W = P*A*Q.
--
-- Initially W1 and W3 are empty (have no columns), and W2 = A. Before
-- the first iteration the routine moves all "non-desirable" columns
-- from the active submatrix W2 to the submatrix W3 to prevent choosing
-- such columns on the subsequent iterations.
--
-- On each next iteration the routine looks for a singleton row, i.e.
-- the row that has the only non-zero element in the active submatrix
-- W2. If such a row exists and the corresponding element is w[i,j],
-- where (by the definition) k1 <= i <= m and k1 <= j <= k2, the routine
-- permutes k1-th and i-th rows and k1-th and j-th columns of the matrix
-- W in order to place the element in the position [k1,k1], removes the
-- k1-th column from the active submatrix W2, and includes this column
-- in the submatrix W1. If no singleton rows exist, but the active
-- submatrix W2 is not empty, the routine chooses a j-th column, whose
-- length is greatest in the active submatrix, removes this column from
-- W2, and includes it in W3, in the hope that rejecting the j-th column
-- will involve appearing new row singletons in W2.
--
-- If the active submatrix W2 becomes empty, the main phase is finished.
-- It may happen that the submatrix W1 has less than m columns, in which
-- case the routine padds the submatrix W1 to the lower triangular form
-- using the columns of the unity submatrix of the matrix A, which were
-- initially rejected as "non-desirable".
--
-- In order to find row signletons for a fixed time the Duff scheme rs
-- is used. This scheme implements a family {R(0), ..., R(n)} of rows of
-- the active submatrix, where R(len) is a set of rows that have len
-- non-zeros in the active submatrix. Each time when a column leaves the
-- active submatrix, the routine relocates each affected row from one
-- set to the other.
--
-- In order to find a column of the active submatrix, which has maximal
-- length, for a fixed time the Duff scheme cs is used. However, unlike
-- the scheme rs after initializing the scheme cs the routine scans this
-- scheme in the order of increasing column lengths and adds all columns
-- the the set C(0), which then is used as ordinary gnum_float linked list
-- to access columns in the reverse order.
--
-- Note that both schemes rs and cs hold rows and columns of the matrix
-- A, not the matrix W. */

struct csa
{     /* common storage area */
      MAT *A; /* MAT A[1:m,1:n]; */
      /* augmented constraint matrix A = (I | -A'), where A' is the
         original constraint matrix */
      PER *P; /* PER P[1:m]; */
      /* the row permutation matrix */
      PER *Q; /* PER Q[1:n]; */
      /* the column permutation matrix */
      int k1;
      /* the leftmost column of the active submatrix W2 */
      int k2;
      /* the rightmost column of the active submatrix W2 */
      DUFF *rs;
      /* active rows of the matrix A */
      DUFF *cs;
      /* active columns of the matrix A */
};

#define iW(i) (P->col[i])
/* converts row number of A to row number of W */

#define iA(i) (P->row[i])
/* converts row number of W to row number of A */

#define jW(j) (Q->row[j])
/* converts column number of A to column number of W */

#define jA(j) (Q->col[j])
/* converts column number of W to column number of A */

/*----------------------------------------------------------------------
-- remove_col() - remove column from the active submatrix.
--
-- This routine updates the row and column Duff schemes for subsequent
-- removing the j-th column from the active submatrix W2. The number j
-- corresponds to the matrix W, not to the matrix A. */

static void remove_col(struct csa *csa, int j)
{     MAT *A = csa->A;
      PER *Q = csa->Q;
      DUFF *rs = csa->rs, *cs = csa->cs;
      int k1 = csa->k1, k2 = csa->k2, len;
      ELEM *e;
      insist(k1 <= j && j <= k2);
      /* update the row scheme */
      for (e = A->col[jA(j)]; e != NULL; e = e->col)
      {  len = rs->len[e->i];
         exclude_obj(rs, e->i);
         include_obj(rs, e->i, len-1);
      }
      /* remove the j-th column from the active column list */
      exclude_obj(cs, jA(j));
      return;
}

/*----------------------------------------------------------------------
-- permute_rows() - permute rows of the matrix W.
--
-- This routine permutes i1-th and i2-th rows of the matrix W. */

static void permute_rows(struct csa *csa, int i1, int i2)
{     PER *P = csa->P;
      int t1, t2;
      t1 = iA(i1), t2 = iA(i2);
      iA(i1) = t2, iW(t2) = i1;
      iA(i2) = t1, iW(t1) = i2;
      return;
}

/*----------------------------------------------------------------------
-- permute_cols() - permute columns of the matrix W.
--
-- This routine permutes j1-th and j2-th columns of the matrix W. */

static void permute_cols(struct csa *csa, int j1, int j2)
{     PER *Q = csa->Q;
      int t1, t2;
      t1 = jA(j1), t2 = jA(j2);
      jA(j1) = t2, jW(t2) = j1;
      jA(j2) = t1, jW(t1) = j2;
      return;
}

/*----------------------------------------------------------------------
-- crash_aa() - crash augmented constraint matrix.
--
-- This routine finds an initial basis matrix using the given augmented
-- constraint matrix A and the tags of "non-desirable" columns. */

int crash_aa(MAT *A, int tag[], PER *P, PER *Q)
{     struct csa _csa, *csa = &_csa;
      DUFF *rs, *cs;
      ELEM *e;
      int m, n, i, j, len, ret;
      m = A->m, n = A->n;
      insist(P->n == m && Q->n == n);
      /* reset permutation matrices, P := I, Q := I */
      reset_per(P);
      reset_per(Q);
      /* create and initialize Duff schemes */
      rs = create_duff(m, n);
      cs = create_duff(n, m);
      for (i = 1; i <= m; i++)
         include_obj(rs, i, count_nz(A, +i));
      for (j = 1; j <= n; j++)
         include_obj(cs, j, count_nz(A, -j));
      /* add all columns of the matrix A to the set C(0) in the order
         of increasing their lengths; these columns will be scanned in
         the reverse (decreasing) order */
      for (len = 1; len <= m; len++)
      {  while (cs->head[len] != 0)
         {  j = cs->head[len];
            exclude_obj(cs, j);
            include_obj(cs, j, 0);
         }
      }
      /* initialize csa; set initial edges of the active submatrix */
      csa->A = A, csa->P = P, csa->Q = Q;
      csa->k1 = 1, csa->k2 = n;
      csa->rs = rs, csa->cs = cs;
      /* now W1 = W3 = <empty>, W2 = W = A */
      /* remove "non-desirable" columns from W2 to W3 */
      for (j = csa->k1; j <= csa->k2; )
      {  if (tag[jA(j)])
         {  remove_col(csa, j);
            permute_cols(csa, csa->k2, j);
            csa->k2--;
         }
         else
            j++;
      }
      /* the main loop starts here */
      while (csa->k1 <= csa->k2)
      {  if (rs->head[1] != 0)
         {  /* the active submatrix W2 has a row singleton */
            i = iW(rs->head[1]);
            /* the singleton in the i-th row of W; find it */
            j = 0;
            for (e = A->row[iA(i)]; e != NULL; e = e->row)
            {  if (csa->k1 <= jW(e->j) && jW(e->j) <= csa->k2)
               {  insist(j == 0);
                  j = jW(e->j);
               }
            }
            insist(j != 0);
            /* the singleton has been found in j-th column of W */
            /* remove the j-th column from the active submatrix W2 */
            remove_col(csa, j);
            /* move w[i,j] to the position w[k1,k1] and add the k1-th
               column of W to the submatrix W1 */
            permute_rows(csa, csa->k1, i);
            permute_cols(csa, csa->k1, j);
            csa->k1++;
         }
         else
         {  /* the active submatrix has no row singletons */
            /* if there is no more columns in the active submatrix W2,
               terminate the process */
            if (cs->head[0] == 0) break;
            /* pull the first column from C(0); this column has largest
               length among other active columns */
            j = jW(cs->head[0]);
            insist(csa->k1 <= j && j <= csa->k2);
            /* remove the j-th column from the active submatrix W2 and
               drop it out adding to the submatrix W3 */
            remove_col(csa, j);
            permute_cols(csa, csa->k2, j);
            csa->k2--;
         }
      }
      /* now there is no "non-desirable" columns in the submatrix W1,
         so it's time to determine its rank deficit */
      ret = m - csa->k1 + 1;
      /* padd the submatrix W1 to the lower triangular form by columns
         of the unity submatrix of the matrix A */
      while (csa->k1 <= m)
      {  /* k1-th column of the matrix W should have 1 in the position
            (k1,k1); but the k1-th row of the matrix W corresponds to
            the iA(k1)-th row of the matrix A, that allows to determine
            what column of the matrix A should be placed instead the
            k1-th column of the matrix W */
         j = jW(iA(csa->k1));
         insist(j >= csa->k2);
         permute_cols(csa, csa->k1, j);
         csa->k1++;
      }
      /* free auxiliary data structure */
      delete_duff(rs);
      delete_duff(cs);
      /* returns to the calling program */
      return ret;
}
#endif

#if 0
/*----------------------------------------------------------------------
-- create_rsm - create revised simplex method common block.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- RSM *create_rsm(LPI *lp, int form);
--
-- *Description*
--
-- The routine create_rsm() obtains information from LP problem object,
-- which the parameter lp points to, and creates a common block used by
-- the revised simplex method routines.
--
-- The parameter form specifies the form of representation of the basis
-- matrix which should be used constructed:
--
-- 0 - PFI;
-- 1 - RFI + Bartels-Golub updating technique;
-- 2 - RFI + Forrest-Tomlin updating technique;
-- 3 - AFI;
-- 4 - UFI.
--
-- The initial basis is standard (all auxiliary variables are basic and
-- all structural variables are non-basic).
--
-- Being created the representation of the basis matrix for the initial
-- standard basis is valid, therefore no reinversion is needed.
--
-- *Returns*
--
-- The routine create_rsm() returns a pointer to the created block. */

RSM *create_rsm(LPI *lp)
{     RSM *rsm;
      int m = glp_get_num_rows(lp), n = glp_get_num_cols(lp), i, j, k;
      if (m == 0) fault("create_rsm: problem has no rows");
      if (n == 0) fault("create_rsm: problem has no columns");
      /* allocate RSM structure */
      rsm = umalloc(sizeof(RSM));
      rsm->m = m;
      rsm->n = n;
      rsm->type = ucalloc(1+m+n, sizeof(int));
      rsm->lb = ucalloc(1+m+n, sizeof(gnum_float));
      rsm->ub = ucalloc(1+m+n, sizeof(gnum_float));
      rsm->A = create_mat(m, m+n);
      rsm->posx = ucalloc(1+m+n, sizeof(int));
      rsm->indb = ucalloc(1+m, sizeof(int));
      rsm->indn = ucalloc(1+n, sizeof(int));
      rsm->tagn = ucalloc(1+n, sizeof(int));
      rsm->inv = inv_create(m, 100);
      rsm->iter = 0;
      /* obtain types and bounds of rows */
      for (i = 1; i <= m; i++)
      {  glp_get_row_bnds(lp, i, &rsm->type[i], &rsm->lb[i],
            &rsm->ub[i]);
      }
      /* obtain types and bounds of columns */
      for (j = 1; j <= n; j++)
      {  glp_get_col_bnds(lp, j, &rsm->type[m+j], &rsm->lb[m+j],
            &rsm->ub[m+j]);
      }
      /* build the expanded matrix A = (I | -A'), where I is the unity
         matrix, A' is the original matrix of constraint coefficients */
      {  int *cn = ucalloc(1+n, sizeof(int));
         gnum_float *ai = ucalloc(1+n, sizeof(gnum_float));
         for (i = 1; i <= m; i++)
         {  int nz = glp_get_row_coef(lp, i, cn, ai), t;
            new_elem(rsm->A, i, i, +1.0);
            for (t = 1; t <= nz; t++)
            {  if (ai[t] != 0.0)
                  new_elem(rsm->A, i, m + cn[t], - ai[t]);
            }
         }
         ufree(cn);
         ufree(ai);
      }
      /* the constraint matrix should have no multiplets */
      if (check_mplets(rsm->A))
         fault("create_rsm: constraint matrix has multiplets");
      /* construct standard initial basis (all auxiliary variables are
         basic and all structural variables are non-basic; in this case
         B = I and N = -A') */
      for (i = 1; i <= m; i++)
      {  k = i; /* x[k] = xB[i] */
         rsm->posx[k] = +i;
         rsm->indb[i] =  k;
      }
      for (j = 1; j <= n; j++)
      {  k = m+j; /* x[k] = xN[j] */
         rsm->posx[k] = -j;
         rsm->indn[j] =  k;
         switch (rsm->type[k])
         {  case 'F':
               rsm->tagn[j] = 'F'; break;
            case 'L':
               rsm->tagn[j] = 'L'; break;
            case 'U':
               rsm->tagn[j] = 'U'; break;
            case 'D':
               rsm->tagn[j] =
                  (gnumabs(rsm->lb[k]) <= gnumabs(rsm->ub[k]) ? 'L' : 'U');
               break;
            case 'S':
               rsm->tagn[j] = 'S'; break;
            default:
               insist(rsm->type[k] != rsm->type[k]);
         }
      }
      /* check RSM for correctness */
      check_rsm(rsm);
      /* return to the calling program */
      return rsm;
}
#endif

/*----------------------------------------------------------------------
-- delete_rsm - delete revised simplex method common block.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- void delete_rsm(RSM *rsm);
--
-- *Description*
--
-- The routine delete_rsm() deletes the revised simplex method common
-- block, which the parameter rsm points to. */

void delete_rsm(RSM *rsm)
{     ufree(rsm->type);
      ufree(rsm->lb);
      ufree(rsm->ub);
      delete_mat(rsm->A);
      ufree(rsm->posx);
      ufree(rsm->indb);
      ufree(rsm->indn);
      ufree(rsm->tagn);
      inv_delete(rsm->inv);
      ufree(rsm);
      return;
}

/*----------------------------------------------------------------------
-- dual_col - choose non-basic variable (dual, standard technique).
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- int dual_col(RSM *rsm, int tagp, gnum_float ap[], gnum_float cbar[],
--    gnum_float tol);
--
-- *Description*
--
-- The dual_col routine chooses non-basic variable (xN)q (i.e. pivot
-- column of the simplex table) which should enter the basis on the next
-- iteration of the dual simplex method. The routine is based on the
-- standard (textbook) technique.
--
-- The parameter tagp is a tag that specifies to what subset xL or xU
-- the chosen basic variable (xB)p should be attributed after it has
-- left the basis. This tag is set by the dual_row routine.
--
-- The array ap should contain p-th (pivot) row if the simplex table,
-- i.e. p-th row of the matrix A~ in locations ap[1], ..., ap[n]. This
-- array is not changed on exit.
--
-- The array cbar should contain reduced costs of non-basic variables in
-- locations cbar[1], ..., cbar[n]. This array is not changed on exit.
--
-- The parameter tol is a relative tolerance (see below).
--
-- The dual_col routine implements the standard (textbook) ratio test
-- for choosing non-basic variable, i.e. the routine determines that
-- non-basic variable whose dual variable reaches its (lower or upper)
-- zero bound first when the dual variable (lambda_B)p that corresponds
-- to the chosen basic variable (xB)p is changing in the feasible
-- direction (increasing if (xB)p goes on its lower bound or decreasing
-- if (xB)p goes on its upper bound). Besides, the following additional
-- rules are used:
--
-- if |ap[j]| < tol * max|ap[*]|, i.e. if the influence coefficient
-- ap[j] is relatively close to zero, it is assumed that the
-- corresponding non-basic variable (xN)j doesn't affect on the basic
-- variable (xB)p and therefore such non-basic variable is not
-- considered to be chosen;
--
-- if the reduced cost cbar[j] of some non-basic variable (xN)j violates
-- its (zero) bound, it is assumed that this happens dur to round-off
-- errors and actually the reduced cost is exactly zero (because the
-- current basis solution should be primal feasible);
--
-- if several dual variables that correspond to non-basic variables
-- reach their (zero) bounds first at the same time, the routine prefers
-- that variable which has the largest (in absolute value) influence
-- coefficient.
--
-- For further details see the program documentation.
--
-- *Returns*
--
-- If the choice has been made, the dual_col routine returns q which is
-- a number of the chosen non-basic variable (xN)q, 1 <= q <= n.
-- Otherwise, if the dual variable (lambda_B)p that corresponds to the
-- chosen basic variable (xB)p can unlimitedly change in the feasible
-- direction and therefore the choice us impossible, the routine returns
-- zero. */

int dual_col(RSM *rsm, int tagp, gnum_float ap[], gnum_float cbar[], gnum_float tol)
{     int n = rsm->n, j, q;
      gnum_float big, eps, temp, teta;
      /* compute the absolute tolerance eps using the given relative
         tolerance tol */
      big = 0.0;
      for (j = 1; j <= n; j++)
         if (big < gnumabs(ap[j])) big = gnumabs(ap[j]);
      eps = tol * big;
      /* turn to the case of increasing xB[p] in order to simplify
         program logic */
      if (tagp == 'U') for (j = 1; j <= n; j++) ap[j] = - ap[j];
      /* initial settings */
      q = 0, teta = DBL_MAX, big = 0.0;
      /* look through the list of non-basic variables */
      for (j = 1; j <= n; j++)
      {  /* if the coefficient ap[j] is too small, it is assumed that
            xB[p] doesn't depend on xN[j] */
         if (ap[j] == 0.0 || gnumabs(ap[j]) < eps) continue;
         /* analyze main cases */
         if (rsm->tagn[j] == 'F')
         {  /* xN[j] is free variable */
            temp = 0.0;
         }
         else if (rsm->tagn[j] == 'L')
         {  /* xN[j] is on its lower bound */
            if (ap[j] < 0.0) continue;
            temp = cbar[j] / ap[j];
         }
         else if (rsm->tagn[j] == 'U')
         {  /* xN[j] is on its upper bound */
            if (ap[j] > 0.0) continue;
            temp = cbar[j] / ap[j];
         }
         else if (rsm->tagn[j] == 'S')
         {  /* xN[j] is fixed variable */
            continue;
         }
         else
            insist(rsm->tagn[j] != rsm->tagn[j]);
         /* if reduced cost of xN[j] (i.e. dual variable) slightly
            violates its bound, temp is negative; in this case it is
            assumed that reduced cost is exactly on its bound (i.e.
            equal to zero), therefore temp is replaced by zero */
         if (temp < 0.0) temp = 0.0;
         /* apply minimal ratio test */
         if (teta > temp || teta == temp && big < gnumabs(ap[j]))
         {  q = j;
            teta = temp;
            big = gnumabs(ap[j]);
         }
      }
      /* restore original signs of the coefficients ap[j] */
      if (tagp == 'U') for (j = 1; j <= n; j++) ap[j] = - ap[j];
      /* returns to the calling program */
      return q;
}

/*----------------------------------------------------------------------
-- dual_row - choose basic variable (dual).
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- int dual_row(RSM *rsm, gnum_float bbar[], gnum_float dvec[], int *tagp,
--    gnum_float tol);
--
-- *Description*
--
-- The dual_row routine chooses basic variable (xB)p (i.e. pivot row of
-- the simplex table) which should leave the basis on the next iteration
-- of the dual simplex method.
--
-- The array bbar should contain current values of basic variables xB in
-- locations bbar[1], ..., bbar[m]. This array is not changed on exit.
--
-- The array dvec should contain elements of the vector delta in
-- locations dvec[1], ..., dvec[m]. This array is not changed on exit.
-- It is allowed to specify NULL instead the array dvec; in this case
-- the routine assumes that all elements of the vector delta are equal
-- to one.
--
-- If the routine chooses some basic variable (xB)p, it stores into
-- location tagp a tag that specifies to what subset xL or xU the basic
-- variable should be attributed after it has left the basis. This tag
-- has the same meaning as the field rsm.tagn (see 'glprsm.h'). Thus,
-- the subset xL means that the basic variable (xB)p violates its lower
-- bound and therefore it should be set on its lower bound. Analogously,
-- the subset xU means that the basic variable (xB)p violates its upper
-- bound and therefore it should be set on its upper bound. Note that if
-- the basic variable (xB)p is of fixed type, it is considered as
-- gnum_float-bounded variable (with lower bound equal to upper bound),
-- therefore its tag should be corrected before changing the basis by
-- means of the change_b routine. This exception is used in order to let
-- other routines know what to do with such fixed basic variable:
-- increase (in case of xL) or decrease (in case of xU).
--
-- The parameter tol is a relative tolerance (see below).
--
-- The dual_row routine considers only those basic variables which
-- violate their bounds:
--
--    bbar[i] < (lB)i - eps  or
--    bbar[i] > (uB)i + eps
--
-- where
--
--    eps = tol * max(1, |(lB)i|) in case of lower bound
--    eps = tol * max(1, |(uB)i|) in case of upper bound
--
-- The routine chooses the basic variable (xB)p which has the largest
-- (in absolute value) *scaled* residual. Thus, if the vector delta is
-- not used, the choice made by the routine corresponds to the textbook
-- technique. Otherwise the choice corresponds to the steepest edge
-- technique.
--
-- *Returns*
--
-- If the choice has been made, the dual_row routine returns p which is
-- a number of the chosen basic variable (xB)p, 1 <= p <= m. Otherwise,
-- if the current basis solution is primal feasible and therefore the
-- choice is impossible, the routine returns zero. */

int dual_row(RSM *rsm, gnum_float bbar[], gnum_float dvec[], int *_tagp,
      gnum_float tol)
{     int m = rsm->m, i, k, p, tagp;
      gnum_float big, temp;
      p = 0, tagp = -1, big = 0.0;
      for (i = 1; i <= m; i++)
      {  k = rsm->indb[i]; /* x[k] = xB[i] */
         if (rsm->type[k] == 'L' || rsm->type[k] == 'D' ||
             rsm->type[k] == 'S')
         {  /* xB[i] has lower bound */
            if (check_rr(bbar[i], rsm->lb[k], tol) < -1)
            {  temp = rsm->lb[k] - bbar[i];
#if 0
               if (dvec != NULL) temp /= sqrtgnum(dvec[i]);
#else /* 3.0.4 */
               temp = (temp * temp) / (dvec == NULL ? 1.0 : dvec[i]);
#endif
               if (big < temp) p = i, tagp = 'L', big = temp;
            }
         }
         if (rsm->type[k] == 'U' || rsm->type[k] == 'D' ||
             rsm->type[k] == 'S')
         {  /* xB[i] has upper bound */
            if (check_rr(bbar[i], rsm->ub[k], tol) > +1)
            {  temp = bbar[i] - rsm->ub[k];
#if 0
               if (dvec != NULL) temp /= sqrtgnum(dvec[i]);
#else /* 3.0.4 */
               temp = (temp * temp) / (dvec == NULL ? 1.0 : dvec[i]);
#endif
               if (big < temp) p = i, tagp = 'U', big = temp;
            }
         }
      }
      *_tagp = tagp;
      return p;
}

/*----------------------------------------------------------------------
-- eval_bbar - compute values of basic variables.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- void eval_bbar(RSM *rsm, gnum_float bbar[]);
--
-- *Description*
--
-- The eval_bbar routine computes values of basic variables xB = beta =
-- = (beta_1, ..., beta_m) that correspond to the current basis solution
-- and stores beta_1, ..., beta_m into locations bbar[1], ..., bbar[m]
-- respectively.
--
-- The vector beta is computed using the following formula:
--
--    beta = - inv(B) * (N * xN) =
--         = inv(B) * (- N[1]*xN[1] - ... - N[n]*xN[n]),
--
-- where N[j] is the column of the expanded constraint matrix A, which
-- corresponds to the non-basic variable xN[j]. */

void eval_bbar(RSM *rsm, gnum_float bbar[])
{     ELEM *e;
      int m = rsm->m, n = rsm->n, i, j, k;
      gnum_float *u = bbar, t;
      /* u := - N*xN = - N[1]*xN[1] - ... - N[n]*xN[n] */
      for (i = 1; i <= m; i++) u[i] = 0.0;
      for (j = 1; j <= n; j++)
      {  t = eval_xn(rsm, j); /* current value of xN[j] */
         if (t == 0.0) continue;
         k = rsm->indn[j]; /* x[k] = xN[j] */
         for (e = rsm->A->col[k]; e != NULL; e = e->col)
            u[e->i] -= e->val * t;
      }
      /* bbar := inv(B)*u */
      ftran(rsm, u, 0);
      return;
}

/*----------------------------------------------------------------------
-- eval_cbar - compute reduced costs of non-basic variables.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- void eval_cbar(RSM *rsm, gnum_float c[], gnum_float pi[], gnum_float cbar[]);
--
-- *Description*
--
-- The eval_cbar routine computes reduced costs d = (d_1, ..., d_n) of
-- non-basic variables that correspond to the current basis solution and
-- stores d_1, ..., d_n to locations cbar[1], ..., cbar[n] respectively.
--
-- On entry the array c should contain the vector of coefficients of the
-- objective function in locations c[1], ..., c[m+n]. The array c is not
-- changed on exit.
--
-- On entry the array pi should contain the vector of simplex (Lagrange)
-- multipliers pi computed by means of the eval_pi routine for the same
-- vector c. The array pi is not changed on exit.
--
-- The vector d is computed using the following formula:
--
--    d[j] = cN[j] - pi' * N[j], j = 1, 2, ..., n,
--
-- where cN[j] is coefficient of the objective function at the variable
-- xN[j], pi is the vector of simplex multipliers, N[j] is the column of
-- the expanded constraint matrix A, which corresponds to the variable
-- xN[j]. */

void eval_cbar(RSM *rsm, gnum_float c[], gnum_float pi[], gnum_float cbar[])
{     ELEM *e;
      int n = rsm->n, j, k;
      /* cbar[j] = d[j] = cN[j] - pi * N[j] */
      for (j = 1; j <= n; j++)
      {  k = rsm->indn[j]; /* x[k] = xN[j] */
         cbar[j] = c[k];
         for (e = rsm->A->col[k]; e != NULL; e = e->col)
            cbar[j] -= pi[e->i] * e->val;
      }
      return;
}

/*----------------------------------------------------------------------
-- eval_col - compute column of simplex table.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- void eval_col(RSM *rsm, int j, gnum_float aj[], int save);
--
-- *Description*
--
-- The eval_col routine computes j-th column of the simplex table, i.e.
-- j-th column of the matrix A~ = -inv(B)*N, and stores its elements to
-- locations aj[1], ..., aj[m] respectively.
--
-- The parameter save is a flag. If this flag is set, it means that the
-- computed column is the column of non-basic variable (xN)q which has
-- been chosen to enter the basis (i.e. j = q). This flag is passed to
-- the ftran routine which is called by the eval_col routine in order to
-- perform forward transformation.
--
-- The j-th column of the simplex table is computed using the following
-- formula:
--
--    A~[j] = - inv(B) * N[j],
--
-- where B is the current basis matrix, N[j] is column of the expanded
-- matrix A, which corresponds to non-basic variable (xN)j. */

void eval_col(RSM *rsm, int j, gnum_float aj[], int save)
{     ELEM *e;
      int m = rsm->m, n = rsm->n, i, k;
      gnum_float *u = aj;
      insist(1 <= j && j <= n);
      k = rsm->indn[j]; /* x[k] = xN[j] */
      /* u = N[j] */
      for (i = 1; i <= m; i++) u[i] = 0.0;
      for (e = rsm->A->col[k]; e != NULL; e = e->col)
         u[e->i] = + e->val;
      /* aj = - inv(B) * u */
      ftran(rsm, u, save);
      for (i = 1; i <= m; i++) aj[i] = - u[i];
      return;
}

/*----------------------------------------------------------------------
-- eval_pi - compute simplex multipliers.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- void eval_pi(RSM *rsm, gnum_float c[], gnum_float pi[]);
--
-- *Description*
--
-- The eval_pi routine computes simplex multipliers pi = (pi_1, ...,
-- pi_m), i.e. Lagrange multipliers for the equality constraints, that
-- correspond to the current basis and stores pi_1, ..., pi_m into
-- locations pi[1], ..., pi[m] respectively.
--
-- On entry the array c should contain the vector of coefficients of the
-- objective function in locations c[1], ..., c[m+n]. The array c is not
-- changed on exit.
--
-- The vector pi is computed using the following formula:
--
--    pi = inv(B') * cB,
--
-- where B' is a matrix transposed to the basis matrix B, cB is the
-- subvector of coefficients of the objective function at the basic
-- variables. */

void eval_pi(RSM *rsm, gnum_float c[], gnum_float pi[])
{     int m = rsm->m, i, k;
      gnum_float *cB = pi;
      /* pi = inv(BT) * cB */
      for (i = 1; i <= m; i++)
      {  k = rsm->indb[i]; /* x[k] = xB[i] */
         cB[i] = c[k];
      }
      btran(rsm, pi);
      return;
}

/*----------------------------------------------------------------------
-- eval_row - compute row of simplex table.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- void eval_row(RSM *rsm, gnum_float zeta[], gnum_float ai[]);
--
-- *Description*
--
-- The eval_row routine computes i-th row of the simplex table, i.e.
-- i-th row of the matrix A~ = -inv(B)*N, and stores its elements into
-- locations ai[1], ..., ai[n] respectively.
--
-- The array zeta should contain i-th row of the inverse inv(B), where
-- B is the current basis matrix, computed by means of the eval_zeta
-- routine. This array is not changed on exit.
--
-- The i-th row of the simplex table is computed using the following
-- formula:
--
--    a~[i] = - N' * zeta,
--
-- where N' is a matrix transposed to N, N is the submatrix formed by
-- non-basic columns of the expanded matrix A, zeta is i-th row of the
-- inverse inv(B). */

void eval_row(RSM *rsm, gnum_float zeta[], gnum_float ai[])
{     ELEM *e;
      int n = rsm->n, j , k;
      for (j = 1; j <= n; j++)
      {  k = rsm->indn[j]; /* x[k] = xN[j] */
         ai[j] = 0.0;
         for (e = rsm->A->col[k]; e != NULL; e = e->col)
            ai[j] -= e->val * zeta[e->i];
      }
      return;
}

/*----------------------------------------------------------------------
-- eval_xn - determine value of non-basic variable.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- gnum_float eval_xn(RSM *rsm, int j);
--
-- *Returns*
--
-- The eval_xn routine returns the value of non-basic variable xN[j],
-- 1 <= j <= n, that corresponds to the current basis solution. */

gnum_float eval_xn(RSM *rsm, int j)
{     int n = rsm->n, k;
      gnum_float t;
      insist(1 <= j && j <= n);
      k = rsm->indn[j]; /* x[k] = xN[j] */
      switch (rsm->tagn[j])
      {  case 'L':
            /* xN[j] on its lower bound */
            t = rsm->lb[k];
            break;
         case 'U':
            /* xN[j] on its upper bound */
            t = rsm->ub[k];
            break;
         case 'F':
            /* xN[j] is free variable */
            t = 0.0;
            break;
         case 'S':
            /* xN[j] is fixed variable */
            t = rsm->lb[k];
            break;
         default:
            insist(rsm->tagn[j] != rsm->tagn[j]);
      }
      return t;
}

/*----------------------------------------------------------------------
-- eval_zeta - compute row of the inverse.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- void eval_zeta(RSM *rsm, int i, gnum_float zeta[]);
--
-- *Description*
--
-- The eval_zeta routine computes i-th row of the inverse, i.e. i-th
-- row of the matrix inv(B), where B is the current basis matrix, and
-- stores its elements into locations zeta[1], ..., zeta[m].
--
-- The i-th row of inv(B) is computed using the following formula:
--
--    zeta = inv(B') * e[i],
--
-- where B' is a matrix transposed to the current basis matrix B, e[i]
-- is the unity vector containing one in i-th position. */

void eval_zeta(RSM *rsm, int i, gnum_float zeta[])
{     int m = rsm->m, j;
      insist(1 <= i && i <= m);
      for (j = 1; j <= m; j++) zeta[j] = 0.0;
      zeta[i] = 1.0;
      btran(rsm, zeta);
      return;
}

/*----------------------------------------------------------------------
-- exact_dvec - compute exact value of delta[i].
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- gnum_float exact_dvec(RSM *rsm, int i);
--
-- *Description*
--
-- The exact_dvec routine computes exact value of delta[i] using its
-- definition:
--
--    delta[i] = 1 + alfa[i,1]^2 + ... + alfa[i,n]^2
--
-- where alfa[i,j] is the element of the current simplex table placed
-- in i-th row and j-th column.
--
-- This operation is extremely inefficient and may be used only for
-- debugging purposes.
--
-- *Returns*
--
-- The exact_dvec routine returns the computed value of delta[i]. */

gnum_float exact_dvec(RSM *rsm, int i)
{     int m = rsm->m, n = rsm->n, j;
      gnum_float *zeta, *ai, t;
      insist(1 <= i && i <= m);
      zeta = ucalloc(1+m, sizeof(gnum_float));
      ai = ucalloc(1+n, sizeof(gnum_float));
      eval_zeta(rsm, i, zeta);
      eval_row(rsm, zeta, ai);
      t = 1.0;
      for (j = 1; j <= n; j++) t += ai[j] * ai[j];
      ufree(zeta), ufree(ai);
      return t;
}

/*----------------------------------------------------------------------
-- exact_gvec - compute exact value of gamma[j].
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- gnum_float exact_gvec(RSM *rsm, int j);
--
-- *Description*
--
-- The exact_gvec routine computes exact value of gamma[j] using its
-- definition:
--
--    gamma[j] = 1 + alfa[1,j]^2 + ... + alfa[m,j]^2
--
-- where alfa[i,j] is the element of the current simplex table placed
-- in i-th row and j-th column.
--
-- This operation is extremely inefficient and may be used only for
-- debugging purposes.
--
-- *Returns*
--
-- The exact_gvec routine returns the computed value of gamma[j]. */

gnum_float exact_gvec(RSM *rsm, int j)
{     int m = rsm->m, n = rsm->n, i;
      gnum_float *aj, t;
      insist(1 <= j && j <= n);
      aj = ucalloc(1+m, sizeof(gnum_float));
      eval_col(rsm, j, aj, 0);
      t = 1.0;
      for (i = 1; i <= m; i++) t += aj[i] * aj[i];
      ufree(aj);
      return t;
}

/*----------------------------------------------------------------------
-- ftran - perform forward transformation.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- void ftran(RSM *rsm, gnum_float u[], int save);
--
-- *Description*
--
-- The ftran routine performs forward transformation (FTRAN) of the
-- vector u using some representation of the current basis matrix.
--
-- This operation means solving the system B*u' = u, where B is the
-- current basis matrix, u is the given vector which should be
-- transformed, u' is the resultant vector.
--
-- On entry the array u should contain elements of the vector u in
-- locations u[1], u[2], ..., u[m], where m is order of the matrix B.
-- On exit this array contains elements of the vector u' in the same
-- locations.
--
-- The parameter save is a flag. If this flag is set, it means that the
-- vector u is the column of non-basic variable (xN)q which has been
-- chosen to enter the basis (i.e. u = Nq).In this case the ftran
-- routine saves some internal information which will be used further
-- by the update_b routine in order to update the representation of the
-- basis matrix for the adjacent basis. It is assumed that the calling
-- program should perform at least one call to the ftran routine with
-- the save flag set before subsequent call to the update_b routine. */

void ftran(RSM *rsm, gnum_float u[], int save)
{     inv_ftran(rsm->inv, u, save);
      return;
}

/*----------------------------------------------------------------------
-- harris_col - choose non-basic variable (dual, Harris technique).
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- int harris_col(RSM *rsm, int tagp, gnum_float ap[], gnum_float c[],
--    gnum_float cbar[], gnum_float tol, gnum_float tol1);
--
-- *Description*
--
-- The harris_col routine chooses non-basic variable (xN)q (i.e. pivot
-- column of the simplex table) which should enter the basis on the next
-- iteration of the dual simplex method. The routine is based on the
-- two-pass ratio test proposed by P.Harris.
--
-- The harris_col routine has analogous program specification as the
-- dual_col routine.
--
-- The first additional parameter is the array c which should contain
-- the expanded vector c of coefficients of the objective function in
-- locations c[1], ..., c[m+n]. It is assumed that the reduced costs are
-- computed using the vector c passed to this routine.
--
-- The second additional parameter is tol1 which is a relative tolerance
-- used for relaxing zero bounds of dual variables lambda_N (that
-- correspond to primal non-basic variables xN) on the first pass. The
-- routine replaces original zero bounds of dual variables by their
-- relaxed bounds:
--
--    (lambda_N)j >= -eps  or  (lambda_N)j <= +eps
--
-- where
--
--    eps = tol1 * max(1, |(cN)j|)
--
-- (the absolute tolerance eps reflects that the reduced cost on
-- non-basic variable (xN)j is the difference dj = (cN)j - pi'*Nj).
--
-- For futher details see the program documentation. */

int harris_col(RSM *rsm, int tagp, gnum_float ap[], gnum_float c[],
      gnum_float cbar[], gnum_float tol, gnum_float tol1)
{     int n = rsm->n, j, q;
      gnum_float big, eps, temp, teta;
#if 0
#     define gap (tol1 * (gnumabs(c[k]) > 1.0 ? gnumabs(c[k]) : 1.0))
#else /* 3.0.3 */
#     define gap tol1
      insist(c == c);
#endif
      /* compute the absolute tolerance eps using the given relative
         tolerance tol */
      big = 0.0;
      for (j = 1; j <= n; j++)
         if (big < gnumabs(ap[j])) big = gnumabs(ap[j]);
      eps = tol * big;
      /* turn to the case of increasing xB[p] in order to simplify
         program logic */
      if (tagp == 'U') for (j = 1; j <= n; j++) ap[j] = - ap[j];
      /* initial settings for the first pass */
      teta = DBL_MAX;
      /* the first look through the list of non-basic variables */
      for (j = 1; j <= n; j++)
      {  /* if the coefficient ap[j] is too small, it is assumed that
            xB[p] doesn't depend on xN[j] */
         if (ap[j] == 0.0 || gnumabs(ap[j]) < eps) continue;
         /* analyze main cases */
#if 0
         k = rsm->indn[j]; /* x[k] = xN[j] */
#endif
         if (rsm->tagn[j] == 'F')
         {  /* xN[j] is free variable */
            if (ap[j] > 0.0) goto lo; else goto up;
         }
         else if (rsm->tagn[j] == 'L')
         {  /* xN[j] is on its lower bound */
            if (ap[j] < 0.0) continue;
lo:         temp = (cbar[j] + gap) / ap[j];
         }
         else if (rsm->tagn[j] == 'U')
         {  /* xN[j] is on its upper bound */
            if (ap[j] > 0.0) continue;
up:         temp = (cbar[j] - gap) / ap[j];
         }
         else if (rsm->tagn[j] == 'S')
         {  /* xN[j] is fixed variable */
            continue;
         }
         else
            insist(rsm->tagn[j] != rsm->tagn[j]);
         /* if reduced costs of xN[j] (i.e. dual variable) slightly
            violates its bound, temp is negative; in this case it is
            assumed that reduced cost is exactly on its bound (i.e.
            equal to zero), therefore temp is replaced by zero */
         if (temp < 0.0) temp = 0.0;
         /* compute maximal allowable change of reduced cost of the
            basis variable xB[p] */
         if (teta > temp) teta = temp;

      }
      /* initial settings for the second pass */
      q = 0, big = 0.0;
      /* the second look through the list of non-basis variables */
      for (j = 1; j <= n; j++)
      {  /* if the coefficient ap[j] is too small, it is assumed that
            xB[p] doesn't depend on xN[j] */
         if (ap[j] == 0.0 || gnumabs(ap[j]) < eps) continue;
         /* analyze main cases */
         if (rsm->tagn[j] == 'F')
         {  /* xN[j] is free variable */
            temp = 0.0;
         }
         else if (rsm->tagn[j] == 'L')
         {  /* xN[j] is on its lower bound */
            if (ap[j] < 0.0) continue;
            temp = cbar[j] / ap[j];
         }
         else if (rsm->tagn[j] == 'U')
         {  /* xN[j] is on its upper bound */
            if (ap[j] > 0.0) continue;
            temp = cbar[j] / ap[j];
         }
         else if (rsm->tagn[j] == 'S')
         {  /* xN[j] is fixed variable */
            continue;
         }
         else
            insist(rsm->tagn[j] != rsm->tagn[j]);
         /* if reduced costs of xN[j] (i.e. dual variable) slightly
            violates its bound, temp is negative; in this case it is
            assumed that reduced cost is exactly on its bound (i.e.
            equal to zero), therefore temp is replaced by zero */
         if (temp < 0.0) temp = 0.0;
         /* apply the dual version of Harris' rule */
         if (temp <= teta &&  big < gnumabs(ap[j]))
         {  q = j;
            big = gnumabs(ap[j]);
         }
      }
      /* restore original signs of the coefficients ap[j] */
      if (tagp == 'U') for (j = 1; j <= n; j++) ap[j] = - ap[j];
      /* returns to the calling program */
      return q;
#     undef gap
}

/*----------------------------------------------------------------------
-- harris_row - choose basic variable (primal, Harris technique).
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- int harris_row(RSM *rsm, int q, int dir, gnum_float aq[], gnum_float bbar[],
--    int *tagp, gnum_float tol, gnum_float tol1);
--
-- *Description*
--
-- The harris_row routine chooses basic variable (xB)p (i.e. pivot row
-- of the simplex table) which should leave the basis on the next
-- iteration of the primal simplex method. The routine is based on the
-- two-pass ratio test proposed by P.Harris.
--
-- The harris_row routine has the same program specification as the
-- pivit_row routine.
--
-- The only difference is the additional parameter tol1 which is a
-- relative tolerance used for relaxing bounds of basic variables on the
-- first pass. The routine replaces original bounds of basic variables
-- by their relaxed bounds:
--
--    (lB)i - eps <= (xB)i <= (uB)i + eps,
--
-- where
--
--    eps1 = tol * max(1, |(lB)i|) in case of lower bound
--    eps1 = tol * max(1, |(uB)i|) in case of upper bound
--
-- For futher details see the program documentation. */

int harris_row(RSM *rsm, int q, int dir, gnum_float aq[], gnum_float bbar[],
      int *_tagp, gnum_float tol, gnum_float tol1)
{     int m = rsm->m, i, k, tag, p, tagp;
      gnum_float *lb = rsm->lb, *ub = rsm->ub;
      gnum_float big, eps, temp, teta;
#if 0
#     define gap(bnd) (tol1 * (gnumabs(bnd) > 1.0 ? gnumabs(bnd) : 1.0))
#else /* 3.0.3 */
#     define gap(bnd) tol1
#endif
      /* compute the absolute tolerance eps using the given relative
         tolerance tol */
      big = 0.0;
      for (i = 1; i <= m; i++)
         if (big < gnumabs(aq[i])) big = gnumabs(aq[i]);
      eps = tol * big;
      /* turn to the case of increasing xN[q] in order to simplify
         program logic */
      if (dir) for (i = 1; i <= m; i++) aq[i] = - aq[i];
      /* initial settings for the first pass */
      k = rsm->indn[q]; /* x[k] = xN[q] */
      if (rsm->type[k] == 'D')
         teta = (ub[k] + gap(ub[k])) - (lb[k] - gap(lb[k]));
      else
         teta = DBL_MAX;
      /* the first look through the list of basis variables */
      for (i = 1; i <= m; i++)
      {  /* if the coefficient aq[i] is too small, it is assumed that
            xB[i] doesn't depend on xN[q] */
         if (aq[i] == 0.0 || gnumabs(aq[i]) < eps) continue;
         /* analyze main cases */
         k = rsm->indb[i]; /* x[k] = xB[i] */
         if (rsm->type[k] == 'F')
         {  /* xB[i] is free variable */
            continue;
         }
         else if (rsm->type[k] == 'L')
         {  /* xB[i] has lower bound */
            if (aq[i] > 0.0) continue;
lo_1:       temp = ((lb[k] - gap(lb[k])) - bbar[i]) / aq[i];
         }
         else if (rsm->type[k] == 'U')
         {  /* xB[i] has upper bound */
            if (aq[i] < 0.0) continue;
up_1:       temp = ((ub[k] + gap(ub[k])) - bbar[i]) / aq[i];
         }
         else if (rsm->type[k] == 'D')
         {  /* xB[i] has both lower and upper bounds */
            if (aq[i] < 0.0) goto lo_1; else goto up_1;
         }
         else if (rsm->type[k] == 'S')
         {  /* xB[i] is fixed variable */
            if (aq[i] < 0.0) goto lo_1; else goto up_1;
         }
         else
            insist(rsm->type[k] != rsm->type[k]);
         /* if xB[i] slightly violates its (relaxed!) bound, temp is
            negative; in this case it is assumes thst xB[i] is exactly
            on its (relaxed!) bound, so temp is replaced by zero */
         if (temp < 0.0) temp = 0.0;
         /* compute maximal allowable change of xN[q] */
         if (teta > temp) teta = temp;
      }
      /* initial settings for the second pass */
      p = 0, tagp = -1, big = 0.0;
      k = rsm->indn[q]; /* x[k] = xN[q] */
      if (rsm->type[k] == 'D')
      {  temp = ub[k] - lb[k];
         if (temp <= teta) p = -1, tagp = -1, big = 1.0;
      }
      /* the second look through the list of the basis variable */
      for (i = 1; i <= m; i++)
      {  /* if the coefficient aq[i] is too small, it is assumed that
            xB[i] doesn't depend on xN[q] */
         if (aq[i] == 0.0 || gnumabs(aq[i]) < eps) continue;
         /* analyze main cases */
         k = rsm->indb[i]; /* x[k] = xB[i] */
         if (rsm->type[k] == 'F')
         {  /* xB[i] is free variable */
            continue;
         }
         else if (rsm->type[k] == 'L')
         {  /* xB[i] has lower bound */
            if (aq[i] > 0.0) continue;
lo_2:       temp = (lb[k] - bbar[i]) / aq[i];
            tag = 'L';
         }
         else if (rsm->type[k] == 'U')
         {  /* xB[i] has upper bound */
            if (aq[i] < 0.0) continue;
up_2:       temp = (ub[k] - bbar[i]) / aq[i];
            tag = 'U';
         }
         else if (rsm->type[k] == 'D')
         {  /* xB[i] has both lower and upper bounds */
            if (aq[i] < 0.0) goto lo_2; else goto up_2;
         }
         else if (rsm->type[k] == 'S')
         {  /* xB[i] is fixed variable */
            temp = 0.0;
            tag = 'S';
         }
         else
            insist(rsm->type[k] != rsm->type[k]);
         /* if xB[i] slightly violates its (original!) bound, temp is
            negative; in this case it is assumed that xB[i] is exactly
            on its (original!) bound, so temp is replaced by zero */
         if (temp < 0.0) temp = 0.0;
         /* apply the Harris' rule */
         if (temp <= teta && big < gnumabs(aq[i]))
            p = i, tagp = tag, big = gnumabs(aq[i]);
      }
      /* restore original signs of the coefficient aq[i] */
      if (dir) for (i = 1; i <= m; i++) aq[i] = - aq[i];
      /* store a tag for xB[p] */
      *_tagp = tagp;
      /* return to the calling program */
      return p;
#     undef gap
}

/*----------------------------------------------------------------------
-- init_dvec - initialize the vector delta.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- void init_dvec(RSM *rsm, gnum_float dvec[]);
--
-- *Description*
--
-- The init_dvec computes the vector delta for the initial basis and
-- stores its elements into locations dvec[1], ..., dvec[m].
--
-- Initial basis is a basis where all auxiliary variables are basic and
-- all structural variables are non-basic. In this special case B = I
-- and N = A^, therefore alfa[i,j] = a^[i,j], where a^[i,j] are elements
-- of the original constraint matrix A^, that allows using the formula
--
--    dvec[i] = 1 + alfa[i,1]^2 + ... + alfa[i,n]^2
--
-- directly for computing the initial vector delta. */

void init_dvec(RSM *rsm, gnum_float dvec[])
{     ELEM *e;
      int m = rsm->m, n = rsm->n, i, j;
      for (i = 1; i <= m; i++) dvec[i] = 1.0;
      for (j = 1; j <= n; j++)
      {  for (e = rsm->A->col[m+j]; e != NULL; e = e->col)
            dvec[e->i] += e->val * e->val;
      }
      return;
}

/*----------------------------------------------------------------------
-- init_gvec - initialize the vector gamma.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- void init_gvec(RSM *rsm, gnum_float gvec[]);
--
-- *Description*
--
-- The init_gvec computes the vector gamma for the initial basis and
-- stores its elements into locations gvec[1], ..., gvec[n].
--
-- Initial basis is a basis where all auxiliary variables are basic and
-- all structural variables are non-basic. In this special case B = I
-- and N = A^, therefore alfa[i,j] = a^[i,j], where a^[i,j] are elements
-- of the original constraint matrix A^, that allows using the formula
--
--    gvec[j] = 1 + alfa[1,j]^2 + ... + alfa[m,j]^2
--
-- directly for computing the initial vector gamma. */

void init_gvec(RSM *rsm, gnum_float gvec[])
{     ELEM *e;
      int m = rsm->m, n = rsm->n, j;
      gnum_float t;
      for (j = 1; j <= n; j++)
      {  t = 1.0;
         for (e = rsm->A->col[m+j]; e != NULL; e = e->col)
            t += e->val * e->val;
         gvec[j] = t;
      }
      return;
}

/*----------------------------------------------------------------------
-- invert_b - rebuild representation of basis matrix.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- int invert_b(RSM *rsm);
--
-- *Description*
--
-- The invert_b routine reinverts the basis matrix B, i.e. it rebuilds
-- anew some representation of the basis matrix.
--
-- This operation can be used in the following cases:
--
-- a) if it is necessary to rebuild the representation for some initial
--    basis matrix;
--
-- b) if the representation of the current basis matrix has become too
--    large or inaccurate;
--
-- c) if the basis matrix has been completely changed.
--
-- The invert_b routine assumes that the array rsm.indb specifies what
-- columns of the expanded matrix A belong to the basis matrix B.
--
-- *Returns*
--
-- If the operation was successful, the invert_b routine returns zero.
-- Otherwise the routine returns non-zero. The latter case means that
-- the basis matrix is numerically singular or ill-conditioned (for
-- futher information see descriptions of particular routines that
-- perform this operation). */

static int col(void *info, int j, int rn[], gnum_float bj[])
{     /* build j-th column of the current basis matrix */
      RSM *rsm = (RSM *)info;
      ELEM *e;
      int nz = 0;
      for (e = rsm->A->col[rsm->indb[j]]; e != NULL; e = e->col)
         if (e->val != 0) nz++, rn[nz] = e->i, bj[nz] = e->val;
      return nz;
}

int invert_b(RSM *rsm)
{     static gnum_float tol[1+3] = { 0.00, 0.10, 0.30, 0.70 };
      int try, ret;
      for (try = 1; try <= 3; try++)
      {  rsm->inv->luf->piv_tol = tol[try];
         ret = inv_decomp(rsm->inv, rsm, col);
         if (ret == 0) break;
      }
      return ret;
}

/*----------------------------------------------------------------------
-- pivot_col - choose non-basic variable (primal).
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- int pivot_col(RSM *rsm, gnum_float c[], gnum_float cbar[], gnum_float gvec[],
--    gnum_float tol);
--
-- *Description*
--
-- The pivot_col routine chooses non-basic variable (xN)q (i.e. pivot
-- column of the simplex table) which should enter the basis on the next
-- iteration of the primal simplex method. Note that the routine assumes
-- that the objective function should be *minimized*.
--
-- The array c should contain the expanded vector of coefficients of
-- the objective function in locations c[1], ..., c[m+n]. This array is
-- not changed on exit.
--
-- The array cbar should contain reduced costs of non-basic variables in
-- locations cbar[1], ..., cbar[n]. This array is not changed on exit.
--
-- The array gvec should contain elements of the vector gamma in
-- locations gvec[1], ..., gvec[n]. This array is not changed on exit.
-- It is allowed to specify NULL instead the array gvec; in this case
-- the routine assumes that all elements of the vector gamma are equal
-- to one.
--
-- The parameter tol is a relative tolerance (see below).
--
-- The pivot_col routine considers only those non-basic variables which
-- changing in feasible direction can improve (decrease) the objective
-- function, i.e. for which
--
--    (xN)j in xF and |dj| >  eps, or
--    (xN)j in xL and  dj  < -eps, or
--    (xN)j in xU and  dj  > +eps
--
-- where
--
--    eps = tol * max(1, |(cN)j|)
--
-- (the absolute tolerance eps reflects that the reduced cost on
-- non-basic variable (xN)j is the difference dj = (cN)j - pi'*Nj).
--
-- The routine chooses the non-basic variable (xN)q which has the
-- largest (in absolute value) scaled reduced cost
--
--    d'q = dq / sqrtgnum(gamma_q)
--
-- Thus, if the vector gamma is not used, the choice made by the routine
-- corresponds to the textbook pricing. Otherwise the choice corresponds
-- to the steepest edge pricing.
--
-- *Returns*
--
-- If the choice has been made, the pivot_col routine returns q which
-- is a number of the chosen non-basic variable (xN)q, 1 <= q <= n.
-- Otherwise, if the current basis solution is dual feasible and the
-- choice is impossible, the routine returns zero. */

int pivot_col(RSM *rsm, gnum_float c[], gnum_float cbar[], gnum_float gvec[],
      gnum_float tol)
{     int n = rsm->n, j, k, q, ret;
      gnum_float big, temp;
      q = 0, big = 0.0;
      for (j = 1; j <= n; j++)
      {  /* skip column if xN[j] can't change */
         if (rsm->tagn[j] == 'S') continue;
         /* skip column if xN[j] doesn't affect on the obj. func. */
         if (cbar[j] == 0.0) continue;
         k = rsm->indn[j]; /* x[k] = xN[j] */
         ret = check_rr(cbar[j] + c[k], c[k], tol);
         switch (rsm->tagn[j])
         {  case 'F':
               /* xN[j] can change in any direction */
               if (-1 <= ret && ret <= +1) continue;
               break;
            case 'L':
               /* xN[j] can increase */
               if (ret >= -1) continue;
               break;
            case 'U':
               /* xN[j] can decrease */
               if (ret <= +1) continue;
               break;
            default:
               insist(rsm->tagn[j] != rsm->tagn[j]);
         }
         /* xN[j] can improve (decrease) the objective function */
#if 0
         if (gvec == NULL)
            temp = gnumabs(cbar[j]);
         else
            temp = gnumabs(cbar[j]) / sqrtgnum(gvec[j]);
#else /* 3.0.4 */
         temp = (cbar[j] * cbar[j]) / (gvec == NULL ? 1.0 : gvec[j]);
#endif
         if (big < temp) q = j, big = temp;
      }
      return q;
}

/*----------------------------------------------------------------------
-- pivot_row - choose basic variable (primal, standard technique).
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- int pivot_row(RSM *rsm, int q, int dir, gnum_float aq[], gnum_float bbar[],
--    int *tagp, gnum_float tol);
--
-- *Description*
--
-- The pivot_row routine chooses basic variable (xB)p (i.e. pivot row of
-- the simplex table) which should leave the basis on the next iteration
-- of the primal simplex method. The routine is based on the standard
-- (textbook) ratio test.
--
-- The parameter q specifies a number of the non-basic variable (xN)q
-- which has been chosen to enter the basis, 1 <= q <= n.
--
-- The parameter dir specifies in what direction the non-basic variable
-- (xN)q is changing:
--
-- 0, if (xN)q is increasing;
--
-- 1, if (xN)q is decreasing.
--
-- The array aq should contain q-th (pivot) column of the simplex table
-- (i.e. of the matrix A~) in locations aq[1], ..., aq[m].
--
-- The array bbar should contain current values of basic variables xB in
-- locations bbar[1], ..., bbar[m].
--
-- If the routine chooses some basic variable (xB)p, it stores into
-- location tagp a tag that specifies to what subset xL, xU, or xS the
-- basic variable should be attributed after it has left the basis. This
-- tag has the same meaning as the field rsm.tagn (see 'glprsm.h').
--
-- The parameter tol is a relative tolerance (see below).
--
-- The pivot_row routine implements the standard (textbook) ratio test
-- for choosing basic variable, i.e. the routine determines that basic
-- variable which reaches its (lower or upper) bound first when the
-- non-basic variable (xN)q is changing in the feasible direction.
-- Besides, the following additional rules are used:
--
-- if |aq[i]| < tol * max|aq[*]|, i.e. if the influence coefficient
-- aq[i] is relatively close to zero, it is assumed that the
-- corresponding basic variable (xB)i doesn't depend on the non-basic
-- variable (xN)q and therefore such basic variable is not considered to
-- be chosen;
--
-- if current value bbar[i] of some basic variable (xB)i violates its
-- bound, it is assumed that this happens dur to round-off errors and
-- actually the basic variable is exactly on its bound (because the
-- current basis solution should be primal feasible);
--
-- if several basic variables reach their bounds first at the same time,
-- the routine prefers that variable which has the largest (in absolute
-- value) influence coefficient.
--
-- For further details see the program documentation.
--
-- *Returns*
--
-- If the choice has been made, the pivot_row routine returns p which
-- is a number of the chosen basic variable (xB)p, 1 <= p <= m. In the
-- special case, if the non-basic variable (xN)q being gnum_float-bounded
-- variable reaches its opposite bound before any basic variable, the
-- routine returns a negative value. Otherwise, if the non-basic
-- variable (xN)q can unlimitedly change in the feasible direction and
-- therefore the choice is impossible, the routine returns zero. */

int pivot_row(RSM *rsm, int q, int dir, gnum_float aq[], gnum_float bbar[],
      int *_tagp, gnum_float tol)
{     int m = rsm->m, i, k, tag, p, tagp;
      gnum_float big, eps, temp, teta;
      /* compute the absolute tolerance eps using the given relative
         tolerance tol */
      big = 0.0;
      for (i = 1; i <= m; i++)
         if (big < gnumabs(aq[i])) big = gnumabs(aq[i]);
      eps = tol * big;
      /* turn to the case of increasing xN[q] in order to simplify
         program logic */
      if (dir) for (i = 1; i <= m; i++) aq[i] = - aq[i];
      /* initial settings */
      k = rsm->indn[q]; /* x[k] = xN[q] */
      if (rsm->type[k] == 'D')
      {  p = -1;
         tagp = -1;
         teta = rsm->ub[k] - rsm->lb[k];
         big = 1.0;
      }
      else
      {  p = 0;
         tagp = -1;
         teta = DBL_MAX;
         big = 0.0;
      }
      /* look through the list of basic variables */
      for (i = 1; i <= m; i++)
      {  /* if the coefficient aq[i] is too small, it is assumed that
            xB[i] doesn't depend on xN[q] */
         if (aq[i] == 0.0 || gnumabs(aq[i]) < eps) continue;
         /* analyze main cases */
         k = rsm->indb[i]; /* x[k] = xB[i] */
         if (rsm->type[k] == 'F')
         {  /* xB[i] is free variable */
            continue;
         }
         else if (rsm->type[k] == 'L')
         {  /* xB[i] has lower bound */
            if (aq[i] > 0.0) continue;
lo:         temp = (rsm->lb[k] - bbar[i]) / aq[i];
            tag = 'L';
         }
         else if (rsm->type[k] == 'U')
         {  /* xB[i] has upper bound */
            if (aq[i] < 0.0) continue;
up:         temp = (rsm->ub[k] - bbar[i]) / aq[i];
            tag = 'U';
         }
         else if (rsm->type[k] == 'D')
         {  /* xB[i] has both lower and upper bounds */
            if (aq[i] < 0.0) goto lo; else goto up;
         }
         else if (rsm->type[k] == 'S')
         {  /* xB[i] is fixed variable */
            temp = 0.0;
            tag = 'S';
         }
         else
            insist(rsm->type[k] != rsm->type[k]);
         /* if xB[i] slightly violates its bound, temp is negative;
            in this case it is assumed that xB[i] is exactly on its
            bound, therefore temp is replaced by zero */
         if (temp < 0.0) temp = 0.0;
         /* apply minimal ratio test */
         if (teta > temp || teta == temp && big < gnumabs(aq[i]))
         {  p = i;
            tagp = tag;
            teta = temp;
            big = gnumabs(aq[i]);
         }
      }
      /* restore original signs of the coefficients aq[i] */
      if (dir) for (i = 1; i <= m; i++) aq[i] = - aq[i];
      /* store a tag for xB[p] */
      *_tagp = tagp;
      /* return to the calling program */
      return p;
}

/*----------------------------------------------------------------------
-- scale_rsm - scale problem components in RSM block.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- void scale_rsm(RSM *rsm, gnum_float R[], gnum_float S[]);
--
-- *Description*
--
-- The routine scale_rsm() performs scaling problem components in the
-- RSM block, which the parameter rsm points to. These components are
-- bounds of variables and constraint coefficients.
--
-- The array R specifies a row scaling matrix. Diagonal elements of this
-- matrix should be placed in locations R[1], R[2], ..., R[m], where m
-- is number of rows (auxiliary variables). R may be NULL, in which case
-- rows are not scaled as if R would be unity matrix.
--
-- The array S specifies a column scaling matrix. Diagonal elements of
-- this matrix should be placed in locations S[1], S[2], ..., S[n],
-- where n is number of columns (structural variables). S may be NULL,
-- in which case columns are not scaled as if S would be unity matrix.
--
-- The purpose of scaling is to replace the original constraint matrix
-- A by the scaled matrix A' = R * A * S. */

void scale_rsm(RSM *rsm, gnum_float R[], gnum_float S[])
{     int m = rsm->m, n = rsm->n, i, j, k;
      /* scale bounds of auxiliary variables */
      if (R != NULL)
      {  for (i = 1; i <= m; i++)
         {  k = i;
            rsm->lb[k] *= R[i];
            rsm->ub[k] *= R[i];
         }
      }
      /* scale bounds of structural variables */
      if (S != NULL)
      {  for (j = 1; j <= n; j++)
         {  k = m + j;
            rsm->lb[k] /= S[j];
            rsm->ub[k] /= S[j];
         }
      }
      /* scale the augmented constraint matrix (unity submatrix is not
         changed) */
      if (!(R == NULL && S == NULL))
      {  for (j = 1; j <= n; j++)
         {  ELEM *e;
            k = m + j;
            for (e = rsm->A->col[k]; e != NULL; e = e->col)
            {  i = e->i;
               if (R != NULL) e->val *= R[i];
               if (S != NULL) e->val *= S[j];
            }
         }
      }
      return;
}

/*----------------------------------------------------------------------
-- update_b - update representation of basis matrix.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- int update_b(RSM *rsm, int p);
--
-- *Description*
--
-- Let p-th column of the current basis matrix B has been replaced by
-- some other column that gives the new (adjacent) basis matrix Bnew.
-- The update_b routine updates some representation of the basis matrix
-- B in order that the updated representation corresponds to the matrix
-- Bnew.
--
-- The new p-th column of the basis matrix is passed implcitly to this
-- routine. It is assmued that this column was saved before by means of
-- the ftran routine.
--
-- *Returns*
--
-- The update_b routine returns one of the following codes:
--
-- 0 - the representation has been successfully updated;
-- 1 - the representation has become inaccurate;
-- 2 - the representation has become too long.
--
-- If the returned code is non-zero, the calling program should rebuild
-- the representation anew by means of the invert_b routine. */

int update_b(RSM *rsm, int p)
{     int ret;
      ret = inv_update(rsm->inv, p);
      return ret;
}

/*----------------------------------------------------------------------
-- update_dvec - update the vector delta.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- void update_dvec(RSM *rsm, gnum_float dvec[], int p, int q, gnum_float ap[],
--    gnum_float aq[], gnum_float w[]);
--
-- *Description*
--
-- The update_dvec routine replaces the vector delta which corresponds
-- to the current basis by the updated vector which corresponds to the
-- adjacent basis.
--
-- On entry the array dvec should contain elements of the vector
-- delta = (delta_1, ..., delta_m) for the current basis in locations
-- dvec[1], ..., dvec[m] respectively. On exit this array will contain
-- elements of the updated vector for the adjacent basis in the same
-- locations.
--
-- The parameter p specifies basic variable (xB)p which has been chosen
-- to leave the basis.
--
-- The parameter q specifies non-basic variable (xN)q which has been
-- chosen to enter the basis.
--
-- On entry the array ap should contain elements of p-th row of the
-- current simplex table in locations ap[1], ..., ap[n]. This array can
-- be computed by means of the eval_row routine. It is not changed on
-- exit.
--
-- On entry the array aq should contain elements of q-th column of the
-- current simplex table in locations aq[1], ..., aq[m]. This array can
-- be computed by means of the eval_col routine. It is not changed on
-- exit.
--
-- The working array w should have at least 1+m locations, where m is
-- order of the basis matrix B.
--
-- The update_dvec routine assumes that the representation of the basis
-- matrix corresponds to the current basis, not to the adjacent one.
-- Therefore this routine should be called before changing the basis.
--
-- For further details see the program documentation. */

void update_dvec(RSM *rsm, gnum_float dvec[], int p, int q, gnum_float ap[],
      gnum_float aq[], gnum_float w[])
{     ELEM *e;
      int m = rsm->m, n = rsm->n, i, j, k;
      gnum_float aiq, t1, t2;
      insist(1 <= p && p <= m && 1 <= q && q <= n);
      dvec[p] = 1.0;
      for (j = 1; j <= n; j++) dvec[p] += ap[j] * ap[j];
      for (i = 1; i <= m; i++) w[i] = 0.0;
      for (j = 1; j <= n; j++)
      {  k = rsm->indn[j];
         if (ap[j] == 0.0) continue;
         for (e = rsm->A->col[k]; e != NULL; e = e->col)
            w[e->i] += ap[j] * e->val;
      }
      ftran(rsm, w, 0);
      for (i = 1; i <= m; i++)
      {  if (i == p) continue;
         aiq = + aq[i] / ap[q];
         t1 = dvec[i] + aiq * aiq * dvec[p] + 2.0 * aiq * w[i];
         t2 = 1 + aiq * aiq;
         dvec[i] = t1 > t2 ? t1 : t2;
      }
      dvec[p] /= (ap[q] * ap[q]);
      return;
}

/*----------------------------------------------------------------------
-- update_gvec - update the vector gamma.
--
-- *Synopsis*
--
-- #include "glprsm.h"
-- void update_gvec(RSM *rsm, gnum_float gvec[], int p, int q, gnum_float ap[],
--    gnum_float aq[], gnum_float w[]);
--
-- *Description*
--
-- The update_gvec routine replaces the vector gamma which corresponds
-- to the current basis by the updated vector which corresponds to the
-- adjacent basis.
--
-- On entry the array gvec should contain elements of the vector
-- gamma = (gamma_1, ..., gamma_n) for the current basis in locations
-- gvec[1], ..., gvec[n] respectively. On exit this array will contain
-- elements of the updated vector for the adjacent basis in the same
-- locations.
--
-- The parameter p specifies basic variable (xB)p which has been chosen
-- to leave the basis.
--
-- The parameter q specifies non-basic variable (xN)q which has been
-- chosen to enter the basis.
--
-- On entry the array ap should contain elements of p-th row of the
-- current simplex table in locations ap[1], ..., ap[n]. This array can
-- be computed by means of the eval_row routine. It is not changed on
-- exit.
--
-- On entry the array aq should contain elements of q-th column of the
-- current simplex table in locations aq[1], ..., aq[m]. This array can
-- be computed by means of the eval_col routine. It is not changed on
-- exit.
--
-- The working array w should have at least 1+m locations, where m is
-- order of the basis matrix B.
--
-- The update_gvec routine assumes that the representation of the basis
-- matrix corresponds to the current basis, not to the adjacent one.
-- Therefore this routine should be called before changing the basis.
--
-- For further details see the program documentation. */

void update_gvec(RSM *rsm, gnum_float gvec[], int p, int q, gnum_float ap[],
      gnum_float aq[], gnum_float w[])
{     ELEM *e;
      int m = rsm->m, n = rsm->n, i, j, k;
      gnum_float apj, tj, t1, t2;
      insist(1 <= p && p <= m && 1 <= q && q <= n);
      gvec[q] = 1.0;
      for (i = 1; i <= m; i++) gvec[q] += aq[i] * aq[i];
      for (i = 1; i <= m; i++) w[i] = aq[i];
      btran(rsm, w);
      for (j = 1; j <= n; j++)
      {  if (j == q) continue;
         apj = - ap[j] / aq[p];
         tj = 0.0;
         k = rsm->indn[j];
         for (e = rsm->A->col[k]; e != NULL; e = e->col)
            tj += e->val * w[e->i];
         t1 = gvec[j] + apj * apj * gvec[q] - 2.0 * apj * tj;
         t2 = 1.0 + apj * apj;
         gvec[j] = t1 > t2 ? t1 : t2;
      }
      gvec[q] /= (aq[p] * aq[p]);
      return;
}

/* eof */
