
#include <string.h>
#include "commonlib.h"
#include "lp_lib.h"
#include "lp_SOS.h"

#ifdef FORTIFY
# include "lp_fortify.h"
#endif


/*
    Specially Ordered Set (SOS) routines - w/interface for lp_solve v5.0+
   ----------------------------------------------------------------------------------
    Author:        Kjell Eikland
    Contact:       kjell.eikland@broadpark.no
    License terms: LGPL.

    Requires:      lp_lib.h

    Release notes:
    v1.0    1 September 2003    Complete package for SOS creation and use in a LP
                                setting.  Notable feature of this implementation
                                compared to those in other commercial systems is
                                the generalization to SOS'es of "unlimited" order.
    v1.1     8 December 2003    Added variable (index) deletion method.

   ----------------------------------------------------------------------------------
*/

/* SOS group functions */
STATIC SOSgroup *create_SOSgroup(lprec *lp)
{
  SOSgroup *group;

  group = (SOSgroup *) calloc(1, sizeof(*group));
  group->lp = lp;
  group->sos_alloc = SOS_START_SIZE;
  group->sos_list = (SOSrec **) malloc((group->sos_alloc) * sizeof(*group->sos_list));
  return(group);
}

STATIC void resize_SOSgroup(SOSgroup *group)
{
  if(group->sos_count == group->sos_alloc) {
    group->sos_alloc = (int)((double) group->sos_alloc*RESIZEFACTOR);
    group->sos_list = (SOSrec **) realloc(group->sos_list,
                                          (group->sos_alloc) * sizeof(*group->sos_list));
  }
}

STATIC int append_SOSgroup(SOSgroup *group, SOSrec *SOS)
{
  int    i, k;
  SOSrec *SOSHold;

  /* Check if we should resize */
  resize_SOSgroup(group);

  /* First append to the end of the list */
  group->sos_list[group->sos_count] = SOS;
  group->sos_count++;
  k = group->sos_count;
  SOS->tagorder = k;

  /* Sort the SOS list by given priority */
  for(i = group->sos_count-1; i > 0; i--) {
    if(group->sos_list[i]->priority < group->sos_list[i-1]->priority) {
      SOSHold = group->sos_list[i];
      group->sos_list[i] = group->sos_list[i-1];
      group->sos_list[i-1] = SOSHold;
      if(SOSHold == SOS)
        k = i-1;
    }
    else
      break;
  }
  /* Return the list index of the new SOS */
  return( k );
}


STATIC int clean_SOSgroup(SOSgroup *group)
{
  int    i, n;
  SOSrec *SOS;
  
  if(group == NULL)
    return( 0 );
    
  /* Delete any SOS without members */  
  n = 0;
  if(group->sos_alloc > 0) {
    for(i = group->sos_count; i > 0; i--) {
      SOS = group->sos_list[i-1];
      if(SOS->members[0] == 0) {
        delete_SOSrec(group, i);
        n++;
      }  
    }    
  }
  return( n );
}


STATIC void free_SOSgroup(SOSgroup **group)
{
  int i;

  if((group == NULL) || (*group == NULL))
    return;
  if((*group)->sos_alloc > 0) {
    for(i = 0; i < (*group)->sos_count; i++)
      free_SOSrec((*group)->sos_list[i]);
    FREE((*group)->sos_list);
  }
  FREE(*group);
}

/* SOS record functions */
STATIC SOSrec *create_SOSrec(SOSgroup *group, char *name, int type, int priority, int size, int *variables, REAL *weights)
{
  SOSrec *SOS;

  SOS = (SOSrec *) calloc(1 , sizeof(*SOS));
  SOS->parent = group;
  SOS->type = type;
  if(name == NULL)
    SOS->name = NULL;
  else
  {
    allocCHAR(group->lp, &SOS->name, (int) (strlen(name)+1), FALSE);
    strcpy(SOS->name, name);
  }
  if(type < 0)
    type = abs(type);
  SOS->tagorder = 0;
  SOS->size = 0;
  SOS->priority = priority;
  SOS->members = NULL;
  SOS->weights = NULL;
  SOS->membersSorted = NULL;
  SOS->membersMapped = NULL;

  if(size > 0)
    size = append_SOSrec(SOS, size, variables, weights);

  return(SOS);
}


