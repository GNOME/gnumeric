
/*
    Mixed integer programming optimization drivers for lp_solve v5.0+
   ----------------------------------------------------------------------------------
    Author:        Michel Berkelaar (to lp_solve v3.2),
                   Kjell Eikland
    Contact:
    License terms: LGPL.

    Requires:      string.h, float.h, lp_lib.h, lp_report.h, lp_simplex.h

    Release notes:
    v5.0.0 31 January 2004      New unit isolating B&B routines.
    v5.0.1 01 February 2004     Complete rewrite into non-recursive version.
    v5.0.2 05 April 2004        Expanded pseudocosting with options for MIP fraction
                                counts and "cost/benefit" ratio (KE special!).
                                Added GUB functionality based on SOS structures.
    v5.0.3    1 May 2004        Changed routine names to be more intuitive.
    v5.0.4    15 May 2004       Added functinality to pack bounds in order to
                                conserve memory in B&B-processing large MIP models.

   ----------------------------------------------------------------------------------
*/

#include <string.h>
#include <float.h>
#include "commonlib.h"
#include "lp_lib.h"
#include "lp_report.h"
#include "lp_simplex.h"
#include "lp_mipbb.h"

#ifdef FORTIFY
# include "lp_fortify.h"
#endif


/* Allocation routine for the BB record structure */
STATIC BBrec *create_BB(lprec *lp, BBrec *parentBB, REAL *UB_active, REAL *LB_active)
{
  BBrec *newBB;

  newBB = (BBrec *) calloc(1, sizeof(*newBB));
  if((newBB != NULL) &&
     allocREAL(lp, &newBB->upbo,  lp->sum + 1, FALSE) &&
     allocREAL(lp, &newBB->lowbo, lp->sum + 1, FALSE)) {

    if(parentBB == NULL) {
      newBB->UB_base = lp->orig_upbo;
      newBB->LB_base = lp->orig_lowbo;
    }
    else {
      newBB->UB_base = UB_active;
      newBB->LB_base = LB_active;
    }
    MEMCOPY(newBB->upbo,  newBB->UB_base, lp->sum + 1);
    MEMCOPY(newBB->lowbo, newBB->LB_base, lp->sum + 1);

    newBB->lp = lp;

    /* Set parent by default, but not child and then compress backwards (if possible) */
    newBB->parent = parentBB;
    if(compress_BB(newBB)) {
      if(parentBB->saved_upbo != NULL)
        lp->bb_compressions++;
      if(parentBB->saved_lowbo != NULL)
        lp->bb_compressions++;
    }

  }
  return( newBB );
}


/* Memory conservation by compressing the parent's bounds */
STATIC MYBOOL compress_BB(BBrec *BB)
{
  BBrec *parentBB;

  if(BB == NULL)
    return( FALSE );
  parentBB = BB->parent;
  if((parentBB == NULL) || (parentBB == parentBB->lp->rootbounds) ||
     (MAX_BB_FULLBOUNDSAVE <= 0) || (MIP_count(parentBB->lp) < MAX_BB_FULLBOUNDSAVE))
    return( FALSE );

  if(BB->LB_base != parentBB->lowbo) {
    parentBB->saved_lowbo = createPackedVector(parentBB->lp->sum, parentBB->lowbo, NULL);
    if(parentBB->saved_lowbo != NULL)
      allocFREE(parentBB->lp, (void **) &(parentBB->lowbo));
  }

  if(BB->UB_base != parentBB->upbo) {
    parentBB->saved_upbo = createPackedVector(parentBB->lp->sum, parentBB->upbo, NULL);
    if(parentBB->saved_upbo != NULL)
      allocFREE(parentBB->lp, (void **) &(parentBB->upbo));
  }

  return( TRUE );
}

STATIC MYBOOL uncompress_BB(BBrec *BB)
{
  if(BB == NULL)
    return( FALSE );

  if(BB->saved_lowbo != NULL) {
    allocREAL(BB->lp, &BB->lowbo, BB->lp->sum + 1, FALSE);
    unpackPackedVector(BB->saved_lowbo, &(BB->lowbo));
    freePackedVector(&BB->saved_lowbo);
  }
  if(BB->saved_upbo != NULL) {
    allocREAL(BB->lp, &BB->upbo, BB->lp->sum + 1, FALSE);
    unpackPackedVector(BB->saved_upbo, &(BB->upbo));
    freePackedVector(&BB->saved_upbo);
  }
  return( TRUE );
}


/* Pushing and popping routines for the B&B structure */

STATIC BBrec *push_BB(lprec *lp, BBrec *parentBB, int varno, int vartype,
                                 REAL *UB_active, REAL *LB_active, int varcus)
/* Push ingoing bounds and B&B data onto the stack */
{
  BBrec *newBB;

  /* Do initialization and updates */
  if(parentBB == NULL)
    parentBB = lp->bb_bounds;
  newBB = create_BB(lp, parentBB, UB_active, LB_active);
  if(newBB != NULL) {

    newBB->varno = varno;
    newBB->vartype = vartype;
    newBB->lastvarcus = varcus;

    /* Handle case where we are pushing at the end */
    if(parentBB == lp->bb_bounds)
      lp->bb_bounds = newBB;
    /* Handle case where we are pushing in the middle */
    else
      newBB->child = parentBB->child;
    if(parentBB != NULL)
      parentBB->child = newBB;

    lp->bb_level++;
    if(lp->bb_level > lp->bb_maxlevel)
      lp->bb_maxlevel = lp->bb_level;

    if(!initbranches_BB(newBB))
      newBB = pop_BB(newBB);
    else if(MIP_count(lp) > 0) {
      if((lp->bb_level <= 1) && (lp->bb_varactive == NULL) &&
         !allocINT(lp, &lp->bb_varactive, lp->columns+1, TRUE))
        newBB = pop_BB(newBB);
      if(varno > 0) {
        lp->bb_varactive[varno-lp->rows]++;
/*        mergeshadow_BB(newBB); */ /* Preparation for dynamic B&B */
      }
    }
  }
  return( newBB );
}

