
#include <string.h>
#include "commonlib.h"
#include "lp_lib.h"
#include "lp_price.h"
#include "lp_pricePSE.h"
#include "lp_matrix.h"

#ifdef FORTIFY
# include "lp_fortify.h"
#endif


/* -------------------------------------------------------------------------
   Basic matrix routines in lp_solve v5.0+
   -------------------------------------------------------------------------
    Author:        Michel Berkelaar (to lp_solve v3.2),
                   Kjell Eikland
    Contact:       kjell.eikland@broadpark.no
    License terms: LGPL.

    Requires:      lp_lib.h, lp_pricerPSE.h, lp_matrix.h

    Release notes:
    v5.0.0  1 January 2004      First integrated and repackaged version.
    v5.0.1  7 May 2004          Added matrix transpose function.

   ------------------------------------------------------------------------- */

STATIC MATrec *mat_create(lprec *lp, int rows, int columns, REAL epsvalue)
{
  MATrec *newmat;

  newmat = (MATrec *) calloc(1, sizeof(*newmat));
  newmat->lp = lp;

  newmat->rows_alloc = 0;
  newmat->columns_alloc = 0;
  newmat->mat_alloc = 0;

  inc_matrow_space(newmat, rows);
  newmat->rows = rows;
  inc_matcol_space(newmat, columns);
  newmat->columns = columns;
  inc_mat_space(newmat, 0);

  newmat->epsvalue = epsvalue;

  return( newmat );
}

STATIC void mat_free(MATrec **matrix)
{
  FREE((*matrix)->col_mat);
  FREE((*matrix)->col_end);
  FREE((*matrix)->row_mat);
#if MatrixRowAccess==RAM_IndexAndCol
  FREE((*matrix)->row_col);
#endif
  FREE((*matrix)->row_end);
  FREE(*matrix);
}

STATIC MYBOOL inc_mat_space(MATrec *mat, int mindelta)
{
  int spaceneeded;

  if(mindelta <= 0)
    mindelta = MAX(mat->rows, mat->columns) + 1;
  if(mat->mat_alloc == 0)
    spaceneeded = mindelta;
  else
    spaceneeded = mat_nonzeros(mat) + mindelta;

  if(spaceneeded >= mat->mat_alloc) {
    /* Let's allocate at least MAT_START_SIZE entries */
    if(mat->mat_alloc < MAT_START_SIZE)
      mat->mat_alloc = MAT_START_SIZE;

    /* Increase the size by RESIZEFACTOR each time it becomes too small */
    while(spaceneeded >= mat->mat_alloc)
      mat->mat_alloc += mat->mat_alloc / RESIZEFACTOR;

    mat->col_mat = (MATitem *) realloc(mat->col_mat, (mat->mat_alloc) * sizeof(*(mat->col_mat)));
#if MatrixRowAccess==RAM_Index
    allocINT(mat->lp, &(mat->row_mat), mat->mat_alloc, AUTOMATIC);
#elif MatrixRowAccess==RAM_IndexAndCol
    allocINT(mat->lp, &(mat->row_mat), mat->mat_alloc, AUTOMATIC);
    allocINT(mat->lp, &(mat->row_col), mat->mat_alloc, AUTOMATIC);
#elif MatrixRowAccess==RAM_Pointer
    mat->row_mat = (MATitem **) realloc(mat->row_mat, (mat->mat_alloc) * sizeof(*(mat->row_mat)));
#elif MatrixRowAccess==RAM_FullCopy
    mat->row_mat = (MATitem *) realloc(mat->row_mat, (mat->mat_alloc) * sizeof(*(mat->row_mat)));
#else
    assert(NULL);
#endif
  }
  return(TRUE);
}

STATIC MYBOOL inc_matrow_space(MATrec *mat, int deltarows)
{
  int    rowsum, oldrowsalloc, rowdelta = DELTAROWALLOC;
  MYBOOL status = TRUE;

  /* Adjust lp row structures */
  if(mat->rows+deltarows > mat->rows_alloc) {
    deltarows = MAX(deltarows, rowdelta);
    oldrowsalloc = mat->rows_alloc;
    mat->rows_alloc += deltarows;
    rowsum = mat->rows_alloc + 1;

    status = allocINT(mat->lp, &mat->row_end, rowsum, AUTOMATIC);
    mat->row_end_valid = FALSE;
  }
  return( status );
}

STATIC MYBOOL inc_matcol_space(MATrec *mat, int deltacols)
{
  int    i,colsum, oldcolsalloc, newcolcount;
  MYBOOL status = TRUE;

  newcolcount = mat->columns+deltacols;

  if(newcolcount >= mat->columns_alloc) {

    /* Update memory allocation and sizes */
    oldcolsalloc = mat->columns_alloc;
    deltacols = MAX(DELTACOLALLOC, newcolcount);
    mat->columns_alloc += deltacols;
    colsum = mat->columns_alloc + 1;
    status = allocINT(mat->lp, &mat->col_end, colsum, AUTOMATIC);

    /* Update column pointers */
    if(oldcolsalloc == 0)
      mat->col_end[0] = 0;
    for(i = MIN(oldcolsalloc, mat->columns) + 1; i < colsum; i++)
      mat->col_end[i] = mat->col_end[i-1];
    mat->row_end_valid = FALSE;
  }
  return( status );
}

STATIC int mat_collength(MATrec *mat, int colnr)
{
  return( mat->col_end[colnr] - mat->col_end[colnr-1] );
}

STATIC int mat_rowlength(MATrec *mat, int rownr)
{
  if(mat_validate(mat)) {
    if(rownr <= 0)
      return( mat->row_end[0] );
    else
      return( mat->row_end[rownr] - mat->row_end[rownr-1] );
  }
  else
    return( 0 );
}

STATIC int mat_nonzeros(MATrec *mat)
{
  return( mat->col_end[mat->columns] );
}

STATIC int mat_shiftrows(MATrec *mat, int *bbase, int delta)
{
  MATitem *matelm1, *matelm2;
  int     j, k, i, ii, thisrow, *colend, base;
  MYBOOL  preparecompact = FALSE;

  if(delta == 0)
    return( 0 );
  base = abs(*bbase);

  if(delta > 0) {

    /* Insert row by simply incrementing existing row indeces */
    if(base <= mat->rows) {
      k = mat_nonzeros(mat);
      for(ii = 0, matelm1 = mat->col_mat; ii < k; ii++, matelm1++) {
        if((*matelm1).row_nr >= base)
          (*matelm1).row_nr += delta;
      }
    }

    /* Set defaults (actual basis set in separate procedure) */
    for(i = 0; i < delta; i++) {
      ii = base + i;
      mat->row_end[ii] = 0;
    }
  }
  else if(base <= mat->rows) {

    /* Check if we should prepare for compacting later
       (this is in order to speed up multiple row deletions) */
    preparecompact = (MYBOOL) (*bbase < 0);
    if(preparecompact)
      *bbase = my_flipsign((*bbase));

    /* First make sure we don't cross the row count border */
    if(base-delta-1 > mat->rows)
      delta = base - mat->rows - 1;

    /* Then scan over all entries shifting and updating rows indeces */
    if(preparecompact) {
      k = 0;
      for(j = 1, colend = mat->col_end + 1;
          j <= mat->columns; j++, colend++) {
        i = k;
        k = *colend;
        matelm2 = mat->col_mat + i;
        for(; i < k; i++, matelm2++) {
          thisrow = (*matelm2).row_nr;
          if(thisrow < base)
            continue;
          else if(thisrow >= base-delta)
            (*matelm2).row_nr += delta;
          else
            (*matelm2).row_nr = -1;
        }
      }
    }
    else {
      k = 0;
      ii = 0;
      matelm1 = mat->col_mat;
      matelm2 = matelm1;
      for(j = 1, colend = mat->col_end + 1;
          j <= mat->columns; j++, colend++) {
        i = k;
        k = *colend;
        matelm2 = mat->col_mat + i;
        for(; i < k; i++, matelm2++) {
          thisrow = (*matelm2).row_nr;
          if(thisrow >= base) {
            if(thisrow >= base-delta)
              (*matelm2).row_nr += delta;
            else
              continue;
          }
          if(matelm1 != matelm2)
            *matelm1 = *matelm2;
          matelm1++;
          ii++;
        }
        *colend = ii;
      }
    }
  }
  return( 0 );
}

