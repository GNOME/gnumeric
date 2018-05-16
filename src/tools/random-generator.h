#ifndef GNUMERIC_RANDOM_GENERATOR_H
#define GNUMERIC_RANDOM_GENERATOR_H

#include <gnumeric.h>
#include <numbers.h>
#include <tools/dao.h>
#include <tools/tools.h>


typedef enum {
  DiscreteDistribution, UniformDistribution, NormalDistribution,
  BernoulliDistribution, BetaDistribution, BinomialDistribution,
  PoissonDistribution, CauchyDistribution, ChisqDistribution, GammaDistribution,
  WeibullDistribution, FdistDistribution, GeometricDistribution,
  Gumbel1Distribution, Gumbel2Distribution, LaplaceDistribution,
  TdistDistribution, LogarithmicDistribution, LogisticDistribution,
  ParetoDistribution, LognormalDistribution, RayleighDistribution,
  LevyDistribution, ExponentialPowerDistribution, RayleighTailDistribution,
  LandauDistribution, GaussianTailDistribution, UniformIntDistribution,
  /* PatternedDistribution, */ NegativeBinomialDistribution, ExponentialDistribution
} random_distribution_t;

typedef struct {
	GnmValue *range;
} discrete_random_tool_t;

typedef struct {
        gnm_float lower_limit;
        gnm_float upper_limit;
} uniform_random_tool_t;

typedef struct {
        gnm_float mean;
        gnm_float stdev;
} normal_random_tool_t;

typedef struct {
        gnm_float p;
} bernoulli_random_tool_t;

typedef struct {
        gnm_float a;
        gnm_float b;
} beta_random_tool_t;

typedef struct {
        gnm_float p;
        int     trials;
} binomial_random_tool_t;

typedef struct {
        gnm_float p;
        int     f;
} negbinom_random_tool_t;

typedef struct {
        gnm_float lambda;
} poisson_random_tool_t;

typedef struct {
        gnm_float b;
} exponential_random_tool_t;

typedef struct {
        gnm_float a;
        gnm_float b;
} exppow_random_tool_t;

typedef struct {
        gnm_float a;
} cauchy_random_tool_t;

typedef struct {
        gnm_float nu;
} chisq_random_tool_t;

typedef struct {
        gnm_float zeta;
        gnm_float sigma;
} lognormal_random_tool_t;

typedef struct {
        gnm_float sigma;
} rayleigh_random_tool_t;

typedef struct {
        gnm_float a;
        gnm_float sigma;
} rayleigh_tail_random_tool_t;

typedef struct {
        gnm_float c;
        gnm_float alpha;
} levy_random_tool_t;

typedef struct {
        gnm_float nu1;
        gnm_float nu2;
} fdist_random_tool_t;

typedef struct {
        gnm_float nu;
} tdist_random_tool_t;

typedef struct {
        gnm_float p;
} logarithmic_random_tool_t;

typedef struct {
        gnm_float a;
        gnm_float b;
} pareto_random_tool_t;

typedef struct {
        gnm_float a;
} logistic_random_tool_t;

typedef struct {
        gnm_float a;
        gnm_float b;
} gamma_random_tool_t;

typedef struct {
        gnm_float a;
        gnm_float b;
} weibull_random_tool_t;

typedef struct {
        gnm_float a;
} laplace_random_tool_t;

typedef struct {
        gnm_float a;
        gnm_float sigma;
} gaussian_tail_random_tool_t;

typedef struct {
        gnm_float a;
        gnm_float b;
} gumbel_random_tool_t;

typedef struct {
        gnm_float p;
} geometric_random_tool_t;

/* typedef struct { */
/*         gnm_float from, to; */
/*         gnm_float step; */
/*         int     repeat_number; */
/*         int     repeat_sequence; */
/* } patterned_random_tool_t; */

typedef union {
        discrete_random_tool_t      discrete;
        uniform_random_tool_t       uniform;
        normal_random_tool_t        normal;
        bernoulli_random_tool_t     bernoulli;
        beta_random_tool_t          beta;
        binomial_random_tool_t      binomial;
        negbinom_random_tool_t      negbinom;
        poisson_random_tool_t       poisson;
        exponential_random_tool_t   exponential;
        exppow_random_tool_t        exppow;
        cauchy_random_tool_t        cauchy;
        chisq_random_tool_t         chisq;
        lognormal_random_tool_t     lognormal;
        rayleigh_random_tool_t      rayleigh;
        rayleigh_tail_random_tool_t rayleigh_tail;
        fdist_random_tool_t         fdist;
        tdist_random_tool_t         tdist;
        logarithmic_random_tool_t   logarithmic;
        logistic_random_tool_t      logistic;
        levy_random_tool_t          levy;
        pareto_random_tool_t        pareto;
        gamma_random_tool_t         gamma;
        geometric_random_tool_t     geometric;
        gumbel_random_tool_t        gumbel;
        laplace_random_tool_t       laplace;
        gaussian_tail_random_tool_t gaussian_tail;
        weibull_random_tool_t       weibull;
/*         patterned_random_tool_t   patterned; */
} random_tool_t;

typedef struct {
	random_tool_t param;
	WorkbookControl *wbc;
	gint n_vars;
	gint count;
	random_distribution_t distribution;
} tools_data_random_t;

gboolean tool_random_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
			     analysis_tool_engine_t selector, gpointer result);

#endif