STATIC void free_BB(BBrec **BB)
{
  if((BB == NULL) || (*BB == NULL))
    return;

  FREE((*BB)->upbo);
  FREE((*BB)->lowbo);

  uncompress_BB((*BB)->parent);

  FREE(*BB);
}

STATIC BBrec *pop_BB(BBrec *BB)
/* Pop / free the previously "pushed" / saved bounds */
{
  int   k;
  BBrec *parentBB;
  lprec *lp = BB->lp;

  if(BB == NULL)
    return( BB );

  /* Handle case where we are popping the end of the chain */
  parentBB = BB->parent;
  if(BB == lp->bb_bounds) {
    lp->bb_bounds = parentBB;
    if(parentBB != NULL)
      parentBB->child = NULL;
  }
  /* Handle case where we are popping inside or at the beginning of the chain */
  else {
    if(parentBB != NULL)
      parentBB->child = BB->child;
    if(BB->child != NULL)
      BB->child->parent = parentBB;
  }

  /* Unwind other variables */
  lp->bb_level--;
  k = BB->varno - lp->rows;
  if(lp->bb_level == 0) {
    if(lp->bb_varactive != NULL) {
      FREE(lp->bb_varactive);
    }
    if(lp->int_count+lp->sc_count > 0)
      free_PseudoCost(lp);
    pop_basis(lp, FALSE);
    lp->rootbounds = NULL;
  }
  else
    lp->bb_varactive[k]--;

  /* Undo SOS/GUB markers */
  if(BB->isSOS && (BB->vartype != BB_INT))
    SOS_unmark(lp->SOS, 0, k);
  else if(BB->isGUB)
    SOS_unmark(lp->GUB, 0, k);

  /* Undo the SC marker */
  if(BB->sc_canset)
    lp->var_is_sc[k] *= -1;

  /* Pop the associated basis */
#if 1
  /* Original version that does not restore previous basis */
  pop_basis(lp, FALSE);
#else
  /* Experimental version that restores previous basis */
  pop_basis(lp, BB->isSOS);
#endif

  /* Finally free the B&B object */
  free_BB(&BB);

  /* Return the parent BB */
  return( parentBB );
}

STATIC MYBOOL initbranches_BB(BBrec *BB)
{
  REAL  new_bound, temp;
  int   k;
  lprec *lp = BB->lp;

 /* Create and initialize local bounds and basis */
  BB->nodestatus = NOTRUN;
  BB->noderesult = lp->infinite;
  push_basis(lp, NULL, NULL, NULL);

 /* Set default number of branches at the current B&B branch */
  if(BB->vartype == BB_REAL)
    BB->nodesleft = 1;

  else {
   /* The default is a binary up-low branching */
    BB->nodesleft = 2;

   /* Initialize the MIP status code pair and set reference values */
    k = BB->varno - lp->rows;
    BB->lastsolution = lp->solution[BB->varno];

   /* Determine if we must process in the B&B SOS mode */
    BB->isSOS = (MYBOOL) ((BB->vartype == BB_SOS) || SOS_is_member(lp->SOS, 0, k));
#ifdef Paranoia
    if((BB->vartype == BB_SOS) && !SOS_is_member(lp->SOS, 0, k))
      report(lp, SEVERE, "initbranches_BB: Inconsistent identification of SOS variable %s (%d)\n",
                         get_col_name(lp, k), k);
#endif

   /* Check if we have a GUB-member variable that needs a triple-branch */
    BB->isGUB = (MYBOOL) ((BB->vartype == BB_INT) && SOS_can_activate(lp->GUB, 0, k));
    if(BB->isGUB)
      BB->nodesleft++;


   /* Set local pruning info, automatic, or user-defined strategy */
    if(BB->vartype == BB_SOS) {
      if(!SOS_can_activate(lp->SOS, 0, k)) {
        BB->nodesleft--;
        BB->isfloor = TRUE;
      }
      else
        BB->isfloor = (MYBOOL) (BB->lastsolution == 0);
    }
    else if(get_var_branch(lp, k) == BRANCH_AUTOMATIC) {
      new_bound = modf(BB->lastsolution/get_PseudoRange(lp, k, BB->vartype), &temp);
      if(_isnan(new_bound))
        new_bound = 0;
      BB->isfloor = (MYBOOL) (new_bound <= 0.5);
      if(is_bb_mode(lp, NODE_GREEDYMODE)) {
       /* Set direction by OF value; note that a zero-value in
          the OF gives priority to floor_first = TRUE */
        if(is_bb_mode(lp, NODE_PSEUDOCOSTMODE))
          BB->sc_bound = get_PseudoCost(lp, k, BB->vartype, BB->lastsolution);
        else
          BB->sc_bound = mat_getitem(lp->matA, 0, k);
        new_bound -= 0.5;
        BB->sc_bound *= new_bound;
        BB->isfloor = (MYBOOL) (BB->sc_bound > 0);
      }
      /* Check for reversal */
      if(is_bb_mode(lp, NODE_BRANCHREVERSEMODE))
        BB->isfloor = !BB->isfloor;
    }
    else
      BB->isfloor = (MYBOOL) (get_var_branch(lp, k) == BRANCH_FLOOR);

 /* SC logic: If the current SC variable value is in the [0..NZLOBOUND> range, then

  	UP: Set lower bound to NZLOBOUND, upper bound is the original
  	LO: Fix the variable by setting upper/lower bound to zero

    ... indicate that the variable is B&B-active by reversing sign of var_is_sc[]. */
    new_bound = fabs(lp->var_is_sc[k]);
    BB->sc_bound = new_bound;
    BB->sc_canset = (MYBOOL) (new_bound != 0);

 /* Must make sure that we handle fractional lower bounds properly;
    also to ensure that we do a full binary tree search */
    new_bound = unscaled_value(lp, new_bound, BB->varno);
    if(is_int(lp, k) && ((new_bound > 0) &&
	                       (BB->lastsolution > floor(new_bound)))) {
      if(BB->lastsolution < ceil(new_bound))
        BB->lastsolution += 1;
      BB->lowbo[BB->varno] = scaled_floor(lp, BB->varno, BB->lastsolution, 1);
    }
  }

  /* Now initialize the brances and set to first */
  return( fillbranches_BB(BB) );

}

