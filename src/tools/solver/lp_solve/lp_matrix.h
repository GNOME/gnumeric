#ifndef HEADER_lp_matrix
#define HEADER_lp_matrix

#include "lp_types.h"


/* Compiler option edevelopment features */
/*#define DebugInv*/                /* Report array values at inversion */

/* Matrix column access macros to be able to easily change storage model */
#define CAM_Record                0
#define CAM_Vector                1
#define MatrixColAccess           CAM_Record

#define COL_MAT_PTR(item)         &(item)
#define COL_MAT_COL(item)         (item).col_nr
#define COL_MAT_ROW(item)         (item).row_nr
#define COL_MAT_VALUE(item)       (item).value


/* Matrix row access macros to be able to easily change storage model */
#define RAM_Index                 0
#define RAM_IndexAndCol           1
#define RAM_Pointer               2
#define RAM_FullCopy              3
#define MatrixRowAccess           RAM_Index

#if MatrixRowAccess==RAM_Index
#define ROW_MAT_PTR(item)         &(mat->col_mat[item])
#define ROW_MAT_ITEM(col,idx,ptr) (idx)
#define ROW_MAT_COL(item)         mat->col_mat[item].col_nr
#define ROW_MAT_ROW(item)         mat->col_mat[item].row_nr
#define ROW_MAT_VALUE(item)       mat->col_mat[item].value

#elif MatrixRowAccess==RAM_IndexAndCol
#define ROW_MAT_PTR(item)         &(mat->col_mat[item])
#define ROW_MAT_ITEM(col,idx,ptr) (idx)
#define ROW_MAT_COL(item)         mat->row_col[item]
#define ROW_MAT_ROW(item)         mat->col_mat[item].row_nr
#define ROW_MAT_VALUE(item)       mat->col_mat[item].value

#elif MatrixRowAccess==RAM_Pointer
#define ROW_MAT_PTR(item)         (item)
#define ROW_MAT_ITEM(col,idx,ptr) (ptr)
#define ROW_MAT_COL(item)         (item)->col_nr
#define ROW_MAT_ROW(item)         (item)->row_nr
#define ROW_MAT_VALUE(item    )   (item)->value

#else /* if MatrixRowAccess==RAM_FullCopy */
#define ROW_MAT_PTR(item)         &(item)
#define ROW_MAT_ITEM(col,idx,ptr) (*ptr)
#define ROW_MAT_COL(item)         (item).col_nr
#define ROW_MAT_ROW(item)         (item).row_nr
#define ROW_MAT_VALUE(item)       (item).value

#endif


/* Sparse matrix element (ordered columnwise) */
typedef struct _MATitem
{
  int  row_nr;
  int  col_nr;
  REAL value;
} MATitem;

typedef struct _MATrec
{
  /* Owner reference */
  lprec     *lp;

  /* Active dimensions */
  int       rows;
  int       columns;
  REAL      epsvalue;           /* Zero element rejection threshold */

  /* Allocated memory */
  int       rows_alloc;
  int       columns_alloc;
  int       mat_alloc;          /* The allocated size for matrix sized structures */

  /* Sparse problem matrix storage */
  MATitem   *col_mat;           /* mat_alloc : The sparse data storage */
  int       *col_end;           /* columns_alloc+1 : col_end[i] is the index of the
                                   first element after column i; column[i] is stored
                                   in elements col_end[i-1] to col_end[i]-1 */
#if MatrixRowAccess==RAM_Index
  int       *row_mat;           /* mat_alloc : From index 0, row_mat contains the
                                   row-ordered index of the elements of col_mat */
#elif MatrixRowAccess==RAM_IndexAndCol
  int       *row_mat;           /* mat_alloc : From index 0, row_mat contains the
                                   row-ordered index of the elements of col_mat */
  int       *row_col;           /* mat_alloc : Column index */
#elif MatrixRowAccess==RAM_Pointer
  MATitem   **row_mat;          /* mat_alloc : From index 0, row_mat contains the
                                   row-ordered pointers to the elements of col_mat */
#else /* if MatrixRowAccess==RAM_FullCopy */
  MATitem   *row_mat;           /* mat_alloc : From index 0, row_mat contains the
                                   row-ordered copy of the elements in col_mat */
#endif
  int       *row_end;           /* rows_alloc+1 : row_end[i] is the index of the
                                   first element in row_mat after row i */
  MYBOOL    row_end_valid;      /* TRUE if row_end & row_mat are valid */
  MYBOOL    is_roworder;        /* FUTURE */

} MATrec;