STATIC int mat_rowcompact(MATrec *mat)
{
  MATitem *matelm1, *matelm2;
  int     i, ii, j, k, nn, *colend;

  nn = 0;
  k  = 0;
  ii = 0;
  matelm1 = mat->col_mat;
  matelm2 = matelm1;
  for(j = 1, colend = mat->col_end + 1;
      j <= mat->columns; j++, colend++) {
    i = k;
    k = *colend;
    matelm2 = mat->col_mat + i;
    for(; i < k; i++, matelm2++) {
      if((*matelm2).row_nr < 0) {
        nn++;
        continue;
      }
      if(matelm1 != matelm2)
        *matelm1 = *matelm2;
      matelm1++;
      ii++;
    }
    *colend = ii;
  }
  return(nn);
}

STATIC int mat_shiftcols(MATrec *mat, int base, int delta)
{
  int     i, ii, k;
  MATitem *matelm1, *matelm2;

  k = 0;
  if(delta == 0)
    return( k );

  if(delta > 0) {
    /* Shift pointers right */
    for(ii = mat->columns; ii > base; ii--) {
      i = ii + delta;
      mat->col_end[i] = mat->col_end[ii];
    }
    /* Set defaults */
    for(i = 0; i < delta; i++) {
      ii = base + i;
      mat->col_end[ii] = mat->col_end[ii-1];
    }
  }
  else {

    /* First make sure we don't cross the column count border */
    if(base-delta-1 > mat->columns)
      delta = base - mat->columns - 1;

    /* Delete sparse matrix data, if required */
    if(base <= mat->columns) {

      i = mat->col_end[base-1];          /* Beginning of data to be deleted */
      matelm1 = mat->col_mat+i;
      ii = mat->col_end[base-delta-1];   /* Beginning of data to be shifted left */
      matelm2 = mat->col_mat+ii;
      k = ii-i;                          /* Number of entries to be deleted */
      if(k > 0) {
        i = mat_nonzeros(mat);
        for(; ii < i; ii++, matelm1++, matelm2++)
          *matelm1 = *matelm2;
      }
    }

    /* Update indexes */
    for(i = base; i <= mat->columns + delta; i++) {
      ii = i - delta;
      mat->col_end[i] = mat->col_end[ii] - k;
    }
  }
  return( k );
}

#if 0
STATIC int mat_appendrow(MATrec *mat, int count, REAL *row, int *colno, REAL mult)
{
  int    i, j, jj, stcol, elmnr, orignr, newnr, firstcol;
  MYBOOL *addto = NULL, isA, isNZ;
  REAL   value;

  /* Check if we are in row order mode and should add as column instead;
     the matrix will be transposed at a later stage */
  isA = (MYBOOL) (mat == mat->lp->matA);
  isNZ = (MYBOOL) (colno != NULL);
  if(mat->is_roworder) {
    if(isNZ)
      sortREALByINT(row, colno, count, 0, TRUE);
    else
      row[0] = 0;
    return( mat_appendcol(mat, count, row, colno, mult) );
  }

  /* Optionally tally and map the new non-zero values */
  firstcol = 0;
  if(isNZ)
    newnr = count;
  else {
    newnr = 0;
    if(!allocMYBOOL(mat->lp, &addto, mat->columns + 1, TRUE)) {
      mat->lp->spx_status = NOMEMORY;
      return( newnr );
    }
    for(i = 1; i <= mat->columns; i++) {
      if(fabs(row[i]) > mat->epsvalue) {
        addto[i] = TRUE;
        if(firstcol == 0)
          firstcol = i;
        newnr++;
      }
    }
    if(newnr == 0)
      firstcol = mat->columns+1;
  }

  /* Make sure we have sufficient space */
  if(!inc_mat_space(mat, newnr)) {
    FREE(addto);
    return( 0 );
  }

  /* Insert the non-zero constraint values */
  orignr = mat_nonzeros(mat) - 1;
  elmnr = orignr + newnr;
  if(isNZ)
    jj = newnr-1;
  else
    jj = mat->columns;
  for(; jj >= firstcol; jj--) {
    if(isNZ)
      j = colno[jj];
    else
      j = jj;
    stcol = mat->col_end[j] - 1;
    mat->col_end[j] = elmnr + 1;

   /* Add a new non-zero entry */
    if(isNZ || addto[j]) {
#ifdef DoMatrixRounding
      value = roundToPrecision(row[j], mat->epsvalue);
#else
      value = row[j];
#endif
      value *= mult;
      if(isA)
        value = scaled_mat(mat->lp, value, mat->rows, j);
      mat->col_mat[elmnr].value = value;
      mat->col_mat[elmnr].row_nr = mat->rows;
      newnr--;
      elmnr--;
    }

   /* Shift previous column entries down */
#if 1
    i = stcol - mat->col_end[j-1] + 1;
    if(i > 0) {
      orignr -= i;
      elmnr  -= i;
      MEMMOVE(mat->col_mat+elmnr+1, mat->col_mat+orignr+1, i);
    }
#else
    for(i = stcol; i >= mat->col_end[j-1]; i--) {
      mat->col_mat[elmnr] = mat->col_mat[orignr];
      orignr--;
      elmnr--;
    }
#endif
  }

  FREE(addto);

  return( newnr );

}
#else
STATIC int mat_appendrow(MATrec *mat, int count, REAL *row, int *colno, REAL mult)
{
  int    i, j, jj = 0, stcol, elmnr, orignr, newnr, firstcol;
  MYBOOL *addto = NULL, isA, isNZ;
  REAL   value;
 
  /* Check if we are in row order mode and should add as column instead;
     the matrix will be transposed at a later stage */
  isA = (MYBOOL) (mat == mat->lp->matA);
  isNZ = (MYBOOL) (colno != NULL);
  if(mat->is_roworder) {
    if(isNZ)
      sortREALByINT(row, colno, count, 0, TRUE);
    else
      row[0] = 0;
    return( mat_appendcol(mat, count, row, colno, mult) );
  }
 
  /* Optionally tally and map the new non-zero values */
  firstcol = mat->columns + 1;
  if(isNZ) {
    newnr = count;
    if(newnr) {
      firstcol = colno[0];
      jj = colno[newnr - 1];
    }
  }
  else {
    newnr = 0;
    if(!allocMYBOOL(mat->lp, &addto, mat->columns + 1, TRUE)) {
      mat->lp->spx_status = NOMEMORY;
      return( newnr );
    }
    for(i = mat->columns; i >= 1; i--) {
      if(fabs(row[i]) > mat->epsvalue) {
        addto[i] = TRUE;
        firstcol = i;
        newnr++;
      }
    }
  }
 
  /* Make sure we have sufficient space */
  if(!inc_mat_space(mat, newnr)) {
    FREE(addto);
    return( 0 );
  }
 
  /* Insert the non-zero constraint values */
  orignr = mat_nonzeros(mat) - 1;
  elmnr = orignr + newnr;
 
  for(j = mat->columns; j >= firstcol; j--) {
    stcol = mat->col_end[j] - 1;
    mat->col_end[j] = elmnr + 1;
 
   /* Add a new non-zero entry */
    if(((isNZ) && (j == jj)) || ((addto != NULL) && (addto[j]))) {
      newnr--;
      if(isNZ) {
        value = row[newnr];
        if(newnr)
          jj = colno[newnr - 1];
        else
          jj = 0;
      }
      else
        value = row[j];
#ifdef DoMatrixRounding
      value = roundToPrecision(value, mat->epsvalue);
#endif
      value *= mult;
      if(isA)
        value = scaled_mat(mat->lp, value, mat->rows, j);
      mat->col_mat[elmnr].value = value;
      mat->col_mat[elmnr].row_nr = mat->rows;
      elmnr--;
    }
 
   /* Shift previous column entries down */
#if 1
    i = stcol - mat->col_end[j-1] + 1;
    if(i > 0) {
      orignr -= i;
      elmnr  -= i;
      MEMMOVE(mat->col_mat+elmnr+1, mat->col_mat+orignr+1, i);
    }
#else
    for(i = stcol; i >= mat->col_end[j-1]; i--) {
      mat->col_mat[elmnr] = mat->col_mat[orignr];
      orignr--;
      elmnr--;
    }
#endif
  }
 
  FREE(addto);
 
  return( newnr );
 
}
#endif