STATIC MYBOOL fillbranches_BB(BBrec *BB)
{
  int    K, k;
  REAL   ult_upbo, ult_lowbo;
  REAL   new_bound, SC_bound, intmargin = BB->lp->epsprimal;
  lprec  *lp = BB->lp;
  MYBOOL OKstatus = FALSE;

  if(lp->bb_break || userabort(lp, MSG_MILPSTRATEGY))
    return( OKstatus );

  K = BB->varno;
  if(K > 0) {

  /* Shortcut variables */
    k = BB->varno - lp->rows;
    ult_upbo  = lp->orig_upbo[K];
    ult_lowbo = lp->orig_lowbo[K];
    SC_bound  = unscaled_value(lp, BB->sc_bound, K);

    /* First, establish the upper bound to be applied (when isfloor == TRUE)
       --------------------------------------------------------------------- */
/*SetUB:*/
    BB->UPbound = lp->infinite;

    /* Handle SC-variables for the [0-LoBound> range */
    if((SC_bound > 0) && (BB->lastsolution < SC_bound)) {
      new_bound = 0;
    }
    /* Handle pure integers (non-SOS, non-SC) */
    else if(BB->vartype == BB_INT) {
      if((floor(BB->lastsolution) < /* Skip cases where the lower bound becomes violated */
          unscaled_value(lp, MAX(ult_lowbo, fabs(lp->var_is_sc[k])), K)-intmargin)) {
        BB->nodesleft--;
        goto SetLB;
      }
      new_bound = scaled_floor(lp, K, BB->lastsolution, 1);
    }
    else if(BB->isSOS) {           /* Handle all SOS variants */
      new_bound = ult_lowbo;
      if(is_int(lp, k))
        new_bound = scaled_ceil(lp, K, unscaled_value(lp, new_bound, K), -1);
    }
    else                           /* Handle all other variable incarnations */
      new_bound = BB->sc_bound;

      /* Check if the new bound might conflict and possibly make adjustments */
    if(new_bound < BB->LB_base[K]) {
#ifdef Paranoia
      debug_print(lp,
          "New upper bound value %g conflicts with old lower bound %g\n",
          new_bound, BB->LB_base[K]);
#endif
      BB->nodesleft--;
      goto SetLB;
    }
#ifdef Paranoia
    /* Do additional consistency checking */
    else if(!check_if_less(lp, new_bound, BB->UB_base[K], K)) {
      BB->nodesleft--;
      goto SetLB;
    }
#endif
    /* Bound (at least near) feasible */
    else {
      /* Makes a difference with models like QUEEN
         (note consistent use of epsint for scaled integer variables) */
      if(fabs(new_bound - BB->LB_base[K]) < intmargin*SCALEDINTFIXRANGE)
        new_bound = BB->LB_base[K];
    }

    BB->UPbound = new_bound;


    /* Next, establish the lower bound to be applied (when isfloor == FALSE)
       --------------------------------------------------------------------- */
SetLB:
    BB->LObound = -lp->infinite;

    /* Handle SC-variables for the [0-LoBound> range */
    if((SC_bound > 0) && (BB->lastsolution < SC_bound)) {
      if(is_int(lp, k))
        new_bound = scaled_ceil(lp, K, SC_bound, 1);
      else
        new_bound = BB->sc_bound;
    }
    /* Handle pure integers (non-SOS, non-SC, but Ok for GUB!) */
    else if((BB->vartype == BB_INT)) {
      if(((ceil(BB->lastsolution) == BB->lastsolution)) ||    /* Skip branch 0 if the current solution is integer */
         (ceil(BB->lastsolution) >   /* Skip cases where the upper bound becomes violated */
          unscaled_value(lp, ult_upbo, K)+intmargin) ||
          (BB->isSOS && (BB->lastsolution == 0))) {           /* Don't branch 0 since this is handled in SOS logic */
        BB->nodesleft--;
        goto Finish;
      }
      new_bound = scaled_ceil(lp, K, BB->lastsolution, 1);
    }
    else if(BB->isSOS) {             /* Handle all SOS variants */
      if(SOS_is_member_of_type(lp->SOS, k, SOS3))
        new_bound = scaled_floor(lp, K, 1, 1);
      else {
        new_bound = ult_lowbo;
        if(is_int(lp, k))
          new_bound = scaled_floor(lp, K, unscaled_value(lp, new_bound, K), 1);
      }
    }
    else 	                           /* Handle all other variable incarnations */
      new_bound = BB->sc_bound;

    /* Check if the new bound might conflict and possibly make adjustments */
    if(new_bound > BB->UB_base[K]) {
#ifdef Paranoia
      debug_print(lp,
        "New lower bound value %g conflicts with old upper bound %g\n",
        new_bound, BB->UB_base[K]);
#endif
      BB->nodesleft--;
      goto Finish;
    }
#ifdef Paranoia
    /* Do additional consistency checking */
    else if(!check_if_less(lp, BB->LB_base[K], new_bound, K)) {
      BB->nodesleft--;
      goto Finish;
    }
#endif
    /* Bound (at least near-)feasible */
    else {
      /* Makes a difference with models like QUEEN
         (note consistent use of lp->epsprimal for scaled integer variables) */
      if(fabs(BB->UB_base[K]-new_bound) < intmargin*SCALEDINTFIXRANGE)
        new_bound = BB->UB_base[K];
    }

    BB->LObound = new_bound;

    /* Prepare for the first branch by making sure we are pointing correctly */
Finish:
    if(BB->nodesleft > 0) {
      /* Do adjustments */
      if((BB->vartype != BB_SOS) && (fabs(BB->LObound-BB->UPbound) < intmargin)) {
        BB->nodesleft--;
        if(fabs(BB->LB_base[K]-BB->LObound) < intmargin)
          BB->isfloor = FALSE;
        else if(fabs(BB->UB_base[K]-BB->UPbound) < intmargin)
          BB->isfloor = TRUE;
        else
          report(BB->lp, IMPORTANT, "fillbranches_BB: Inconsistent equal-valued bounds for %s\n",
                                    get_col_name(BB->lp, k));
      }
      if((BB->nodesleft == 1) &&
         ((BB->isfloor && (BB->UPbound >= lp->infinite)) ||
          (!BB->isfloor && (BB->LObound <= -lp->infinite))))
        BB->isfloor = !BB->isfloor;
      /* Header initialization */
      BB->isfloor = !BB->isfloor;
      while(!OKstatus && (BB->nodesleft > 0))
        OKstatus = nextbranch_BB( BB );
    }

    /* Set an SC variable active, if necessary */
    if(BB->sc_canset)
      lp->var_is_sc[k] *= -1;
  }
  else {
    BB->nodesleft--;
    OKstatus = TRUE;
  }

  return( OKstatus );
}

