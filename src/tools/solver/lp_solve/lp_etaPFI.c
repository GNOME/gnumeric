
/*
    Modularized simplex inverse modules - w/interface for lp_solve v5.0+
   ----------------------------------------------------------------------------------
    Author:        Michel Berkelaar (to lp_solve v3.2),
                   Kjell Eikland
    Contact:       kjell.eikland@broadpark.no
    License terms: LGPL.

    Requires:      lp_etaPFI.h, lp_lib.h, lp_colamdMDO.h

    Release notes:
    v1.0    1 September 2003    Implementation of the original lp_solve product form
                                of the inverse using a sparse eta matrix format with
                                a significant zero'th index containing the objective.
                                The new implementation includes optimal column
                                ordering at reinversion using the colamd library.
                                For lp_solve B-inverse details confer the text at the
                                beginning of lp_lib.cpp.
    v2.0    1 April 2004        Repackaged and streamlined for both internal and
                                external usage, using the new inverse/factorization
                                interface library.
    v2.0.1  23 May 2004         Moved mustrefact() function into the BFP structure.

   ----------------------------------------------------------------------------------
*/

#include <stdlib.h>
#include <malloc.h>

#include "lp_lib.h"
#include "commonlib.h"
#include "lp_etaPFI.h"

#ifdef FORTIFY
# include "lp_fortify.h"
#endif


/* Include routines common to inverse implementations; unfortunately,
   etaPFI requires the optional shared routines to be unique. */
#include "lp_BFP1.c"


/* MUST MODIFY */
char * BFP_CALLMODEL bfp_name(void)
{
#if INVERSE_ACTIVE == INVERSE_LEGACY
  return( "etaPFI v1.0" );
#else
  return( "etaPFI v2.0" );
#endif
}

/* MUST MODIFY */
MYBOOL BFP_CALLMODEL bfp_init(lprec *lp, int size, int delta)
{
  INVrec *eta;

  eta = (INVrec *) calloc(1, sizeof(*lp->invB));
  lp->invB = eta;

  bfp_resize(lp, ETA_START_SIZE);
  if(delta <= 0)
    delta = bfp_pivotmax(lp);
  bfp_pivotalloc(lp, size+delta);

  bfp_restart(lp);
  bfp_preparefactorization(lp);
  eta->extraD = lp->infinite;
  eta->num_refact = 0;

  eta->statistic1 = 0;
  eta->statistic2 = delta;

  return(TRUE);
}


MYBOOL BFP_CALLMODEL bfp_restart(lprec *lp)
{
  INVrec *eta;

  eta = lp->invB;
  if(eta == NULL)
    return( FALSE );

  eta->status = BFP_STATUS_SUCCESS;
  eta->max_Bsize = 0;
  eta->max_colcount = 0;
  eta->max_etasize = 0;
  eta->num_refact = 0;
  eta->pcol = NULL;
/*  eta->set_Bidentity = FALSE; */
  eta->extraD = lp->infinite;
  eta->num_pivots = 0;

  eta->last_colcount = 0;
  eta->statistic1 = 0;
  eta->statistic2 = bfp_pivotmax(lp);

  return( TRUE );
}

void BFP_CALLMODEL bfp_pivotalloc(lprec *lp, int newsize)
{
  INVrec *eta;
  MYBOOL isfirst;

  eta = lp->invB;
  isfirst = (MYBOOL) (eta->eta_col_end == NULL);
  newsize += 1;
  allocINT(lp, &eta->eta_col_end, newsize, AUTOMATIC);
  allocINT(lp, &eta->eta_col_nr, newsize, AUTOMATIC);
  if(isfirst)
    eta->eta_col_end[0] = 0;
}


void BFP_CALLMODEL bfp_resize(lprec *lp, int newsize)
{
  INVrec *eta;

  eta = lp->invB;
  if(eta->eta_matalloc == 0)
    eta->eta_matalloc = newsize;
  else {
    while(eta->eta_matalloc <= newsize)
      eta->eta_matalloc += eta->eta_matalloc / RESIZEFACTOR;
  }
  allocREAL(lp, &eta->eta_value, eta->eta_matalloc + 1, AUTOMATIC);
  allocINT(lp, &eta->eta_row_nr, eta->eta_matalloc + 1, AUTOMATIC);
}


