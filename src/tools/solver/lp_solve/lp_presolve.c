
/* -------------------------------------------------------------------------
   Presolve routines for lp_solve v5.0+
   -------------------------------------------------------------------------
    Author:        Kjell Eikland
    Contact:       kjell.eikland@broadpark.no
    License terms: LGPL.

    Requires:      lp_lib.h, lp_presolve, lp_crash.h, lp_scale.h

    Release notes:
    v5.0.0  1 January 2004      Significantly expanded and repackaged
                                presolve routines.
    v5.0.1  1 April   2004      Added reference to new crash module

   ------------------------------------------------------------------------- */

#include <string.h>
#include "commonlib.h"
#include "lp_lib.h"
#include "lp_presolve.h"
#include "lp_crash.h"
#include "lp_scale.h"

#ifdef FORTIFY
# include "lp_fortify.h"
#endif


#define DoPresolveRounding

STATIC REAL presolve_round(lprec *lp, REAL value, MYBOOL isGE)
{
#ifdef DoPresolveRoundingTight
  REAL epsvalue = lp->epsmachine;
#else
  REAL epsvalue = lp->epsprimal;
#endif

#ifdef DoPresolveRounding
  value += my_chsign(isGE, epsvalue/SCALEDINTFIXRANGE);
  value = restoreINT(value, epsvalue);
#endif
  return( value );
}
STATIC REAL presolve_precision(lprec *lp, REAL value)
{
#ifdef DoPresolveRoundingTight
  REAL epsvalue = lp->epsmachine;
#else
  REAL epsvalue = lp->epsprimal;
#endif

#ifdef DoPresolveRounding
  value = restoreINT(value, epsvalue);
#endif
  return( value );
}

STATIC int presolve_validate(lprec *lp, LLrec *rowmap, LLrec *colmap)
{
  int i, j, errc = 0;

  /* Validate constraint bounds */
  for(i = 1; i < lp->rows; i++) {
    if((rowmap != NULL) && !isActiveLink(rowmap, i))
      continue;
    /* Check if we have a negative range */
    if(lp->orig_upbo[i] < 0) {
      errc++;
      report(lp, SEVERE, "presolve_validate: Detected negative range %g for row %d\n",
                         lp->orig_upbo[i], i);
    }
  }
  /* Validate variables */
  for(j = 1; j < lp->columns; j++) {
    if((colmap != NULL) && !isActiveLink(colmap, j))
      continue;
    i = lp->rows+j;
    /* Check if we have infeasible  bounds */
    if(lp->orig_lowbo[i] > lp->orig_upbo[i]) {
      errc++;
      report(lp, SEVERE, "presolve_validate: Detected UB < LB for column %d\n",
                         j);
    }
  }
  /* Return total number of errors */
  return( errc );
}


STATIC int presolve_tighten(lprec *lp, int i, int j, LLrec *colmap,
                            int items,
                            REAL *UPpluvalue, REAL *UPnegvalue,
                            REAL *LOpluvalue, REAL *LOnegvalue, int *count)
{
  REAL    RHlow, RHup, RHrange, LObound, UPbound, Value, margin;
  MYBOOL  SCvar;
  int     elmnr, elmend, k, oldcount;
  MATitem *matelem;
  MATrec  *mat = lp->matA;

#ifdef DoPresolveRoundingTight
  margin = lp->epsmachine;
#else
  margin = lp->epsprimal;
#endif
#ifdef Paranoia
  if(!isActiveLink(colmap, j))
    report(lp, SEVERE, "presolve_tighten: The selected column %d was already eliminated\n",
                       j);
#endif

  Value = get_mat(lp,i,j);
  if(Value == 0)
    return(RUNNING);

  /* Initialize and identify semicontinuous variable */
  LObound = get_lowbo(lp, j);
  UPbound = get_upbo(lp, j);
  SCvar = is_semicont(lp, j);
  if(SCvar && (UPbound > LObound)) {
    if(LObound > 0)
      LObound = 0;
    else if(UPbound < 0)
      UPbound = 0;
  }

  /* Get singleton variable bounds */
  if(items == 1) {
    RHlow = get_rh_lower(lp, i);
    RHup  = get_rh_upper(lp, i);
    if(Value < 0) {
      RHrange = RHup;
      RHup = -RHlow;
      RHlow = -RHrange;
      Value = -Value;
    }
    if(RHlow <= -lp->infinite)
      RHrange = my_chsign(Value < 0, -lp->infinite);
    else
      RHrange = RHlow / Value;
    if(RHrange > -lp->infinite)
      RHlow = MAX(LObound, RHrange);

    if(RHup >= lp->infinite)
      RHrange = my_chsign(Value < 0, lp->infinite);
    else
      RHrange = RHup / Value;
    if(RHrange < lp->infinite)
      RHup = MIN(UPbound, RHrange);
  }
  else {
    RHlow = -lp->infinite;
    RHup = lp->infinite;
  }

  /* Look for opportunity to tighten upper and lower variable and constraint bounds */
  oldcount = (*count);
  if((RHup < lp->infinite) && (RHup+margin < UPbound)) {
    if(is_int(lp, j)) {
      if(lp->columns_scaled && is_integerscaling(lp) && (ceil(RHup) - RHup > margin))
        RHup = floor(RHup) + margin;
      else
        RHup = floor(RHup);
    }
    if(UPbound < lp->infinite) {
      elmnr = mat->col_end[j-1];
      elmend = mat->col_end[j];
      for(matelem = mat->col_mat+elmnr; elmnr < elmend; elmnr++, matelem++) {
        k = (*matelem).row_nr;
        Value = my_chsign(is_chsign(lp, k), (*matelem).value);
        Value = unscaled_mat(lp, Value, k, j);
        if((Value > 0) && (UPpluvalue[k] < lp->infinite))
          UPpluvalue[k] += (RHup-UPbound)*Value;
        else if((Value < 0) && (UPnegvalue[i] < lp->infinite))
          UPnegvalue[k] += (RHup-UPbound)*Value;
      }
    }
    if(RHup < UPbound) {
      UPbound = RHup;
      (*count)++;
    }
  }
  if((RHlow > -lp->infinite) && (RHlow-margin > LObound)) {
    if(is_int(lp, j)) {
      if(lp->columns_scaled && is_integerscaling(lp) && (RHlow - floor(RHlow) > margin))
        RHlow = ceil(RHlow)-margin;
      else
        RHlow = ceil(RHlow);
    }
    if(LObound > -lp->infinite) {
      elmnr = mat->col_end[j-1];
      elmend = mat->col_end[j];
      for(matelem = mat->col_mat+elmnr; elmnr < elmend; elmnr++, matelem++) {
        k = (*matelem).row_nr;
        Value = my_chsign(is_chsign(lp, k), (*matelem).value);
        Value = unscaled_mat(lp, Value, k, j);
        if((Value > 0) && (LOpluvalue[k] > -lp->infinite))
          LOpluvalue[k] += (RHlow-LObound)*Value;
        else if((Value < 0) && (LOnegvalue[k] > -lp->infinite))
          LOnegvalue[k] += (RHlow-LObound)*Value;
      }
    }
    if(RHlow > LObound) {
      LObound = RHlow;
      (*count)++;
    }
  }

  /* Now set the new variable bounds, if they are tighter */
  if((*count) > oldcount) {
#if 0     /* Experimental new version */
    UPbound = presolve_round(lp, UPbound-margin, FALSE);
    LObound = presolve_round(lp, LObound+margin, TRUE);
#else     /* Safe new version */
    UPbound = presolve_precision(lp, UPbound);
    LObound = presolve_precision(lp, LObound);
#endif
    if(LObound > UPbound) {
      if(LObound-UPbound < margin) {
        LObound = UPbound;
      }
      else {
        report(lp, IMPORTANT, "presolve_tighten: Found LB %g > UB %g in row %d, column %d\n",
		                          LObound, UPbound, i, j);
	      return(INFEASIBLE);
      }
    }
    if(lp->spx_trace || (lp->verbose > DETAILED))
      report(lp, NORMAL, "presolve_tighten: Replaced bounds on column %d to [%g ... %g]\n",
	                       j, LObound, UPbound);
    set_bounds(lp, j, LObound, UPbound);
  }

  return( RUNNING );
}