STATIC MYBOOL nextbranch_BB(BBrec *BB)
{
  int    k;
  lprec  *lp = BB->lp;
  MYBOOL OKstatus = FALSE;

  if(lp->bb_break || userabort(lp, MSG_MILPSTRATEGY)) {
    /* Handle the special case of B&B restart;
       (typically used with the restart after pseudocost initialization) */
    if((lp->bb_level == 1) && (lp->bb_break == AUTOMATIC)) {
      lp->bb_break = FALSE;
      OKstatus = TRUE;
    }
    return( OKstatus );
  }

  if(BB->nodesleft > 0) {

    /* Step and update remaining branch count */
    k = BB->varno - lp->rows;
    BB->isfloor = !BB->isfloor;
    BB->nodesleft--;

    /* Special SOS handling:
       1) Undo and set new marker for k,
       2) In case that previous branch was ceiling restore upper bounds of the
          non-k variables outside of the SOS window set to 0 */
    if(BB->isSOS && (BB->vartype != BB_INT)) {

      /* First undo previous marker */
      if((BB->nodessolved > 0) || ((BB->nodessolved == 0) && (BB->nodesleft == 0))) {
        if(BB->isfloor) {
          if((BB->nodesleft == 0) && (lp->orig_lowbo[BB->varno] != 0))
            return( OKstatus );
          restore_bounds(lp, TRUE, FALSE);
        }
        SOS_unmark(lp->SOS, 0, k);
      }

      /* Set new SOS marker */
      if(BB->isfloor)
        SOS_set_marked(lp->SOS, 0, k, (MYBOOL) (BB->UPbound != 0));
      else {
        SOS_set_marked(lp->SOS, 0, k, TRUE);
        if(SOS_fix_unmarked(lp->SOS, k, 0, BB->upbo, 0, TRUE, NULL) < 0)
          return( OKstatus );
      }
    }

    /* Special GUB handling (three branches):
       1) Undo and set new marker for k,
       2) Restore upper bounds of the left/right/all non-k variables
          set to 0 in the previous branch
       3) Set new upper bounds for the non-k variables (k is set later) */
    else if(BB->isGUB) {

      /* First restore entering upper bounds and undo previous marker */
      if(BB->nodessolved > 0) {
        restore_bounds(lp, TRUE, BB->isfloor);
        SOS_unmark(lp->GUB, 0, k);
      }

      /* Make sure we take floor bound twice */
      if((BB->nodesleft == 0) && !BB->isfloor)
        BB->isfloor = !BB->isfloor;

      /* Handle two floor instances;
         (selected variable and left/right halves of non-selected variables at 0) */
      SOS_set_marked(lp->GUB, 0, k, (MYBOOL) !BB->isfloor);
      if(BB->isfloor) {
        if(SOS_fix_GUB(lp->GUB, k, 0, BB->upbo, (MYBOOL) (BB->nodesleft > 0)) < 0)
          return( OKstatus );
      }
      /* Handle one ceil instance;
         (selected variable at 1, all other at 0) */
      else {
        if(SOS_fix_unmarked(lp->GUB, k, 0, BB->upbo, 0, TRUE, NULL) < 0)
          return( OKstatus );
      }
    }

    OKstatus = TRUE;

  }
  /* Initialize simplex status variables */
  if(OKstatus) {
    lp->bb_totalnodes++;
    BB->nodestatus = NOTRUN;
    BB->noderesult = lp->infinite;
  }
  return( OKstatus );
}