void BFP_CALLMODEL bfp_free(lprec *lp)
{
  INVrec *eta;

  eta = lp->invB;
  if(eta == NULL)
    return;

  FREE(eta->eta_value);
  FREE(eta->eta_row_nr);
  FREE(eta->eta_col_end);
  FREE(eta->eta_col_nr);
  FREE(eta);
  lp->invB = NULL;
}


/* MUST MODIFY */
MYBOOL BFP_CALLMODEL bfp_canresetbasis(lprec *lp)
{
  return( TRUE );
}

int BFP_CALLMODEL bfp_preparefactorization(lprec *lp)
{
  INVrec *eta;

  eta = lp->invB;

  /* Finish any outstanding business */
  if(eta->is_dirty == AUTOMATIC)
    bfp_finishfactorization(lp);

  /* Reset additional indicators */
  eta->force_refact = FALSE;
  eta->num_refact++;
  eta->eta_colcount = 0;

  /* Signal that we are reinverting */
  eta->is_dirty = AUTOMATIC;

 /* Set time of start of current refactorization cycle */
  eta->time_refactstart = timeNow();
  eta->time_refactnext  = 0;
  
  return( 0 );
}


void BFP_CALLMODEL bfp_finishfactorization(lprec *lp)
{
  INVrec *eta;

  eta = lp->invB;

  /* Collect and optionally report statistics */
  if((lp->bb_totalnodes <= 1) && (lp->verbose & MSG_PERFORMANCE)) {
    REAL hold;
    int  stepsize = 5;

    /* Report size of eta */
    hold = bfp_efficiency(lp);
    if((hold >= 10) && (hold >= stepsize*(1 + (int) eta->statistic1 / stepsize)))
      lp->report(lp, NORMAL, "Reduced speed with inverse density at %.1fx basis matrix.\n",
                             hold);
    eta->statistic1 = my_max(eta->statistic1, hold);
    /* Report numeric stability */
    hold = (REAL) (lp->total_iter-lp->total_bswap)/(bfp_refactcount(lp)+1);
    if((hold <= 30) && (hold <= stepsize*(1 + (int) eta->statistic2 / stepsize)))
      lp->report(lp, NORMAL, "Reduced numeric accuracy with %.1f pivots/reinverse.\n",
                             hold);
    eta->statistic2 = my_min(eta->statistic2, hold);
  }
  eta->last_colcount = bfp_colcount(lp);
  eta->max_colcount = my_max(eta->max_colcount, eta->last_colcount);
  eta->max_etasize = my_max(eta->max_etasize, bfp_nonzeros(lp, FALSE));

  /* Signal that we done reinverting */
  lp->invB->is_dirty = FALSE;
  lp->justInverted = TRUE;
  lp->doInvert = FALSE;

  /* Store information about the current inverse */
  eta->extraD = lp->Extrad;
  eta->num_pivots = 0;

}


int BFP_CALLMODEL bfp_colcount(lprec *lp)
{
  return(lp->invB->eta_colcount);
}


int BFP_CALLMODEL bfp_nonzeros(lprec *lp, MYBOOL maximum)
{
  if(maximum == TRUE)
    return(lp->invB->max_etasize);
  else if(maximum == AUTOMATIC)
    return(lp->invB->max_Bsize);
  else
    return(lp->invB->eta_col_end[lp->invB->eta_colcount]);
}


int BFP_CALLMODEL bfp_memallocated(lprec *lp)
{
  return(lp->invB->eta_matalloc);
}


