
#include "string.h"
#include "commonlib.h"
#include "lp_lib.h"
#include "lp_utils.h"
#ifdef INTEGERTIME
  #include <time.h>
#else
  #include <sys/timeb.h>
#endif

#ifdef FORTIFY
# include "lp_fortify.h"
#endif


/*
    Miscellaneous utilities as implemented for lp_solve v5.0+
   ----------------------------------------------------------------------------------
    Author:        Kjell Eikland
    Contact:       kjell.eikland@broadpark.no
    License terms: GLPL.

    Requires:      lp_utils.h, lp_lib.h

    Release notes:
    v1.0.0  1 January 2003      Memory allocation, sorting, searching, time and
                                doubly linked list functions.
    v1.1.0  15 May 2004         Added vector packing functionality

   ----------------------------------------------------------------------------------
*/

STATIC MYBOOL allocCHAR(lprec *lp, char **ptr, int size, MYBOOL clear)
{
  if(clear == TRUE)
    *ptr = (char *) calloc(size, sizeof(**ptr));
  else if(clear & AUTOMATIC) {
    *ptr = (char *) realloc(*ptr, size * sizeof(**ptr));
    if(clear & TRUE)
      memset(*ptr, '\0', size * sizeof(**ptr));
  }
  else
    *ptr = (char *) malloc(size * sizeof(**ptr));
  if(((*ptr) == NULL) && (size > 0)) {
    lp->report(lp, CRITICAL, "alloc of %d bytes failed on line %d of file %s\n",
                             size * sizeof(**ptr), __LINE__, __FILE__);
    return( FALSE );
  }
  else
    return( TRUE );
}
STATIC MYBOOL allocMYBOOL(lprec *lp, MYBOOL **ptr, int size, MYBOOL clear)
{
  if(clear == TRUE)
    *ptr = (MYBOOL *) calloc(size, sizeof(**ptr));
  else if(clear & AUTOMATIC) {
    *ptr = (MYBOOL *) realloc(*ptr, size * sizeof(**ptr));
    if(clear & TRUE)
      memset(*ptr, '\0', size * sizeof(**ptr));
  }
  else
    *ptr = (MYBOOL *) malloc(size * sizeof(**ptr));
  if(((*ptr) == NULL) && (size > 0)) {
    lp->report(lp, CRITICAL, "alloc of %d bytes failed on line %d of file %s\n",
                             size * sizeof(**ptr), __LINE__, __FILE__);
    return( FALSE );
  }
  else
    return( TRUE );
}
STATIC MYBOOL allocINT(lprec *lp, int **ptr, int size, MYBOOL clear)
{
  if(clear == TRUE)
    *ptr = (int *) calloc(size, sizeof(**ptr));
  else if(clear & AUTOMATIC) {
    *ptr = (int *) realloc(*ptr, size * sizeof(**ptr));
    if(clear & TRUE)
      memset(*ptr, '\0', size * sizeof(**ptr));
  }
  else
    *ptr = (int *) malloc(size * sizeof(**ptr));
  if(((*ptr) == NULL) && (size > 0)) {
    lp->report(lp, CRITICAL, "alloc of %d bytes failed on line %d of file %s\n",
                             size * sizeof(**ptr), __LINE__, __FILE__);
    return( FALSE );
  }
  else
    return( TRUE );
}
STATIC MYBOOL allocREAL(lprec *lp, REAL **ptr, int size, MYBOOL clear)
{
  if(clear == TRUE)
    *ptr = (REAL *) calloc(size, sizeof(**ptr));
  else if(clear & AUTOMATIC) {
    *ptr = (REAL *) realloc(*ptr, size * sizeof(**ptr));
    if(clear & TRUE)
      memset(*ptr, '\0', size * sizeof(**ptr));
  }
  else
    *ptr = (REAL *) malloc(size * sizeof(**ptr));
  if(((*ptr) == NULL) && (size > 0)) {
    lp->report(lp, CRITICAL, "alloc of %d bytes failed on line %d of file %s\n",
                             size * sizeof(**ptr), __LINE__, __FILE__);
    return( FALSE );
  }
  else
    return( TRUE );
}
STATIC MYBOOL allocLREAL(lprec *lp, LREAL **ptr, int size, MYBOOL clear)
{
  if(clear == TRUE)
    *ptr = (LREAL *) calloc(size, sizeof(**ptr));
  else if(clear & AUTOMATIC) {
    *ptr = (LREAL *) realloc(*ptr, size * sizeof(**ptr));
    if(clear & TRUE)
      memset(*ptr, '\0', size * sizeof(**ptr));
  }
  else
    *ptr = (LREAL *) malloc(size * sizeof(**ptr));
  if(((*ptr) == NULL) && (size > 0)) {
    lp->report(lp, CRITICAL, "alloc of %d bytes failed on line %d of file %s\n",
                             size * sizeof(**ptr), __LINE__, __FILE__);
    return( FALSE );
  }
  else
    return( TRUE );
}

