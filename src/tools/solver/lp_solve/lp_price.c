
#include <string.h>
#include "commonlib.h"
#include "lp_lib.h"
#include "lp_price.h"
#include "lp_pricePSE.h"

#ifdef FORTIFY
# include "lp_fortify.h"
#endif

/*
    Simplex pricing utility module - w/interface for lp_solve v5.0+
   ----------------------------------------------------------------------------------
    Author:        Kjell Eikland
    Contact:       kjell.eikland@broadpark.no
    License terms: LGPL.

    Requires:      lp_lib.h, commonlib.h

    Release notes:
    v1.0.0  1 July 2004         Routines extracted from lp_lib.
    v1.0.1 10 July 2004         Added comparison operators for determination of
                                entering and leaving variables.
                                Added routines for multiple and partial pricing
                                and made corresponding changes to colprim and
                                rowdual.
   ----------------------------------------------------------------------------------
*/

/* Comparison operators for entering and leaving variables for both the primal and
   dual simplexes.  The functions compare a candidate variable with an incumbent
   and can return the values:

       1 for the candidate being "better",
      -1 for the candidate being "worse", and
       0 for the candidate being "equal" to the incumbent.
*/
int CMP_CALLMODEL compareImprovementVar(const pricerec *current, const pricerec *candidate)
{
  static REAL  testvalue;
  static int   result;
  static lprec *lp;

  result = 0;
  lp = current->lp;

  if(lp->_piv_rule_ != PRICER_FIRSTINDEX) {

    /* Compute the ranking test metric. */
    testvalue = candidate->pivot - current->pivot;
    if(current->isdual)
      testvalue = -testvalue;

    /* Find the largest value */
    if(testvalue > lp->epsvalue)
      result = 1;
    else if (testvalue < -lp->epsmachine)
      result = -1;
  }

  /* Handle ties via index ordinal */
  if(result == 0) {
    int varno = current->varno, candidatevarno = candidate->varno;

    if(current->isdual) {
      candidatevarno = lp->var_basic[candidatevarno];
      varno          = lp->var_basic[varno];
    }
    if(candidatevarno < varno)
      result = 1;
    else if(candidatevarno > varno)
      result = -1;
    if(lp->_piv_left_)
      result = -result;
  }

  return( result );

}

int CMP_CALLMODEL compareSubstitutionVar(pricerec *current, pricerec *candidate)
{
  static REAL  testvalue, margin;
  static int   result;
  static lprec *lp;

  result = 0;
  lp = current->lp;

  /* Compute the ranking test metric. */
#if 0
  testvalue = candidate->theta - current->theta;
#else
  testvalue = my_reldiff(candidate->theta, current->theta);
#endif

  /* Find if the new Theta is smaller or near equal (i.e. testvalue <= eps)
     compared to the previous best; ties will be broken by pivot size or index */
  margin = lp->epsvalue;
  if(testvalue < -margin)
    result = 1;
  else if(testvalue > margin)
    result = -1;

  /* Resolve a tie */
  if(result == 0) {
    REAL pivot = fabs(current->pivot), candidatepivot = fabs(candidate->pivot);

    if(lp->_piv_rule_ == PRICER_FIRSTINDEX) {
#if 1
      /* Special secondary selection by pivot size (stability protection) */
      margin = lp->epspivot;
      if((candidatepivot >= margin) && (pivot < margin))
        result = 1;
#endif
    }

    else {

      /* General secondary selection based on pivot size */
      if(candidatepivot > pivot+margin)
        result = 1;
      else if(candidatepivot < pivot-margin)
        result = -1;
    }

    /* Tertiary selection by index value */
    if(result == 0) {
      int varno = current->varno, candidatevarno = candidate->varno;

      if(!current->isdual) {
        candidatevarno = lp->var_basic[candidatevarno];
        varno          = lp->var_basic[varno];
      }
      if(candidatevarno < varno)
        result = 1;
      else if(candidatevarno > varno)
        result = -1;
      if(lp->_piv_left_)
        result = -result;
    }
  }

  return( result );
}