int bfp_ETApreparepivot(lprec *lp, int row_nr, int col_nr, REAL *pcol, MYBOOL *frow)
/* Find optimal pivot row and do any column preprocessing before
   being added to the eta file -- Only used in bfp_refactorize() */
{
  int  i, rowalt, refnr;
  REAL hold, test;

#if 0
  fsolve(lp, col_nr, pcol, NULL, lp->epsmachine, 1.0, TRUE);
#else
  i = lp->get_lpcolumn(lp, col_nr, pcol, NULL, NULL);
  bfp_ftran_prepare(lp, pcol, NULL);
#endif

  if(frow == NULL) {
    if(fabs(pcol[row_nr]) < lp->epsmachine)
      return( -1 );
    else
      return( row_nr );
  }

  /* Find largest coefficient row available to be pivoted */
  refnr = row_nr;
  row_nr  = lp->rows + 1;
  rowalt = 0;
  hold  = -lp->infinite;
  /* Loop over rows that currently contain slacks;
     (i.e. the positions that are not already taken by another pivot) */
#ifdef UseMarkowitzStatistic
  i = 0;
  while((i = nextActiveLink((LLrec *) frow, i)) != 0) {
#else
  for(i = 1; i <= lp->rows; i++)
    if(frow[i] == FALSE) {
#endif
      /* Get largest absolute value */
      test = fabs(pcol[i]);
      if(test > hold) {
        hold = test;
        row_nr = i;
#ifdef LegacyEtaPivotChoice
        if(hold > lp->epspivot)
          break;
#endif
      }
      if((rowalt == 0) && (test == 1))   /* Unit row */
        rowalt = i;
    }

  if((row_nr > lp->rows) || (hold < lp->epsmachine))
    row_nr = -1;

  return( row_nr );

}

int bfp_ETApivotrow(lprec *lp, int datacolumn)
/* Get pivot row for a particular data column */
{
  INVrec *eta;
  int    n = -1;

  eta = lp->invB;
  if(datacolumn <= eta->last_colcount) {
    n = eta->eta_col_end[datacolumn] - 1;
    n = eta->eta_row_nr[n];
    if(n < 1) {
      lp->report(lp, CRITICAL, "bfp_ETApivotrow: Invalid pivot row identified");
      n = -1;
    }
  }
  return( n );
}


void bfp_ETAupdateCounts(lprec *lp, MYBOOL *usedpos, int rownr, int colnr)
{
  usedpos[rownr] = AUTOMATIC;
  usedpos[colnr] = AUTOMATIC;
}
void bfp_ETAreduceCounts(lprec *lp, MYBOOL *usedpos,
                         int *rownum, int *colnum, int rownr, int colnr, MYBOOL defer)
{
  int    i, j, k;
  MATrec *mat = lp->matA;

  /* Reduce row NZ counts */
  if(rownum != NULL) {
    for(j = mat->col_end[colnr - 1]; j < mat->col_end[colnr]; j++) {
      i = mat->col_mat[j].row_nr;
      if(usedpos[i] == FALSE)
        rownum[i]--;
    }
    rownum[rownr] = -1;
  }
  usedpos[rownr] = AUTOMATIC+defer; /* To distinguish it from basic slacks! */

  /* Reduce column NZ counts */
  if(colnum != NULL) {
    k = mat->row_end[rownr];
    for(j = mat->row_end[rownr - 1]; j < k; j++) {
      i = ROW_MAT_COL(mat->row_mat[j]);
      if(usedpos[lp->rows + i] == TRUE)
        colnum[i]--;
    }
    colnum[colnr] = -1;
  }
  usedpos[lp->rows+colnr] = AUTOMATIC+defer;

}

void bfp_ETAsimpleiteration(lprec *lp, int row_nr, int col_nr, REAL *pcol)
/* Called from bfp_refactorize() with two cases;
    pcol == NULL : A singleton row or column that allows easy elimination,
    pcol != NULL : A dense row/column combination presolved with ftran    */
{
  bfp_prepareupdate(lp, row_nr, col_nr, pcol);
  lp->set_basisvar(lp, row_nr, col_nr);
  bfp_finishupdate(lp, FALSE);
} /* bfp_ETAsimpleiteration */


int BFP_CALLMODEL bfp_factorize(lprec *lp, int uservars, int Bsize, MYBOOL *usedpos)
{
  REAL    *pcol, hold;
  int     *colnum, *rownum, *col, *row;
  int     *mdo = NULL;
#ifdef UseMarkowitzStatistic
  REAL    absval, testval;
  LLrec   *nextrow;
  int     ii, jj;
#endif
  int     k, kk, i, j, numit, rownr, colnr;
  int     singularities = 0;
  MATitem *matentry;
  MATrec  *mat = lp->matA;

 /* Check if there is anyting to do */
  lp->invB->max_Bsize = my_max(lp->invB->max_Bsize, Bsize+(1+lp->rows-uservars));
  if(uservars == 0)
    return(singularities);

 /* Allocate other necessary working arrays */
  allocINT(lp,  &col,    lp->rows + 1, TRUE);
  allocINT(lp,  &row,    lp->rows + 1, TRUE);
  allocREAL(lp, &pcol,   lp->rows + 1, TRUE);
  allocINT(lp,  &rownum, lp->rows + 1, TRUE);
  allocINT(lp,  &colnum, lp->columns + 1, TRUE);

 /* Get (net/un-eliminated) row and column entry counts for basic user variables */
#ifdef ExcludeCountOrderOF
  usedpos[0] = TRUE;
#else
  usedpos[0] = FALSE;
#endif
  for(j = 1; j <= lp->columns; j++) {

    /* If it is a basic user variable...*/
    if(usedpos[lp->rows+j]) {
      i = mat->col_end[j - 1];
      kk = mat->col_end[j];

      /* Count relevant non-zero values */
      for(matentry = mat->col_mat + i, numit = 0; i < kk; i++, matentry++, numit++) {
        k = (*matentry).row_nr;

        /* Check for objective row phase 1 adjustments;
           increment count only if necessary following modifyOF1() test */
        if((numit == 0) && !usedpos[0]) {
          if(k == 0)
            hold = (*matentry).value;
          else
            hold = 0;
          if(!lp->modifyOF1(lp, lp->rows+j, &hold, 1.0)) {
            if(k == 0)
              continue;
          }
          else if(k > 0) {
            numit++;
            colnum[j]++;
            rownum[0]++;
          }
        }

       /* Exclude pre-eliminated rows due to basic slacks;
          this is a characteristic of the eta-model that presumes an
          initial basis with all slacks; i.e. an identity matrix */
        if(!usedpos[k]) {
          numit++;
          colnum[j]++;
          rownum[k]++;
        }
      }
    }
  }

 /* Initialize counters */
  numit = 0;
  k = 0;

#ifdef UseMarkowitzStatistic
 /* Create a linked list for the available pivot rows */
  createLink(lp->rows, &nextrow, usedpos);
#endif

 /* Loop over constraint rows, hunting for ROW singletons */
#ifdef ReprocessSingletons
Restart:
#endif
  kk = 0;
  for(rownr = 1; rownr <= lp->rows; rownr++) {

   /* Only process if the corresponding slack is non-basic (basis slot is available) */
    if((usedpos[rownr] == FALSE) && (rownum[rownr] == 1)) {

     /* Find first basic user column available to be pivoted */
      j = mat->row_end[rownr - 1];
      i = mat->row_end[rownr];
      while((j < i) && (usedpos[lp->rows + ROW_MAT_COL(mat->row_mat[j])] != TRUE))
        j++;
#ifdef Paranoia
      if(j >= i)
        lp->report(lp, SEVERE, "bfp_factorize: No column to pivot IN due to an internal error\n");
#endif
      colnr = ROW_MAT_COL(mat->row_mat[j]);

     /* Reduce item counts for the selected pivot column/row */
#ifdef UseMarkowitzStatistic
      removeLink(nextrow, rownr);
#endif
      bfp_ETAreduceCounts(lp, usedpos, rownum, colnum, rownr, colnr, TRUE);

     /* Perform the pivot */
      bfp_ETAsimpleiteration(lp, rownr, lp->rows+colnr, NULL);
      k++;
      kk++;
    }
  }

 /* Loop over columns, hunting for COLUMN singletons;
    (only store row and column indexes for pivoting in at the end of refactorization) */
  if(k < lp->rows)
  for(colnr = 1; (k < lp->rows) && (colnr <= lp->columns); colnr++) {

   /* Only accept basic user columns not already pivoted in */
    if((usedpos[lp->rows + colnr] == TRUE) && (colnum[colnr] == 1)) {

     /* Find first available basis column to be pivoted out */
      j = mat->col_end[colnr - 1];
      i = mat->col_end[colnr];
      for(matentry = mat->col_mat + j; (j < i) && (usedpos[(*matentry).row_nr] != FALSE);
          j++, matentry++);
#ifdef Paranoia
      if(j >= i)
        lp->report(lp, SEVERE, "bfp_factorize: No column to pivot OUT due to an internal error\n");
#endif
      rownr = (*matentry).row_nr;

     /* Reduce item counts for the selected pivot column/row */
#ifdef UseMarkowitzStatistic
      removeLink(nextrow, rownr);
#endif
      bfp_ETAreduceCounts(lp, usedpos, rownum, colnum, rownr, colnr, FALSE);

      /* Store pivot information and update counters */
      col[numit] = colnr;
      row[numit] = rownr;
      numit++;
      k++;
      kk++;
    }
  }

 /* Check timeout and user abort again */
  if(lp->userabort(lp, -1))
    goto Cleanup;
  if(k >= lp->rows)
    goto Process;

 /* Reprocess the singleton elimination loop until supply exhausted */
#ifdef ReprocessSingletons
  if(kk > 0)
    goto Restart;
#endif

 /* Determine the number of remaining pivots and fill a minimum degree ordering column index array */
  k = lp->rows - k;
  mdo = bfp_createMDO(lp, usedpos, k, 
#ifdef UseLegacyOrdering
                      FALSE);
#else
                      TRUE);
#endif
  if(mdo == NULL)
    goto Cleanup;
  else if(mdo[0] == 0)
    goto Process;  
  kk = mdo[0];  

 /* Loop over all unprocessed basic user columns, finding an appropriate
    unused basis column position to pivot the user column into */
  for(i = 1; i <= kk; i++) {

  /* Get the entering variable */
    colnr = mdo[i];

  /* Solve for and eliminate the entering column / variable */
#ifdef UseMarkowitzStatistic
    rownr = bfp_ETApreparepivot(lp, -i, colnr, pcol, (MYBOOL*) nextrow);
#else
    rownr = bfp_ETApreparepivot(lp, -i, colnr, pcol, usedpos);
#endif
    if(rownr < 0) {
     /* This column is singular; Just let it leave the basis, making one of the
        slack variables basic in its place. (Source: Geosteiner changes!) */
      if(lp->spx_trace)
        lp->report(lp, DETAILED, "bfp_factorize: Skipped singular column %d\n", colnr);
      singularities++;
      continue;
    }
    else {
#ifdef UseMarkowitzStatistic
     /* Do a simple "local" Markowitz-based pivot selection;
        this generally reduces eta NZ count, but does not always give
        a speed improvement due to weaker numerics and added overhead
        (the numeric pivot selection tolerance limit for application
         of the Markowitz metric is 0.1, which is a typical value) */
      k = rownr;
      absval = fabs(pcol[rownr]);
      hold = 0.1*absval;
/*      for(j = 1; j <= lp->rows; j++) if(!usedpos[j]) { */
      j = 0;
      while((j = nextActiveLink(nextrow, j)) != 0) {

        /* Skip previously selected positions and focus row */
        if(j == rownr)
          continue;
        /* Pick a pivot row that is shorter than the previous best */
        jj = rownum[j];
        ii = rownum[k];
        if((jj > 0) && (jj <= ii)) {
          /* Make sure that we preserve numeric accuracy */
          testval = fabs(pcol[j]);
          if(((jj == ii) && (testval > absval)) ||
             ((jj < ii)  && (testval > hold))) {
            k = j;
            absval = testval;
          }
        }
      }
      rownr = k;
      bfp_ETAsimpleiteration(lp, rownr, colnr, pcol);
    }

   /* Reduce item counts for the selected pivot column/row */
    if(rownr > 0)
      removeLink(nextrow, rownr);
    bfp_ETAreduceCounts(lp, usedpos, rownum, NULL, rownr, colnr-lp->rows, FALSE);

#else
      bfp_ETAsimpleiteration(lp, rownr, colnr, pcol);
    }

   /* Update occupancy states for pivot column/row */
    bfp_ETAupdateCounts(lp, usedpos, rownr, colnr);
#endif

   /* Check timeout and user abort again */
    if(lp->userabort(lp, -1))
      goto Cleanup;
  }

 /* Perform pivoting of the singleton columns stored above */