STATIC int solve_LP(lprec *lp, BBrec *BB, REAL *upbo, REAL *lowbo)
{
  int   tilted, restored, status;
  REAL  testOF;
  BBrec *perturbed = NULL;

  if(lp->bb_break)
    return(PROCBREAK);

#ifdef Paranoia
  debug_print(lp, "solve_LP: Starting solve for iteration %d, B&B node level %d.\n",
                  lp->total_iter, lp->bb_totalnodes);
  if(lp->bb_trace &&
     !validate_bounds(lp, upbo, lowbo))
    report(lp, SEVERE, "solve_LP: Inconsistent bounds at iteration %d, B&B node level %d.\n",
                       lp->total_iter, lp->bb_totalnodes);
#endif

  /* Copy user-specified entering bounds into lp_solve working bounds;
     note that lp->bb_action must have been set before entering this routine */
  impose_bounds(lp, upbo, lowbo);

  /* Restore previously pushed / saved basis for this level if we are in
     the B&B mode and it is not the first call of the binary tree */
  if((BB->nodessolved > 1) && is_bb_action(lp, ACTION_REINVERT))
    restore_basis(lp);

  /* Solve and possibly handle degeneracy cases via bound relaxations */
  status = RUNNING;
  tilted = 0;
  restored = 0;

  while(status == RUNNING) {

    /* Copy user-specified entering bounds into lp_solve working bounds and run */
    status = spx_run(lp);
    lp->bb_status = status;

    if(tilted < 0)
      break;

    else if((status == OPTIMAL) && (tilted > 0)) {
    /* Restore original pre-perturbed problem bounds, and solve again using the basis
       found for the perturbed problem; also make sure we rebase and reinvert. */
      free_BB(&perturbed);
      if(lp->spx_trace)
        report(lp, DETAILED, "solve_LP: Restoring relaxed bounds.\n");
      impose_bounds(lp, upbo, lowbo);
      lp->bb_action = ACTION_ACTIVE | ACTION_REINVERT | ACTION_REBASE | ACTION_RECOMPUTE;
      BB->UBzerobased = FALSE;
      if(lp->bb_totalnodes == 0)
        lp->real_solution = lp->infinite;
      status = RUNNING;
      tilted = 0;
      restored++;
    }

#ifdef EnableAntiDegen
    else if(((lp->bb_level <= 1) || is_anti_degen(lp, ANTIDEGEN_DURINGBB)) &&
            (((status == LOSTFEAS) && is_anti_degen(lp, ANTIDEGEN_LOSTFEAS)) ||
             ((status == INFEASIBLE) && is_anti_degen(lp, ANTIDEGEN_INFEASIBLE)) ||
             ((status == NUMFAILURE) && is_anti_degen(lp, ANTIDEGEN_NUMFAILURE)) ||
             ((status == DEGENERATE) && is_anti_degen(lp, ANTIDEGEN_STALLING)))) {
     /* Allow up to .. consecutive relaxations for non-B&B phases */
      if((tilted <= DEF_MAXRELAX) && (restored <= DEF_MAXRELAX)) {

        /* Create working copy of ingoing bounds if this is the first perturbation */
        if(tilted == 0)
          perturbed = create_BB(lp, BB, upbo, lowbo);

        /* Perturb/shift variable bounds; also make sure we rebase and reinvert */
        perturb_bounds(lp, perturbed, TRUE, (MYBOOL) (tilted > 0));
        impose_bounds(lp, perturbed->upbo, perturbed->lowbo);
        lp->bb_action = ACTION_ACTIVE | ACTION_REINVERT | ACTION_REBASE | ACTION_RECOMPUTE;
        BB->UBzerobased = FALSE;
        status = RUNNING;
        tilted++;
        if(lp->spx_trace)
          report(lp, DETAILED, "solve_LP: Starting bound relaxation number %d (reason=%d)\n",
                               tilted, status);
      }
      else if(lp->spx_trace) {
        report(lp, DETAILED, "solve_LP: Relaxation limit exceeded in resolving infeasibility\n");
        free_BB(&perturbed);
      }
    }
#endif
    
#ifndef FinalOptimalErrorLimitation
    else
      /* Reduce likelihood of numeric errors in slack variables identified by check_solution;
         assume acceptable error accumulation within 1/4th of pivot limit per refactorization */
      if((lp->bb_totalnodes == 0) && (status == OPTIMAL) &&
         (lp->bfp_pivotcount(lp) > MIN(10, lp->bfp_pivotmax(lp)/4)))
        invert(lp, INITSOL_USEZERO);
#endif

  }

  /* Handle the different simplex outcomes */
  if(status != OPTIMAL) {
    lp->bb_parentOF = lp->infinite;
    if((status == USERABORT) || (status == TIMEOUT)) {
      /* Construct the last feasible solution, if available */
      if((lp->solutioncount == 0) &&
         ((lp->simplex_mode & (SIMPLEX_Phase2_PRIMAL | SIMPLEX_Phase2_DUAL)) > 0)) {
        lp->solutioncount++;
        construct_solution(lp, NULL);
        transfer_solution(lp, TRUE);
      }
      /* Return messages */
      report(lp, NORMAL, "\nlp_solve optimization was stopped %s.\n",
                         ((status == USERABORT) ? "by the user" : "due to time-out"));
    }
    else if(BB->varno == 0)
      report(lp, NORMAL, "The model %s\n",
      (status == UNBOUNDED) ? "is UNBOUNDED" :
      ((status == INFEASIBLE) ? "is INFEASIBLE" : "FAILED"));
  }
  
  else { /* ... there is a good solution */
    construct_solution(lp, NULL);
#ifdef Paranoia
    debug_print(lp, "solve_LP: A valid solution was found\n");
    debug_print_solution(lp);
#endif
    if((lp->bb_level <= 1) && (restored > 0))
      report(lp, NORMAL, "%s numerics encountered; validate accuracy\n",
                 (restored == 1) ? "Difficult" : "Severe");
    /* Hande case where a user bound on the OF was found to 
       have been set tool aggressively, giving an infeasible model */
    if(lp->spx_status != OPTIMAL)  
      status = lp->spx_status;
      
    else if((lp->bb_totalnodes == 0) && (MIP_count(lp) > 0)) {
      if(lp->lag_status != RUNNING) {
        report(lp, NORMAL, "\nRelaxed solution  " MPSVALUEMASK " found as B&B basis.\n",
                           lp->solution[0]);
        report(lp, NORMAL, " \n");
      }
      if((lp->usermessage != NULL) && (lp->msgmask & MSG_LPOPTIMAL))
        lp->usermessage(lp, lp->msghandle, MSG_LPOPTIMAL);
      set_varpriority(lp);
    }

   /* Check if we have a numeric problem (an earlier version of this code used the
      absolute difference, but it is not robust for large valued OFs) */
    testOF = my_chsign(is_maxim(lp), my_reldiff(lp->solution[0], lp->real_solution));
    if(testOF < -lp->epsprimal) {
      report(lp, DETAILED, "solve_LP: A MIP subproblem returned a value better than the base\n");
      status = INFEASIBLE;
      lp->spx_status = status;
      lp->doInvert = TRUE;
      lp->doRebase = TRUE;
      lp->doRecompute = TRUE;
    }
    else if(testOF < 0)  /* Avoid problems later (could undo integer roundings, but usually Ok) */
      lp->solution[0] = lp->real_solution;

  }

  /* status can have the following values:
     OPTIMAL, SUBOPTIMAL, TIMEOUT, USERABORT, PROCFAIL, UNBOUNDED and INFEASIBLE. */

  return( status );
} /* solve_LP */