STATIC int append_SOSrec(SOSrec *SOS, int size, int *variables, REAL *weights)
{
  int   i, oldsize, newsize, nn;
  lprec *lp = SOS->parent->lp;

  oldsize = SOS->size;
  newsize = oldsize + size;
  nn = abs(SOS->type);

 /* Shift existing active data right (normally zero) */
  if(SOS->members == NULL)
    allocINT(lp, &SOS->members, 1+newsize+1+nn, TRUE);
  else {
    allocINT(lp, &SOS->members, 1+newsize+1+nn, AUTOMATIC);
    for(i = newsize+1+nn; i > newsize+1; i--)
	  SOS->members[i] = SOS->members[i-size];
  }
  SOS->members[0] = newsize;
  SOS->members[newsize+1] = nn;

 /* Copy the new data into the arrays */
  if(SOS->weights == NULL)
    allocREAL(lp, &SOS->weights, 1+newsize, TRUE);
  else
    allocREAL(lp, &SOS->weights, 1+newsize, AUTOMATIC);
  for(i = oldsize+1; i <= newsize; i++) {
    SOS->members[i] = variables[i-oldsize-1];
    if((SOS->members[i] < 1) || (SOS->members[i] > lp->columns))
      report(lp, IMPORTANT, "append_SOS_rec: Invalid SOS variable definition index %d\n", SOS->members[i]);
    else {
      if(SOS->isGUB)
        lp->must_be_int[SOS->members[i]] |= ISGUB;
      else
        lp->must_be_int[SOS->members[i]] |= ISSOS;
    }
    if(weights == NULL)
      SOS->weights[i] = i;  /* Follow standard, which is sorted ascending */
    else
      SOS->weights[i] = weights[i-oldsize-1];
    SOS->weights[0] += SOS->weights[i];
  }

 /* Sort the new paired lists ascending by weight (simple bubble sort) */
  i = sortByREAL(SOS->members, SOS->weights, newsize, 1, TRUE);
  if(i > 0)
    report(lp, CRITICAL, "Invalid SOS variable weight at index %d\n", i);

 /* Define mapping arrays to search large SOS's faster */
  allocINT(lp, &SOS->membersSorted, newsize, AUTOMATIC);
  allocINT(lp, &SOS->membersMapped, newsize, AUTOMATIC);
  for(i = oldsize+1; i <= newsize; i++) {
    SOS->membersSorted[i - 1] = SOS->members[i];
    SOS->membersMapped[i - 1] = i;
  }
  sortByINT(SOS->membersMapped, SOS->membersSorted, newsize, 0, TRUE);

 /* Confirm the new size */
  SOS->size = newsize;

  return(newsize);

}


STATIC int make_SOSchain(lprec *lp, MYBOOL forceresort)
{
  int      i, j, k, n;
  REAL     *order, sum, weight;
  SOSgroup *group = lp->SOS;

  /* Resort individual SOS member lists, if specified */
  if(forceresort)
    SOS_sort_members(group, 0);

  /* Tally SOS variables and create master SOS variable list */
  n = 0;
  for(i = 0; i < group->sos_count; i++)
    n += group->sos_list[i]->size;
  lp->sos_vars = n;
  if(lp->sos_vars > 0) /* Prevent memory loss in case of multiple solves */
    FREE(lp->sos_priority);
  allocINT(lp, &lp->sos_priority, n, FALSE);
  allocREAL(lp, &order, n, FALSE);

  /* Move variable data to the master SOS list and sort */
  n = 0;
  sum = 0;
  for(i = 0; i < group->sos_count; i++) {
    for(j = 1; j <= group->sos_list[i]->size; j++) {
      lp->sos_priority[n] = group->sos_list[i]->members[j];
      weight = group->sos_list[i]->weights[j];
      sum += weight;
      order[n] = sum;
      n++;
    }
  }
  i = sortByREAL(lp->sos_priority, order, n, 0, FALSE);

  /* Remove duplicate SOS variables */
  for(i = 0; i < n; i++) {
    /* Scan forward to look for duplicate variables */
    for(j = i+1; j < n; j++) {
      if(lp->sos_priority[i] == lp->sos_priority[j]) {
        /* Duplicate found, shrink the tail end of the list */
        for(k = j+1; k < n; k++)
          lp->sos_priority[k-1] = lp->sos_priority[k];
        n--;
      }
    }
  }

  /* Adjust the size of the master variable list, if necessary */
  if(n < lp->sos_vars) {
    allocINT(lp, &lp->sos_priority, n, AUTOMATIC);
    lp->sos_vars = n;
  }

  free(order);
  return(n);

}


