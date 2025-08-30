#ifndef _GNM_MATHFUNC_H_
# define _GNM_MATHFUNC_H_

#include <numbers.h>
#include <math.h>
#include <glib.h>
#include <gnumeric.h>

G_BEGIN_DECLS

#ifdef qgamma
/* It was reported that mips-sgi-irix6.5 has a weird and conflicting define
   for qgamma.  See bug 1689.  */
#warning "Your <math.h> is somewhat broken; we'll work around that."
#undef qgamma
#endif

#define M_PIgnum    GNM_const(3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117)
#define M_PI_2gnum (M_PIgnum / 2)
/* The following are very good given a good compiler.  */
#define M_LN2gnum   GNM_const(0.693147180559945309417232121458176568075500134360255254120680009493393621969694715605863326996419)
#define M_LN10gnum  GNM_const(2.302585092994045684017991454684364207601101488628772976033327900967572609677352480235997205089598)
#define M_LN10INVgnum  GNM_const(0.434294481903251827651128918916605082294397005803666566114)
#define M_SQRT2gnum GNM_const(1.414213562373095048801688724209698078569671875376948073176679737990732478462107038850387534327642)
#define	M_Egnum         GNM_const(2.718281828459045235360287471352662497757247)
#define M_LN_SQRT_2PI   GNM_const(0.918938533204672741780329736406)  /* log(sqrt(2*pi)) */

/* ------------------------------------------------------------------------- */

gnm_float log1pmx (gnm_float x);
gnm_float lgamma1p (gnm_float a);
gnm_float bd0(gnm_float x, gnm_float M);
gnm_float gnm_taylor_log1p (gnm_float x, int k);
gnm_float swap_log_tail (gnm_float lp);
gnm_float pow1p (gnm_float x, gnm_float y);
gnm_float pow1pm1 (gnm_float x, gnm_float y);
gnm_float logspace_add (gnm_float logx, gnm_float logy);
gnm_float logspace_sub (gnm_float logx, gnm_float logy);
gnm_float gnm_owent (gnm_float h, gnm_float a);
gnm_float gnm_logcf (gnm_float x, gnm_float i, gnm_float d, gnm_float tol);
gnm_float expmx2h (gnm_float x);
gnm_float gnm_agm(gnm_float a, gnm_float b);
gnm_float gnm_lambert_w(gnm_float x, int k);
gnm_float gnm_ilog (gnm_float x, gnm_float b);
gnm_float gnm_logbase(gnm_float x, gnm_float b);

/* "d": density.  */
/* "p": distribution function.  */
/* "q": inverse distribution function.  */

/* The normal distribution.  */
gnm_float pnorm (gnm_float x, gnm_float mu, gnm_float sigma, gboolean lower_tail, gboolean log_p);
gnm_float qnorm (gnm_float p, gnm_float mu, gnm_float sigma, gboolean lower_tail, gboolean log_p);

/* The gamma distribution.  */
gnm_float dgamma (gnm_float x, gnm_float shape, gnm_float scale, gboolean give_log);
gnm_float pgamma (gnm_float x, gnm_float shape, gnm_float scale, gboolean lower_tail, gboolean log_p);
gnm_float qgamma (gnm_float p, gnm_float shape, gnm_float scale, gboolean lower_tail, gboolean log_p);

/* The beta distribution.  */
gnm_float dbeta (gnm_float x, gnm_float a, gnm_float b, gboolean give_log);
gnm_float pbeta (gnm_float x, gnm_float a, gnm_float b, gboolean lower_tail, gboolean log_p);
gnm_float qbeta (gnm_float p, gnm_float a, gnm_float b, gboolean lower_tail, gboolean log_p);

/* The t distribution.  */
gnm_float dt (gnm_float x, gnm_float n, gboolean give_log);
gnm_float pt (gnm_float x, gnm_float n, gboolean lower_tail, gboolean log_p);
gnm_float qt (gnm_float p, gnm_float n, gboolean lower_tail, gboolean log_p);

/* The F distribution.  */
gnm_float df (gnm_float x, gnm_float n1, gnm_float n2, gboolean give_log);
gnm_float pf (gnm_float x, gnm_float n1, gnm_float n2, gboolean lower_tail, gboolean log_p);
gnm_float qf (gnm_float p, gnm_float n1, gnm_float n2, gboolean lower_tail, gboolean log_p);

/* The chi-squared distribution.  */
gnm_float dchisq (gnm_float x, gnm_float df, gboolean give_log);
gnm_float pchisq (gnm_float x, gnm_float df, gboolean lower_tail, gboolean log_p);
gnm_float qchisq (gnm_float p, gnm_float df, gboolean lower_tail, gboolean log_p);

/* The Weibull distribution.  */
gnm_float dweibull (gnm_float x, gnm_float shape, gnm_float scale, gboolean give_log);
gnm_float pweibull (gnm_float x, gnm_float shape, gnm_float scale, gboolean lower_tail, gboolean log_p);
gnm_float qweibull (gnm_float p, gnm_float shape, gnm_float scale, gboolean lower_tail, gboolean log_p);