STATIC void presolve_init(lprec *lp, int *plucount, int *negcount, int *pluneg)
{
  int    i, ix, colnr;
  MYBOOL isMI;
  REAL   value, lobound, upbound;
  MATrec *mat = lp->matA;

  /* First do tallies; loop over nonzeros by column */
  for(colnr = 1; colnr <= lp->columns; colnr++) {

    upbound = get_upbo(lp, colnr);
    lobound = get_lowbo(lp, colnr);

    /* Handle strictly negative variables as converted to positive range with negative sign */
    isMI = is_negative(lp, colnr);

    if(is_semicont(lp, colnr) && (upbound > lobound)) {
      if(lobound > 0)
        lobound = 0;
      else if(upbound < 0)
        upbound = 0;
    }

    for(ix = mat->col_end[colnr - 1]; ix < mat->col_end[colnr]; ix++) {

     /* Retrieve row data and prepare */
      i = mat->col_mat[ix].row_nr;
      value = my_chsign(isMI, mat->col_mat[ix].value);
      value = my_chsign(is_chsign(lp, i), value);

     /* Cumulate counts */
      if(value > 0)
        plucount[i]++;
      else
        negcount[i]++;
      if((lobound < 0) && (upbound > 0))
        pluneg[i]++;
    }
  }
}

STATIC int presolve_nextcol(MATrec *mat, int rownr, int prevcol, LLrec *colmap)
/* Find the first active (non-eliminated) nonzero column in rownr after prevcol */
{
  int j, jb = 0, je = mat->row_end[rownr];

  if(rownr > 0)
    jb = mat->row_end[rownr-1];
  for(; jb < je; jb++) {
    j = ROW_MAT_COL(mat->row_mat[jb]);
    if((j > prevcol) && isActiveLink(colmap, j))
       return( jb );
  }
  return( je );
}
STATIC int presolve_nextrow(MATrec *mat, int colnr, int prevrow, LLrec *rowmap)
/* Find the first active (non-eliminated) nonzero row in colnr after prevrow */
{
  int i, ib = mat->col_end[colnr-1], ie = mat->col_end[colnr];

  for(; ib < ie; ib++) {
    i = mat->col_mat[ib].row_nr;
    if((i > prevrow) && isActiveLink(rowmap, i))
       return( ib );
  }
  return( ie );
}

STATIC void presolve_rowupdate(lprec *lp, int rownr, int *collength, MYBOOL remove)
{
  if(collength != NULL) {
    if(remove) {
      int     i, ie;
      MATrec  *mat = lp->matA;

      i = mat->row_end[rownr-1];
      ie = mat->row_end[rownr];
      for(; i < ie; i++)
        collength[ROW_MAT_COL(mat->row_mat[i])]--;
    }
  }
}