STATIC MYBOOL delete_SOSrec(SOSgroup *group, int sosindex)
{
#ifdef Paranoia
  if((sosindex <= 0) || (sosindex > group->sos_count)) {
    report(group->lp, IMPORTANT, "delete_SOSrec: Invalid SOS index %d\n", sosindex);
    return(FALSE);
  }
#endif

  /* Delete and free the SOS record */
  free_SOSrec(group->sos_list[sosindex-1]);
  while(sosindex < group->sos_count) {
    group->sos_list[sosindex-1] = group->sos_list[sosindex];
    sosindex++;
  }
  group->sos_count--;
  return(TRUE);
}


STATIC void free_SOSrec(SOSrec *SOS)
{
  if(SOS->name != NULL)
    free(SOS->name);
  if(SOS->size > 0) {
    free(SOS->members);
    free(SOS->weights);
    free(SOS->membersSorted);
    free(SOS->membersMapped);
  }
  free(SOS);
}


STATIC MYBOOL SOS_sort_members(SOSgroup *group, int sosindex)
/* Routine to (re-)sort SOS member arrays for faster access to large SOSes */
{
  int    i, n;
  int    *list;
  lprec  *lp = group->lp;
  SOSrec *SOS;

#ifdef Paranoia
  if((sosindex < 0) || (sosindex > group->sos_count)) {
    report(lp, IMPORTANT, "SOS_sort_members: Invalid SOS index %d\n", sosindex);
    return(FALSE);
  }
#endif

  if((sosindex == 0) && (group->sos_count == 1))
    sosindex = 1;

  if(sosindex == 0) {
    for(i = 1; i <= group->sos_count; i++) {
      if(!SOS_sort_members(group, i))
        return(FALSE);
    }
  }
  else {
    SOS = group->sos_list[sosindex-1];
    list = SOS->members;
    n = list[0];
    /* Make sure that the arrays are properly allocated and sized */
    if(n != group->sos_list[sosindex-1]->size) {
      allocINT(lp, &SOS->membersSorted, n, AUTOMATIC);
      allocINT(lp, &SOS->membersMapped, n, AUTOMATIC);
      group->sos_list[sosindex-1]->size = n;
    }
    /* Reload the arrays and do the sorting */
    for(i = 1; i <= n; i++) {
      SOS->membersSorted[i - 1] = list[i];
      SOS->membersMapped[i - 1] = i;
    }
    sortByINT(SOS->membersMapped, SOS->membersSorted, n, 0, TRUE);
  }
  return( TRUE );
}


STATIC MYBOOL SOS_shift_col(SOSgroup *group, int sosindex, int column, int delta, MYBOOL forceresort)
/* Routine to adjust SOS indeces for variable insertions or deletions;
   Note: SOS_shift_col must be called before make_SOSchain! */
{
  int    i, ii, n, nn, nr;
  int    changed;
  int    *list;
  REAL   *weights;

#ifdef Paranoia
  lprec  *lp = group->lp;

  if((sosindex < 0) || (sosindex > group->sos_count)) {
    report(lp, IMPORTANT, "SOS_shift_col: Invalid SOS index %d\n", sosindex);
    return(FALSE);
  }
  else if((column < 1) || (delta == 0)) {
    report(lp, IMPORTANT, "SOS_shift_col: Invalid column %d specified with delta %d\n",
                          column, delta);
    return(FALSE);
  }
#endif

  if((sosindex == 0) && (group->sos_count == 1))
    sosindex = 1;

  if(sosindex == 0) {
    for(i = 1; i <= group->sos_count; i++) {
      if(!SOS_shift_col(group, i, column, delta, forceresort))
        return(FALSE);
    }
  }
  else {
    list = group->sos_list[sosindex-1]->members;
    weights = group->sos_list[sosindex-1]->weights;
    n = list[0];
    nn = list[n+1];

    /* Case where variable indeces are to be incremented */
    if(delta > 0) {
      for(i = 1; i <= n; i++) {
        if(list[i] >= column)
          list[i] += delta;
      }
    }
    /* Case where variables are to be deleted/indeces decremented */
    else {
      changed = 0;
      for(i = 1, ii = 0; i <= n; i++) {
        nr = list[i];
        /* Check if this SOS variable should be deleted */
        if((nr >= column) && (nr < column-delta))
          continue;
        /* If the index is "high" then decrement */
        if(nr > column) {
          changed++;
          nr += delta;
        }
        ii++;
        list[ii] = nr;
        weights[ii] = weights[i];
      }
      /* Update the SOS length / type indicators */
      if(ii < n) {
        list[0] = ii;
        list[ii+1] = nn;
      }

     /* Update mapping arrays to search large SOS's faster */
      if(forceresort && ((ii < n) || (changed > 0)))
        SOS_sort_members(group, sosindex);
    }

  }
  return(TRUE);

}


