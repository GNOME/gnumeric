#ifndef HEADER_lp_MPS
#define HEADER_lp_MPS

#include "lp_types.h"

/* For MPS file reading and writing */

#define MPSFIXED 1
#define MPSFREE  2

#ifdef __cplusplus
extern "C" {
#endif

/* Read an MPS file */
lprec *MPS_readfile(char *filename, int typeMPS, int verbose);
lprec *MPS_readhandle(FILE *filename, int typeMPS, int verbose);

/* Write a MPS file to output */
MYBOOL MPS_writefile(lprec *lp, int typeMPS, char *filename);
MYBOOL MPS_writehandle(lprec *lp, int typeMPS, FILE *output);


#ifdef __cplusplus
 }
#endif

#endif /* HEADER_lp_MPS */