STATIC int mat_appendcol(MATrec *mat, int count, REAL *column, int *rowno, REAL mult)
{
  int     i, row, elmnr, lastnr;
  MATitem *elm;
  REAL    value;
  MYBOOL  isA;

  /* Make sure we have enough space */
  if(!inc_mat_space(mat, mat->rows+1))
    return( 0 );

  if(rowno != NULL)
    count--;
  isA = (MYBOOL) (mat == mat->lp->matA);

  /* Append sparse regular constraint values */
  elmnr = mat->col_end[mat->columns - 1];

  if(column != NULL) {
    row = -1;
    elm = mat->col_mat + elmnr;
    for(i = 0 ; i <= count ; i++) {
      value = column[i];
      if(fabs(value) > mat->epsvalue) {
        if(rowno == NULL)
          row = i;
        else {
          lastnr = row;
          row = rowno[i];
          /* Check if we have come to the Lagrangean constraints */
          if(row > mat->rows) break;
          if(row <= lastnr)
            return( -1 );
        }
#ifdef DoMatrixRounding
        value = roundToPrecision(value, mat->epsvalue);
#endif
        if(mat->is_roworder)
          value *= mult;
        else if(isA) {
          value = my_chsign(is_chsign(mat->lp, row), value);
          value = scaled_mat(mat->lp, value, row, mat->columns);
        }

       /* Store the item and update counters */
        (*elm).row_nr = row;
        (*elm).value  = value;
        elm++;
        elmnr++;
      }
    }

   /* Fill dense Lagrangean constraints */
    if(get_Lrows(mat->lp) > 0)
      mat_appendcol(mat->lp->matL, get_Lrows(mat->lp), column+mat->rows, NULL, mult);

  }

 /* Set end of data */
  mat->col_end[mat->columns] = elmnr;

  return( mat->col_end[mat->columns] - mat->col_end[mat->columns-1] );
}

STATIC int mat_checkcounts(MATrec *mat, int *rownum, int *colnum, MYBOOL freeonexit)
{
  int i, j, n;

  if(rownum == NULL)
    allocINT(mat->lp, &rownum, mat->rows + 1, TRUE);
  if(colnum == NULL)
    allocINT(mat->lp, &colnum, mat->columns + 1, TRUE);

  for(i = 1 ; i <= mat->columns; i++) {
    n = mat->col_end[i];
    for(j = mat->col_end[i - 1]; j < n; j++) {
      colnum[i]++;
      rownum[mat->col_mat[j].row_nr]++;
    }
  }

  n = 0;
  if((mat->lp->do_presolve != PRESOLVE_NONE) &&
     (mat->lp->spx_trace || (mat->lp->verbose > NORMAL))) {
    for(j = 1; j <= mat->columns; j++)
      if(colnum[j] == 0) {
        n++;
        report(mat->lp, FULL, "mat_checkcounts: Variable %s is not used in any constraints\n",
                              get_col_name(mat->lp, j));
      }
    for(i = 0; i <= mat->rows; i++)
      if(rownum[i] == 0) {
        n++;
        report(mat->lp, FULL, "mat_checkcounts: Constraint %s empty\n",
                              get_row_name(mat->lp, i));
      }
  }

  if(freeonexit) {
    FREE(rownum);
    FREE(colnum);
  }

  return( n );

}

STATIC MYBOOL mat_validate(MATrec *mat)
/* Routine to make sure that row mapping arrays are valid */
{
  int     i, j, je, row_nr, *rownum;
  MATitem *matelem;

  if(!mat->row_end_valid) {

    MEMCLEAR(mat->row_end, mat->rows + 1);
    allocINT(mat->lp, &rownum, mat->rows + 1, TRUE);

    /* First tally row counts and then cumulate them */
    matelem = mat->col_mat;
    j = mat_nonzeros(mat);
    for(i = 0; i < j; i++, matelem++)
      mat->row_end[(*matelem).row_nr]++;
    for(i = 1; i <= mat->rows; i++)
      mat->row_end[i] += mat->row_end[i - 1];

    /* Calculate the column index for every non-zero */
    for(i = 1; i <= mat->columns; i++) {
      j = mat->col_end[i - 1];
      je = mat->col_end[i];
      for(matelem = mat->col_mat + j; j < je; j++, matelem++) {
        row_nr = (*matelem).row_nr;
        (*matelem).col_nr = i;
#if MatrixRowAccess==RAM_IndexAndCol
        ROW_MAT_COL(j) = i;
#endif
        if(row_nr == 0)
          mat->row_mat[rownum[row_nr]] = ROW_MAT_ITEM(i, j, matelem);
        else
          mat->row_mat[mat->row_end[row_nr - 1] + rownum[row_nr]] = ROW_MAT_ITEM(i, j, matelem);
        rownum[row_nr]++;
      }
    }

    FREE(rownum);
    mat->row_end_valid = TRUE;
  }

  if(mat == mat->lp->matA)
    mat->lp->model_is_valid = TRUE;
  return( TRUE );
}

/* Implement combined binary/linear sub-search for matrix look-up */
int mat_findelm(MATrec *mat, int row, int column)
{
  int low, high, mid, item;

#if 0
  if(mat->row_end_valid && (row > 0) &&
     (ROW_MAT_COL(mat->row_mat[(low = mat->row_end[row-1])]) == column))
    return(low);
#endif

  if((column < 1) || (column > mat->columns)) {
    report(mat->lp, IMPORTANT, "mat_findelm: Column %d out of range\n", column);
    return( -1 );
  }
  if((row < 0) || (row > mat->rows)) {
    report(mat->lp, IMPORTANT, "mat_findelm: Row %d out of range\n", row);
    return( -1 );
  }

  low = mat->col_end[column - 1];
  high = mat->col_end[column] - 1;
  if(low > high)
    return( -2 );

 /* Do binary search logic */
  mid = (low+high) / 2;
  item = mat->col_mat[mid].row_nr;
  while(high - low > LINEARSEARCH) {
    if(item < row) {
      low = mid + 1;
      mid = (low+high) / 2;
      item = mat->col_mat[mid].row_nr;
    }
    else if(item > row) {
      high = mid - 1;
      mid = (low+high) / 2;
      item = mat->col_mat[mid].row_nr;
    }
    else {
      low = mid;
      high = mid;
    }
  }

 /* Do linear scan search logic */
  if((high > low) && (high - low <= LINEARSEARCH)) {
    item = mat->col_mat[low].row_nr;
    while((low < high) && (item < row)) {
      low++;
      item = mat->col_mat[low].row_nr;
    }
    if(item == row)
      high = low;
  }

  if((low == high) && (row == item))
    return( low );
  else
    return( -2 );
}

