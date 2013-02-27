/*
 * Documentation for those functions where inline documentation is
 * not appropriate, for example because the source code was imported
 * from elsewhere.
 */

/* ------------------------------------------------------------------------- */

/**
 * log1pmx
 * @x: a number
 *
 * Returns: log(1+@x)-@x with less rounding error than the naive formula,
 * especially for small values of @x.
 */


/**
 * pow1p:
 * @x: a number
 * @y: a number
 *
 * Returns: (1+@x)^@y with less rounding error than the naive formula.
 */


/**
 * pow1pm1:
 * @x: a number
 * @y: a number
 *
 * Returns: (1+@x)^@y-1 with less rounding error than the naive formula.
 */

/* ------------------------------------------------------------------------- */

/**
 * dnorm: the density function of the normal distribution
 * @x: observation
 * @mu: mean of the distribution
 * @sigma: standard deviation of the distribution
 * @give_log: if %TRUE, log of the result will be returned instead
 *
 * Returns: density of the normal distribution
 */

/**
 * pnorm: the cumulative density function of the normal distribution
 * @x: observation
 * @mu: mean of the distribution
 * @sigma: standard deviation of the distribution
 * @lower_tail: if %TRUE, the lower tail of the distribution is considered.
 * @log_p: if %TRUE, log of the result will be returned instead
 *
 * Returns: cumulative density of the normal distribution
 */

/**
 * qnorm: the probability quantile function of the normal distribution
 * @p: probability
 * @mu: mean of the distribution
 * @sigma: standard deviation of the distribution
 * @lower_tail: if %TRUE, the lower tail of the distribution is considered.
 * @log_p: if %TRUE, @p is given as log probability
 *
 * Returns: the observation with cumulative probability @p.
 */

/* ------------------------------------------------------------------------- */