/* The Poisson distribution.  */
gnm_float dpois (gnm_float x, gnm_float lambda, gboolean give_log);
gnm_float ppois (gnm_float x, gnm_float lambda, gboolean lower_tail, gboolean log_p);
gnm_float qpois (gnm_float p, gnm_float lambda, gboolean lower_tail, gboolean log_p);

/* The exponential distribution.  */
gnm_float dexp (gnm_float x, gnm_float scale, gboolean give_log);
gnm_float pexp (gnm_float x, gnm_float scale, gboolean lower_tail, gboolean log_p);
gnm_float qexp (gnm_float p, gnm_float scale, gboolean lower_tail, gboolean log_p);

/* Binomial distribution.  */
gnm_float dbinom (gnm_float x, gnm_float n, gnm_float psuc, gboolean give_log);
gnm_float pbinom (gnm_float x, gnm_float n, gnm_float psuc, gboolean lower_tail, gboolean log_p);
gnm_float pbinom2 (gnm_float x0, gnm_float x1, gnm_float n, gnm_float p);
gnm_float qbinom (gnm_float p, gnm_float n, gnm_float psuc, gboolean lower_tail, gboolean log_p);

/* Negative binomial distribution.  */
gnm_float dnbinom (gnm_float x, gnm_float n, gnm_float psuc, gboolean give_log);
gnm_float pnbinom (gnm_float x, gnm_float n, gnm_float psuc, gboolean lower_tail, gboolean log_p);
gnm_float qnbinom (gnm_float p, gnm_float n, gnm_float psuc, gboolean lower_tail, gboolean log_p);

/* Hyper-geometrical distribution.  */
gnm_float dhyper (gnm_float x, gnm_float r, gnm_float b, gnm_float n, gboolean give_log);
gnm_float phyper (gnm_float x, gnm_float r, gnm_float b, gnm_float n, gboolean lower_tail, gboolean log_p);

/* Geometric distribution.  */
gnm_float dgeom (gnm_float x, gnm_float psuc, gboolean give_log);
gnm_float pgeom (gnm_float x, gnm_float psuc, gboolean lower_tail, gboolean log_p);
gnm_float qgeom (gnm_float p, gnm_float psuc, gboolean lower_tail, gboolean log_p);

/* Cauchy distribution.  */
gnm_float dcauchy (gnm_float x, gnm_float location, gnm_float scale, gboolean give_log);
gnm_float pcauchy (gnm_float x, gnm_float location, gnm_float scale, gboolean lower_tail, gboolean log_p);

/* The probability density functions. */
gnm_float random_exppow_pdf     (gnm_float x, gnm_float a, gnm_float b);
gnm_float random_laplace_pdf    (gnm_float x, gnm_float a);

/* Studentized range distribution */
/* Note: argument order differs from R.  */
gnm_float ptukey(gnm_float x, gnm_float nmeans, gnm_float df, gnm_float nranges, gboolean lower_tail, gboolean log_p);
gnm_float qtukey(gnm_float p, gnm_float nmeans, gnm_float df, gnm_float nranges, gboolean lower_tail, gboolean log_p);

/* ------------------------------------------------------------------------- */

/* Matrix functions. */

GType gnm_matrix_get_type (void);

struct GnmMatrix_ {
	int ref_count;
	gnm_float **data;   /* [y][x] */
	int cols, rows;
};

GnmMatrix *gnm_matrix_new (int rows, int cols); /* Note the order: y then x. */
GnmMatrix *gnm_matrix_ref (GnmMatrix *m);
void gnm_matrix_unref (GnmMatrix *m);
GnmMatrix *gnm_matrix_from_value (GnmValue const *v, GnmValue **perr, GnmEvalPos const *ep);
GnmValue *gnm_matrix_to_value (GnmMatrix const *m);
gboolean gnm_matrix_is_empty (GnmMatrix const *m);

void gnm_matrix_multiply (GnmMatrix *C, const GnmMatrix *A, const GnmMatrix *B);

gboolean gnm_matrix_eigen (GnmMatrix const *m, GnmMatrix *EIG, gnm_float *eigenvalues);

gboolean gnm_matrix_modified_cholesky (GnmMatrix const *A,
				       GnmMatrix *L,
				       gnm_float *D,
				       gnm_float *E,
				       int *P);

GORegressionResult gnm_linear_solve_posdef (GnmMatrix const *A, const gnm_float *b,
					    gnm_float *x);

GORegressionResult gnm_linear_solve (GnmMatrix const *A, const gnm_float *b,
				     gnm_float *x);

GORegressionResult gnm_linear_solve_multiple (GnmMatrix const *A, GnmMatrix *B);

/* ------------------------------------------------------------------------- */

void mathfunc_init (void);

/* ------------------------------------------------------------------------- */

G_END_DECLS

#endif /* _GNM_MATHFUNC_H_ */
