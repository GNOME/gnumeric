/* glpmat.h */

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

#ifndef _GLPMAT_H
#define _GLPMAT_H

#include "gnumeric-config.h"
#include "gnumeric.h"
#include "numbers.h"

#include "glplib.h"

#define aat_numb              glp_aat_numb
#define aat_symb              glp_aat_symb
#define append_lines          glp_append_lines
#define check_mat             glp_check_mat
#define check_mplets          glp_check_mplets
#define check_per             glp_check_per
#define clear_line            glp_clear_line
#define clear_lines           glp_clear_lines
#define clear_mat             glp_clear_mat
#define copy_mat              glp_copy_mat
#define copy_per              glp_copy_per
#define count_nz              glp_count_nz
#define create_mat            glp_create_mat
#define create_per            glp_create_per
#define delete_lines          glp_delete_lines
#define delete_mat            glp_delete_mat
#define delete_per            glp_delete_per
#define eq_scaling            glp_eq_scaling
#define gm_scaling            glp_gm_scaling
#define inv_per               glp_inv_per
#define iper_vec              glp_iper_vec
#define l_solve               glp_l_solve
#define load_mat              glp_load_mat
#define lt_solve              glp_lt_solve
#define mat_per               glp_mat_per
#define mat_vec               glp_mat_vec
#define mprd_numb             glp_mprd_numb
#define mprd_symb             glp_mprd_symb
#define new_elem              glp_new_elem
#define per_mat               glp_per_mat
#define per_sym               glp_per_sym
#define per_vec               glp_per_vec
#define reset_per             glp_reset_per
#define save_mat              glp_save_mat
#define scrape_mat            glp_scrape_mat
#define show_mat              glp_show_mat
#define sort_mat              glp_sort_mat
#define submatrix             glp_submatrix
#define sum_mplets            glp_sum_mplets
#define sym_vec               glp_sym_vec
#define test_mat_d            glp_test_mat_d
#define test_mat_e            glp_test_mat_e
#define tmat_vec              glp_tmat_vec
#define trn_mat               glp_trn_mat
#define u_solve               glp_u_solve
#define ut_solve              glp_ut_solve
#define v_solve               glp_v_solve
#define vt_solve              glp_vt_solve

typedef struct MAT MAT;
typedef struct ELEM ELEM;
typedef struct PER PER;

struct MAT
{     /* sparse matrix */
      POOL *pool;
      /* memory pool for allocating matrix elements */
      int m_max;
      /* current dimension of array of row pointers; this dimension is
         automatically increased when necessary */
      int n_max;
      /* current dimension of array of column pointers; this dimension
         is automatically increased when necessary */
      int m;
      /* number of rows (>= 0) */
      int n;
      /* number of columns (>= 0) */
      ELEM **row; /* ELEM *row[1+m_max]; */
      /* array of row pointers; row[0] is not used; row[i] points to
         the i-th row (i = 1, ..., m); row[m+1], ..., row[m_max] are
         reserved for new rows that can be added to the matrix */
      ELEM **col; /* ELEM *col[1+n_max]; */
      /* array of column pointers; col[0] is not used; col[i] points to
         the j-th column (j = 1, ..., n); col[n+1], ..., col[n_max] are
         reserved for new columns that can be added to the matrix */
};

struct ELEM
{     /* element of sparse matrix */
      int i;
      /* row number (1 <= i <= m) */
      int j;
      /* column number (1 <= j <= n) */
      gnm_float val;
      /* element value (usually non-zero, but may be zero) */
      ELEM *row;
      /* pointer to the next element in the same row (this linked list
         is unordered) */
      ELEM *col;
      /* pointer to the next element in the same column (this linked
         list is unordered) */
};

struct PER
{     /* permutation matrix */
      int n;
      /* order of matrix */
      int *row; /* int row[1+n]; */
      /* row-wise format: if P[i,j] = 1 then row[i] = j (row[0] is not
         used) */
      int *col; /* int col[1+n]; */
      /* column-wise format: if P[i,j] = 1 then col[j] = i (col[0] is
         not used) */
};

extern MAT *aat_numb(MAT *S, MAT *A, gnm_float D[], gnm_float work[]);
/* compute matrix product S = A * D * A'; numeric phase */

extern MAT *aat_symb(MAT *S, MAT *A, char work[]);
/* compute matrix product S = A * A'; symbolic phase */

extern MAT *append_lines(MAT *A, int dm, int dn);
/* append new rows and columns to sparse matrix */

extern void check_mat(MAT *A);
/* check sparse matrix for correctness */

extern ELEM *check_mplets(MAT *A);
/* check whether sparse matrix has multiplets */

extern void check_per(PER *P);
/* check permutation matrix for correctness */

extern MAT *clear_line(MAT *A, int k);
/* clear row or column of sparse matrix */

