#ifdef GNUMERIC_USE_GMP

#include <gmp.h>
typedef mpf_t  float_t;
typedef mpz_t  int_t;

#else
typedef double float_t;
typedef int    int_t;

#define mpz_clear(x)
#define mpf_clear(x)

#endif


