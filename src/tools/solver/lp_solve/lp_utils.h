#ifndef HEADER_lp_utils
#define HEADER_lp_utils

#include "lp_types.h"

typedef struct _LLrec
{
  int       size;               /* The allocated list size */
  int       count;              /* The current entry count */
  int       *map;               /* The list of forward and backward-mapped entries */
} LLrec;

typedef struct _PVrec
{
  int       count;              /* The allocated list item count */
  int       *startpos;          /* Starting index of the current value */
  REAL      *value;             /* The list of forward and backward-mapped entries */
} PVrec;


#ifdef __cplusplus
extern "C" {
#endif

/* Put function headers here */
STATIC MYBOOL allocCHAR(lprec *lp, char **ptr, int size, MYBOOL clear);
STATIC MYBOOL allocMYBOOL(lprec *lp, MYBOOL **ptr, int size, MYBOOL clear);
STATIC MYBOOL allocINT(lprec *lp, int **ptr, int size, MYBOOL clear);
STATIC MYBOOL allocREAL(lprec *lp, REAL **ptr, int size, MYBOOL clear);
STATIC MYBOOL allocLREAL(lprec *lp, LREAL **ptr, int size, MYBOOL clear);
STATIC MYBOOL allocFREE(lprec *lp, void **ptr);
REAL *cloneREAL(lprec *lp, REAL *origlist, int size);
MYBOOL *cloneMYBOOL(lprec *lp, MYBOOL *origlist, int size);
int *cloneINT(lprec *lp, int *origlist, int size);

STATIC void roundVector(LREAL *myvector, int endpos, LREAL roundzero);
STATIC REAL normalizeVector(REAL *myvector, int endpos);

STATIC void swapINT(int *item1, int *item2);
STATIC void swapREAL(REAL *item1, REAL *item2);
STATIC void swapPTR(void **item1, void **item2);
STATIC REAL restoreINT(REAL valREAL, REAL epsilon);
STATIC REAL roundToPrecision(REAL value, REAL precision);

STATIC int searchFor(int target, int *attributes, int size, int offset, MYBOOL absolute);

STATIC MYBOOL isINT(lprec *lp, REAL value);
STATIC MYBOOL isInf(lprec *lp, REAL value);
STATIC MYBOOL isOrigFixed(lprec *lp, int varno);
STATIC void chsign_bounds(REAL *lobound, REAL *upbound);

/* Doubly linked list routines */
STATIC int createLink(int size, LLrec **linkmap, MYBOOL *usedpos);
STATIC MYBOOL freeLink(LLrec **linkmap);
STATIC int sizeLink(LLrec *linkmap);
STATIC MYBOOL isActiveLink(LLrec *linkmap, int itemnr);
STATIC int countActiveLink(LLrec *linkmap);
STATIC int countInactiveLink(LLrec *linkmap);
STATIC int firstActiveLink(LLrec *linkmap);
STATIC int lastActiveLink(LLrec *linkmap);
STATIC MYBOOL appendLink(LLrec *linkmap, int newitem);
STATIC MYBOOL insertLink(LLrec *linkmap, int afteritem, int newitem);
STATIC MYBOOL fillLink(LLrec *linkmap);
STATIC int nextActiveLink(LLrec *linkmap, int backitemnr);
STATIC int prevActiveLink(LLrec *linkmap, int forwitemnr);
STATIC int firstInactiveLink(LLrec *linkmap);
STATIC int lastInactiveLink(LLrec *linkmap);
STATIC int nextInactiveLink(LLrec *linkmap, int backitemnr);
STATIC int prevInactiveLink(LLrec *linkmap, int forwitemnr);
STATIC MYBOOL removeLink(LLrec *linkmap, int itemnr);
STATIC LLrec *cloneLink(LLrec *sourcemap);
STATIC int compareLink(LLrec *linkmap1, LLrec *linkmap2);
STATIC MYBOOL verifyLink(LLrec *linkmap, int itemnr, MYBOOL doappend);

/* Packed vector routines */
STATIC PVrec  *createPackedVector(int size, REAL *values, int *workvector);
STATIC MYBOOL unpackPackedVector(PVrec *PV, REAL **target);
STATIC REAL   getvaluePackedVector(PVrec *PV, int index);
STATIC MYBOOL freePackedVector(PVrec **PV);

#ifdef __cplusplus
 }
#endif

#endif /* HEADER_lp_utils */