extern MAT *clear_lines(MAT *A, int rs[], int cs[]);
/* clear rows and columns of sparse matrix */

extern MAT *clear_mat(MAT *A);
/* clear sparse matrix (A := 0) */

extern MAT *copy_mat(MAT *B, MAT *A);
/* copy sparse matrix (B := A) */

extern PER *copy_per(PER *Q, PER *P);
/* copy permutation matrix (Q := P) */

extern int count_nz(MAT *A, int k);
/* count non-zeros of sparse matrix */

extern MAT *create_mat(int m, int n);
/* create sparse matrix */

extern PER *create_per(int n);
/* create permutation matrix */

extern MAT *delete_lines(MAT *A, int rs[], int cs[]);
/* delete rows and columns from sparse matrix */

extern void delete_mat(MAT *A);
/* delete sparse matrix */

extern void delete_per(PER *P);
/* delete permutation matrix */

extern void eq_scaling(MAT *A, gnm_float R[], gnm_float S[], int ord);
/* implicit equilibration scaling */

extern void gm_scaling(MAT *A, gnm_float R[], gnm_float S[], int ord,
      gnm_float eps, int itmax);
/* implicit geometric mean scaling */

extern PER *inv_per(PER *P);
/* invert permutation matrix */

extern gnm_float *iper_vec(gnm_float y[], PER *P, gnm_float x[]);
/* permute vector elements in inverse order (y := P' * x) */

extern gnm_float *l_solve(MAT *L, gnm_float x[]);
/* solve lower triangular system L*x = b */

extern MAT *load_mat(char *fname);
/* read sparse matrix from text file using plain format */

extern gnm_float *lt_solve(MAT *L, gnm_float x[]);
/* solve transposed lower triangular system L'*x = b */

extern MAT *mat_per(MAT *A, PER *P, void *work[]);
/* permute matrix columns (A := A * P) */

extern gnm_float *mat_vec(gnm_float y[], MAT *A, gnm_float x[]);
/* multiply matrix on vector (y := A * x) */

extern MAT *mprd_numb(MAT *C, MAT *A, MAT *B, gnm_float _work[]);
/* multiply matrices (C := A * B); numeric phase */

extern MAT *mprd_symb(MAT *C, MAT *A, MAT *B, char work[]);
/* multiply matrices (C := A * B); symbolic phase */

extern ELEM *new_elem(MAT *A, int i, int j, gnm_float val);
/* create new element of matrix */

extern MAT *per_mat(PER *P, MAT *A, void *work[]);
/* permute matrix rows (A := P * A) */

extern MAT *per_sym(PER *P, MAT *A, void *work[]);
/* permute symmetric matrix (A := P*A*P') */

extern gnm_float *per_vec(gnm_float y[], PER *P, gnm_float x[]);
/* permute vector elements (y := P * x) */

extern PER *reset_per(PER *P);
/* reset permutation matrix (P := I) */

extern int save_mat(MAT *A, char *fname);
/* write sparse matrix to text file using plain format */

extern int scrape_mat(MAT *A, gnm_float eps);
/* remove tiny elements from sparse matrix */

extern int show_mat(MAT *A, int type, char *fname);
/* output matrix pattern as raster image */

extern MAT *sort_mat(MAT *A);
/* ordering row and column lists of sparse matrix */

extern MAT *submatrix(MAT *B, MAT *A, int i1, int i2, int j1, int j2);
/* copy submatrix */

extern int sum_mplets(MAT *A, gnm_float eps);
/* sum multiplets of sparse matrix */

extern gnm_float *sym_vec(gnm_float y[], MAT *A, gnm_float x[]);
/* multiply symmetric matrix on vector (y := A * x) */

extern MAT *test_mat_d(int n, int c);
/* create test sparse matrix of D(n,c) class */

extern MAT *test_mat_e(int n, int c);
/* create test sparse matrix of E(n,c) class */

extern gnm_float *tmat_vec(gnm_float y[], MAT *A, gnm_float x[]);
/* multiply transposed matrix on vector (y := A' * x) */

extern MAT *trn_mat(MAT *A);
/* transpose sparse matrix */

extern gnm_float *u_solve(MAT *U, gnm_float x[]);
/* solve upper triangular system U*x = b */

extern gnm_float *ut_solve(MAT *U, gnm_float x[]);
/* solve transposed upper triangular system U'*x = b */

extern gnm_float *v_solve(PER *P, MAT *V, PER *Q, gnm_float x[],
      gnm_float work[]);
/* solve implicit upper triangular system V*x = b */

extern gnm_float *vt_solve(PER *P, MAT *V, PER *Q, gnm_float x[],
      gnm_float work[]);
/* solve implicit transposed upper triangular system V'*x = b */

#endif

/* eof */