STATIC BBrec *findself_BB(BBrec *BB)
{
  int   varno = BB->varno, vartype = BB->vartype;

  BB = BB->parent;
  while((BB != NULL) && (BB->vartype != vartype) && (BB->varno != varno))
    BB = BB->parent;
  return( BB );
}

STATIC MYBOOL mergeshadow_BB(BBrec *BB)
{
  BBrec *childBB, *predBB = findself_BB(BB);

  if(predBB == NULL)
    return( FALSE );

  else {
    /* First copy dominant bound from earlier self BB instance */
    if(predBB->UPbound < BB->UPbound)
      BB->UPbound = predBB->UPbound;
    if(predBB->LObound > BB->LObound)
      BB->LObound = predBB->LObound;
    /* Link relevant child bounds to parent bounds */
    if(predBB->parent != NULL) {
      childBB = predBB->child;
      while(childBB != BB) {
        if(childBB->UB_base == BB->upbo)
          childBB->UB_base = predBB->parent->UB_base;
        if(childBB->LB_base == BB->lowbo)
          childBB->LB_base = predBB->parent->LB_base;
        childBB = childBB->child;
      }
    }
    /* Then simply remove the earlier self instance */
    pop_BB(predBB);

    return( TRUE );
  }
}

STATIC MYBOOL findnode_BB(BBrec *BB, int *varno, int *vartype, int *varcus)
{
  int    firstnint, lastnint, countnint;
  REAL   tmpreal, varsol;
  MYBOOL is_better = FALSE, is_equal = FALSE;
  lprec  *lp = BB->lp;

  /* Initialize result and return variables */
  *varno = 0;
  *vartype = BB_REAL;
  *varcus = 0;
  countnint = 0;
  BB->nodestatus = lp->spx_status;
  BB->noderesult = lp->solution[0];

  /* If this solution is worse than the best so far, this branch dies.
     If we can only have integer OF values, and we only need the first solution
     then the OF must be at least (unscaled) 1 better than the best so far */
  if((lp->bb_limitlevel != 0) && (MIP_count(lp) > 0)) {

    /* Check that we don't have a limit on the recursion level; two versions supported:
        1) Absolute B&B level (bb_limitlevel > 0), and
        2) B&B level relative to the "B&B order" (bb_limitlevel < 0). */
    lastnint =  lp->sos_vars + lp->sc_count;
    if((lp->bb_limitlevel > 0) && (lp->bb_level > lp->bb_limitlevel+lastnint))
      return( FALSE );
    else if((lp->bb_limitlevel < 0) && 
            (lp->bb_level > 2*(lp->int_count+lastnint)*abs(lp->bb_limitlevel))) {
      if(lp->bb_limitlevel == DEF_BB_LIMITLEVEL)
        report(lp, IMPORTANT, "findnode_BB: Default B&B limit reached at %d; optionally change strategy or limit.\n\n",
                              lp->bb_level);
      return( FALSE );
    }

    /* First initialize or update pseudo-costs from previous optimal solution */
    if(BB->varno == 0) {
      varsol = lp->infinite;
      if((lp->int_count+lp->sc_count > 0) && (lp->bb_PseudoCost == NULL))
        lp->bb_PseudoCost = init_PseudoCost(lp);
    }
    else {
      varsol = lp->solution[BB->varno];
      if( ((lp->int_count > 0) && (BB->vartype == BB_INT)) ||
          ((lp->sc_count > 0) && (BB->vartype == BB_SC) && !is_int(lp, BB->varno-lp->rows)) )
        update_PseudoCost(lp, BB->varno-lp->rows, BB->vartype, BB->isfloor, varsol);
    }

    /* Make sure we don't have numeric problems (typically due to integer scaling) */
    tmpreal = lp->mip_absgap;
    if((lp->bb_totalnodes == 0) &&
            (my_chsign(is_maxim(lp), lp->solution[0]-lp->real_solution) < -tmpreal)) {
      if(lp->bb_trace)
        report(lp, IMPORTANT, "findnode_BB: Simplex failure due to loss of numeric accuracy\n");
      lp->spx_status = NUMFAILURE;
      return( FALSE );
    }

    /* Abandon this branch if the solution is worse than the previous best MIP solution */
    tmpreal = MAX(tmpreal, lp->bb_deltaOF - lp->epsprimal);
    if((BB->lp->solutioncount > 0) &&
       ((my_chsign(is_maxim(lp), my_reldiff(lp->solution[0],lp->best_solution[0])) > -lp->mip_relgap) ||
        (my_chsign(is_maxim(lp), lp->solution[0]-lp->best_solution[0]) > -tmpreal))) {
      return( FALSE );
    }

    /* Check if solution contains enough satisfied INT, SC and SOS variables */
    firstnint = 1;
    lastnint  = lp->columns;

    /* Collect violated SC variables (since they can also be real-valued); the
	     approach is to get them out of the way, since a 0-value is assumed to be "cheap" */
    if(lp->sc_count > 0) {
      *varno = find_sc_bbvar(lp, &countnint);
      if(*varno > 0)
        *vartype = BB_SC;
    }

    /* Look among SOS variables if no SC candidate was found */
    if((SOS_count(lp) > 0) && (*varno == 0)) {
      *varno = find_sos_bbvar(lp, &countnint, FALSE);
      if(*varno < 0)
	      *varno = 0;
      else if(*varno > 0)
        *vartype = BB_SOS;
    }

    /* Then collect INTS that are not integer valued, and verify bounds */
    if((lp->int_count > 0) && (*varno == 0)) {
      *varno = find_int_bbvar(lp, &countnint, BB->lowbo, BB->upbo, &firstnint, &lastnint);
      if(*varno > 0)
        *vartype = BB_INT;
    }

    /* Check if the current MIP solution is optimal; equal or better */
    if(*varno == 0) {
      is_better = (MYBOOL) (my_chsign(is_maxim(lp), lp->solution[0]-lp->best_solution[0]) < -tmpreal);
      is_equal  = !is_better && (MYBOOL)
                  (fabs(my_reldiff(lp->solution[0],lp->best_solution[0])) <= lp->mip_relgap);

      if(is_equal) {
        if((lp->solutionlimit <= 0) || (lp->solutioncount < lp->solutionlimit)) {
          lp->solutioncount++;
          lp->bb_solutionlevel = MIN(lp->bb_solutionlevel, lp->bb_level);
          if((lp->usermessage != NULL) && (lp->msgmask & MSG_MILPEQUAL))
            lp->usermessage(lp, lp->msghandle, MSG_MILPEQUAL);
        }
      }

      /* Current solution is better */
      else if(is_better) {

        /* Update grand total solution count and check if we should go from
           depth-first to best-first variable selection mode */
        if(lp->bb_varactive != NULL) {
          lp->bb_varactive[0]++;
          if((lp->bb_varactive[0] == 1) &&
             is_bb_mode(lp, NODE_DEPTHFIRSTMODE) && is_bb_mode(lp, NODE_DYNAMICMODE))
            lp->bb_rule &= !NODE_DEPTHFIRSTMODE;
        }

        lp->bb_status = FEASFOUND;
        lp->solutioncount = 1;
        if(lp->bb_trace ||
           ((lp->verbose>=NORMAL) && (lp->print_sol == FALSE) && (lp->lag_status != RUNNING))) {
          report(lp, IMPORTANT,
                 "%s solution " MPSVALUEMASK " found at iteration %7d, %6d nodes explored (gap %.1f%%)\n",
                 (my_chsign(is_maxim(lp), lp->best_solution[0]) >= lp->infinite) ? "Feasible" : "Improved",
                 lp->solution[0], lp->total_iter, lp->bb_totalnodes, 100*fabs(my_reldiff(lp->solution[0], lp->bb_limitOF)));
        }
        if((lp->usermessage != NULL) && (MIP_count(lp) > 0)) {
          if((lp->msgmask & MSG_MILPFEASIBLE) &&
             (my_chsign(is_maxim(lp), lp->best_solution[0]) >= lp->infinite))
            lp->usermessage(lp, lp->msghandle, MSG_MILPFEASIBLE);
          else if((lp->msgmask & MSG_MILPBETTER) && (lp->msgmask & MSG_MILPBETTER))
            lp->usermessage(lp, lp->msghandle, MSG_MILPBETTER);
        }

        lp->bb_solutionlevel = lp->bb_level;
        if(lp->break_at_first ||
           (!(fabs(lp->break_at_value) == lp->infinite) &&
            (my_chsign(is_maxim(lp), /* lp->best_solution[0] */ lp->solution[0]-lp->break_at_value) <= 0))) /* Was > until v5.0.6.0 */
          lp->bb_break = TRUE;
      }
    }
  }
  else {
    is_better = TRUE;
    lp->solutioncount = 1;
  }

  /* Transfer the successful solution vector */
  if(is_better || is_equal) {
#ifdef Paranoia
    if((lp->bb_level > 0) &&
       (check_solution(lp, lp->columns, lp->solution, 
                           lp->orig_upbo, lp->orig_lowbo, DEF_EPSSOLUTION) != OPTIMAL)) {
      lp->solutioncount = 0;
      lp->spx_status = NUMFAILURE;
      lp->bb_status = lp->spx_status;
      lp->bb_break = TRUE;
      return( FALSE );
    }
#endif
    transfer_solution(lp, (MYBOOL) ((lp->do_presolve & PRESOLVE_LASTMASKMODE) != PRESOLVE_NONE));
    if((MIP_count(lp) > 0) && (lp->bb_totalnodes > 0)) {
      if ((!calculate_duals(lp)) ||
          (is_presolve(lp, PRESOLVE_SENSDUALS) &&
           (!calculate_sensitivity_duals(lp) || !calculate_sensitivity_obj(lp))
          )
         ) {
        report(lp, IMPORTANT, "findnode_BB: Unable to allocate working memory for duals.\n");
/*        lp->spx_status = NOMEMORY; */
      }
    }
    if(lp->print_sol != FALSE) {
      print_objective(lp);
      print_solution(lp, 1);
    }
  }

  /* Do tracing and determine if we have arrived at the estimated lower MIP limit */
  *varcus = countnint;
  if(MIP_count(lp) > 0) {
    if((lp->solutioncount == 1) && (lp->solutionlimit == 1) &&
       ((fabs(lp->bb_limitOF-lp->best_solution[0]) < lp->mip_absgap) ||
        (fabs(my_reldiff(lp->bb_limitOF, lp->best_solution[0])) < lp->mip_relgap))) {
      lp->bb_break = TRUE;
      return( FALSE );
    }
    else if(lp->bb_level > 0)
      if(lp->spx_trace)
        report(lp, DETAILED, "B&B level %d OPT %s value " MPSVALUEMASK "\n",
                             lp->bb_level, (*varno) ? "   " : "INT", lp->solution[0]);
    return( (MYBOOL) (*varno > 0));
  }
  else
    return( FALSE );

}