Process:
  for(i = numit - 1; i >= 0; i--) {
    colnr = col[i];
    rownr = row[i];
    bfp_ETAsimpleiteration(lp, rownr, lp->rows+colnr, NULL);
  }

 /* Finally, wrap up the refactorization */
Cleanup:
  FREE(mdo);
  if(pcol != NULL) {
    FREE(pcol);
    FREE(col);
    FREE(row);
    FREE(rownum);
    FREE(colnum);
  }
#ifdef UseMarkowitzStatistic
  freeLink(&nextrow);
#endif

  lp->invB->num_singular += singularities;
  return(singularities);
}


LREAL BFP_CALLMODEL bfp_prepareupdate(lprec *lp, int row_nr, int col_nr, REAL *pcol)
/* Was condensecol() in versions of lp_solve before 4.0.1.8 - KE */
{
  int    i, j, k, colusr, elnr, min_size;
  LREAL  pivValue;
  INVrec *eta;
  MATrec *mat = lp->matA;

  eta = lp->invB;
  eta->pcol = pcol;

  elnr = eta->eta_col_end[eta->eta_colcount];
  pivValue = 0;

#ifdef Paranoia
  if(row_nr < 1 || col_nr < 1)
    lp->report(lp, CRITICAL, "bfp_prepareupdate: Invalid row/column combination specified");
#endif

  min_size = elnr + lp->rows + 2;
  if(min_size >= eta->eta_matalloc) /* maximum local growth of Eta */
    bfp_resize(lp, min_size);

  /* Fill the eta-colum from the A matrix */
  if(pcol == NULL) {
    k = 0;
    colusr = col_nr - lp->rows;
    i = mat->col_mat[mat->col_end[colusr - 1]].row_nr;

   /* Handle phase 1 objective function adjustments */
    pivValue = 0;
    if((i > 0) && lp->modifyOF1(lp, col_nr, &pivValue, 1.0)) {
      eta->eta_row_nr[elnr] = 0;
      eta->eta_value[elnr] = pivValue;
      elnr++;
    }

    for(j = mat->col_end[colusr - 1]; j < mat->col_end[colusr]; j++) {

      /* The pivot row is stored at the end of the column; store its index */
      i = mat->col_mat[j].row_nr;
      if(i == row_nr) {
        k = j;
        continue;
      }

      /* Append other non-zero column values */
      pivValue = mat->col_mat[j].value;

      /* Handle non-zero phase 1 objective function values */
      if((i == 0) && !lp->modifyOF1(lp, col_nr, &pivValue, 1.0))
        continue;

      /* Do some additional pivot accuracy management */
#if 0
      if(my_abs(my_abs(pivValue)-1) < lp->epsmachine) pivValue = my_sign(pivValue, 1);
#endif
      eta->eta_row_nr[elnr] = i;
      eta->eta_value[elnr] = pivValue;
      elnr++;
    }
    eta->eta_row_nr[elnr] = mat->col_mat[k].row_nr;
    pivValue = mat->col_mat[k].value;
    eta->eta_value[elnr] = pivValue;
    elnr++;
  }

  /* Fill the eta-colum from a dense, ftran-preprocessed column
     where the data has been retrieved with obtain_column().
     Note that phase 1 objective function adjustments are done in
     obtain_column/expand_column, called in fsolve() */
  else {

    for(i = 0; i <= lp->rows; i++) {

      pivValue = pcol[i];

      if((i != row_nr) && (pivValue != 0)) {
        eta->eta_row_nr[elnr] = i;
        eta->eta_value[elnr] = pivValue;
        elnr++;
      }
    }
    eta->eta_row_nr[elnr] = row_nr;
    pivValue = pcol[row_nr];
    eta->eta_value[elnr] = pivValue;
    elnr++;
  }

  eta->eta_col_nr[eta->eta_colcount] = col_nr;
  eta->eta_col_end[eta->eta_colcount + 1] = elnr;
  eta->last_colcount = my_max(eta->last_colcount, eta->eta_colcount+1);

  /* Set completion status; but hold if we are reinverting */
  if(eta->is_dirty != AUTOMATIC)
    lp->invB->is_dirty = TRUE;

  return(pivValue);
}


