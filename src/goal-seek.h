#ifndef GNUMERIC_GOAL_SEEK_H
#define GNUMERIC_GOAL_SEEK_H

#include "numbers.h"
#include <glib.h>

typedef struct {
	gnum_float xmin;           /* Minimum allowed value for x.  */
	gnum_float xmax;           /* Maximum allowed value for x.  */
	gnum_float precision;      /* Desired relative precision.  */

	gboolean havexpos;         /* Do we have a valid xpos?  */
	gnum_float xpos;           /* Value for which f(xpos) > 0.  */
	gnum_float ypos;           /* f(xpos).  */

	gboolean havexneg;         /* Do we have a valid xneg?  */
	gnum_float xneg;           /* Value for which f(xneg) < 0.  */
	gnum_float yneg;           /* f(xneg).  */

	gnum_float root;           /* Value for which f(root) == 0.  */
} GoalSeekData;

typedef enum { GOAL_SEEK_OK, GOAL_SEEK_ERROR } GoalSeekStatus;

typedef GoalSeekStatus (*GoalSeekFunction) (gnum_float x, gnum_float *y, void *user_data);

void goal_seek_initialise (GoalSeekData *data);

GoalSeekStatus goal_seek_point (GoalSeekFunction f,
				GoalSeekData *data,
				void *user_data,
				gnum_float x0);

GoalSeekStatus goal_seek_newton (GoalSeekFunction f,
				 GoalSeekFunction df,
				 GoalSeekData *data,
				 void *user_data,
				 gnum_float x0);

GoalSeekStatus goal_seek_bisection (GoalSeekFunction f,
				    GoalSeekData *data,
				    void *user_data);

GoalSeekStatus goal_seek_trawl_uniformly (GoalSeekFunction f,
					  GoalSeekData *data,
					  void *user_data,
					  gnum_float xmin, gnum_float xmax,
					  int points);

GoalSeekStatus goal_seek_trawl_normally (GoalSeekFunction f,
					 GoalSeekData *data,
					 void *user_data,
					 gnum_float mu, gnum_float sigma,
					 int points);

#endif