/* Validity operators for entering and leaving columns for both the primal and dual
   simplex.  All candidates must satisfy these tests to qualify to be allowed to be
   a subject for the comparison functions/operators. */
STATIC MYBOOL validImprovementVar(pricerec *candidate)
{
  REAL candidatepivot;

  if(candidate->isdual)
    candidatepivot = -candidate->pivot;
  else
    candidatepivot = candidate->pivot;

#ifdef Paranoia
  return( (MYBOOL) ((candidate->varno > 0) && (candidatepivot > candidate->lp->epsvalue)) );
#else
  return( (MYBOOL) (candidatepivot > candidate->lp->epsvalue) );
#endif
}

STATIC MYBOOL validSubstitutionVar(pricerec *candidate)
{
  static lprec *lp;

  lp = candidate->lp;

#ifdef Paranoia
  if(candidate->varno <= 0)
    return( FALSE );
  else
#endif
  if(candidate->pivot >= lp->infinite)
    return( (MYBOOL) (candidate->theta < lp->infinite) );
  else
    return( (MYBOOL) ((candidate->theta < lp->infinite) &&
                      (fabs(candidate->pivot) >= lp->epspivot)) );
}

/* Function to add a valid improvement candidate into the
   sorted multiple price list at the proper location */
STATIC int addImprovementVar(pricerec *candidate)
{
  int   i, insertpos, delta;
  lprec *lp = candidate->lp;

  /* Find the insertion point (if any) */
  if(lp->multiused > 0) {
    delta = 1;
    i = sizeof(*candidate);
    insertpos = findIndexEx(candidate, lp->multivar-delta, lp->multiused, delta, i,
                            (findCompare_func *)compareImprovementVar);
    if(insertpos > 0)
      return( -1 );
    insertpos = -insertpos - delta;
  }
  else
    insertpos = 0;

  /* Shift the existing data down to make space for the new record */
  if(insertpos < lp->multiused) {
    delta = MIN(lp->multisize, lp->multiused) - insertpos;
    if(delta > 0)
      MEMMOVE(lp->multivar + insertpos + 1, lp->multivar + insertpos, delta);
  }

  /* Insert the new record */
  if(insertpos >= 0) {
    MEMCOPY(lp->multivar + insertpos, candidate, 1);
    if(lp->multiused < lp->multisize)
      lp->multiused++;
  }

  return( insertpos );
}

STATIC MYBOOL findImprovementVar(pricerec *current, pricerec *candidate, MYBOOL collectMP)
/* PRIMAL: Find a variable to enter the basis
   DUAL:   Find a variable to leave the basis

   Allowed variable set: Any pivot PRIMAL:larger or DUAL:smaller than threshold value of 0 */
{
  MYBOOL Accept, Action = FALSE;

 /* Check for validity and comparison result with previous best */
  Accept = validImprovementVar(candidate);
  if(Accept) {
    if(collectMP)
      addImprovementVar(candidate);
    if(current->varno > 0)
      Accept = (MYBOOL) (compareImprovementVar(current, candidate) > 0);
  }

 /* Apply candidate if accepted */
  if(Accept) {
    (*current) = *candidate;

#ifdef ForceEarlyBlandRule
    /* Force immediate acceptance for Bland's rule using the primal simplex */
    if(!candidate->isdual)
      Action = (MYBOOL) (candidate->lp->_piv_rule_ == PRICER_FIRSTINDEX);
#endif
  }
  return(Action);
}

STATIC MYBOOL findSubstitutionVar(pricerec *current, pricerec *candidate)
/* PRIMAL: Find a variable to leave the basis
   DUAL:   Find a variable to enter the basis

   Allowed variable set: Equal-valued smallest thetas! */
{
  MYBOOL Accept, Action = FALSE;

 /* Check for validity and comparison result with previous best */
  Accept = validSubstitutionVar(candidate);
  if(Accept && (current->varno != 0))
    Accept = (MYBOOL) (compareSubstitutionVar(current, candidate) > 0);

 /* Apply candidate if accepted */
  if(Accept) {
    (*current) = *candidate;

    /* Force immediate acceptance for Bland's rule using the dual simplex */
#ifdef ForceEarlyBlandRule
    if(candidate->isdual)
      Action = (MYBOOL) (candidate->lp->_piv_rule_ == PRICER_FIRSTINDEX);
#endif
  }
  return(Action);
}