REAL BFP_CALLMODEL bfp_pivotRHS(lprec *lp, LREAL theta, REAL *pcol)
/* Was rhsmincol(), ie. "rhs minus column" in versions of lp_solve before 4.0.1.8 - KE */
{
  int    i;
  LREAL  f = 0;

 /* Round net RHS values (should not be smaller than the factor used in recompute_solution) */
  REAL   roundzero = lp->epsmachine;

  if(pcol == NULL) {
    int    j, k;
    INVrec *eta;

    eta = lp->invB;
    j = eta->eta_col_end[eta->eta_colcount];
    k = eta->eta_col_end[eta->eta_colcount + 1];
    {
#if 1
    int  *nv;
    REAL *vv;
    for(i = j, nv = eta->eta_row_nr+j, vv = eta->eta_value+j;
        i < k; i++, nv++, vv++) {
      f = lp->rhs[*nv] - theta * (*vv);
      my_roundzero(f, roundzero);
      lp->rhs[*nv] = f;
    }
#else
    int varbas;
    for(i = j; i < k; i++) {
      varbas = eta->eta_row_nr[i];
      f = lp->rhs[varbas] - theta*eta->eta_value[i];
      my_roundzero(f, roundzero);
      lp->rhs[varbas] = f;
    }
#endif
    }
    f = eta->eta_value[k - 1];
  }
  else {
    LREAL *rhs;

    for(i = 0, rhs = lp->rhs; i<= lp->rows; i++, rhs++, pcol++) {
      if(*pcol == 0)
        continue;
      *rhs -= theta * (*pcol);
      my_roundzero((*rhs), roundzero);
    }
    f = 0;
  }

  return( f );

}