int SOS_get_type(SOSgroup *group, int sosindex)
{
#ifdef Paranoia
  if((sosindex < 1) || (sosindex > group->sos_count)) {
    report(group->lp, IMPORTANT, "SOS_get_type: Invalid SOS index %d\n", sosindex);
    return(FALSE);
  }
#endif

  return(group->sos_list[sosindex-1]->type);
}


int SOS_infeasible(SOSgroup *group, int sosindex)
{
  int    i, n, nn, failindex;
  int    *list;
  lprec  *lp = group->lp;

#ifdef Paranoia
  if((sosindex < 0) || (sosindex > group->sos_count)) {
    report(lp, IMPORTANT, "SOS_infeasible: Invalid SOS index %d\n", sosindex);
    return(FALSE);
  }
#endif

  if(sosindex == 0 && group->sos_count == 1)
    sosindex = 1;

  failindex = 0;
  if(sosindex == 0) {
    for(i = 1; i <= group->sos_count; i++) {
      failindex = SOS_infeasible(group, i);
      if(failindex > 0) break;
    }
  }
  else {
    list = group->sos_list[sosindex-1]->members;
    n = list[0];
    nn = list[n+1];
   /* Find index of next lower-bounded variable */
    for(i = 1; i <= n; i++) {
      if(lp->orig_lowbo[lp->rows + abs(list[i])] > 0)
        break;
    }

   /* Find if there is another lower-bounded variable beyond the type window */
    i = i+nn;
    while(i <= n) {
      if(lp->orig_lowbo[lp->rows + abs(list[i])] > 0)
        break;
      i++;
    }
    if(i <= n)
      failindex = abs(list[i]);
  }
  return(failindex);
}


int SOS_member_index(SOSgroup *group, int sosindex, int member)
{
  int    n;
  SOSrec *SOS;

  SOS = group->sos_list[sosindex-1];
  n = SOS->members[0];

  n = searchFor(member, SOS->membersSorted, n, 0, FALSE);
  if(n >= 0)
    n = SOS->membersMapped[n];

  return(n);
}


int SOS_is_member(SOSgroup *group, int sosindex, int column)
{
  int    i, n = FALSE, *list;
  lprec  *lp;

  if(group == NULL)
    return( FALSE );
  lp = group->lp;

#ifdef Paranoia
  if((sosindex < 0) || (sosindex > group->sos_count)) {
    report(lp, IMPORTANT, "SOS_is_member: Invalid SOS index %d\n", sosindex);
    return(n);
  }
#endif

  if((sosindex == 0) && (group->sos_count == 1))
    sosindex = 1;

  if(sosindex == 0) {
    if(lp->must_be_int[column] & (ISSOS | ISGUB)) {
      for(i = 1; i <= group->sos_count; i++) {
        n = SOS_is_member(group, i, column);
        if(n)
          break;
      }
    }
  }
  else if(lp->must_be_int[column] & (ISSOS | ISGUB)) {

   /* Search for the variable */
    i = SOS_member_index(group, sosindex, column);

   /* Signal active status if found, otherwise return FALSE */
    if(i > 0) {
      list = group->sos_list[sosindex-1]->members;
      if(list[i] < 0)
        n = -TRUE;
      else
	    n = TRUE;
    }
  }
  return(n);
}


MYBOOL SOS_is_member_of_type(SOSgroup *group, int column, int sostype)
{
  int i;

  for(i = 1; i <= group->sos_count; i++) {
	if((SOS_get_type(group, i) == sostype) && SOS_is_member(group, i, column))
      return(TRUE);
  }
  return(FALSE);
}


MYBOOL SOS_set_GUB(SOSgroup *group, int sosindex, MYBOOL state)
{
  int i;

#ifdef Paranoia
  if((sosindex < 0) || (sosindex > group->sos_count)) {
    report(group->lp, IMPORTANT, "SOS_set_GUB: Invalid SOS index %d\n", sosindex);
    return(FALSE);
  }
#endif
  if((sosindex == 0) && (group->sos_count == 1))
    sosindex = 1;

  if(sosindex == 0) {
    for(i = 1; i <= group->sos_count; i++)
      SOS_set_GUB(group, i, state);
  }
  else
    group->sos_list[sosindex-1]->isGUB = state;
  return(TRUE);
}


