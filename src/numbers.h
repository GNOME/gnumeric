#ifndef GNUMERIC_NUMBERS_H
#define GNUMERIC_NUMBERS_H
#ifdef GNUMERIC_USE_GMP

#include <gmp.h>
typedef mpf_t  float_t;
typedef mpz_t  int_t;

#else
typedef double float_t;
typedef int    int_t;

#define mpz_clear(x)
#define mpz_init(x) x = 0
#define mpz_set(a,b) a = b
#define mpz_init_set(a,b) a = b
#define mpz_add(x,a,b) x = a + b
#define mpz_sub(x,a,b) x = a - b
#define mpz_mul(x,a,b) x = a * b
#define mpz_tdivq(x,a,b) x = a / b
#define mpz_cmp_si(a,b) (a == b)
#define mpz_set_si(a,b) a = b
#define mpz_neg(a,b) a = -b

#define mpf_clear(x)
#define mpf_init(x) x = 0
#define mpf_init_set(a,b) a = b
#define mpf_set(a,b) a =b
#define mpf_set_z(x,v) x = v
#define mpf_add(x,a,b) x = a + b
#define mpf_sub(x,a,b) x = a - b
#define mpf_mul(x,a,b) x = a * b
#define mpf_div(x,a,b) x = a / b
#define mpf_cmp_si(a,b) a == b
#define mpf_neg(a,b) a = -b
#endif

#endif /* GNUMERIC_NUMBERS_H */