int mat_findins(MATrec *mat, int row, int column, int *insertpos, MYBOOL validate)
{
  int low, high, mid, item, exitvalue, insvalue;

#if 0
  if(mat->row_end_valid && (row > 0) &&
     (ROW_MAT_COL(mat->row_mat[(low = mat->row_end[row-1])]) == column)) {
    insvalue = low;
    exitvalue = low;
    goto Done;
  }
#endif

  insvalue = -1;

  if((column < 1) || (column > mat->columns)) {
    if((column > 0) && !validate) {
      insvalue = mat->col_end[mat->columns];
      exitvalue = -2;
      goto Done;
    }
    report(mat->lp, IMPORTANT, "mat_findins: Column %d out of range\n", column);
    exitvalue = -1;
    goto Done;
  }
  if((row < 0) || (row > mat->rows)) {
    if((row >= 0) && !validate) {
      insvalue = mat->col_end[column];
      exitvalue = -2;
      goto Done;
    }
    report(mat->lp, IMPORTANT, "mat_findins: Row %d out of range\n", row);
    exitvalue = -1;
    goto Done;
  }

  low = mat->col_end[column - 1];
  insvalue = low;
  high = mat->col_end[column] - 1;
  if(low > high) {
    exitvalue = -2;
    goto Done;
  }

 /* Do binary search logic */
  mid = (low+high) / 2;
  item = mat->col_mat[mid].row_nr;
  while(high - low > LINEARSEARCH) {
    if(item < row) {
      low = mid + 1;
      mid = (low+high) / 2;
      item = mat->col_mat[mid].row_nr;
    }
    else if(item > row) {
      high = mid - 1;
      mid = (low+high) / 2;
      item = mat->col_mat[mid].row_nr;
    }
    else {
      low = mid;
      high = mid;
    }
  }

 /* Do linear scan search logic */
  if((high > low) && (high - low <= LINEARSEARCH)) {
    item = mat->col_mat[low].row_nr;
    while((low < high) && (item < row)) {
      low++;
      item = mat->col_mat[low].row_nr;
    }
    if(item == row)
      high = low;
  }

  insvalue = low;
  if((low == high) && (row == item))
    exitvalue = low;
  else {
    if((low < mat->col_end[column]) && (mat->col_mat[low].row_nr < row))
      insvalue++;
    exitvalue = -2;
  }

Done:
  if(insertpos != NULL)
    (*insertpos) = insvalue;
  return( exitvalue );
}

STATIC REAL mat_getitem(MATrec *mat, int row, int column)
{
  int elmnr;

#ifdef DirectOverrideOF
  if((row == 0) && (mat == mat->lp->matA) && (mat->lp->OF_override != NULL))
    return( mat->lp->OF_override[column] );
  else
#endif
  {
    elmnr = mat_findelm(mat, row, column);
    if(elmnr >= 0)
      return(mat->col_mat[elmnr].value);
    else
      return(0);
  }
}

STATIC void mat_multrow(MATrec *mat, int row_nr, REAL mult)
{
  int i, k1, k2;

  if(row_nr == 0) {
    k2 = mat->col_end[0];
    for(i = 1; i <= mat->columns; i++) {
      k1 = k2;
      k2 = mat->col_end[i];
      if((k1 < k2) && (mat->col_mat[k1].row_nr == row_nr))
        mat->col_mat[k1].value *= mult;
    }
  }
  else if(mat_validate(mat)) {
    if(row_nr == 0)
      k1 = 0;
    else
      k1 = mat->row_end[row_nr-1];
    k2 = mat->row_end[row_nr];
    for(i = k1; i < k2; i++)
      ROW_MAT_VALUE(mat->row_mat[i]) *= mult;
  }
}

STATIC void mat_multcol(MATrec *mat, int col_nr, REAL mult)
{
  int    i, ie;
  MYBOOL isA;

#ifdef Paranoia
  if(col_nr < 1 || col_nr > mat->columns) {
    report(mat->lp, IMPORTANT, "mult_column: Column %d out of range\n", col_nr);
    return;
  }
#endif

  isA = (MYBOOL) (mat == mat->lp->matA);

  ie = mat->col_end[col_nr];
  for(i = mat->col_end[col_nr - 1]; i < ie; i++)
    mat->col_mat[i].value *= mult;
  if(isA && (get_Lrows(mat->lp) > 0))
    mat_multcol(mat->lp->matL, col_nr, mult);
}

STATIC MYBOOL mat_setvalue(MATrec *mat, int Row, int Column, REAL Value, MYBOOL doscale)
{
  int    elmnr, lastelm, i;
  MYBOOL isA;

  /* This function is inefficient if used to add new matrix entries in
     other places than at the end of the matrix. OK for replacing existing
     a non-zero value with another non-zero value */
  isA = (MYBOOL) (mat == mat->lp->matA);
  if(isA && mat->is_roworder)
    mat_transpose(mat, TRUE);

  /* Set small numbers to zero */
  if(fabs(Value) < mat->epsvalue)
    Value = 0;
#ifdef DoMatrixRounding
  else
    Value = roundToPrecision(Value, mat->epsvalue);
#endif

  /* Check if we need to update column space */
  if(Column > mat->columns) {
    if(isA)
      inc_col_space(mat->lp, Column - mat->columns);
    else
      inc_matcol_space(mat, Column - mat->columns);
  }

  /* Find out if we already have such an entry, or return insertion point */
  i = mat_findins(mat, Row, Column, &elmnr, FALSE);
  if(i == -1)
    return(FALSE);

  if(isA) {
    if((Row > 0) && mat->lp->is_basic[mat->rows+Column])
      mat->lp->basis_valid = FALSE;
    mat->lp->doInvert = TRUE;
  }

  if(i >= 0) {
    /* there is an existing entry */
    if(fabs(Value) > mat->epsvalue) { /* we replace it by something non-zero */
      if(isA) {
        Value = my_chsign(is_chsign(mat->lp, Row), Value);
        if(doscale && mat->lp->scaling_used)
          Value = scaled_mat(mat->lp, Value, Row,Column);
      }
      mat->col_mat[elmnr].value = Value;
    }
    else { /* setting existing non-zero entry to zero. Remove the entry */
      /* This might remove an entire column, or leave just a bound. No
          nice solution for that yet */

      /* Shift up tail end of the matrix */
      lastelm = mat_nonzeros(mat);
      for(i = elmnr; i < lastelm ; i++)
        mat->col_mat[i] = mat->col_mat[i + 1];
      for(i = Column; i <= mat->columns; i++)
        mat->col_end[i]--;

      mat->row_end_valid = FALSE;
    }
  }
  else if(fabs(Value) > mat->epsvalue) {
    /* no existing entry. make new one only if not nearly zero */
    /* check if more space is needed for matrix */
    if (!inc_mat_space(mat, 1))
      return(FALSE);

    if(Column > mat->columns) {
      if(isA)
        shift_coldata(mat->lp, mat->columns+1, Column - mat->columns);
      else
        mat_shiftcols(mat, mat->columns+1, Column - mat->columns);
    }

    /* Shift down tail end of the matrix by one */
    lastelm = mat_nonzeros(mat);
    for(i = lastelm; i > elmnr ; i--)
      mat->col_mat[i] = mat->col_mat[i - 1];

    /* Set new element */
    mat->col_mat[elmnr].row_nr = Row;
    if(isA) {
      Value = my_chsign(is_chsign(mat->lp, Row), Value);
      if(doscale)
        Value = scaled_mat(mat->lp, Value, Row, Column);
    }
    mat->col_mat[elmnr].value = Value;

    /* Update column indexes */
    for(i = Column; i <= mat->columns; i++)
      mat->col_end[i]++;

    mat->row_end_valid = FALSE;
  }

  if(isA && (mat->lp->var_is_free != NULL) && (mat->lp->var_is_free[Column] > 0))
    return( mat_setvalue(mat, Row, mat->lp->var_is_free[Column], -Value, doscale) );
  return(TRUE);
}

STATIC int mat_findcolumn(MATrec *mat, int matindex)
{
  int j;

  for(j = 1; j <= mat->columns; j++) {
    if(matindex < mat->col_end[j])
      break;
  }
  return(j);
}