STATIC MYBOOL allocFREE(lprec *lp, void **ptr)
{
  MYBOOL status = TRUE;

  if(*ptr != NULL) {
    free(*ptr);
    *ptr = NULL;
  }
  else {
    status = FALSE;
    lp->report(lp, CRITICAL, "free failed on line %d of file %s\n",
                             __LINE__, __FILE__);
  }
  return(status);
}

REAL *cloneREAL(lprec *lp, REAL *origlist, int size)
{
  REAL *newlist;

  size += 1;
  if(allocREAL(lp, &newlist, size, FALSE))
    MEMCOPY(newlist, origlist, size);
  return(newlist);
}
MYBOOL *cloneMYBOOL(lprec *lp, MYBOOL *origlist, int size)
{
  MYBOOL *newlist;

  size += 1;
  if(allocMYBOOL(lp, &newlist, size, FALSE))
    MEMCOPY(newlist, origlist, size);
  return(newlist);
}
int *cloneINT(lprec *lp, int *origlist, int size)
{
  int *newlist;

  size += 1;
  if(allocINT(lp, &newlist, size, FALSE))
    MEMCOPY(newlist, origlist, size);
  return(newlist);
}

STATIC void roundVector(LREAL *myvector, int endpos, LREAL roundzero)
{
  if(roundzero > 0)
    for(; endpos >= 0; myvector++, endpos--)
      if(fabs(*myvector) < roundzero)
        *myvector = 0;
/*      my_round((*myvector), roundzero); */ /* Experimental */
}

STATIC REAL normalizeVector(REAL *myvector, int endpos)
/* Scale the ingoing vector so that its norm is unit, and return the original length */
{
  int  i;
  REAL SSQ;

  /* Cumulate squares */
  SSQ = 0;
  for(i = 0; i <= endpos; myvector++, i++)
    SSQ += (*myvector) * (*myvector);

  /* Normalize */
  SSQ = sqrt(SSQ);
  if(SSQ > 0)
    for(myvector--; i > 0; myvector--, i--)
      (*myvector) /= SSQ;

  return( SSQ );
}

/* ---------------------------------------------------------------------------------- */
/* Other general utilities                                                            */
/* ---------------------------------------------------------------------------------- */

STATIC void swapINT(int *item1, int *item2)
{
  STATIC int hold = *item1;
  *item1 = *item2;
  *item2 = hold;
}

STATIC void swapREAL(REAL *item1, REAL *item2)
{
  STATIC REAL hold = *item1;
  *item1 = *item2;
  *item2 = hold;
}

STATIC void swapPTR(void **item1, void **item2)
{
  STATIC void *hold;
  hold = *item1;
  *item1 = *item2;
  *item2 = hold;
}


STATIC REAL restoreINT(REAL valREAL, REAL epsilon)
{
  STATIC REAL valINT, fracREAL, fracABS;

  fracREAL = modf(valREAL, &valINT);
  fracABS = fabs(fracREAL);
  if(fracABS < epsilon)
    return(valINT);
  else if(fracABS > 1-epsilon) {
    if(fracREAL < 0)
      return(valINT-1);
    else
      return(valINT+1);
  }
  return(valREAL);
}

STATIC REAL roundToPrecision(REAL value, REAL precision)
{
#if 1
  REAL  vmod;
  int   vexp2, vexp10;
# ifndef __HYPER
#   ifdef WIN32
#     define __HYPER hyper
#   else
#     define __HYPER long long
#   endif
# endif
  __HYPER sign;

  sign  = my_sign(value);
  value = fabs(value);

  /* Round to integer if possible */
  if(value < precision)
    return( 0 );
  else if(value == floor(value))
    return( value*sign );
  else if((value < (REAL) MAXINT64) &&
     (modf((REAL) (value+precision), &vmod) < precision)) {
    sign *= (__HYPER) (value+precision);
    return( (REAL) sign );
  }

  /* Optionally round with base 2 representation for additional precision */
#define roundPrecisionBase2
#ifdef roundPrecisionBase2
  value = frexp(value, &vexp2);
#else
  vexp2 = 0;
#endif

  /* Convert to desired precision */
  vexp10 = (int) log10(value);
  precision *= pow(10.0, vexp10);
  modf(value/precision+0.5, &value);
  value *= sign*precision;

  /* Restore base 10 representation if base 2 was active */
  if(vexp2 != 0)
    value = ldexp(value, vexp2);
#endif

  return( value );
}