MYBOOL SOS_is_GUB(SOSgroup *group, int sosindex)
{
  int    i;

#ifdef Paranoia
  if((sosindex < 0) || (sosindex > group->sos_count)) {
    report(group->lp, IMPORTANT, "SOS_is_GUB: Invalid SOS index %d\n", sosindex);
    return(FALSE);
  }
#endif

  if((sosindex == 0) && (group->sos_count == 1))
    sosindex = 1;

  if(sosindex == 0) {
    for(i = 1; i <= group->sos_count; i++) {
      if(SOS_is_GUB(group, i))
        return(TRUE);
    }
    return(FALSE);
  }
  else
    return( group->sos_list[sosindex-1]->isGUB );
}


MYBOOL SOS_is_marked(SOSgroup *group, int sosindex, int column)
{
  int    i, n, *list;
  lprec  *lp;

  if(group == NULL)
    return( FALSE );
  lp = group->lp;

#ifdef Paranoia
  if((sosindex < 0) || (sosindex > group->sos_count)) {
    report(lp, IMPORTANT, "SOS_is_marked: Invalid SOS index %d\n", sosindex);
    return(FALSE);
  }
#endif

  if(!(lp->must_be_int[column] & (ISSOS | ISGUB)))
    return(FALSE);

  if((sosindex == 0) && (group->sos_count == 1))
    sosindex = 1;

  if(sosindex == 0) {
    for(i = 1; i <= group->sos_count; i++) {
      n = SOS_is_marked(group, i, column);
      if(n)
        return(TRUE);
    }
  }
  else  {
    list = group->sos_list[sosindex-1]->members;
    n = list[0];

   /* Search for the variable (normally always faster to do linear search here) */
    column = -column;
    for(i = 1; i <= n; i++)
      if(list[i] == column)
        return(TRUE);
  }
  return(FALSE);
}


MYBOOL SOS_is_active(SOSgroup *group, int sosindex, int column)
{
  int    i, n, nn, *list;
  lprec  *lp = group->lp;

#ifdef Paranoia
  if((sosindex < 0) || (sosindex > group->sos_count)) {
    report(lp, IMPORTANT, "SOS_is_active: Invalid SOS index %d\n", sosindex);
    return(FALSE);
  }
#endif

  if(!(lp->must_be_int[column] & (ISSOS | ISGUB)))
    return(FALSE);

  if((sosindex == 0) && (group->sos_count == 1))
    sosindex = 1;

  if(sosindex == 0) {
    for(i = 1; i <= group->sos_count; i++) {
      n = SOS_is_active(group, i, column);
      if(n)
        return(TRUE);
    }
  }
  else {

    list = group->sos_list[sosindex-1]->members;
    n = list[0]+1;
    nn = list[n];

   /* Scan the active (non-zero) SOS index list */
	for(i = 1; (i <= nn) && (list[n+i] != 0); i++)
      if(list[n+i] == column) return(TRUE);
  }
  return(FALSE);
}


MYBOOL SOS_is_full(SOSgroup *group, int sosindex, int column, MYBOOL activeonly)
{
  int    i, nn, n, *list;
  lprec  *lp = group->lp;

#ifdef Paranoia
  if((sosindex < 0) || (sosindex > group->sos_count)) {
    report(lp, IMPORTANT, "SOS_is_full: Invalid SOS index %d\n", sosindex);
    return(FALSE);
  }
#endif

  if(!(lp->must_be_int[column] & (ISSOS | ISGUB)))
    return(FALSE);

  if((sosindex == 0) && (group->sos_count == 1))
    sosindex = 1;

  if(sosindex == 0) {
    for(i = 1; i <= group->sos_count; i++) {
      if(SOS_is_full(group, i, column, activeonly))
        return(TRUE);
    }
  }
  else if(SOS_is_member(group, sosindex, column)) {

    list = group->sos_list[sosindex-1]->members;
    n = list[0]+1;
    nn = list[n];

   /* Info: Last item in the active list is non-zero if the current SOS is full */
    if(list[n+nn] != 0)
      return(TRUE);

    if(!activeonly) {
      /* Spool to last active item */
      for(i = nn-1; (i > 0) && (list[n+i] == 0); i--);
      if(i > 0) {
        nn = nn - i;
        i = SOS_member_index(group, sosindex, list[n+i]);
        for(; (nn > 0) && (list[i] < 0); i++, nn--);
        if(nn == 0)
          return(TRUE);
      }
    }
  }

  return(FALSE);
}