STATIC int solve_BB(BBrec *BB, REAL **UB_active, REAL **LB_active)
{
  int   K, status;
  lprec *lp = BB->lp;

  /* Protect against infinite recursions do to integer rounding effects */
  status = PROCFAIL;

  /* Shortcut variables, set default bounds */
  K = BB->varno;
  *UB_active = BB->upbo;
  *LB_active = BB->lowbo;

  /* Load MIP bounds */
  if(K > 0) {

    /* Set inverse and solution vector update parameters */
#ifdef FastMIP
    lp->bb_action = ACTION_ACTIVE | ACTION_RECOMPUTE;
    if((lp->bb_totalnodes == 0) || (lp->spx_status == OPTIMAL))
      lp->bb_action |= ACTION_REINVERT | ACTION_REBASE;
    else {
      if((BB->vartype == BB_SOS) ||
         (!lp->is_basic[K] && ((lp->is_lower[K] && !BB->isfloor) ||
                               (!lp->is_lower[K] && BB->isfloor)) ))
        lp->bb_action |= ACTION_REBASE;
    }
#else
    lp->bb_action = ACTION_ACTIVE | ACTION_REINVERT | ACTION_REBASE | ACTION_RECOMPUTE;
#endif

    /* BRANCH_FLOOR: Force the variable to be smaller than the B&B upper bound */
    if(BB->isfloor) {

      (*UB_active)[K] = BB->UPbound;
      *LB_active = BB->LB_base;

    }

    /* BRANCH_CEILING: Force the variable to be greater than the B&B lower bound */
    else {

      if(!(BB->isSOS && (BB->vartype != BB_INT)) && !BB->isGUB)
        *UB_active = BB->UB_base;
      (*LB_active)[K] = BB->LObound;

    }

    /* Update MIP node count */
    BB->nodessolved++;

  }

  /* Solve! */
  status = solve_LP(lp, BB, *UB_active, *LB_active);
  return( status );
}