/* ---------------------------------------------------------------------------------- */
/* Searching function specialized for lp_solve                                        */
/* ---------------------------------------------------------------------------------- */
STATIC int searchFor(int target, int *attributes, int size, int offset, MYBOOL absolute)
{
  int beginPos, endPos;
  int newPos, match;

 /* Set starting and ending index offsets */
  beginPos = offset;
  endPos = beginPos + size - 1;

 /* Do binary search logic based on a sorted attribute vector */
  newPos = (beginPos + endPos) / 2;
  match = attributes[newPos];
  if(absolute)
    match = abs(match);
  while(endPos - beginPos > LINEARSEARCH) {
    if(match < target) {
	    beginPos = newPos + 1;
      newPos = (beginPos + endPos) / 2;
      match = attributes[newPos];
	    if(absolute)
	      match = abs(match);
    }
	  else if(match > target) {
	    endPos = newPos - 1;
      newPos = (beginPos + endPos) / 2;
      match = attributes[newPos];
      if(absolute)
        match = abs(match);
    }
    else {
      beginPos = newPos;
      endPos = newPos;
    }
  }

 /* Do linear (unsorted) search logic */
  if(endPos - beginPos <= LINEARSEARCH) {
    match = attributes[beginPos];
    if(absolute)
      match = abs(match);
      while((beginPos < endPos) && (match != target)) {
        beginPos++;
        match = attributes[beginPos];
        if(absolute)
          match = abs(match);
      }
      if(match == target)
        endPos = beginPos;
  }

 /* Return the index if a match was found, or signal failure with a -1 */
  if((beginPos == endPos) && (match == target))
    return(beginPos);
  else
    return(-1);

}


/* ---------------------------------------------------------------------------------- */
/* Other supporting math routines                                                     */
/* ---------------------------------------------------------------------------------- */

