/*
 * Documentation for those functions where inline documentation is
 * not appropriate, for example because the source code was imported
 * from elsewhere.
 */

/* ------------------------------------------------------------------------- */

/**
 * log1pmx:
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
 * Returns: The result of (1+@x)^@y with less rounding error than the
 * naive formula.
 */


/**
 * pow1pm1:
 * @x: a number
 * @y: a number
 *
 * Returns: The result of (1+@x)^@y-1 with less rounding error than the
 * naive formula.
 */

/* ------------------------------------------------------------------------- */

/**
 * dnorm:
 * @x: observation
 * @mu: mean of the distribution
 * @sigma: standard deviation of the distribution
 * @give_log: if %TRUE, log of the result will be returned instead
 *
 * Returns: density of the normal distribution.
 */

/**
 * pnorm:
 * @x: observation
 * @mu: mean of the distribution
 * @sigma: standard deviation of the distribution
 * @lower_tail: if %TRUE, the lower tail of the distribution is considered.
 * @log_p: if %TRUE, log of the result will be returned instead
 *
 * Returns: cumulative density of the normal distribution.
 */

/**
 * qnorm:
 * @p: probability
 * @mu: mean of the distribution
 * @sigma: standard deviation of the distribution
 * @lower_tail: if %TRUE, the lower tail of the distribution is considered.
 * @log_p: if %TRUE, @p is given as log probability
 *
 * Returns: the observation with cumulative probability @p for the
 * normal distribution.
 */

/* ------------------------------------------------------------------------- */

/**
 * dlnorm:
 * @x: observation
 * @logmean: mean of the underlying normal distribution
 * @logsd: standard deviation of the underlying normal distribution
 * @give_log: if %TRUE, log of the result will be returned instead
 *
 * Returns: density of the normal distribution.
 */

/**
 * plnorm:
 * @x: observation
 * @logmean: mean of the underlying normal distribution
 * @logsd: standard deviation of the underlying normal distribution
 * @lower_tail: if %TRUE, the lower tail of the distribution is considered.
 * @log_p: if %TRUE, log of the result will be returned instead
 *
 * Returns: cumulative density of the normal distribution.
 */

/**
 * qlnorm:
 * @p: probability
 * @logmean: mean of the underlying normal distribution
 * @logsd: standard deviation of the underlying normal distribution
 * @lower_tail: if %TRUE, the lower tail of the distribution is considered.
 * @log_p: if %TRUE, @p is given as log probability
 *
 * Returns: the observation with cumulative probability @p for the
 * log normal distribution.
 */

/* ------------------------------------------------------------------------- */

/**
 * dgamma:
 * @x: observation
 * @shape: the shape parameter of the distribution
 * @scale: the scale parameter of the distribution
 * @give_log: if %TRUE, log of the result will be returned instead
 *
 * Returns: density of the gamma distribution.
 */

/**
 * pgamma:
 * @x: observation
 * @shape: the shape parameter of the distribution
 * @scale: the scale parameter of the distribution
 * @lower_tail: if %TRUE, the lower tail of the distribution is considered.
 * @log_p: if %TRUE, log of the result will be returned instead
 *
 * Returns: cumulative density of the gamma distribution.
 */

/**
 * qgamma:
 * @p: probability
 * @shape: the shape parameter of the distribution
 * @scale: the scale parameter of the distribution
 * @lower_tail: if %TRUE, the lower tail of the distribution is considered.
 * @log_p: if %TRUE, @p is given as log probability
 *
 * Returns: the observation with cumulative probability @p for the
 * gamma distribution.
 */

/* ------------------------------------------------------------------------- */