STATIC MYBOOL mat_transpose(MATrec *mat, MYBOOL col1_row0)
{
  int     i, j, nz, k;
  MATitem *newmat;
  MYBOOL  status;

  if(col1_row0 && !mat->is_roworder)
    return( FALSE );

  status = mat_validate(mat);
  if(status) {

    /* Create a column-ordered sparse element list; "column" index must be shifted */
    nz = mat_nonzeros(mat);
    if(nz > 0) {
      newmat = (MATitem *) malloc((mat->mat_alloc) * sizeof(*(mat->col_mat)));
      if(col1_row0) {  /* Transposition of fast constraint add mode */
        for(i = nz-1; i >= 0 ; i--) {
          newmat[i] = mat->col_mat[mat->row_mat[i]];
          newmat[i].row_nr = newmat[i].col_nr-1;
        }
      }
      else {           /* Transposition where row index 0 becomes columns+1 */
        j = mat->row_end[0];
        for(i = nz-1; i >= j ; i--) {
          k = i-j;
          newmat[k] = mat->col_mat[mat->row_mat[i]];
          newmat[k].row_nr = newmat[k].col_nr;
        }
        for(i = j-1; i >= 0 ; i--) {
          k = nz-j+i;
          newmat[k] = mat->col_mat[mat->row_mat[i]];
          newmat[k].row_nr = newmat[k].col_nr;
        }
      }
      swapPTR((void **) &mat->col_mat, (void **) &newmat);
      FREE(newmat);
    }

    /* Transfer row start to column start position; must adjust for different offsets */
    if(mat->rows == mat->rows_alloc)
      inc_matcol_space(mat, 1);
    if(col1_row0)
      mat->columns--;
    else {
      j = mat->row_end[0];
      for(i = mat->rows; i >= 1; i--)
        mat->row_end[i] -= j;
      mat->rows++;
      mat->row_end[mat->rows] = nz;
    }
    swapPTR((void **) &mat->row_end, (void **) &mat->col_end);

    /* Swap array sizes */
    swapINT(&mat->rows, &mat->columns);
    swapINT(&mat->rows_alloc, &mat->columns_alloc);

    /* Finally set current storage mode */
    mat->is_roworder = (MYBOOL) !mat->is_roworder;
    mat->row_end_valid = FALSE;
  }
  return(status);
}

/* ---------------------------------------------------------------------------------- */
/* High level matrix inverse and product routines in lp_solve                         */
/* ---------------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------------- */
/* Inverse handling                                                                   */
/* ---------------------------------------------------------------------------------- */
/*
        A brief description of the inversion logic in lp_solve
   -----------------------------------------------------------------

   In order to better understand the code in lp_solve in relation to
   standard matrix-based textbook descriptions, I (KE) will briefly
   explain the conventions and associated matrix algebra.  The matrix
   description of a linear program (as represented by lp_solve) goes
   like this:

           maximize         c'x
           subject to  r <=  Ax <= b
           where       l <=   x <= u

   The matrix A is partitioned into two column sets [B|N], where B is
   a square matrix of "basis" variables containing non-fixed
   variables of the linear program at any given stage and N is the
   submatrix of corresponding non-basic, fixed variables. The
   variables (columns) in N may be fixed at their lower or upper levels.

   Similarly, the c vector is partitioned into the basic and non-basic
   parts [z|n].

   lp_solve stores the objective vector c and the data matrix A in a
   common sparse format where c is put as the 0-th row of A. This may
   be called the "A~" form:

                       A~ = [ c ]
                            [ A ]

   Linear programming involves solving linear equations based on B,
   various updates and bookkeeping operations.  The relationship
   between the common storage of c and A (e.g. A~) vs. the inverse of
   B therefore needs to be understood.  In lp_solve, B is stored in
   an expanded, bordered format using the following (non-singular)
   representation:

                       B~ = [ 1 z ]
                            [ 0 B ]

   Any inversion engine used by lp_solve must therefore explicitly
   represent and handle the implications of this structure for
   associated matrix multiplications.

   The standard matrix formula for computing the inverse of a bordered
   matrix shows what the inversion of B~ actually produces:

                  Inv(B~) = [ 1 -z*Inv(B) ]
                            [ 0   Inv(B)  ]

   The A~ and B~ representations mean that it becomes necessary to be
   aware of the side effects of the presence of the top row when doing
   product operations such as b'N, btran and ftran.  A very nice
   thing about the matrix representation in lp_solve is that a very
   common update in the simplex algorithm (reduced costs) is obtained
   simply by setting 1 at the top of the vector being pre-multiplied
   with Inv(B~).

   However, if the objective vector (c) is changed, the representation
   requires B / B~ to be reinverted.  Also, when doing ftran, btran
   and x'A-type operations, you will patently get the incorrect result
   if you simply copy the operations described in textbooks.  First I'll
   show the results of an ftran operation:

                   Bx = a  ==>  x = ftran(a)

   In lp_solve, this operation solves:

                   [ 1 z ] [y] = [d]
                   [ 0 B ] [x]   [a]

   Using the Inv(B~) expression earlier, the ftran result is:

             [y] = [ 1 -z*Inv(B) ] [d] = [ d - z*Inv(B)*a ]
             [x]   [ 0   Inv(B)  ] [a]   [   Inv(B)*a     ]

   Similarily, doing the left solve - performing the btran calculation:

                   [x y] [ 1 z ] = [d a']
                         [ 0 B ]

   ... will produce the following result in lp_solve:

   [x y] = [d a'] [ 1 -z*Inv(B) ] = [ d | -d*z*Inv(B) + a'*Inv(B) ]
                  [ 0   Inv(B)  ]

   So, if you thought you were computing "a'*Inv(B)", look again.
   In order to produce the desired result, you have to set d to 0
   before the btran operation.

   Equipped with this understanding, I hope that you see that
   the approach in lp_solve is actually pretty convenient.  It
   also makes it far easier to extend functionality by drawing on
   formulas and expressions from LP literature that assume the
   conventional syntax and representation.

                                   Kjell Eikland -- November 2003.

*/