STATIC MYBOOL isINT(lprec *lp, REAL value)
{
#if 1
  return( (MYBOOL) (modf(fabs(value)+lp->epsint, &value) < 2*lp->epsint) );
#elif 0
  value -= (REAL)floor(value);
  return( (MYBOOL) ((value < lp->epsint) || (value > (1 - lp->epsint)) );
#else
  value += lp->epsint;
  return( (MYBOOL) (fabs(value-floor(value)) < 2*lp->epsint) );
#endif
}

STATIC MYBOOL isInf(lprec *lp, REAL value)
{
  if(fabs(value) >= lp->infinite)
    return( TRUE );
  else
    return( FALSE );
}

STATIC MYBOOL isOrigFixed(lprec *lp, int varno)
{
  return( (MYBOOL) (lp->orig_upbo[varno] - lp->orig_lowbo[varno] <= lp->epsmachine) );
}

STATIC void chsign_bounds(REAL *lobound, REAL *upbound)
{
  REAL temp;
  temp = *upbound;
  if(fabs(*lobound) > 0)
    *upbound = -(*lobound);
  else
    *upbound = 0;
  if(fabs(temp) > 0)
    *lobound = -temp;
  else
    *lobound = 0;
}


/* ---------------------------------------------------------------------------------- */
/* Define routines for doubly linked lists of integers                                */
/* ---------------------------------------------------------------------------------- */

STATIC int createLink(int size, LLrec **linkmap, MYBOOL *usedpos)
{
  int i, j;
  MYBOOL reverse;

  *linkmap = (LLrec *) calloc(1, sizeof(**linkmap));
  if(*linkmap == NULL)
    return( -1 );

  reverse = (MYBOOL) (size < 0);
  if(reverse)
    size = -size;
  (*linkmap)->map = (int *) calloc(2*(size + 1), sizeof(int));
  if((*linkmap)->map == NULL)
    return( -1 );

  (*linkmap)->size = size;
  j = 0;
  if(usedpos == NULL)
    (*linkmap)->map[0] = 0;
  else {
    for(i = 1; i <= size; i++)
      if(!usedpos[i] ^ reverse) {
        /* Set the forward link */
        (*linkmap)->map[j] = i;
        /* Set the backward link */
        (*linkmap)->map[size+i] = j;
        j = i;
        (*linkmap)->count++;
      }
  }
  (*linkmap)->map[2*size+1] = j;

  return( (*linkmap)->count );
}

STATIC MYBOOL freeLink(LLrec **linkmap)
{
  MYBOOL status = TRUE;

  if((linkmap == NULL) || (*linkmap == NULL))
    status = FALSE;
  else {
    if((*linkmap)->map != NULL)
      free((*linkmap)->map);
    free(*linkmap);
    *linkmap = NULL;
  }
  return( status );
}

STATIC int sizeLink(LLrec *linkmap)
{
  return(linkmap->size);
}

STATIC MYBOOL isActiveLink(LLrec *linkmap, int itemnr)
{
  if((linkmap->map[itemnr] != 0) ||
     (linkmap->map[linkmap->size+itemnr] != 0) ||
     (linkmap->map[0] == itemnr))
    return( TRUE );
  else
    return( FALSE );
}

STATIC int countActiveLink(LLrec *linkmap)
{
  return(linkmap->count);
}

STATIC int countInactiveLink(LLrec *linkmap)
{
  return(linkmap->size-linkmap->count);
}

STATIC int firstActiveLink(LLrec *linkmap)
{
  return(linkmap->map[0]);
}

STATIC int lastActiveLink(LLrec *linkmap)
{
  return(linkmap->map[2*linkmap->size+1]);
}

STATIC MYBOOL appendLink(LLrec *linkmap, int newitem)
{
  int k, size;
  size = linkmap->size;

  if(linkmap->map[newitem] != 0)
    return( FALSE );

  /* Link forward */
  k = linkmap->map[2*size+1];
  linkmap->map[k] = newitem;

  /* Link backward */
  linkmap->map[size+newitem] = k;
  linkmap->map[2*size+1] = newitem;

  /* Update count and return */
  linkmap->count++;

  return( TRUE );
}

STATIC MYBOOL insertLink(LLrec *linkmap, int afteritem, int newitem)
{
  int k, size;

  size = linkmap->size;

  if(linkmap->map[newitem] != 0)
    return( FALSE );

  if(afteritem == linkmap->map[2*size+1])
    appendLink(linkmap, newitem);
  else {
    /* Link forward */
    k = linkmap->map[afteritem];
    linkmap->map[afteritem] = newitem;
    linkmap->map[newitem] = k;

    /* Link backward */
    linkmap->map[size+k] = newitem;
    linkmap->map[size+newitem] = afteritem;

    /* Update count */
    linkmap->count++;
  }

  return( TRUE );
}

STATIC MYBOOL fillLink(LLrec *linkmap)
{
  int k, size;
  size = linkmap->size;

  k = firstActiveLink(linkmap);
  if(k != 0)
    return( FALSE );
  for(k = 1; k <= size; k++)
    appendLink(linkmap, k);
  return( TRUE );
}

STATIC int nextActiveLink(LLrec *linkmap, int backitemnr)
{
  if((backitemnr < 0) || (backitemnr > linkmap->size))
    return( -1 );
  else
    return(linkmap->map[backitemnr]);
}

STATIC int prevActiveLink(LLrec *linkmap, int forwitemnr)
{
  if((forwitemnr <= 0) || (forwitemnr > linkmap->size+1))
    return( -1 );
  else
    return(linkmap->map[linkmap->size+forwitemnr]);
}

STATIC int firstInactiveLink(LLrec *linkmap)
{
  int i, n;

  if(countInactiveLink(linkmap) == 0)
    return( 0 );
  n = 1;
  i = firstActiveLink(linkmap);
  while(i == n) {
    n++;
    i = nextActiveLink(linkmap, i);
  }
  return( n );
}

STATIC int lastInactiveLink(LLrec *linkmap)
{
  int i, n;

  if(countInactiveLink(linkmap) == 0)
    return( 0 );
  n = linkmap->size;
  i = lastActiveLink(linkmap);
  while(i == n) {
    n--;
    i = prevActiveLink(linkmap, i);
  }
  return( n );
}

STATIC int nextInactiveLink(LLrec *linkmap, int backitemnr)
{
  do {
    backitemnr++;
  } while((backitemnr <= linkmap->size) && isActiveLink(linkmap, backitemnr));
  if(backitemnr <= linkmap->size)
    return( backitemnr );
  else
    return( 0 );
}

STATIC int prevInactiveLink(LLrec *linkmap, int forwitemnr)
{
  return( 0 );
}

STATIC MYBOOL removeLink(LLrec *linkmap, int itemnr)
{
  int prevnr, nextnr, size;

  size = linkmap->size;

  if((itemnr <= 0) || (itemnr > size))
    return( FALSE );

  /* Get link data at the specified position */
  nextnr = linkmap->map[itemnr];
  prevnr = linkmap->map[size+itemnr];

  /* Update forward link */
  linkmap->map[prevnr] = linkmap->map[itemnr];
  linkmap->map[itemnr] = 0;

  /* Update backward link */
  if(nextnr == 0)
    linkmap->map[2*size+1] = prevnr;
  else
    linkmap->map[size+nextnr] = linkmap->map[size+itemnr];
  linkmap->map[size+itemnr] = 0;

  /* Decrement the count */
  linkmap->count--;

  return( TRUE );
}

STATIC LLrec *cloneLink(LLrec *sourcemap)
{
  LLrec *testmap;

  createLink(sourcemap->size, &testmap, NULL);
  MEMCOPY(testmap->map, sourcemap->map, 2*(sourcemap->size+1));
  testmap->size = sourcemap->size;
  testmap->count = sourcemap->count;

  return(testmap);
}

STATIC int compareLink(LLrec *linkmap1, LLrec *linkmap2)
{
  int test;

  test = memcmp(&linkmap1->size, &linkmap2->size, sizeof(int));
  if(test == 0)
    test = memcmp(&linkmap1->count, &linkmap2->count, sizeof(int));
    if(test == 0)
      test = memcmp(linkmap1->map, linkmap2->map, sizeof(int)*(2*linkmap1->size+1));

  return( test );
}

STATIC MYBOOL verifyLink(LLrec *linkmap, int itemnr, MYBOOL doappend)
{
  LLrec *testmap;

  testmap = cloneLink(linkmap);
  if(doappend) {
    appendLink(testmap, itemnr);
    removeLink(testmap, itemnr);
  }
  else {
    int previtem = prevActiveLink(testmap, itemnr);
    removeLink(testmap, itemnr);
    insertLink(testmap, previtem, itemnr);
  }
  itemnr = compareLink(linkmap, testmap);
  freeLink(&testmap);
  return((MYBOOL) (itemnr == 0));
}

/* Packed vector routines */
STATIC PVrec *createPackedVector(int size, REAL *values, int *workvector)
{
  int      i, k;
  REGISTER REAL  ref;
  PVrec    *newPV = NULL;
  MYBOOL   localWV = (MYBOOL) (workvector == NULL);

  if(localWV)
    workvector = (int *) malloc((size+1)*sizeof(*workvector));

  /* Tally equal-valued vector entries - also check if it is worth compressing */
  k = 0;
  workvector[k] = 1;
  ref = values[1];
  for(i = 2; i <= size; i++) {
    if(fabs(ref - values[i]) > 1.0e-16) {
      k++;
      workvector[k] = i;
      ref = values[i];
    }
  }
  if(k > size / 2) {
    if(localWV)
      FREE(workvector);
    return( newPV );
  }

  /* Create the packing object, adjust the position vector and allocate value vector */
  newPV = (PVrec *) malloc(sizeof(*newPV));
  k++;                            /* Adjust from index to to count */
  newPV->count = k;
  if(localWV)
    newPV->startpos = (int *) realloc(workvector, (k + 1)*sizeof(*(newPV->startpos)));
  else {
    newPV->startpos = (int *) malloc((k + 1)*sizeof(*(newPV->startpos)));
    MEMCOPY(newPV->startpos, workvector, k);
  }
  newPV->startpos[k] = size + 1;  /* Store terminal index + 1 for searching purposes */
  newPV->value = (REAL *) malloc(k*sizeof(*(newPV->value)));

  /* Fill the values vector before returning */
  for(i = 0; i < k; i++)
    newPV->value[i] = values[newPV->startpos[i]];

  return( newPV );
}

STATIC MYBOOL unpackPackedVector(PVrec *PV, REAL **target)
{
  int      i, ii, k;
  REGISTER REAL ref;

  /* Test for validity of the target and create it if necessary */
  if(target == NULL)
    return( FALSE );
  if(*target == NULL)
    allocREAL(NULL, target, PV->startpos[PV->count], FALSE);

  /* Expand the packed vector into the target */
  i = PV->startpos[0];
  for(k = 0; k < PV->count; k++) {
    ii = PV->startpos[k+1];
    ref = PV->value[k];
    while (i < ii) {
      (*target)[i] = ref;
      i++;
    }
  }
  return( TRUE );
}

STATIC REAL getvaluePackedVector(PVrec *PV, int index)
{
  index = searchFor(index, PV->startpos, PV->count, 0, FALSE);
  index = abs(index)-1;
  if(index >= 0)
    return( PV->value[index] );
  else
    return( 0 );
}

STATIC MYBOOL freePackedVector(PVrec **PV)
{
  if((PV == NULL) || (*PV == NULL))
    return( FALSE );

  FREE((*PV)->value);
  FREE((*PV)->startpos);
  FREE(*PV);
  return( TRUE );
}