MYBOOL BFP_CALLMODEL bfp_finishupdate(lprec *lp, MYBOOL changesign)
/* Was addetacol() in versions of lp_solve before 4.0.1.8 - KE */
{
  int    i, j, k;
  REAL   theta, *value;
  INVrec *eta;

  eta = lp->invB;

  /* Check if a data column has been loaded by bfp_putcolumn */
  if(!eta->is_dirty)
    return( FALSE );

 /* Do fast eta transformation (formally "u") */
  j = eta->eta_col_end[eta->eta_colcount];
  eta->eta_colcount++;
  k = eta->eta_col_end[eta->eta_colcount] - 1;

 /* Handle following cases: 1) changesign == FALSE, theta == -1    -> Drop straight through
                            2)            == FALSE        != -1
                            3)            == TRUE         ==  1    -> Sign change only
                            4)            == TRUE         !=  1 */
#if 1
 /* Amended old style */
  if(changesign)
    for(i = j, value = eta->eta_value+j; i <= k; i++, value++)
      *value = -(*value);

  value  = eta->eta_value+k;
  theta  = 1.0 / (*value);
  *value = theta;
  if(fabs(theta+1) > EPS_ETAMACHINE) {
    theta = -theta;
    while(k > j) {
      k--;
      value--;
      *value *= theta;
    }
  }

#else
 /* New streamlined style (change sign loop eliminated) -- not entirely debugged */
  value = eta->eta_value+k;
  theta = my_chsign(changesign, *value);
  if(fabs(theta+1) > EPS_ETAMACHINE) {
    theta  = 1.0 / theta;
    *value = theta;
    theta  = my_chsign(changesign, -theta);
    while(k > j) {
      k--;
      value--;
      *value *= theta;
    }
  }
  else if(fabs(theta-1) <= EPS_ETAMACHINE) {
    if(changesign) {
      k++;
      value++;
    }
    while(k > j) {
      k--;
      value--;
      *value = -(*value);
    }
  }

#endif

  eta->num_pivots++;

  /* Reset indicators; treat reinversion specially */
  if(eta->is_dirty != AUTOMATIC) {
    eta->is_dirty = FALSE;
  }

  lp->justInverted = FALSE;
  return( TRUE );

} /* bfp_finishupdate */


