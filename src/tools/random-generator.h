#ifndef GNUMERIC_RANDOM_GENERATOR_H
#define GNUMERIC_RANDOM_GENERATOR_H

#include "gnumeric.h"
#include "numbers.h"
#include "dao.h"
#include "tools.h"


typedef enum {
  DiscreteDistribution, UniformDistribution, NormalDistribution,
  BernoulliDistribution, BetaDistribution, BinomialDistribution,
  PoissonDistribution, CauchyDistribution, ChisqDistribution,
  WeibullDistribution, FdistDistribution,
  /* PatternedDistribution, */ NegativeBinomialDistribution, ExponentialDistribution
} random_distribution_t;

typedef struct {
	Value *range;
} discrete_random_tool_t;

typedef struct {
        gnum_float lower_limit;
        gnum_float upper_limit;
} uniform_random_tool_t;

typedef struct {
        gnum_float mean;
        gnum_float stdev;
} normal_random_tool_t;

typedef struct {
        gnum_float p;
} bernoulli_random_tool_t;

typedef struct {
        gnum_float a;
        gnum_float b;
} beta_random_tool_t;

typedef struct {
        gnum_float p;
        int     trials;
} binomial_random_tool_t;

typedef struct {
        gnum_float p;
        int     f;
} negbinom_random_tool_t;

typedef struct {
        gnum_float lambda;
} poisson_random_tool_t;

typedef struct {
        gnum_float b;
} exponential_random_tool_t;

typedef struct {
        gnum_float a;
} cauchy_random_tool_t;

typedef struct {
        gnum_float nu;
} chisq_random_tool_t;

typedef struct {
        gnum_float nu1;
        gnum_float nu2;
} fdist_random_tool_t;

typedef struct {
        gnum_float a;
        gnum_float b;
} weibull_random_tool_t;

/* typedef struct { */
/*         gnum_float from, to; */
/*         gnum_float step; */
/*         int     repeat_number; */
/*         int     repeat_sequence; */
/* } patterned_random_tool_t; */

typedef union {
        discrete_random_tool_t    discrete;
        uniform_random_tool_t     uniform;
        normal_random_tool_t      normal;
        bernoulli_random_tool_t   bernoulli;
        beta_random_tool_t        beta;
        binomial_random_tool_t    binomial;
        negbinom_random_tool_t    negbinom;
        poisson_random_tool_t     poisson;
        exponential_random_tool_t exponential;
        cauchy_random_tool_t      cauchy;
        chisq_random_tool_t       chisq;
        fdist_random_tool_t       fdist;
        weibull_random_tool_t     weibull;
/*         patterned_random_tool_t   patterned; */
} random_tool_t;

typedef struct {
	random_tool_t param;
	WorkbookControlGUI *wbcg;       
	gint n_vars;
	gint count;
	random_distribution_t distribution;
} tools_data_random_t;

gboolean tool_random_engine (data_analysis_output_t *dao, gpointer specs, 
			     analysis_tool_engine_t selector, gpointer result);

#endif