/* Partial pricing management routines */
STATIC partialrec *partial_createBlocks(lprec *lp, MYBOOL isrow)
{
  partialrec *blockdata;

  blockdata = (partialrec *) calloc(1, sizeof(*blockdata));
  blockdata->lp = lp;
  blockdata->blockcount = 1;
  blockdata->blocknow = 1;
  blockdata->isrow = isrow;

  return(blockdata);
}
STATIC int partial_countBlocks(lprec *lp, MYBOOL isrow)
{
  partialrec *blockdata = IF(isrow, lp->rowblocks, lp->colblocks);

  if(blockdata == NULL)
    return( 1 );
  else
    return( blockdata->blockcount );
}
STATIC int partial_activeBlocks(lprec *lp, MYBOOL isrow)
{
  partialrec *blockdata = IF(isrow, lp->rowblocks, lp->colblocks);

  if(blockdata == NULL)
    return( 1 );
  else
    return( blockdata->blocknow );
}
STATIC void partial_freeBlocks(partialrec **blockdata)
{
  if((blockdata == NULL) || (*blockdata == NULL))
    return;
  FREE((*blockdata)->blockend);
  FREE((*blockdata)->blockpos);
  FREE(*blockdata);
}


/* Function to provide for left-right or right-left scanning of entering/leaving
   variables; note that *end must have been initialized by the calling routine! */
STATIC void makePriceLoop(lprec *lp, int *start, int *end, int *delta)
{
  int offset = is_piv_mode(lp, PRICE_LOOPLEFT);

  if((offset) ||
#if 0
     TRUE ||
#endif
     (((lp->total_iter+offset) % 2 == 0) && is_piv_mode(lp, PRICE_LOOPALTERNATE))) {
    *delta = -1; /* Step backwards - "left" */
    swapINT(start, end);
    lp->_piv_left_ = TRUE;
  }
  else {
    *delta = 1;  /* Step forwards - "right" */
    lp->_piv_left_ = FALSE;
  }
}

