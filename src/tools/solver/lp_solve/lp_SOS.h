#ifndef HEADER_lp_SOS
#define HEADER_lp_SOS

/* Specially Ordered Sets (SOS) prototypes and settings                      */
/* ------------------------------------------------------------------------- */

#include "lp_types.h"


/* SOS constraint defines                                                    */
/* ------------------------------------------------------------------------- */
#define SOS1                     1
#define SOS2                     2
#define SOS3                    -1
#define SOS_START_SIZE          10     /* Start size of SOS_list array; realloced if needed */

typedef struct _SOSgroup SOSgroup;

typedef struct _SOSrec
{
  SOSgroup  *parent;
  int       tagorder;
  char      *name;
  int       type;
  MYBOOL    isGUB;
  int       size;
  int       priority;
  int       *members;
  REAL      *weights;
  int       *membersSorted;
  int       *membersMapped;
} SOSrec;

/* typedef */ struct _SOSgroup
{
  lprec     *lp;                /* Pointer to owner */
  SOSrec    **sos_list;         /* Array of pointers to SOS lists */
  int       sos_alloc;          /* Size allocated to specially ordered sets (SOS1, SOS2...) */
  int       sos_count;          /* Number of specially ordered sets (SOS1, SOS2...) */
} /* SOSgroup */;


#ifdef __cplusplus
extern "C" {
#endif

/* SOS storage structure */
STATIC SOSgroup *create_SOSgroup(lprec *lp);
STATIC void resize_SOSgroup(SOSgroup *group);
STATIC int append_SOSgroup(SOSgroup *group, SOSrec *SOS);
STATIC int clean_SOSgroup(SOSgroup *group);
STATIC void free_SOSgroup(SOSgroup **group);
STATIC SOSrec *create_SOSrec(SOSgroup *group, char *name, int type, int priority, int size, int *variables, REAL *weights);
STATIC MYBOOL delete_SOSrec(SOSgroup *group, int sosindex);
STATIC int append_SOSrec(SOSrec *SOS, int size, int *variables, REAL *weights);
STATIC void free_SOSrec(SOSrec *SOS);

/* SOS utilities */
STATIC int make_SOSchain(lprec *lp, MYBOOL forceresort);
STATIC MYBOOL SOS_sort_members(SOSgroup *group, int sosindex);
STATIC MYBOOL SOS_shift_col(SOSgroup *group, int sosindex, int column, int delta, MYBOOL forceresort);
int SOS_get_type(SOSgroup *group, int sosindex);
int SOS_infeasible(SOSgroup *group, int sosindex);
int SOS_member_index(SOSgroup *group, int sosindex, int member);
int SOS_is_member(SOSgroup *group, int sosindex, int column);
MYBOOL SOS_is_member_of_type(SOSgroup *group, int column, int sostype);
MYBOOL SOS_set_GUB(SOSgroup *group, int sosindex, MYBOOL state);
MYBOOL SOS_is_GUB(SOSgroup *group, int sosindex);
MYBOOL SOS_is_marked(SOSgroup *group, int sosindex, int column);
MYBOOL SOS_is_active(SOSgroup *group, int sosindex, int column);
MYBOOL SOS_is_full(SOSgroup *group, int sosindex, int column, MYBOOL activeonly);
MYBOOL SOS_can_activate(SOSgroup *group, int sosindex, int column);
MYBOOL SOS_set_marked(SOSgroup *group, int sosindex, int column, MYBOOL asactive);
MYBOOL SOS_unmark(SOSgroup *group, int sosindex, int column);
int SOS_fix_unmarked(SOSgroup *group, int variable, int sosindex, REAL *bound, REAL value,
                     MYBOOL isupper, int *diffcount);
int SOS_fix_GUB(SOSgroup *group, int variable, int sosindex, REAL *bound, MYBOOL isleft);
int SOS_is_satisfied(SOSgroup *group, int sosindex, REAL *solution);

#ifdef __cplusplus
 }
#endif

#endif /* HEADER_lp_SOS */