MYBOOL SOS_can_activate(SOSgroup *group, int sosindex, int column)
{
  int    i, n, nn, *list;
  lprec  *lp;

  if(group == NULL)
    return( FALSE );
  lp = group->lp;

#ifdef Paranoia
  if((sosindex < 0) || (sosindex > group->sos_count)) {
    report(lp, IMPORTANT, "SOS_can_activate: Invalid SOS index %d\n", sosindex);
    return(FALSE);
  }
#endif

  if(!(lp->must_be_int[column] & (ISSOS | ISGUB)))
    return(FALSE);

  if((sosindex == 0) && (group->sos_count == 1))
    sosindex = 1;

  if(sosindex == 0) {
    for(i = 1; i <= group->sos_count; i++) {
      nn = SOS_can_activate(group, i, column);
      if(nn == FALSE)
        return(FALSE);
    }
  }
  else if(SOS_is_member(group, sosindex, column)) {

    list = group->sos_list[sosindex-1]->members;
    n = list[0]+1;
    nn = list[n];

   /* Accept if the SOS is empty */
    if(list[n+1] == 0)
      return(TRUE);

   /* Cannot activate a variable if the SOS is full */
    if(list[n+nn] != 0)
      return(FALSE);

   /* Check if we can set variable active in SOS2..SOSn
     (must check left and right neighbours if one variable is already active) */
    if(nn > 1) {

     /* Find the variable that was last activated;
	   	Also check that the candidate variable is not already active */
      for(i = 1; i <= nn; i++) {
        if(list[n+i] == 0)
          break;
        if(list[n+i] == column)
          return(FALSE);
      }
      i--;
      nn = list[n+i];

     /* SOS accepts an additional variable; confirm neighbourness of candidate;
        Search for the SOS set index of the last activated variable */
      n = list[0];
      for(i = 1; i <= n; i++)
        if(abs(list[i]) == nn) break;
      if(i > n) {
        report(lp, CRITICAL, "SOS_can_activate: Internal index error at SOS %d\n", sosindex);
        return(FALSE);
      }

     /* SOS accepts an additional variable; confirm neighbourness of candidate */

     /* Check left neighbour */
      if((i > 1) && (list[i-1] == column))
        return(TRUE);
     /* Check right neighbour */
      if((i < n) && (list[i+1] == column))
        return(TRUE);

     /* It is not the right neighbour; return false */
      return(FALSE);
    }
  }
  return(TRUE);
}


MYBOOL SOS_set_marked(SOSgroup *group, int sosindex, int column, MYBOOL asactive)
{
  int    i, n, nn, *list;
  lprec  *lp = group->lp;

#ifdef Paranoia
  if((sosindex < 0) || (sosindex > group->sos_count)) {
    report(lp, IMPORTANT, "SOS_set_marked: Invalid SOS index %d\n", sosindex);
    return(FALSE);
  }
#endif

  if(!(lp->must_be_int[column] & (ISSOS | ISGUB)))
    return(FALSE);

  if((sosindex == 0) && (group->sos_count == 1))
    sosindex = 1;

 /* Define an IBM-"SOS3" member variable temporarily as integer, if it is
    not already a permanent integer; is reset in SOS_unmark */
  if(asactive && !is_int(lp, column) && SOS_is_member_of_type(group, column, SOS3)) {
    lp->must_be_int[column] |= ISSOSTEMPINT;
    lp_solve_set_int(lp, column, TRUE);
  }

  if(sosindex == 0) {
    nn = 0;
    for(i = 1; i <= group->sos_count; i++)
      if(SOS_set_marked(group, i, column, asactive))
        nn++;
    return((MYBOOL) (nn == group->sos_count));
  }
  else {
    list = group->sos_list[sosindex-1]->members;
    n = list[0]+1;
    nn = list[n];

   /* Search for the variable */
    i = SOS_member_index(group, sosindex, column);

   /* First mark active in the set member list as used */
    if((i > 0) && (list[i] > 0))
      list[i] *= -1;
    else
      return(TRUE);

   /* Then move the variable to the live list */
    if(asactive) {
      for(i = 1; i <= nn; i++) {
        if(list[n+i] == column)
          return(FALSE);
        else if(list[n+i] == 0) {
          list[n+i] = column;
          return(FALSE);
        }
      }
	  }
    return(TRUE);
  }
}


