#ifndef GNUMERIC_GOAL_SEEK_H
#define GNUMERIC_GOAL_SEEK_H

#include "numbers.h"
#include <glib.h>

typedef struct {
	float_t xmin;           /* Minimum allowed value for x.  */
	float_t xmax;           /* Maximum allowed value for x.  */
	float_t precision;      /* Desired relative precision.  */

	gboolean havexpos;      /* Do we have a valid xpos?  */
	float_t xpos;           /* Value for which f(xpos) > 0.  */
	float_t ypos;           /* f(xpos).  */

	gboolean havexneg;      /* Do we have a valid xneg?  */
	float_t xneg;           /* Value for which f(xneg) < 0.  */
	float_t yneg;           /* f(xneg).  */

	float_t root;           /* Value for which f(root) == 0.  */
} GoalSeekData;

typedef enum { GOAL_SEEK_OK, GOAL_SEEK_ERROR } GoalSeekStatus;

typedef GoalSeekStatus (*GoalSeekFunction) (float_t x, float_t *y, void *user_data);

GoalSeekStatus goal_seek_newton (GoalSeekFunction f,
				 GoalSeekFunction df,
				 GoalSeekData *data,
				 void *user_data,
				 float_t x0);

GoalSeekStatus goal_seek_bisection (GoalSeekFunction f,
				    GoalSeekData *data,
				    void *user_data);

GoalSeekStatus goal_seek_trawl_uniformly (GoalSeekFunction f,
					  GoalSeekData *data,
					  void *user_data,
					  float_t xmin, float_t xmax,
					  int points);

GoalSeekStatus goal_seek_trawl_normally (GoalSeekFunction f,
					 GoalSeekData *data,
					 void *user_data,
					 float_t mu, float_t sigma,
					 int points);

#endif
