#ifndef HEADER_lp_pricePSE
#define HEADER_lp_pricePSE

#include "lp_types.h"

#define ApplySteepestEdgeMinimum

#ifdef __cplusplus
extern "C" {
#endif

/* Price norm management routines */
STATIC void initPricer(lprec *lp);
STATIC MYBOOL applyPricer(lprec *lp);
STATIC void simplexPricer(lprec *lp, MYBOOL isdual);
STATIC void freePricer(lprec *lp);
STATIC void resizePricer(lprec *lp);
STATIC REAL getPricer(lprec *lp, int item, MYBOOL isdual);
STATIC void restartPricer(lprec *lp, MYBOOL isdual);
STATIC void updatePricer(lprec *lp, int rownr, int colnr, REAL *pcol, REAL *prow, int *nzprow);
STATIC void verifyPricer(lprec *lp);

#ifdef __cplusplus
 }
#endif

#endif /* HEADER_lp_pricePSE */