STATIC void presolve_colupdate(lprec *lp, int colnr, int *pluneg, int *rowlength, 
                                                     int *plucount, int *negcount,
                                                     REAL *pluupper, REAL *negupper,
                                                     REAL *plulower, REAL *neglower, MYBOOL remove)
{
  int     i, ix, ie;
  MYBOOL  isneg, isMI;
  REAL    lobound, upbound, lovalue, upvalue,
          value, fixvalue, mult, epsvalue;
  MATitem *matelem;
  MATrec  *mat = lp->matA;

#ifdef DoPresolveRoundingTight
  epsvalue = lp->epsmachine;
#else
  epsvalue = lp->epsprimal;
#endif

  if(remove)
    mult = -1;
  else
    mult = 1;
  upbound = get_upbo(lp, colnr);
  lobound = get_lowbo(lp, colnr);

  /* Handle strictly negative variables as converted to positive range with negative sign */
  isMI = is_negative(lp, colnr);

  /* Set "exiting" value in case we are deleting a variable */
  if(upbound-lobound < epsvalue)
    fixvalue = lp->orig_lowbo[lp->rows+colnr];
  else
    fixvalue = 0;

  /* Adjust semi-continuous variable bounds to zero-base */
  if(is_semicont(lp, colnr) && (upbound > lobound)) {
    if(lobound > 0)
      lobound = 0;
    else if(upbound < 0)
      upbound = 0;
  }

  ix = mat->col_end[colnr - 1];
  ie = mat->col_end[colnr];
  for(matelem = mat->col_mat+ix; ix < ie; ix++, matelem++) {

   /* Retrieve row data and adjust RHS if we are deleting a variable */
    i = (*matelem).row_nr;
    value = (*matelem).value;

    if(remove && (fixvalue != 0)) {
      lp->orig_rh[i] -= value * fixvalue;
      my_roundzero(lp->orig_rh[i], epsvalue);
    }

   /* Prepare for further processing */
    value = unscaled_mat(lp, value, i, colnr);
    value = my_chsign(is_chsign(lp, i), value);
    isneg = (MYBOOL) (value < 0);
    if(isMI)
      isneg = !isneg;

   /* Reduce row variable counts */
    if(remove) {
      if(rowlength != NULL)
        rowlength[i]--;
      if(isneg)
        negcount[i]--;
      else
        plucount[i]--;
      if((lobound < 0) && (upbound > 0))
        pluneg[i]--;
    }

   /* Compute associated constraint contribution values */
    if(isneg) {
      chsign_bounds(&lobound, &upbound);
      value = -value;
    }
    upvalue = my_if(upbound < lp->infinite, value*upbound, lp->infinite);
    lovalue = my_if(lobound > -lp->infinite, value*lobound, -lp->infinite);

   /* Cumulate effective upper row bound (only bother about non-finite bound) */
    if(isneg) {
      if((negupper[i] < lp->infinite) && (upvalue < lp->infinite)) {
        negupper[i] += mult*upvalue;
        negupper[i] = presolve_round(lp, negupper[i], FALSE);
      }
      else if(!remove)
        negupper[i] = lp->infinite;
    }
    else {
      if((pluupper[i] < lp->infinite) && (upvalue < lp->infinite)) {
        pluupper[i] += mult*upvalue;
        pluupper[i] = presolve_round(lp, pluupper[i], FALSE);
      }
      else if(!remove)
        pluupper[i] = lp->infinite;
    }

   /* Cumulate effective lower row bound (only bother about non-finite bound) */
    if(isneg) {
      if((neglower[i] > -lp->infinite) && (lovalue > -lp->infinite)) {
        neglower[i] += mult*lovalue;
        neglower[i] = presolve_round(lp, neglower[i], TRUE);
      }
      else if(!remove)
        neglower[i] = -lp->infinite;

      /* Remember to reset reversed bounds */
      chsign_bounds(&lobound, &upbound);
    }
    else {
      if((plulower[i] > -lp->infinite) && (lovalue > -lp->infinite)) {
        plulower[i] += mult*lovalue;
        plulower[i] = presolve_round(lp, plulower[i], TRUE);
      }
      else if(!remove)
        plulower[i] = -lp->infinite;
    }
  }
}


STATIC void presolve_finalize(lprec *lp, LLrec *rowmap, LLrec *colmap)
{
  int i, ke, kb, n;

  if(colmap != NULL) {
    ke = lastInactiveLink(colmap);
    n = countInactiveLink(colmap);
    if((n > 0) && (ke > 0)) {
      kb = lastActiveLink(colmap);
      while(kb > ke)
        kb = prevActiveLink(colmap, kb);
      ke++;
      while ((n > 0) && (ke > 0)) {
        for(i = ke-1; i > kb; i--) {
          del_column(lp, i);
          n--;
        }
        ke = kb;
        kb = prevActiveLink(colmap, kb);
      }
    }
    freeLink(&colmap);
  }
  if(rowmap != NULL) {
    ke = lastInactiveLink(rowmap);
    n = countInactiveLink(rowmap);
    if((n > 0) && (ke > 0)) {
      kb = lastActiveLink(rowmap);
      while(kb > ke)
        kb = prevActiveLink(rowmap, kb);
      ke++;
      while ((n > 0) && (ke > 0)) {
        for(i = ke-1; i > kb; i--) {
#ifdef DeferredCompactA
          del_constraint(lp, -i);
#else
          del_constraint(lp, i);
#endif
          n--;
        }
        ke = kb;
        kb = prevActiveLink(rowmap, kb);
      }
    }
    freeLink(&rowmap);
#ifdef DeferredCompactA
    mat_rowcompact(lp->matA);
#endif
  }
  mat_validate(lp->matA);
}

STATIC REAL sumplumin(lprec *lp, int item, REAL *plu, REAL *neg)
{
  if(fabs(plu[item]) >= lp->infinite)
    return( plu[item] );
  else if(fabs(neg[item]) >= lp->infinite)
    return( neg[item] );
  else
    return( plu[item]+neg[item] );
}

STATIC int presolve_collength(MATrec *mat, int column, LLrec *rowmap, int *collength)
{
  if(collength != NULL)
    column = collength[column];
  else if(rowmap == NULL)
    column = mat_collength(mat, column);
  else {
    int ib = mat->col_end[column-1],
        ie = mat->col_end[column];
    MATitem *item;
    column = 0;
    for(item = mat->col_mat+ib; ib < ie; ib++, item++) {
      if(((*item).row_nr == 0) ||
         isActiveLink(rowmap, (*item).row_nr))
        column++;
    }
  }
  return( column );
}
STATIC int presolve_rowlength(MATrec *mat, int row, LLrec *colmap, int *rowlength)
{
  if(rowlength != NULL)
    row = rowlength[row];
  else if(colmap == NULL)
    row = mat_rowlength(mat, row);
  else {
    int ib = 0, ie = mat->row_end[row];
    if(row > 0)
      ib = mat->row_end[row-1],
    row = 0;
    for(; ib < ie; ib++) {
      if(isActiveLink(colmap, ROW_MAT_COL(mat->row_mat[ib])))
        row++;
    }
  }
  return( row );
}

