#ifndef HEADER_lp_scale
#define HEADER_lp_scale

#include "lp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Put function headers here */
STATIC MYBOOL scale_updatecolumns(lprec *lp, REAL *scalechange, MYBOOL updateonly);
STATIC MYBOOL scale_updaterows(lprec *lp, REAL *scalechange, MYBOOL updateonly);
STATIC MYBOOL scale_rows(lprec *lp);
STATIC MYBOOL scale_columns(lprec *lp);
STATIC void unscale_columns(lprec *lp);
STATIC REAL scale(lprec *lp);
STATIC MYBOOL scaleCR(lprec *lp);
STATIC MYBOOL finalize_scaling(lprec *lp);
STATIC REAL lp_solve_auto_scale(lprec *lp);
void undoscale(lprec *lp);

#ifdef __cplusplus
 }
#endif

#endif /* HEADER_lp_scale */
