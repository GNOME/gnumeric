
/*
   ----------------------------------------------------------------------------------
   Crash management routines in lp_solve v5.0+
   ----------------------------------------------------------------------------------
    Author:        Kjell Eikland
    Contact:       kjell.eikland@broadpark.no
    License terms: LGPL.

    Requires:      lp_lib.h, lp_utils.h, lp_matrix.h

    Release notes:
    v1.0.0  1 April   2004      First version.

   ----------------------------------------------------------------------------------
*/

#include "commonlib.h"
#include "lp_lib.h"
#include "lp_utils.h"
#include "lp_matrix.h"
#include "lp_crash.h"

#ifdef FORTIFY
# include "lp_fortify.h"
#endif


MYBOOL crash_basis(lprec *lp)
{
  int     i;
  MATrec  *mat = lp->matA;

  /* Initialize basis indicators */
  if(!lp->basis_valid)
    default_basis(lp);
  else
    lp->var_basic[0] = FALSE;
  for(i = 0; i <= lp->sum; i++)
    lp->is_basic[i] = FALSE;
  for(i = 1; i <= lp->rows; i++)
    lp->is_basic[lp->var_basic[i]] = TRUE;

  if((lp->crashmode == CRASH_MOSTFEASIBLE) && mat_validate(mat)) {
    /* The logic here follows Maros */
    LLrec   *rowLL = NULL, *colLL = NULL;
    MATitem *matelem;
    int     ii, rx, cx, ix;
    REAL    wx, tx, *rowMAX = NULL, *colMAX = NULL;
    int     *rowNZ = NULL, *colNZ = NULL, *rowWT = NULL, *colWT = NULL;

    report(lp, NORMAL, "crash_basis: Basis crashing selected\n");

    /* Tally row and column non-zero counts */
    allocINT(lp,  &rowNZ, lp->rows+1,     TRUE);
    allocINT(lp,  &colNZ, lp->columns+1,  TRUE);
    allocREAL(lp, &rowMAX, lp->rows+1,    FALSE);
    allocREAL(lp, &colMAX, lp->columns+1, FALSE);
    for(i = 0, matelem = mat->col_mat; i < mat_nonzeros(mat); i++, matelem++) {
      rx = (*matelem).row_nr;
      cx = (*matelem).col_nr;
      wx = fabs((*matelem).value);
      rowNZ[rx]++;
      colNZ[cx]++;
      if(i == 0) {
        rowMAX[rx] = wx;
        colMAX[cx] = wx;
        colMAX[0]  = wx;
      }
      else {
        rowMAX[rx] = my_max(rowMAX[rx], wx);
        colMAX[cx] = my_max(colMAX[cx], wx);
        colMAX[0]  = my_max(colMAX[0],  wx);
      }
    }
    /* Reduce counts for small magnitude to preserve stability */
    for(i = 0, matelem = mat->col_mat; i < mat_nonzeros(mat); i++, matelem++) {
      rx = (*matelem).row_nr;
      cx = (*matelem).col_nr;
      wx = fabs((*matelem).value);
#ifdef CRASH_SIMPLESCALE
      if(wx < CRASH_THRESHOLD * colMAX[0]) {
        rowNZ[rx]--;
        colNZ[cx]--;
      }
#else
      if(wx < CRASH_THRESHOLD * rowMAX[rx])
        rowNZ[rx]--;
      if(wx < CRASH_THRESHOLD * colMAX[cx])
        colNZ[cx]--;
#endif
    }

    /* Set up priority tables */
    allocINT(lp, &rowWT, lp->rows+1, TRUE);
    createLink(lp->rows,    &rowLL, NULL);
    for(i = 1; i <= lp->rows; i++) {
      if(get_constr_type(lp, i)==EQ)
        ii = 3;
      else if(lp->upbo[i] < lp->infinite)
        ii = 2;
      else if(fabs(lp->rhs[i]) < lp->infinite)
        ii = 1;
      else
        ii = 0;
      rowWT[i] = ii;
      if(ii > 0)
        appendLink(rowLL, i);
    }
    allocINT(lp, &colWT, lp->columns+1, TRUE);
    createLink(lp->columns, &colLL, NULL);
    for(i = 1; i <= lp->columns; i++) {
      ix = lp->rows+i;
      if(is_free(lp, i))
        ii = 3;
      else if(lp->upbo[ix] >= lp->infinite)
        ii = 2;
      else if(fabs(lp->upbo[ix]-lp->lowbo[ix]) > lp->epsmachine)
        ii = 1;
      else
        ii = 0;
      colWT[i] = ii;
      if(ii > 0)
        appendLink(colLL, i);
    }

    /* Loop over all basis variables */
    for(i = 1; i <= lp->rows; i++) {

      /* Select row */
      rx = 0;
      wx = -lp->infinite;
      for(ii = firstActiveLink(rowLL); ii > 0; ii = nextActiveLink(rowLL, ii)) {
        tx = rowWT[ii] - CRASH_SPACER*rowNZ[ii];
        if(tx > wx) {
          rx = ii;
          wx = tx;
        }
      }
      if(rx == 0)
        break;
      removeLink(rowLL, rx);

      /* Select column */
      cx = 0;
      wx = -lp->infinite;
      for(ii = mat->row_end[rx-1]; ii < mat->row_end[rx]; ii++) {

        /* Update NZ column counts for row selected above */
        tx = fabs(ROW_MAT_VALUE(mat->row_mat[ii]));
        ix = ROW_MAT_COL(mat->row_mat[ii]);
#ifdef CRASH_SIMPLESCALE
        if(tx >= CRASH_THRESHOLD * colMAX[0])
#else
        if(tx >= CRASH_THRESHOLD * colMAX[ix])
#endif
          colNZ[ix]--;
        if(!isActiveLink(colLL, ix) || (tx < CRASH_THRESHOLD * rowMAX[rx]))
          continue;

        /* Now do the test for best pivot */
        tx = my_sign(get_OF_raw(lp, lp->rows+ix)) - my_sign(ROW_MAT_VALUE(mat->row_mat[ii]));
        tx = colWT[ix] + CRASH_WEIGHT*tx - CRASH_SPACER*colNZ[ix];
        if(tx > wx) {
          cx = ix;
          wx = tx;
        }
      }
      if(cx == 0)
        break;
      removeLink(colLL, cx);

      /* Update row NZ counts */
      for(ii = mat->col_end[cx-1]; ii < mat->col_end[cx]; ii++) {
        wx = fabs(mat->col_mat[ii].value);
        ix = mat->col_mat[ii].row_nr;
#ifdef CRASH_SIMPLESCALE
        if(wx >= CRASH_THRESHOLD * colMAX[0])
#else
        if(wx >= CRASH_THRESHOLD * rowMAX[ix])
#endif
          rowNZ[ix]--;
      }

      /* Set new basis variable */
      setBasisVar(lp, rx, lp->rows+cx);
    }

    /* Clean up */
    FREE(rowNZ);
    FREE(colNZ);
    FREE(rowMAX);
    FREE(colMAX);
    FREE(rowWT);
    FREE(colWT);
    freeLink(&rowLL);
    freeLink(&colLL);
  }
  return( TRUE );
}

