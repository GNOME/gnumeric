#ifndef HEADER_commonlib
#define HEADER_commonlib

#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>

#define BIGNUMBER    1.0e+30
#define TINYNUMBER   1.0e-4
#define MACHINEPREC  2.22e-16
#define MATHPREC     1.0e-16
#define ERRLIMIT     1.0e-6

#ifndef LINEARSEARCH
  #define LINEARSEARCH 5
#endif

#if 0
  #define INTEGERTIME
#endif

#ifndef MYBOOL
/*  #define MYBOOL       unsigned int */
  #define MYBOOL       unsigned char
#endif

#ifndef REAL
  #define REAL         double
#endif

#ifndef my_boolstr
  #define my_boolstr(x)          (!(x) ? "FALSE" : "TRUE")
#endif

#ifndef NULL
  #define NULL 	       0
#endif

#ifndef FALSE
  #define FALSE        0
  #define TRUE         1
#endif

#ifndef DOFASTMATH
  #define DOFASTMATH
#endif


#ifndef CALLOC
#define CALLOC(ptr, nr)\
  if(!((void *) ptr = calloc((size_t)(nr), sizeof(*ptr))) && nr) {\
    printf("calloc of %d bytes failed on line %d of file %s\n",\
           (size_t) nr * sizeof(*ptr), __LINE__, __FILE__);\
  }
#endif

#ifndef MALLOC
#define MALLOC(ptr, nr)\
  if(!((void *) ptr = malloc((size_t)((size_t) (nr) * sizeof(*ptr)))) && nr) {\
    printf("malloc of %d bytes failed on line %d of file %s\n",\
           (size_t) nr * sizeof(*ptr), __LINE__, __FILE__);\
  }
#endif

#ifndef REALLOC
#define REALLOC(ptr, nr)\
  if(!((void *) ptr = realloc(ptr, (size_t)((size_t) (nr) * sizeof(*ptr)))) && nr) {\
    printf("realloc of %d bytes failed on line %d of file %s\n",\
           (size_t) nr * sizeof(*ptr), __LINE__, __FILE__);\
  }
#endif

#ifndef FREE
#define FREE(ptr)\
  if((void *) ptr != NULL) {\
    free(ptr);\
    ptr = NULL; \
  }
#endif

#ifndef MEMCOPY
#define MEMCOPY(nptr, optr, nr)\
  memcpy((nptr), (optr), (size_t)((size_t)(nr) * sizeof(*(optr))))
#endif

#ifndef MEMMOVE
#define MEMMOVE(nptr, optr, nr)\
  memmove((nptr), (optr), (size_t)((size_t)(nr) * sizeof(*(optr))))
#endif

#ifndef MALLOCCCPY
#define MALLOCCPY(nptr, optr, nr)\
  {MALLOC(nptr, (size_t)(nr))\
   MEMCPY(nptr, optr, (size_t)(nr))}
#endif

#ifndef MEMCLEAR
/*#define useMMX*/
#ifdef useMMX
  #define MEMCLEAR(ptr, nr)\
    mem_set((ptr), '\0', (size_t)((size_t)(nr) * sizeof(*(ptr))))
#else
  #define MEMCLEAR(ptr, nr)\
    memset((ptr), '\0', (size_t)((size_t)(nr) * sizeof(*(ptr))))
#endif
#endif


#define MIN(x, y)    ((x) < (y) ? (x) : (y))
#define MAX(x, y)    ((x) > (y) ? (x) : (y))
#define IF(t, x, y)  ((t) ? (x) : (y))
#define SIGN(x)      ((x) < 0 ? -1 : 1)

#ifndef CMP_CALLMODEL
  #ifdef WIN32
    # define CMP_CALLMODEL _cdecl
  #else
    # define CMP_CALLMODEL
  #endif
#endif

#define CMP_ATTRIBUTES(item) (((char *) attributes)+item*recsize)
typedef int (CMP_CALLMODEL findCompare_func)(const void *current, const void *candidate);


#ifdef __cplusplus
  extern "C" {
#endif

int mod( int n, int d );

int findIndex(int target, int *attributes, int count, int offset);
int findIndexEx(void *target, void *attributes, int count, int offset, int recsize, findCompare_func findCompare);

int sortByREAL(int *item, REAL *weight, int size, int offset, MYBOOL unique);
int sortByINT(int *item, int *weight, int size, int offset, MYBOOL unique);
REAL sortREALByINT(REAL *item, int *weight, int size, int offset, MYBOOL unique);

double timeNow();

void blockWriteBOOL(FILE *output, char *label, MYBOOL *vector, int first, int last, MYBOOL asRaw);
void blockWriteINT(FILE *output, char *label, int *vector, int first, int last);
void blockWriteREAL(FILE *output, char *label, REAL *vector, int first, int last);

void printvec( int n, REAL *x, int modulo );
void printmatSQ( int size, int n, REAL *X, int modulo );
void printmatUT( int size, int n, REAL *U, int modulo );

#ifdef WIN32
int fileCount( char *filemask );
MYBOOL fileSearchPath( char *envvar, char *searchfile, char *foundpath );
#endif

#ifdef __cplusplus
  }
#endif

#endif /* HEADER_commonlib */
