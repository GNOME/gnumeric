#ifndef GNUMERIC_RANDOM_GENERATOR_H
#define GNUMERIC_RANDOM_GENERATOR_H

#include "gnumeric.h"
#include "numbers.h"
#include "dao.h"
#include "tools.h"


typedef enum {
  DiscreteDistribution, UniformDistribution, NormalDistribution,
  BernoulliDistribution, BinomialDistribution, PoissonDistribution,
  PatternedDistribution, NegativeBinomialDistribution, ExponentialDistribution
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
        gnum_float from, to;
        gnum_float step;
        int     repeat_number;
        int     repeat_sequence;
} patterned_random_tool_t;

typedef union {
        discrete_random_tool_t    discrete;
        uniform_random_tool_t     uniform;
        normal_random_tool_t      normal;
        bernoulli_random_tool_t   bernoulli;
        binomial_random_tool_t    binomial;
        negbinom_random_tool_t    negbinom;
        poisson_random_tool_t     poisson;
        exponential_random_tool_t exponential;
        patterned_random_tool_t   patterned;
} random_tool_t;

int random_tool           (WorkbookControl *context, Sheet *sheet,
			   int vars, int count, random_distribution_t distribution,
			   random_tool_t *param, data_analysis_output_t *dao);


#endif