STATIC MYBOOL __WINAPI invert(lprec *lp, MYBOOL shiftbounds)
{
  MYBOOL *usedpos, resetbasis;
  REAL   test;
  int    k, i, j;
  int    singularities, usercolB;

 /* Make sure the tags are correct */
  if(!mat_validate(lp->matA)) {
    lp->spx_status = INFEASIBLE;
    return(FALSE);
  }

 /* Create the inverse management object at the first call to invert() */
  if(lp->invB == NULL)
    lp->bfp_init(lp, lp->rows, 0);
  else
    lp->bfp_preparefactorization(lp);
  singularities = 0;

 /* Must save spx_status since it is used to carry information about
    the presence and handling of singular columns in the matrix */
  if(userabort(lp, MSG_INVERT))
    return(FALSE);

#ifdef Paranoia
  if(lp->spx_trace)
    report(lp, DETAILED, "invert: iteration %6d, inv-length %4d, OF " MPSVALUEMASK " \n",
                         get_total_iter(lp), lp->bfp_colcount(lp), (double) -lp->rhs[0]);
#endif

 /* Store state of pre-existing basis, and at the same time check if
    the basis is I; in this case take the easy way out */
  if(!allocMYBOOL(lp, &usedpos, lp->sum + 1, TRUE)) {
    lp->spx_status = NOMEMORY;
    lp->bb_break = TRUE;
    return(FALSE);
  }
  usedpos[0] = TRUE;
  usercolB = 0;
  for(i = 1; i <= lp->rows; i++) {
    k = lp->var_basic[i];
    if(k > lp->rows)
      usercolB++;
    usedpos[k] = TRUE;
  }
#ifdef Paranoia
  if(!verifyBasis(lp))
    report(lp, SEVERE, "invert: Invalid basis detected (iteration %d).\n",
                       get_total_iter(lp));
#endif

 /* Tally matrix nz-counts and check if we should reset basis
    indicators to all slacks */
  resetbasis = (MYBOOL) ((usercolB > 0) && lp->bfp_canresetbasis(lp));
  k = 0;
  for(i = 1; i <= lp->rows; i++) {
    if(lp->var_basic[i] > lp->rows)
      k += mat_collength(lp->matA, lp->var_basic[i] - lp->rows);
    if(resetbasis) {
      j = lp->var_basic[i];
      if(j > lp->rows)
        lp->is_basic[j] = FALSE;
      lp->var_basic[i] = i;
      lp->is_basic[i] = TRUE;
    }
  }
 /* If we don't reset the basis, optionally sort it by index.
    (experimental code at one time used for debugging) */
#if 0 
  if(!resetbasis) {
    j = lp->rows;
    for(i = 1; i <= lp->rows; i++) {
      if(lp->is_basic[i])
        lp->var_basic[i] = i;
      else {
        j++;
        while((j <= lp->sum) && !lp->is_basic[j])
          j++;
        lp->var_basic[i] = j;
      }
    }
  }
#endif  

 /* Now do the refactorization */
  singularities = lp->bfp_factorize(lp, usercolB, k, usedpos);

 /* Do user reporting */
  if(userabort(lp, MSG_INVERT))
    goto Cleanup;

#ifdef Paranoia
  if(lp->spx_trace) {
    k = lp->bfp_nonzeros(lp, FALSE);
    report(lp, DETAILED, "invert: inv-length %4d, inv-NZ %4d\n",
                         lp->bfp_colcount(lp), k);
  }
#endif

 /* Finalize factorization/inversion */
  lp->bfp_finishfactorization(lp);

  /* Recompute the RHS ( Ref. lp_solve inverse logic and Chvatal p. 121 ) */
#ifdef DebugInv
  blockWriteLREAL(stdout, "RHS-values pre invert", lp->rhs, 0, lp->rows);
#endif
  recompute_solution(lp, shiftbounds);
  restartPricer(lp, AUTOMATIC);
#ifdef DebugInv
  blockWriteLREAL(stdout, "RHS-values post invert", lp->rhs, 0, lp->rows);
#endif

Cleanup:
  /* Check for numerical instability indicated by frequent refactorizations */
  test = get_refactfrequency(lp, FALSE);
  if(test < MIN_REFACTFREQUENCY) {
    test = get_refactfrequency(lp, TRUE);
    report(lp, NORMAL, "invert: Refactorization frequency %.1g indicates numeric instability\n",
                       test);
    lp->spx_status = NUMFAILURE;
  }

  FREE(usedpos);
  return((MYBOOL) (singularities <= 0));
} /* invert */


STATIC MYBOOL fimprove(lprec *lp, REAL *pcol, int *nzidx, REAL roundzero)
{
  REAL   *errors, sdp;
  int    j;
  MYBOOL Ok = TRUE;

  allocREAL(lp, &errors, lp->rows + 1, FALSE);
  if(errors == NULL) {
    lp->spx_status = NOMEMORY;
    Ok = FALSE;
    return(Ok);
  }
  MEMCOPY(errors, pcol, lp->rows + 1);
  lp->bfp_ftran_normal(lp, pcol, nzidx);
  prod_Ax(lp, SCAN_ALLVARS, pcol, NULL, 0, 0.0, -1, errors, NULL);
  lp->bfp_ftran_normal(lp, errors, NULL);

  sdp = 0;
  for(j = 1; j <= lp->rows; j++)
    if(fabs(errors[j])>sdp)
      sdp = fabs(errors[j]);
  if(sdp > lp->epsmachine) {
    report(lp, DETAILED, "Iterative FTRAN correction metric %g", sdp);
    for(j = 1; j <= lp->rows; j++) {
      pcol[j] += errors[j];
      my_roundzero(pcol[j], roundzero);
    }
  }
  FREE(errors);
  return(Ok);
}

STATIC MYBOOL bimprove(lprec *lp, REAL *rhsvector, int *nzidx, REAL roundzero)
{
  int    j;
  REAL   *errors, err, maxerr;
  MYBOOL Ok = TRUE;

  allocREAL(lp, &errors, lp->sum + 1, FALSE);
  if(errors == NULL) {
    lp->spx_status = NOMEMORY;
    Ok = FALSE;
    return(Ok);
  }
  MEMCOPY(errors, rhsvector, lp->sum + 1);

  /* Solve Ax=b for x, compute b back */
  lp->bfp_btran_normal(lp, errors, nzidx);
  prod_xA(lp, SCAN_ALLVARS+USE_BASICVARS, errors, NULL, XRESULT_FREE, 0.0, 1.0, 
                                          errors, NULL);

  /* Take difference with ingoing values, while shifting the column values
     to the rows section and zeroing the columns again */
  for(j = 1; j <= lp->rows; j++)
    errors[j] = errors[lp->rows+lp->var_basic[j]] - rhsvector[j];
  for(j = lp->rows; j <= lp->sum; j++)
    errors[j] = 0;

  /* Solve the b errors for the iterative x adjustment */
  lp->bfp_btran_normal(lp, errors, NULL);

  /* Generate the adjustments and compute statistic */
  maxerr = 0;
  for(j = 1; j <= lp->rows; j++) {
    if(lp->var_basic[j]<=lp->rows) continue;
    err = errors[lp->rows+lp->var_basic[j]];
    if(fabs(err)>maxerr)
      maxerr = fabs(err);
  }
  if(maxerr > lp->epsmachine) {
    report(lp, DETAILED, "Iterative BTRAN correction metric %g", maxerr);
    for(j = 1; j <= lp->rows; j++) {
      if(lp->var_basic[j]<=lp->rows) continue;
      rhsvector[j] += errors[lp->rows+lp->var_basic[j]];
      my_roundzero(rhsvector[j], roundzero);
    }
  }
  FREE(errors);
  return(Ok);
}

STATIC void ftran(lprec *lp, REAL *rhsvector, int *nzidx, REAL roundzero)
{
  if((lp->improve & IMPROVE_FTRAN) && lp->bfp_pivotcount(lp))
    fimprove(lp, rhsvector, nzidx, roundzero);
  else {
    lp->bfp_ftran_normal(lp, rhsvector, nzidx);
  }
}

STATIC void btran(lprec *lp, REAL *rhsvector, int *nzidx, REAL roundzero)
{
  if((lp->improve & IMPROVE_BTRAN) && lp->bfp_pivotcount(lp))
    bimprove(lp, rhsvector, nzidx, roundzero);
  else {
    lp->bfp_btran_normal(lp, rhsvector, nzidx);
  }
}

STATIC MYBOOL fsolve(lprec *lp, int varin, REAL *pcol, int *nzidx, REAL roundzero, REAL ofscalar, MYBOOL prepareupdate)
/* Was setpivcol in versions earlier than 4.0.1.8 - KE */
{
  MYBOOL ok = TRUE;

  if(varin > 0)
    obtain_column(lp, varin, pcol, nzidx, NULL);

 /* Solve, adjusted for objective function scalar */
  pcol[0] *= ofscalar;
  if(prepareupdate)
    lp->bfp_ftran_prepare(lp, pcol, nzidx);
  else
    ftran(lp, pcol, nzidx, roundzero);

  return(ok);

} /* fsolve */


STATIC MYBOOL bsolve(lprec *lp, int row_nr, REAL *rhsvector, int *nzidx, REAL roundzero, REAL ofscalar)
{
  MYBOOL ok = TRUE;

  if(row_nr >= 0)
    row_nr = obtain_column(lp, row_nr, rhsvector, nzidx, NULL);

  /* Solve, adjusted for objective function scalar */
  rhsvector[0] *= ofscalar;
  btran(lp, rhsvector, nzidx, roundzero);

  return(ok);

} /* bsolve */


STATIC int prod_Ax(lprec *lp, int varset, REAL *input, int *nzinput, 
                              int range, REAL roundzero, REAL multfactor, 
                                          REAL *output, int *nzoutput)