void BFP_CALLMODEL bfp_ftran_normal(lprec *lp, REAL *pcol, int *nzidx)
/* Note that ftran does not "expand" B indexes to the actual basis
   column, and that both the input and output range is [0..rows] */
{
  int    i, j, k, r, *rowp;
  LREAL  theta, *vcol;
  REAL   *valuep;
  INVrec *eta;

  eta = lp->invB;

  /* Initialize case where long doubles may be used */
  if(sizeof(LREAL) == sizeof(REAL))
    vcol = pcol;
  else {
    allocLREAL(lp, &vcol, lp->rows + 1, FALSE);
    for(i = 0; i <= lp->rows; i++)
      vcol[i] = pcol[i];
  }

  for(i = 1; i <= eta->eta_colcount; i++) {
    k = eta->eta_col_end[i] - 1;
    r = eta->eta_row_nr[k];
    theta = vcol[r];
    if(theta != 0) {
      j = eta->eta_col_end[i - 1];

      /* CPU intensive loop, let's do pointer arithmetic */
      for(rowp = eta->eta_row_nr + j, valuep = eta->eta_value + j;
          j < k; j++, rowp++, valuep++) {
        vcol[(*rowp)] += theta * (*valuep);
      }
      vcol[r] *= eta->eta_value[k];
    }
  }

  /* Finalize case where long doubles may be used */
  if(sizeof(LREAL) != sizeof(REAL)) {
    for(i = 0; i <= lp->rows; i++)
      pcol[i] = vcol[i];
    FREE(vcol);
  }

} /* ftran */