MYBOOL SOS_unmark(SOSgroup *group, int sosindex, int column)
{
  int    i, n, nn, *list;
  MYBOOL isactive;
  lprec  *lp = group->lp;

#ifdef Paranoia
  if((sosindex < 0) || (sosindex > group->sos_count)) {
    report(lp, IMPORTANT, "SOS_unmark: Invalid SOS index %d\n", sosindex);
    return(FALSE);
  }
#endif

  if(!(lp->must_be_int[column] & (ISSOS | ISGUB)))
    return(FALSE);

  if((sosindex == 0) && (group->sos_count == 1))
    sosindex = 1;

 /* Undefine a SOS3 member variable that has temporarily been set as integer */
  if(lp->must_be_int[column] & ISSOSTEMPINT) {
    lp->must_be_int[column] &= !ISSOSTEMPINT;
	lp_solve_set_int(lp, column, FALSE);
  }

  if(sosindex == 0) {
    nn = 0;
    for(i = 1; i <= group->sos_count; i++)
      if(SOS_unmark(group, i, column))
        nn++;
    return((MYBOOL) (nn == group->sos_count));
  }
  else {
    list = group->sos_list[sosindex-1]->members;
    n = list[0]+1;
    nn = list[n];

   /* Search for the variable */
    i = SOS_member_index(group, sosindex, column);

   /* Restore sign in main list */
    if((i > 0) && (list[i] < 0))
      list[i] *= -1;
    else
      return(TRUE);

   /* Find the variable in the active list... */
    isactive = SOS_is_active(group, sosindex, column);
    if(isactive) {
      for(i = 1; i <= nn; i++)
        if(list[n+i] == column)
          break;
     /* ...shrink the list if found, otherwise return error */
      if(i <= nn) {
        for(; i<nn; i++)
	      list[n+i] = list[n+i+1];
        list[n+nn] = 0;
        return(TRUE);
      }
      return(FALSE);
    }
    else
      return(TRUE);
  }
}


int SOS_fix_unmarked(SOSgroup *group, int variable, int sosindex, REAL *bound, REAL value, MYBOOL isupper, int *diffcount)
{
  int    i, ii, count, n, nn, nLeft, nRight, *list;
  lprec  *lp = group->lp;

#ifdef Paranoia
  if((sosindex < 0) || (sosindex > group->sos_count)) {
    report(lp, IMPORTANT, "SOS_fix_unmarked: Invalid SOS index %d\n", sosindex);
    return(FALSE);
  }
#endif

  if((sosindex == 0) && (group->sos_count == 1))
    sosindex = 1;

  count = 0;
  if(sosindex == 0) {
    for(i = 1; i <= group->sos_count; i++) {
      if(SOS_is_member(group, i, variable))
	      count += SOS_fix_unmarked(group, variable, i, bound, value, isupper, diffcount);
    }
  }
  else {
    list = group->sos_list[sosindex-1]->members;
    n = list[0]+1;

   /* Count the number of active and free SOS variables */
    nn = list[n];
    for(i = 1; i <= nn; i++) {
      if(list[n+i] == 0)
	    break;
    }
    i--;
    i = nn - i;  /* Establish the number of unused slots */

   /* Determine the free SOS variable window */
    if(i == nn) {
      nLeft = 0;
      nRight = SOS_member_index(group, sosindex, variable);
    }
    else {
      nLeft  = SOS_member_index(group, sosindex, list[n+1]);
      if(variable == list[n+1])
        nRight = nLeft;
      else
        nRight = SOS_member_index(group, sosindex, variable);
    }

    nRight += i;  /* Loop (nRight+1)..n */

   /* Fix variables outside of the free SOS variable window */
    for(i = 1; i < n; i++)  {
     /* Skip the SOS variable window */
      if((i >= nLeft) && (i <= nRight))
        continue;
     /* Otherwise proceed to set bound */
      ii = list[i];
      if(ii > 0) {
        ii += lp->rows;
        if(bound[ii] != value) {
         /* Verify that we don't violate original bounds */
          if(isupper && (value < lp->orig_lowbo[ii]))
            return(-ii);
          else if(!isupper && (value > lp->orig_upbo[ii]))
            return(-ii);
         /* OK, set the new bound */
          count++;
          bound[ii] = value;
        }
  	    if((diffcount != NULL) && (lp->solution[ii] != value))
          (*diffcount)++;
      }
    }
  }
  return(count);
}