/* prod_Ax is only used in fimprove; note that it is NOT VALIDATED/verified as of 20030801 - KE */
{
  int     j, k, colnr, Extrap,
          ib, ie, vb, ve;
  MYBOOL  omitfixed, omitnonfixed;
  REAL    sdp;
  MATitem *matentry;

  /* Find what variable range to scan (default is all) */
  /* First determine the starting position; add from the top, going down */
  Extrap = abs(lp->Extrap);
  vb = 1;
  if(varset & SCAN_ARTIFICIALVARS)
   vb = lp->sum - Extrap + 1;
  if(varset & SCAN_USERVARS)
    vb = lp->rows + 1;
  if(varset & SCAN_SLACKVARS)
    vb = 1;

  /* Then determine the ending position, add from the bottom, going up */
  ve = lp->sum;
  if(varset & SCAN_SLACKVARS)
    ve = lp->rows;
  if(varset & SCAN_USERVARS)
    ve = lp->sum - Extrap;
  if(varset & SCAN_ARTIFICIALVARS)
    ve = lp->sum;

  /* Determine exclusion columns */
  omitfixed = (MYBOOL) ((varset & OMIT_FIXED) != 0);
  omitnonfixed = (MYBOOL) ((varset & OMIT_NONFIXED) != 0);
  if(omitfixed && omitnonfixed)
    return(FALSE);

  /* Scan the basic columns */
  for(j = 1; j <= lp->rows; j++) {
    colnr = lp->var_basic[j];

    /* Do exclusions */
    if(colnr < vb || colnr > ve)
      continue;

    sdp = lp->upbo[colnr];
    if((omitfixed && (sdp == 0)) ||
       (omitnonfixed && (sdp != 0)))
      continue;

    /* Exclusions done, perform the multiplication */
    sdp = multfactor*input[j];
    if(colnr <= lp->rows)               /* A slack variable is in the basis */
      output[colnr] += sdp;
    else {                              /* A normal variable is in the basis */
      colnr -= lp->rows;
      ie = lp->matA->col_end[colnr];
      ib = lp->matA->col_end[colnr - 1];
      for(matentry = lp->matA->col_mat + ib; ib < ie; ib++, matentry++) {
        k = (*matentry).row_nr;
        output[k] += (*matentry).value*sdp;
      }
    }
  }
  roundVector(output+1, lp->rows-1, roundzero);

  return(TRUE);
}

STATIC int prod_xA(lprec *lp, int varset, REAL *input, int *nzinput,
                              int range,  REAL roundzero, REAL ofscalar,
                                          REAL *output, int *nzoutput)
/* Note that the dot product xa is stored at the active column index of A, i.e. of a.
   This means that if the basis only contains non-slack variables, output may point to
   the same vector as input, without overwriting the [0..rows] elements. */
{
  int      i, rownr, varnr, Extrap, vb, ve, ib, ie;
  MYBOOL   omitfixed, omitnonfixed;
  RREAL    vmax;
  REGISTER RREAL v;
  REAL     matValue, *rhsvector;
  int      inz, *rowin, countNZ = 0;
  MATrec   *mat = lp->matA;
  MATitem  *matItem;

  /* Clean output area (only necessary if we are returning the full vector) */
  if(nzoutput == NULL) {
    if(input == output)
      MEMCLEAR(output+lp->rows+1, lp->columns);
    else
      MEMCLEAR(output, lp->sum+1);
  }

  /* Find what variable range to scan - default is {SCAN_USERVARS} */
  /* First determine the starting position; add from the top, going down */
  Extrap = abs(lp->Extrap);
  vb = lp->rows + 1;
  if(varset & SCAN_ARTIFICIALVARS)
    vb = lp->sum - Extrap + 1;
  if(varset & SCAN_USERVARS)
    vb = lp->rows + 1;
  if(varset & SCAN_SLACKVARS)
    vb = 1;

  /* Then determine the ending position, add from the bottom, going up */
  ve = lp->sum;
  if(varset & SCAN_SLACKVARS)
    ve = lp->rows;
  if(varset & SCAN_USERVARS)
    ve = lp->sum - Extrap;
  if(varset & SCAN_ARTIFICIALVARS)
    ve = lp->sum;

  /* Adjust for partial pricing */
  if(varset & SCAN_PARTIALBLOCK) {
    if(vb > lp->rows)
      vb = MAX(vb, partial_blockStart(lp, FALSE));
    ve = MIN(ve, partial_blockEnd(lp, FALSE));
  }

  /* Determine exclusion columns */
  omitfixed = (MYBOOL) ((varset & OMIT_FIXED) != 0);
  omitnonfixed = (MYBOOL) ((varset & OMIT_NONFIXED) != 0);
  if(omitfixed && omitnonfixed)
    return(FALSE);

  /* Scan the target colums */
  vmax = 0;
  for(varnr = vb, rhsvector = output + vb;
      varnr <= ve; varnr++, rhsvector++) {

    /* Skip gap in the specified colum scan range (possibly user variables) */
    if((varnr > lp->rows) && (varnr <= lp->sum-Extrap) && !(varset & SCAN_USERVARS))
      continue;

    /* Find if the variable is in the scope - default is {Ø} */
    i = lp->is_basic[varnr];
    if((varset & USE_BASICVARS) > 0 && (i))
      ;
    else if((varset & USE_NONBASICVARS) > 0 && (!i))
      ;
    else
      continue;

    v = lp->upbo[varnr];
    if((omitfixed && (v == 0)) ||
       (omitnonfixed && (v != 0)))
      continue;

    if(varnr <= lp->rows) {
      v = input[varnr];
    }
    else {
      i = varnr - lp->rows;
      v = 0;
      ie = mat->col_end[i];
      ib = mat->col_end[i - 1];
      if(ib < ie) {

        /* Do dense input vector version */
        if(nzinput == NULL) {

          /* Handle the objective row specially */
          matItem = mat->col_mat + ib;
          rownr = (*matItem).row_nr;
          if(rownr == 0) {
            matValue = (*matItem).value;
            ib++;
            matItem++;
          }
          else {
            rownr = 0;
            matValue = 0;
          }
          if(modifyOF1(lp, varnr, &matValue, ofscalar))
            v += input[rownr] * matValue;

          /* Then loop over all regular rows */
          if(ib < ie)
          for(; ib < ie; ib++, matItem++) {
            v += input[(*matItem).row_nr] * (*matItem).value;
          }
        }

        /* Do sparse input vector version */
        else {

          /* Initialize pointers */
          inz = 1;
          rowin = nzinput+inz;
          matItem = mat->col_mat + ib;
          rownr = matItem->row_nr;
          ie--;

          /* Handle OF row separately (since it can be overridden) */
          if(*rowin == 0) {
            if(rownr == 0) {
              matValue = matItem->value;
              ib++;
              /* Step forward at right */
              if(ib <= ie) {
                matItem++;
                rownr = matItem->row_nr;
              }
            }
            else
              matValue = 0;
            if(modifyOF1(lp, varnr, &matValue, ofscalar))
              v += input[0]*matValue;
            /* Step forward at left */
            inz++;
            rowin++;
          }

          /* Then loop over all non-OF rows */
          while((inz <= *nzinput) && (ib <= ie)) {

           /* Try to synchronize at right */
            while((*rowin > rownr) && (ib < ie)) {
              ib++;
              matItem++;
              rownr = matItem->row_nr;
            }
            /* Try to synchronize at left */
            while((*rowin < rownr) && (inz < *nzinput)) {
              inz++;
              rowin++;
            }
            /* Perform dot product operation if there was a match */
            if(*rowin == rownr) {
              v += input[*rowin] * matItem->value;
              /* Step forward at left */
              inz++;
              rowin++;
            }
          }
        }
      }
#ifndef Prod_xA_RoundRelative
      my_roundzero(v, roundzero);
#endif
    }
    vmax = MAX(vmax, fabs((REAL) v));
    if((v != 0) && ((range == XRESULT_FREE) || (v*range > 0))) {
      countNZ++;
      if(nzoutput != NULL)
        nzoutput[countNZ] = varnr;
    }
    *rhsvector = (REAL) v;
  }

  /* Check if we should do relative rounding */
#ifdef Prod_xA_RoundRelative
  if((roundzero > 0) && (nzoutput != NULL)) {
    ie = 0;
    for(i = 1; i <= countNZ;  i++) {
      rownr = nzoutput[i];
      if(fabs(output[rownr])/vmax < roundzero)
        output[rownr] = 0;
      else if((range == XRESULT_FREE) || (output[rownr]*range > 0)) {
        ie++;
        nzoutput[ie] = rownr;
      }
    }
    countNZ = ie;
  }
#endif

  if(nzoutput != NULL)
    *nzoutput = countNZ;
  return(countNZ);
}

