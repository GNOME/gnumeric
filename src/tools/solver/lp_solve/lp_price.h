#ifndef HEADER_lp_price
#define HEADER_lp_price

#include "lp_types.h"

/* Define pivot definition */
/*
typedef struct _pricerec
{
  REAL    theta;
  REAL    pivot;
  int     varno;
  lprec   *lp;
  MYBOOL  isdual;
} pricerec;
*/

#ifdef __cplusplus
extern "C" {
#endif

/* Comparison and validity routines */
int CMP_CALLMODEL compareImprovementVar(const pricerec *current, const pricerec *candidate);
int CMP_CALLMODEL compareSubstitutionVar(pricerec *current, pricerec *candidate);
STATIC MYBOOL validImprovementVar(pricerec *candidate);
STATIC MYBOOL validSubstitutionVar(pricerec *candidate);

/* Row+column selection routines */
STATIC int addImprovementVar(pricerec *candidate);
STATIC MYBOOL findImprovementVar(pricerec *current, pricerec *candidate, MYBOOL collectMP);
STATIC MYBOOL findSubstitutionVar(pricerec *current, pricerec *candidate);
STATIC REAL normalizeEdge(lprec *lp, int item, REAL edge, MYBOOL isdual);
STATIC void makePriceLoop(lprec *lp, int *start, int *end, int *delta);

/* Leaving variable selection and entering column pricing loops */
STATIC int colprim(lprec *lp, MYBOOL minit, REAL *drow, int *nzdrow);
STATIC int rowprim(lprec *lp, int colnr, LREAL *theta, REAL *pcol);
STATIC int rowdual(lprec *lp, MYBOOL eliminate);
STATIC int coldual(lprec *lp, int row_nr, MYBOOL minit, REAL *prow, int *nzprow,
                                                        REAL *drow, int *nzdrow);

/* Partial pricing management routines */
STATIC partialrec *partial_createBlocks(lprec *lp, MYBOOL isrow);
STATIC int partial_countBlocks(lprec *lp, MYBOOL isrow);
STATIC int partial_activeBlocks(lprec *lp, MYBOOL isrow);
STATIC void partial_freeBlocks(partialrec **blockdata);

/* Partial and multiple pricing utility routines */
STATIC int partial_findBlocks(lprec *lp, MYBOOL autodefine, MYBOOL isrow);
STATIC int partial_blockStart(lprec *lp, MYBOOL isrow);
STATIC int partial_blockEnd(lprec *lp, MYBOOL isrow);
STATIC int partial_blockNextPos(lprec *lp, int block, MYBOOL isrow);

STATIC MYBOOL partial_blockStep(lprec *lp, MYBOOL isrow);
STATIC MYBOOL partial_isVarActive(lprec *lp, int varno, MYBOOL isrow);

STATIC MYBOOL multi_prepareVector(lprec *lp, int blocksize);
STATIC int *multi_createVarList(lprec *lp);

#ifdef __cplusplus
 }
#endif

#endif /* HEADER_lp_price */