STATIC int presolve(lprec *lp)
{
  MYBOOL candelete;
  int    i,j,ix,iix,jx,jjx, n,nm,nn=0, nc,nv,nb,nr,ns,nt,nl;
  int	   status;
  int    *plucount, *negcount, *pluneg;
  int    *rowlength = NULL, *collength = NULL;
  REAL   *pluupper, *negupper,
         *plulower, *neglower,
         value, bound, test, epsvalue = lp->epsprimal;
  MATrec *mat = lp->matA;
  LLrec  *rowmap = NULL, *colmap = NULL;

 /* Check if we have already done presolve */
  status = RUNNING;
  if(lp->wasPresolved)
    return(status);

 /* Finalize basis indicators; if no basis was created earlier via
    set_basis or crash_basis then simply set the default basis. */
  if(!lp->basis_valid)
    lp->var_basic[0] = AUTOMATIC; /* Flag that we are presolving */
  
 /* Lock the variable mapping arrays and counts ahead of any row/column
    deletion or creation in the course of presolve, solvelp or postsolve */
  varmap_lock(lp);
  mat_validate(mat);

 /* Do the scaling of the problem (can also be moved to the end of the
    presolve block (before "Finish:") to possibly reduce rounding errors */
#ifndef PostScale
  lp_solve_auto_scale(lp);
#endif

#if 0
write_lp(lp, "test_in.lp");    /* Write to lp-formatted file for debugging */
/*write_mps(lp, "test_in.mps");*/  /* Write to lp-formatted file for debugging */
#endif

 /* Do traditional simple presolve */
  yieldformessages(lp);
  if((lp->do_presolve & PRESOLVE_LASTMASKMODE) == PRESOLVE_NONE) 
    mat_checkcounts(mat, NULL, NULL, TRUE);

  else {

    if(lp->full_solution == NULL)
      allocREAL(lp, &lp->full_solution, lp->sum_alloc+1, TRUE);

    nm = 0;
    nl = 0;
    nv = 0;
    nc = 0;
	  nb = 0;
    nr = 0;
	  ns = 0;
	  nt = 0;

   /* Identify infeasible SOS'es prior to any pruning */
	  for(i = 1; i <= SOS_count(lp); i++) {
	    nn = SOS_infeasible(lp->SOS, i);
	    if(nn > 0) {
        report(lp, NORMAL, "presolve: Found SOS %d (type %d) to be range-infeasible on variable %d\n",
			                      i, SOS_get_type(lp->SOS, i), nn);
        status = INFEASIBLE;
        ns++;
      }
    }
	  if(ns > 0)
	    goto Finish;

   /* Create row and column counts */
#if 1
    allocINT(lp,  &rowlength, lp->rows + 1, TRUE);
    for(i = 1; i <= lp->rows; i++)
      rowlength[i] = presolve_rowlength(mat, i, NULL, NULL);
    allocINT(lp,  &collength, lp->columns + 1, TRUE);
    for(j = 1; j <= lp->columns; j++)
      collength[j] = presolve_collength(mat, j, NULL, NULL);
#endif
      
   /* Create NZ count and sign arrays, and do general initialization of row bounds */
    allocINT(lp,  &plucount,  lp->rows + 1, TRUE);
    allocINT(lp,  &negcount,  lp->rows + 1, TRUE);
    allocINT(lp,  &pluneg,    lp->rows + 1, TRUE);
    allocREAL(lp, &pluupper,  lp->rows + 1, TRUE);
    allocREAL(lp, &negupper,  lp->rows + 1, TRUE);
    allocREAL(lp, &plulower,  lp->rows + 1, TRUE);
    allocREAL(lp, &neglower,  lp->rows + 1, TRUE);
    createLink(lp->rows, &rowmap, NULL);
      fillLink(rowmap);
    createLink(lp->columns, &colmap, NULL);
      fillLink(colmap);
    presolve_init(lp, plucount, negcount, pluneg);

   /* Accumulate constraint bounds based on bounds on individual variables */
Restart:
    nm++;
    for(j = firstActiveLink(colmap); j != 0; j = nextActiveLink(colmap, j)) {
      presolve_colupdate(lp, j, pluneg, NULL, 
                                plucount, negcount,
                                pluupper, negupper,
                                plulower, neglower, FALSE);
      if((status == RUNNING) && userabort(lp, -1))
        goto Complete;
    }

    /* Reentry point */
Redo:
    nl++;
    nn = 0;

   /* Eliminate empty or fixed columns (including trivial OF column singletons) */
    if(is_presolve(lp, PRESOLVE_COLS) && mat_validate(mat)) {
      if(userabort(lp, -1))
        goto Complete;
      n = 0;
      for(j = lastActiveLink(colmap); j > 0; ) {
        candelete = FALSE;
        ix = lp->rows + j;
        iix = presolve_collength(mat, j, rowmap, collength);
        if(iix == 0) {
          if(SOS_is_member(lp->SOS, 0, j))
            report(lp, NORMAL, "presolve: Found empty variable %d as member of a SOS\n",
                                get_col_name(lp,j));
          else {
            if(lp->orig_lowbo[ix] != 0)
              report(lp, DETAILED, "presolve: Found empty non-zero variable %s\n",
                                    get_col_name(lp,j));
            candelete = TRUE;
          }
        }
        else if(isOrigFixed(lp, ix)) {
          if(SOS_is_member(lp->SOS, 0, j))
            continue;
          report(lp, DETAILED, "presolve: Eliminated variable %s fixed at %g\n",
                                get_col_name(lp,j), get_lowbo(lp, j));
          candelete = TRUE;
        }
        else if((iix == 1) && (mat->col_mat[iix = mat->col_end[j-1]].row_nr == 0) &&
                !SOS_is_member(lp->SOS, 0, j)) {

          if(get_OF_raw(lp, ix) > 0)
            test = get_lowbo(lp, j);
          else
            test = get_upbo(lp, j);
          if(isInf(lp, fabs(test))) {
            report(lp, DETAILED, "presolve: Unbounded variable %s\n",
                                  get_col_name(lp,j));
            status = UNBOUNDED;
          }
          else {
            /* Fix the value at its best bound */
            set_bounds(lp, j, test, test);
            report(lp, DETAILED, "presolve: Eliminated trivial variable %s fixed at %g\n",
                                  get_col_name(lp,j), test);
            candelete = TRUE;
          }
        }

        ix = j;
        j = prevActiveLink(colmap, j);
        if(candelete) {
          value = get_lowbo(lp, ix);
          lp->full_solution[lp->orig_rows + lp->var_to_orig[lp->rows + ix]] = value;
          presolve_colupdate(lp, ix, pluneg, rowlength,
                                     plucount, negcount,
                                     pluupper, negupper,
                                     plulower, neglower, TRUE);
          removeLink(colmap, ix);
          n++;
      	  nv++;
  		    nn++;
        }
      }
    }

   /* Eliminate linearly dependent rows; loop backwards over every row */
    if(is_presolve(lp, PRESOLVE_LINDEP) && mat_validate(mat)) {
      int firstix, RT1, RT2;
      if(userabort(lp, -1))
        goto Complete;
      n = 0;
      for(i = lastActiveLink(rowmap); (i > 0) && (status == RUNNING); ) {

        /* First scan for rows with identical row lengths */
        ix = prevActiveLink(rowmap, i);
        if(ix == 0)
          break;

        /* Don't bother about empty rows or row singletons since they are
           handled by PRESOLVE_ROWS */
        j = presolve_rowlength(mat, i, colmap, rowlength);
        if(j <= 1) {
          i = ix;
          continue;
        }

#if 0
        /* Enable this to scan all rows back */
        RT2 = lp->rows;
#else
        RT2 = 1;
#endif
        firstix = ix;
        for(RT1 = 0; (ix > 0) && (RT1 < RT2) && (status == RUNNING);
            ix = prevActiveLink(rowmap, ix), RT1++)  {
          candelete = FALSE;
          if(presolve_rowlength(mat, ix, colmap, rowlength) != j)
            continue;

          /* Check if the beginning columns are identical; if not, continue */
          iix = presolve_nextcol(mat, ix, 0, colmap);
          jjx = presolve_nextcol(mat, i,  0, colmap);

          if(ROW_MAT_COL(mat->row_mat[iix]) != ROW_MAT_COL(mat->row_mat[jjx]))
            continue;

          /* We have a candidate row; check if the entries have a fixed non-zero ratio */
          test  = get_mat_byindex(lp, iix, TRUE);
          value = get_mat_byindex(lp, jjx, TRUE);
          bound = test / value;
          value = bound;

          /* Loop over remaining entries */
          jx = mat->row_end[i];
          jjx = presolve_nextcol(mat, i, ROW_MAT_COL(mat->row_mat[jjx]), colmap);
          for(; (jjx < jx) && (value == bound);
              jjx = presolve_nextcol(mat, i, ROW_MAT_COL(mat->row_mat[jjx]), colmap)) {
             iix = presolve_nextcol(mat, ix, ROW_MAT_COL(mat->row_mat[iix]), colmap);
             if(ROW_MAT_COL(mat->row_mat[iix]) != ROW_MAT_COL(mat->row_mat[jjx]))
               break;
             test  = get_mat_byindex(lp, iix, TRUE);
             value = get_mat_byindex(lp, jjx, TRUE);

             /* If the ratio is different from the reference value we have a mismatch */
             value = test / value;
             if(bound == lp->infinite)
               bound = value;
             else if(fabs(value - bound) > epsvalue)
               break;
          }

          /* Check if we found a match (we traversed all active columns without a break) */
          if(jjx >= jx) {

            /* Get main reference values */
            test  = lp->orig_rh[ix];
            value = lp->orig_rh[i] * bound;

            /* First check for inconsistent equalities */
            if((fabs(test - value) > epsvalue) &&
               ((get_constr_type(lp, ix) == EQ) && (get_constr_type(lp, i) == EQ)))
              status = INFEASIBLE;

            else {

              /* Update lower and upper bounds */
              if(is_chsign(lp, i) != is_chsign(lp, ix))
                bound = -bound;

              test = get_rh_lower(lp, i);
              if(test <= -lp->infinite)
                test *= my_sign(bound);
              else
                test *= bound;

              value = get_rh_upper(lp, i);
              if(value >= lp->infinite)
                value *= my_sign(bound);
              else
                value *= bound;

              if((bound < 0))
                swapREAL(&test, &value);

              if(get_rh_lower(lp, ix) < test)
                set_rh_lower(lp, ix, test);
              if(get_rh_upper(lp, ix) > value)
                set_rh_upper(lp, ix, value);

              /* Check results */
              test  = get_rh_lower(lp, ix);
              value = get_rh_upper(lp, ix);
              if(fabs(value-test) < epsvalue)
                lp_solve_set_constr_type(lp, ix, EQ);
              else if(value < test)
                status = INFEASIBLE;

              /* Verify if we can continue */
              candelete = (MYBOOL) (status == RUNNING);
              if(!candelete) {
                report(lp, IMPORTANT, "presolve: Range infeasibility found involving rows %d and %d\n",
                                      ix, i);
              }
            }
          }
          /* Perform i-row deletion if authorized */
          if(candelete) {
            presolve_rowupdate(lp, i, collength, TRUE);
            removeLink(rowmap, i);
            n++;
            nc++;
          }
        }
        i = firstix;
      }
    }

   /* Aggregate and tighten bounds using 2-element EQs */
    if(FALSE &&
       (lp->equalities > 0) && is_presolve(lp, PRESOLVE_AGGREGATE) && mat_validate(mat)) {
      if(userabort(lp, -1))
        goto Complete;
      n = 0;
      for(i = lastActiveLink(rowmap); (i > 0) && (status == RUNNING); ) {
        /* Find an equality constraint with 2 elements; the pivot row */
        if(!is_constr_type(lp, i, EQ) || (presolve_rowlength(mat, i, colmap, rowlength) != 2)) {
          i = prevActiveLink(rowmap, i);
          continue;
        }
        /* Get the column indeces of NZ-values of the pivot row */
        jx = mat->row_end[i-1];
        j =  mat->row_end[i];
        for(; jx < j; jx++)
          if(isActiveLink(colmap, ROW_MAT_COL(mat->row_mat[jx])))
            break;
        jjx = jx+1;
        for(; jjx < j; jjx++)
          if(isActiveLink(colmap, ROW_MAT_COL(mat->row_mat[jjx])))
            break;
        jx  = ROW_MAT_COL(mat->row_mat[jx]);
        jjx = ROW_MAT_COL(mat->row_mat[jjx]);
        if(SOS_is_member(lp->SOS, 0, jx) && SOS_is_member(lp->SOS, 0, jjx)) {
          i = prevActiveLink(rowmap, i);
          continue;
        }
        /* Determine which column we should eliminate (index in jx) :
           1) the longest column
           2) the variable not being a SOS member
           3) an integer variable  */
        if(presolve_collength(mat, jx, rowmap, collength) < 
           presolve_collength(mat, jjx, rowmap, collength))
          swapINT(&jx, &jjx);
        if(SOS_is_member(lp->SOS, 0, jx))
          swapINT(&jx, &jjx);
        if(!is_int(lp, jx) && is_int(lp, jjx))
          swapINT(&jx, &jjx);
        /* Whatever the priority above, we must have bounds to work with;
           give priority to the variable with the smallest bound */
        test  = get_upbo(lp, jjx)-get_lowbo(lp, jjx);
        value = get_upbo(lp, jx)-get_lowbo(lp, jx);
        if(test < value)
          swapINT(&jx, &jjx);
        /* Try to set tighter bounds on the non-eliminated variable (jjx) */
        test  = get_mat(lp, i, jjx); /* Non-eliminated variable coefficient a */
        value = get_mat(lp, i, jx);  /* Eliminated variable coefficient     b */
#if 1
        bound = get_lowbo(lp, jx);
        if((bound > -lp->infinite)) {
          bound = (get_rh(lp, i)-value*bound) / test;
          if(bound < get_upbo(lp, jjx)-epsvalue)
            lp_solve_set_upbo(lp, jjx, presolve_round(lp, bound, FALSE));
        }
        bound = get_upbo(lp, jx);
        if((bound < lp->infinite)) {
          bound = (get_rh(lp, i)-value*bound) / test;
          if(bound > get_lowbo(lp, jjx)+epsvalue)
            lp_solve_set_lowbo(lp, jjx, presolve_round(lp, bound, TRUE));
        }
        i = prevActiveLink(rowmap, i);
#else
        /* Loop over the non-zero rows of the column to be eliminated;
           substitute jx-variable by updating rhs and jjx coefficients */
        for(ix = mat->col_end[jx-1]; ix < mat->col_end[jx]; ix++) {
          REAL newvalue;
          iix = mat->col_mat[ix].row_nr;
          if((iix == i) ||
             ((iix > 0) && !isActiveLink(rowmap, iix)))
            continue;
          /* Do the update */
          bound = unscaled_mat(lp, mat->col_mat[ix].value, iix, jx)/value;
          bound = my_chsign(is_chsign(lp, iix), bound);
          newvalue = get_mat(lp, iix, jjx) - bound*test;
            lp_solve_set_mat(lp, iix, jjx, presolve_precision(lp, newvalue));
          newvalue = get_rh(lp, iix) - bound*get_rh(lp, i);
            lp_solve_set_rh(lp, iix, presolve_precision(lp, newvalue));
        }
        /* Delete the column */
        removeLink(colmap, jx);
        nc++;
        n++;
        /* Delete the row */
        ix = i;
        i = prevActiveLink(rowmap, i);
        presolve_rowupdate(lp, ix, collength, TRUE);
        removeLink(rowmap, ix);
        nr++;
        n++;
        mat_validate(mat);
#endif
      }
    }

#if 0
   /* Increase A matrix sparsity by discovering common subsets using 2-element EQs */
    if((lp->equalities > 0) && is_presolve(lp, PRESOLVE_SPARSER) && mat_validate(mat)) {
      if(userabort(lp, -1))
        goto Complete;
      n = 0;
      for(i = lastActiveLink(rowmap); (i > 0) && (status == RUNNING); ) {
        candelete = FALSE;
        /* Find an equality constraint with 2 elements; the pivot row */
        if(!is_constr_type(lp, i, EQ) || (mat_rowlength(mat, i) != 2)) {
          i = prevActiveLink(rowmap, i);
          continue;
        }
        /* Get the column indeces of NZ-values of the pivot row */
        jx = mat->row_end[i-1];
        jx = ROW_MAT_COL(mat->row_mat[jx]);
        jjx = mat->row_end[i];
        jjx = ROW_MAT_COL(mat->row_mat[jjx]);
        /* Scan to find a row with matching column entries */
        for(ix = lp->col_end[jx-1]; ix < lp->col_end[jx]; ix++) {
          if(lp->col_mat[ix].row_nr == i)
            continue;
          /* We now have a single matching value, find the next */
          if(ix < lp->col_end[jx]) {
            for(iix = lp->col_end[jjx-1]; iix < lp->col_end[jjx]; iix++)
              if(lp->col_mat[iix].row_nr >= ix)
                break;
            /* Abort this row if there was no second column match */
            if((iix >= lp->col_end[jjx]) || (lp->col_mat[iix].row_nr > ix) )
              break;
            /* Otherwise, do variable subsitution and mark pivot row for deletion */
            candelete = TRUE;
            nc++;
            /*
             ... Add remaining logic later!
            */
          }
        }
        ix = i;
        i = prevActiveLink(rowmap, i);
        if(candelete) {
          presolve_rowupdate(lp, ix, collength, TRUE);
          removeLink(rowmap, ix);
          n++;
        }
      }
    }
#endif

   /* Eliminate empty rows, convert row singletons to bounds,
      tighten bounds, and remove always satisfied rows */
    if(is_presolve(lp, PRESOLVE_ROWS) && mat_validate(mat)) {
      if(userabort(lp, -1))
        goto Complete;
      n = 0;
      for(i = lastActiveLink(rowmap); i > 0; ) {
        candelete = FALSE;

	     /* First identify any full row infeasibilities */
	      if(!is_constr_type(lp, i, LE))
          if(MAX(sumplumin(lp, i, plulower,neglower),
                 sumplumin(lp, i, pluupper,negupper)) < get_rh(lp, i)-lp->infinite) {
             report(lp, NORMAL, "presolve: Found upper bound infeasibility in row %d\n", i);
            return(INFEASIBLE);
          }
        if(!is_constr_type(lp, i, GE))
          if(MIN(sumplumin(lp, i, plulower,neglower),
                 sumplumin(lp, i, pluupper,negupper)) > get_rh(lp, i)+lp->infinite) {
            report(lp, NORMAL, "presolve: Found lower bound infeasibility in row %d\n", i);
            return(INFEASIBLE);
          }

        j = plucount[i]+negcount[i];

       /* Delete non-zero rows and variables that are completely determined;
          note that this step can provoke infeasibility in some tight models */
        if((j > 0)                                       /* Only examine non-empty rows, */
           && (fabs(lp->orig_rh[i]) < epsvalue)          /* .. and the current RHS is zero, */
           && ((plucount[i] == 0) || (negcount[i] == 0)) /* .. and the parameter signs are all equal, */
           && (pluneg[i] == 0)                           /* .. and no (quasi) free variables, */
           && (is_constr_type(lp, i, EQ)
#if 1
               || (fabs(get_rh_lower(lp, i)-sumplumin(lp, i, pluupper,negupper)) < epsvalue)  /* Convert to equalities */
               || (fabs(get_rh_upper(lp, i)-sumplumin(lp, i, plulower,neglower)) < epsvalue)  /* Convert to equalities */
#endif
              )
              ) {
          /* Delete the columns of this row, but make sure we don't delete SOS variables */
          for(ix = mat->row_end[i]-1; ix >= mat->row_end[i-1]; ix--) {
            jx = ROW_MAT_COL(mat->row_mat[ix]);
            if(isActiveLink(colmap, jx) && !SOS_is_member(lp->SOS, 0, jx)) {
#if 0
              if(get_OF_raw(lp, lp->rows+jx) > 0)
                test = get_lowbo(lp, jx);
              else
                test = get_upbo(lp, jx);
              set_bounds(lp, jx, test, test);
#endif
              presolve_colupdate(lp, jx, pluneg, rowlength,
                                         plucount, negcount,
                                         pluupper, negupper,
                                         plulower, neglower, TRUE);
              removeLink(colmap, jx);
              nv++;
            }
          }
          /* Then delete the row, which is redundant */
          candelete = TRUE;
          nc++;
        }
        else

       /* Then delete any empty or always satisfied / redundant row that cannot at
          the same time guarantee that we can also delete associated variables */
        if((j == 0) ||                                   /* Always delete an empty row */
           ((j > 1) &&
            (pluneg[i] == 0) && ((plucount[i] == 0) ||
                                 (negcount[i] == 0)) &&  /* Consider removing if block above is ON! */
            (sumplumin(lp, i, pluupper,negupper)-sumplumin(lp, i, plulower,neglower) < epsvalue))      /* .. or if it is always satisfied (redundant) */
          ) {
          candelete = TRUE;
          nc++;
        }

       /* Convert row singletons to bounds (delete fixed columns in columns section);
          creates numeric instability in BOEING1.MPS */
        else if((j == 1) &&
                (sumplumin(lp, i, pluupper,negupper)-sumplumin(lp, i, plulower,neglower) >= epsvalue)) {
          j = presolve_nextcol(mat, i, 0, colmap);
          j = ROW_MAT_COL(mat->row_mat[j]);
          status = presolve_tighten(lp, i, j, colmap, plucount[i]+negcount[i],
                                                      pluupper, negupper,
                                                      plulower, neglower, &nt);
          if(status == INFEASIBLE) {
            nn = 0;
            break;
          }
          candelete = TRUE;
          nb++;
      }

       /* Check if we have a constraint made redundant through bounds on individual variables */
        else if((sumplumin(lp, i, plulower,neglower) >= get_rh_lower(lp, i)-epsvalue) &&
                (sumplumin(lp, i, pluupper,negupper) <= get_rh_upper(lp, i)+epsvalue)) {
          candelete = TRUE;
          nc++;
        }

#ifdef AggressiveRowPresolve
       /* Look for opportunity to tighten constraint bounds;
          know to create problems with scaled ADLittle.mps */
        else if(j > 1) {
          test = sumplumin(lp, i, plulower,neglower);
          if(test > get_rh_lower(lp, i)+epsvalue) {
            set_rh_lower(lp, i, presolve_round(lp, test, TRUE));
            nr++;
          }
          test = sumplumin(lp, i, pluupper,negupper);
          if(test < get_rh_upper(lp, i)-epsvalue) {
            set_rh_upper(lp, i, presolve_round(lp, test, FALSE));
            nr++;
          }
        }
#endif

        /* Get next row and do the deletion of the previous, if indicated */
        ix = i;
        i = prevActiveLink(rowmap, i);
        if(candelete) {
          presolve_rowupdate(lp, ix, collength, TRUE);
          removeLink(rowmap, ix);
          n++;
          nn++;
        }
        /* Look for opportunity to convert ranged constraint to equality-type */
        else if(!is_constr_type(lp, ix, EQ) && (get_rh_range(lp, ix) < epsvalue))
          lp_solve_set_constr_type(lp, ix, EQ);
      }
    }

    /* Check if we can tighten bounds on individual variables;
       note that this can create degeneracy in some models */
    if(FALSE) {
      REAL rhup, rhlo, varup, varlo;
      for(j = firstActiveLink(colmap); j > 0; j = nextActiveLink(colmap, j)) {
        varup  = get_upbo(lp, j);
        varlo = get_lowbo(lp, j);
        for(ix = mat->col_end[j-1]; ix < mat->col_end[j]; ix++) {
          iix = mat->col_mat[ix].row_nr;
          if((iix == 0) || !isActiveLink(rowmap, iix) ||
             (pluneg[iix] != 0) ||
             (plucount[iix]*negcount[iix] > 0))
            continue;
          
          /* Retrieve coefficients and standardize to positive */
          value = my_chsign(is_chsign(lp, iix), get_mat_byindex(lp, ix, FALSE));
          rhup = get_rh_upper(lp, iix);
          rhlo = get_rh_lower(lp, iix);
          if(negcount[iix] > 0) {
            value = -value;
            rhup = -rhup;
            rhlo = -rhlo;
            swapREAL(&rhup, &rhlo);
          }
          
          if(!isInf(lp,rhup) && (isInf(lp,varup) || (value*varup > rhup+epsvalue))) {
            varup = rhup / value;
            lp_solve_set_upbo(lp, j, varup);
            nt++;
            nn++;
          }
/*          if(!isInf(lp,rhlo) && (isInf(lp,varlo) || (value*varlo < rhlo-epsvalue))) {
            varlo = rhlo / value;
            lp_solve_set_lowbo(lp, j, varlo);
            nt++;
            nn++;
          }*/
          if(varup-varlo < 0) {
            status = INFEASIBLE;
            break;
          }
        }
      }
    }
   
   
	 /* Try again if we were successful in this presolve loop */
    if((status == RUNNING) && !userabort(lp, -1)) {
      if(nn > 0) goto Redo;

      /* Optionally do an extra loop from scratch */
      if((nm < 1) && (nc+nb+nt+nv+nr > 0)) {
        MEMCLEAR(plucount, lp->rows+1);
	      MEMCLEAR(negcount, lp->rows+1);
	      MEMCLEAR(pluneg  , lp->rows+1);
	      MEMCLEAR(pluupper, lp->rows+1);
        MEMCLEAR(negupper, lp->rows+1);
	      MEMCLEAR(plulower, lp->rows+1);
        MEMCLEAR(neglower, lp->rows+1);
        goto Restart;
      }
    }

Complete:
   /* See if we can convert some constraints to SOSes (only SOS1 handled) */
    if(is_presolve(lp, PRESOLVE_SOS) &&
       (MIP_count(lp) > 0) && mat_validate(mat)) {
      n = 0;
      for(i = lastActiveLink(rowmap); i > 0; ) {
        candelete = FALSE;
        test = get_rh(lp, i);
        jx = get_constr_type(lp, i);
#ifdef EnableBranchingOnGUB
        if((test == 1) && (jx != GE)) {
#else
        if((test == 1) && (jx == LE)) {
#endif
          jjx = mat->row_end[i-1];
          iix = mat->row_end[i];
          for(; jjx < iix; jjx++) {
            j = ROW_MAT_COL(mat->row_mat[jjx]);
            if(!isActiveLink(colmap, j))
              continue;
            if(!is_binary(lp, j) || (get_mat(lp, i, j) != 1))
              break;
          }
          if(jjx >= iix) {
            char SOSname[16];

            /* Define a new SOS instance */
            sprintf(SOSname, "SOS_%d", SOS_count(lp) + 1);
            ix = add_SOS(lp, SOSname, 1, 1, 0, NULL, NULL);
            if(jx == EQ)
              SOS_set_GUB(lp->SOS, ix, TRUE);
            value = 0;
            jjx = mat->row_end[i-1];
            for(; jjx < iix; jjx++) {
              j = ROW_MAT_COL(mat->row_mat[jjx]);
              if(!isActiveLink(colmap, j))
                continue;
              value += 1;
              append_SOSrec(lp->SOS->sos_list[ix-1], 1, &j, &value);
            }
            candelete = TRUE;
            nc++;
          }
        }

        /* Get next row and do the deletion of the previous, if indicated */
        ix = i;
        i = prevActiveLink(rowmap, i);
        if(candelete) {
          presolve_rowupdate(lp, ix, collength, TRUE);
          removeLink(rowmap, ix);
          n++;
          nn++;
        }
      }
      if(n)
        report(lp, NORMAL, "presolve: Converted %5d constraints to SOS1.\n", n);
    }

   /* Finalize presolve */
#ifdef Paranoia
    i = presolve_validate(lp, rowmap, colmap);
    if(i > 0)
      report(lp, SEVERE, "presolve: %d internal consistency failure(s) detected\n", i);
#endif
    presolve_finalize(lp, rowmap, colmap);

   /* Tighten MIP bound if possible (should ideally use some kind of smart heuristic) */
#ifndef PostScale
    if((MIP_count(lp) > 0) || (get_Lrows(lp) > 0)) {
      if(is_maxim(lp))
        lp->bb_heuristicOF = MAX(lp->bb_heuristicOF, sumplumin(lp, 0, plulower, neglower));
      else
        lp->bb_heuristicOF = MIN(lp->bb_heuristicOF, sumplumin(lp, 0, pluupper, negupper));
    }
#endif

   /* Report summary information */
	  if(nv)
      report(lp, NORMAL, "presolve: Removed   %5d empty or fixed variables.\n", nv);
	  if(nb)
      report(lp, NORMAL, "presolve: Converted %5d row singletons to variable bounds.\n", nb);
	  if(nt)
      report(lp, NORMAL, "presolve: Tightened %5d other variable bounds.\n", nt);
	  if(nc)
      report(lp, NORMAL, "presolve: Removed   %5d empty or redundant constraints.\n", nc);
	  if(nr)
      report(lp, NORMAL, "presolve: Tightened %5d constraint bounds.\n", nr);

    if(nc+nb+nt+nv+nr > 0)
      report(lp, NORMAL, " \n");

    /* Report optimality or infeasibility */
    if(sumplumin(lp, 0, pluupper,negupper)-sumplumin(lp, 0, plulower,neglower) < epsvalue) {
      report(lp, NORMAL, "presolve: Identified optimal OF value %g\n",
                         sumplumin(lp, 0, pluupper,negupper));
#ifndef PostScale
      lp->bb_limitOF = sumplumin(lp, 0, pluupper,negupper);
#endif
#if 0
      status = OPTIMAL;
#endif
    }
    else if(status != RUNNING)
      report(lp, NORMAL, "presolve: Infeasibility or unboundedness detected.\n");

    /* Clean up */
    FREE(rowlength);
	  FREE(collength);
    FREE(plucount);
	  FREE(negcount);
	  FREE(pluneg);
	  FREE(plulower);
    FREE(neglower);
	  FREE(pluupper);
    FREE(negupper);

  }

  /* Signal that we are done presolving */
  if((lp->usermessage != NULL) && 
     ((lp->do_presolve & PRESOLVE_LASTMASKMODE) != 0) && (lp->msgmask & MSG_PRESOLVE))
     lp->usermessage(lp, lp->msghandle, MSG_PRESOLVE);
     
  /* Clean out empty SOS records */
  if(SOS_count(lp) > 0) {
    clean_SOSgroup(lp->SOS);
    if(lp->SOS->sos_count == 0)
      free_SOSgroup(&(lp->SOS));
  }
  
  /* Create master SOS variable list */
  if(SOS_count(lp) > 0)
    make_SOSchain(lp, (MYBOOL) ((lp->do_presolve & PRESOLVE_LASTMASKMODE) != PRESOLVE_NONE));

  /* Crash the basis, if specified */
  crash_basis(lp);

#ifdef PostScale
  if(status == RUNNING)
    lp_solve_auto_scale(lp);
#endif

Finish:
  lp->wasPresolved = TRUE;

#if 0
  write_lp(lp, "test_out.lp");   /* Must put here due to variable name mapping */
#endif
#if 0
  REPORT_debugdump(lp, "testint2.txt", FALSE);
#endif

  return( status );

}

