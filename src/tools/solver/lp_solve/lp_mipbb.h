#ifndef HEADER_lp_mipbb
#define HEADER_lp_mipbb

#include "lp_types.h"
#include "lp_utils.h"


/* Bounds storage for B&B routines */
typedef struct _BBrec
{
  lprec     *lp;
  int       varno;
  int       vartype;
  int       lastvarcus;                  /* Count of non-int variables of the previous branch */
  REAL      lastsolution;                /* Optimal solution of the previous branch */
  REAL      sc_bound;
  MYBOOL    sc_canset;
  MYBOOL    isSOS;
  MYBOOL    isGUB;
  MYBOOL    UBzerobased;
  REAL      *UB_base, *upbo,  UPbound;
  REAL      *LB_base, *lowbo, LObound;
  PVrec     *saved_upbo, *saved_lowbo;
  MYBOOL    isfloor;
  int       nodesleft;
  int       nodessolved;
  int       nodestatus;
  REAL      noderesult;
  struct    _BBrec *parent;
  struct    _BBrec *child;
} BBrec;

#ifdef __cplusplus
extern "C" {
#endif

STATIC BBrec *create_BB(lprec *lp, BBrec *parentBB, REAL *UB_active, REAL *LB_active);
STATIC MYBOOL compress_BB(BBrec *BB);
STATIC MYBOOL uncompress_BB(BBrec *BB);
STATIC BBrec *push_BB(lprec *lp, BBrec *parentBB, int varno, int vartype,
                                 REAL *UB_active, REAL *LB_active, int varcus);
STATIC MYBOOL initbranches_BB(BBrec *BB);
STATIC MYBOOL fillbranches_BB(BBrec *BB);
STATIC MYBOOL nextbranch_BB(BBrec *BB);
STATIC BBrec *findself_BB(BBrec *BB);
STATIC MYBOOL mergeshadow_BB(BBrec *BB);
STATIC int solve_LP(lprec *lp, BBrec *BB, REAL *upbo, REAL *lowbo);
STATIC MYBOOL findnode_BB(BBrec *BB, int *varno, int *vartype, int *varcus);
STATIC int solve_BB(BBrec *BB, REAL **UB_active, REAL **LB_active);
STATIC void free_BB(BBrec **BB);
STATIC BBrec *pop_BB(BBrec *BB);

STATIC int run_BB(lprec *lp);

#ifdef __cplusplus
 }
#endif

#endif /* HEADER_lp_mipbb */