/* This is the non-recursive B&B driver routine - beautifully simple, yet so subtle! */
STATIC int run_BB(lprec *lp)
{
  BBrec *currentBB;
  REAL  *UB_active, *LB_active;
  int   varno, vartype, varcus, prevsolutions;
  int   status = NOTRUN;

  /* Initialize */  
  prevsolutions = lp->solutioncount;
  lp->rootbounds = currentBB = push_BB(lp, NULL, 0, BB_REAL, NULL, NULL, 0);

  /* Perform the branch & bound loop */
  while(lp->bb_level > 0) {

    status = solve_BB(currentBB, &UB_active, &LB_active);

    if((status == OPTIMAL) && findnode_BB(currentBB, &varno, &vartype, &varcus))
      currentBB = push_BB(lp, currentBB, varno, vartype, UB_active, LB_active, varcus);

    else while((lp->bb_level > 0) && !nextbranch_BB(currentBB))
      currentBB = pop_BB(currentBB);

  }

  /* Check if we should adjust status */
  if(lp->solutioncount > prevsolutions) {
    if((status == PROCBREAK) || (status == USERABORT) || (status == TIMEOUT))
      status = SUBOPTIMAL;
    else
      status = OPTIMAL;
    if(lp->bb_totalnodes > 0)
      lp->spx_status = OPTIMAL;
  }
  return( status );
}