STATIC void prod_xA2(lprec *lp, REAL *prow, int prange, REAL proundzero, int *nzprow,
                                REAL *drow, int drange, REAL droundzero, int *nzdrow, REAL ofscalar)
{
#ifdef UseDouble_prod_xA
  prod_xA(lp, SCAN_ALLVARS+
              SCAN_PARTIALBLOCK+
              USE_NONBASICVARS+OMIT_FIXED,
              prow, NULL, XRESULT_RC, proundzero, ofscalar, 
              prow, nzprow);
  prod_xA(lp, SCAN_ALLVARS+
              SCAN_PARTIALBLOCK+
              USE_NONBASICVARS+OMIT_FIXED,
              drow, NULL, XRESULT_FREE, droundzero, ofscalar, 
              drow, nzdrow);
#else
  int      i, ii, varnr, rownr, ib, ie;
  RREAL    dmax, pmax;
  REGISTER RREAL d, p;
  MATrec   *mat = lp->matA;
  REAL     matValue;
  MATitem  *matItem;

  /* Loop over slack variables only if we do sparse mapping; since slack variable
     coefficients are all 1, values in the incoming vectors are preserved */
  pmax = 0;
  dmax = 0;
  i  = partial_blockStart(lp, FALSE);
  ii = partial_blockEnd(lp, FALSE);
  if(ii == lp->sum)
    ii -= abs(lp->Extrap);
  if((nzprow != NULL) || (nzdrow != NULL)) {
    if(nzprow != NULL)
      *nzprow = 0;
    if(nzdrow != NULL)
      *nzdrow = 0;
    for(; i <= lp->rows; i++)
      if(!lp->is_basic[i] && (lp->upbo[i] > 0)) {
        pmax = MAX(pmax, fabs(prow[i]));
        dmax = MAX(dmax, fabs(drow[i]));              
        if((nzprow != NULL) && (prow[i] != 0) && 
           ((prange == XRESULT_FREE) || (prow[i]*my_chsign(lp->is_lower[i], -1) > 0))) {
          (*nzprow)++;
          nzprow[*nzprow] = i;
        }
        if((nzdrow != NULL) && (drow[i] != 0) &&
           ((drange == XRESULT_FREE) || (drow[i]*my_chsign(lp->is_lower[i], -1) > 0))) {
          (*nzdrow)++;
          nzdrow[*nzdrow] = i;
        }
      }
  }

  /* Loop over user variables or any active subset */
  i = MAX(i, lp->rows + 1);
  for(; i <= ii; i++) {

    varnr = i - lp->rows;

    /* Only non-basic user variables */
    if(lp->is_basic[i])
      continue;

    /* Use only non-fixed variables */
    if(lp->upbo[i] == 0)
      continue;

    p = 0;
    d = 0;
    ie = mat->col_end[varnr];
    ib = mat->col_end[varnr - 1];

    if(ib < ie) {

      /* Handle the objective row specially */
      matItem = mat->col_mat + ib;
      rownr = (*matItem).row_nr;
      if(rownr == 0) {
        matValue = (*matItem).value;
        ib++;
        matItem++;
      }
      else {
        rownr = 0;
        matValue = 0;
      }
      if(modifyOF1(lp, i, &matValue, ofscalar)) {
        p += prow[rownr] * matValue;
        d += drow[rownr] * matValue;
      }

      /* Then loop over all regular rows */
      if(ib < ie)
      for( ; ib < ie; ib++, matItem++) {
        rownr = (*matItem).row_nr;
        matValue = (*matItem).value;
        p += prow[rownr] * matValue;
        d += drow[rownr] * matValue;
      }

    }

    my_roundzero(p, proundzero);
    pmax = MAX(pmax, fabs((REAL) p));
    prow[i] = (REAL) p;
    if((nzprow != NULL) && (p != 0) &&
       ((prange == XRESULT_FREE) || (p*my_chsign(lp->is_lower[i], -1) > 0))) {
      (*nzprow)++;
      nzprow[*nzprow] = i;
    }
    my_roundzero(d, droundzero);
    dmax = MAX(dmax, fabs((REAL) d));
    drow[i] = (REAL) d;
    if((nzdrow != NULL) && (d != 0) &&
       ((drange == XRESULT_FREE) || (d*my_chsign(lp->is_lower[i], -1) > 0))) {
      (*nzdrow)++;
      nzdrow[*nzdrow] = i;
    }
  }

  /* Check if we should do relative rounding */
#ifdef Prod_xA_RoundRelative
  if((proundzero > 0) && (nzprow != NULL)) {
    ie = 0;
    for(i = 1; i <= *nzprow;  i++) {
      rownr = nzprow[i];
      if(fabs(prow[rownr])/pmax < proundzero)
        prow[rownr] = 0;
      else if((prange == XRESULT_FREE) || (prow[rownr]*my_chsign(lp->is_lower[rownr], -1) > 0)) {
        ie++;
        nzprow[ie] = rownr;
      }
    }
    *nzprow = ie;
  }
  if((droundzero > 0) && (nzdrow != NULL)) {
    ie = 0;
    for(i = 1; i <= *nzdrow;  i++) {
      rownr = nzdrow[i];
      if(fabs(drow[rownr])/dmax < droundzero)
        drow[rownr] = 0;
      else if((drange == XRESULT_FREE) || (drow[rownr]*my_chsign(lp->is_lower[rownr], -1) > 0)) {
        ie++;
        nzdrow[ie] = rownr;
      }
    }
    *nzdrow = ie;
  }
#endif

#endif

}

STATIC void bsolve_xA2(lprec *lp, int row_nr1, REAL *vector1, REAL roundzero1, int *nzvector1,
                                  int row_nr2, REAL *vector2, REAL roundzero2, int *nzvector2)
{
  REAL ofscalar = 1.0;
  int  rc_range = XRESULT_RC;

 /* Clear and initialize first vector */
  if(nzvector1 == NULL)
    MEMCLEAR(vector1, lp->sum + 1);
  else
    MEMCLEAR(vector1, lp->rows + 1);
  vector1[row_nr1] = 1;
/*  lp->workrowsINT[0] = 1;
  lp->workrowsINT[1] = row_nr1; */

  if(vector2 == NULL) {
    lp->bfp_btran_normal(lp, vector1, NULL);
    prod_xA(lp, SCAN_USERVARS+
                SCAN_PARTIALBLOCK+
                USE_NONBASICVARS+OMIT_FIXED,
                vector1, NULL, rc_range, roundzero1, ofscalar*0,
                vector1, nzvector1);
  }
  else {

   /* Clear and initialize second vector */
    if(nzvector2 == NULL)
      MEMCLEAR(vector2, lp->sum + 1);
    else
      MEMCLEAR(vector2, lp->rows + 1);
    vector2[row_nr2] = 1;
/*    lp->workrowsINT[2] = 1;
    lp->workrowsINT[3] = row_nr2; */

   /* A double BTRAN equation solver process is implemented "in-line" below in
      order to save time and to implement different rounding for the two */
    lp->bfp_btran_double(lp, vector1, NULL, vector2, NULL);

   /* Multiply solution vectors with matrix values */
    prod_xA2(lp, vector1, rc_range,     roundzero1, nzvector1,
                 vector2, XRESULT_FREE, roundzero2, nzvector2, ofscalar);
  }
}