int SOS_fix_GUB(SOSgroup *group, int variable, int sosindex, REAL *bound, MYBOOL isleft)
{
  int    i, ii, count, n, nn, nLeft, *list;
  REAL   value = 0;
  lprec  *lp = group->lp;

#ifdef Paranoia
  if((sosindex < 0) || (sosindex > group->sos_count)) {
    report(lp, IMPORTANT, "SOS_fix_GUB: Invalid SOS index %d\n", sosindex);
    return(FALSE);
  }
#endif

  if((sosindex == 0) && (group->sos_count == 1))
    sosindex = 1;

  count = 0;
  if(sosindex == 0) {
    for(i = 1; i <= group->sos_count; i++) {
      if(SOS_is_member(group, i, variable))
	      count += SOS_fix_GUB(group, variable, i, bound, isleft);
    }
  }
  else {
    list = group->sos_list[sosindex-1]->members;
    n = list[0];

   /* Count the number of unmarked SOS variables */
    nn = 0;
    for(i = 1; i <= n; i++) {
      ii = list[i];
      if((ii > 0) && (bound[lp->rows+ii] > value))
        nn++;
    }
    if(nn == 0)
      return(count);

   /* Establish the number of unmarked variables in the left window
      (note that "variable" should have been marked previously) */
    nLeft = nn / 2;

   /* Fix variables in the specified variable window */
    nn = 0;
    for(i = 1; (i <= n); i++)  {

     /* Find next unmarked variable */
      ii = list[i];
      if((ii < 0) || (bound[lp->rows+ii] == value))
        continue;
      nn++;

     /* Check if we are in the appropriate window */
      if((isleft && (nn <= nLeft)) ||
         (!isleft && (nn > nLeft))) {

        /* Proceed to set 0-bound */
        ii += lp->rows;
        /* Verify that we don't violate original bounds */
        if(value < lp->orig_lowbo[ii])
          return(-ii);
       /* OK, set the new bound */
        count++;
        bound[ii] = value;
      }
    }
  }
  return(count);
}

int SOS_is_satisfied(SOSgroup *group, int sosindex, REAL *solution)
/* Determine if the SOS is satisfied for the current solution vector;
   The return code is in the range [-2..+2], depending on the type of
   satisfaction.  Positive return value means too many non-zero values,
   negative value means set incomplete:

              -2: Set member count not full (SOS3)
              -1: Set member count not full
               0: Set is full (also returned if the SOS index is invalid)
               1: Too many non-zero sequential variables
               2: Set consistency error

*/
{
  int    i, n, nn, count, *list;
  int    type, status = 0;
  lprec  *lp = group->lp;

#ifdef Paranoia
  if((sosindex < 0) || (sosindex > group->sos_count)) {
    report(lp, IMPORTANT, "SOS_is_satisfied: Invalid SOS index %d\n", sosindex);
    return( 0 );
  }
#endif

  if((sosindex == 0) && (group->sos_count == 1))
    sosindex = 1;

  if(sosindex == 0) {
    for(i = 1; i <= group->sos_count; i++) {
      status = SOS_is_satisfied(group, i, solution);
      if((status != 0) && (status != -1))
        break;
    }
  }
  else {
    type = SOS_get_type(group, sosindex);
    list = group->sos_list[sosindex-1]->members;
    n = list[0]+1;
    nn = list[n];

   /* Count the number of active SOS variables */
    for(i = 1; i <= nn; i++) {
      if(list[n+i] == 0)
        break;
    }
    count = i-1;
    if(count == nn)
      status = 0;    /* Set is full    */
    else
      status = -1;   /* Set is partial */

   /* Find index of the first active variable; fail if some are non-zero */
    if(count > 0) {
      nn = list[n+1];
      for(i = 1; i < n; i++) {
        if(abs(list[i]) == nn || (solution[lp->rows + abs(list[i])] != 0))
          break;
      }
      if(abs(list[i]) != nn)
        status = 2;  /* Set consistency error (leading set variables are non-zero) */
      else {
       /* Scan the active SOS variables; fail if some are zero */
        while(count > 0) {
          if(solution[lp->rows + abs(list[i])] == 0)
            break;
          i++;
          count--;
        }
        if(count > 0)
          status = 2; /* Set consistency error (active set variables are zero) */
      }
    }
    else {
      i = 1;
      /* There are no active variables; see if we have happened to find a valid header */
      while((i < n) && (solution[lp->rows + abs(list[i])] == 0))
        i++;
      count = 0;
      while((i < n) && (count <= nn) && (solution[lp->rows + abs(list[i])] != 0)) {
        count++;
        i++;
      }
      if(count > nn)
        status = 1;   /* Too-many sequential non-zero variables */
    }

   /* Scan the trailing set of SOS variables; fail if some are non-zero */
    if(status <= 0) {
      n--;
      while(i <= n) {
        if(solution[lp->rows + abs(list[i])] != 0)
          break;
        i++;
      }
      if(i <= n)
        status = 1;  /* Too-many sequential non-zero variables */

     /* Code member deficiency for SOS3 separately */
      else if((status == -1) && (type <= SOS3))
        status = -2;
    }

  }
  return(status);
}