/* Find the primal simplex entering non-basic column variable */
STATIC int colprim(lprec *lp, MYBOOL minit, REAL *drow, int *nzdrow)
{
  int      i, ix, iy, iz;
  REAL     f;
  pricerec current, candidate;
  MYBOOL   collectMP = FALSE;
  int      *multivars = NULL;

  /* Compute reduced costs c - c*Inv(B), if necessary
     (i.e. the previous iteration was not a minor iteration) */
  if(!minit) {
#ifdef UsePrimalReducedCostUpdate
    /* Recompute from scratch only at the beginning, otherwise update */
    if((lp->current_iter > 0) && !lp->justInverted)
#endif
    compute_reducedcosts(lp, FALSE, 0, NULL, NULL,
                                       drow, nzdrow);
  }

  /* Identify pivot column according to pricing strategy; set
     entering variable initial threshold reduced cost value to "0" */
  current.pivot    = lp->epsprimal;    /* Minimum acceptable improvement */
  current.varno    = 0;
  current.lp       = lp;
  current.isdual   = FALSE;
  candidate.lp     = lp;
  candidate.isdual = FALSE;

  /* Update local value of pivot setting and determine active multiple pricing mode */
  lp->_piv_rule_ = get_piv_rule(lp);
  if(lp->multisize > 0) {
    collectMP = lp->justInverted;
    if(!collectMP) {
      multivars = multi_createVarList(lp);
      lp->multiused = 0;
      ix = 1;
      iy = lp->multiused;
      goto doLoop;
    }
  }
#ifdef UseSparseReducedCost
  ix = 1;
  iy = nzdrow[0];

  /* Loop over active partial row set; we presume that reduced costs
     have only been updated for columns in the active partial range. */
doLoop:
  makePriceLoop(lp, &ix, &iy, &iz);
  for(; ix*iz <= iy*iz; ix += iz) {
    i = nzdrow[ix];
#if 0
    if(i > lp->sum-abs(lp->Extrap))
      continue;
#endif

#else
  ix = partial_blockStart(lp, FALSE);
  iy = partial_blockEnd(lp, FALSE);
  if(iy == lp->sum)
    iy -= abs(lp->Extrap);

  /* Loop over active partial row set */
doLoop:
  makePriceLoop(lp, &ix, &iy, &iz);
  for(; ix*iz <= iy*iz; ix += iz) {

    /* Map loop variable to target */
    if(multivars == NULL)
      i = ix;
    else
      i = multivars[ix];

    /* Only scan non-basic variables */
    if(lp->is_basic[i])
      continue;

    /* Never pivot in fixed variables/equality slacks */
    if(lp->upbo[i] == 0)
      continue;

#endif

    /* Check if the pivot candidate is on the block-list */
    if(lp->rejectpivot[0] > 0) {
      int k;
      for(k = 1; (k <= lp->rejectpivot[0]) && (i != lp->rejectpivot[k]); k++);
      if(k <= lp->rejectpivot[0])
        continue;
    }

   /* Retrieve the applicable reduced cost */
    f = my_chsign(lp->is_lower[i], drow[i]);
    if(f <= lp->epsprimal)  /* Threshold should not be smaller than 0 */
      continue;

   /* Find entering variable according to strategy (largest positive f) */
    candidate.pivot = normalizeEdge(lp, i, f, FALSE);
    candidate.varno = i;
    if(findImprovementVar(&current, &candidate, collectMP))
      break;
  }

  /* Check if we should loop again after a multiple pricing update */
  if(multivars != NULL) {
    FREE(multivars);
    ix = partial_blockStart(lp, FALSE);
    iy = partial_blockEnd(lp, FALSE);
    if(iy == lp->sum)
      iy -= abs(lp->Extrap);
    if(lp->multiused+iy-ix < lp->multisize)
      goto doLoop;
  }

  if(lp->spx_trace) {
    if(current.varno > 0)
      report(lp, DETAILED, "col_prim: Column %d reduced cost = " MPSVALUEMASK "\n",
                          current.varno, current.pivot);
    else
      report(lp, DETAILED, "col_prim: No positive reduced costs found, optimality!\n");
  }
  if(current.varno == 0) {
    lp->doIterate = FALSE;
    lp->doInvert = FALSE;
    lp->spx_status = OPTIMAL;
  }

  return(current.varno);
} /* colprim */

/* Find the primal simplex leaving basic column variable */
STATIC int rowprim(lprec *lp, int colnr, LREAL *theta, REAL *pcol)
{
  int      i, iy, iz, pass, k;
  LREAL    f, savef;
  REAL     Hscale, Hlimit;
  pricerec current, candidate;

  /* Update local value of pivot setting */
  lp->_piv_rule_ = get_piv_rule(lp);

#ifdef UseHarrisTwoPass
  pass = 2;
#else
  pass = 1;
#endif
  current.theta    = lp->infinite;
  current.pivot    = 0;
  current.varno    = 0;
  current.isdual   = FALSE;
  current.lp       = lp;
  candidate.isdual = FALSE;
  candidate.lp     = lp;
  savef  = 0;
  for(; pass > 0; pass--) {
    if(pass == 2) {
      Hlimit = lp->infinite;
      Hscale = 1000;
    }
    else {
      Hlimit = fabs(current.theta);
      Hscale = 0;
    }
    current.theta = lp->infinite;
    current.pivot = 0;
    current.varno = 0;
    savef = 0;

    i  = 1;
    iy = lp->rows;
    makePriceLoop(lp, &i, &iy, &iz);
    for(; i*iz <= iy*iz; i += iz) {
      f = pcol[i];
      if(fabs(f) < lp->epspivot) {
        if(lp->spx_trace)
          report(lp, FULL, "rowprim: Pivot " MPSVALUEMASK " rejected, too small (limit " MPSVALUEMASK ")\n",
                           f, lp->epspivot);
      }
      else { /* Pivot possibly alright */
        candidate.theta = f;
        candidate.pivot = f;
        candidate.varno = i;
        k = compute_theta(lp, i, &candidate.theta, Hscale, TRUE);
        if(fabs(candidate.theta) >= lp->infinite) {
          savef = f;
          candidate.theta = 2*lp->infinite;
          continue;
        }

#if 0
       /* Give priority to equality slack and fixed variable removal */
        if((lp->upbo[k] < lp->epsmachine) &&
           (candidate.pivot > 0.6 * MAX(current.pivot, lp->epspivot))) {
          current = candidate;
          break;
        }
#endif

       /* Find the candidate leaving variable according to strategy (smallest theta) */
        if(fabs(candidate.theta) > Hlimit)
          continue;
        if(findSubstitutionVar(&current, &candidate))
          break;
      }
    }
  }

  /* Handle case of no available leaving variable */
  if(current.varno == 0) {
    if(lp->upbo[colnr] >= lp->infinite) {
      lp->doIterate = FALSE;
      lp->doInvert = FALSE;
      lp->spx_status = UNBOUNDED;
    }
    else {
      i = 1;
      while((pcol[i] >= 0) && (i <= lp->rows))
        i++;
      if(i > lp->rows) { /* Empty column with upper bound! */
        lp->is_lower[colnr] = FALSE;
        lp->rhs[0] += lp->upbo[colnr]*pcol[0];
        lp->doIterate = FALSE;
        lp->doInvert = FALSE;
      }
      else /* if(pcol[i]<0) */
      {
        current.varno = i;
      }
    }
  }
  else if(current.theta >= lp->infinite) {
    report(lp, IMPORTANT, "rowprim: Numeric instability pcol[%d] = %g, rhs[%d] = %g, upbo = %g\n",
                          current.varno, savef, current.varno, lp->rhs[current.varno],
                          lp->upbo[lp->var_basic[current.varno]]);
  }

 /* Flag that we can do a major iteration */
  if(current.varno > 0)
    lp->doIterate = TRUE;

  if(lp->spx_trace)
    report(lp, DETAILED, "row_prim: %d, pivot size = " MPSVALUEMASK "\n",
                         current.varno, pcol[current.varno]);

  *theta = current.theta;
  return(current.varno);
} /* rowprim */

/* Find the dual simplex leaving basic variable */
STATIC int rowdual(lprec *lp, MYBOOL eliminate)
{
  int      k, i, iy, iz, ii;
  REAL     rh, up, epsvalue;
  pricerec current, candidate;
  MYBOOL   collectMP = FALSE;
  int      *multivars = NULL;

  /* Initialize */
  epsvalue = lp->epsprimal;
  current.pivot    = +epsvalue;  /* Initialize leaving variable threshold; "less than 0" */
  current.varno    = 0;
  current.isdual   = TRUE;
  current.lp       = lp;
  candidate.isdual = TRUE;
  candidate.lp     = lp;

  /* Update local value of pivot setting and determine active multiple pricing mode */
  lp->_piv_rule_ = get_piv_rule(lp);
  if(lp->multisize > 0) {
    collectMP = lp->justInverted;
    if(!collectMP) {
      multivars = multi_createVarList(lp);
      lp->multiused = 0;
      k = 1;
      iy = lp->multiused;
      goto doLoop;
    }
  }
  k  = partial_blockStart(lp, TRUE);
  iy = partial_blockEnd(lp, TRUE);

  /* Loop over active partial row set */
doLoop:
  makePriceLoop(lp, &k, &iy, &iz);
  for(; k*iz <= iy*iz; k += iz) {

    /* Map loop variable to target */
    if(multivars == NULL)
      i = k;
    else
      i = multivars[k];

    /* Set local variables */
    ii = lp->var_basic[i];
    up = lp->upbo[ii];
    rh = lp->rhs[i];

   /* Analyze relevant constraints ...
      KE version skips uninteresting alternatives and gives a noticeable speedup */
    if((rh < 0) || (rh > up)) {
      if(rh > up)
        rh = up-rh;

     /* Give a slight preference to fixed variables (mainly equality slacks) */
      if(up < epsvalue) {
        rh *= 1.0+lp->epspivot;
        /* Give an extra boost to equality slack elimination, if specified */
        if(eliminate)
          rh *= 10.0;
      }

     /* Select leaving variable according to strategy (the most negative g/largest violation) */
      candidate.pivot = normalizeEdge(lp, i, rh, TRUE);
      candidate.varno = i;
      if(findImprovementVar(&current, &candidate, collectMP))
        break;
    }
  }

  /* Check if we should loop again after a multiple pricing update */
  if(multivars != NULL) {
    FREE(multivars);
    k  = partial_blockStart(lp, TRUE);
    iy = partial_blockEnd(lp, TRUE);
    if(lp->multiused+iy-k < lp->multisize)
      goto doLoop;
  }

  /* Produce statistics */
  if(lp->spx_trace) {
    if(current.varno > 0) {
      report(lp, DETAILED, "row_dual: rhs[%d] = " MPSVALUEMASK "\n",
                           current.varno, lp->rhs[current.varno]);
      if(lp->upbo[lp->var_basic[current.varno]] < lp->infinite)
        report(lp, DETAILED, "\t\tupper bound of is_basic variable:    %18g\n",
                             lp->upbo[lp->var_basic[current.varno]]);
    }
    else
      report(lp, FULL, "rowdual: No infeasibilities found\n");
  }

  return(current.varno);
} /* rowdual */

/* Find the dual simplex entering non-basic variable */
STATIC int coldual(lprec *lp, int row_nr, MYBOOL minit, REAL *prow, int *nzprow,
                                                        REAL *drow, int *nzdrow)
{
  int      i, iy, iz, k;
#ifdef UseSparseReducedCost
  int      ix;
#endif
  LREAL    w, g, quot;
  REAL     epsvalue = lp->epsprimal;
  pricerec current, candidate;

  /* Initialize */
  current.theta    = lp->infinite;
  current.pivot    = 0;
  current.varno    = 0;
  current.isdual   = TRUE;
  current.lp       = lp;
  candidate.isdual = TRUE;
  candidate.lp     = lp;

  /* Determine the current loading state of the outgoing variable; note that the
     basic variables are always lower-bounded, by lp_solve convention */
  w = lp->rhs[row_nr];
  g = -lp->upbo[lp->var_basic[row_nr]];
  if(g > -lp->infinite)
    g += w;
  my_roundzero(w, epsvalue);
  my_roundzero(g, epsvalue);
  if(g >= 0)        /* The leaving variable is at or above its upper bound */
    g = -1;
  else if(w <= 0)   /* The leaving variable is at or below its lower bound */
    g = 1;
  else {            /* Check for accumulation of numerical errors */
    g = 0;
    report(lp, DETAILED, "coldual: Leaving variable %d does not violate bounds (minit=%s)!\n",
                       row_nr, my_boolstr(minit));
#ifndef FixInaccurateDualMinit
    report(lp, DETAILED, "         Try to enable FixInaccurateDualMinit compiler directive.\n");
#endif
    return( 0 );
  }

  /* Update local value of pivot setting */
  lp->_piv_rule_ = get_piv_rule(lp);

  /* Compute reduced costs */
  if(!minit) {
#ifdef UseDualReducedCostUpdate
    /* Recompute from scratch only at the beginning, otherwise update */
    if((lp->current_iter > 0) && !lp->justInverted)
      compute_reducedcosts(lp, TRUE, row_nr, prow, nzprow,
                                             NULL, NULL);
    else
#endif
    compute_reducedcosts(lp, TRUE, row_nr, prow, nzprow,
                                           drow, nzdrow);
  }

  /* Loop over all entering column candidates */
#ifdef UseSparseReducedCost
  ix = 1;
  iy = nzprow[0];
  makePriceLoop(lp, &ix, &iy, &iz);
  for(; ix*iz <= iy*iz; ix += iz) {
    i = nzprow[ix];

#else
  i  = 1;
  iy = lp->sum;
  makePriceLoop(lp, &i, &iy, &iz);
  for(; i*iz <= iy*iz; i += iz) {

    /* We are only looking for non-basic, non-equality slack / non-fixed variables to enter */
    if(lp->is_basic[i] || (fabs(lp->upbo[i]) < epsvalue))
      continue;

#endif

    /* Check if the pivot candidate is on the block-list */
    for(k = 1; (k <= lp->rejectpivot[0]) && (i != lp->rejectpivot[k]); k++);
    if(k <= lp->rejectpivot[0])
      continue;

    /* We have a candidate; check if it is good enough and better than the last;
       note that prow = w and drow = cbar in Chvatal's "nomenclatura" */
    w = prow[i];
    if(lp->is_lower[i])
      w *= g;
    else
      w *= -g;

    if(w > -lp->epspivot) {
      if(lp->spx_trace)
        report(lp, FULL, "coldual: Pivot %g rejected as too small (limit=%g)\n",
                         (double)w, (double)lp->epspivot);
    }
    else {
      quot = drow[i];
      if(lp->is_lower[i])
        quot /= -w;
      else
        quot /= w;

     /* Apply the selected pivot strategy (smallest theta) */
#if 1
      quot = fabs(quot);  /*  *** Should validate and test if this is necessary - KE */
#else
      if(quot < 0)
        continue;
#endif
      candidate.theta = quot;
      candidate.pivot = w;
      candidate.varno = i;
      if(findSubstitutionVar(&current, &candidate))
        break;
    }
  }

  i = current.varno;
  if(lp->spx_trace)
    report(lp, NORMAL, "col_dual: Entering column %d, reduced cost %g, pivot element value %g\n",
                       i, drow[i], prow[i]);

  return( i );
} /* coldual */


STATIC REAL normalizeEdge(lprec *lp, int item, REAL edge, MYBOOL isdual)
{

  edge /= getPricer(lp, item, isdual);
#ifdef EnableRandomizedPricing
  if(is_piv_strategy(lp, PRICE_RANDOMIZE))
    edge *= (1.0-PRICER_RANDFACT) + PRICER_RANDFACT*(rand() % RANDSCALE) / (REAL) RANDSCALE;
#endif
  return( edge );

}

/* Support routines for block detection and partial pricing */
STATIC int partial_findBlocks(lprec *lp, MYBOOL autodefine, MYBOOL isrow)
{
  int    i, jj, n, nb, ne, items;
  REAL   hold, biggest, *sum = NULL;
  MATrec *mat = lp->matA;
  partialrec *blockdata;

  if(!mat_validate(mat))
    return( 1 );

  blockdata = IF(isrow, lp->rowblocks, lp->colblocks);
  items     = IF(isrow, lp->rows, lp->columns);
  allocREAL(lp, &sum, items+1, FALSE);

  /* Loop over items and compute the average column index for each */
  sum[0] = 0;
  for(i = 1; i <= items; i++) {
    n = 0;
    if(isrow) {
      nb = mat->row_end[i-1];
      ne = mat->row_end[i];
    }
    else {
      nb = mat->col_end[i-1];
      ne = mat->col_end[i];
    }
    n = ne-nb;
    sum[i] = 0;
    if(n > 0) {
      if(isrow)
        for(jj = nb; jj < ne; jj++)
          sum[i] += ROW_MAT_COL(jj);
      else
        for(jj = nb; jj < ne; jj++)
          sum[i] += COL_MAT_ROW(mat->col_mat[jj]);
      sum[i] /= n;
    }
    else
      sum[i] = sum[i-1];
  }

  /* Loop over items again, find largest difference and make monotone */
  hold = 0;
  biggest = 0;
  for(i = 2; i <= items; i++) {
    hold = sum[i] - sum[i-1];
    if(hold > 0) {
      if(hold > biggest)
        biggest = hold;
    }
    else
      hold = 0;
    sum[i-1] = hold;
  }

  /* Loop over items again and find differences exceeding threshold;
     the discriminatory power of this routine depends strongly on the
     magnitude of the scaling factor - from empirical evidence > 0.9 */
  biggest = MAX(1, 0.9*biggest);
  n = 0;
  nb = 0;
  ne = 0;
  for(i = 1; i < items; i++)
    if(sum[i] > biggest) {
      ne += i-nb;        /* Compute sum of index gaps between maxima */
      nb = i;
      n++;               /* Increment count */
    }

  /* Clean up */
  FREE(sum);

  /* Require that the maxima are spread "nicely" across the columns,
     otherwise return that there is only one monolithic block.
     (This is probably an area for improvement in the logic!) */
  if(n > 0) {
    ne /= n;                 /* Average index gap between maxima */
    i = IF(isrow, lp->columns, lp->rows);
    nb = i / ne;             /* Another estimated block count */
    if(abs(nb - n) > 2)      /* Probably Ok to require equality (nb==n)*/
      n = 1;
    else if(autodefine)      /* Generate row/column break-indeces for partial pricing */
      set_partialprice(lp, nb, NULL, isrow);
  }
  else
    n = 1;

  return( n );
}
STATIC int partial_blockStart(lprec *lp, MYBOOL isrow)
{
  partialrec *blockdata;

  blockdata = IF(isrow, lp->rowblocks, lp->colblocks);
  if(blockdata == NULL)
    return( 1 );
  else
    return( blockdata->blockend[blockdata->blocknow-1] );
}
STATIC int partial_blockEnd(lprec *lp, MYBOOL isrow)
{
  partialrec *blockdata;

  blockdata = IF(isrow, lp->rowblocks, lp->colblocks);
  if(blockdata == NULL)
    return( IF(isrow, lp->rows, lp->sum) );
  else
    return( blockdata->blockend[blockdata->blocknow]-1 );
}
STATIC int partial_blockNextPos(lprec *lp, int block, MYBOOL isrow)
{
  partialrec *blockdata;

  blockdata = IF(isrow, lp->rowblocks, lp->colblocks);
#ifdef Paranoia
  if((blockdata == NULL) || (block <= 1) || (block > blockdata->blockcount)) {
    report(lp, SEVERE, "partial_blockNextPos: Invalid block %d specified.\n",
                       block);
    return( -1 );
  }
#endif
  block--;
  if(blockdata->blockpos[block] == blockdata->blockend[block+1])
    blockdata->blockpos[block] = blockdata->blockend[block];
  else
    blockdata->blockpos[block]++;
  return( blockdata->blockpos[block] );
}
STATIC MYBOOL partial_blockStep(lprec *lp, MYBOOL isrow)
{
  partialrec *blockdata;

  blockdata = IF(isrow, lp->rowblocks, lp->colblocks);
  if(blockdata == NULL)
    return( FALSE );
  else if(blockdata->blocknow < blockdata->blockcount) {
    blockdata->blocknow++;
    return( FALSE);
  }
  else {
    blockdata->blocknow = 1;
    return( TRUE );
  }
}
STATIC MYBOOL partial_isVarActive(lprec *lp, int varno, MYBOOL isrow)
{
  partialrec *blockdata;

  blockdata = IF(isrow, lp->rowblocks, lp->colblocks);
  if(blockdata == NULL)
    return( TRUE );
  else {
    return( (MYBOOL) ((varno >= blockdata->blockend[blockdata->blocknow-1]) &&
                      (varno < blockdata->blockend[blockdata->blocknow])) );
  }
}
STATIC MYBOOL multi_prepareVector(lprec *lp, int blocksize)
{
  if((blocksize > 1) && (lp->multiblockdiv > 1)) {
    lp->multisize = blocksize;
    lp->multisize += lp->multisize/lp->multiblockdiv;
    lp->multivar = (pricerec *) realloc(lp->multivar, (lp->multisize+1)*sizeof(*(lp->multivar)));
  }
  else {
    lp->multisize = 0;
    FREE(lp->multivar);
  }
  lp->multinow = 1;

  return( TRUE );
}

STATIC int *multi_createVarList(lprec *lp)
{
  int *list = NULL;

  if((lp->multiused > 0) && allocINT(lp, &list, lp->multiused+1, FALSE)) {
    int i;

    list[0] = lp->multiused;
    for(i = 1; i <= lp->multiused; i++)
      list[i] = lp->multivar[i-1].varno;
  }
  return( list );
}

