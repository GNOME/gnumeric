#ifndef HEADER_lp_simplex
#define HEADER_lp_simplex

#include "lp_types.h"


#ifdef __cplusplus
extern "C" {
#endif

/* Put function headers here */
STATIC int primloop(lprec *lp, MYBOOL feasible);
STATIC int dualloop(lprec *lp, MYBOOL feasible);
STATIC int spx_run(lprec *lp);
STATIC int spx_solve(lprec *lp);
STATIC int lag_solve(lprec *lp, REAL start_bound, int num_iter);
STATIC int heuristics(lprec *lp, int mode);
STATIC int lin_solve(lprec *lp);

#ifdef __cplusplus
 }
#endif

#endif /* HEADER_lp_simplex */