void BFP_CALLMODEL bfp_ftran_prepare(lprec *lp, REAL *pcol, int *nzidx)
/* Does nothing particular in the etaPFI version of the inverse;
   in other versions it is used to additionally store necessary data
   to prepare for pivoting / update of the inverse */
{
#ifdef EtaFtranRoundRelative
  int  i;
  REAL pmax, *x;
#endif

  bfp_ftran_normal(lp, pcol, nzidx);

  /* Help numeric accuracy in case we have not scaled to equilibration */
#ifdef EtaFtranRoundRelative
  pmax = 0;
  for(i = 0, x = pcol; i <= lp->rows; i++, x++)
    if(fabs(*x) > pmax)
      pmax = fabs(*x);
  if(pmax > 0)
  for(i = 0, x = pcol; i <= lp->rows; i++, x++)
    if(fabs(*x/pmax) < EPS_ETAPIVOT)
      *x = 0;
#else
  roundVector(pcol, lp->rows, EPS_ETAPIVOT);
#endif
}


void BFP_CALLMODEL bfp_btran_normal(lprec *lp, REAL *prow, int *nzidx)
/* Note that btran does not "expand" B indexes to the actual basis
   column, and that both the input and output range is [0..rows] */
{
  int      i, jb, je, k, *rowp;
  LREAL    fmax;
  REGISTER LREAL f;
  REAL     *valuep;
  INVrec   *eta = lp->invB;

  /* Prepare for maximum value (initialize to prevent division by zero */
  k = 0;
  fmax = 0;
  for(i = eta->eta_colcount; i >= 1; i--) {
    f = 0;
    jb = eta->eta_col_end[i-1];
    je = eta->eta_col_end[i] - 1;
    for(rowp = eta->eta_row_nr + jb, valuep = eta->eta_value + jb;
        jb <= je;    jb++, rowp++, valuep++) {
      f += prow[(*rowp)] * (*valuep);
    }
    fmax = my_max(fmax, fabs(f));
#ifndef EtaBtranRoundRelative
    my_roundzero(f, EPS_ETAPIVOT);
#endif
    jb = eta->eta_row_nr[je];
    prow[jb] = f;

  }

  /* Help numeric accuracy in case we have not scaled to equilibration */
#ifdef EtaBtranRoundRelative
  if(fmax > 0) {
    for(i = 0, valuep = prow; i <= lp->rows; i++, valuep++)
      if(fabs(*valuep/fmax) < EPS_ETAPIVOT)
        *valuep = 0;
  }
#endif

} /* btran */


void BFP_CALLMODEL bfp_btran_double(lprec *lp, REAL *prow, int *pnzidx, REAL *drow, int *dnzidx)
{
  int      i, j, k, *rowp;
  LREAL    dmax, fmax;
  REGISTER LREAL  d, f;
  REAL     *valuep;
  INVrec   *eta;

  eta = lp->invB;

  fmax = 0;
  dmax = 0;
  for(i = eta->eta_colcount; i >= 1; i--) {
    d = 0;
    f = 0;
    k = eta->eta_col_end[i] - 1;
    j = eta->eta_col_end[i - 1];

  /* This is one of the loops where the program consumes a lot of CPU time;
     let's help the compiler by doing some pointer arithmetic instead of array indexing */
    for(rowp = eta->eta_row_nr + j, valuep = eta->eta_value + j;
        j <= k; j++, rowp++, valuep++) {
      f += prow[(*rowp)] * (*valuep);
      d += drow[(*rowp)] * (*valuep);
    }
    fmax = my_max(fmax, fabs(f));
    dmax = my_max(dmax, fabs(d));

    j = eta->eta_row_nr[k];

#ifndef EtaBtranRoundRelative
    my_roundzero(f, EPS_ETAPIVOT);
    my_roundzero(d, EPS_ETAPIVOT);
#endif
    prow[j] = f;
    drow[j] = d;
  }

  /* Help numeric accuracy in case we have not scaled to equilibration */
#ifdef EtaBtranRoundRelative
  if((fmax > 0) && (dmax > 0)) {
    REAL *valued;
    for(i = 0, valuep = prow, valued = drow; i <= lp->rows; i++, valuep++, valued++) {
      if(fabs(*valuep/fmax) < EPS_ETAPIVOT)
        *valuep = 0;
      if(fabs(*valued/dmax) < EPS_ETAPIVOT)
        *valued = 0;
    }
  }
#endif

}
