
/*
    Core optimization drivers for lp_solve v5.0+
   ----------------------------------------------------------------------------------
    Author:        Michel Berkelaar (to lp_solve v3.2),
                   Kjell Eikland
    Contact:
    License terms: LGPL.

    Requires:      lp_lib.h, lp_simplex.h, lp_presolve.h, lp_pricerPSE.h

    Release notes:
    v5.0.0  1 January 2004      New unit applying stacked basis and bounds storage.
    v5.0.1 31 January 2004      Moved B&B routines to separate file and implemented
                                a new runsolver() general purpose call method.
    v5.0.2  1 May 2004          Changed routine names to be more intuitive.

   ----------------------------------------------------------------------------------
*/

#include <string.h>
#include "commonlib.h"
#include "lp_lib.h"
#include "lp_simplex.h"
#include "lp_crash.h"
#include "lp_presolve.h"
#include "lp_price.h"
#include "lp_pricePSE.h"

#ifdef FORTIFY
# include "lp_fortify.h"
#endif


STATIC int primloop(lprec *lp, MYBOOL feasible)
{
  int	   i, j, k, ok = TRUE;
  LREAL  theta = 0.0;
  REAL   *drow = NULL, *prow = NULL, *pcol = NULL, prevobj, epsvalue;
  MYBOOL primal = TRUE, minit;
  MYBOOL pivdynamic;
  int    oldpivrule, oldpivmode, pivrule, Blandswitches,
         colnr, rownr, lastnr, minitcount = 0;
  int    Rcycle = 0, Ccycle = 0, Ncycle = 0, changedphase = FALSE;
  int    *nzdrow = NULL;

 /* Add sufficent number of artificial variables to make the problem feasible
    through the first phase; delete when primal feasibility has been achieved */
  lp->Extrap = 0;
#ifdef EnablePrimalPhase1
  if(!feasible) {
#ifdef Paranoia
    if(!verifyBasis(lp))
      report(lp, SEVERE, "primloop: No valid basis for artificial variables\n");
#endif
#if 0
    /* First check if we can get away with a single artificial variable */
    if(lp->equalities == 0) {
      i = (int) feasibilityOffset(lp, !primal);
      add_artificial(lp, i);
    }
    else
#endif
    /* Otherwise add as many as is necessary to force basic feasibility */
      for(i = 1; i <= lp->rows; i++)
        add_artificial(lp, i);
  }
  if(lp->spx_trace)
    report(lp, DETAILED, "Extrap count = %d\n", lp->Extrap);
#endif

  if(lp->spx_trace)
    report(lp, DETAILED, "Entered primal simplex algorithm with feasibility %s\n",
                         my_boolstr(feasible));

 /* Create work arrays */
#ifdef UseSparseReducedCost
  allocINT(lp, &nzdrow, lp->sum + 1, FALSE);
#endif
  allocREAL(lp, &drow, lp->sum + 1, TRUE);
  allocREAL(lp, &prow, lp->sum + 1, TRUE);
  allocREAL(lp, &pcol, lp->rows + 1, TRUE);

 /* Refactorize the basis and set status variables */
  i = my_if(is_bb_action(lp, ACTION_REBASE), INITSOL_SHIFTZERO, INITSOL_USEZERO);
  if((lp->spx_status == SWITCH_TO_PRIMAL) || (lp->Extrap != 0) ||
     is_bb_action(lp, ACTION_REINVERT)) {
    simplexPricer(lp, (MYBOOL)!primal);

    /* Do basis crashing before refactorization, if specified */
    invert(lp, (MYBOOL) i);
  }
  else {
    if(is_bb_action(lp, ACTION_RECOMPUTE))
      recompute_solution(lp, (MYBOOL) i);
    restartPricer(lp, (MYBOOL)!primal);
  }
  lp->bb_action = ACTION_NONE;

  lp->spx_status = RUNNING;
  lp->doIterate = FALSE;
  minit = ITERATE_MAJORMAJOR;
  prevobj = lp->rhs[0];
  oldpivmode = lp->piv_strategy;
  oldpivrule = get_piv_rule(lp);
  pivdynamic = ANTICYCLEBLAND && is_piv_mode(lp, PRICE_ADAPTIVE);
  epsvalue = lp->epspivot;
  Blandswitches = 0;
  rownr = 0;
  colnr = 0;
  lastnr = 0;
  lp->rejectpivot[0] = 0;
  if(feasible)
    lp->simplex_mode = SIMPLEX_Phase2_PRIMAL;
  else
    lp->simplex_mode = SIMPLEX_Phase1_PRIMAL;

 /* Iterate while we are successful; exit when the model is infeasible/unbounded,
    or we must terminate due to numeric instability or user-determined reasons */
  while(lp->spx_status == RUNNING) {

    if(lp->spx_trace)
      if(lastnr > 0)
      report(lp, NORMAL, "primloop: Objective at iteration %7d is " MPSVALUEMASK " (%4d: %4d %s- %4d)\n",
                         get_total_iter(lp), lp->rhs[0], rownr, lastnr,
                         my_if(minit == ITERATE_MAJORMAJOR, "<","|"), colnr);

    pivrule = get_piv_rule(lp);
	  if(pivdynamic && ((pivrule != PRICER_FIRSTINDEX) ||
                      (pivrule != oldpivrule))
#if PrimalPivotStickiness==2
       && (lp->fixedvars == 0)
#elif PrimalPivotStickiness==1
       && feasible
#endif
      ) {
      /* Check if we have a stationary solution */
      if((minit == ITERATE_MAJORMAJOR) && !lp->justInverted &&
         (fabs(my_reldiff(lp->rhs[0], prevobj)) < epsvalue)) {
        Ncycle++;
        /* Start to monitor for variable cycling if this is the initial stationarity */
        if(Ncycle <= 1) {
          Ccycle = colnr;
          Rcycle = rownr;
        }
        /* Check if we should change pivoting strategy due to stationary variable cycling */
		    else if((pivrule == oldpivrule) && (((MAX_STALLCOUNT > 1) && (Ncycle > MAX_STALLCOUNT)) ||
                                            (Ccycle == rownr) || (Rcycle == colnr))) {
          /* First check if we should give up on Bland's rule and try perturbed bound
             relaxation instead */
#ifdef EnableStallAntiDegen
          if((MAX_BLANDSWITCH >= 0) && (Blandswitches >= MAX_BLANDSWITCH)) {
            lp->spx_status = DEGENERATE;
            break;
          }
#endif
          Blandswitches++;
          lp->piv_strategy = PRICER_FIRSTINDEX;  /* There is no advanced normalization for Bland's rule, restart at end */
          Ccycle = 0;
          Rcycle = 0;
          Ncycle = 0;
          if(lp->spx_trace)
            report(lp, DETAILED, "primloop: Detected cycling at iteration %d; changed to FIRST INDEX rule!\n",
	  	                           get_total_iter(lp));
		    }
      }
#if 0
      /* Handle cycling or stationary situations by switching to the dual simplex */
      else if((pivrule == oldpivrule) && feasible && (lp->simplex_strategy & SIMPLEX_DYNAMIC)) {
        lp->spx_status = SWITCH_TO_DUAL;
        if(lp->total_iter == 0)
          report(lp, NORMAL, "Start dual simplex for finalization at iteration  %7d\n",
                             get_total_iter(lp));
        break;
      }
#endif
      /* Change back to original selection strategy as soon as possible */
      else if((minit == ITERATE_MAJORMAJOR) && (pivrule != oldpivrule)) {
        lp->piv_strategy = oldpivmode;
        restartPricer(lp, AUTOMATIC);    /* Pricer restart following Bland's rule */
        Ccycle = 0;
        Rcycle = 0;
        Ncycle = 0;
        if(lp->spx_trace)
          report(lp, DETAILED, "...returned to original pivot selection rule at iteration %d.\n",
	                             get_total_iter(lp));
      }
    }

   /* Store current LP value for reference at next iteration */
    prevobj = lp->rhs[0];

    lp->doIterate = FALSE;
    lp->doInvert = FALSE;

   /* Find best column to enter the basis */
RetryCol:
    if(!changedphase) {
      i = 0;
      do {
        if(partial_countBlocks(lp, (MYBOOL)!primal) > 1)
          partial_blockStep(lp, (MYBOOL)!primal);
        colnr = colprim(lp, (MYBOOL) (minit == ITERATE_MINORRETRY), drow, nzdrow);
        i++;
      } while ((colnr == 0) && (i < partial_countBlocks(lp, (MYBOOL)!primal)));

#ifdef FinalOptimalErrorLimitation
      /* Do additional checking that we have a correct identification of optimality */
      if((colnr == 0) && !lp->justInverted) {
        lp->doInvert = TRUE;
        i = invert(lp, INITSOL_USEZERO);
        colnr = colprim(lp, FALSE, drow, nzdrow, NULL);
      }
#endif
    }

    if(colnr > 0) {
      changedphase = FALSE;
      fsolve(lp, colnr, pcol, lp->workrowsINT, lp->epsmachine, 1.0, TRUE);   /* Solve entering column for Pi */
#ifdef UseRejectionList
      if(is_anti_degen(lp, ANTIDEGEN_COLUMNCHECK) && !check_degeneracy(lp, pcol, NULL)) {
        if(lp->rejectpivot[0] < DEF_MAXPIVOTRETRY/3) {
          i = ++lp->rejectpivot[0];
          lp->rejectpivot[i] = colnr;
          report(lp, DETAILED, "Entering column %d found to be non-improving due to degeneracy!\n",
                     colnr);
          goto RetryCol;
        }
        else {
          lp->rejectpivot[0] = 0;
          report(lp, DETAILED, "Gave up trying to find a strictly improving entering column!\n");
        }
      }
#endif

      /* Find the leaving variable that gives the most stringent bound on the entering variable */
      rownr = rowprim(lp, colnr, &theta, pcol);
#if 0
      report(lp, NORMAL, "Iteration %d: Enter %d, Leave %d\n", lp->current_iter, colnr, rownr);
#endif

      /* See if we can do a straight artificial<->slack replacement (when "colnr" is a slack) */
      if((lp->Extrap != 0) && (rownr == 0) && (colnr <= lp->rows))
        rownr = findAnti_artificial(lp, colnr);

      if(rownr > 0) {
        lp->rejectpivot[0] = 0;
        lp->bfp_prepareupdate(lp, rownr, colnr, pcol);
      }
      else if(lp->spx_status == UNBOUNDED) {
        report(lp, DETAILED, "primloop: The model is primal unbounded.\n");
        break;
      }
#ifdef UseRejectionList
      else if(lp->rejectpivot[0] < DEF_MAXPIVOTRETRY) {
        lp->spx_status = RUNNING;
        if(lp->justInverted) {
          lp->rejectpivot[0]++;
          lp->rejectpivot[lp->rejectpivot[0]] = colnr;
          report(lp, DETAILED, "...trying to recover via another pivot column!\n");
        }
        else {
          lp->doInvert = TRUE;
          invert(lp, INITSOL_USEZERO);
        }
        goto RetryCol;
      }
#endif
      else {

        /* Assume failure if we are still unsuccessful and the model is not unbounded */
        if((rownr == 0) && (lp->spx_status == RUNNING)) {
          report(lp, IMPORTANT, "primloop: Could not find a leaving variable for entering %d (iteration %d)\n",
                                 colnr, get_total_iter(lp));
          lp->spx_status = NUMFAILURE;
        }
      }
    }
#ifdef EnablePrimalPhase1
    else if(!feasible || isPhase1(lp)) {

      if(feasiblePhase1(lp, epsvalue)) {
        lp->spx_status = RUNNING;
        if(lp->bb_totalnodes == 0) {
          report(lp, NORMAL, "Found feasibility by primal simplex at iteration  %7d\n",
                              get_total_iter(lp));
          if((lp->usermessage != NULL) && (lp->msgmask & MSG_LPFEASIBLE))
            lp->usermessage(lp, lp->msghandle, MSG_LPFEASIBLE);
        }
        changedphase = FALSE;
        feasible = TRUE;
        lp->simplex_mode = SIMPLEX_Phase2_PRIMAL;

       /* We can do two things now;
          1) delete the rows belonging to those variables, since they are redundant, OR
          2) drive out the existing artificial variables via pivoting. */
        if(lp->Extrap > 0) {

#ifdef Phase1EliminateRedundant
         /* If it is not a MIP model we can try to delete redundant rows */
          if((lp->bb_totalnodes == 0) && (MIP_count(lp) == 0)) {
            while(lp->Extrap > 0) {
              i = lp->rows;
              while((i > 0) && (lp->var_basic[i] <= lp->sum-lp->Extrap))
                i--;
#ifdef Paranoia
              if(i <= 0) {
                report(lp, SEVERE, "primloop: Could not find redundant artificial.\n");
                break;
              }
#endif
              /* Obtain column and row indeces */
              j = lp->var_basic[i]-lp->rows;
              k = get_artificialRow(lp, j);

              /* Delete row before column due to basis "compensation logic" */
              if(lp->is_basic[k]) {
                lp->is_basic[lp->rows+j] = FALSE;
                del_constraint(lp, k);
              }
              else
                setBasisVar(lp, i, k);
              del_column(lp, j);
              lp->Extrap--;
            }
            lp->basis_valid = TRUE;
          }
         /* Otherwise we drive out the artificials by elimination pivoting */
          else {
            eliminate_artificials(lp, prow);
            lp->doIterate = FALSE;
          }
#else
          lp->Extrap = my_flipsign(lp->Extrap);
#endif
        }
        lp->doInvert = TRUE;
        prevobj = lp->infinite;
      }
      else {
        lp->spx_status = INFEASIBLE;
        minit = ITERATE_MAJORMAJOR;
        if(lp->spx_trace)
          report(lp, NORMAL, "Found infeasibility in primal simplex at iteration %7d\n",
                             get_total_iter(lp));
      }
    }
#endif

    /* Pivot row/col and update the inverse */
    if(lp->doIterate) {
      lastnr = lp->var_basic[rownr];

      if(lp->justInverted)
        minitcount = 0;
      else if(minitcount > MAX_MINITUPDATES)
        recompute_solution(lp, INITSOL_USEZERO);
      minit = performiteration(lp, rownr, colnr, theta, primal, NULL, NULL);
      if(minit == ITERATE_MINORRETRY)
        minitcount++;

      if((lp->spx_status == USERABORT) || (lp->spx_status == TIMEOUT))
        break;
#ifdef UsePrimalReducedCostUpdate
      /* Do a fast update of the reduced costs in preparation for the next iteration */
      if(minit == ITERATE_MAJORMAJOR)
        update_reducedcosts(lp, primal, lastnr, colnr, pcol, drow);
#endif

#ifdef EnablePrimalPhase1
      /* Detect if an auxiliary variable has left the basis and delete it; if
         the non-basic variable only changed bound (a "minor iteration"), the
         basic artificial variable did not leave and there is nothing to do */
      if((minit == ITERATE_MAJORMAJOR) && (lastnr > lp->sum - abs(lp->Extrap))) {
#ifdef Paranoia
        if(lp->is_basic[lastnr] || !lp->is_basic[colnr])
          report(lp, SEVERE, "primloop: Invalid basis indicator for variable %d at iteration %d\n",
                              lastnr, get_total_iter(lp));
#endif
        del_column(lp, lastnr-lp->rows);
        if(lp->Extrap > 0)
          lp->Extrap--;
        else
          lp->Extrap++;
        if(lp->Extrap == 0) {
          colnr = 0;
          prevobj = lp->infinite;
          changedphase = TRUE;
        }
      }

#endif
    }

    if(lp->spx_status == SWITCH_TO_DUAL)
      ;
    else if(!changedphase && lp->bfp_mustrefactorize(lp)) {
      i = invert(lp, INITSOL_USEZERO);
#ifdef ResetMinitOnReinvert
      minit = ITERATE_MAJORMAJOR;
#endif

      if((lp->spx_status == USERABORT) || (lp->spx_status == TIMEOUT))
        break;
      else if(!i) {
        lp->spx_status = SINGULAR_BASIS;
        break;
      }
      /* Check whether we are still feasible or not... */
      if(!isPrimalFeasible(lp, lp->epspivot)) {
        lp->spx_status = LOSTFEAS;
      }
    }
    userabort(lp, -1);

  }
  if (lp->piv_strategy != oldpivmode)
    lp->piv_strategy = oldpivmode;

#ifdef EnablePrimalPhase1

  /* Remove any remaining artificial variables (feasible or infeasible model) */
  lp->Extrap = abs(lp->Extrap);
  if(lp->Extrap > 0) {
    clear_artificials(lp);
    if(lp->spx_status != OPTIMAL)
      restore_basis(lp);
    i = invert(lp, INITSOL_USEZERO);
  }
#ifdef Paranoia
  if(!verifyBasis(lp))
    report(lp, SEVERE, "primloop: Invalid basis detected due to internal error\n");
#endif

  /* Switch to dual phase 1 simplex for MIP models during B&B phases */
  if((lp->bb_totalnodes == 0) && (MIP_count(lp) > 0) &&
     ((lp->simplex_strategy & SIMPLEX_Phase1_DUAL) == 0)) {
    lp->simplex_strategy &= !SIMPLEX_Phase1_PRIMAL;
    lp->simplex_strategy += SIMPLEX_Phase1_DUAL;
  }

#endif

#ifdef UseSparseReducedCost
  FREE(nzdrow);
#endif
  FREE(drow);
  FREE(prow);
  FREE(pcol);

  return(ok);
} /* primloop */