#ifdef __cplusplus
__EXTERN_C {
#endif

/* Sparse matrix routines */
STATIC MATrec *mat_create(lprec *lp, int rows, int columns, REAL epsvalue);
STATIC void mat_free(MATrec **matrix);
STATIC MYBOOL inc_matrow_space(MATrec *mat, int deltarows);
STATIC int mat_rowcompact(MATrec *mat);
STATIC MYBOOL inc_matcol_space(MATrec *mat, int deltacols);
STATIC MYBOOL inc_mat_space(MATrec *mat, int mindelta);
STATIC int mat_shiftrows(MATrec *mat, int *bbase, int delta);
STATIC int mat_shiftcols(MATrec *mat, int base, int delta);
STATIC int mat_appendrow(MATrec *mat, int count, REAL *row, int *colno, REAL mult);
STATIC int mat_appendcol(MATrec *mat, int count, REAL *column, int *rowno, REAL mult);
STATIC MYBOOL mat_validate(MATrec *mat);
STATIC int mat_findelm(MATrec *mat, int row, int column);
STATIC int mat_findins(MATrec *mat, int row, int column, int *insertpos, MYBOOL validate);
STATIC void mat_multcol(MATrec *mat, int col_nr, REAL mult);
STATIC REAL mat_getitem(MATrec *mat, int row, int column);
STATIC int mat_nonzeros(MATrec *mat);
STATIC int mat_collength(MATrec *mat, int colnr);
STATIC int mat_rowlength(MATrec *mat, int rownr);
STATIC void mat_multrow(MATrec *mat, int row_nr, REAL mult);
STATIC MYBOOL mat_setvalue(MATrec *mat, int Row, int Column, REAL Value, MYBOOL doscale);
STATIC int mat_checkcounts(MATrec *mat, int *rownum, int *colnum, MYBOOL freeonexit);
STATIC MYBOOL mat_transpose(MATrec *mat, MYBOOL col1_row0);

STATIC MYBOOL fimprove(lprec *lp, REAL *pcol, int *nzidx, REAL roundzero);
STATIC void ftran(lprec *lp, REAL *rhsvector, int *nzidx, REAL roundzero);
STATIC MYBOOL bimprove(lprec *lp, REAL *rhsvector, int *nzidx, REAL roundzero);
STATIC void btran(lprec *lp, REAL *rhsvector, int *nzidx, REAL roundzero);
STATIC int prod_Ax(lprec *lp, int varset, REAL *input, int *nzinput, int range, REAL roundzero, REAL multfactor, REAL *output, int *nzoutput);
STATIC int prod_xA(lprec *lp, int varset, REAL *input, int *nzinput, int range, REAL roundzero, REAL ofscalar, REAL *output, int *nzoutput);
STATIC void prod_xA2(lprec *lp, REAL *prow, int prange, REAL proundzero, int *pnzprow,
                                REAL *drow, int drange, REAL droundzero, int *dnzdrow, REAL ofscalar);
STATIC MYBOOL fsolve(lprec *lp, int varin, REAL *pcol, int *nzidx, REAL roundzero, REAL ofscalar, MYBOOL prepareupdate);
STATIC MYBOOL bsolve(lprec *lp, int row_nr, REAL *rhsvector, int *nzidx, REAL roundzero, REAL ofscalar);
STATIC void bsolve_xA2(lprec *lp, int row_nr1, REAL *vector1, REAL roundzero1, int *nzvector1,
                                  int row_nr2, REAL *vector2, REAL roundzero2, int *nzvector2);

#ifdef __cplusplus
}
#endif

#endif /* HEADER_lp_matrix */
