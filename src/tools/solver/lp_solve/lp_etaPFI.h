#ifndef HEADER_lp_etaPFI
#define HEADER_lp_etaPFI

#include "lp_types.h"

/* Eta PFI option defines */
#if INVERSE_ACTIVE == INVERSE_LEGACY
  #define UseLegacyOrdering               /* Use unoptimized column ordering like in v4 and before */
  #define LegacyEtaPivotChoice            /* Do not look for largest pivot, only first satisfactory */
#else
  #define ReprocessSingletons             /* Try to increase sparsity by iteratively looking for singletons */
  #define UseMarkowitzStatistic           /* Try to minimize fill-in by the Markowitz statistic */
#endif
#define ExcludeCountOrderOF               /* This define typically gives sparser inverses */

#define EtaFtranRoundRelative             /* Do FTRAN relative value rounding management */
/*#define EtaBtranRoundRelative*/         /* Do BTRAN relative value rounding management */

/* Eta parameter defines */
#define MATINDEXBASE                   0
#define INVDELTAROWS                   0  /* Additional rows inserted at the top */
#define ETA_START_SIZE  3*MAT_START_SIZE  /* Start size of Eta-array; realloc'ed if needed;
                                             typical scalars are in the 2-10 range with MDO */
#define DEF_MAXPIVOT                  42  /* Maximum number of pivots before reinversion;
                                             best average performance in the 38-48 range */
#define EPS_ETAMACHINE  lp->epsmachine    /* lp->epsvalue */
#define EPS_ETAPIVOT    lp->epsmachine


/* typedef */ struct _INVrec
{
  int       status;                       /* Last operation status code */
  int       eta_matalloc;                 /* The allocated memory for Eta non-zero values */
  int       eta_colcount;                 /* The number of Eta columns */
  REAL      *eta_value;                   /* eta_alloc: Structure containing values of Eta */
  int       *eta_row_nr;                  /*  "     " : Structure containing row indexes of Eta */
  int       *eta_col_nr;                  /* eta_columns: Structure containting column indexes of Eta */
  int       *eta_col_end;                 /* rows_alloc + MaxNumInv : eta_col_end[i] is
                			                       the start index of the next Eta column */
  int       max_Bsize;                    /* The largest B matrix of user variables */
  int       max_colcount;                 /* The maximum number of user columns in eta */
  int       last_colcount;                /* The previous column size of the eta */
  int       max_etasize;                  /* The largest eta-file generated */
  int       num_refact;                   /* Number of times the basis was refactored */
  int       num_pivots;                   /* Number of pivots since last refactorization */
  double    time_refactstart;             /* Time since start of last refactorization-pivots cyle */
  double    time_refactnext;              /* Time estimated to next refactorization */
  REAL      *pcol;                        /* Vector pointer to the last base for the eta update */
  int       num_singular;                 /* The total number of singular updates */
  REAL      extraD;                       /* The dual objective function offset for the current inverse */
  REAL      statistic1;
  REAL      statistic2;
  MYBOOL    is_dirty;                     /* Specifies if a column is incompletely processed */
  MYBOOL    force_refact;                 /* Force refactorization at the next opportunity */
  MYBOOL    set_Bidentity;                /* Force B to be the identity matrix at the next refactorization */
} /* INVrec */;


#ifdef __cplusplus
/*namespace etaPFI*/
extern "C" {
#endif

/* Put function headers here */
#include "lp_BFP.h"


#ifdef __cplusplus
 }
#endif

#endif /* HEADER_lp_etaPFI */