STATIC int dualloop(lprec *lp, MYBOOL feasible)
{
  int    i, ok = TRUE;
  LREAL  theta = 0.0;
  REAL   *drow = NULL, *prow = NULL, *pcol = NULL, prevobj, epsvalue;
  MYBOOL primal = FALSE;
  MYBOOL minit, pivdynamic, forceoutEQ = FALSE;
  int    oldpivrule, oldpivmode, pivrule, Blandswitches,
         colnr, rownr, lastnr, minitcount = 0;
  int    Rcycle = 0, Ccycle = 0, Ncycle = 0, changedphase = TRUE;
#ifdef FixInaccurateDualMinit
  int    minitcolnr = 0;
#endif
#ifdef UseSparseReducedCost
  int    *nzprow = NULL;
#endif

  if(lp->spx_trace)
    report(lp, DETAILED, "Entering dual simplex algorithm\n");

 /* Set Extrad value to force dual feasibility; reset when
    "local optimality" has been achieved or a dual non-feasibility
    has been encountered (no candidate for a first leaving variable) */
  if(feasible)
    lp->Extrad = 0;
  else
    lp->Extrad = feasibilityOffset(lp, (MYBOOL)!primal);

  if(lp->spx_trace)
    report(lp, DETAILED, "Extrad = %g\n", (double)lp->Extrad);

 /* Allocate work arrays */
  allocREAL(lp, &drow, lp->sum + 1, TRUE);
#ifdef UseSparseReducedCost
  allocINT(lp, &nzprow, lp->sum + 1, FALSE);
#endif
  allocREAL(lp, &prow, lp->sum + 1, TRUE);
  allocREAL(lp, &pcol, lp->rows + 1, TRUE);

 /* Refactorize the basis and set status variables */
  i = my_if(is_bb_action(lp, ACTION_REBASE), INITSOL_SHIFTZERO, INITSOL_USEZERO);
  if((lp->spx_status == SWITCH_TO_DUAL) || (lp->Extrad != 0) ||
     is_bb_action(lp, ACTION_REINVERT)) {
    simplexPricer(lp, (MYBOOL)!primal);

    /* Do basis crashing before refactorization, if specified */
    invert(lp, (MYBOOL) i);
  }
  else {
    if(is_bb_action(lp, ACTION_RECOMPUTE))
      recompute_solution(lp, (MYBOOL) i);
    restartPricer(lp, (MYBOOL)!primal);
  }
  lp->bb_action = ACTION_NONE;

  lp->spx_status = RUNNING;
  lp->doIterate = FALSE;
  minit = ITERATE_MAJORMAJOR;
  prevobj = lp->rhs[0];
  oldpivmode = lp->piv_strategy;
  oldpivrule = get_piv_rule(lp);
  pivdynamic = ANTICYCLEBLAND && is_piv_mode(lp, PRICE_ADAPTIVE);
  epsvalue = lp->epspivot;
  Blandswitches = 0;
  rownr = 0;
  colnr = -1;  /* Used to detect infeasibility at the beginning of the dual loop */
  lastnr = 0;
  lp->rejectpivot[0] = 0;
  if(feasible)
    lp->simplex_mode = SIMPLEX_Phase2_DUAL;
  else
    lp->simplex_mode = SIMPLEX_Phase1_DUAL;

  /* Check if we have equality slacks in the basis and we should try to
     drive them out in order to reduce chance of degeneracy in Phase 1 */
  if(!feasible && (lp->fixedvars > 0) && is_anti_degen(lp, ANTIDEGEN_FIXEDVARS))
    forceoutEQ = TRUE;

  while(lp->spx_status == RUNNING) {

    if(lp->spx_trace)
      if(lastnr > 0)
      report(lp, NORMAL, "dualloop: Objective at iteration %7d is " MPSVALUEMASK " (%4d: %4d %s- %4d)\n",
                         get_total_iter(lp), lp->rhs[0], rownr, lastnr,
                         my_if(minit == ITERATE_MAJORMAJOR, "<","|"), colnr);

    pivrule = get_piv_rule(lp);
	  if(pivdynamic && ((pivrule != PRICER_FIRSTINDEX) ||
                      (pivrule != oldpivrule))
/* lp_solve has problems with ffff8000.mps if one of these is not activated; problem source unknown */
#if DualPivotStickiness==2
      /* Stays with pricing rule as long as possible (also preserves accuracy) */
       && (lp->fixedvars == 0)
#elif DualPivotStickiness==1
      /* Stays with pricing rule only if the model is infeasible */
       && feasible
#endif
      ) {
      /* Check if we have a stationary solution */
      if((minit == ITERATE_MAJORMAJOR) && !lp->justInverted &&
         (fabs(my_reldiff(lp->rhs[0], prevobj)) < epsvalue)) {
        Ncycle++;
        /* Start to monitor for variable cycling if this is the initial stationarity */
        if(Ncycle <= 1) {
          Ccycle = colnr;
          Rcycle = rownr;
        }
        /* Check if we should change pivoting strategy due to stationary variable cycling */
		    else if((pivrule == oldpivrule) && (((MAX_STALLCOUNT > 1) && (Ncycle > MAX_STALLCOUNT)) ||
                                            (Ccycle == rownr) || (Rcycle == colnr))) {
          /* First check if we should give up on Bland's rule and try perturbed bound
             relaxation instead */
#ifdef EnableStallAntiDegen
          if((MAX_BLANDSWITCH >= 0) && (Blandswitches >= MAX_BLANDSWITCH)) {
            lp->spx_status = DEGENERATE;
            break;
          }
#endif
          Blandswitches++;
          lp->piv_strategy = PRICER_FIRSTINDEX;  /* There is no advanced normalization for Bland's rule, restart at end */
          Ccycle = 0;
          Rcycle = 0;
          Ncycle = 0;
          if(lp->spx_trace)
            report(lp, DETAILED, "dualloop: Detected cycling at iteration %d; changed to FIRST INDEX rule!\n",
			                           get_total_iter(lp));
		    }
      }
      /* Handle cycling or stationary situations by switching to the primal simplex */
      else if((pivrule == oldpivrule) && feasible && (lp->simplex_strategy & SIMPLEX_DYNAMIC)) {
        lp->spx_status = SWITCH_TO_PRIMAL;
        break;
      }
      /* Change back to original selection strategy as soon as possible */
      else if((minit == ITERATE_MAJORMAJOR) && (pivrule != oldpivrule)) {
        lp->piv_strategy = oldpivmode;
        restartPricer(lp, AUTOMATIC);    /* Pricer restart following Bland's rule */
        Ccycle = 0;
        Rcycle = 0;
        Ncycle = 0;
        if(lp->spx_trace)
          report(lp, DETAILED, "...returned to original pivot selection rule at iteration %d.\n",
	                             get_total_iter(lp));
      }
    }

    /* Store current LP value for reference at next iteration */
    changedphase = FALSE;
    prevobj = lp->rhs[0];
    lastnr = lp->var_basic[rownr];
    lp->doInvert = FALSE;

    /* Do minor iterations (non-basic variable bound switches) for as
       long as possible since this is a cheap way of iterating */
#ifdef Phase1DualPriceEqualities
RetryRow:
#endif
    if(minit != ITERATE_MINORRETRY) {
      /* forceoutEQ
           FALSE : Only eliminate assured "good" violated equality constraint slacks
           TRUE  : Seek more aggressive elimination of equality constraint slacks
                   (but not as aggressive as the rule used in lp_solve v4.0 and earlier) */
      i = 0;
      do {
        if(partial_countBlocks(lp, (MYBOOL)!primal) > 1)
          partial_blockStep(lp, (MYBOOL)!primal);
        rownr = rowdual(lp, forceoutEQ);
        i++;
      } while ((rownr == 0) && (i < partial_countBlocks(lp, (MYBOOL)!primal)));

#ifdef FinalOptimalErrorLimitation
      /* Do additional checking that we have a correct identification of optimality */
      if((rownr == 0) && !lp->justInverted) {
        lp->doInvert = TRUE;
        i = invert(lp, INITSOL_USEZERO);
        forceoutEQ = TRUE;
        rownr = rowdual(lp, forceoutEQ);
      }
#endif
      lastnr = lp->var_basic[rownr];
    }

    if(rownr > 0) {
#ifdef UseRejectionList
RetryCol:
#endif
      lp->doIterate = FALSE;
      colnr = coldual(lp, rownr, (MYBOOL)(minit == ITERATE_MINORRETRY),
                          prow, nzprow, drow, NULL);
      if(colnr > 0) {
        lp->doIterate = TRUE;
        fsolve(lp, colnr, pcol, lp->workrowsINT, lp->epsmachine, 1.0, TRUE);

#ifdef FixInaccurateDualMinit
       /* Prevent bound flip-flops during minor iterations; used to detect
          infeasibility after triggering of minor iteration accuracy management */
        if(colnr != minitcolnr)
          minitcolnr = 0;
#endif

       /* Getting division by zero here; catch it and try to recover */
        if(pcol[rownr] == 0) {
          if(lp->spx_trace)
            report(lp, DETAILED, "dualloop: Attempt to divide by zero (pcol[%d])\n", rownr);
          lp->doIterate = FALSE;
          if(!lp->justInverted) {
            report(lp, DETAILED, "...trying to recover by reinverting!\n");
            lp->doInvert = TRUE;
          }
#ifdef UseRejectionList
          else if(lp->rejectpivot[0] < DEF_MAXPIVOTRETRY) {
            lp->rejectpivot[0]++;
            lp->rejectpivot[lp->rejectpivot[0]] = colnr;
            if(lp->bb_totalnodes == 0)
              report(lp, DETAILED, "...trying to recover via another pivot column!\n");
            goto RetryCol;
          }
#endif
          else {
            if(lp->bb_totalnodes == 0)
              report(lp, DETAILED, "...cannot recover by reinverting.\n");
            lp->spx_status = NUMFAILURE;
            ok = FALSE;
          }
        }
        else {
          lp->rejectpivot[0] = 0;
          theta = lp->bfp_prepareupdate(lp, rownr, colnr, pcol);


         /* Verify numeric accuracy of the inverse and change to
            the "theoretically" correct version of the theta */
          if((lp->improve & IMPROVE_INVERSE) &&
             (my_reldiff(fabs(theta),fabs(prow[colnr])) > lp->epspivot)) {
            lp->doInvert = TRUE;
#ifdef IncreasePivotOnReducedAccuracy
            if(lp->bfp_pivotcount(lp) < 2*DEF_MAXPIVOTRETRY)
              lp->epspivot *= 3.0;
#endif
            report(lp, DETAILED, "dualloop: Reinvert because of loss of accuracy at iteration %d\n",
                                 get_total_iter(lp));
          }
          theta = my_chsign(!lp->is_lower[colnr], prow[colnr]);

          compute_theta(lp, rownr, &theta, 0, primal);
        }
      }
#ifdef FixInaccurateDualMinit
      /* Reinvert and try another row if we did not find a bound-violated leaving column */
      else if((minit != ITERATE_MAJORMAJOR) && (colnr != minitcolnr)) {
        minitcolnr = colnr;
        lp->doInvert = TRUE;
        i = invert(lp, INITSOL_USEZERO);
        if((lp->spx_status == USERABORT) || (lp->spx_status == TIMEOUT))
          break;
        else if(!i) {
          lp->spx_status = SINGULAR_BASIS;
          break;
        }
        minit = ITERATE_MAJORMAJOR;
        continue;
      }
#endif
      else {
        if(lp->justInverted && (lp->simplex_mode == SIMPLEX_Phase2_DUAL))
          lp->spx_status = LOSTFEAS;
#if 1
        else if(!lp->justInverted && (lp->bb_level <= 1)) {
#else
        else if(!lp->justInverted) {
#endif
          lp->doIterate = FALSE;
          lp->doInvert = TRUE;
        }
        else {
          if((lp->spx_trace && (lp->bb_totalnodes == 0)) ||
             (lp->bb_trace && (lp->bb_totalnodes > 0)))
            report(lp, DETAILED, "Model lost dual feasibility\n");
          lp->spx_status = INFEASIBLE;
          ok = FALSE;
          break;
        }
      }
    }
    else {

      /* New code to solve to optimality using the dual, but only if the user
         has specified a preference for the dual simplex - KE added 20030731 */
      lp->doIterate = FALSE;
      if((lp->Extrad != 0) && (colnr < 0) && !isPrimalFeasible(lp, lp->epsprimal)) {
        if(feasible) {
          if(lp->bb_totalnodes == 0)
            report(lp, DETAILED, "Model is dual infeasible and primal feasible\n");
          lp->spx_status = SWITCH_TO_PRIMAL;
          lp->doInvert = (MYBOOL) (lp->Extrad != 0);
          lp->Extrad = 0;
        }
        else {
          if(lp->bb_totalnodes == 0)
            report(lp, NORMAL, "Model is primal and dual infeasible\n");
          lp->spx_status = INFEASIBLE;
          ok = FALSE;
        }
        break;
      }
      else if(lp->Extrad == 0) {

        /* We are feasible (and possibly also optimal) */
        feasible = TRUE;
        lp->simplex_mode = SIMPLEX_Phase2_DUAL;

        /* Check if we still have equality slacks stuck in the basis; drive them out? */
        if((lp->fixedvars > 0) && lp->bb_totalnodes == 0)
#ifdef Paranoia
          report(lp, NORMAL,
#else
          report(lp, DETAILED,
#endif
                    "Found dual solution with %d fixed slack variables left basic\n",
                    lp->fixedvars);

#ifdef Phase1DualPriceEqualities
        if(!forceoutEQ) {
          forceoutEQ = TRUE;
          goto RetryRow;
        }
        colnr = 0;
#else

#if 1
       /* Problem: Check if we are dual degenerate and need to switch to the
          primal simplex (there is a flaw in the dual simplex code) */
        colnr = colprim(lp, FALSE, drow, nzprow);
#else
        colnr = 0;
#endif

#endif

        if(colnr == 0)
          lp->spx_status = OPTIMAL;
        else {
          lp->spx_status = SWITCH_TO_PRIMAL;
          if(lp->total_iter == 0)
            report(lp, NORMAL, "Use primal simplex for finalization at iteration  %7d\n",
                               get_total_iter(lp));
        }
        if((lp->total_iter == 0) && (lp->spx_status == OPTIMAL))
          report(lp, NORMAL, "Optimal solution with dual simplex at iteration   %7d\n",
                             get_total_iter(lp));
        break;
      }
      else {

        /* We are feasible (and possibly also optimal) */
        feasible = TRUE;
        lp->simplex_mode = SIMPLEX_Phase2_DUAL;

        /* Set default action; force an update of the rhs vector, adjusted for
           the new Extrad=0 (set here so that usermessage() behaves properly) */
        lp->spx_status = RUNNING;

        if(lp->total_iter == 0) {
          report(lp, NORMAL, "Found feasibility by dual simplex at iteration    %7d\n",
                             get_total_iter(lp));

          if((lp->usermessage != NULL) && (lp->msgmask & MSG_LPFEASIBLE))
            lp->usermessage(lp, lp->msghandle, MSG_LPFEASIBLE);
        }
        lp->Extrad = 0;
        lp->doInvert = TRUE;
        changedphase = TRUE;

        /* Override default action, if so selected by the user */
        if((lp->simplex_strategy & SIMPLEX_DUAL_PRIMAL) && (lp->fixedvars == 0))
          lp->spx_status = SWITCH_TO_PRIMAL;
      }
    }

    if(lp->doIterate) {
      i = lp->var_basic[rownr];

      if(lp->justInverted)
        minitcount = 0;
      else if(minitcount > MAX_MINITUPDATES)
        recompute_solution(lp, INITSOL_USEZERO);
      minit = performiteration(lp, rownr, colnr, theta, primal, prow, nzprow);
      if(minit == ITERATE_MINORRETRY)
        minitcount++;

#ifdef UseDualReducedCostUpdate
      /* Do a fast update of the reduced costs in preparation for the next iteration */
      if(minit == ITERATE_MAJORMAJOR)
        update_reducedcosts(lp, primal, i, colnr, prow, drow);
#endif
      if((minit == ITERATE_MAJORMAJOR) && is_fixedvar(lp, i))
        lp->fixedvars--;
      else if(minit == ITERATE_MINORMAJOR)
        ;
      if((lp->spx_status == USERABORT) || (lp->spx_status == TIMEOUT))
        break;
    }

    if(lp->spx_status == SWITCH_TO_PRIMAL) {
    }
    else if(lp->bfp_mustrefactorize(lp)) {
      i = invert(lp, INITSOL_USEZERO);
#ifdef ResetMinitOnReinvert
      minit = ITERATE_MAJORMAJOR;
#endif
      if((lp->spx_status == USERABORT) || (lp->spx_status == TIMEOUT))
        break;
      else if(!i) {
        lp->spx_status = SINGULAR_BASIS;
        break;
      }
    }
    userabort(lp, -1);
  }

  if (lp->piv_strategy != oldpivmode)
    lp->piv_strategy = oldpivmode;

#ifdef UseSparseReducedCost
  FREE(nzprow);
#endif
  FREE(drow);
  FREE(prow);
  FREE(pcol);

  return(ok);
}

STATIC int spx_run(lprec *lp)
{
  int    i, singular_count, lost_feas_count;
  MYBOOL feasible, lost_feas, bb_skipinv;

  lp->current_iter = 0;
  lp->current_bswap = 0;
  if(lp->rowblocks != NULL)
    lp->rowblocks->blocknow = 1;
  if(lp->colblocks != NULL)
    lp->colblocks->blocknow = 1;
  lp->spx_status = RUNNING;
  lp->bb_status = lp->spx_status;
  lp->Extrap = 0;
  lp->Extrad = 0;
  singular_count  = 0;
  lost_feas_count = 0;
  lost_feas = FALSE;
  lp->simplex_mode = SIMPLEX_DYNAMIC;

  /* Reinvert for initialization, if necessary */
  bb_skipinv = is_bb_action(lp, ACTION_ACTIVE);
  if(!bb_skipinv && lp->doInvert) {
    i = my_if(lp->doRebase, INITSOL_SHIFTZERO, INITSOL_USEZERO);
    invert(lp, (MYBOOL) i);
  }
  feasible = isPrimalFeasible(lp, lp->epspivot);

  /* Compute the number of fixed basic variables */
  lp->fixedvars = 0;
  for(i = 1; i <= lp->sum; i++) {
    if(lp->is_basic[i] && is_fixedvar(lp, i))
      lp->fixedvars++;
  }

  /* Loop for as long as needed */
  while(lp->spx_status == RUNNING) {

    if(userabort(lp, -1))
      break;

    /* Check whether we are feasible or infeasible. */
    feasible &= !bb_skipinv;
    bb_skipinv = FALSE;
	  if(lp->spx_trace) {
      if(feasible)
        report(lp, NORMAL, "Start at feasible basis\n");
      else if(lost_feas_count > 0)
        report(lp, NORMAL, "Continuing at infeasible basis\n");
      else
        report(lp, NORMAL, "Start at infeasible basis\n");
    }

   /* Now do the simplex magic */
#ifdef EnablePrimalPhase1
    if(((lp->simplex_strategy & SIMPLEX_Phase1_DUAL) == 0) ||
       ((MIP_count(lp) > 0) && (lp->total_iter == 0) &&
        is_presolve(lp, PRESOLVE_REDUCEMIP))) {
#else
    if(feasible &&
       ((lp->simplex_strategy & SIMPLEX_Phase1_DUAL) == 0)) {
#endif
      if(!lost_feas && feasible && ((lp->simplex_strategy & SIMPLEX_Phase2_DUAL) > 0))
        lp->spx_status = SWITCH_TO_DUAL;
      else
        primloop(lp, feasible);
      if(lp->spx_status == SWITCH_TO_DUAL)
        dualloop(lp, TRUE);
    }
    else {
      if(!lost_feas && feasible && ((lp->simplex_strategy & SIMPLEX_Phase2_PRIMAL) > 0))
        lp->spx_status = SWITCH_TO_PRIMAL;
      else
        dualloop(lp, feasible);
      if(lp->spx_status == SWITCH_TO_PRIMAL)
        primloop(lp, TRUE);
    }

    /* Check for simplex outcomes that always involve breaking out of the loop;
       this includes optimality, unboundedness, pure infeasibility (i.e. not
       loss of feasibility), numerical failure and perturbation-based degeneracy
       handling */
    i = lp->spx_status;
    feasible = (MYBOOL) ((i == OPTIMAL) && isPrimalFeasible(lp, lp->epspivot));
    if(feasible || (i == UNBOUNDED))
      break;
#ifdef EnableAntiDegen
    else if(((i == INFEASIBLE) && is_anti_degen(lp, ANTIDEGEN_INFEASIBLE)) ||
            ((i == LOSTFEAS) && is_anti_degen(lp, ANTIDEGEN_LOSTFEAS)) ||
            ((i == NUMFAILURE) && is_anti_degen(lp, ANTIDEGEN_NUMFAILURE)) ||
            ((i == DEGENERATE) && is_anti_degen(lp, ANTIDEGEN_STALLING))) {
      if((lp->bb_level <= 1) || is_anti_degen(lp, ANTIDEGEN_DURINGBB))
        break;
    }
#endif

    /* Check for outcomes that may involve trying another simplex loop */
    if(lp->spx_status == SINGULAR_BASIS) {
      lost_feas = FALSE;
      singular_count++;
      if(singular_count >= DEF_MAXSINGULARITIES) {
        report(lp, IMPORTANT, "spx_run: Failure due to too many singular bases.\n");
        lp->spx_status = NUMFAILURE;
        break;
      }
      if(lp->spx_trace || (lp->verbose > DETAILED))
        report(lp, NORMAL, "spx_run: Singular basis; attempting to recover.\n");
      lp->spx_status = RUNNING;
      /* Singular pivots are simply skipped by the inversion, leaving a row's
         slack variable in the basis instead of the singular user variable. */
    }
    else {
      lost_feas = (MYBOOL) (lp->spx_status == LOSTFEAS);
#if 0
      /* Optionally handle loss of numerical accuracy as loss of feasibility,
         but only attempt a single loop to try to recover from this. */
      lost_feas |= (MYBOOL) ((lp->spx_status == NUMFAILURE) && (lost_feas_count < 1));
#endif
      if(lost_feas) {
        lost_feas_count++;
        if(lost_feas_count < DEF_MAXSINGULARITIES) {
          report(lp, DETAILED, "spx_run: Recovering from lost feasibility (iteration %d).\n",
                                get_total_iter(lp));
          lp->spx_status = RUNNING;
        }
        else {
          report(lp, IMPORTANT, "Failure due to loss of feasibility %d times (iteration %d).\n",
                                 lost_feas_count, get_total_iter(lp));
          lp->spx_status = NUMFAILURE;
        }
      }
    }
  }

  /* Update iteration tallies before returning */
  lp->total_iter   += lp->current_iter;
  lp->current_iter  = 0;
  lp->total_bswap  += lp->current_bswap;
  lp->current_bswap = 0;

  return(lp->spx_status);
} /* spx_run */

lprec *make_lag(lprec *lpserver)
{
  int    i;
  lprec  *hlp;
  MYBOOL ret;
  REAL   *duals;

  /* Create a Lagrangean solver instance */
  hlp = lp_solve_make_lp(0, lpserver->columns);

  if(hlp != NULL) {

    /* First create and core variable data */
    set_sense(hlp, is_maxim(lpserver));
    hlp->lag_bound = lpserver->bb_limitOF;
    for(i = 1; i <= lpserver->columns; i++) {
      lp_solve_set_mat(hlp, 0, i, get_mat(lpserver, 0, i));
      if(is_binary(lpserver, i))
        set_binary(hlp, i, TRUE);
      else {
        lp_solve_set_int(hlp, i, is_int(lpserver, i));
        set_bounds(hlp, i, get_lowbo(lpserver, i), get_upbo(lpserver, i));
      }
    }
    /* Then fill data for the Lagrangean constraints */
    hlp->matL = lpserver->matA;
    inc_lag_space(hlp, lpserver->rows, TRUE);
    ret = get_ptr_sensitivity_rhs(hlp, &duals, NULL, NULL);
    for(i = 1; i <= lpserver->rows; i++) {
      hlp->lag_con_type[i] = get_constr_type(lpserver, i);
      hlp->lag_rhs[i] = lpserver->orig_rh[i];
      hlp->lambda[i] = (ret) ? duals[i - 1] : 0.0;
    }
  }

  return(hlp);
}

STATIC int heuristics(lprec *lp, int mode)
/* Initialize / bound a MIP problem */
{
  lprec *hlp;
  int   status = PROCFAIL;

  if(lp->bb_level > 1)
    return( status );

  status = RUNNING;
  if(FALSE && (lp->int_count > 0)) {

    /* 1. Copy the problem into a new relaxed instance, extracting Lagrangean constraints */
    hlp = make_lag(lp);

    /* 2. Run the Lagrangean relaxation */
    status = lp_solve_solve(hlp);

    /* 3. Copy the key results (bound) into the original problem */
    lp->bb_heuristicOF = hlp->best_solution[0];

    /* 4. Delete the helper heuristic */
    hlp->matL = NULL;
    lp_solve_delete_lp(hlp);
  }
  return( status );
}

STATIC int lag_solve(lprec *lp, REAL start_bound, int num_iter)
{
  int    i, j, citer, nochange, oldpresolve;
  MYBOOL LagFeas, AnyFeas, Converged, same_basis;
  REAL   *OrigObj, *ModObj, *SubGrad, *BestFeasSol;
  REAL   Zub, Zlb, Znow, Zprev, Zbest, rhsmod, hold;
  REAL   Phi, StepSize = 0.0, SqrsumSubGrad;
  double starttime;

  /* Make sure we have something to work with */
  if(lp->spx_status != OPTIMAL) {
    lp->lag_status = NOTRUN;
    return( lp->lag_status );
  }
  lp->lag_status = RUNNING;

  /* Prepare for Lagrangean iterations using results from relaxed problem */
  starttime   = lp->timestart;
  oldpresolve = lp->do_presolve;
  lp->do_presolve = PRESOLVE_NONE;
  push_basis(lp, NULL, NULL, NULL);

  /* Allocate iteration arrays */
  allocREAL(lp, &OrigObj, lp->columns + 1, FALSE);
  allocREAL(lp, &ModObj, lp->columns + 1, TRUE);
  allocREAL(lp, &SubGrad, get_Lrows(lp) + 1, TRUE);
  allocREAL(lp, &BestFeasSol, lp->sum + 1, TRUE);

  /* Initialize variables (assume minimization problem in overall structure) */
  Zlb      = lp->best_solution[0];
  Zub      = start_bound;
  Zbest    = Zub;
  Znow     = Zlb;
  Zprev    = lp->infinite;
  rhsmod   = 0;

  Phi      = DEF_LAGCONTRACT; /* In the range 0-2.0 to guarantee convergence */
/*  Phi      = 0.15; */
  LagFeas  = FALSE;
  Converged= FALSE;
  AnyFeas  = FALSE;
  citer    = 0;
  nochange = 0;

  /* Initialize reference and solution vectors; don't bother about the
     original OF offset since we are maintaining an offset locally. */
  get_row(lp, 0, OrigObj);
#ifdef DirectOverrideOF
  set_OF_override(lp, ModObj);
#endif
  OrigObj[0] = get_rh(lp, 0);
  for(i = 1 ; i <= get_Lrows(lp); i++)
    lp->lambda[i] = 0;

  /* Iterate to convergence, failure or user-specified termination */
  while((lp->lag_status == RUNNING) && (citer < num_iter)) {

    citer++;

    /* Compute constraint feasibility gaps and associated sum of squares,
       and determine feasibility over the Lagrangean constraints;
       SubGrad is the subgradient, which here is identical to the slack. */
    LagFeas = TRUE;
    Converged = TRUE;
    SqrsumSubGrad = 0;
    for(i = 1; i <= get_Lrows(lp); i++) {
      hold = lp->lag_rhs[i];
      for(j = 1; j <= lp->columns; j++)
        hold -= mat_getitem(lp->matL, i, j) * lp->best_solution[lp->rows + j];
      if(LagFeas) {
        if(lp->lag_con_type[i] == EQ) {
          if(fabs(hold) > lp->epsprimal)
            LagFeas = FALSE;
        }
        else if(hold < -lp->epsprimal)
          LagFeas = FALSE;
      }
      /* Test for convergence and update */
      if(Converged && (fabs(my_reldiff(hold , SubGrad[i])) > lp->lag_accept))
        Converged = FALSE;
      SubGrad[i] = hold;
      SqrsumSubGrad += hold * hold;
    }
    SqrsumSubGrad = sqrt(SqrsumSubGrad);
#if 1
    Converged &= LagFeas;
#endif
    if(Converged)
      break;

    /* Modify step parameters and initialize ahead of next iteration */
    Znow = lp->best_solution[0] - rhsmod;
    if(Znow > Zub) {
      /* Handle exceptional case where we overshoot */
      Phi *= DEF_LAGCONTRACT;
      StepSize *= (Zub-Zlb) / (Znow-Zlb);
    }
    else
#define LagBasisContract
#ifdef LagBasisContract
/*      StepSize = Phi * (Zub - Znow) / SqrsumSubGrad; */
      StepSize = Phi * (2-DEF_LAGCONTRACT) * (Zub - Znow) / SqrsumSubGrad;
#else
      StepSize = Phi * (Zub - Znow) / SqrsumSubGrad;
#endif

    /* Compute the new dual price vector (Lagrangean multipliers, lambda) */
    for(i = 1; i <= get_Lrows(lp); i++) {
      lp->lambda[i] += StepSize * SubGrad[i];
      if((lp->lag_con_type[i] != EQ) && (lp->lambda[i] > 0)) {
        /* Handle case where we overshoot and need to correct (see above) */
        if(Znow < Zub)
          lp->lambda[i] = 0;
      }
    }
/*    normalizeVector(lp->lambda, get_Lrows(lp)); */

    /* Save the current vector if it is better */
    if(LagFeas && (Znow < Zbest)) {

      /* Recompute the objective function value in terms of the original values */
      MEMCOPY(BestFeasSol, lp->best_solution, lp->sum+1);
      hold = OrigObj[0];
      for(i = 1; i <= lp->columns; i++)
        hold += lp->best_solution[lp->rows + i] * OrigObj[i];
      BestFeasSol[0] = hold;
      if(lp->lag_trace)
        report(lp, NORMAL, "lag_solve: Improved feasible solution at iteration %d of %g\n",
                           citer, hold);

      /* Reset variables */
      Zbest = Znow;
      AnyFeas  = TRUE;
      nochange = 0;
    }
    else if(Znow == Zprev) {
      nochange++;
      if(nochange > LAG_SINGULARLIMIT) {
        Phi *= 0.5;
        nochange = 0;
      }
    }
    Zprev = Znow;

    /* Recompute the objective function values for the next iteration */
    for(j = 1; j <= lp->columns; j++) {
      hold = 0;
      for(i = 1; i <= get_Lrows(lp); i++)
        hold += lp->lambda[i] * mat_getitem(lp->matL, i, j);
      ModObj[j] = OrigObj[j] - my_chsign(is_maxim(lp), hold);
#ifndef DirectOverrideOF
      lp_solve_set_mat(lp, 0, j, ModObj[j]);
#endif
    }

    /* Recompute the fixed part of the new objective function */
    rhsmod = my_chsign(is_maxim(lp), get_rh(lp, 0));
    for(i = 1; i <= get_Lrows(lp); i++)
      rhsmod += lp->lambda[i] * lp->lag_rhs[i];

    /* Print trace/debugging information, if specified */
    if(lp->lag_trace) {
      report(lp, IMPORTANT, "Zub: %10g Zlb: %10g Stepsize: %10g Phi: %10g Feas %d\n",
                 (REAL) Zub, (REAL) Zlb, (REAL) StepSize, (REAL) Phi, LagFeas);
      for(i = 1; i <= get_Lrows(lp); i++)
        report(lp, IMPORTANT, "%3d SubGrad %10g lambda %10g\n",
                   i, (REAL) SubGrad[i], (REAL) lp->lambda[i]);
      if(lp->sum < 20)
        lp_solve_print_lp(lp);
    }

    /* Solve the Lagrangean relaxation, handle failures and compute
       the Lagrangean objective value, if successful */
    i = spx_solve(lp);
    if(lp->spx_status == UNBOUNDED) {
      if(lp->lag_trace) {
        report(lp, NORMAL, "lag_solve: Unbounded solution encountered with this OF:\n");
        for(i = 1; i <= lp->columns; i++)
          report(lp, NORMAL, MPSVALUEMASK " ", (REAL) ModObj[i]);
      }
      goto Leave;
    }
    else if((lp->spx_status == NUMFAILURE)   || (lp->spx_status == PROCFAIL) ||
            (lp->spx_status == USERABORT) || (lp->spx_status == TIMEOUT) ||
            (lp->spx_status == INFEASIBLE)) {
      lp->lag_status = lp->spx_status;
    }

    /* Compare optimal bases and contract if we have basis stationarity */
#ifdef LagBasisContract
    same_basis = compare_basis(lp);
    if(LagFeas &&
       !same_basis) {
      pop_basis(lp, FALSE);
      push_basis(lp, NULL, NULL, NULL);
      Phi *= DEF_LAGCONTRACT;
    }
    if(lp->lag_trace) {
      report(lp, DETAILED, "lag_solve: Simplex status code %d, same basis %s\n",
                 lp->spx_status, my_boolstr(same_basis));
      print_solution(lp, 1);
    }
#endif
  }

  /* Transfer solution values */
  if(AnyFeas) {
    lp->lag_bound = my_chsign(is_maxim(lp), Zbest);
    for(i = 0; i <= lp->sum; i++)
      lp->solution[i] = BestFeasSol[i];
    transfer_solution(lp, TRUE);
    if(!is_maxim(lp))
      for(i = 1; i <= get_Lrows(lp); i++)
        lp->lambda[i] = my_flipsign(lp->lambda[i]);
  }

  /* Do standard postprocessing */
Leave:

  /* Set status variables and report */
  if(citer >= num_iter) {
    if(AnyFeas)
      lp->lag_status = FEASFOUND;
    else
      lp->lag_status = NOFEASFOUND;
  }
  else
    lp->lag_status = lp->spx_status;
  if(lp->lag_status == OPTIMAL) {
    report(lp, NORMAL, "\nLagrangean convergence achieved in %d iterations\n",  citer);
    i = check_solution(lp, lp->columns,
                       lp->best_solution, lp->orig_upbo, lp->orig_lowbo, DEF_EPSSOLUTION);
  }
  else {
    report(lp, NORMAL, "\nUnsatisfactory convergence achieved over %d Lagrangean iterations.\n",
                       citer);
    if(AnyFeas)
      report(lp, NORMAL, "The best feasible Lagrangean objective function value was %g\n",
                         lp->best_solution[0]);
  }

  /* Restore the original objective function */
#ifdef DirectOverrideOF
  set_OF_override(lp, NULL);
#else
  for(i = 1; i <= lp->columns; i++)
    lp_solve_set_mat(lp, 0, i, OrigObj[i]);
#endif

  /* ... and then free memory */
  FREE(BestFeasSol);
  FREE(SubGrad);
  FREE(OrigObj);
  FREE(ModObj);
  pop_basis(lp, FALSE);

  lp->timestart = starttime;
  lp->timeend = timeNow();
  lp->do_presolve = oldpresolve;

  return( lp->lag_status );
}

STATIC int spx_solve(lprec *lp)
{
  int       status, itemp;
  MYBOOL    iprocessed;
  REAL      test;

  lp->timestart        = timeNow();
  lp->timeend          = 0;

  lp->total_iter       = 0;
  lp->total_bswap      = 0;
  lp->bb_maxlevel      = 1;
  lp->bb_totalnodes    = 0;
  lp->bb_level         = 0;
  lp->bb_solutionlevel = 0;
  lp->bb_compressions  = 0;
  lp->obj_mincost      = lp->infinite;
  if(lp->invB != NULL)
    lp->bfp_restart(lp);

  lp->spx_status = presolve(lp);
  if(lp->spx_status != RUNNING)
    goto Leave;

  iprocessed = !lp->wasPreprocessed;
  preprocess(lp);
  if(userabort(lp, -1))
    goto Leave;

  if(mat_validate(lp->matA)) {

    /* Do standard initializations */
    /*allocINT(lp, &(lp->workrowsINT), lp->rows+1, TRUE);*/
    lp->solutioncount = 0;
    lp->real_solution = lp->infinite;
    if(is_maxim(lp) && (lp->bb_heuristicOF >= lp->infinite))
      lp->best_solution[0] = -lp->infinite;
    else if(!is_maxim(lp) && (lp->bb_heuristicOF <= -lp->infinite))
      lp->best_solution[0] = lp->infinite;
    else
      lp->best_solution[0] = lp->bb_heuristicOF;

    lp->doInvert = TRUE;
    lp->doRebase = TRUE;
    lp->bb_break = FALSE;
    lp->bb_action = ACTION_NONE;  /* TEST */

    /* Do the call to the real underlying solver (note that
       run_BB is replacable with any compatible MIP solver) */
    status = run_BB(lp);

    /* Restore modified problem */
    if(iprocessed)
      postprocess(lp);

    if(lp->lag_status != RUNNING) {
      if((status == OPTIMAL) || (status == SUBOPTIMAL)) {
        itemp = check_solution(lp, lp->columns, lp->best_solution,
                                   lp->orig_upbo, lp->orig_lowbo, DEF_EPSSOLUTION);
        if((itemp != OPTIMAL) && (lp->spx_status == OPTIMAL))
          lp->spx_status = itemp;
        else if((itemp == OPTIMAL) && (status == SUBOPTIMAL))
          lp->spx_status = status;
      }
      else {
        report(lp, NORMAL, "lp_solve unsuccessful after %d iterations and a last best value of %g\n",
               lp->total_iter, lp->best_solution[0]);
        if(lp->bb_totalnodes > 0)
          report(lp, NORMAL, "lp_solve explored %d nodes before termination\n",
                 lp->bb_totalnodes);
      }
    }
    /*FREE(lp->workrowsINT);
    lp->workrowsINT = NULL;*/

    goto Leave;
  }

  /* If we get here, mat_validate(lp) failed. */
  if(lp->bb_trace || lp->spx_trace)
    report(lp, CRITICAL, "spx_solve: The current LP seems to be invalid\n");
  lp->spx_status = NUMFAILURE;

Leave:
  lp->timeend = timeNow();

  if((lp->lag_status != RUNNING) && (lp->invB != NULL)) {
    itemp = lp->bfp_nonzeros(lp, TRUE);
    test = 100;
    if(lp->total_iter > 0)
      test *= (REAL) lp->total_bswap/lp->total_iter;
    report(lp, NORMAL, "\n ");
    report(lp, NORMAL, "Memo: Largest [%s] inv(B) had %d NZ entries, %.1fx largest basis.\n",
                        lp->bfp_name(), itemp, lp->bfp_efficiency(lp));
    report(lp, NORMAL, "      In the total iteration count %d, %d (%.1f%%) were minor/bound swaps.\n",
                        lp->total_iter, lp->total_bswap, test);
    report(lp, NORMAL, "      There were %d refactorizations, on average %.1f major pivots/refact.\n",
                        lp->bfp_refactcount(lp), get_refactfrequency(lp, TRUE));

    if(MIP_count(lp) > 0) {
      report(lp, NORMAL, "      The maximum B&B level was %d, %.1fx MIP order with %g compressions/node.\n",
                        lp->bb_maxlevel, (REAL) lp->bb_maxlevel / (MIP_count(lp)+lp->int_count),
                        (REAL) lp->bb_compressions/(lp->bb_totalnodes+1.0));
      if(lp->bb_solutionlevel > 0)
      report(lp, NORMAL, "      The B&B level was %d at the optimal solution.\n",
                        lp->bb_solutionlevel);
    }
    report(lp, NORMAL, "      Total solver time was %.3f seconds.\n",
                        lp->timeend- lp->timestart);
  }

  return(lp->spx_status);

} /* spx_solve */

int lin_solve(lprec *lp)
{
  int status = NOTRUN;

  if(lp->duals != NULL)
    FREE(lp->duals);

  if(lp->dualsfrom != NULL)
    FREE(lp->dualsfrom);

  if(lp->dualstill != NULL)
    FREE(lp->dualstill);

  if(lp->objfrom != NULL)
    FREE(lp->objfrom);

  if(lp->objtill != NULL)
    FREE(lp->objtill);

  /* Do heuristics ahead of solving the model */
  if(heuristics(lp, AUTOMATIC) != RUNNING)
    return( INFEASIBLE );

  /* Solve the full, prepared model */
  status = spx_solve(lp);
  if(get_Lrows(lp) > 0) {
    if(status == OPTIMAL)
      status = lag_solve(lp, lp->bb_heuristicOF, DEF_LAGMAXITERATIONS);
    else
      report(lp, IMPORTANT, "\nCannot do Lagrangean optimization since core model was not solved.\n");
  }

  return( status );
}


